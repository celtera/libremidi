#pragma once
#include <libremidi/backends/jack/config.hpp>
#include <libremidi/backends/jack/helpers.hpp>
#include <libremidi/detail/observer.hpp>

namespace libremidi
{
class observer_jack final
    : public observer_api
    , private jack_client
    , private error_handler
{
public:
  struct
      : observer_configuration
      , jack_observer_configuration
  {
  } configuration;

  explicit observer_jack(observer_configuration&& conf, jack_observer_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    // Initialize JACK client
    if (configuration.context)
    {
      this->client = configuration.context;

      if (configuration.has_callbacks())
        jack_set_port_registration_callback(this->client, JackPortRegistrationCallback{}, this);
    }
    else
    {
      jack_status_t status{};
      this->client
          = jack_client_open(configuration.client_name.c_str(), JackNoStartServer, &status);
      if (this->client != nullptr)
      {
        if (status != jack_status_t{})
          warning(configuration, "observer_jack: " + std::to_string((int)jack_status_t{}));

        if (configuration.has_callbacks())
        {
          jack_set_port_registration_callback(this->client, JackPortRegistrationCallback{}, this);
        }
        jack_activate(this->client);
      }
    }
  }

  int process(jack_nframes_t nframes) { return 0; }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::UNIX_JACK; }

  std::vector<libremidi::port_information> get_input_ports() const noexcept override
  {
    return get_ports(this->client, JackPortIsInput);
  }

  std::vector<libremidi::port_information> get_output_ports() const noexcept override
  {
    return get_ports(this->client, JackPortIsOutput);
  }

  ~observer_jack() { }
};
}
