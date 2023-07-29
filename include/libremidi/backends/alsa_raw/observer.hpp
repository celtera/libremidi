#pragma once
#if LIBREMIDI_HAS_UDEV
  #include <libremidi/backends/alsa_raw/config.hpp>
  #include <libremidi/backends/alsa_raw/helpers.hpp>
  #include <libremidi/backends/dummy.hpp>
  #include <libremidi/backends/linux/helpers.hpp>
  #include <libremidi/backends/linux/udev.hpp>
  #include <libremidi/detail/observer.hpp>

  #include <poll.h>

  #include <stdexcept>

namespace libremidi::alsa_raw
{
class observer_impl : public observer_api
{
public:
  struct
      : observer_configuration
      , alsa_raw_observer_configuration
  {
  } configuration;

  explicit observer_impl(observer_configuration&& conf, alsa_raw_observer_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    if (!configuration.has_callbacks())
      return;

    fds[0] = udev;
    fds[1] = termination_event;
    fds[2] = timer_fd;

    // Set-up initial state
    if (configuration.notify_in_constructor)
      check_devices();

    // Start thread
    thread = std::thread{[this] { run(); }};
  }

  ~observer_impl()
  {
    termination_event.notify();

    if (thread.joinable())
      thread.join();
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::ALSA_RAW; }

  std::vector<libremidi::input_port> get_input_ports() const noexcept override
  {
    std::vector<libremidi::input_port> ret;
    alsa_raw_helpers::enumerator new_devs;

    new_devs.enumerate_cards();
    for (auto& d : new_devs.inputs)
    {
      ret.push_back(to_port_info<true>(d));
    }
    return ret;
  }

  std::vector<libremidi::output_port> get_output_ports() const noexcept override
  {
    std::vector<libremidi::output_port> ret;
    alsa_raw_helpers::enumerator new_devs;

    new_devs.enumerate_cards();
    for (auto& d : new_devs.outputs)
    {
      ret.push_back(to_port_info<false>(d));
    }
    return ret;
  }

private:
  void run()
  {
    for (;;)
    {
      if (int err = poll(fds, 3, -1); err < 0)
      {
        if (err == -EAGAIN)
          continue;
        else
          return;
      }

      // Check udev
      if (fds[0].revents & POLLIN)
      {
        udev_device* dev = udev_monitor_receive_device(udev.monitor);
        if (!dev)
          continue;

        std::string_view act = udev_device_get_action(dev);
        std::string_view ss = udev_device_get_subsystem(dev);
        if (!act.empty() && ss == "snd_seq")
        {
          if (act == "add" || act == "remove")
          {
            // Check every 100 milliseconds for ten seconds
            this->timer_fd.restart(configuration.poll_period.count());
            timer_check_counts = 100;
          }
        }

        udev_device_unref(dev);

        fds[0].revents = 0;
      }

      // Check eventfd
      if (fds[1].revents & POLLIN)
      {
        break;
      }

      // Check timer
      if (fds[2].revents & POLLIN)
      {
        if (this->timer_check_counts-- <= 0)
          this->timer_fd.cancel();
        fds[2].revents = 0;

        check_devices();
      }
    }
  }

  template <bool Input>
  auto to_port_info(alsa_raw_helpers::alsa_raw_port_info p) const noexcept
      -> std::conditional_t<Input, input_port, output_port>
  {
    return {
        {.client = 0,
         .port = raw_to_port_handle({p.card, p.dev, p.sub}),
         .manufacturer = p.card_name,
         .device_name = p.device_name,
         .port_name = p.subdevice_name,
         .display_name = p.subdevice_name}};
  }

  void check_devices()
  {
    alsa_raw_helpers::enumerator new_devs;

    new_devs.enumerate_cards();

    for (auto& in_prev : current_devices.inputs)
    {
      if (auto it = std::find(new_devs.inputs.begin(), new_devs.inputs.end(), in_prev);
          it == new_devs.inputs.end())
      {
        if (auto& cb = this->configuration.input_removed)
        {
          cb(to_port_info<true>(in_prev));
        }
      }
    }

    for (auto& in_next : new_devs.inputs)
    {
      if (auto it
          = std::find(current_devices.inputs.begin(), current_devices.inputs.end(), in_next);
          it == current_devices.inputs.end())
      {
        if (auto& cb = this->configuration.input_added)
        {
          cb(to_port_info<true>(in_next));
        }
      }
    }

    for (auto& out_prev : current_devices.outputs)
    {
      if (auto it = std::find(new_devs.outputs.begin(), new_devs.outputs.end(), out_prev);
          it == new_devs.outputs.end())
      {
        if (auto& cb = this->configuration.output_removed)
        {
          cb(to_port_info<false>(out_prev));
        }
      }
    }

    for (auto& out_next : new_devs.outputs)
    {
      if (auto it
          = std::find(current_devices.outputs.begin(), current_devices.outputs.end(), out_next);
          it == current_devices.outputs.end())
      {
        if (auto& cb = this->configuration.output_added)
        {
          cb(to_port_info<false>(out_next));
        }
      }
    }
    current_devices = new_devs;
  }

  udev_helper udev{};
  eventfd_notifier termination_event{};
  timerfd_timer timer_fd{};
  int timer_check_counts = 0;
  std::thread thread;
  alsa_raw_helpers::enumerator current_devices;

  pollfd fds[3]{};
};
}
#else
  #include <libremidi/backends/dummy.hpp>
namespace libremidi::alsa_raw
{
struct observer_impl : observer_dummy
{
  explicit observer_alsa_raw(
      observer_configuration&& conf, alsa_raw_observer_configuration&& apiconf)
      : observer_dummy{dummy_configuration{}, dummy_configuration{}}
  {
  }
};
}
#endif
