#pragma once
#include <libremidi/backends/coreaudio/config.hpp>
#include <libremidi/backends/coreaudio/helpers.hpp>
#include <libremidi/detail/midi_out.hpp>

namespace libremidi
{
class midi_out_core final
    : public midi_out_default<midi_out_core>
    , private coremidi_data
{
public:
  static const constexpr auto backend = "CoreMIDI";

  struct
      : output_configuration
      , coremidi_output_configuration
  {
  } configuration;

  midi_out_core(output_configuration&& conf, coremidi_output_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    // Set up our client.
    MIDIClientRef client;
    OSStatus result = MIDIClientCreate(toCFString(configuration.client_name).get(), nullptr, nullptr, &client);
    if (result != noErr)
    {
      error<driver_error>(
          "midi_out_core::initialize: error creating OS-X MIDI client object: "
          + std::to_string(result));
      return;
    }

    // Save our api-specific connection information.
    this->client = client;
    this->endpoint = 0;
  }
  ~midi_out_core()
  {
    // Close a connection if it exists.
    midi_out_core::close_port();

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
      error<invalid_parameter_error>(
          "midi_out_core::open_port: invalid 'portNumber' argument: "
          + std::to_string(portNumber));
      return;
    }

    MIDIPortRef port;
    OSStatus result = MIDIOutputPortCreate(this->client, toCFString(portName).get(), &port);
    if (result != noErr)
    {
      MIDIClientDispose(this->client);
      error<driver_error>("midi_out_core::open_port: error creating OS-X MIDI output port.");
      return;
    }

    // Get the desired output port identifier.
    MIDIEndpointRef destination = MIDIGetDestination(portNumber);
    if (destination == 0)
    {
      MIDIPortDispose(port);
      MIDIClientDispose(this->client);
      error<driver_error>(
          "midi_out_core::open_port: error getting MIDI output destination "
          "reference.");
      return;
    }

    // Save our api-specific connection information.
    this->port = port;
    this->destinationId = destination;
    connected_ = true;
  }

  void open_virtual_port(std::string_view portName) override
  {
    if (this->endpoint)
    {
      warning(
          "midi_out_core::open_virtual_port: a virtual output port already "
          "exists!");
      return;
    }

    // Create a virtual MIDI output source.
    MIDIEndpointRef endpoint;
    OSStatus result = MIDISourceCreate(this->client, toCFString(portName).get(), &endpoint);

    if (result != noErr)
    {
      error<driver_error>("midi_out_core::initialize: error creating OS-X virtual MIDI source.");
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

  unsigned int get_port_count() const override
  {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);
    return MIDIGetNumberOfDestinations();
  }

  std::string get_port_name(unsigned int portNumber) const override
  {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);
    if (portNumber >= MIDIGetNumberOfDestinations())
    {
      error<invalid_parameter_error>(
          "midi_out_core::get_port_name: invalid 'portNumber' argument: "
          + std::to_string(portNumber));
      return {};
    }

    auto portRef = MIDIGetDestination(portNumber);
    auto nameRef = ConnectedEndpointName(portRef);

    char name[128];
    CFStringGetCString(nameRef, name, sizeof(name), kCFStringEncodingUTF8);
    CFRelease(nameRef);

    return name;
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
    if (this->endpoint)
    {
      result = MIDIReceived(this->endpoint, packetList);
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
      result = MIDISend(this->port, this->destinationId, packetList);
      if (result != noErr)
      {
        warning("midi_out_core::send_message: error sending MIDI message to port.");
      }
    }
  }

  MIDIEndpointRef destinationId{};
};
}
