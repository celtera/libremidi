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

// Inputs progress from [A] to [B] to [B,C] to [B,C,D]. Outputs start at [X,Y].
std::atomic<int> generation{}, input_scans{}, output_scans{}, output_count{2};
UINT input_size() { ++input_scans; return std::max(generation.load(), 1); }
UINT output_size() { ++output_scans; return output_count; }
MMRESULT input_caps(UINT_PTR i, MIDIINCAPSW* caps, UINT)
{
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

#if defined(CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES)
enum { native, no_export, fail_second } mode = native;
std::atomic<int> registrations{}, unregistrations{};
CONFIGRET (WINAPI* register_cm)(PCM_NOTIFY_FILTER, PVOID, PCM_NOTIFY_CALLBACK, PHCMNOTIFICATION);
CONFIGRET (WINAPI* unregister_cm)(HCMNOTIFICATION);
struct registration
{
  HCMNOTIFICATION handle{};
  void* context{};
  PCM_NOTIFY_CALLBACK callback{};
  void signal(CM_NOTIFY_ACTION action) const
  {
    callback(handle, context, action, nullptr, 0);
  }
} input, output;

CONFIGRET WINAPI subscribe(PCM_NOTIFY_FILTER filter, void* ctx, PCM_NOTIFY_CALLBACK cb,
                           PHCMNOTIFICATION result)
{
  constexpr GUID in{0x504be32c, 0xccf6, 0x4d2c, {0xb7,0x3f,0x6f,0x8b,0x37,0x47,0xe2,0x2b}};
  constexpr GUID out{0x6dc23320, 0xab33, 0x4ce4, {0x80,0xd4,0xbb,0xb3,0xeb,0xbf,0x28,0x14}};
  const auto& guid = filter->u.DeviceInterface.ClassGuid;
  require(filter->FilterType == CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE && !filter->Flags
              && (guid == in || guid == out), "expected MIDI interface class filter");
  if (guid == out && mode == fail_second)
    return CR_FAILURE;
  const auto r = register_cm(filter, ctx, cb, result);
  if (r == CR_SUCCESS)
  {
    (guid == in ? input : output) = {*result, ctx, cb};
    ++registrations;
  }
  return r;
}

CONFIGRET WINAPI unsubscribe(HCMNOTIFICATION h)
{
  const auto r = unregister_cm(h);
  require(r == CR_SUCCESS, "native unregistration failed");
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
#endif
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

#if defined(CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES)
static void test_registration()
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
    return;
  }
  require(active_at_snapshot == 2, "partial registration not rolled back before snapshot");

  const int initial_input = input_scans, initial_output = output_scans;
  generation = 1;
  for (int i = 0; i < 12; ++i)
    input.signal(CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL);
  until([&] { return removed_a == 1 && added_b == 1; }, "same-count A to B swap");
  require(input_scans == initial_input + 1 && output_scans == initial_output,
          "burst should coalesce into one affected-direction scan");

  generation = 2;
  input.signal(CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL);
  until([&] { return added_c == 1; }, "second notification missed progressive port C");
  generation = 3; // No further event: WinMM publishes D after its interface.
  until([&] { return added_d == 1; }, "quiet reconciliation missed late D");
  require(input_scans == initial_input + 3, "unexpected polling between reconciliations");
  output_count = 0;
  output.signal(CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL);
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
}
#endif

int main()
{
  using namespace libremidi;
  using namespace std::chrono;
#if defined(CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES)
  test_registration();
  mode = no_export;
#endif
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
