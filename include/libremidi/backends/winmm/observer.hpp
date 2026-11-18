#pragma once
#include <libremidi/backends/winmm/config.hpp>
#include <libremidi/backends/winmm/helpers.hpp>
#include <libremidi/detail/observer.hpp>

#include <algorithm>
#include <thread>

NAMESPACE_LIBREMIDI
{

class observer_winmm : public observer_api
{
public:
  struct
      : observer_configuration
      , winmm_observer_configuration
  {
  } configuration;

  explicit observer_winmm(observer_configuration&& conf, winmm_observer_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    if (!configuration.has_callbacks())
      return;

    if (configuration.notify_in_constructor)
      check_new_ports<true>();
    else
      check_new_ports<false>();
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::WINDOWS_MM; }

  std::vector<input_port> get_input_ports() const noexcept override
  {
    std::vector<input_port> ports;
    winmm::get_port_list(ports);
    return ports;
  }

  std::vector<output_port> get_output_ports() const noexcept override
  {
    std::vector<output_port> ports;
    winmm::get_port_list(ports);
    return ports;
  }

protected:
  template <bool Notify>
  void check_new_ports()
  {
    check_input_ports<Notify>();
    check_output_ports<Notify>();
  }

  template <bool Notify>
  void check_input_ports()
  {
    if (!configuration.input_added && !configuration.input_removed)
      return;
    std::vector<input_port> currInputPortList;
    if (!winmm::get_port_list(currInputPortList))
      return; // An incomplete snapshot is not a removal.

    if constexpr (Notify)
    {
      compare_port_lists_and_notify_clients(
          inputPortList, currInputPortList, configuration.input_added,
          configuration.input_removed);
    }
    inputPortList = std::move(currInputPortList);
  }

  template <bool Notify>
  void check_output_ports()
  {
    if (!configuration.output_added && !configuration.output_removed)
      return;
    std::vector<output_port> currOutputPortList;
    if (!winmm::get_port_list(currOutputPortList))
      return;

    if constexpr (Notify)
    {
      compare_port_lists_and_notify_clients(
          outputPortList, currOutputPortList, configuration.output_added,
          configuration.output_removed);
    }
    outputPortList = std::move(currOutputPortList);
  }

  void compare_port_lists_and_notify_clients(
      const auto& prevList, const auto& currList, const auto& portAddedFunc,
      const auto& portRemovedFunc)
  {
    if (portAddedFunc)
    {
      for (const auto& port : currList)
      {
        auto iter
            = std::ranges::find(prevList, port.display_name, &port_information::display_name);
        if (iter == prevList.end())
          portAddedFunc(port);
      }
    }

    if (portRemovedFunc)
    {
      for (const auto& port : prevList)
      {
        auto iter
            = std::ranges::find(currList, port.display_name, &port_information::display_name);
        if (iter == currList.end())
          portRemovedFunc(port);
      }
    }
  }

  std::vector<input_port> inputPortList;
  std::vector<output_port> outputPortList;
};
}

#if __has_include(<stop_token>) && __cpp_lib_jthread >= 201911L
  #include <stop_token>
NAMESPACE_LIBREMIDI::winmm
{
class observer_threaded final : public observer_winmm
{
public:
  explicit observer_threaded(observer_configuration&& conf, winmm_observer_configuration&& apiconf)
      : observer_winmm{std::move(conf), std::move(apiconf)}
  {
    thread = std::jthread([this](std::stop_token tk) {
      while (!tk.stop_requested())
      {
        check_new_ports<true>();
        std::this_thread::sleep_for(this->configuration.poll_period);
      }
    });
  }

private:
  std::jthread thread;
};
}
#else
  #include <atomic>
  #include <semaphore>
NAMESPACE_LIBREMIDI::winmm
{
class observer_threaded final : public observer_winmm
{
public:
  explicit observer_threaded(observer_configuration&& conf, winmm_observer_configuration&& apiconf)
      : observer_winmm{std::move(conf), std::move(apiconf)}
      , sema{0}
  {
    thread = std::thread([this] {
      while (!stop_flag.test(std::memory_order_acquire))
      {
        check_new_ports<true>();
        std::this_thread::sleep_for(this->configuration.poll_period);
      }
      sema.release();
    });
  }

  ~observer_threaded()
  {
    stop_flag.test_and_set();
    sema.acquire();
    thread.join();
  }

private:
  std::thread thread;
  std::atomic_flag stop_flag = ATOMIC_FLAG_INIT;
  std::binary_semaphore sema;
};
}
#endif

NAMESPACE_LIBREMIDI::winmm
{
class observer_manual final : public observer_winmm
{
public:
  explicit observer_manual(observer_configuration&& conf, winmm_observer_configuration&& apiconf)
      : observer_winmm{std::move(conf), std::move(apiconf)}
  {
    this->configuration.manual_poll({.callback = [this] { this->check_new_ports<true>(); }});
  }

  ~observer_manual() { }
};
}
NAMESPACE_LIBREMIDI
{
template <>
inline std::unique_ptr<observer_api> make<observer_winmm>(
    libremidi::observer_configuration&& conf, libremidi::winmm_observer_configuration&& api)
{
  if (!conf.has_callbacks())
    return std::make_unique<observer_winmm>(std::move(conf), std::move(api));
  if (api.manual_poll)
    return std::make_unique<winmm::observer_manual>(std::move(conf), std::move(api));
  else
    return std::make_unique<winmm::observer_threaded>(std::move(conf), std::move(api));
}
}
