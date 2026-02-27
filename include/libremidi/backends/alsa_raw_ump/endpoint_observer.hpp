#if 0
  #pragma once
  #include <libremidi/backends/alsa_raw/helpers.hpp>
  #include <libremidi/backends/alsa_raw_ump/endpoint_config.hpp>
  #include <libremidi/backends/linux/alsa.hpp>
  #include <libremidi/backends/linux/helpers.hpp>
  #include <libremidi/detail/ump_endpoint_api.hpp>
  #include <libremidi/error_handler.hpp>

  #include <alsa/asoundlib.h>
  #if LIBREMIDI_ALSA_HAS_UMP
    #include <alsa/ump.h>
  #endif

namespace libremidi::alsa_raw_ump
{

/// Observer for discovering UMP endpoints on ALSA Raw
class endpoint_observer_impl final
    : public ump_endpoint_observer_api
    , public error_handler
{
public:
  struct
      : libremidi::ump_endpoint_observer_configuration
      , alsa_raw_ump::endpoint_observer_api_configuration
  {
  } configuration;

  const libasound& snd = libasound::instance();

  explicit endpoint_observer_impl(
      libremidi::ump_endpoint_observer_configuration&& conf,
      alsa_raw_ump::endpoint_observer_api_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
  #if LIBREMIDI_ALSA_HAS_UMP
    if (!snd.ump.available)
    {
      valid_ = std::errc::function_not_supported;
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

  ~endpoint_observer_impl() override = default;

  libremidi::API get_current_api() const noexcept override
  {
    return libremidi::API::ALSA_RAW_UMP;
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
  #if LIBREMIDI_ALSA_HAS_UMP
  void enumerate_endpoints()
  {
    int card = -1;

    while (snd.card.next(&card) >= 0 && card >= 0)
    {
      enumerate_card(card);
    }
  }

  void enumerate_card(int card)
  {
    char name[32];
    snprintf(name, sizeof(name), "hw:%d", card);

    snd_ctl_t* ctl = nullptr;
    if (snd.ctl.open(&ctl, name, 0) < 0)
      return;

    // Get card name
    char* card_name = nullptr;
    snd.card.get_name(card, &card_name);
    std::string card_name_str = card_name ? card_name : "";
    free(card_name);

    int device = -1;
    while (snd.ctl.ump.next_device(ctl, &device) >= 0 && device >= 0)
    {
      enumerate_device(ctl, card, device, card_name_str);
    }

    snd.ctl.close(ctl);
  }

  void enumerate_device(snd_ctl_t* ctl, int card, int device, const std::string& card_name)
  {
    // Build device name
    char dev_name[64];
    snprintf(dev_name, sizeof(dev_name), "hw:%d,%d", card, device);

    // Try to open the UMP endpoint to get full information
    snd_ump_t* ump_in = nullptr;
    snd_ump_t* ump_out = nullptr;

    // Try bidirectional first (both pointers), then input-only, then output-only
    // The mode parameter is for flags like SND_RAWMIDI_NONBLOCK, not direction
    int err = snd.ump.open(&ump_in, &ump_out, dev_name, 0);
    if (err < 0)
    {
      // Try input-only
      err = snd.ump.open(&ump_in, nullptr, dev_name, 0);
      if (err < 0)
      {
        // Try output-only
        err = snd.ump.open(nullptr, &ump_out, dev_name, 0);
      }
    }

    snd_ump_t* ump_handle = ump_in ? ump_in : ump_out;
    if (!ump_handle)
      return;

    ump_endpoint_info ep_info;
    ep_info.endpoint_id = std::string(dev_name);

    // Get endpoint information
    snd_ump_endpoint_info_t* ep = nullptr;
    snd_ump_endpoint_info_alloca(&ep);

    if (snd.ump.endpoint_info(ump_handle, ep) >= 0)
    {
      // Basic info
      const char* ep_name = snd.ump.endpoint_info_get_name(ep);
      ep_info.name = ep_name ? ep_name : card_name;
      ep_info.display_name = ep_info.name;
      ep_info.manufacturer = card_name;

      // Protocol capabilities
      unsigned int proto_caps = snd.ump.endpoint_info_get_protocol_caps(ep);
      if ((proto_caps & SND_UMP_EP_INFO_PROTO_MIDI1) && (proto_caps & SND_UMP_EP_INFO_PROTO_MIDI2))
        ep_info.supported_protocols = midi_protocol::both;
      else if (proto_caps & SND_UMP_EP_INFO_PROTO_MIDI2)
        ep_info.supported_protocols = midi_protocol::midi2;
      else
        ep_info.supported_protocols = midi_protocol::midi1;

      // Current protocol
      unsigned int proto = snd.ump.endpoint_info_get_protocol(ep);
      ep_info.active_protocol
          = (proto & SND_UMP_EP_INFO_PROTO_MIDI2) ? midi_protocol::midi2 : midi_protocol::midi1;

      // JR Timestamps
      if (proto_caps & SND_UMP_EP_INFO_PROTO_JRTS_TX)
        ep_info.jr_timestamps.can_transmit = true;
      if (proto_caps & SND_UMP_EP_INFO_PROTO_JRTS_RX)
        ep_info.jr_timestamps.can_receive = true;

      // Version
      unsigned int version = snd.ump.endpoint_info_get_version(ep);
      ep_info.version.major = (version >> 8) & 0xFF;
      ep_info.version.minor = version & 0xFF;

      // Device identity
      unsigned int mfg_id = snd.ump.endpoint_info_get_manufacturer_id(ep);
      if (mfg_id != 0)
      {
        midi_device_identity identity;
        identity.manufacturer_id = mfg_id;
        identity.family_id = snd.ump.endpoint_info_get_family_id(ep);
        identity.model_id = snd.ump.endpoint_info_get_model_id(ep);

        const unsigned char* sw_rev = snd.ump.endpoint_info_get_sw_revision(ep);
        if (sw_rev)
        {
          identity.software_revision[0] = sw_rev[0];
          identity.software_revision[1] = sw_rev[1];
          identity.software_revision[2] = sw_rev[2];
          identity.software_revision[3] = sw_rev[3];
        }
        ep_info.device_identity = identity;
      }

      // Product instance ID
      const char* product_id = snd.ump.endpoint_info_get_product_id(ep);
      if (product_id)
        ep_info.product_instance_id = product_id;

      // Static function blocks flag
      unsigned int flags = snd.ump.endpoint_info_get_flags(ep);
      ep_info.static_function_blocks = (flags & SND_UMP_EP_INFO_STATIC_BLOCKS) != 0;

      // Enumerate function blocks
      unsigned int num_blocks = snd.ump.endpoint_info_get_num_blocks(ep);
      for (unsigned int i = 0; i < num_blocks; ++i)
      {
        enumerate_block(ump_handle, i, ep_info);
      }
    }

    // Set transport type based on device path (heuristic)
    ep_info.transport = endpoint_transport_type::usb; // Most ALSA MIDI is USB

    // Close handles
    if (ump_in)
      snd.ump.close(ump_in);
    if (ump_out && ump_out != ump_in)
      snd.ump.close(ump_out);

    cached_endpoints_.push_back(std::move(ep_info));
  }

  void enumerate_block(snd_ump_t* ump_handle, unsigned int block_id, ump_endpoint_info& ep_info)
  {
    snd_ump_block_info_t* block = nullptr;
    snd_ump_block_info_alloca(&block);
    snd.ump.block_info_set_block_id(block, block_id);

    if (snd.ump.block_info(ump_handle, block) < 0)
      return;

    function_block_info fb;
    fb.block_id = snd.ump.block_info_get_block_id(block);
    fb.active = snd.ump.block_info_get_active(block) != 0;

    // Direction
    unsigned int dir = snd.ump.block_info_get_direction(block);
    switch (dir)
    {
      case SND_UMP_DIR_INPUT:
        fb.direction = function_block_direction::input;
        break;
      case SND_UMP_DIR_OUTPUT:
        fb.direction = function_block_direction::output;
        break;
      case SND_UMP_DIR_BIDIRECTION:
      default:
        fb.direction = function_block_direction::bidirectional;
        break;
    }

    // UI hint
    unsigned int ui_hint = snd.ump.block_info_get_ui_hint(block);
    switch (ui_hint)
    {
      case SND_UMP_BLOCK_UI_HINT_RECEIVER:
        fb.ui_hint = function_block_ui_hint::receiver;
        break;
      case SND_UMP_BLOCK_UI_HINT_SENDER:
        fb.ui_hint = function_block_ui_hint::sender;
        break;
      case SND_UMP_BLOCK_UI_HINT_BOTH:
        fb.ui_hint = function_block_ui_hint::both;
        break;
      default:
        fb.ui_hint = function_block_ui_hint::unknown;
        break;
    }

    // Groups
    fb.groups.first_group = snd.ump.block_info_get_first_group(block);
    fb.groups.num_groups = snd.ump.block_info_get_num_groups(block);

    // Flags
    unsigned int flags = snd.ump.block_info_get_flags(block);
    fb.is_midi1 = (flags & SND_UMP_BLOCK_IS_MIDI1) != 0;
    fb.is_low_speed = (flags & SND_UMP_BLOCK_IS_LOWSPEED) != 0;

    // MIDI-CI
    fb.midi_ci_version = snd.ump.block_info_get_midi_ci_version(block);
    fb.max_sysex8_streams = snd.ump.block_info_get_sysex8_streams(block);

    // Name
    const char* name = snd.ump.block_info_get_name(block);
    fb.name = name ? name : ("Block " + std::to_string(block_id));

    ep_info.function_blocks.push_back(std::move(fb));
  }
  #endif

  std::vector<ump_endpoint_info> cached_endpoints_;
};

} // namespace libremidi::alsa_raw_ump
#endif
