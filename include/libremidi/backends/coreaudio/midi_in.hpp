#pragma once
#include <libremidi/backends/coreaudio/config.hpp>
#include <libremidi/detail/midi_in.hpp>

namespace libremidi
{
class midi_in_core final
    : public midi_in_api
    , private coremidi_data
{
public:
  explicit midi_in_core(std::string_view clientName)
      : midi_in_api{}
  {
    // Set up our client.
    MIDIClientRef client{};
    CFStringRef name
        = CFStringCreateWithCString(nullptr, clientName.data(), kCFStringEncodingASCII);
    OSStatus result = MIDIClientCreate(name, nullptr, nullptr, &client);
    if (result != noErr)
    {
      error<driver_error>(
          "midi_in_core::initialize: error creating OS-X MIDI client object: "
          + std::to_string(result));
      return;
    }

    // Save our api-specific connection information.
    this->client = client;
    this->endpoint = 0;
    CFRelease(name);
  }

  ~midi_in_core() override
  {
    // Close a connection if it exists.
    midi_in_core::close_port();

    // Cleanup.
    MIDIClientDispose(this->client);
    if (this->endpoint)
      MIDIEndpointDispose(this->endpoint);
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::MACOSX_CORE; }

  void open_port(unsigned int portNumber, std::string_view portName) override
  {
    if (connected_)
    {
      warning("midi_in_core::open_port: a valid connection already exists!");
      return;
    }

    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);
    unsigned int nSrc = MIDIGetNumberOfSources();
    if (nSrc < 1)
    {
      error<no_devices_found_error>("midi_in_core::open_port: no MIDI input sources found!");
      return;
    }

    if (portNumber >= nSrc)
    {
      error<invalid_parameter_error>(
          "midi_in_core::open_port: invalid 'portNumber' argument: " + std::to_string(portNumber));
      return;
    }

    MIDIPortRef port;
    CFStringRef portNameRef
        = CFStringCreateWithCString(nullptr, portName.data(), kCFStringEncodingASCII);
    OSStatus result
        = MIDIInputPortCreate(this->client, portNameRef, midiInputCallback, (void*)this, &port);
    CFRelease(portNameRef);

    if (result != noErr)
    {
      MIDIClientDispose(this->client);
      error<driver_error>("midi_in_core::open_port: error creating OS-X MIDI input port.");
      return;
    }

    // Get the desired input source identifier.
    MIDIEndpointRef endpoint = MIDIGetSource(portNumber);
    if (endpoint == 0)
    {
      MIDIPortDispose(port);
      MIDIClientDispose(this->client);
      error<driver_error>("midi_in_core::open_port: error getting MIDI input source reference.");
      return;
    }

    // Make the connection.
    result = MIDIPortConnectSource(port, endpoint, nullptr);
    if (result != noErr)
    {
      MIDIPortDispose(port);
      MIDIClientDispose(this->client);
      error<driver_error>("midi_in_core::open_port: error connecting OS-X MIDI input port.");
      return;
    }

    // Save our api-specific port information.
    this->port = port;

    connected_ = true;
  }
  void open_virtual_port(std::string_view portName) override
  {
    // Create a virtual MIDI input destination.
    MIDIEndpointRef endpoint;
    CFStringRef portNameRef
        = CFStringCreateWithCString(nullptr, portName.data(), kCFStringEncodingASCII);
    OSStatus result = MIDIDestinationCreate(
        this->client, portNameRef, midiInputCallback, (void*)this, &endpoint);
    CFRelease(portNameRef);

    if (result != noErr)
    {
      error<driver_error>(
          "midi_in_core::open_virtual_port: error creating virtual OS-X MIDI "
          "destination.");
      return;
    }

    // Save our api-specific connection information.
    this->endpoint = endpoint;
  }
  void close_port() override
  {
    if (this->endpoint)
    {
      MIDIEndpointDispose(this->endpoint);
      this->endpoint = 0;
    }

    if (this->port)
    {
      MIDIPortDispose(this->port);
      this->port = 0;
    }

    connected_ = false;
  }
  void set_client_name(std::string_view clientName) override
  {
    warning(
        "midi_in_core::setClientName: this function is not implemented for the "
        "MACOSX_CORE API!");
  }
  void set_port_name(std::string_view portName) override
  {
    warning(
        "midi_in_core::setPortName: this function is not implemented for the "
        "MACOSX_CORE API!");
  }
  unsigned int get_port_count() const override
  {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);
    return MIDIGetNumberOfSources();
  }
  std::string get_port_name(unsigned int portNumber) const override
  {
    CFStringRef nameRef;
    MIDIEndpointRef portRef;
    char name[128];

    std::string stringName;
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);
    if (portNumber >= MIDIGetNumberOfSources())
    {
      error<invalid_parameter_error>(
          "midi_in_core::get_port_name: invalid 'portNumber' argument: "
          + std::to_string(portNumber));
      return {};
    }

    portRef = MIDIGetSource(portNumber);
    nameRef = ConnectedEndpointName(portRef);
    CFStringGetCString(nameRef, name, sizeof(name), kCFStringEncodingUTF8);
    CFRelease(nameRef);

    return stringName = name;
  }

private:
  static void midiInputCallback(const MIDIPacketList* list, void* procRef, void* /*srcRef*/)
  {
    auto& self = *(midi_in_core*)procRef;
    auto& data = self;

    unsigned char status{};
    unsigned short nBytes{}, iByte{}, size{};
    unsigned long long time{};

    bool& continueSysex = data.continueSysex;
    message& msg = data.message;

    const MIDIPacket* packet = &list->packet[0];
    for (unsigned int i = 0; i < list->numPackets; ++i)
    {

      // My interpretation of the CoreMIDI documentation: all message
      // types, except sysex, are complete within a packet and there may
      // be several of them in a single packet.  Sysex messages can be
      // broken across multiple packets and PacketLists but are bundled
      // alone within each packet (these packets do not contain other
      // message types).  If sysex messages are split across multiple
      // MIDIPacketLists, they must be handled by multiple calls to this
      // function.

      nBytes = packet->length;
      if (nBytes == 0)
      {
        packet = MIDIPacketNext(packet);
        continue;
      }

      // Calculate time stamp.
      if (data.firstMessage)
      {
        data.firstMessage = false;
        msg.timestamp = 0;
      }
      else
      {
        time = packet->timeStamp;
        if (time == 0)
        { // this happens when receiving asynchronous sysex messages
          time = AudioGetCurrentHostTime();
        }
        time -= self.lastTime;
        time = AudioConvertHostTimeToNanos(time);
        if (!continueSysex)
          msg.timestamp = time * 0.000000001;
      }

      // Track whether any non-filtered messages were found in this
      // packet for timestamp calculation
      bool foundNonFiltered = false;

      iByte = 0;
      if (continueSysex)
      {
        // We have a continuing, segmented sysex message.
        if (!(data.ignoreFlags & 0x01))
        {
          // If we're not ignoring sysex messages, copy the entire packet.
          for (unsigned int j = 0; j < nBytes; ++j)
            msg.bytes.push_back(packet->data[j]);
        }
        continueSysex = packet->data[nBytes - 1] != 0xF7;

        if (!(data.ignoreFlags & 0x01) && !continueSysex)
        {
          // If not a continuing sysex message, invoke the user callback
          // function or queue the message.
          data.on_message_received(std::move(msg));
        }
      }
      else
      {
        while (iByte < nBytes)
        {
          size = 0;
          // We are expecting that the next byte in the packet is a status
          // byte.
          status = packet->data[iByte];
          if (!(status & 0x80))
            break;
          // Determine the number of bytes in the MIDI message.
          if (status < 0xC0)
            size = 3;
          else if (status < 0xE0)
            size = 2;
          else if (status < 0xF0)
            size = 3;
          else if (status == 0xF0)
          {
            // A MIDI sysex
            if (data.ignoreFlags & 0x01)
            {
              size = 0;
              iByte = nBytes;
            }
            else
              size = nBytes - iByte;
            continueSysex = packet->data[nBytes - 1] != 0xF7;
          }
          else if (status == 0xF1)
          {
            // A MIDI time code message
            if (data.ignoreFlags & 0x02)
            {
              size = 0;
              iByte += 2;
            }
            else
              size = 2;
          }
          else if (status == 0xF2)
            size = 3;
          else if (status == 0xF3)
            size = 2;
          else if (status == 0xF8 && (data.ignoreFlags & 0x02))
          {
            // A MIDI timing tick message and we're ignoring it.
            size = 0;
            iByte += 1;
          }
          else if (status == 0xFE && (data.ignoreFlags & 0x04))
          {
            // A MIDI active sensing message and we're ignoring it.
            size = 0;
            iByte += 1;
          }
          else
            size = 1;

          // Copy the MIDI data to our vector.
          if (size)
          {
            foundNonFiltered = true;
            msg.bytes.assign(&packet->data[iByte], &packet->data[iByte + size]);
            if (!continueSysex)
            {
              // If not a continuing sysex message, invoke the user callback
              // function or queue the message.
              data.on_message_received(std::move(msg));
            }
            iByte += size;
          }
        }
      }

      // Save the time of the last non-filtered message
      if (foundNonFiltered)
      {
        self.lastTime = packet->timeStamp;
        if (self.lastTime == 0)
        { // this happens when receiving asynchronous sysex messages
          self.lastTime = AudioGetCurrentHostTime();
        }
      }

      packet = MIDIPacketNext(packet);
    }
  }
};

}
