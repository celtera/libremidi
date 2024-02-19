#pragma once
#include <libremidi/backends/coremidi/config.hpp>
#include <libremidi/backends/coremidi/helpers.hpp>
#include <libremidi/detail/midi_in.hpp>
#include <libremidi/detail/midi_stream_decoder.hpp>

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

  timestamp absolute_timestamp() const noexcept override
  {
    return coremidi_data::time_in_nanos(LIBREMIDI_AUDIO_GET_CURRENT_HOST_TIME());
  }

  static void midiInputCallback(const MIDIPacketList* list, void* procRef, void* /*srcRef*/)
  {
    static constexpr timestamp_backend_info timestamp_info{
        .has_absolute_timestamps = true,
        .absolute_is_monotonic = false,
        .has_samples = false,
    };

    auto& self = *(midi_in_core*)procRef;

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

      if (packet->length == 0)
      {
        packet = MIDIPacketNext(packet);
        continue;
      }

      auto to_ns = [packet] { return time_in_nanos(packet); };
      self.m_processing.on_bytes_multi(
          {packet->data, packet->data + packet->length},
          self.m_processing.timestamp<timestamp_info>(to_ns, 0));

      packet = MIDIPacketNext(packet);
    }
  }

  midi1::input_state_machine m_processing{this->configuration};
};
}
