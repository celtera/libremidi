#pragma once
#include <libremidi/backends/winmidi/helpers.hpp>

#if LIBREMIDI_WINMIDI_HAS_VIRTUAL_DEVICE
#include <array>
#include <bit>

NAMESPACE_LIBREMIDI::winmidi
{
inline hstring virtual_product_instance_id()
{
  // The service uses this value to construct a device instance ID. Keep it
  // unique across processes, alphanumeric, and below the UMP 42-byte limit.
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

inline midi2v::MidiVirtualDeviceCreationConfig setup_virtualdevice_config(
    std::string_view manufacturer, std::string_view port_name, MidiFunctionBlockDirection direction)
{
  const auto name = to_hstring(port_name);
  MidiDeclaredEndpointInfo info{
      name, virtual_product_instance_id(), true, true, false, false,
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

struct virtual_port
{
  MidiEndpointConnection endpoint{nullptr};
  event_token received{};

  stdx::error open(
      const MidiSession& session, std::string_view manufacturer, std::string_view name,
      MidiFunctionBlockDirection direction,
      const TypedEventHandler<IMidiMessageReceivedEventSource, MidiMessageReceivedEventArgs>& handler
          = nullptr)
  {
    if (!session)
      return std::errc::not_connected;

    try
    {
      const auto config = setup_virtualdevice_config(manufacturer, name, direction);
      const auto device = midi2v::MidiVirtualDeviceManager::CreateVirtualDevice(config);
      if (!device)
        return std::errc::device_or_resource_busy;
      endpoint = session.CreateEndpointConnection(device.DeviceEndpointDeviceId());
      if (!endpoint)
        return std::errc::address_not_available;

      // This private, device-side connection must not install a COM raw
      // callback: it would bypass the plugin that answers discovery requests.
      if (endpoint.AddMessageProcessingPlugin(device) == MidiMessageProcessingPluginAddResult::Succeeded)
      {
        if (handler)
          received = endpoint.MessageReceived(handler);
        if ((!handler || received) && endpoint.Open())
          return {};
      }
    }
    catch (const hresult_error&)
    {
      // Roll back the session-owned connection, even if Open never succeeded.
    }
    close(session);
    return std::errc::io_error;
  }

  void close(const MidiSession& session)
  {
    if (!endpoint)
      return;
    if (received)
    {
      endpoint.MessageReceived(received);
      received = {};
    }
    // Disconnect calls Cleanup on the attached plugin and releases it.
    session.DisconnectEndpointConnection(endpoint.ConnectionId());
    endpoint = nullptr;
  }
};
}
#endif
