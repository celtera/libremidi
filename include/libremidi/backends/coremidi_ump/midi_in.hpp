#pragma once
#include <libremidi/backends/coremidi_ump/config.hpp>
#include <libremidi/backends/coremidi_ump/helpers.hpp>
#include <libremidi/detail/midi_in.hpp>

namespace libremidi::coremidi_ump
{

class midi_in_impl final
    : public midi2::in_api
    , private coremidi_data
    , public error_handler
{
public:
  struct
      : ump_input_configuration
      , coremidi_ump::input_configuration
  {
  } configuration;

  midi_in_impl(ump_input_configuration&& conf, coremidi_input_configuration&& apiconf)
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

  ~midi_in_impl() override
  {
    // Close a connection if it exists.
    close_port();

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

  libremidi::API get_current_api() const noexcept override { return libremidi::API::COREMIDI_UMP; }

  bool open_port(const input_port& info, std::string_view portName) override
  {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);

    auto source = locate_object(*this, info, kMIDIObjectType_Source);
    if (source == 0)
      return false;

    // Create our local sink
    MIDIPortRef port;

    OSStatus result = MIDIInputPortCreateWithProtocol(
        this->client, toCFString(portName).get(), kMIDIProtocol_2_0, &port,
        ^(const MIDIEventList* evtlist, void* __nullable srcConnRefCon) {
            this->midiInputCallback(evtlist, srcConnRefCon);
        });

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
    OSStatus result = MIDIDestinationCreateWithProtocol(
        this->client, toCFString(portName).get(), kMIDIProtocol_2_0, &endpoint,
        ^(const MIDIEventList* evtlist, void* __nullable srcConnRefCon) {
            this->midiInputCallback(evtlist, srcConnRefCon);
        });

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

  void set_timestamp(const MIDIEventPacket& packet, libremidi::ump& msg) noexcept { }

  void midiInputCallback(const MIDIEventList* list, void* /*srcRef*/)
  {
    unsigned short nBytes{};

    const MIDIEventPacket* packet = &list->packet[0];
    for (unsigned int i = 0; i < list->numPackets; ++i)
    {
      nBytes = packet->wordCount;
      if (nBytes == 0)
      {
        packet = MIDIEventPacketNext(packet);
        continue;
      }

      libremidi::ump msg;
      coremidi_data::set_timestamp(*this, packet->timeStamp, msg.timestamp);

      if (packet->wordCount <= 4)
      {
        std::copy_n(packet->words, packet->wordCount, msg.bytes);
        configuration.on_message(std::move(msg));
      }
      last_time = time_in_nanos(packet->timeStamp);

      packet = MIDIEventPacketNext(packet);
    }
  }

  unsigned long long last_time{};
};

}
