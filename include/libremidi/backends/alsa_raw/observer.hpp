#pragma once
#include <optional>
#include <libremidi/backends/alsa_raw/config.hpp>
#include <libremidi/backends/alsa_raw/helpers.hpp>
#include <libremidi/backends/dummy.hpp>

#if LIBREMIDI_HAS_UDEV
  #include <libremidi/backends/linux/helpers.hpp>
  #include <libremidi/backends/linux/udev.hpp>
#endif
#include <libremidi/detail/observer.hpp>

NAMESPACE_LIBREMIDI::alsa_raw
{
template <typename Enumerator>
class observer_impl_base
    : public observer_api
    , public error_handler
{
public:
  struct
      : observer_configuration
      , alsa_raw_observer_configuration
  {
  } configuration;

  const libasound& snd = libasound::instance();

  explicit observer_impl_base(
      observer_configuration&& conf, alsa_raw_observer_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
  }

  ~observer_impl_base()
  {
#if LIBREMIDI_HAS_UDEV
    m_termination_event.notify();

    if (m_thread.joinable())
      m_thread.join();
#endif
  }

  // Must be called for child classes due to virtual function calls
  void finish_init()
  {
    if (!configuration.has_callbacks())
      return;

#if LIBREMIDI_HAS_UDEV
    m_fds[0] = this->m_udev;
    m_fds[1] = m_termination_event;
    m_fds[2] = m_timer_fd;
#endif

    // Set-up initial state
    this->check_devices(configuration.notify_in_constructor);

#if LIBREMIDI_HAS_UDEV
    // Start thread
    m_thread = std::thread{[this] { this->run(); }};
#endif
  }

  std::vector<libremidi::input_port> get_input_ports() const noexcept override
  {
    std::vector<libremidi::input_port> ret;
    Enumerator new_devs{*this};

    new_devs.enumerate_cards();
    for (auto& d : new_devs.inputs)
    {
      if (auto p = wanted_port<true>(d))
        ret.push_back(std::move(*p));
    }
    return ret;
  }

  std::vector<libremidi::output_port> get_output_ports() const noexcept override
  {
    std::vector<libremidi::output_port> ret;
    Enumerator new_devs{*this};

    new_devs.enumerate_cards();
    for (auto& d : new_devs.outputs)
    {
      if (auto p = wanted_port<false>(d))
        ret.push_back(std::move(*p));
    }
    return ret;
  }

#if LIBREMIDI_HAS_UDEV
  void run()
  {
    for (;;)
    {
      if (int err = poll(m_fds, 3, -1); err < 0)
      {
        if (err == -EAGAIN)
          continue;
        else
          return;
      }

      // Check eventfd to see if we need to exit
      if (m_fds[1].revents & POLLIN)
      {
        break;
      }

      // Check udev
      if (m_fds[0].revents & POLLIN)
      {
        udev_device* dev = m_udev.udev.monitor_receive_device(m_udev.monitor);
        if (!dev)
          continue;

        std::string_view act = m_udev.udev.device_get_action(dev);
        std::string_view ss = m_udev.udev.device_get_subsystem(dev);
        if (!act.empty() && ss == "snd_seq")
        {
          if (act == "add" || act == "remove")
          {
            // Check every 100 milliseconds for ten seconds
            this->m_timer_fd.restart(configuration.poll_period.count());
            m_timer_check_counts = 100;
          }
        }

        m_udev.udev.device_unref(dev);

        m_fds[0].revents = 0;
      }

      // Check timer
      if (m_fds[2].revents & POLLIN)
      {
        if (this->m_timer_check_counts-- <= 0)
          this->m_timer_fd.cancel();
        m_fds[2].revents = 0;

        check_devices(true);
      }
    }
  }
#endif

  template <bool Input>
  auto to_port_info(const alsa_raw::alsa_raw_port_info& p) const noexcept
      -> std::conditional_t<Input, input_port, output_port>
  {
#if LIBREMIDI_HAS_UDEV
    auto [container, device, type, manufacturer, product, serial]
        = get_udev_soundcard_info(m_udev, p.card);
#else
    container_identifier container{};
    device_identifier device{};
    // Nothing can say which kind of hardware this is without udev, but this
    // backend enumerates ALSA sound cards, so hardware it is: `unknown` would
    // belong to no transport group and be filtered out.
    libremidi::transport_type type = libremidi::transport_type::hardware;
    std::string manufacturer;
    std::string product;
    std::string serial;
#endif
    std::string display_name = p.subdevice_name;
    if (display_name.empty())
      display_name = p.device_name;
    if (display_name.empty())
      display_name = p.card_name;
    if (display_name.empty())
      display_name = product;
    if (display_name.empty())
      display_name = "unknown";

    return {
        {.api = get_current_api(),
         .client = 0,
         .container = container,
         .device = device,
         .port = raw_to_port_handle({p.card, p.dev, p.sub}),
         .manufacturer = manufacturer,
         .product = product,
         .serial = serial,
         .device_name = p.device_name,
         .port_name = p.subdevice_name,
         .display_name = std::move(display_name),
         .type = type}};
  }

  //! The transport comes out of udev, so it is only known once the port has
  //! been built: the filter has to come after the conversion.
  template <bool Input>
  auto wanted_port(const alsa_raw::alsa_raw_port_info& p) const noexcept
      -> std::optional<std::conditional_t<Input, input_port, output_port>>
  {
    auto info = to_port_info<Input>(p);

    // udev can also fail at run time, for a card with no controlC node. Same
    // answer: it is a sound card.
    auto transport = info.type;
    if (transport == libremidi::transport_type::unknown)
      transport = libremidi::transport_type::hardware;

    if (!this->configuration.accepts(transport))
      return std::nullopt;
    return info;
  }

  void check_devices(bool notify)
  {
    Enumerator new_devs{*this};

    new_devs.enumerate_cards();

    for (auto& in_prev : m_current_inputs)
    {
      if (auto it = std::find(new_devs.inputs.begin(), new_devs.inputs.end(), in_prev);
          it == new_devs.inputs.end())
      {
        if (auto& cb = this->configuration.input_removed)
        {
          if (auto p = wanted_port<true>(in_prev))
            cb(std::move(*p));
        }
      }
    }

    for (auto& out_prev : m_current_outputs)
    {
      if (auto it = std::find(new_devs.outputs.begin(), new_devs.outputs.end(), out_prev);
          it == new_devs.outputs.end())
      {
        if (auto& cb = this->configuration.output_removed)
        {
          if (auto p = wanted_port<false>(out_prev))
            cb(std::move(*p));
        }
      }
    }

    if (notify)
    {
      if (auto& cb = this->configuration.input_added)
      {
        for (auto& in_next : new_devs.inputs)
        {
          if (auto it = std::find(m_current_inputs.begin(), m_current_inputs.end(), in_next);
              it == m_current_inputs.end())
          {
            if (auto p = wanted_port<true>(in_next))
            cb(std::move(*p));
          }
        }
      }

      if (auto& cb = this->configuration.output_added)
      {
        for (auto& out_next : new_devs.outputs)
        {
          if (auto it = std::find(m_current_outputs.begin(), m_current_outputs.end(), out_next);
              it == m_current_outputs.end())
          {
            if (auto p = wanted_port<false>(out_next))
            cb(std::move(*p));
          }
        }
      }
    }
    m_current_inputs = std::move(new_devs.inputs);
    m_current_outputs = std::move(new_devs.outputs);
  }

#if LIBREMIDI_HAS_UDEV
  udev_helper m_udev{};
  eventfd_notifier m_termination_event{};
  timerfd_timer m_timer_fd{};
  int m_timer_check_counts = 0;
  std::thread m_thread;
  pollfd m_fds[3]{};
#endif

  std::vector<alsa_raw_port_info> m_current_inputs;
  std::vector<alsa_raw_port_info> m_current_outputs;
};
}

NAMESPACE_LIBREMIDI::alsa_raw
{
struct observer_impl : observer_impl_base<alsa_raw::midi1_enumerator>
{
  observer_impl(observer_configuration&& conf, alsa_raw_observer_configuration&& apiconf)
      : observer_impl_base<alsa_raw::midi1_enumerator>{std::move(conf), std::move(apiconf)}
  {
    finish_init();
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::ALSA_RAW; }
};
}
