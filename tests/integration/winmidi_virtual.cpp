#include <libremidi/backends/winmidi/virtual_port.hpp>
#include <libremidi/libremidi.hpp>

#include <chrono>
#include <iostream>
#include <semaphore>
#include <thread>

using namespace libremidi::winmidi;
using namespace std::chrono_literals;
namespace
{
// Group 0, MIDI 2.0 Control Change, recognizable in either direction.
constexpr uint32_t cc = 0x40b05b00, ping = 0x12345678, pong = 0x23456789, ack = 0x3456789a;

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

void host()
{
  std::binary_semaphore request{0}, finished{0};
  libremidi::ump_input_configuration cfg{
      .on_message = [&](const libremidi::ump& msg) {
        if (msg.size() != 2 || msg[0] != cc)
          return;
        if (msg[1] == ping)
          request.release();
        else if (msg[1] == ack)
          finished.release();
      }};
  libremidi::midi_in in{cfg, input_configuration{}};
  libremidi::midi_out out{{}, output_configuration{}};
  for (int round = 0; round < 2; ++round)
  {
    const auto name = "lm-" + to_string(virtual_product_instance_id());
    check(in.open_virtual_port(name + "-in"));
    check(out.open_virtual_port(name + "-out"));
    std::cout << name << std::endl; // pipe to the client, not a MIDI device selector from the user
    require(request.try_acquire_for(40s), "host did not receive the client's request");
    check(out.send_ump(cc, pong));
    require(finished.try_acquire_for(10s), "client did not acknowledge host's reply");
    check(out.close_port());
    check(in.close_port());
  }
}

bool is_virtual_transport(const auto& transport)
{
  if constexpr (requires { transport.TransportId(); })
    return transport.TransportId() == midi2v::MidiVirtualDeviceManager::TransportId();
  else
    return transport.TransportId == midi2v::MidiVirtualDeviceManager::TransportId();
}

void client()
{
  // Use the service's public view, independent of libremidi's observer/GTB fallback.
  // Keep the SDK initialized in the client process as well as in the host.
  winmidi_shared_data sdk;
  const auto session = MidiSession::Create(L"libremidi virtual port smoke client");
  require(!!session, "could not create client session");
  for (int round = 0; round < 2; ++round)
  {
    std::string name;
    require(!!std::getline(std::cin, name) && name.starts_with("lm-"), "missing host handshake");
    const auto in_name = to_hstring(name + "-in"), out_name = to_hstring(name + "-out");
    hstring destination, source;
    const auto deadline = std::chrono::steady_clock::now() + 30s;
    do
    {
      for (const auto& ep : MidiEndpointDeviceInformation::FindAll())
      {
        if (ep.Name() != in_name && ep.Name() != out_name)
          continue;
        if (!is_virtual_transport(ep.GetTransportSuppliedInfo()))
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

    std::binary_semaphore response{0};
    auto to_host = session.CreateEndpointConnection(destination);
    auto from_host = session.CreateEndpointConnection(source);
    require(to_host && from_host, "could not create public endpoint connections");
    auto handler = from_host.MessageReceived(auto_revoke, [&](const auto&, const auto& args) {
      uint32_t a{}, b{}, c{}, d{};
      if (args.FillWords(a, b, c, d) == 2 && a == cc && b == pong)
        response.release();
    });
    require(from_host.Open() && to_host.Open(), "could not open public endpoints");
    require(to_host.SendSingleMessageWords(0, cc, ping) == MidiSendMessageResults::Succeeded,
            "could not send request");
    require(response.try_acquire_for(10s), "client did not receive host's reply");
    handler.revoke();
    session.DisconnectEndpointConnection(from_host.ConnectionId());
    require(to_host.SendSingleMessageWords(0, cc, ack) == MidiSendMessageResults::Succeeded,
            "could not acknowledge reply");
    session.DisconnectEndpointConnection(to_host.ConnectionId());
  }
  session.Close();
}
}

int main(int argc, char** argv)
try
{
  require(argc == 2, "specify host or client");
  init_apartment();
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
