#pragma once
#include <libremidi/config.hpp>

#include <windows.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <iterator>
#include <system_error>

NAMESPACE_LIBREMIDI::winmm
{
class observer_worker
{
  using clock = std::chrono::steady_clock;
  using milliseconds = std::chrono::milliseconds;

  // cfgmgr32.h only declares the notification API for a Windows 8 target, and
  // some MinGW SDKs lack it. The entry points are resolved at run time, so
  // their ABI is restated here: neither the availability of notifications nor
  // the layout of this class depend on the WINVER of the including file.
  // Only a build for Windows XP leaves the registration out, and with it the
  // loader imports that such a build is not otherwise checked for.
  struct notify_filter // CM_NOTIFY_FILTER
  {
    DWORD size;
    DWORD flags;
    DWORD type; // 0: CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE
    DWORD reserved;
    union
    {
      GUID interface_class;
      HANDLE target;
      WCHAR instance_id[200]; // MAX_DEVICE_ID_LEN
    } u;
  };
  // CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL and CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL
  static constexpr DWORD interface_arrival = 0, interface_removal = 1;
  using notify_callback = DWORD(CALLBACK*)(void*, void*, DWORD, void*, DWORD);
  using register_fn = DWORD(WINAPI*)(notify_filter*, void*, notify_callback, void**);
  using unregister_fn = DWORD(WINAPI*)(void*);
#if defined(CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES)
  static_assert(sizeof(notify_filter) == sizeof(CM_NOTIFY_FILTER)
                && offsetof(notify_filter, type) == offsetof(CM_NOTIFY_FILTER, FilterType)
                && offsetof(notify_filter, u) == offsetof(CM_NOTIFY_FILTER, u)
                && CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL == interface_arrival
                && CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL == interface_removal
                && CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE == 0 && CR_SUCCESS == 0);
#endif

  // The first handle is always the stop event (highest wait priority). Each
  // successful registration appends one auto-reset event for its direction.
  HANDLE events[3]{};
  DWORD count{1};
  bool is_input[3]{};
  const bool inputs, outputs;

  [[maybe_unused]] HMODULE cfgmgr{};
  [[maybe_unused]] unregister_fn unregister_notification{};
  [[maybe_unused]] void* notifications[3]{};

  static bool supports_midi_interfaces() noexcept
  {
    // CM can register an arbitrary GUID even on Windows 8, before Windows
    // exposed these MIDI interface classes. Don't depend on the host manifest.
    using rtl_get_version = LONG(WINAPI*)(OSVERSIONINFOW*);
    const auto version = std::bit_cast<rtl_get_version>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW os{};
    os.dwOSVersionInfoSize = sizeof(os);
    return version && version(&os) == 0 && os.dwMajorVersion >= 10;
  }

  static DWORD CALLBACK changed(void*, void* event, DWORD action, void*, DWORD) noexcept
  {
    if (action == interface_arrival || action == interface_removal)
      SetEvent(event);
    return ERROR_SUCCESS;
  }

#if !defined(_WIN32_WINNT) || _WIN32_WINNT >= 0x0600
  void subscribe() noexcept
  {
    if (!supports_midi_interfaces())
      return;
    cfgmgr = LoadLibraryExW(L"cfgmgr32.dll", nullptr, 0x00000800 /* LOAD_LIBRARY_SEARCH_SYSTEM32 */);
    if (!cfgmgr)
      return;
    const auto reg = std::bit_cast<register_fn>(
        GetProcAddress(cfgmgr, "CM_Register_Notification"));
    unregister_notification = std::bit_cast<unregister_fn>(
        GetProcAddress(cfgmgr, "CM_Unregister_Notification"));
    if (!reg || !unregister_notification)
      return;

    // DEVINTERFACE_MIDI_INPUT and DEVINTERFACE_MIDI_OUTPUT, including for
    // toolchains whose mmdeviceapi.h does not declare these constants yet.
    static constexpr GUID midi_input{0x504be32c, 0xccf6, 0x4d2c,
                                     {0xb7, 0x3f, 0x6f, 0x8b, 0x37, 0x47, 0xe2, 0x2b}};
    static constexpr GUID midi_output{0x6dc23320, 0xab33, 0x4ce4,
                                      {0x80, 0xd4, 0xbb, 0xb3, 0xeb, 0xbf, 0x28, 0x14}};
    const auto add = [&](bool input, const GUID& guid) {
      notify_filter filter{};
      filter.size = sizeof(filter);
      filter.u.interface_class = guid;
      auto event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      if (!event)
        return false;
      if (reg(&filter, event, &changed, &notifications[count]) != 0 /* CR_SUCCESS */)
      {
        CloseHandle(event);
        return false;
      }
      is_input[count] = input;
      events[count++] = event;
      return true;
    };
    if ((inputs && !add(true, midi_input)) || (outputs && !add(false, midi_output)))
      unsubscribe(); // Partial coverage is not a watcher; poll both subscribed flows.
  }

  void unsubscribe() noexcept
  {
    while (count > 1)
    {
      --count;
      // Drains CM callbacks before their event handle can be closed/reused.
      unregister_notification(notifications[count]);
      CloseHandle(events[count]);
    }
    if (cfgmgr)
    {
      FreeLibrary(cfgmgr);
      cfgmgr = nullptr;
    }
  }
#else
  void subscribe() noexcept { }
  void unsubscribe() noexcept { }
#endif

  static DWORD timeout(milliseconds duration) noexcept
  {
    return static_cast<DWORD>(std::clamp<milliseconds::rep>(duration.count(), 0, INFINITE - 1));
  }

  void poll(milliseconds poll_period, auto& scan)
  {
    while (WaitForSingleObject(events[0], timeout(poll_period)) == WAIT_TIMEOUT)
    {
      if (inputs)
        scan(true);
      if (outputs && !stopped())
        scan(false);
    }
  }

public:
  // WinMM can publish a port some time after its device interface: a
  // notification is followed by scans at these delays, until one sees a change.
  static constexpr milliseconds settle[]{
      milliseconds{100}, milliseconds{400}, milliseconds{1000}};

  // WinMM can also change without any notification. Quiet directions are
  // rechecked at this period, never more often than the application asked for.
  static constexpr milliseconds reconcile_period(milliseconds poll_period) noexcept
  {
    return std::max(poll_period, milliseconds{2000});
  }

  observer_worker(bool inputs, bool outputs)
      : inputs{inputs}, outputs{outputs}
  {
    events[0] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!events[0])
      throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
    subscribe();
  }

  observer_worker(const observer_worker&) = delete;
  observer_worker& operator=(const observer_worker&) = delete;

  ~observer_worker()
  {
    unsubscribe();
    CloseHandle(events[0]);
  }

  void stop() noexcept { SetEvent(events[0]); }
  bool stopped() const noexcept { return WaitForSingleObject(events[0], 0) == WAIT_OBJECT_0; }

  // scan(bool input) returns whether the ports of that direction changed.
  void run(milliseconds poll_period, auto scan)
  {
    if (count == 1)
      return poll(poll_period, scan);

    const auto reconcile = reconcile_period(poll_period);
    constexpr std::size_t settled = std::size(settle);
    struct
    {
      clock::time_point due, hint;
      std::size_t step{settled};
    } flow[3];
    for (DWORD i = 1; i < count; ++i)
      flow[i].due = clock::now() + reconcile;

    for (;;)
    {
      auto next = flow[1].due;
      for (DWORD i = 2; i < count; ++i)
        next = std::min(next, flow[i].due);
      const auto delay = timeout(std::chrono::ceil<milliseconds>(next - clock::now()));
      const auto result = WaitForMultipleObjects(count, events, FALSE, delay);
      if (result == WAIT_OBJECT_0)
        return;
      if (result == WAIT_FAILED)
        return poll(poll_period, scan); // The notifications cannot be waited for anymore.

      const auto now = clock::now();
      if (result != WAIT_TIMEOUT)
      {
        // Coalesce from the first hint: later arrivals cannot postpone a scan.
        auto& f = flow[result - WAIT_OBJECT_0];
        f.hint = now;
        f.step = 0;
        f.due = std::min(f.due, now + settle[0]);
      }
      for (DWORD i = 1; i < count; ++i)
      {
        auto& f = flow[i];
        if (f.due > now || stopped())
          continue;
        const bool found = scan(is_input[i]);
        if (f.step < settled && !found && ++f.step < settled)
          f.due = f.hint + settle[f.step];
        else
        {
          f.step = settled;
          f.due = clock::now() + reconcile;
        }
      }
    }
  }
};
}
