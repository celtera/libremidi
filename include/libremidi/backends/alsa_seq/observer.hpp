#pragma once
#include <libremidi/backends/alsa_seq/config.hpp>
#include <libremidi/backends/alsa_seq/helpers.hpp>
#include <libremidi/backends/linux/helpers.hpp>
#include <libremidi/detail/midi_api.hpp>
#include <libremidi/detail/observer.hpp>

#include <alsa/asoundlib.h>

#include <bitset>
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

template <typename ConfigurationImpl>
class observer_impl
    : public observer_api
    , public alsa_data
    , public error_handler
{
public:
  struct
      : libremidi::observer_configuration
      , ConfigurationImpl
  {
  } configuration;

  explicit observer_impl(libremidi::observer_configuration&& conf, ConfigurationImpl&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    using namespace std::literals;
    if (int err = init_client(configuration); err < 0)
    {
      libremidi_handle_error(
          this->configuration,
          "error creating ALSA sequencer client "
          "object.");
      return;
    }

    if (!configuration.has_callbacks())
      return;

    // Init with the existing ports
    if (configuration.notify_in_constructor)
      init_all_ports();

    // Create the port to listen on the server events
    {
#if __has_include(<alsa/ump.h>)
      constexpr int midi2_cap
          = ConfigurationImpl::midi_version == 2 ? SND_SEQ_PORT_CAP_UMP_ENDPOINT : 0;
#else
      constexpr int midi2_cap = 0;
#endif

      constexpr int caps = SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE
                           | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE | midi2_cap;
      int err = alsa_data::create_port(
          *this, "libremidi-observe", caps, SND_SEQ_PORT_TYPE_APPLICATION, false);
      if (err < 0)
      {
        libremidi_handle_error(this->configuration, "error creating ALSA sequencer port.");
        return;
      }
    }

    // Connect the ALSA server events to our port
    {
      int err
          = snd.seq.connect_from(seq, vport, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
      if (err < 0)
      {
        libremidi_handle_error(this->configuration, "error connecting to ALSA sequencer.");
        return;
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
    if (int err = snd.seq.get_any_client_info(seq, client, cinfo); err < 0)
      return std::nullopt;

    snd_seq_port_info_t* pinfo;
    snd_seq_port_info_alloca(&pinfo);
    if (int err = snd.seq.get_any_port_info(seq, client, port, pinfo); err < 0)
      return std::nullopt;

    const auto tp = snd.seq.port_info_get_type(pinfo);
    bool ok = this->configuration.track_any;
    if ((tp & SND_SEQ_PORT_TYPE_HARDWARE) && this->configuration.track_hardware)
      ok = true;
    else if ((tp & SND_SEQ_PORT_TYPE_SOFTWARE) && this->configuration.track_virtual)
      ok = true;
    if (!ok)
      return {};

    if (auto name = snd.seq.client_info_get_name(cinfo))
      p.client_name = name;

    if (auto name = snd.seq.port_info_get_name(pinfo))
      p.port_name = name;

    auto cap = snd.seq.port_info_get_capability(pinfo);
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
        snd, this->seq, [this](snd_seq_client_info_t& client, snd_seq_port_info_t& port) {
          int clt = snd.seq.client_info_get_client(&client);
          int pt = snd.seq.port_info_get_port(&port);
          register_port(clt, pt);
        });
  }

  libremidi::API get_current_api() const noexcept override
  {
    if constexpr (ConfigurationImpl::midi_version == 1)
      return libremidi::API::ALSA_SEQ;
    else
      return libremidi::API::ALSA_SEQ_UMP;
  }

  std::vector<libremidi::input_port> get_input_ports() const noexcept override
  {
    std::vector<libremidi::input_port> ret;
    alsa_seq::for_all_ports(
        snd, this->seq, [this, &ret](snd_seq_client_info_t& client, snd_seq_port_info_t& port) {
          int clt = snd.seq.client_info_get_client(&client);
          int pt = snd.seq.port_info_get_port(&port);
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
        snd, this->seq, [this, &ret](snd_seq_client_info_t& client, snd_seq_port_info_t& port) {
          int clt = snd.seq.client_info_get_client(&client);
          int pt = snd.seq.port_info_get_port(&port);
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
    if (p.client == snd.seq.client_id(seq))
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
        snd.seq.delete_port(seq, vport);

      if (!configuration.context)
        snd.seq.close(seq);
    }
  }

private:
  std::map<std::pair<int, int>, port_info> knownClients_;
};

template <typename ConfigurationImpl>
class observer_threaded : public observer_impl<ConfigurationImpl>
{
public:
  observer_threaded(libremidi::observer_configuration&& conf, ConfigurationImpl&& apiconf)
      : observer_impl<ConfigurationImpl>{std::move(conf), std::move(apiconf)}
  {
    // Create relevant descriptors
    auto& snd = alsa_data::snd;
    const auto N = snd.seq.poll_descriptors_count(this->seq, POLLIN);
    descriptors_.resize(N + 1);
    snd.seq.poll_descriptors(this->seq, descriptors_.data(), N, POLLIN);
    descriptors_.back() = this->termination_event;

    // Start the listening thread
    thread = std::thread{[this] {
      auto& snd = alsa_data::snd;
      for (;;)
      {
        int err = poll(descriptors_.data(), descriptors_.size(), -1);
        if (err >= 0)
        {
          // We got our stop-thread signal
          if (descriptors_.back().revents & POLLIN)
            break;

          snd_seq_event_t* ev{};
          event_handle handle{snd};
          while (snd.seq.event_input(this->seq, &ev) >= 0)
          {
            handle.reset(ev);
            this->handle_event(*ev);
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

template <typename ConfigurationImpl>
class observer_manual : public observer_impl<ConfigurationImpl>
{
public:
  observer_manual(libremidi::observer_configuration&& conf, ConfigurationImpl&& apiconf)
      : observer_impl<ConfigurationImpl>{std::move(conf), std::move(apiconf)}
  {
    this->configuration.manual_poll(
        poll_parameters{.addr = this->vaddr, .callback = [this](const auto& v) {
                          this->handle_event(v);
                          return 0;
                        }});
  }

  ~observer_manual() { this->configuration.stop_poll(this->vaddr); }
};
}

namespace libremidi
{
template <>
inline std::unique_ptr<observer_api>
make<alsa_seq::observer_impl<alsa_seq::observer_configuration>>(
    libremidi::observer_configuration&& conf, libremidi::alsa_seq::observer_configuration&& api)
{
  if (api.manual_poll)
    return std::make_unique<alsa_seq::observer_manual<alsa_seq::observer_configuration>>(
        std::move(conf), std::move(api));
  else
    return std::make_unique<alsa_seq::observer_threaded<alsa_seq::observer_configuration>>(
        std::move(conf), std::move(api));
}
}
