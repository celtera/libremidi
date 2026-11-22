// Uses only the service's cross-wired diagnostic endpoints. No hardware or
// virtual-device creation is involved. Return 77 when the service is unavailable.
#include <libremidi/backends/winmidi/session.hpp>
#include <combaseapi.h>
#include <winrt/Windows.Devices.Midi2.Diagnostics.h>

#include <barrier>
#include <chrono>
#include <cstdio>
#include <future>
#include <stdexcept>

namespace w = libremidi::winmidi;
using namespace std::chrono_literals;

static void require(bool pass, const char* what)
{
  if (!pass) throw std::runtime_error{what};
}

static void require_sta()
{
  APTTYPE type{};
  APTTYPEQUALIFIER qualifier{};
  require(SUCCEEDED(CoGetApartmentType(&type, &qualifier)) && (type == APTTYPE_MAINSTA || type == APTTYPE_STA),
          "host apartment changed");
}

struct input_log
{
  std::mutex mutex;
  std::condition_variable changed;
  std::vector<uint32_t> words;

  void record(uint32_t word)
  {
    { std::lock_guard lock{mutex}; words.push_back(word); }
    changed.notify_all();
  }
  void wait(size_t count)
  {
    std::unique_lock lock{mutex};
    require(changed.wait_for(lock, 5s, [&] { return words.size() >= count; }), "receive timeout");
  }
  bool equals(std::initializer_list<uint32_t> expected)
  {
    std::lock_guard lock{mutex};
    return words == std::vector<uint32_t>(expected);
  }
};

int main() try
{
  // The host owns this apartment; the backend must leave it STA.
  winrt::init_apartment(winrt::apartment_type::single_threaded);
  // Exercise the COM guard even on CI machines without MIDI Services.
  {
    w::midi_apartment mta;
    require_sta();
  }
  require_sta();
  if (!w::service_available()) return 77;
  namespace sdk = winrt::Windows::Devices::Midi2;
  using diagnostics = sdk::Diagnostics::MidiDiagnostics;
  auto a = diagnostics::DiagnosticsLoopbackAEndpointDeviceId();
  auto b = diagnostics::DiagnosticsLoopbackBEndpointDeviceId();
  w::session_handle first{nullptr}, second{nullptr};
  auto host = first.get({});
  require(host.Name() == w::host_session_name() && host.SessionId() == second.get({}).SessionId(),
          "host session was not shared or named after its executable");
  auto session = first.get("libremidi session regression");
  require(session.Name() == L"libremidi session regression" && session.SessionId() != host.SessionId(),
          "explicit name was ignored");
  require(second.get("libremidi session regression").SessionId() == session.SessionId(), "named session not shared");
  {
    w::session_handle supplied{&session};
    require(supplied.get("ignored").SessionId() == session.SessionId(), "supplied session replaced");
  }
  auto independent = sdk::MidiSession::Create(L"libremidi isolation regression");
  require(independent != nullptr, "could not create independent session");

  input_log zeros, fives;
  std::promise<void> entered, resume;
  auto release = resume.get_future();
  auto zero = std::make_shared<w::port_receiver>(libremidi::ump_input_configuration{
      .on_message = [&](libremidi::ump&& m) {
        zeros.record(m.data[0]);
        entered.set_value();
        require(release.wait_for(5s) == std::future_status::ready, "callback not released");
      }, .midi1_channel_events_to_midi2 = false}, 0);
  auto five = std::make_shared<w::port_receiver>(libremidi::ump_input_configuration{
      .on_message = [&](libremidi::ump&& m) { fives.record(m.data[0]); },
      .midi1_channel_events_to_midi2 = false}, 5);

  std::barrier start{2};
  auto concurrent = std::async(std::launch::async, [&] {
    w::midi_apartment mta;
    start.arrive_and_wait();
    return w::ordinary_connection::acquire(session, b, zero);
  });
  start.arrive_and_wait();
  auto keep = w::ordinary_connection::acquire(session, b, five);
  auto closing = concurrent.get();
  require(keep == closing && session.Connections().Size() == 1, "duplicate connection");
  auto isolated = w::ordinary_connection::acquire(independent, b);
  require(keep->endpoint.ConnectionId() != isolated->endpoint.ConnectionId(), "sessions accidentally share connection");
  auto sender = w::ordinary_connection::acquire(session, a);
  auto send = [&](uint32_t word) {
    const auto result = sender->endpoint.SendSingleMessagePacket(sdk::MidiMessage32{0, word});
    require(sdk::MidiEndpointConnection::SendMessageSucceeded(result), "diagnostic send failed");
  };
  constexpr uint32_t z = 0x20903C40, f = 0x25903D40;
  send(z);
  require(entered.get_future().wait_for(5s) == std::future_status::ready, "group zero receive timeout");
  std::promise<void> requested;
  auto drained = std::async(std::launch::async, [&] {
    w::midi_apartment mta;
    requested.set_value();
    zero->gate.stop();
    zero.reset();
    closing.reset();
  });
  requested.get_future().wait();
  require(drained.wait_for(10ms) != std::future_status::ready, "close did not drain callback");
  resume.set_value();
  drained.get();
  send(z);
  send(f);
  fives.wait(1);
  require(zeros.equals({z}) && fives.equals({f}), "incorrect group routing after close");
  require(session.Connections().Size() == 2, "closing one input disconnected a sibling");

  // Destroy the last input from its own callback. Teardown may not join the
  // calling SDK thread. Reopening the same key waits for physical disconnect.
  five->gate.stop();
  five.reset();
  std::promise<void> self_closed;
  std::shared_ptr<w::port_receiver> self;
  self = std::make_shared<w::port_receiver>(libremidi::ump_input_configuration{
      .on_message = [&](libremidi::ump&&) {
        self->gate.stop();
        self.reset();
        keep.reset();
        self_closed.set_value();
      }}, 5);
  w::ordinary_connection::acquire(session, b, self);
  send(f);
  require(self_closed.get_future().wait_for(5s) == std::future_status::ready, "self-close deadlock");
  auto reopened = w::ordinary_connection::acquire(session, b);
  reopened.reset();
  sender.reset();
  isolated.reset();
  require(session.IsOpen() && session.Connections().Size() == 0 && independent.Connections().Size() == 0,
          "last release leaked a connection or closed a supplied session");
  require_sta();
  std::puts("PASS: concurrent shared open; session isolation; group routing; draining close; self-close; last disconnect; STA preserved");
  return 0;
}
catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
