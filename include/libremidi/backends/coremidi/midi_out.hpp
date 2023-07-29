#pragma once
#include <libremidi/backends/coremidi/config.hpp>
#include <libremidi/backends/coremidi/helpers.hpp>
#include <libremidi/detail/midi_out.hpp>

namespace libremidi
{
class midi_out_core final
    : public midi1::out_api
    , private coremidi_data
    , public error_handler
{
public:
  struct
      : output_configuration
      , coremidi_output_configuration
  {
  } configuration;

  midi_out_core(output_configuration&& conf, coremidi_output_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    if (auto result = init_client(configuration); result != noErr)
    {
      error<driver_error>(
          this->configuration,
          "midi_out_core: error creating MIDI client object: " + std::to_string(result));
      return;
    }
  }

  ~midi_out_core()
  {
    midi_out_core::close_port();

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
    warning(configuration, "midi_out_core: set_client_name unsupported");
  }
  void set_port_name(std::string_view) override
  {
    warning(configuration, "midi_out_core: set_port_name unsupported");
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::COREMIDI; }

  bool open_port(const output_port& info, std::string_view portName) override
  {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);

    // Find where we want to send
    auto destination = locate_object(*this, info, kMIDIObjectType_Destination);
    if (destination == 0)
      return false;

    // Create our local source
    MIDIPortRef port;
    OSStatus result = MIDIOutputPortCreate(this->client, toCFString(portName).get(), &port);
    if (result != noErr)
    {
      close_client();
      error<driver_error>(
          this->configuration, "midi_out_core::open_port: error creating macOS MIDI output port.");
      return false;
    }

    // Save our api-specific connection information.
    this->port = port;
    this->destinationId = destination;

    return true;
  }

  bool open_virtual_port(std::string_view portName) override
  {
    if (this->endpoint)
    {
      warning(
          configuration,
          "midi_out_core::open_virtual_port: a virtual output port already "
          "exists!");
      return false;
    }

    // Create a virtual MIDI output source.
    MIDIEndpointRef endpoint;
    OSStatus result = MIDISourceCreate(this->client, toCFString(portName).get(), &endpoint);

    if (result != noErr)
    {
      error<driver_error>(
          this->configuration,
          "midi_out_core::initialize: error creating macOS virtual MIDI source.");
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

  void send_message(const unsigned char* message, size_t size) override
  {
    unsigned int nBytes = static_cast<unsigned int>(size);
    if (nBytes == 0)
    {
      warning(configuration, "midi_out_core::send_message: no data in message argument!");
      return;
    }

    if (message[0] != 0xF0 && nBytes > 3)
    {
      warning(
          configuration,
          "midi_out_core::send_message: message format problem ... not sysex but "
          "> 3 bytes?");
      return;
    }

    const MIDITimeStamp timestamp = AudioGetCurrentHostTime();

    const ByteCount bufsize = nBytes > 65535 ? 65535 : nBytes;
    Byte buffer[bufsize + 16]; // pad for other struct members
    ByteCount listSize = sizeof(buffer);
    MIDIPacketList* packetList = (MIDIPacketList*)buffer;

    ByteCount remainingBytes = nBytes;
    while (remainingBytes)
    {
      MIDIPacket* packet = MIDIPacketListInit(packetList);
      // A MIDIPacketList can only contain a maximum of 64K of data, so if our message is longer,
      // break it up into chunks of 64K or less and send out as a MIDIPacketList with only one
      // MIDIPacket. Here, we reuse the memory allocated above on the stack for all.
      ByteCount bytesForPacket = remainingBytes > 65535 ? 65535 : remainingBytes;
      const Byte* dataStartPtr = (const Byte*)&message[nBytes - remainingBytes];
      packet = MIDIPacketListAdd(
          packetList, listSize, packet, timestamp, bytesForPacket, dataStartPtr);
      remainingBytes -= bytesForPacket;

      if (!packet)
      {
        error<driver_error>(
            this->configuration, "midi_out_core::send_message: could not allocate packet list");
        return;
      }

      // Send to any destinations that may have connected to us.
      if (this->endpoint)
      {
        auto result = MIDIReceived(this->endpoint, packetList);
        if (result != noErr)
        {
          warning(
              this->configuration,
              "midi_out_core::send_message: error sending MIDI to virtual "
              "destinations.");
        }
      }

      // And send to an explicit destination port if we're connected.
      if (this->destinationId != 0)
      {
        auto result = MIDISend(this->port, this->destinationId, packetList);
        if (result != noErr)
        {
          warning(
              this->configuration,
              "midi_out_core::send_message: error sending MIDI message to port.");
        }
      }
    }
  }

  MIDIEndpointRef destinationId{};
};
}
