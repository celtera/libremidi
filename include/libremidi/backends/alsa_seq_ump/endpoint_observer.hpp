#if 0
  #pragma once
  #include <libremidi/backends/alsa_seq/helpers.hpp>
  #include <libremidi/backends/alsa_seq/observer.hpp>
  #include <libremidi/backends/alsa_seq_ump/endpoint_config.hpp>
  #include <libremidi/backends/linux/helpers.hpp>
  #include <libremidi/detail/ump_endpoint_api.hpp>
  #include <libremidi/error_handler.hpp>

  #include <alsa/asoundlib.h>

  #if LIBREMIDI_HAS_UDEV
    #include <libremidi/backends/linux/udev.hpp>
  #endif

  #include <map>

namespace libremidi::alsa_seq_ump
{

/// Observer for discovering UMP endpoints on ALSA Sequencer
class endpoint_observer_impl final
    : public ump_endpoint_observer_api
    , public alsa_seq::alsa_data
    , public error_handler
{
public:
  using port_info = libremidi::alsa_seq::port_info;
  struct
      : libremidi::observer_configuration
      , alsa_seq_ump::endpoint_observer_api_configuration
  {
  } configuration;

  explicit endpoint_observer_impl(
      libremidi::observer_configuration&& conf,
      alsa_seq_ump::endpoint_observer_api_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
  #if LIBREMIDI_ALSA_HAS_UMP
    if (!snd.seq.available || !snd.seq.ump.available)
    {
      valid_ = std::errc::function_not_supported;
      return;
    }

    // Initialize the sequencer client
    if (init_seq_client() < 0)
    {
      valid_ = std::errc::io_error;
      return;
    }

    // Initial enumeration
    enumerate_endpoints();

    if (configuration.notify_existing_in_constructor && configuration.endpoint_added)
    {
      for (const auto& ep : cached_endpoints_)
        configuration.endpoint_added(ep);
    }

    valid_ = stdx::error{};
  #else
    valid_ = std::errc::function_not_supported;
  #endif
  }

  ~endpoint_observer_impl() override
  {
    if (seq && !configuration.context)
      snd.seq.close(seq);
  }

  libremidi::API get_current_api() const noexcept override
  {
    return libremidi::API::ALSA_SEQ_UMP;
  }

  std::vector<ump_endpoint_info> get_endpoints() const noexcept override
  {
    return cached_endpoints_;
  }

  stdx::error refresh() noexcept override
  {
  #if LIBREMIDI_ALSA_HAS_UMP
    auto old_endpoints = std::move(cached_endpoints_);
    cached_endpoints_.clear();

    enumerate_endpoints();

    // Notify changes
    if (configuration.has_callbacks())
    {
      // Find removed endpoints
      for (const auto& old_ep : old_endpoints)
      {
        bool found = false;
        for (const auto& new_ep : cached_endpoints_)
        {
          if (old_ep.endpoint_id == new_ep.endpoint_id)
          {
            found = true;
            break;
          }
        }
        if (!found && configuration.endpoint_removed)
          configuration.endpoint_removed(old_ep);
      }

      // Find added endpoints
      for (const auto& new_ep : cached_endpoints_)
      {
        bool found = false;
        for (const auto& old_ep : old_endpoints)
        {
          if (old_ep.endpoint_id == new_ep.endpoint_id)
          {
            found = true;
            break;
          }
        }
        if (!found && configuration.endpoint_added)
          configuration.endpoint_added(new_ep);
      }
    }

    return stdx::error{};
  #else
    return std::errc::function_not_supported;
  #endif
  }

private:
  std::optional<port_info> get_info(
      int client, int port, snd_seq_client_info_t& cinfo,
      snd_seq_port_info_t& pinfo) const noexcept
  {
    const auto tp = snd.seq.port_info_get_type(&pinfo);
    const auto cap = snd.seq.port_info_get_capability(&pinfo);
    if ((cap & SND_SEQ_PORT_CAP_NO_EXPORT) != 0)
      return std::nullopt;

    port_info p;
    p.client = client;
    p.port = port;

    bool ok = this->configuration.track_any;

    static constexpr auto virtual_port = SND_SEQ_PORT_TYPE_SOFTWARE | SND_SEQ_PORT_TYPE_SYNTHESIZER
                                         | SND_SEQ_PORT_TYPE_APPLICATION;

    if ((tp & SND_SEQ_PORT_TYPE_HARDWARE) && this->configuration.track_hardware)
    {
      p.type = libremidi::transport_type::hardware;
      ok = true;
    }
    else if ((tp & virtual_port) && this->configuration.track_virtual)
    {
      p.type = libremidi::transport_type::software;
      ok = true;
    }
    if (!ok)
      return {};

    if (auto name = snd.seq.client_info_get_name(&cinfo))
      p.client_name = name;

    if (auto name = snd.seq.port_info_get_name(&pinfo))
      p.port_name = name;

    if (int card = snd.seq.client_info_get_card(&cinfo); card >= 0)
      p.card = card;

    p.is_input = (cap & SND_SEQ_PORT_CAP_DUPLEX) | (cap & SND_SEQ_PORT_CAP_READ);
    p.is_output = (cap & SND_SEQ_PORT_CAP_DUPLEX) | (cap & SND_SEQ_PORT_CAP_WRITE);

    return p;
  }

  #if LIBREMIDI_ALSA_HAS_UMP
  int init_seq_client()
  {
    if (configuration.context)
    {
      seq = configuration.context;
      return 0;
    }

    int ret = snd.seq.open(&seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);
    if (ret < 0)
      return ret;

    if (!configuration.client_name.empty())
      snd.seq.set_client_name(seq, configuration.client_name.c_str());

    // Set to MIDI 2.0 mode
    if (snd.seq.set_client_midi_version)
      snd.seq.set_client_midi_version(seq, SND_SEQ_CLIENT_UMP_MIDI_2_0);

    return 0;
  }

  void enumerate_endpoints()
  {
    // Group ports by client to form endpoints
    std::map<int, std::vector<port_info>> clients_ports;

    alsa_seq::for_all_ports(
        snd, seq, [this, &clients_ports](snd_seq_client_info_t& cinfo, snd_seq_port_info_t& pinfo) {
      int client = snd.seq.client_info_get_client(&cinfo);
      int port = snd.seq.port_info_get_port(&pinfo);

      // Skip our own client
      if (client == snd.seq.client_id(seq))
        return;

      // Skip system client
      if (client == 0)
        return;

      auto port_opt = get_info(client, port, cinfo, pinfo);
      if (port_opt)
        clients_ports[client].push_back(*port_opt);
    });

    // Convert grouped ports to endpoints
    for (auto& [client_id, ports] : clients_ports)
    {
      if (ports.empty())
        continue;

      create_endpoint_from_ports(client_id, ports);
    }
  }

  void create_endpoint_from_ports(int client_id, const std::vector<port_info>& ports)
  {
    if (ports.empty())
      return;

    const auto& first_port = ports.front();

    ump_endpoint_info ep_info;

    // Create a unique endpoint ID from client:port
    // For multi-port clients, use client ID
    if (ports.size() == 1)
    {
      ep_info.endpoint_id
          = std::to_string(first_port.client) + ":" + std::to_string(first_port.port);
    }
    else
    {
      ep_info.endpoint_id = std::to_string(client_id);
    }

    ep_info.name = first_port.client_name;
    ep_info.display_name = first_port.client_name;
    ep_info.manufacturer = "";

    // Set protocol - ALSA Seq UMP always supports both
    ep_info.supported_protocols = midi_protocol::both;
    ep_info.active_protocol = midi_protocol::midi2;

    // Transport type
    // FIXME
    if (first_port.type & libremidi::transport_type::hardware)
      ep_info.transport = endpoint_transport_type::usb;
    else
      ep_info.transport = endpoint_transport_type::virtual_port;

    // Create function blocks from ports
    uint8_t block_id = 0;
    for (const auto& port : ports)
    {
      function_block_info fb;
      fb.block_id = block_id++;
      fb.active = true;
      fb.name = port.port_name;

      // Determine direction from port capabilities
      if (port.is_input && port.is_output)
        fb.direction = function_block_direction::bidirectional;
      else if (port.is_input)
        fb.direction = function_block_direction::input;
      else if (port.is_output)
        fb.direction = function_block_direction::output;
      else
        continue; // Skip ports with no direction

      // Set UI hint based on direction
      if (port.is_input && port.is_output)
        fb.ui_hint = function_block_ui_hint::both;
      else if (port.is_input)
        fb.ui_hint = function_block_ui_hint::sender;
      else
        fb.ui_hint = function_block_ui_hint::receiver;

      // Each port is group 0 for now (single group per block)
      fb.groups.first_group = 0;
      fb.groups.num_groups = 1;

      // Store port address in groups for later lookup
      // We encode client:port in a way that's retrievable
      fb.groups.first_group = static_cast<uint8_t>(port.port);

      ep_info.function_blocks.push_back(std::move(fb));
    }

    cached_endpoints_.push_back(std::move(ep_info));
  }
  #endif

  std::vector<ump_endpoint_info> cached_endpoints_;

  #if LIBREMIDI_HAS_UDEV
  udev_helper m_udev{};
  #endif
};

} // namespace libremidi::alsa_seq_ump

#endif
