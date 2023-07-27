#pragma once
#include <libremidi/backends/winmm/config.hpp>
#include <libremidi/backends/winmm/helpers.hpp>
#include <libremidi/detail/observer.hpp>

#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>
#include <ranges>

namespace libremidi
{

class observer_winmm final : public observer_api
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
    thread = std::jthread([this](std::stop_token tk) {
      while (!tk.stop_requested())
      {
        check_new_ports();
        std::this_thread::sleep_for(this->configuration.poll_period);
      }
    });
  }

  ~observer_winmm() { thread.get_stop_source().request_stop(); }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::WINDOWS_MM; }

  std::vector<port_information> get_input_ports() const noexcept override
  {
      return get_port_list(INPUT);
  }

  std::vector<port_information> get_output_ports() const noexcept override
  {
      return get_port_list(OUTPUT);
  }

private:
  void check_new_ports()
  {
    auto currInputPortList = get_port_list(INPUT);
    compare_port_lists_and_notify_clients(
        inputPortList, currInputPortList, configuration.input_added, configuration.input_removed);
    inputPortList = std::move(currInputPortList);

    auto currOutputPortList = get_port_list(OUTPUT);
    compare_port_lists_and_notify_clients(
        outputPortList, currOutputPortList, configuration.output_added,
        configuration.output_removed);
    outputPortList = std::move(currOutputPortList);
  }

  void compare_port_lists_and_notify_clients(
      const std::vector<port_information>& prevList, const std::vector<port_information>& currList,
      const libremidi::port_callback& portAddedFunc,
      const libremidi::port_callback& portRemovedFunc)
  {
    if (portAddedFunc)
    {
      for (const auto& port : currList)
      {
        auto iter = std::ranges::find(prevList, port.display_name, &port_information::display_name);
        if (iter == prevList.end())
          portAddedFunc(port);
      }
    }

    if (portRemovedFunc)
    {
      for (const auto port : prevList)
      {
        auto iter = std::ranges::find(currList, port.display_name, &port_information::display_name);
        if (iter == currList.end())
          portRemovedFunc(port);
      }
    }
  }

  port_information to_in_port_info(std::size_t index) const noexcept
  {
    MIDIINCAPS deviceCaps;
    midiInGetDevCaps(index, &deviceCaps, sizeof(MIDIINCAPS));

    auto rawName = ConvertToUTF8(deviceCaps.szPname);
    auto portName = rawName;
    MakeUniqueInPortName(portName, index);
    return port_information{
          .client = 0,
          .port = index,
          .manufacturer = "",
          .device_name = "",
          .port_name = rawName,
          .display_name = portName };
  }

  port_information to_out_port_info(std::size_t index) const noexcept
  {
    MIDIOUTCAPS deviceCaps;
    midiOutGetDevCaps(index, &deviceCaps, sizeof(MIDIOUTCAPS));

    auto rawName = ConvertToUTF8(deviceCaps.szPname);
    auto portName = rawName;
    MakeUniqueOutPortName(portName, index);
    return port_information{
          .client = 0,
          .port = index,
          .manufacturer = "",
          .device_name = "",
          .port_name = rawName,
          .display_name = portName };
  }

  std::vector<port_information> get_port_list(bool input) const noexcept
  {
    std::vector<port_information> portList;
    std::size_t nDevices = input ? midiInGetNumDevs() : midiOutGetNumDevs();

    for (std::size_t i = 0; i < nDevices; ++i)
    {
      portList.push_back(input ? to_in_port_info(i) : to_out_port_info(i));
    }
    return portList;
  }

  static constexpr bool INPUT = true;
  static constexpr bool OUTPUT = false;

  std::jthread thread;

  std::vector<port_information> inputPortList;
  std::vector<port_information> outputPortList;
};
}
