#pragma once
#include <libremidi/backends/linux/dylib_loader.hpp>
#include <libremidi/backends/linux/helpers.hpp>
#include <libremidi/observer_configuration.hpp>

#include <libudev.h>

#include <cassert>
#include <optional>

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

    LIBREMIDI_SYMBOL_INIT(udev, device_get_action)
    LIBREMIDI_SYMBOL_INIT(udev, device_get_subsystem)
    LIBREMIDI_SYMBOL_INIT(udev, device_get_property_value)
    LIBREMIDI_SYMBOL_INIT(udev, device_new_from_syspath)
    LIBREMIDI_SYMBOL_INIT(udev, device_get_parent_with_subsystem_devtype)
    LIBREMIDI_SYMBOL_INIT(udev, device_unref)

    LIBREMIDI_SYMBOL_INIT(udev, monitor_enable_receiving)
    LIBREMIDI_SYMBOL_INIT(udev, monitor_get_fd)
    LIBREMIDI_SYMBOL_INIT(udev, monitor_new_from_netlink)
    LIBREMIDI_SYMBOL_INIT(udev, monitor_receive_device)
    LIBREMIDI_SYMBOL_INIT(udev, monitor_unref)

    LIBREMIDI_SYMBOL_INIT(udev, enumerate_new)
    LIBREMIDI_SYMBOL_INIT(udev, enumerate_unref)
    LIBREMIDI_SYMBOL_INIT(udev, enumerate_add_match_is_initialized)
    LIBREMIDI_SYMBOL_INIT(udev, enumerate_add_match_subsystem)
    LIBREMIDI_SYMBOL_INIT(udev, enumerate_add_match_sysname)
    LIBREMIDI_SYMBOL_INIT(udev, enumerate_scan_devices)
    LIBREMIDI_SYMBOL_INIT(udev, enumerate_get_list_entry)

    LIBREMIDI_SYMBOL_INIT(udev, list_entry_get_name)
    LIBREMIDI_SYMBOL_INIT(udev, list_entry_get_next)

    LIBREMIDI_SYMBOL_INIT2(udev, new, create)
    LIBREMIDI_SYMBOL_INIT(udev, unref)
  }

  static const libudev& instance()
  {
    static const libudev self;
    return self;
  }

  dylib_loader library;
  bool available{true};

  LIBREMIDI_SYMBOL_DEF(udev, device_get_action)
  LIBREMIDI_SYMBOL_DEF(udev, device_get_subsystem)
  LIBREMIDI_SYMBOL_DEF(udev, device_new_from_syspath)
  LIBREMIDI_SYMBOL_DEF(udev, device_get_property_value)
  LIBREMIDI_SYMBOL_DEF(udev, device_get_parent_with_subsystem_devtype)
  LIBREMIDI_SYMBOL_DEF(udev, device_unref)

  LIBREMIDI_SYMBOL_DEF(udev, monitor_enable_receiving)
  LIBREMIDI_SYMBOL_DEF(udev, monitor_get_fd)
  LIBREMIDI_SYMBOL_DEF(udev, monitor_new_from_netlink)
  LIBREMIDI_SYMBOL_DEF(udev, monitor_receive_device)
  LIBREMIDI_SYMBOL_DEF(udev, monitor_unref)

  LIBREMIDI_SYMBOL_DEF(udev, enumerate_new)
  LIBREMIDI_SYMBOL_DEF(udev, enumerate_unref)
  LIBREMIDI_SYMBOL_DEF(udev, enumerate_add_match_is_initialized)
  LIBREMIDI_SYMBOL_DEF(udev, enumerate_add_match_subsystem)
  LIBREMIDI_SYMBOL_DEF(udev, enumerate_add_match_sysname)
  LIBREMIDI_SYMBOL_DEF(udev, enumerate_scan_devices)
  LIBREMIDI_SYMBOL_DEF(udev, enumerate_get_list_entry)

  LIBREMIDI_SYMBOL_DEF(udev, list_entry_get_name)
  LIBREMIDI_SYMBOL_DEF(udev, list_entry_get_next)

  LIBREMIDI_SYMBOL_DEF2(udev, new, create)
  LIBREMIDI_SYMBOL_DEF(udev, unref)
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

struct udev_enumerator
{
  explicit udev_enumerator(const udev_helper& h)
      : helper{h}
  {
    auto& udev = helper.udev;
    auto& instance = helper.instance;
    enumerator = udev.enumerate_new(instance);
    assert(enumerator);
  }

  ~udev_enumerator()
  {
    auto& udev = helper.udev;
    udev.enumerate_unref(enumerator);
  }

  const udev_helper& helper;
  udev_enumerate* enumerator{};
  operator udev_enumerate*() const noexcept { return enumerator; }
};

struct udev_soundcard_info
{
  container_identifier container;
  device_identifier path;
  port_information::port_type type{};
};

inline udev_soundcard_info get_udev_soundcard_info(const udev_helper& helper, int card)
{
  auto& udev = helper.udev;
  udev_enumerator e{helper};
  udev.enumerate_add_match_subsystem(e, "sound");
  udev.enumerate_add_match_sysname(e, ("controlC" + std::to_string(card)).c_str());
  if (udev.enumerate_scan_devices(e) != 0)
    return {};

  for (auto entry = udev.enumerate_get_list_entry(e); entry;
       entry = udev.list_entry_get_next(entry))
  {
    auto path = udev.list_entry_get_name(entry);
    auto dev = udev.device_new_from_syspath(helper.instance, path);
    auto usb_device = udev.device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");
    port_information::port_type type = port_information::port_type::hardware;
    if (usb_device)
      type = (port_information::port_type)(type | port_information::port_type::usb);
    else
      type = (port_information::port_type)(type | port_information::port_type::pci);

    auto id_path = udev.device_get_property_value(dev, "ID_PATH");

    return udev_soundcard_info{.container = id_path, .path = path, .type = type};
  }
  return {};
}
}
