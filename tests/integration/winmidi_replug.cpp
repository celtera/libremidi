// A device that leaves and comes back, with ports open on it: a loopback pair
// is removed and created again. No hardware. Returns 77 when the service or
// its loopback transport is unavailable.
#include <libremidi/libremidi.hpp>
#include <libremidi/backends/winmidi.hpp>

#include <winrt/Windows.Devices.Midi2.Transports.Loopback.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

using namespace std::chrono_literals;
namespace lb = winrt::Windows::Devices::Midi2::Transports::Loopback;

static void require(bool pass, const char* what)
{
  if (!pass) throw std::runtime_error{what};
}

template <typename F>
static bool eventually(F&& f, std::chrono::milliseconds limit = 5s)
{
  const auto end = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < end)
  {
    if (f()) return true;
    std::this_thread::sleep_for(10ms);
  }
  return f();
}

struct loopback_pair
{
  winrt::guid association;
  loopback_pair()
  {
    lb::MidiLoopbackEndpointDefinition a{L"libremidi replug A", L"libremidi test", L"lmreplug_a"};
    lb::MidiLoopbackEndpointDefinition b{L"libremidi replug B", L"libremidi test", L"lmreplug_b"};
    const auto made = lb::MidiLoopbackManager::CreateTransientLoopback(lb::MidiLoopbackCreationConfig{a, b});
    require(made && made.Success(), "could not create the loopback pair");
    association = made.CreatedLoopbackEntry().AssociationId();
  }
  loopback_pair(const loopback_pair&) = delete;
  ~loopback_pair()
  {
    lb::MidiLoopbackManager::RemoveTransientLoopback(lb::MidiLoopbackRemovalConfig{association});
  }
};

template <typename Port>
static std::optional<Port> find(const std::vector<Port>& ports, std::string_view name)
{
  for (const auto& p : ports)
    if (p.device_name == name) return p;
  return {};
}

int main()
try
{
  winrt::init_apartment();
  const auto api = libremidi::API::WINDOWS_MIDI_SERVICES;
  libremidi::observer_configuration software;
  software.track_virtual = true;
  libremidi::observer observer{software, libremidi::observer_configuration_for(api)};
  if (observer.get_current_api() != api || !lb::MidiLoopbackManager::IsTransportAvailable())
    return 77;

  const auto output_on_a = [&] { return find(observer.get_output_ports(), "libremidi replug A"); };
  const auto input_on_b = [&] { return find(observer.get_input_ports(), "libremidi replug B"); };

  std::optional<loopback_pair> device;
  device.emplace();
  require(eventually([&] { return output_on_a() && input_on_b(); }), "the pair is not listed");

  std::atomic<uint32_t> first{0}, second{0};
  libremidi::midi_in in{
      libremidi::ump_input_configuration{
          .on_message = [&](const libremidi::ump& u) { first = u[1]; }},
      libremidi::midi_in_configuration_for(api)};
  libremidi::midi_out out{{}, libremidi::midi_out_configuration_for(api)};
  require(!in.open_port(*input_on_b()).is_set(), "could not open the input");
  require(!out.open_port(*output_on_a()).is_set(), "could not open the output");
  require(!out.send_ump(0x40903C00u, 1u).is_set(), "could not send");
  require(eventually([&] { return first == 1u; }), "nothing received before the removal");

  // The device leaves: its ports report errors, they do not stay silent.
  device.reset();
  require(eventually([&] { return !output_on_a() && !input_on_b(); }), "the pair is still listed");
  require(eventually([&] { return out.send_ump(0x40903C00u, 2u).is_set(); }),
          "sending to a removed endpoint reports no error");

  // It comes back under the same identifiers. Ports opened now work at once,
  // and the input that was never closed continues on their connection.
  device.emplace();
  require(eventually([&] { return output_on_a() && input_on_b(); }), "the pair is not listed again");
  require(out.send_ump(0x40903C00u, 3u).is_set(), "the output opened before the removal was reused");
  out.close_port();
  require(!out.open_port(*output_on_a()).is_set(), "could not reopen the output");
  libremidi::midi_in again{
      libremidi::ump_input_configuration{
          .on_message = [&](const libremidi::ump& u) { second = u[1]; }},
      libremidi::midi_in_configuration_for(api)};
  require(!again.open_port(*input_on_b()).is_set(), "could not open a second input");
  require(eventually([&] {
            out.send_ump(0x40903C00u, 4u);
            return second == 4u;
          }),
          "the input opened after the return receives nothing");
  require(eventually([&] { return first == 4u; }),
          "the input opened before the removal does not continue");

  std::puts("ports report the removal and work again after the return");
}
catch (const winrt::hresult_error& e)
{
  std::fprintf(stderr, "%ls\n", e.message().c_str());
  return e.code() == REGDB_E_CLASSNOTREG ? 77 : 1;
}
catch (const std::exception& e)
{
  std::fprintf(stderr, "%s\n", e.what());
  return 1;
}
