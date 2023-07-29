#pragma once
#include <libremidi/backends/winuwp/config.hpp>
#include <libremidi/detail/observer.hpp>

namespace libremidi
{
class observer_winuwp_internal
{
public:
  struct port_info
  {
    hstring id;
    hstring name;
  };
  struct callback
  {
    int token{};
    std::function<void(const port_info&)> function;
  };
  struct callbacks
  {
    std::vector<callback> cbs;
    int current_token{};
    void operator()(const port_info& p)
    {
      for (auto& cb : cbs)
        cb.function(p);
    }

    int add(std::function<void(const port_info&)> f)
    {
      int tk = current_token++;
      cbs.emplace_back(tk, f);
      return tk;
    }
    void remove(int tk)
    {
      auto it = std::remove_if(
          cbs.begin(), cbs.end(), [tk](const callback& c) { return c.token == tk; });
      auto r = std::distance(it, cbs.end());
      cbs.erase(it, cbs.end());
    }
  };

  explicit observer_winuwp_internal(hstring deviceSelector) { initialize(deviceSelector); }
  ~observer_winuwp_internal() { terminate(); }

  std::vector<port_info> get_ports() const
  {
    std::lock_guard<std::mutex> lock(portListMutex_);
    return portList_;
  }

  unsigned int get_port_count() const
  {
    std::lock_guard<std::mutex> lock(portListMutex_);
    return static_cast<unsigned int>(portList_.size());
  }

  bool get_port_info(unsigned int portNumber, port_info& portInfo) const
  {
    std::lock_guard<std::mutex> lock(portListMutex_);
    if (portNumber >= portList_.size())
      return false;
    portInfo = portList_[portNumber];
    return true;
  }

  hstring get_port_id(unsigned int portNumber) const
  {
    std::lock_guard<std::mutex> lock(portListMutex_);
    return portNumber < portList_.size() ? portList_[portNumber].id : hstring{};
  }

  std::string get_port_name(unsigned int portNumber) const
  {
    std::lock_guard<std::mutex> lock(portListMutex_);
    return portNumber < portList_.size() ? to_string(portList_[portNumber].name) : std::string{};
  }

  int PortAdded(const std::function<void(port_info)>& handler)
  {
    return portAddedEvent_.add(handler);
  }

  void PortAdded(int token) noexcept { portAddedEvent_.remove(token); }

  int PortRemoved(const std::function<void(port_info)>& handler)
  {
    return portRemovedEvent_.add(handler);
  }

  void PortRemoved(int token) noexcept { portRemovedEvent_.remove(token); }

private:
  observer_winuwp_internal(const observer_winuwp_internal&) = delete;
  observer_winuwp_internal& operator=(const observer_winuwp_internal&) = delete;

private:
  void initialize(hstring deviceSelector)
  {
    deviceWatcher_ = DeviceInformation::CreateWatcher(deviceSelector);

    evTokenOnDeviceAdded_
        = deviceWatcher_.Added({this, &observer_winuwp_internal::on_device_added});
    evTokenOnDeviceRemoved_
        = deviceWatcher_.Removed({this, &observer_winuwp_internal::on_device_removed});
    evTokenOnDeviceUpdated_
        = deviceWatcher_.Updated({this, &observer_winuwp_internal::on_device_updated});
    evTokenOnDeviceEnumerationCompleted_ = deviceWatcher_.EnumerationCompleted(
        {this, &observer_winuwp_internal::on_device_enumeration_completed});

    deviceWatcher_.Start();
  }

  void terminate()
  {
    deviceWatcher_.Stop();
    deviceWatcher_.EnumerationCompleted(evTokenOnDeviceEnumerationCompleted_);
    deviceWatcher_.Updated(evTokenOnDeviceUpdated_);
    deviceWatcher_.Removed(evTokenOnDeviceRemoved_);
    deviceWatcher_.Added(evTokenOnDeviceAdded_);
  }

  void on_device_added(DeviceWatcher sender, DeviceInformation deviceInfo)
  {
    port_info p;
    {
      std::lock_guard<std::mutex> lock(portListMutex_);
      p = port_info{deviceInfo.Id(), deviceInfo.Name()};
      portList_.push_back(p);
    }
    portAddedEvent_(p);
  }

  void on_device_removed(DeviceWatcher sender, DeviceInformationUpdate deviceUpdate)
  {
    const auto id = deviceUpdate.Id();
    auto pred = [&id](const port_info& portInfo) { return portInfo.id == id; };
    std::optional<port_info> p;
    hstring name;
    {
      std::lock_guard<std::mutex> lock(portListMutex_);
      auto iter = std::find_if(portList_.begin(), portList_.end(), pred);
      if (iter != portList_.end())
      {
        p = *iter;
        portList_.erase(iter);
      }
    }
    if (p)
      portRemovedEvent_(*p);
  }

  void on_device_updated(DeviceWatcher sender, DeviceInformationUpdate deviceUpdate) { }

  void on_device_enumeration_completed(DeviceWatcher sender, IInspectable const&) { }

private:
  std::vector<port_info> portList_;
  mutable std::mutex portListMutex_;

  DeviceWatcher deviceWatcher_{nullptr};
  event_token evTokenOnDeviceAdded_;
  event_token evTokenOnDeviceRemoved_;
  event_token evTokenOnDeviceUpdated_;
  event_token evTokenOnDeviceEnumerationCompleted_;

  callbacks portAddedEvent_;
  callbacks portRemovedEvent_;
};

class observer_winuwp final : public observer_api
{
public:
  struct
      : observer_configuration
      , winuwp_observer_configuration
  {
  } configuration;

  using port_info = observer_winuwp_internal::port_info;
  explicit observer_winuwp(observer_configuration&& conf, winuwp_observer_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    if (!configuration.has_callbacks())
      return;

    if (configuration.notify_in_constructor)
    {
      if (configuration.input_added)
        for (const auto& p : get_input_ports())
          configuration.input_added(p);

      if (configuration.output_added)
        for (const auto& p : get_output_ports())
          configuration.output_added(p);
    }

    evTokenOnInputAdded_
        = internalInPortObserver_.PortAdded([this](const port_info& p) { on_input_added(p); });
    evTokenOnInputRemoved_
        = internalInPortObserver_.PortRemoved([this](const port_info& p) { on_input_removed(p); });
    evTokenOnOutputAdded_
        = internalOutPortObserver_.PortAdded([this](const port_info& p) { on_output_added(p); });
    evTokenOnOutputRemoved_ = internalOutPortObserver_.PortRemoved(
        [this](const port_info& p) { on_output_removed(p); });
  }

  ~observer_winuwp()
  {
    if (!configuration.has_callbacks())
      return;
    internalInPortObserver_.PortAdded(evTokenOnInputAdded_);
    internalInPortObserver_.PortRemoved(evTokenOnInputRemoved_);
    internalOutPortObserver_.PortAdded(evTokenOnOutputAdded_);
    internalOutPortObserver_.PortRemoved(evTokenOnOutputRemoved_);
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::WINDOWS_UWP; }

  template <bool Input>
  auto to_port_info(const observer_winuwp_internal::port_info& p) const noexcept
      -> std::conditional_t<Input, input_port, output_port>
  {
    return {
        {.client = 0,
         .port = 0,
         .manufacturer = "",
         .device_name = "",
         .port_name = to_string(p.id),
         .display_name = to_string(p.name)}};
  }

  std::vector<libremidi::input_port> get_input_ports() const noexcept override
  {
    std::vector<libremidi::input_port> ret;
    for (auto& port : internalInPortObserver_.get_ports())
      ret.push_back(to_port_info<true>(port));
    return ret;
  }

  std::vector<libremidi::output_port> get_output_ports() const noexcept override
  {
    std::vector<libremidi::output_port> ret;
    for (auto& port : internalOutPortObserver_.get_ports())
      ret.push_back(to_port_info<false>(port));
    return ret;
  }

  static observer_winuwp_internal& get_internal_in_port_observer()
  {
    return internalInPortObserver_;
  }

  static observer_winuwp_internal& get_internal_out_port_observer()
  {
    return internalOutPortObserver_;
  }

  void on_input_added(const observer_winuwp_internal::port_info& name)
  {
    if (configuration.input_added)
      configuration.input_added(to_port_info<true>(name));
  }

  void on_input_removed(const observer_winuwp_internal::port_info& name)
  {
    if (configuration.input_removed)
      configuration.input_removed(to_port_info<true>(name));
  }

  void on_output_added(const observer_winuwp_internal::port_info& name)
  {
    if (configuration.output_added)
      configuration.output_added(to_port_info<false>(name));
  }

  void on_output_removed(const observer_winuwp_internal::port_info& name)
  {
    if (configuration.output_removed)
      configuration.output_removed(to_port_info<false>(name));
  }

private:
  static inline observer_winuwp_internal internalInPortObserver_{MidiInPort::GetDeviceSelector()};
  static inline observer_winuwp_internal internalOutPortObserver_{
      MidiOutPort::GetDeviceSelector()};

  int evTokenOnInputAdded_{-1};
  int evTokenOnInputRemoved_{-1};
  int evTokenOnOutputAdded_{-1};
  int evTokenOnOutputRemoved_{-1};
};

}
