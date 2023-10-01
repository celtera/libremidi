#pragma once
#include <libremidi/backends/coremidi_ump/config.hpp>
#include <libremidi/backends/coremidi_ump/helpers.hpp>
#include <libremidi/cmidi2.hpp>
#include <libremidi/detail/midi_out.hpp>

namespace libremidi::coremidi_ump
{
class midi_out_impl final
    : public midi2::out_api
    , private coremidi_data
    , public error_handler
{
public:
  struct
      : libremidi::output_configuration
      , coremidi_ump::output_configuration
  {
  } configuration;

  midi_out_impl(
      libremidi::output_configuration&& conf, coremidi_ump::output_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    if (auto result = init_client(configuration); result != noErr)
    {
      error<driver_error>(
          this->configuration,
          "midi_out_impl: error creating MIDI client object: " + std::to_string(result));
      return;
    }
  }

  ~midi_out_impl()
  {
    midi_out_impl::close_port();

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
    warning(configuration, "midi_out_impl: set_client_name unsupported");
  }
  void set_port_name(std::string_view) override
  {
    warning(configuration, "midi_out_impl: set_port_name unsupported");
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::COREMIDI_UMP; }

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
          this->configuration, "midi_out_impl::open_port: error creating macOS MIDI output port.");
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
          "midi_out_impl::open_virtual_port: a virtual output port already "
          "exists!");
      return false;
    }

    // Create a virtual MIDI output source.
    OSStatus result = MIDISourceCreateWithProtocol(
        this->client, toCFString(portName).get(), kMIDIProtocol_2_0, &this->endpoint);

    if (result != noErr)
    {
      this->endpoint = 0;
      error<driver_error>(
          this->configuration,
          "midi_out_impl::initialize: error creating macOS virtual MIDI source.");
      return false;
    }

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

  void send_ump(const uint32_t* ump, size_t size) override
  {
    if (size >= 65535 / sizeof(uint32_t))
    {
      // FIXME break it up in multiple umps...
      return;
    }

    MIDIEventList eventList = {};
    MIDIEventPacket* packet = MIDIEventListInit(&eventList, kMIDIProtocol_2_0);
    MIDITimeStamp ts = AudioGetCurrentHostTime();
    const auto sz = cmidi2_ump_get_num_bytes(ump[0]) / 4;

    // And send to an explicit destination port if we're connected.
    packet = MIDIEventListAdd(&eventList, sizeof(MIDIEventList), packet, ts, sz, ump);

    if (this->endpoint)
    {
      auto result = MIDIReceivedEventList(this->endpoint, &eventList);
      if (result != noErr)
      {
        warning(
            this->configuration,
            "midi_out_core::send_message: error sending MIDI to virtual "
            "destinations.");
      }
    }

    if (this->destinationId != 0)
    {
      auto result = MIDISendEventList(this->port, this->destinationId, &eventList);
      if (result != noErr)
      {
        warning(
            this->configuration,
            "midi_out_core::send_message: error sending MIDI message to port.");
      }
    }
  }

  MIDIEndpointRef destinationId{};
};
}
