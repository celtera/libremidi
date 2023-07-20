#pragma once
#include <libremidi/backends/winmm/config.hpp>
#include <libremidi/backends/winmm/helpers.hpp>
#include <libremidi/detail/observer.hpp>

#include <condition_variable>
#include <mutex>
#include <thread>

namespace libremidi
{

class observer_winmm final : public observer_api
{
private:
  using CallbackFunc = std::function<void(int, std::string)>;

  std::thread watchThread;
  std::condition_variable watchThreadCV;
  std::mutex watchThreadMutex;
  bool watchThreadShutdown{};

  static constexpr bool INPUT = true;
  static constexpr bool OUTPUT = false;

public:
  using PortList = std::vector<std::string>;

  PortList inputPortList;
  PortList outputPortList;
  explicit observer_winmm(observer::callbacks&& c)
      : observer_api{std::move(c)}
  {
    inputPortList = get_port_list(INPUT);
    outputPortList = get_port_list(OUTPUT);

    watchThreadShutdown = false;
    watchThread = std::thread([this]() { watch_thread(); });
  }

  ~observer_winmm()
  {
    signal_watch_thread_shutdown();
    watchThreadCV.notify_all();
    if (watchThread.joinable())
      watchThread.join();
  }

private:
  void watch_thread()
  {
    while (!wait_for_watch_thread_shutdown_signal(RT_WINMM_OBSERVER_POLL_PERIOD_MS))
    {
      auto currInputPortList = get_port_list(INPUT);
      compare_port_lists_and_notify_clients(
          inputPortList, currInputPortList, callbacks_.input_added, callbacks_.input_removed);
      inputPortList = currInputPortList;

      auto currOutputPortList = get_port_list(OUTPUT);
      compare_port_lists_and_notify_clients(
          outputPortList, currOutputPortList, callbacks_.output_added, callbacks_.output_removed);
      outputPortList = currOutputPortList;
    }
  }

  void compare_port_lists_and_notify_clients(
      const PortList& prevList, const PortList& currList, const CallbackFunc& portAddedFunc,
      const CallbackFunc& portRemovedFunc)
  {
    if (portAddedFunc)
    {
      for (const auto& portName : currList)
      {
        auto iter = std::find(prevList.begin(), prevList.end(), portName);
        if (iter == prevList.end())
          portAddedFunc(0, portName);
      }
    }
    if (portRemovedFunc)
    {
      for (const auto portName : prevList)
      {
        auto iter = std::find(currList.begin(), currList.end(), portName);
        if (iter == currList.end())
          portRemovedFunc(0, portName);
      }
    }
  }

  bool wait_for_watch_thread_shutdown_signal(unsigned int timeoutMs)
  {
    using namespace std::chrono_literals;
    std::unique_lock<std::mutex> lock(watchThreadMutex);
    return watchThreadCV.wait_for(lock, timeoutMs * 1ms, [this]() { return watchThreadShutdown; });
  }

  void signal_watch_thread_shutdown()
  {
    std::lock_guard lock(watchThreadMutex);
    watchThreadShutdown = true;
  }

public:
  PortList get_port_list(bool input) const
  {
    // true Get input, false get output
    PortList portList;
    unsigned int nDevices = input ? midiInGetNumDevs() : midiOutGetNumDevs();
    for (unsigned int ix = 0; ix < nDevices; ++ix)
    {
      std::string portName;
      if (input)
      {
        MIDIINCAPS deviceCaps;
        midiInGetDevCaps(ix, &deviceCaps, sizeof(MIDIINCAPS));
        portName = ConvertToUTF8(deviceCaps.szPname);

#ifndef LIBREMIDI_DO_NOT_ENSURE_UNIQUE_PORTNAMES
        MakeUniqueInPortName(portName, ix);
#endif
      }
      else
      {
        MIDIOUTCAPS deviceCaps;
        midiOutGetDevCaps(ix, &deviceCaps, sizeof(MIDIOUTCAPS));
        portName = ConvertToUTF8(deviceCaps.szPname);

#ifndef LIBREMIDI_DO_NOT_ENSURE_UNIQUE_PORTNAMES
        MakeUniqueOutPortName(portName, ix);
#endif
      }
      portList.push_back(portName);
    }
    return portList;
  }
};

}
