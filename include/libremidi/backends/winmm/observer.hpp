#pragma once
#include <libremidi/backends/winmm/config.hpp>
#include <libremidi/backends/winmm/helpers.hpp>
#include <libremidi/detail/observer.hpp>

#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>

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
      const std::vector<std::string>& prevList, const std::vector<std::string>& currList,
      const libremidi::port_callback& portAddedFunc,
      const libremidi::port_callback& portRemovedFunc)
  {
    if (portAddedFunc)
    {
      for (const auto& portName : currList)
      {
        auto iter = std::find(prevList.begin(), prevList.end(), portName);
        if (iter == prevList.end())
        {
          portAddedFunc(port_information{
              .client = 0,
              .port = 0,
              .manufacturer = "",
              .device_name = "",
              .port_name = portName,
              .display_name = ""});
        }
      }
    }

    if (portRemovedFunc)
    {
      for (const auto portName : prevList)
      {
        auto iter = std::find(currList.begin(), currList.end(), portName);
        if (iter == currList.end())
        {
          portRemovedFunc(port_information{
              .client = 0,
              .port = 0,
              .manufacturer = "",
              .device_name = "",
              .port_name = portName,
              .display_name = ""});
        }
      }
    }
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::WINDOWS_MM; }
  std::vector<port_information> get_input_ports() const noexcept override
  {
    std::vector<port_information> ret;
    for (auto&& portName : get_port_list(INPUT))
    {
      ret.push_back(port_information{
          .client = 0,
          .port = 0,
          .manufacturer = "",
          .device_name = "",
          .port_name = portName,
          .display_name = portName });
    }
    return ret;
  }

  std::vector<port_information> get_output_ports() const noexcept override
  {
    std::vector<port_information> ret;
    for (auto&& portName : get_port_list(OUTPUT))
    {
      ret.push_back(port_information{
          .client = 0,
          .port = 0,
          .manufacturer = "",
          .device_name = "",
          .port_name = portName,
          .display_name = portName });
    }
    return ret;
  }

  std::vector<std::string> get_port_list(bool input) const noexcept
  {
    // true Get input, false get output
    std::vector<std::string> portList;
    unsigned int nDevices = input ? midiInGetNumDevs() : midiOutGetNumDevs();
    std::string portName;

    for (unsigned int ix = 0; ix < nDevices; ++ix)
    {
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

  static constexpr bool INPUT = true;
  static constexpr bool OUTPUT = false;

  std::jthread thread;

  std::vector<std::string> inputPortList;
  std::vector<std::string> outputPortList;
};
}
