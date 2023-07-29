#pragma once
#include <libremidi/backends/coremidi/config.hpp>
#include <libremidi/backends/coremidi/helpers.hpp>
#include <libremidi/detail/midi_in.hpp>

namespace libremidi
{
class midi_in_core final
    : public midi1::in_api
    , private coremidi_data
    , public error_handler
{
public:
  struct
      : input_configuration
      , coremidi_input_configuration
  {
  } configuration;

  midi_in_core(input_configuration&& conf, coremidi_input_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    if (auto result = init_client(configuration); result != noErr)
    {
      error<driver_error>(
          this->configuration,
          "midi_in_core: error creating MIDI client object: " + std::to_string(result));
      return;
    }
  }

  ~midi_in_core() override
  {
    // Close a connection if it exists.
    midi_in_core::close_port();

    if (this->endpoint)
      MIDIEndpointDispose(this->endpoint);

    close_client();
  }

  void close_client()
  {
    if (!configuration.context)
      MIDIClientDispose(this->client);
  }

  void set_client_name(std::string_view) override
  {
    warning(configuration, "midi_in_core: set_client_name unsupported");
  }
  void set_port_name(std::string_view) override
  {
    warning(configuration, "midi_in_core: set_port_name unsupported");
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::COREMIDI; }

  bool open_port(const input_port& info, std::string_view portName) override
  {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);

    auto source = locate_object(*this, info, kMIDIObjectType_Source);
    if (source == 0)
      return false;

    // Create our local sink
    MIDIPortRef port;
    OSStatus result = MIDIInputPortCreate(
        this->client, toCFString(portName).get(), midiInputCallback, (void*)this, &port);

    if (result != noErr)
    {
      close_client();
      error<driver_error>(
          this->configuration, "midi_in_core::open_port: error creating macOS MIDI input port: "
                                   + std::to_string(result));
      return false;
    }

    // Make the connection.
    if (result = MIDIPortConnectSource(port, source, nullptr); result != noErr)
    {
      MIDIPortDispose(port);
      close_client();
      error<driver_error>(
          this->configuration, "midi_in_core::open_port: error connecting macOS MIDI input port.");
      return false;
    }

    // Save our api-specific port information.
    this->port = port;
    return true;
  }

  bool open_virtual_port(std::string_view portName) override
  {
    // Create a virtual MIDI input destination.
    MIDIEndpointRef endpoint;
    OSStatus result = MIDIDestinationCreate(
        this->client, toCFString(portName).get(), midiInputCallback, (void*)this, &endpoint);

    if (result != noErr)
    {
      error<driver_error>(
          this->configuration,
          "midi_in_core::open_virtual_port: error creating virtual macOS MIDI "
          "destination.");
      return false;
    }

    // Save our api-specific connection information.
    this->endpoint = endpoint;
    return true;
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
  }

  static void midiInputCallback(const MIDIPacketList* list, void* procRef, void* /*srcRef*/)
  {
    auto& self = *(midi_in_core*)procRef;

    unsigned char status{};
    unsigned short nBytes{}, iByte{}, size{};

    bool& continueSysex = self.continueSysex;
    auto& msg = self.message;

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
      coremidi_data::set_timestamp(self, packet->timeStamp, msg.timestamp);

      // Track whether any non-filtered messages were found in this
      // packet for timestamp calculation
      bool foundNonFiltered = false;

      iByte = 0;
      if (continueSysex)
      {
        // We have a continuing, segmented sysex message.
        if (!self.configuration.ignore_sysex)
        {
          // If we're not ignoring sysex messages, copy the entire packet.
          msg.bytes.insert(msg.bytes.end(), packet->data, packet->data + nBytes);
        }
        continueSysex = packet->data[nBytes - 1] != 0xF7;

        if (!self.configuration.ignore_sysex && !continueSysex)
        {
          // If not a continuing sysex message, invoke the user callback
          // function or queue the message.
          self.configuration.on_message(std::move(msg));
          msg.clear();
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
            if (self.configuration.ignore_sysex)
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
            if (self.configuration.ignore_timing)
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
          else if (status == 0xF8 && (self.configuration.ignore_timing))
          {
            // A MIDI timing tick message and we're ignoring it.
            size = 0;
            iByte += 1;
          }
          else if (status == 0xFE && (self.configuration.ignore_sensing))
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
              self.configuration.on_message(std::move(msg));
              msg.clear();
            }
            iByte += size;
          }
        }
      }

      // Save the time of the last non-filtered message
      if (foundNonFiltered)
      {
        self.last_time = time_in_nanos(packet->timeStamp);
      }

      packet = MIDIPacketNext(packet);
    }
  }

  unsigned long long last_time{};
};
}
