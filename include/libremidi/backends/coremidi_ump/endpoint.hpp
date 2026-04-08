#pragma once
#include <libremidi/ump_endpoint_info.hpp>
#include <libremidi/backends/coremidi/helpers.hpp>
#include <libremidi/backends/coremidi_ump/endpoint_config.hpp>
#include <libremidi/detail/ump_endpoint_api.hpp>
#include <libremidi/detail/ump_stream.hpp>
#include <libremidi/error_handler.hpp>

#if __MAC_OS_X_VERSION_MIN_REQUIRED < 140000
#define kMIDIPropertyUMPCanTransmitGroupless CFSTR("ump endpoint")
#define kMIDIPropertyUMPActiveGroupBitmap CFSTR("active group bitmap")
#endif
#if __MAC_OS_X_VERSION_MIN_REQUIRED < 150000
#define kMIDIPropertyAssociatedEndpoint CFSTR("associated endpoint")
#endif

NAMESPACE_LIBREMIDI::coremidi_ump
{

class endpoint_impl final
    : public ump_endpoint_api
    , public error_handler
{
public:
  struct
      : libremidi::remote_ump_endpoint_configuration
      , coremidi_ump::endpoint_api_configuration
  {
  } configuration;

  endpoint_impl(
      libremidi::remote_ump_endpoint_configuration&& conf,
      coremidi_ump::endpoint_api_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    if (configuration.context)
    {
      m_client = *configuration.context;
    }
    else
    {
      auto result = MIDIClientCreate(
          toCFString(configuration.client_name).get(), nullptr, nullptr, &m_client);
      if (result != noErr)
      {
        libremidi_handle_error(
            this->configuration, "error creating MIDI client object: " + std::to_string(result));
        client_open_ = std::errc::io_error;
        return;
      }
      m_owns_client = true;
    }

    client_open_ = stdx::error{};
  }

  ~endpoint_impl() override
  {
    close();

    if (m_owns_client && m_client)
      MIDIClientDispose(m_client);
  }

  libremidi::API get_current_api() const noexcept override
  {
    return libremidi::API::COREMIDI_UMP;
  }

  stdx::error open(const ump_endpoint_info& endpoint, std::string_view /*local_name*/) override
  {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);

    const auto* id_str = libremidi_variant_alias::get_if<std::string>(&endpoint.endpoint_id);
    if (!id_str)
      return std::errc::invalid_argument;

    MIDIEndpointRef source_ref = 0;
    MIDIEndpointRef dest_ref = 0;

    auto colon_pos = id_str->find(':');
    if (colon_pos == std::string::npos)
      return std::errc::invalid_argument;

    try
    {
      source_ref = static_cast<MIDIEndpointRef>(std::stoul(id_str->substr(0, colon_pos)));
      dest_ref = static_cast<MIDIEndpointRef>(std::stoul(id_str->substr(colon_pos + 1)));
    }
    catch (...)
    {
      return std::errc::invalid_argument;
    }

    m_protocol = kMIDIProtocol_2_0;
    if (endpoint.active_protocol == midi_protocol::midi1)
      m_protocol = kMIDIProtocol_1_0;

    if (source_ref && (configuration.on_message || configuration.on_raw_data))
    {
      MIDIPortRef in_port;
      OSStatus result = MIDIInputPortCreateWithProtocol(
          m_client, toCFString("Input").get(), m_protocol, &in_port,
          ^(const MIDIEventList* evtlist, void* __nullable /*srcConnRefCon*/) {
              this->midiInputCallback(evtlist);
          });

      if (result != noErr)
      {
        libremidi_handle_error(this->configuration, "error creating macOS MIDI input port.");
        return std::errc::io_error;
      }

      if (result = MIDIPortConnectSource(in_port, source_ref, nullptr); result != noErr)
      {
        MIDIPortDispose(in_port);
        libremidi_handle_error(this->configuration, "error connecting macOS MIDI input port.");
        return std::errc::io_error;
      }

      m_input_port = in_port;
    }

    if (dest_ref)
    {
      MIDIPortRef out_port;
      OSStatus result = MIDIOutputPortCreate(m_client, toCFString("Output").get(), &out_port);
      if (result != noErr)
      {
        if (m_input_port) MIDIPortDispose(m_input_port);
        m_input_port = 0;
        libremidi_handle_error(this->configuration, "error creating macOS MIDI output port.");
        return std::errc::io_error;
      }
      m_output_port = out_port;
      m_destinationId = dest_ref;
    }

    connected_endpoint_ = endpoint;
    active_protocol_ = endpoint.active_protocol;
    port_open_ = true;
    connected_ = true;

    return stdx::error{};
  }

  stdx::error open_virtual(std::string_view port_name) override
  {
    auto cf_name = toCFString(port_name);

    OSStatus result = MIDISourceCreateWithProtocol(
        m_client, cf_name.get(), kMIDIProtocol_2_0, &m_virtual_source);
    if (result != noErr)
    {
      libremidi_handle_error(this->configuration, "error creating virtual MIDI source.");
      return std::errc::io_error;
    }

    MIDIObjectSetIntegerProperty(m_virtual_source, kMIDIPropertyUMPCanTransmitGroupless, 1);
    MIDIObjectSetIntegerProperty(m_virtual_source, kMIDIPropertyUMPActiveGroupBitmap, 0xFFFF);

    result = MIDIDestinationCreateWithProtocol(
        m_client, cf_name.get(), kMIDIProtocol_2_0, &m_virtual_destination,
        ^(const MIDIEventList* evtlist, void*) {
            this->midiInputCallback(evtlist);
        });

    if (result != noErr)
    {
      MIDIEndpointDispose(m_virtual_source);
      m_virtual_source = 0;
      libremidi_handle_error(this->configuration, "error creating virtual MIDI destination.");
      return std::errc::io_error;
    }

    MIDIObjectSetIntegerProperty(m_virtual_destination, kMIDIPropertyUMPCanTransmitGroupless, 1);
    MIDIObjectSetIntegerProperty(m_virtual_destination, kMIDIPropertyUMPActiveGroupBitmap, 0xFFFF);

    // Associate source and destination so CoreMIDI treats them as one endpoint
    SInt32 srcUID = 0, dstUID = 0;
    MIDIObjectGetIntegerProperty(m_virtual_source, kMIDIPropertyUniqueID, &srcUID);
    MIDIObjectGetIntegerProperty(m_virtual_destination, kMIDIPropertyUniqueID, &dstUID);
    if (srcUID && dstUID)
    {
      MIDIObjectSetIntegerProperty(m_virtual_source, kMIDIPropertyAssociatedEndpoint, dstUID);
      MIDIObjectSetIntegerProperty(m_virtual_destination, kMIDIPropertyAssociatedEndpoint, srcUID);
    }

    active_protocol_ = midi_protocol::midi2;
    port_open_ = true;
    connected_ = true;

    return stdx::error{};
  }

  stdx::error close() override
  {
    if (m_virtual_source)
    {
      MIDIEndpointDispose(m_virtual_source);
      m_virtual_source = 0;
    }
    if (m_virtual_destination)
    {
      MIDIEndpointDispose(m_virtual_destination);
      m_virtual_destination = 0;
    }
    if (m_input_port)
    {
      MIDIPortDispose(m_input_port);
      m_input_port = 0;
    }
    if (m_output_port)
    {
      MIDIPortDispose(m_output_port);
      m_output_port = 0;
    }
    m_destinationId = 0;

    port_open_ = false;
    connected_ = false;
    connected_endpoint_.reset();

    return stdx::error{};
  }

  stdx::error send_ump(const uint32_t* ump_stream, std::size_t count) override
  {
    MIDIEventList* eventList = reinterpret_cast<MIDIEventList*>(m_eventListBuffer);
    MIDIEventPacket* packet = MIDIEventListInit(eventList, m_protocol);
    const MIDITimeStamp ts = LIBREMIDI_AUDIO_GET_CURRENT_HOST_TIME();

    auto write_fun = [ts, &packet, &eventList](const uint32_t* ump, int bytes) -> std::errc {
      packet = MIDIEventListAdd(eventList, event_list_max_size, packet, ts, bytes / 4, ump);
      return packet ? std::errc{0} : std::errc::not_enough_memory;
    };

    auto realloc_fun = [this, &packet, &eventList] {
      push_event_list(eventList);
      packet = MIDIEventListInit(eventList, m_protocol);
    };

    segment_ump_stream(ump_stream, count, write_fun, realloc_fun);
    return push_event_list(eventList);
  }

  int64_t current_time() const noexcept override
  {
    return coremidi_data::time_in_nanos(LIBREMIDI_AUDIO_GET_CURRENT_HOST_TIME());
  }

  stdx::error push_event_list(MIDIEventList* eventList)
  {
    if (m_virtual_source)
    {
      if (MIDIReceivedEventList(m_virtual_source, eventList) != noErr)
      {
        libremidi_handle_warning(this->configuration, "error sending MIDI to virtual destinations.");
        return std::errc::io_error;
      }
    }

    if (m_destinationId != 0)
    {
      if (MIDISendEventList(m_output_port, m_destinationId, eventList) != noErr)
      {
        libremidi_handle_warning(this->configuration, "error sending MIDI message to port.");
        return std::errc::io_error;
      }
    }

    return stdx::error{};
  }

  void midiInputCallback(const MIDIEventList* list)
  {
    const MIDIEventPacket* packet = &list->packet[0];
    for (unsigned int i = 0; i < list->numPackets; ++i)
    {
      if (packet->wordCount > 0)
      {
        const int64_t timestamp = coremidi_data::time_in_nanos(packet->timeStamp);
        dispatch_ump(packet->words, packet->wordCount, timestamp);
      }
      packet = MIDIEventPacketNext(packet);
    }
  }

  void dispatch_ump(const uint32_t* words, size_t max_words, int64_t timestamp)
  {
    if (configuration.on_raw_data)
      configuration.on_raw_data({words, max_words}, timestamp);

    if (configuration.on_message)
    {
      size_t offset = 0;
      while (offset < max_words)
      {
        ump msg;
        msg.data[0] = words[offset];

        uint8_t type = (msg.data[0] >> 28) & 0x0F;
        size_t msg_words = 1;
        switch (type)
        {
          case 0x0: case 0x1: case 0x2: case 0x6: case 0x7:
            msg_words = 1; break;
          case 0x3: case 0x4: case 0x8: case 0x9: case 0xA:
            msg_words = 2; break;
          case 0xB: case 0xC:
            msg_words = 3; break;
          case 0x5: case 0xD: case 0xE: case 0xF:
            msg_words = 4; break;
        }

        if (offset + msg_words > max_words)
          break;

        for (size_t w = 1; w < msg_words && w < 4; ++w)
          msg.data[w] = words[offset + w];

        msg.timestamp = timestamp;
        configuration.on_message(std::move(msg));

        offset += msg_words;
      }
    }
  }

  MIDIClientRef m_client{0};
  bool m_owns_client{false};
  MIDIProtocolID m_protocol{kMIDIProtocol_2_0};

  MIDIPortRef m_input_port{0};
  MIDIPortRef m_output_port{0};
  MIDIEndpointRef m_destinationId{0};

  MIDIEndpointRef m_virtual_source{0};
  MIDIEndpointRef m_virtual_destination{0};

  static constexpr int event_list_max_size = 65535;
  unsigned char m_eventListBuffer[sizeof(MIDIEventList) + event_list_max_size];
};

}
