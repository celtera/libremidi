#pragma once
#include <libremidi/backends/linux/helpers.hpp>

#include <libudev.h>

#include <cassert>

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

}
