#pragma once
#include <libremidi/backends/linux/dylib_loader.hpp>
#include <libremidi/backends/linux/helpers.hpp>

#include <libudev.h>

#include <cassert>

namespace libremidi
{

struct libudev
{
  // Useful one-liner:
  // nm -A * | grep ' udev_'  | grep -v '@' | cut -f 2 -d 'U' | sort | uniq   | sed 's/ udev_//'| awk ' { print "LIBREMIDI_SYMBOL_DEF(udev, "$1");" }'

  explicit libudev()
      : library{"libudev.so.1"}
  {
    if (!library)
    {
      available = false;
      return;
    }

    LIBREMIDI_SYMBOL_INIT(udev, device_get_action);
    LIBREMIDI_SYMBOL_INIT(udev, device_get_subsystem);
    LIBREMIDI_SYMBOL_INIT(udev, device_unref);
    LIBREMIDI_SYMBOL_INIT(udev, monitor_enable_receiving);
    LIBREMIDI_SYMBOL_INIT(udev, monitor_get_fd);
    LIBREMIDI_SYMBOL_INIT(udev, monitor_new_from_netlink);
    LIBREMIDI_SYMBOL_INIT(udev, monitor_receive_device);
    LIBREMIDI_SYMBOL_INIT(udev, monitor_unref);
    LIBREMIDI_SYMBOL_INIT2(udev, new, create);
    LIBREMIDI_SYMBOL_INIT(udev, unref);
  }

  static const libudev& instance()
  {
    static const libudev self;
    return self;
  }

  dylib_loader library;
  bool available{true};

  LIBREMIDI_SYMBOL_DEF(udev, device_get_action);
  LIBREMIDI_SYMBOL_DEF(udev, device_get_subsystem);
  LIBREMIDI_SYMBOL_DEF(udev, device_unref);
  LIBREMIDI_SYMBOL_DEF(udev, monitor_enable_receiving);
  LIBREMIDI_SYMBOL_DEF(udev, monitor_get_fd);
  LIBREMIDI_SYMBOL_DEF(udev, monitor_new_from_netlink);
  LIBREMIDI_SYMBOL_DEF(udev, monitor_receive_device);
  LIBREMIDI_SYMBOL_DEF(udev, monitor_unref);
  LIBREMIDI_SYMBOL_DEF2(udev, new, create);
  LIBREMIDI_SYMBOL_DEF(udev, unref);
};

struct udev_helper
{
  udev_helper()
  {
    instance = udev.create();
    assert(instance);

    monitor = udev.monitor_new_from_netlink(instance, "udev");
    assert(monitor);
    udev.monitor_enable_receiving(monitor);
  }

  ~udev_helper()
  {
    udev.monitor_unref(monitor);
    udev.unref(instance);
  }

  udev_helper(const udev_helper&) = delete;
  udev_helper(udev_helper&&) = delete;
  udev_helper& operator=(const udev_helper&) = delete;
  udev_helper& operator=(udev_helper&&) = delete;

  operator pollfd() const noexcept
  {
    return {.fd = udev.monitor_get_fd(monitor), .events = POLLIN, .revents = 0};
  }

  const libudev& udev = libudev::instance();
  struct udev* instance{};
  udev_monitor* monitor{};
};

}
