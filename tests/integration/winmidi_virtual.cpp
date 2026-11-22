#include <libremidi/config.hpp>
#include <libremidi/backends/winmidi/virtual_port.hpp>
#include <libremidi/libremidi.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

using namespace libremidi::winmidi;
using namespace std::chrono_literals;
namespace
{
// Group 0, MIDI 2.0 Control Change, recognizable in either direction.
constexpr uint32_t cc = 0x40b05b00, ping = 0x12345678, pong = 0x23456789;

void require(bool condition, const char* why)
{
  if (!condition)
    throw std::runtime_error(why);
}

void check(stdx::error error)
{
  if (error != stdx::error{})
    error.throw_exception();
}

// Auto-reset: the service may deliver a packet more than once.
winrt::handle signal()
{
  winrt::handle event{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
  require(!!event, "could not create event");
  return event;
}

bool wait(const winrt::handle& event, std::chrono::milliseconds limit)
{
  return WaitForSingleObject(event.get(), static_cast<DWORD>(limit.count())) == WAIT_OBJECT_0;
}

void host()
{
  const auto request = signal();
  libremidi::ump_input_configuration cfg{
      .on_message = [&](const libremidi::ump& msg) {
        if (msg.size() == 2 && msg[0] == cc && msg[1] == ping)
          SetEvent(request.get());
      }};
  // Both rounds reuse the identifiers: the client expects the same endpoints again.
  input_configuration in_api;
  in_api.virtual_endpoint_id = "libremidi_Smoke-in";
  output_configuration out_api;
  out_api.virtual_endpoint_id = "libremidi_Smoke-out";
  libremidi::midi_in in{cfg, in_api};
  libremidi::midi_out out{{}, out_api};
  for (int round = 0; round < 2; ++round)
  {
    const auto name = "lm-" + to_string(virtual_product_instance_id());
    // Auto-reset event: client has disconnected, then client has observed removal.
    // The "closed" pipe message separates the two signals.
    const auto event_name = to_hstring(name);
    winrt::handle ready{CreateEventW(nullptr, FALSE, FALSE, event_name.c_str())};
    require(!!ready && GetLastError() != ERROR_ALREADY_EXISTS, "could not create round event");
    ResetEvent(request.get()); // A late duplicate of the previous round's request.
    check(in.open_virtual_port(name + "-in"));
    check(out.open_virtual_port(name + "-out"));
    std::cout << name << std::endl; // pipe to the client, not a MIDI device selector from the user
    require(wait(request, 40s), "host did not receive the client's request");
    check(out.send_ump(cc, pong));
    require(WaitForSingleObject(ready.get(), 20000) == WAIT_OBJECT_0, "client did not disconnect");
    check(out.close_port());
    check(in.close_port());
    std::cout << "closed" << std::endl;
    require(WaitForSingleObject(ready.get(), 40000) == WAIT_OBJECT_0, "client did not observe removal");
  }
}

void wait_for_removal(const hstring& destination, const hstring& source)
{
  const auto deadline = std::chrono::steady_clock::now() + 30s;
  do
  {
    // The MIDI SDK's FindAll() can return an empty view on error. Use the same
    // device selector directly so an enumeration failure cannot count as removal.
    const auto devices = DeviceInformation::FindAllAsync(MidiEndpointConnection::GetDeviceSelector()).get();
    bool present = false;
    for (const auto& device : devices)
    {
      const auto id = device.Id(); // The SDK normalizes these IDs to lower case.
      if (CompareStringOrdinal(id.c_str(), -1, destination.c_str(), -1, TRUE) == CSTR_EQUAL
          || CompareStringOrdinal(id.c_str(), -1, source.c_str(), -1, TRUE) == CSTR_EQUAL)
      {
        present = true;
        break;
      }
    }
    if (!present)
      return;
    std::this_thread::sleep_for(250ms);
  } while (std::chrono::steady_clock::now() < deadline);
  throw std::runtime_error("public endpoint still discoverable after close");
}

void exchange(const MidiSession& session, const hstring& destination, const hstring& source)
{
  const auto response = signal();
  auto to_host = session.CreateEndpointConnection(destination);
  auto from_host = session.CreateEndpointConnection(source);
  require(to_host && from_host, "could not create public endpoint connections");
  auto handler = from_host.MessageReceived(auto_revoke, [&](const auto&, const auto& args) {
    uint32_t a{}, b{}, c{}, d{};
    if (args.FillWords(a, b, c, d) == 2 && a == cc && b == pong)
      SetEvent(response.get());
  });
  require(from_host.Open() && to_host.Open(), "could not open public endpoints");
  require(to_host.SendSingleMessageWords(0, cc, ping) == MidiSendMessageResults::Succeeded,
          "could not send request");
  require(wait(response, 10s), "client did not receive host's reply");
  handler.revoke();
  session.DisconnectEndpointConnection(from_host.ConnectionId());
  session.DisconnectEndpointConnection(to_host.ConnectionId());
}

void client()
{
  // Use the service's public view, independent of libremidi's observer/GTB fallback.
  const auto session = MidiSession::Create(L"libremidi virtual port smoke client");
  require(!!session, "could not create client session");
  hstring first_destination, first_source;
  for (int round = 0; round < 2; ++round)
  {
    std::string name;
    require(!!std::getline(std::cin, name) && name.starts_with("lm-"), "missing host handshake");
    winrt::handle ready{OpenEventW(EVENT_MODIFY_STATE, FALSE, to_hstring(name).c_str())};
    require(!!ready, "could not open round event");
    const auto in_name = to_hstring(name + "-in"), out_name = to_hstring(name + "-out");
    hstring destination, source;
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    do
    {
      destination.clear();
      source.clear();
      for (const auto& ep : MidiEndpointDeviceInformation::FindAll())
      {
        if (ep.Name() != in_name && ep.Name() != out_name)
          continue;
        const auto transport = ep.GetTransportSuppliedInfo();
        if (!transport || transport.TransportId() != midi2v::MidiVirtualDeviceManager::TransportId())
          continue;
        auto blocks = ep.GetDeclaredFunctionBlocks();
        if (blocks.Size() != 1)
          continue; // Discovery is asynchronous. Do not accept the default endpoint metadata.
        auto block = blocks.GetAt(0);
        if (!block.IsActive() || block.Number() != 0 || block.FirstGroup().Index() != 0
            || block.GroupCount() != 1 || block.Name() != ep.Name())
          continue;
        if (ep.Name() == in_name && block.Direction() == MidiFunctionBlockDirection::BlockInput)
          destination = ep.EndpointDeviceId();
        if (ep.Name() == out_name && block.Direction() == MidiFunctionBlockDirection::BlockOutput)
          source = ep.EndpointDeviceId();
      }
      if (!destination.empty() && !source.empty())
        break;
      std::this_thread::sleep_for(250ms);
    } while (std::chrono::steady_clock::now() < deadline);
    require(!destination.empty() && !source.empty(), "public endpoints or declared blocks missing");

    // The configured identifiers name the endpoints, in lower case, on every open.
    require(std::wstring_view{destination}.find(L"libremidi_smoke-in") != std::wstring_view::npos
                && std::wstring_view{source}.find(L"libremidi_smoke-out") != std::wstring_view::npos,
            "endpoint IDs do not carry the configured identifiers");
    if (round == 0)
    {
      first_destination = destination;
      first_source = source;
    }
    require(destination == first_destination && source == first_source,
            "endpoint IDs changed between two opens with the same identifiers");

    exchange(session, destination, source);
    check_bool(SetEvent(ready.get()));
    std::string closed;
    require(!!std::getline(std::cin, closed) && closed == "closed", "missing host close handshake");
    wait_for_removal(destination, source);
    check_bool(SetEvent(ready.get()));
  }
  session.Close();
}
}

int main(int argc, char** argv)
try
{
  require(argc == 2, "specify host or client");
  init_apartment();
  if (!service_available())
    return 77;
  const auto manager = try_get_activation_factory<
      midi2v::MidiVirtualDeviceManager, midi2v::IMidiVirtualDeviceManagerStatics>();
  if (!manager || !manager.IsTransportAvailable())
    return 77;
  if (std::string_view(argv[1]) == "host")
    host();
  else if (std::string_view(argv[1]) == "client")
    client();
  else
    throw std::runtime_error("unknown role");
}
catch (const std::exception& e)
{
  std::cerr << e.what() << '\n';
  return 1;
}
