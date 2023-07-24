#pragma once
#if LIBREMIDI_HAS_UDEV
  #include <libremidi/backends/alsa_raw/config.hpp>
  #include <libremidi/backends/alsa_raw/helpers.hpp>
  #include <libremidi/backends/dummy.hpp>
  #include <libremidi/backends/linux/helpers.hpp>
  #include <libremidi/detail/observer.hpp>

  #include <libudev.h>
  #include <poll.h>

  #include <stdexcept>

namespace libremidi
{
struct udev_helper
{
  udev_helper()
  {
    instance = udev_new();
    assert(instance);

    monitor = udev_monitor_new_from_netlink(instance, "udev");
    assert(monitor);
    udev_monitor_enable_receiving(monitor);
  }

  ~udev_helper()
  {
    udev_monitor_unref(monitor);
    udev_unref(instance);
  }

  udev_helper(const udev_helper&) = delete;
  udev_helper(udev_helper&&) = delete;
  udev_helper& operator=(const udev_helper&) = delete;
  udev_helper& operator=(udev_helper&&) = delete;

  operator pollfd() const noexcept
  {
    return {.fd = udev_monitor_get_fd(monitor), .events = POLLIN};
  }

  udev* instance{};
  udev_monitor* monitor{};
};

class observer_alsa_raw final : public observer_api
{
public:
  explicit observer_alsa_raw(observer::callbacks&& c)
      : observer_api{std::move(c)}
  {
    fds[0] = udev;
    fds[1] = event_fd;
    fds[2] = timer_fd;

    // Set-up initial state
    check_devices();

    // Start thread
    thread = std::thread{[this] { run(); }};
  }

  ~observer_alsa_raw()
  {
    event_fd.notify();

    if (thread.joinable())
      thread.join();
  }

private:
  void run()
  {
    for (;;)
    {
      int res = poll(fds, 3, -1);
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
            this->timer_fd.restart(100'000'000);
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

  void check_devices()
  {
    raw_alsa_helpers::enumerator new_devs;

    new_devs.enumerate_cards();

    int k = 0;
    for (auto& in_prev : current_devices.inputs)
    {
      if (auto it = std::find(new_devs.inputs.begin(), new_devs.inputs.end(), in_prev);
          it == new_devs.inputs.end())
      {
        if (auto& cb = this->callbacks_.input_removed)
        {
          cb(k, in_prev.subdevice_name);
        }
      }
      k++;
    }

    k = 0;
    for (auto& in_next : new_devs.inputs)
    {
      if (auto it
          = std::find(current_devices.inputs.begin(), current_devices.inputs.end(), in_next);
          it == current_devices.inputs.end())
      {
        if (auto& cb = this->callbacks_.input_added)
        {
          cb(k, in_next.subdevice_name);
        }
      }
      k++;
    }

    k = 0;
    for (auto& out_prev : current_devices.outputs)
    {
      if (auto it = std::find(new_devs.outputs.begin(), new_devs.outputs.end(), out_prev);
          it == new_devs.outputs.end())
      {
        if (auto& cb = this->callbacks_.output_removed)
        {
          cb(k, out_prev.subdevice_name);
        }
      }
      k++;
    }

    k = 0;
    for (auto& out_next : new_devs.outputs)
    {
      if (auto it
          = std::find(current_devices.outputs.begin(), current_devices.outputs.end(), out_next);
          it == current_devices.outputs.end())
      {
        if (auto& cb = this->callbacks_.output_added)
        {
          cb(k, out_next.subdevice_name);
        }
      }
      k++;
    }
    current_devices = new_devs;
  }

  udev_helper udev{};
  eventfd_notifier event_fd{};
  timerfd_timer timer_fd{};
  int timer_check_counts = 0;
  std::thread thread;
  raw_alsa_helpers::enumerator current_devices;

  pollfd fds[3]{};
};
}
#else
  #include <libremidi/backends/dummy.hpp>
namespace libremidi
{
using observer_alsa_raw = observer_dummy;
}
#endif
