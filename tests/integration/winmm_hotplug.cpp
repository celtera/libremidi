// Compile the backend in isolation: keep real CM registration, callbacks, waits
// and teardown, but control when the WinMM snapshot changes without toggling a
// user's device. No test hooks in the library or dependencies on MIDI hardware.
#include <libremidi/detail/observer.hpp>
#include <libremidi/backends/winmm/error_domain.hpp>

#include <cfgmgr32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

using namespace std::chrono_literals;
namespace
{
void require(bool ok, const char* message)
{
  if (!ok)
  {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
  }
}

void until(auto condition, const char* message)
{
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!condition() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(5ms);
  require(condition(), message);
}

// Inputs progress from [A] to [B] to [B,C] to [B,C,D]... Outputs start at [X,Y].
std::atomic<int> generation{}, input_scans{}, output_scans{}, output_count{2};
std::atomic<int> caps_delay_ms{}, publish_at_scan{-1};
std::atomic<bool> fail_wait{};
UINT input_size()
{
  if (++input_scans == publish_at_scan)
    ++generation; // WinMM publishes one more input, whenever this scan happens.
  return std::max(generation.load(), 1);
}
UINT output_size() { ++output_scans; return output_count; }
MMRESULT input_caps(UINT_PTR i, MIDIINCAPSW* caps, UINT)
{
  if (const int delay = caps_delay_ms)
    Sleep(delay); // A driver that answers slowly.
  *caps = {};
  caps->szPname[0] = i ? L'B' + static_cast<WCHAR>(i) : (generation == 0 ? L'A' : L'B');
  return MMSYSERR_NOERROR;
}
MMRESULT output_caps(UINT_PTR i, MIDIOUTCAPSW* caps, UINT)
{
  *caps = {};
  caps->szPname[0] = i ? L'Y' : L'X';
  return MMSYSERR_NOERROR;
}

// The notification ABI, spelled without the declarations that cfgmgr32.h hides
// from a pre-Windows-8 target: this test runs whatever WINVER it is built for.
// The backend checks its own copy against the SDK's when both are visible.
constexpr DWORD interface_arrival = 0, interface_removal = 1;
struct cm_filter
{
  DWORD size, flags, type, reserved;
  GUID interface_class;
};
using cm_callback = DWORD (CALLBACK*)(void*, void*, DWORD, void*, DWORD);
enum { native, no_export, fail_second } mode = native;
std::atomic<int> registrations{}, unregistrations{};
DWORD (WINAPI* register_cm)(cm_filter*, void*, cm_callback, void**);
DWORD (WINAPI* unregister_cm)(void*);
struct registration
{
  void* handle{};
  void* context{};
  cm_callback callback{};
  void signal(DWORD action) const
  {
    callback(handle, context, action, nullptr, 0);
  }
} input_cm, output_cm;

DWORD WINAPI subscribe(cm_filter* filter, void* ctx, cm_callback cb, void** result)
{
  constexpr GUID in{0x504be32c, 0xccf6, 0x4d2c, {0xb7,0x3f,0x6f,0x8b,0x37,0x47,0xe2,0x2b}};
  constexpr GUID out{0x6dc23320, 0xab33, 0x4ce4, {0x80,0xd4,0xbb,0xb3,0xeb,0xbf,0x28,0x14}};
  const auto& guid = filter->interface_class;
  require(filter->size == 416 && filter->type == 0 && !filter->flags
              && (guid == in || guid == out), "expected MIDI interface class filter");
  if (guid == out && mode == fail_second)
    return 0x13; // CR_FAILURE
  const auto r = register_cm(filter, ctx, cb, result);
  if (r == 0)
  {
    (guid == in ? input_cm : output_cm) = {*result, ctx, cb};
    ++registrations;
  }
  return r;
}

DWORD WINAPI unsubscribe(void* h)
{
  const auto r = unregister_cm(h);
  require(r == 0, "native unregistration failed");
  ++unregistrations;
  return r;
}

FARPROC WINAPI resolve(HMODULE module, const char* name)
{
  const auto symbol = GetProcAddress(module, name);
  if (!std::strcmp(name, "CM_Register_Notification"))
  {
    register_cm = reinterpret_cast<decltype(register_cm)>(symbol);
    return symbol && mode != no_export ? reinterpret_cast<FARPROC>(&subscribe) : nullptr;
  }
  if (!std::strcmp(name, "CM_Unregister_Notification"))
  {
    unregister_cm = reinterpret_cast<decltype(unregister_cm)>(symbol);
    return symbol ? reinterpret_cast<FARPROC>(&unsubscribe) : nullptr;
  }
  return symbol;
}
#define GetProcAddress resolve

DWORD wait_many(DWORD count, const HANDLE* handles, BOOL all, DWORD ms)
{
  if (fail_wait)
  {
    SetLastError(ERROR_INVALID_HANDLE);
    return WAIT_FAILED;
  }
  return WaitForMultipleObjects(count, handles, all, ms);
}
#define WaitForMultipleObjects wait_many
}

#define midiInGetNumDevs input_size
#define midiOutGetNumDevs output_size
#define midiInGetDevCapsW input_caps
#define midiOutGetDevCapsW output_caps
#include <libremidi/backends/winmm/observer.hpp>
#undef midiInGetNumDevs
#undef midiOutGetNumDevs
#undef midiInGetDevCapsW
#undef midiOutGetDevCapsW
#undef GetProcAddress
#undef WaitForMultipleObjects

using worker = libremidi::winmm::observer_worker;
static_assert(worker::reconcile_period(20ms) == 2s, "fast polls must not speed up reconciliation");
static_assert(worker::reconcile_period(2s) == 2s);
static_assert(worker::reconcile_period(10s) == 10s, "reconciliation outpaces a slow poll_period");
static_assert(worker::reconcile_period(1h) == 1h);

static auto since(std::chrono::steady_clock::time_point start)
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start);
}

// Each observer below watches inputs only, and is alone: `input_cm` is its registration.
static void test_settling()
{
  using namespace libremidi;
  std::atomic<int> added{};
  observer_configuration conf;
  conf.notify_in_constructor = false;
  conf.input_added = [&](const input_port&) { ++added; };
  auto obs = make<observer_winmm>(std::move(conf), winmm_observer_configuration{.poll_period = 20ms});

  // WinMM publishes the port after the first scan that follows its interface.
  // The next scan is due 400 ms after the hint; a reconciliation, 2.1 s after.
  int scans = input_scans;
  publish_at_scan = scans + 2;
  auto start = std::chrono::steady_clock::now();
  input_cm.signal(interface_arrival);
  until([&] { return added == 1; }, "late port missed");
  require(since(start) < 1500ms, "late port was left to the slow reconciliation");
  std::this_thread::sleep_for(1400ms - since(start));
  require(input_scans == scans + 2, "settling continued after it found the change");

  // Nothing changes: the settling scans are bounded.
  scans = input_scans;
  start = std::chrono::steady_clock::now();
  input_cm.signal(interface_removal);
  until([&] { return input_scans == scans + 3; }, "expected three settling scans for a quiet hint");
  require(since(start) < 1900ms, "settling scans were left to the slow reconciliation");
  std::this_thread::sleep_for(600ms);
  require(input_scans == scans + 3, "settling scans are not bounded");
  require(added == 1, "quiet hint reported a port");
  std::puts("settling: late port found by the second of three bounded rescans");
}

static void test_slow_poll_period()
{
  using namespace libremidi;
  std::atomic<int> added{};
  observer_configuration conf;
  conf.notify_in_constructor = false;
  conf.input_added = [&](const input_port&) { ++added; };
  auto obs = make<observer_winmm>(std::move(conf), winmm_observer_configuration{.poll_period = 1h});
  const int scans = input_scans;
  std::this_thread::sleep_for(2300ms);
  require(input_scans == scans, "idle watcher scanned more often than poll_period");
  ++generation;
  input_cm.signal(interface_arrival);
  until([&] { return added == 1; }, "notification ignored with a slow poll_period");
  const auto start = std::chrono::steady_clock::now();
  obs.reset();
  require(since(start) < 1s, "slow poll_period blocked shutdown");
  std::puts("poll_period: a one-hour period is not reconciled every two seconds; notifications still scan");
}

static void test_wait_failure()
{
  using namespace libremidi;
  std::atomic<int> added{};
  observer_configuration conf;
  conf.notify_in_constructor = false;
  conf.input_added = [&](const input_port&) { ++added; };
  auto obs = make<observer_winmm>(std::move(conf), winmm_observer_configuration{.poll_period = 20ms});
  const int scans = input_scans;
  fail_wait = true;
  input_cm.signal(interface_arrival); // Wakes the worker: its next wait fails.
  // Settling and reconciliation together could not scan this often.
  const auto start = std::chrono::steady_clock::now();
  until([&] { return input_scans > scans + 12; }, "watcher ended instead of polling after a failed wait");
  require(since(start) < 2s, "fallback did not poll at poll_period");
  ++generation; // Without a notification.
  until([&] { return added == 1; }, "fallback polling missed a port");
  fail_wait = false;
  obs.reset();
  std::puts("wait failure: watcher keeps polling at poll_period");
}

static void test_slow_drivers()
{
  using namespace libremidi;
  observer_configuration conf;
  conf.notify_in_constructor = false;
  conf.input_added = [](const input_port&) {};
  generation = 8; // Eight ports of 250 ms each: a full scan lasts two seconds.
  auto obs = make<observer_winmm>(std::move(conf), winmm_observer_configuration{.poll_period = 20ms});
  const int scans = input_scans;
  caps_delay_ms = 250;
  input_cm.signal(interface_arrival);
  until([&] { return input_scans > scans; }, "no scan started");
  std::this_thread::sleep_for(200ms); // Inside the scan.
  const auto start = std::chrono::steady_clock::now();
  obs.reset();
  const auto took = since(start);
  caps_delay_ms = 0;
  require(took < 1s, "destruction waited for the whole scan of slow drivers");
  std::puts("slow drivers: destruction waits for one capability read, not the scan");
}

static bool test_registration()
{
  using namespace libremidi;
  int active_at_snapshot{-1};
  std::atomic<int> added_b{}, added_c{}, added_d{}, removed_a{}, removed_outputs{};
  std::unique_ptr<observer_api> obs;
  const auto caller = std::this_thread::get_id();
  observer_configuration conf;
  conf.input_added = [&](const input_port& p) {
    if (p.port_name == "A")
    {
      require(caller == std::this_thread::get_id(), "initial snapshot was not synchronous");
      active_at_snapshot = registrations - unregistrations;
    }
    else
    {
      require(caller != std::this_thread::get_id(), "scan ran on the notification caller");
      if (p.port_name == "B") ++added_b;
      if (p.port_name == "C") ++added_c;
      if (p.port_name == "D") ++added_d;
    }
  };
  conf.input_removed = [&](const input_port& p) { if (p.port_name == "A") ++removed_a; };
  conf.output_removed = [&](const output_port&) { ++removed_outputs; obs.reset(); };
  obs = make<observer_winmm>(std::move(conf), winmm_observer_configuration{.poll_period = 20ms});

  require(active_at_snapshot == registrations - unregistrations,
          "registration must finish before the initial snapshot");
  if (active_at_snapshot == 0)
  {
    generation = 1;
    until([&] { return removed_a == 1 && added_b == 1; }, "native fallback missed A to B swap");
    std::puts("native registration unavailable: unsignalled A->B delivered by fallback");
    return false;
  }
  require(active_at_snapshot == 2, "partial registration not rolled back before snapshot");

  const int initial_input = input_scans, initial_output = output_scans;
  generation = 1;
  for (int i = 0; i < 12; ++i)
    input_cm.signal(interface_arrival);
  until([&] { return removed_a == 1 && added_b == 1; }, "same-count A to B swap");
  require(input_scans == initial_input + 1 && output_scans == initial_output,
          "burst should coalesce into one affected-direction scan");

  generation = 2;
  input_cm.signal(interface_arrival);
  until([&] { return added_c == 1; }, "second notification missed progressive port C");
  generation = 3; // No further event: WinMM publishes D after its interface.
  until([&] { return added_d == 1; }, "quiet reconciliation missed late D");
  require(input_scans == initial_input + 3, "unexpected polling between reconciliations");
  output_count = 0;
  output_cm.signal(interface_removal);
  until([&] { return unregistrations == registrations; }, "destroying observer from callback");
  require(removed_outputs == 1, "later callback delivered after self-destruction");
  std::puts("native: both MIDI GUIDs registered before snapshot; burst A->B, progressive C, late D, removal-only and self-stop passed");

  mode = fail_second;
  {
    observer_configuration both;
    both.notify_in_constructor = false;
    both.input_removed = [](const input_port&) {};
    std::atomic<bool> removed{};
    both.output_removed = [&](const output_port&) { removed = true; };
    output_count = 2;
    auto partial = make<observer_winmm>(std::move(both), winmm_observer_configuration{.poll_period = 20ms});
    require(registrations == unregistrations, "partial registration not rolled back");
    output_count = 0;
    until([&] { return removed.load(); }, "failed direction was not covered by polling");
  }
  mode = native;
  return true;
}

int main()
{
  using namespace libremidi;
  using namespace std::chrono;
  if (test_registration())
  {
    test_settling();
    test_slow_poll_period();
    test_wait_failure();
    test_slow_drivers();
  }
  mode = no_export;
  generation = 0;
  std::atomic<int> removals{};
  observer_configuration fallback;
  fallback.notify_in_constructor = false;
  fallback.input_removed = [&](const input_port& p) { if (p.port_name == "A") ++removals; };
  {
    auto obs = make<observer_winmm>(std::move(fallback), winmm_observer_configuration{.poll_period = 20ms});
    generation = 1;
    until([&] { return removals == 1; }, "fallback missed same-count swap without a notification");
  }
  {
    observer_configuration slow;
    slow.output_added = [](const output_port&) {};
    auto obs = make<observer_winmm>(std::move(slow), winmm_observer_configuration{.poll_period = 1h});
    std::this_thread::sleep_for(20ms);
    const auto start = steady_clock::now();
    obs.reset();
    require(steady_clock::now() - start < 1s, "slow fallback poll blocked shutdown");
  }
  std::puts("fallback: same-count swap without notification and interruptible one-hour wait passed");

  generation = 0;
  observer_configuration manual;
  manual.notify_in_constructor = false;
  manual.input_removed = [&](const input_port& p) { if (p.port_name == "A") ++removals; };
  std::function<void()> poll;
  winmm_observer_configuration api;
  api.poll_period = 1ms;
  api.manual_poll = [&](const poll_parameters& p) { poll = p.callback; };
  auto obs = make<observer_winmm>(std::move(manual), std::move(api));
  const auto initial = input_scans.load();
  generation = 1;
  std::this_thread::sleep_for(20ms);
  require(input_scans == initial && removals == 1, "manual observer started a worker");
  require(bool(poll), "manual observer did not supply a poll function");
  poll();
  require(removals == 2, "manual poll missed same-count removal");
  std::puts("manual: no automatic scans; caller-triggered removal passed");
}
