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

  libremidi::API get_current_api() const noexcept override { return libremidi::API::COREMIDI_UMP; }

  std::error_code open_port(const input_port& info, std::string_view portName) override
  {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);

    auto source = locate_object(*this, info, kMIDIObjectType_Source);
    if (source == 0)
      return std::make_error_code(std::errc::invalid_argument);

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
      return from_osstatus(result);
    }

    // Make the connection.
    if (result = MIDIPortConnectSource(port, source, nullptr); result != noErr)
    {
      MIDIPortDispose(port);
      close_client();
      error<driver_error>(
          this->configuration, "midi_in_core::open_port: error connecting macOS MIDI input port.");
      return from_osstatus(result);
    }

    // Save our api-specific port information.
    this->port = port;
    return std::error_code{};
  }

  std::error_code open_virtual_port(std::string_view portName) override
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
      return from_osstatus(result);
    }

    // Save our api-specific connection information.
    this->endpoint = endpoint;
    return std::error_code{};
  }

  std::error_code close_port() override
  {
    return coremidi_data::close_port();
  }

  timestamp absolute_timestamp() const noexcept override
  {
    return coremidi_data::time_in_nanos(LIBREMIDI_AUDIO_GET_CURRENT_HOST_TIME());
  }

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
        std::copy_n(packet->words, packet->wordCount, msg.data);
        configuration.on_message(std::move(msg));
      }
      last_time = time_in_nanos(packet->timeStamp);

      packet = MIDIEventPacketNext(packet);
    }
  }

  unsigned long long last_time{};
};

}
