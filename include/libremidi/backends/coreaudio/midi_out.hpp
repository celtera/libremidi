#pragma once
#include <libremidi/backends/coreaudio/config.hpp>
#include <libremidi/detail/midi_out.hpp>

namespace libremidi
{
class midi_out_core final : public midi_out_api
{
public:
  midi_out_core(std::string_view clientName)
  {
    // Set up our client.
    MIDIClientRef client;
    CFStringRef name
        = CFStringCreateWithCString(nullptr, clientName.data(), kCFStringEncodingASCII);
    OSStatus result = MIDIClientCreate(name, nullptr, nullptr, &client);
    if (result != noErr)
    {
      std::ostringstream ost;
      ost << "midi_in_core::initialize: error creating OS-X MIDI client object (" << result
          << ").";
      error<driver_error>(ost.str());
      return;
    }

    // Save our api-specific connection information.
    data.client = client;
    data.endpoint = 0;
    CFRelease(name);
  }
  ~midi_out_core()
  {
    // Close a connection if it exists.
    midi_out_core::close_port();

    // Cleanup.
    MIDIClientDispose(data.client);
    if (data.endpoint)
      MIDIEndpointDispose(data.endpoint);
  }
  libremidi::API get_current_api() const noexcept override { return libremidi::API::MACOSX_CORE; }
  void open_port(unsigned int portNumber, std::string_view portName) override
  {
    if (connected_)
    {
      warning("midi_out_core::open_port: a valid connection already exists!");
      return;
    }

    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);
    unsigned int nDest = MIDIGetNumberOfDestinations();
    if (nDest < 1)
    {
      error<no_devices_found_error>(
          "midi_out_core::open_port: no MIDI output destinations found!");
      return;
    }

    if (portNumber >= nDest)
    {
      std::ostringstream ost;
      ost << "midi_out_core::open_port: the 'portNumber' argument (" << portNumber
          << ") is invalid.";
      error<invalid_parameter_error>(ost.str());
      return;
    }

    MIDIPortRef port;
    CFStringRef portNameRef
        = CFStringCreateWithCString(nullptr, portName.data(), kCFStringEncodingASCII);
    OSStatus result = MIDIOutputPortCreate(data.client, portNameRef, &port);
    CFRelease(portNameRef);
    if (result != noErr)
    {
      MIDIClientDispose(data.client);
      error<driver_error>("midi_out_core::open_port: error creating OS-X MIDI output port.");
      return;
    }

    // Get the desired output port identifier.
    MIDIEndpointRef destination = MIDIGetDestination(portNumber);
    if (destination == 0)
    {
      MIDIPortDispose(port);
      MIDIClientDispose(data.client);
      error<driver_error>(
          "midi_out_core::open_port: error getting MIDI output destination "
          "reference.");
      return;
    }

    // Save our api-specific connection information.
    data.port = port;
    data.destinationId = destination;
    connected_ = true;
  }
  void open_virtual_port(std::string_view portName) override
  {
    if (data.endpoint)
    {
      warning(
          "midi_out_core::open_virtual_port: a virtual output port already "
          "exists!");
      return;
    }

    // Create a virtual MIDI output source.
    MIDIEndpointRef endpoint;
    CFStringRef portNameRef
        = CFStringCreateWithCString(nullptr, portName.data(), kCFStringEncodingASCII);
    OSStatus result = MIDISourceCreate(data.client, portNameRef, &endpoint);
    CFRelease(portNameRef);

    if (result != noErr)
    {
      error<driver_error>("midi_out_core::initialize: error creating OS-X virtual MIDI source.");
      return;
    }

    // Save our api-specific connection information.
    data.endpoint = endpoint;
  }
  void close_port() override
  {
    if (data.endpoint)
    {
      MIDIEndpointDispose(data.endpoint);
      data.endpoint = 0;
    }

    if (data.port)
    {
      MIDIPortDispose(data.port);
      data.port = 0;
    }

    connected_ = false;
  }
  void set_client_name(std::string_view clientName) override
  {
    warning(
        "midi_out_core::set_client_name: this function is not implemented for "
        "the "
        "MACOSX_CORE API!");
  }
  void set_port_name(std::string_view portName) override
  {
    warning(
        "midi_out_core::set_port_name: this function is not implemented for the "
        "MACOSX_CORE API!");
  }
  unsigned int get_port_count() override
  {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);
    return MIDIGetNumberOfDestinations();
  }
  std::string get_port_name(unsigned int portNumber) override
  {
    CFStringRef nameRef;
    MIDIEndpointRef portRef;
    char name[128];

    std::string stringName;
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);
    if (portNumber >= MIDIGetNumberOfDestinations())
    {
      std::ostringstream ost;
      ost << "midi_out_core::get_port_name: the 'portNumber' argument (" << portNumber
          << ") is invalid.";
      warning(ost.str());
      return stringName;
    }

    portRef = MIDIGetDestination(portNumber);
    nameRef = ConnectedEndpointName(portRef);
    CFStringGetCString(nameRef, name, sizeof(name), kCFStringEncodingUTF8);
    CFRelease(nameRef);

    return stringName = name;
  }
  void send_message(const unsigned char* message, size_t size) override
  {
    // We use the MIDISendSysex() function to asynchronously send sysex
    // messages.  Otherwise, we use a single CoreMidi MIDIPacket.
    unsigned int nBytes = static_cast<unsigned int>(size);
    if (nBytes == 0)
    {
      warning("midi_out_core::send_message: no data in message argument!");
      return;
    }

    MIDITimeStamp timestamp = AudioGetCurrentHostTime();
    OSStatus result;

    if (message[0] != 0xF0 && nBytes > 3)
    {
      warning(
          "midi_out_core::send_message: message format problem ... not sysex but "
          "> 3 bytes?");
      return;
    }

    ByteCount listSize = nBytes + (sizeof(MIDIPacketList));
    Byte* buffer = (Byte*)alloca(listSize);
    MIDIPacketList* packetList = (MIDIPacketList*)buffer;
    MIDIPacket* packet = MIDIPacketListInit(packetList);

    ByteCount remainingBytes = nBytes;
    while (remainingBytes && packet)
    {
      ByteCount bytesForPacket = remainingBytes > 65535
                                     ? 65535
                                     : remainingBytes; // 65535 = maximum size of a MIDIPacket
      const Byte* dataStartPtr = (const Byte*)&message[nBytes - remainingBytes];

      packet = MIDIPacketListAdd(
          packetList, listSize, packet, timestamp, bytesForPacket, dataStartPtr);
      remainingBytes -= bytesForPacket;
    }

    if (!packet)
    {
      error<driver_error>("midi_out_core::send_message: could not allocate packet list");
      return;
    }

    // Send to any destinations that may have connected to us.
    if (data.endpoint)
    {
      result = MIDIReceived(data.endpoint, packetList);
      if (result != noErr)
      {
        warning(
            "midi_out_core::send_message: error sending MIDI to virtual "
            "destinations.");
      }
    }

    // And send to an explicit destination port if we're connected.
    if (connected_)
    {
      result = MIDISend(data.port, data.destinationId, packetList);
      if (result != noErr)
      {
        warning("midi_out_core::send_message: error sending MIDI message to port.");
      }
    }
  }

private:
  coremidi_data data;
};
}
