#pragma once
#include <libremidi/config.hpp>

#include <windows.h>
#include <cfgmgr32.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <system_error>

NAMESPACE_LIBREMIDI::winmm
{
class observer_worker
{
  using clock = std::chrono::steady_clock;
  using milliseconds = std::chrono::milliseconds;

  // The first handle is always the stop event (highest wait priority). Each
  // successful registration appends one auto-reset event for its direction.
  HANDLE events[3]{};
  DWORD count{1};
  bool is_input[3]{};
  const bool inputs, outputs;

#if defined(CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES)
  HMODULE cfgmgr{};
  decltype(&CM_Unregister_Notification) unregister_notification{};
  HCMNOTIFICATION notifications[3]{};

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

  static DWORD CALLBACK changed(HCMNOTIFICATION, void* event, CM_NOTIFY_ACTION action,
                                CM_NOTIFY_EVENT_DATA*, DWORD) noexcept
  {
    if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL
        || action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL)
      SetEvent(event);
    return ERROR_SUCCESS;
  }

  void subscribe() noexcept
  {
    if (!supports_midi_interfaces())
      return;
    cfgmgr = LoadLibraryExW(L"cfgmgr32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!cfgmgr)
      return;
    const auto reg = std::bit_cast<decltype(&CM_Register_Notification)>(
        GetProcAddress(cfgmgr, "CM_Register_Notification"));
    unregister_notification = std::bit_cast<decltype(unregister_notification)>(
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
      CM_NOTIFY_FILTER filter{};
      filter.cbSize = sizeof(filter);
      filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
      filter.u.DeviceInterface.ClassGuid = guid;
      auto event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      if (!event)
        return false;
      if (reg(&filter, event, &changed, &notifications[count]) != CR_SUCCESS)
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
#endif

  static DWORD timeout(milliseconds duration) noexcept
  {
    return static_cast<DWORD>(std::clamp<milliseconds::rep>(duration.count(), 0, INFINITE - 1));
  }

public:
  observer_worker(bool inputs, bool outputs)
      : inputs{inputs}, outputs{outputs}
  {
    events[0] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!events[0])
      throw std::system_error(static_cast<int>(GetLastError()), std::system_category());
#if defined(CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES)
    subscribe();
#endif
  }

  observer_worker(const observer_worker&) = delete;
  observer_worker& operator=(const observer_worker&) = delete;

  ~observer_worker()
  {
#if defined(CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES)
    unsubscribe();
#endif
    CloseHandle(events[0]);
  }

  void stop() noexcept { SetEvent(events[0]); }
  bool stopped() const noexcept { return WaitForSingleObject(events[0], 0) == WAIT_OBJECT_0; }

  void run(milliseconds poll_period, auto scan)
  {
    if (count == 1)
    {
      while (WaitForSingleObject(events[0], timeout(poll_period)) == WAIT_TIMEOUT)
      {
        if (inputs)
          scan(true);
        if (outputs && !stopped())
          scan(false);
      }
      return;
    }

    constexpr milliseconds debounce{100}, reconcile{2000};
    constexpr auto never = clock::time_point::max();
    clock::time_point due[3];
    bool final_check[3]{};
    for (DWORD i = 1; i < count; ++i)
    {
      // An interface can already be registered at startup while its WinMM
      // port is still being published. Reconcile the initial snapshot too.
      due[i] = clock::now() + reconcile;
      final_check[i] = true;
    }
    for (;;)
    {
      const auto next = *std::min_element(due + 1, due + count);
      const auto delay = next == never ? INFINITE
                                       : timeout(std::chrono::ceil<milliseconds>(next - clock::now()));
      const auto result = WaitForMultipleObjects(count, events, FALSE, delay);
      if (result == WAIT_OBJECT_0 || result == WAIT_FAILED)
        return;
      const auto now = clock::now();
      if (result != WAIT_TIMEOUT)
      {
        const auto i = result - WAIT_OBJECT_0;
        due[i] = now + debounce;
        final_check[i] = false;
      }
      for (DWORD i = 1; i < count; ++i)
      {
        if (due[i] > now)
          continue;
        due[i] = final_check[i] ? never : now + reconcile;
        final_check[i] = true;
        if (!stopped())
          scan(is_input[i]);
      }
    }
  }
};
}
