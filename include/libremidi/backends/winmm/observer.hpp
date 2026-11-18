#pragma once
#include <libremidi/backends/winmm/config.hpp>
#include <libremidi/backends/winmm/helpers.hpp>
#include <libremidi/backends/winmm/observer_worker.hpp>
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

  // A callback may destroy its automatic observer in the middle of a diff.
  virtual bool callbacks_enabled() const noexcept { return true; }

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
        if (iter == prevList.end() && callbacks_enabled())
          portAddedFunc(port);
      }
    }

    if (portRemovedFunc)
    {
      for (const auto& port : prevList)
      {
        auto iter
            = std::ranges::find(currList, port.display_name, &port_information::display_name);
        if (iter == currList.end() && callbacks_enabled())
          portRemovedFunc(port);
      }
    }
  }

  std::vector<input_port> inputPortList;
  std::vector<output_port> outputPortList;
};
}

NAMESPACE_LIBREMIDI::winmm
{
class observer_threaded final : public observer_api
{
  // Registration must precede even the synchronous initial snapshot. CM
  // callbacks only signal events and cannot observe a partially built state.
  struct state : observer_worker, observer_winmm
  {
    state(observer_configuration&& conf, winmm_observer_configuration&& api)
        : observer_worker{conf.input_added || conf.input_removed,
                          conf.output_added || conf.output_removed}
        , observer_winmm{std::move(conf), std::move(api)}
    {
    }

    bool callbacks_enabled() const noexcept override { return !stopped(); }

    void watch()
    {
      run(configuration.poll_period, [this](bool input) {
        if (input)
          check_input_ports<true>();
        else
          check_output_ports<true>();
      });
    }
  };

  std::shared_ptr<state> data;
  std::thread worker;

public:
  observer_threaded(observer_configuration&& conf, winmm_observer_configuration&& api)
      : data{std::make_shared<state>(std::move(conf), std::move(api))}
      , worker{[s = data] { s->watch(); }}
  {
  }

  ~observer_threaded()
  {
    data->stop();
    // If destroyed by its own callback, the worker holds the state until
    // that callback returns. Otherwise join before returning to the owner.
    if (worker.get_id() == std::this_thread::get_id())
      worker.detach();
    else
      worker.join();
  }

  libremidi::API get_current_api() const noexcept override { return data->get_current_api(); }
  std::vector<input_port> get_input_ports() const noexcept override { return data->get_input_ports(); }
  std::vector<output_port> get_output_ports() const noexcept override { return data->get_output_ports(); }
};
}

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
  else if (!conf.has_callbacks())
    return std::make_unique<observer_winmm>(std::move(conf), std::move(api));
  else
    return std::make_unique<winmm::observer_threaded>(std::move(conf), std::move(api));
}
}
