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

  observer_winuwp_internal(hstring deviceSelector) { initialize(deviceSelector); }
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

  event_token PortAdded(TypedEventHandler<int, hstring> const& handler)
  {
    return portAddedEvent_.add(handler);
  }

  void PortAdded(event_token const& token) noexcept { portAddedEvent_.remove(token); }

  event_token PortRemoved(TypedEventHandler<int, hstring> const& handler)
  {
    return portRemovedEvent_.add(handler);
  }

  void PortRemoved(event_token const& token) noexcept { portRemovedEvent_.remove(token); }

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
    int portNumber = -1;
    hstring name;
    {
      std::lock_guard<std::mutex> lock(portListMutex_);
      portNumber = static_cast<int>(portList_.size());
      name = deviceInfo.Name();
      portList_.push_back({deviceInfo.Id(), deviceInfo.Name()});
    }
    portAddedEvent_(portNumber, name);
  }

  void on_device_removed(DeviceWatcher sender, DeviceInformationUpdate deviceUpdate)
  {
    const auto id = deviceUpdate.Id();
    auto pred = [&id](const port_info& portInfo) { return portInfo.id == id; };
    int portNumber = -1;
    hstring name;
    {
      std::lock_guard<std::mutex> lock(portListMutex_);
      auto iter = std::find_if(portList_.begin(), portList_.end(), pred);
      if (iter != portList_.end())
      {
        portNumber = static_cast<int>(std::distance(portList_.begin(), iter));
        name = iter->name;
        portList_.erase(iter);
      }
    }
    if (portNumber >= 0)
      portRemovedEvent_(portNumber, name);
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

  winrt::event<TypedEventHandler<int, hstring>> portAddedEvent_;
  winrt::event<TypedEventHandler<int, hstring>> portRemovedEvent_;
};

class observer_winuwp final : public observer_api
{
public:
  struct
      : observer_configuration
      , winuwp_observer_configuration
  {
  } configuration;

  explicit observer_winuwp(observer_configuration&& conf, winuwp_observer_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    if (!configuration.has_callbacks())
      return;

    evTokenOnInputAdded_
        = internalInPortObserver_.PortAdded({this, &observer_winuwp::on_input_added});
    evTokenOnInputRemoved_
        = internalInPortObserver_.PortRemoved({this, &observer_winuwp::on_input_removed});
    evTokenOnOutputAdded_
        = internalOutPortObserver_.PortAdded({this, &observer_winuwp::on_output_added});
    evTokenOnOutputRemoved_
        = internalOutPortObserver_.PortRemoved({this, &observer_winuwp::on_output_removed});
  }

  ~observer_winuwp()
  {
    internalInPortObserver_.PortAdded(evTokenOnInputAdded_);
    internalInPortObserver_.PortRemoved(evTokenOnInputRemoved_);
    internalOutPortObserver_.PortAdded(evTokenOnOutputAdded_);
    internalOutPortObserver_.PortRemoved(evTokenOnOutputRemoved_);
  }

  static observer_winuwp_internal& get_internal_in_port_observer()
  {
    return internalInPortObserver_;
  }

  static observer_winuwp_internal& get_internal_out_port_observer()
  {
    return internalOutPortObserver_;
  }

  void on_input_added(int portNumber, hstring name)
  {
    if (configuration.input_added)
      configuration.input_added(portNumber, to_string(name));
  }

  void on_input_removed(int portNumber, hstring name)
  {
    if (configuration.input_removed)
      configuration.input_removed(portNumber, to_string(name));
  }

  void on_output_added(int portNumber, hstring name)
  {
    if (configuration.output_added)
      configuration.output_added(portNumber, to_string(name));
  }

  void on_output_removed(int portNumber, hstring name)
  {
    if (configuration.output_removed)
      configuration.output_removed(portNumber, to_string(name));
  }

private:
  static observer_winuwp_internal internalInPortObserver_;
  static observer_winuwp_internal internalOutPortObserver_;

  event_token evTokenOnInputAdded_;
  event_token evTokenOnInputRemoved_;
  event_token evTokenOnOutputAdded_;
  event_token evTokenOnOutputRemoved_;
};

observer_winuwp_internal observer_winuwp::internalInPortObserver_(MidiInPort::GetDeviceSelector());
observer_winuwp_internal
    observer_winuwp::internalOutPortObserver_(MidiOutPort::GetDeviceSelector());

}
