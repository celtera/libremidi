#pragma once
#include <libremidi/backends/alsa_seq/config.hpp>
#include <libremidi/backends/alsa_seq/helpers.hpp>
#include <libremidi/backends/linux/helpers.hpp>
#include <libremidi/detail/observer.hpp>

#include <alsa/asoundlib.h>

#include <map>

namespace libremidi
{
class observer_alsa final : public observer_api
{
public:
  struct
      : observer_configuration
      , alsa_sequencer_observer_configuration
  {
  } configuration;

  explicit observer_alsa(
      observer_configuration&& conf, alsa_sequencer_observer_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    using namespace std::literals;
    int err = snd_seq_open(&seq_, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);
    if (err < 0)
    {
      throw driver_error("observer_alsa: snd_seq_open failed");
    }

    client_ = snd_seq_client_id(seq_);

    // Init with the existing ports
    init_all_ports();

    if (!configuration.has_callbacks())
      return;

    // Create relevant descriptors
    const auto N = snd_seq_poll_descriptors_count(seq_, POLLIN);
    descriptors_.resize(N + 1);
    snd_seq_poll_descriptors(seq_, descriptors_.data(), N, POLLIN);
    descriptors_.back() = this->event_fd;

    err = snd_seq_set_client_name(seq_, "libremidi-observe");
    if (err < 0)
    {
      throw driver_error("observer_alsa: snd_seq_set_client_name failed");
    }

    err = snd_seq_create_simple_port(
        seq_, "libremidi-observe",
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_READ
            | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_APPLICATION);
    if (err < 0)
    {
      throw driver_error("observer_alsa: snd_seq_create_simple_port failed");
    }
    port_ = err;

    err = snd_seq_connect_from(seq_, port_, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
    if (err < 0)
    {
      throw driver_error("observer_alsa: snd_seq_connect_from failed");
    }

    thread = std::thread{[this] {
      for (;;)
      {
        int err = poll(descriptors_.data(), descriptors_.size(), -1);
        if (err >= 0)
        {
          // We got our stop-thread signal
          if (descriptors_.back().revents & POLLIN)
            break;

          // Otherwise handle ALSA events
          snd_seq_event_t* ev;
          while (snd_seq_event_input(seq_, &ev) >= 0)
          {
            handle_event(ev);
          }
        }
      }
    }};
  }

  struct alsa_seq_port_info
  {
    std::string client_name;
    std::string port_name;
    int client{};
    int port{};
    bool isInput{};
    bool isOutput{};
  };

  alsa_seq_port_info get_info(int client, int port) const noexcept
  {
    alsa_seq_port_info p;
    p.client = client;
    p.port = port;

    snd_seq_client_info_t* cinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_get_any_client_info(seq_, client, cinfo);

    snd_seq_port_info_t* pinfo;
    snd_seq_port_info_alloca(&pinfo);
    snd_seq_get_any_port_info(seq_, client, port, pinfo);

    if (auto name = snd_seq_client_info_get_name(cinfo))
      p.client_name = name;

    if (auto name = snd_seq_port_info_get_name(pinfo))
      p.port_name = name;

    auto cap = snd_seq_port_info_get_capability(pinfo);
    // FIXME isn't it missing SND_SEQ_PORT_CAP_SUBS_READ / WRITE??
    p.isInput = (cap & SND_SEQ_PORT_CAP_DUPLEX) | (cap & SND_SEQ_PORT_CAP_READ);
    p.isOutput = (cap & SND_SEQ_PORT_CAP_DUPLEX) | (cap & SND_SEQ_PORT_CAP_WRITE);

    return p;
  }

  libremidi::port_information to_port_info(alsa_seq_port_info p) const noexcept
  {
    static_assert(sizeof(this->seq_) <= sizeof(libremidi::client_handle));
    static_assert(sizeof(std::uintptr_t) <= sizeof(libremidi::client_handle));
    return {
        .client = std::uintptr_t(this->seq_),
        .port = alsa_seq::seq_to_port_handle(p.client, p.port),
        .manufacturer = "",
        .device_name = p.client_name,
        .port_name = p.port_name,
        .display_name = p.port_name};
  }

  void init_all_ports()
  {
    alsa_seq::for_all_ports(
        this->seq_, [this](snd_seq_client_info_t& client, snd_seq_port_info_t& port) {
          int clt = snd_seq_client_info_get_client(&client);
          int pt = snd_seq_port_info_get_port(&port);
          register_port(clt, pt);
        });
  }

  libremidi::API get_current_api() const noexcept override
  {
    return libremidi::API::LINUX_ALSA_SEQ;
  }

  std::vector<libremidi::port_information> get_input_ports() const noexcept override
  {
    std::vector<libremidi::port_information> ret;
    alsa_seq::for_all_ports(
        this->seq_, [this, &ret](snd_seq_client_info_t& client, snd_seq_port_info_t& port) {
          int clt = snd_seq_client_info_get_client(&client);
          int pt = snd_seq_port_info_get_port(&port);
          auto p = get_info(clt, pt);
          if (p.isInput)
            ret.push_back(to_port_info(p));
        });
    return ret;
  }

  std::vector<libremidi::port_information> get_output_ports() const noexcept override
  {
    std::vector<libremidi::port_information> ret;
    alsa_seq::for_all_ports(
        this->seq_, [this, &ret](snd_seq_client_info_t& client, snd_seq_port_info_t& port) {
          int clt = snd_seq_client_info_get_client(&client);
          int pt = snd_seq_port_info_get_port(&port);
          auto p = get_info(clt, pt);
          if (p.isOutput)
            ret.push_back(to_port_info(p));
        });
    return ret;
  }

  void register_port(int client, int port)
  {
    auto p = get_info(client, port);
    if (p.client == client_)
      return;

    knownClients_[{p.client, p.port}] = p;
    if (p.isInput && configuration.input_added)
    {
      configuration.input_added(to_port_info(p));
    }

    if (p.isOutput && configuration.output_added)
    {
      configuration.output_added(to_port_info(p));
    }
  }

  void unregister_port(int client, int port)
  {
    auto p = get_info(client, port);
    if (p.client == client_)
      return;

    auto it = knownClients_.find({p.client, p.port});
    if (it != knownClients_.end())
    {
      p = it->second;
      knownClients_.erase(it);
    }

    if (p.isInput && configuration.input_removed)
    {
      configuration.input_removed(to_port_info(p));
    }

    if (p.isOutput && configuration.output_added)
    {
      configuration.output_removed(to_port_info(p));
    }
  }

  void handle_event(snd_seq_event_t* ev)
  {
    switch (ev->type)
    {
      case SND_SEQ_EVENT_PORT_START: {
        register_port(ev->data.addr.client, ev->data.addr.port);
        break;
      }
      case SND_SEQ_EVENT_PORT_EXIT: {
        unregister_port(ev->data.addr.client, ev->data.addr.port);
        break;
      }
      case SND_SEQ_EVENT_PORT_CHANGE:
      default:
        break;
    }
  }

  ~observer_alsa()
  {
    event_fd.notify();

    if (thread.joinable())
      thread.join();

    if (seq_)
    {
      if (port_)
        snd_seq_delete_port(seq_, port_);
      snd_seq_close(seq_);
    }
  }

private:
  snd_seq_t* seq_{};
  eventfd_notifier event_fd{};
  std::thread thread;
  std::vector<pollfd> descriptors_;
  std::map<std::pair<int, int>, alsa_seq_port_info> knownClients_;
  int client_{};
  int port_{};
};
}
