#pragma once
#include <libremidi/backends/winmidi/session.hpp>

#if LIBREMIDI_WINMIDI_HAS_VIRTUAL_DEVICE
#include <array>
#include <bit>

NAMESPACE_LIBREMIDI::winmidi
{
// The service builds the device instance ID from this value: it rejects other
// characters, cuts what follows the 32nd one, and compares in lower case.
inline constexpr std::size_t virtual_endpoint_id_max_length = 32;

constexpr bool valid_virtual_endpoint_id(std::string_view id) noexcept
{
  if (id.empty() || id.size() > virtual_endpoint_id_max_length)
    return false;
  for (char c : id)
  {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'
          || c == '_'))
      return false;
  }
  return true;
}

inline hstring virtual_product_instance_id()
{
  // Unique across processes, and already in the form the service keeps.
  GUID guid;
  check_hresult(CoCreateGuid(&guid));
  constexpr wchar_t hex[] = L"0123456789abcdef";
  std::array<wchar_t, 32> text;
  auto out = text.begin();
  for (auto byte : std::bit_cast<std::array<uint8_t, 16>>(guid))
  {
    *out++ = hex[byte >> 4];
    *out++ = hex[byte & 15];
  }
  return hstring{text.data(), static_cast<uint32_t>(text.size())};
}

// endpoint_id: empty, or accepted by valid_virtual_endpoint_id.
inline hstring virtual_product_instance_id(std::string_view endpoint_id)
{
  if (endpoint_id.empty())
    return virtual_product_instance_id();

  // Announce the identifier as the service stores it.
  std::array<wchar_t, virtual_endpoint_id_max_length> text;
  auto out = text.begin();
  for (char c : endpoint_id.substr(0, text.size()))
    *out++ = static_cast<wchar_t>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  return hstring{text.data(), static_cast<uint32_t>(out - text.begin())};
}

inline midi2v::MidiVirtualDeviceCreationConfig setup_virtualdevice_config(
    std::string_view manufacturer, std::string_view port_name, MidiFunctionBlockDirection direction,
    std::string_view endpoint_id = {})
{
  const auto name = to_hstring(port_name);
  MidiDeclaredEndpointInfo info{
      name, virtual_product_instance_id(endpoint_id), true, true, false, false,
      /* static function blocks */ true, /* declared function block count */ 1, 1, 1};
  midi2v::MidiVirtualDeviceCreationConfig config{
      name, name, to_hstring(manufacturer.empty() ? "libremidi" : manufacturer), info};
  MidiFunctionBlock block;
  block.Number(0);
  block.IsActive(true);
  block.Name(name);
  block.FirstGroup(MidiGroup(uint8_t{0}));
  block.GroupCount(1);
  block.Direction(direction);
  config.FunctionBlocks().Append(block);
  return config;
}

class virtual_port
{
  struct connection
  {
    MidiSession session;
    MidiEndpointConnection endpoint{nullptr};
    event_token received{};
    PTP_WORK retire;

    explicit connection(const MidiSession& s)
        : session{s}
        , retire{CreateThreadpoolWork([](PTP_CALLBACK_INSTANCE, void* p, PTP_WORK) {
            delete static_cast<connection*>(p);
          }, this, nullptr)}
    {
      if (!retire)
        throw_last_error();
    }

    ~connection()
    {
      if (endpoint)
      {
        if (received)
          endpoint.MessageReceived(received);
        // Disconnect calls Cleanup on the attached plugin and releases it.
        session.DisconnectEndpointConnection(endpoint.ConnectionId());
      }
      CloseThreadpoolWork(retire);
    }
  };

  struct release
  {
    void operator()(connection* c) const noexcept
    {
      // Disconnect can join the calling SDK receive thread.
      if (receive_scope::inside)
        SubmitThreadpoolWork(c->retire);
      else
        delete c;
    }
  };
  using owner = std::unique_ptr<connection, release>;
  owner active;

public:
  explicit operator bool() const noexcept { return bool(active); }
  const MidiEndpointConnection& endpoint() const noexcept { return active->endpoint; }
  void close() noexcept { active.reset(); }

  stdx::error open(
      const MidiSession& session, std::string_view manufacturer, std::string_view name,
      std::string_view endpoint_id, MidiFunctionBlockDirection direction,
      const TypedEventHandler<
          IMidiMessageReceivedEventSource, winrt::Windows::Devices::Midi2::MidiMessageReceivedEventArgs>&
          handler = nullptr)
  {
    if (!endpoint_id.empty() && !valid_virtual_endpoint_id(endpoint_id))
      return std::errc::invalid_argument;
    if (!session || !session.IsOpen())
      return std::errc::not_connected;

    // Roll back the application's session connection if registration or Open
    // fails. The SDK exposes no removal API for the service's private device.
    owner next{new connection{session}};
    const auto config = setup_virtualdevice_config(manufacturer, name, direction, endpoint_id);
    const auto device = midi2v::MidiVirtualDeviceManager::CreateVirtualDevice(config);
    if (!device)
      return std::errc::device_or_resource_busy;
    next->endpoint = session.CreateEndpointConnection(device.DeviceEndpointDeviceId());
    if (!next->endpoint)
      return std::errc::address_not_available;

    // A raw COM callback on this private connection would bypass discovery.
    if (next->endpoint.AddMessageProcessingPlugin(device) != MidiMessageProcessingPluginAddResult::Succeeded)
      return std::errc::protocol_error;
    if (handler)
    {
      next->received = next->endpoint.MessageReceived(handler);
      if (!next->received)
        return std::errc::io_error;
    }
    if (!next->endpoint.Open())
      return std::errc::connection_refused;
    active = std::move(next);
    return {};
  }
};
}
#endif
