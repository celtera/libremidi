#pragma once
#include <libremidi/backends/alsa_seq/config.hpp>
#include <libremidi/backends/alsa_seq/helpers.hpp>
#include <libremidi/backends/linux/helpers.hpp>
#include <libremidi/detail/observer.hpp>

#include <alsa/asoundlib.h>

#include <map>

namespace libremidi::alsa_seq
{

struct port_info
{
  std::string client_name;
  std::string port_name;
  int client{};
  int port{};
  bool isInput{};
  bool isOutput{};
};
class observer_impl
    : public observer_api
    , public alsa_data
{
public:
  struct
      : libremidi::observer_configuration
      , alsa_seq::observer_configuration
  {
  } configuration;

  explicit observer_impl(
      libremidi::observer_configuration&& conf, alsa_seq::observer_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    using namespace std::literals;
    if (int err = init_client(configuration); err < 0)
    {
      throw driver_error("observer_alsa: snd_seq_open failed");
    }

    if (!configuration.has_callbacks())
      return;

    // Init with the existing ports
    if (configuration.notify_in_constructor)
      init_all_ports();

    // Create the port to listen on the server events
    {
      int err = alsa_data::create_port(
          *this, "libremidi-observe",
          SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_READ
              | SND_SEQ_PORT_CAP_SUBS_WRITE,
          SND_SEQ_PORT_TYPE_APPLICATION, false);
      if (err < 0)
      {
        throw driver_error("observer: ALSA error creating port.");
      }
    }

    // Connect the ALSA server events to our port
    {
      int err
          = snd_seq_connect_from(seq, vport, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
      if (err < 0)
      {
        throw driver_error("observer_alsa: snd_seq_connect_from failed");
      }
    }
  }

  std::optional<port_info> get_info(int client, int port) const noexcept
  {
    port_info p;
    p.client = client;
    p.port = port;

    snd_seq_client_info_t* cinfo;
    snd_seq_client_info_alloca(&cinfo);
    if (int err = snd_seq_get_any_client_info(seq, client, cinfo); err < 0)
      return std::nullopt;

    snd_seq_port_info_t* pinfo;
    snd_seq_port_info_alloca(&pinfo);
    if (int err = snd_seq_get_any_port_info(seq, client, port, pinfo); err < 0)
      return std::nullopt;

    const auto tp = snd_seq_port_info_get_type(pinfo);
    bool ok = false;
    if((tp & SND_SEQ_PORT_TYPE_HARDWARE) && this->configuration.track_hardware)
      ok = true;
    else if((tp & SND_SEQ_PORT_TYPE_SOFTWARE) && this->configuration.track_virtual)
      ok = true;
    if(!ok)
      return {};

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

  template <bool Input>
  auto to_port_info(port_info p) const noexcept
      -> std::conditional_t<Input, input_port, output_port>
  {
    static_assert(sizeof(this->seq) <= sizeof(libremidi::client_handle));
    static_assert(sizeof(std::uintptr_t) <= sizeof(libremidi::client_handle));
    return {
        {.client = std::uintptr_t(this->seq),
         .port = alsa_seq::seq_to_port_handle(p.client, p.port),
         .manufacturer = "",
         .device_name = p.client_name,
         .port_name = p.port_name,
         .display_name = p.port_name}};
  }

  void init_all_ports()
  {
    alsa_seq::for_all_ports(
        this->seq, [this](snd_seq_client_info_t& client, snd_seq_port_info_t& port) {
          int clt = snd_seq_client_info_get_client(&client);
          int pt = snd_seq_port_info_get_port(&port);
          register_port(clt, pt);
        });
  }

  libremidi::API get_current_api() const noexcept override
  {
    return libremidi::API::ALSA_SEQ;
  }

  std::vector<libremidi::input_port> get_input_ports() const noexcept override
  {
    std::vector<libremidi::input_port> ret;
    alsa_seq::for_all_ports(
        this->seq, [this, &ret](snd_seq_client_info_t& client, snd_seq_port_info_t& port) {
          int clt = snd_seq_client_info_get_client(&client);
          int pt = snd_seq_port_info_get_port(&port);
          if (auto p = get_info(clt, pt))
            if (p->isInput)
              ret.push_back(to_port_info<true>(*p));
        });
    return ret;
  }

  std::vector<libremidi::output_port> get_output_ports() const noexcept override
  {
    std::vector<libremidi::output_port> ret;
    alsa_seq::for_all_ports(
        this->seq, [this, &ret](snd_seq_client_info_t& client, snd_seq_port_info_t& port) {
          int clt = snd_seq_client_info_get_client(&client);
          int pt = snd_seq_port_info_get_port(&port);
          if (auto p = get_info(clt, pt))
            if (p->isOutput)
              ret.push_back(to_port_info<false>(*p));
        });
    return ret;
  }

  void register_port(int client, int port)
  {
    auto pp = get_info(client, port);
    if (!pp)
      return;
    auto& p = *pp;
    if (p.client == snd_seq_client_id(seq))
      return;

    knownClients_[{p.client, p.port}] = p;
    if (p.isInput && configuration.input_added)
    {
      configuration.input_added(to_port_info<true>(p));
    }

    if (p.isOutput && configuration.output_added)
    {
      configuration.output_added(to_port_info<false>(p));
    }
  }

  void unregister_port(int client, int port)
  {
    auto it = knownClients_.find({client, port});
    if (it != knownClients_.end())
    {
      auto p = it->second;
      knownClients_.erase(it);

      if (p.isInput && configuration.input_removed)
      {
        configuration.input_removed(to_port_info<true>(p));
      }

      if (p.isOutput && configuration.output_removed)
      {
        configuration.output_removed(to_port_info<false>(p));
      }
    }
  }

  void handle_event(const snd_seq_event_t& ev)
  {
    switch (ev.type)
    {
      case SND_SEQ_EVENT_PORT_START: {
        register_port(ev.data.addr.client, ev.data.addr.port);
        break;
      }
      case SND_SEQ_EVENT_PORT_EXIT: {
        unregister_port(ev.data.addr.client, ev.data.addr.port);
        break;
      }
      case SND_SEQ_EVENT_PORT_CHANGE:
      default:
        break;
    }
  }

  ~observer_impl()
  {
    if (seq)
    {
      if (vport)
        snd_seq_delete_port(seq, vport);

      if (!configuration.context)
        snd_seq_close(seq);
    }
  }

private:
  std::map<std::pair<int, int>, port_info> knownClients_;
};

class observer_threaded : public observer_impl
{
public:
  observer_threaded(
      libremidi::observer_configuration&& conf, alsa_seq::observer_configuration&& apiconf)
      : observer_impl{std::move(conf), std::move(apiconf)}
  {
    // Create relevant descriptors
    const auto N = snd_seq_poll_descriptors_count(seq, POLLIN);
    descriptors_.resize(N + 1);
    snd_seq_poll_descriptors(seq, descriptors_.data(), N, POLLIN);
    descriptors_.back() = this->termination_event;

    // Start the listening thread
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
          snd_seq_event_t* ev{};
          libremidi::unique_handle<snd_seq_event_t, snd_seq_free_event> handle;
          while (snd_seq_event_input(seq, &ev) >= 0)
          {
            handle.reset(ev);
            handle_event(*ev);
          }
        }
      }
    }};
  }

  ~observer_threaded()
  {
    termination_event.notify();

    if (thread.joinable())
      thread.join();
  }

  eventfd_notifier termination_event{};
  std::thread thread;
  std::vector<pollfd> descriptors_;
};

class observer_manual : public observer_impl
{
public:
  observer_manual(
      libremidi::observer_configuration&& conf, alsa_seq::observer_configuration&& apiconf)
      : observer_impl{std::move(conf), std::move(apiconf)}
  {
    configuration.manual_poll(
        poll_parameters{.addr = this->vaddr, .callback = [this](const snd_seq_event_t& v) {
                          handle_event(v);
                          return 0;
                        }});
  }

  ~observer_manual() { configuration.stop_poll(this->vaddr); }
};
}

namespace libremidi
{
template <>
inline std::unique_ptr<observer_api> make<alsa_seq::observer_impl>(
    libremidi::observer_configuration&& conf, libremidi::alsa_seq::observer_configuration&& api)
{
  if (api.manual_poll)
    return std::make_unique<alsa_seq::observer_manual>(std::move(conf), std::move(api));
  else
    return std::make_unique<alsa_seq::observer_threaded>(std::move(conf), std::move(api));
}
}
