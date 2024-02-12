#pragma once

#include <libremidi/backends/linux/helpers.hpp>
#include <libremidi/backends/linux/pipewire.hpp>
#include <libremidi/backends/pipewire/context.hpp>
#include <libremidi/detail/memory.hpp>
#include <libremidi/detail/midi_in.hpp>

#include <atomic>
#include <semaphore>
#include <stop_token>
#include <thread>

namespace libremidi
{
struct pipewire_helpers
{
  struct port
  {
    void* data{};
  };

  // All pipewire operations have to happen in the same thread
  // - and pipewire checks that internally.
  std::jthread main_loop_thread;
  const libpipewire& pw = libpipewire::instance();
  std::shared_ptr<pipewire_instance> global_instance;
  std::shared_ptr<pipewire_context> global_context;
  std::unique_ptr<pipewire_filter> filter;

  int64_t this_instance{};

  eventfd_notifier termination_event{};
  pollfd fds[2]{};

  pipewire_helpers()
  {
    static std::atomic_int64_t instance{};
    this_instance = ++instance;

    fds[1] = termination_event;
  }

  template <typename Self>
  int connect(Self& self)
  {

    if (this->filter)
      return 0;

    // Initialize PipeWire client
#if 0
    auto& configuration = self.configuration;
    if (configuration.context)
    {
      // FIXME case where user provides an existing filter

      if (!configuration.set_process_func)
        return -1;
      configuration.set_process_func(
          {.token = this_instance,
           .callback = [&self, p = std::weak_ptr{this->port.impl}](int nf) -> int {
             auto pt = p.lock();
             if (!pt)
               return 0;
             auto ppt = pt->load();
             if (!ppt)
               return 0;

             self.process(nf);

             self.check_client_released();
             return 0;
           }});

      this->client = configuration.context;
      return 0;
    }
    else
#endif
    {
      this->global_instance = std::make_shared<pipewire_instance>();
      this->global_context = std::make_shared<pipewire_context>(this->global_instance);
      this->filter = std::make_unique<pipewire_filter>(this->global_context);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
      static constexpr struct pw_filter_events filter_events
          = {.version = PW_VERSION_FILTER_EVENTS,
             .process = +[](void* _data, struct spa_io_position* position) -> void {
               Self& self = *static_cast<Self*>(_data);
               self.process(position);
             }};
#pragma GCC diagnostic pop

      this->filter->create_filter(self.configuration.client_name, filter_events, &self);
      this->filter->start_filter();
      return 0;
    }
    return 0;
  }

  template <typename Self>
  void disconnect(Self&)
  {
#if 0
    if (self.configuration.context)
    {
      if (self.configuration.clear_process_func)
      {
        self.configuration.clear_process_func(this_instance);
      }
    }
    else
#endif
    {
      termination_event.notify();
    }
  }

  void run_poll_loop()
  {
    // Note: called from a std::jthread.
    if (int fd = this->global_context->get_fd(); fd != -1)
    {
      fds[0] = {.fd = fd, .events = POLLIN, .revents = 0};

      for (;;)
      {
        if (int err = poll(fds, 2, -1); err < 0)
        {
          if (err == -EAGAIN)
            continue;
          else
            return;
        }

        // Check pipewire fd:
        if (fds[0].revents & POLLIN)
        {
          if (auto lp = this->global_context->lp)
          {
            int result = pw_loop_iterate(lp, 0);
            if (result < 0)
              std::cerr << "pw_loop_iterate: " << spa_strerror(result) << "\n";
          }
          fds[0].revents = 0;
        }

        // Check exit fd:
        if (fds[1].revents & POLLIN)
        {
          break;
        }
      }
    }
  }

  template <typename Self>
  bool create_local_port(Self& self, std::string_view portName, spa_direction direction)
  {
    assert(this->filter);

    if (portName.empty())
      portName = direction == SPA_DIRECTION_INPUT ? "i" : "o";

    if (!this->filter->port)
    {
      this->filter->create_local_port(portName.data(), direction);
      main_loop_thread = std::jthread{[this, &self]() { run_poll_loop(); }};
    }

    if (!this->filter->port)
    {
      self.template error<driver_error>(self.configuration, "PipeWire: error creating port");
      return false;
    }
    return true;
  }

  void do_close_port()
  {
    if (!this->filter)
      return;
    if (!this->filter->port)
      return;

    if (main_loop_thread.joinable())
    {
      termination_event.notify();
      main_loop_thread.request_stop();
      main_loop_thread.join();
    }

    this->filter->remove_port();
  }

  void rename_port(std::string_view port_name)
  {
    if (this->filter)
      this->filter->rename_port(port_name);
  }

  template <bool Input>
  static auto to_port_info(pw_main_loop* client, void* port)
      -> std::conditional_t<Input, input_port, output_port>
  {
    return {{
        .client = reinterpret_cast<std::uintptr_t>(client),
        .port = 0,
        .manufacturer = "",
        .device_name = "",
        .port_name = "",
        .display_name = "",
    }};
  }

  template <bool Input>
  static auto get_ports(pw_main_loop* client, const char* pattern, int flags) noexcept
      -> std::vector<std::conditional_t<Input, input_port, output_port>>
  {
    std::vector<std::conditional_t<Input, input_port, output_port>> ret;

    if (!client)
      return {};

    return ret;
  }
};
}
