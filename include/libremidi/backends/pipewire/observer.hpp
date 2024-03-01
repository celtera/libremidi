#pragma once
#include <libremidi/backends/pipewire/config.hpp>
#include <libremidi/backends/pipewire/helpers.hpp>
#include <libremidi/detail/observer.hpp>

#include <unordered_set>

namespace libremidi
{
class observer_pipewire final
    : public observer_api
    , private pipewire_helpers
    , private error_handler
{
public:
  struct
      : observer_configuration
      , pipewire_observer_configuration
  {
  } configuration;

  explicit observer_pipewire(observer_configuration&& conf, pipewire_observer_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    create_context(*this);

    // FIXME notify_in_constructor
    // FIXME port rename callback
#if 0
    // Initialize PipeWire client
    if (configuration.context)
    {
      this->client = configuration.context;
      set_callbacks();
    }
    else
#endif
    {
      this->add_callbacks(configuration);
      this->start_thread();
    }
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::PIPEWIRE; }

  std::vector<libremidi::input_port> get_input_ports() const noexcept override
  {
    return get_ports<SPA_DIRECTION_OUTPUT>(*this->global_context);
  }

  std::vector<libremidi::output_port> get_output_ports() const noexcept override
  {
    return get_ports<SPA_DIRECTION_INPUT>(*this->global_context);
  }

  ~observer_pipewire()
  {
    stop_thread();
    destroy_context();
#if 0
    if (client && !configuration.context)
    {
      // If we own the client, deactivate it
      pipewire_deactivate(this->client);
      pipewire_client_close(this->client);
      this->client = nullptr;
    }
#endif
  }

  std::unordered_set<std::string> seen_input_ports;
  std::unordered_set<std::string> seen_output_ports;
};
}
