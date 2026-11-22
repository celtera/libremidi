#pragma once
#include <libremidi/backends/winmidi/config.hpp>
#include <libremidi/backends/winmidi/helpers.hpp>
#include <libremidi/backends/winmidi/observer.hpp>
#include <libremidi/detail/midi_in.hpp>
#include <libremidi/detail/midi_stream_decoder.hpp>
#include <libremidi/detail/ump_stream.hpp>

NAMESPACE_LIBREMIDI::winmidi
{
class midi_in_impl final
    : public midi2::in_api
    , public error_handler
{
public:
  struct
      : libremidi::ump_input_configuration
      , winmidi::input_configuration
  {
  } configuration;

#if LIBREMIDI_WINMIDI_HAS_COM_EXTENSIONS
  struct raw_callback_type final : IMidiEndpointConnectionMessagesReceivedCallback
  {
    explicit raw_callback_type(midi_in_impl& self)
        : self{self}
    {

    }

    midi_in_impl& self;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
    {
      if (!ppvObject)
        return E_POINTER;

      if (riid == __uuidof(IUnknown) ||
          riid == ::IID_IMidiEndpointConnectionMessagesReceivedCallback)
      {
        *ppvObject = static_cast<IMidiEndpointConnectionMessagesReceivedCallback*>(this);
        AddRef();
        return S_OK;
      }

      *ppvObject = nullptr;
      return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
      return 1;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
      return 1;
    }

    HRESULT STDMETHODCALLTYPE MessagesReceived(
        GUID sessionId,
        GUID connectionId,
        UINT64 timestamp,
        UINT32 wordCount,
        const UINT32* messages) override {
      HRESULT res{};
      self.process_message(sessionId, connectionId, timestamp, wordCount, messages);
      return res;
    }
  } raw_callback{*this};
#endif

  explicit midi_in_impl(
      libremidi::ump_input_configuration&& conf, winmidi::input_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
      , m_session{
            configuration.context ? *configuration.context
                                  : MidiSession::Create(to_hstring(configuration.client_name))}
  {
    if (!m_session)
      return;
    this->client_open_ = stdx::error{};

    m_tick_frequency_hz = MidiClock::TimestampFrequency();
    if (m_tick_frequency_hz == 0)
      m_tick_frequency_hz = 10'000'000; // 100 ns ticks
  }

  ~midi_in_impl() override
  {
    close_port();
    this->client_open_ = std::errc::not_connected;
  }

  libremidi::API get_current_api() const noexcept override
  {
    return libremidi::API::WINDOWS_MIDI_SERVICES;
  }

  stdx::error open_port(const input_port& port, std::string_view) override
  {
    try
    {
      auto ep = get_port(port, port_direction::input);
      if (!ep)
        return std::errc::address_not_available;
      m_group_filter = static_cast<int>(port.port);
      m_endpoint = m_session.CreateEndpointConnection(ep.EndpointDeviceId());
      if (!m_endpoint)
        return std::errc::device_or_resource_busy;

  #if !LIBREMIDI_WINMIDI_HAS_COM_EXTENSIONS
      m_revoke_token = m_endpoint.MessageReceived(
          [this](
              const IMidiMessageReceivedEventSource&,
              const winrt::Windows::Devices::Midi2::MidiMessageReceivedEventArgs& args) {
        process_message(args);
      });
  #else
      m_endpoint.as(::IID_IMidiEndpointConnectionRaw, m_raw_endpoint.put_void());

      m_raw_endpoint->SetMessagesReceivedCallback(
          &raw_callback
      );
  #endif

      m_endpoint.Open();

      return stdx::error{};
    }
    catch (...)
    {
      return std::errc::io_error;
    }
  }

#if LIBREMIDI_WINMIDI_HAS_VIRTUAL_DEVICE
  stdx::error open_virtual_port(std::string_view port_name) override
  {
    m_group_filter = -1; // A virtual device is not a single projected group.
    // Create endpoint information for the virtual device
    using namespace midi2v;

    auto conf = setup_virtualdevice_config(configuration.client_name, port_name, port_name, MidiFunctionBlockDirection::BlockInput);

    m_virtual = MidiVirtualDeviceManager::CreateVirtualDevice(conf);
    if (m_virtual == nullptr)
      return std::errc::device_or_resource_busy;

    try
    {
      // Create a connection to the device-side endpoint
      m_endpoint = m_session.CreateEndpointConnection(m_virtual.DeviceEndpointDeviceId());
      if (!m_endpoint)
        return std::errc::device_or_resource_busy;

      // Add the virtual device as a message processing plugin to receive messages
      m_endpoint.AddMessageProcessingPlugin(m_virtual);

      // Register message received event handler
      m_revoke_token = m_endpoint.MessageReceived(
          [this](
              const IMidiMessageReceivedEventSource&,
              const winrt::Windows::Devices::Midi2::MidiMessageReceivedEventArgs& args) {
        process_message(args);
      });

      m_endpoint.Open();

      return stdx::error{};
    }
    catch (...)
    {
      return std::errc::io_error;
    }
  }
#endif

  // split in two so ticks * 1e9 can't overflow
  std::uint64_t ticks_to_ns(std::uint64_t ticks) const noexcept
  {
    constexpr std::uint64_t ns_per_s = 1'000'000'000ull;
    const std::uint64_t whole_seconds = ticks / m_tick_frequency_hz;
    const std::uint64_t remainder_ticks = ticks % m_tick_frequency_hz;
    return whole_seconds * ns_per_s + (remainder_ticks * ns_per_s) / m_tick_frequency_hz;
  }

  void process_message(
      const winrt::Windows::Devices::Midi2::MidiMessageReceivedEventArgs& msg)
  {
    static constexpr timestamp_backend_info timestamp_info{
        .has_absolute_timestamps = true,
        .absolute_is_monotonic = false,
        .has_samples = false,
    };

    if (!ump_matches_group(msg.PeekFirstWord(), m_group_filter))
      return;
    const auto& ump = msg.GetMessagePacket();

    const auto& b = ump.GetAllWords();

    uint32_t ump_space[64];
    array_view<uint32_t> ref{ump_space};
    const auto count = b.GetMany(0, ref);

    auto to_ns = [this, t = ump.Timestamp()] { return ticks_to_ns(t); };
    m_processing.on_bytes(
        {ump_space, ump_space + count}, m_processing.timestamp<timestamp_info>(to_ns, 0));
  }

#if LIBREMIDI_WINMIDI_HAS_COM_EXTENSIONS
  void process_message(
      const GUID& /* sessionId */,
      const GUID& /* connectionId */,
      UINT64 timestamp,
      UINT32 wordCount,
      const UINT32* ump)
  {
    static constexpr timestamp_backend_info timestamp_info{
        .has_absolute_timestamps = true,
        .absolute_is_monotonic = false,
        .has_samples = false,
    };

    // The COM event is a word-aligned batch, not necessarily all from the same group.
    std::span<const uint32_t> words{ump, wordCount};
    auto run = next_ump_group_run(words, m_group_filter);
    if (run.empty())
      return;

    auto to_ns = [this, t = timestamp] { return ticks_to_ns(t); };
    const auto ts = m_processing.timestamp<timestamp_info>(to_ns, 0);
    do
    {
      m_processing.on_bytes(run, ts);
      run = next_ump_group_run(words, m_group_filter);
    } while (!run.empty());
  }
#endif

  stdx::error close_port() override
  {
    if(!m_endpoint)
      return std::errc::not_connected;

#if LIBREMIDI_WINMIDI_HAS_COM_EXTENSIONS
    if(m_raw_endpoint) {
      m_raw_endpoint->RemoveMessagesReceivedCallback();
      m_raw_endpoint = nullptr;
    }
    // When no raw API: everything goes through revoke_token.
    // Otherwise: only virtual ports.
    else if(m_virtual)
#endif
      m_endpoint.MessageReceived(m_revoke_token);

    m_session.DisconnectEndpointConnection(m_endpoint.ConnectionId());

#if LIBREMIDI_WINMIDI_HAS_VIRTUAL_DEVICE
    if (m_virtual)
    {
      m_virtual.Cleanup();
      m_virtual = nullptr;
    }
  #endif
    return stdx::error{};
  }

  virtual timestamp absolute_timestamp() const noexcept override { return {}; }

private:
  MidiSession m_session;
  winrt::event_token m_revoke_token{};
  MidiEndpointConnection m_endpoint{nullptr};
#if LIBREMIDI_WINMIDI_HAS_COM_EXTENSIONS
  winrt::impl::com_ref<IMidiEndpointConnectionRaw> m_raw_endpoint{};
#endif
#if LIBREMIDI_WINMIDI_HAS_VIRTUAL_DEVICE
  midi2v::MidiVirtualDevice m_virtual{nullptr};
#endif
  midi2::input_state_machine m_processing{this->configuration};
  int m_group_filter = -1;
  std::uint64_t m_tick_frequency_hz{};
};
}
