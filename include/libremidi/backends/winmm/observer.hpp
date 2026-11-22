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

    check_new_ports();
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
  void check_new_ports()
  {
    check_input_ports();
    check_output_ports();
  }

  void check_input_ports()
  {
    check_ports(
        inputPortList, notifyInputs, configuration.input_added, configuration.input_removed);
  }

  void check_output_ports()
  {
    check_ports(
        outputPortList, notifyOutputs, configuration.output_added, configuration.output_removed);
  }

  template <typename Port>
  void check_ports(
      std::vector<Port>& previous, bool& notify, const auto& added, const auto& removed)
  {
    if (!added && !removed)
      return;

    // A device that cannot be read is absent from the scan, not a reason to
    // hide the other ones.
    std::vector<Port> current;
    winmm::get_port_list(current);

    if (notify)
    {
      // For listed ports the numbered name identifies the occurrence.
      auto report_missing = [](const auto& from, const auto& other, const auto& report) {
        if (!report)
          return;
        for (const auto& port : from)
        {
          if (std::ranges::none_of(other, [&](const auto& p) {
                return p.display_name == port.display_name && p.device == port.device;
              }))
            report(port);
        }
      };
      report_missing(previous, current, removed);
      report_missing(current, previous, added);
    }
    previous = std::move(current);
    notify = true;
  }

  std::vector<input_port> inputPortList;
  std::vector<output_port> outputPortList;
  bool notifyInputs = configuration.notify_in_constructor;
  bool notifyOutputs = configuration.notify_in_constructor;
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
        check_new_ports();
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
        check_new_ports();
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
    this->configuration.manual_poll({.callback = [this] { this->check_new_ports(); }});
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
