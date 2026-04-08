#pragma once
#include <libremidi/ump_endpoint_info.hpp>
#include <libremidi/backends/coremidi/helpers.hpp>
#include <libremidi/backends/coremidi_ump/endpoint_config.hpp>
#include <libremidi/detail/ump_endpoint_api.hpp>
#include <libremidi/error_handler.hpp>

#include <map>

NAMESPACE_LIBREMIDI::coremidi_ump
{

class endpoint_observer_impl final
    : public ump_endpoint_observer_api
    , public error_handler
{
public:
  struct
      : libremidi::observer_configuration
      , coremidi_ump::endpoint_observer_api_configuration
  {
  } configuration;

  endpoint_observer_impl(
      libremidi::observer_configuration&& conf,
      coremidi_ump::endpoint_observer_api_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    OSStatus result = MIDIClientCreate(
        toCFString(configuration.client_name).get(),
        +[](const MIDINotification* message, void* ctx) {
            static_cast<endpoint_observer_impl*>(ctx)->on_notify(message);
        },
        this, &m_client);

    if (result != noErr)
    {
      valid_ = std::errc::io_error;
      return;
    }

    if (configuration.on_create_context)
      configuration.on_create_context(m_client);

    enumerate_endpoints();
    valid_ = stdx::error{};
  }

  ~endpoint_observer_impl() override
  {
    if (m_client)
      MIDIClientDispose(m_client);
  }

  libremidi::API get_current_api() const noexcept override
  {
    return libremidi::API::COREMIDI_UMP;
  }

  std::vector<ump_endpoint_info> get_endpoints() const noexcept override
  {
    return cached_endpoints_;
  }

  stdx::error refresh() noexcept override
  {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);
    cached_endpoints_.clear();
    enumerate_endpoints();
    return stdx::error{};
  }

private:
  void on_notify(const MIDINotification* message)
  {
    switch (message->messageID)
    {
      case kMIDIMsgObjectAdded:
      case kMIDIMsgObjectRemoved:
        refresh();
        break;
      default:
        break;
    }
  }

  void enumerate_endpoints()
  {
    std::map<std::string, std::pair<MIDIEndpointRef, MIDIEndpointRef>> endpoints;

    auto num_sources = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < num_sources; ++i)
    {
      MIDIEndpointRef ep = MIDIGetSource(i);
      auto name = get_endpoint_name(ep);
      if (!name.empty())
        endpoints[name].first = ep;
    }

    auto num_dests = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < num_dests; ++i)
    {
      MIDIEndpointRef ep = MIDIGetDestination(i);
      auto name = get_endpoint_name(ep);
      if (!name.empty())
        endpoints[name].second = ep;
    }

    for (auto& [name, eps] : endpoints)
    {
      if (!eps.first && !eps.second)
        continue;

      ump_endpoint_info info;
      info.api = libremidi::API::COREMIDI_UMP;
      info.name = name;
      info.display_name = name;

      bool is_virtual = is_virtual_endpoint(eps.first ? eps.first : eps.second);
      info.transport = is_virtual
          ? endpoint_transport_type::virtual_port
          : endpoint_transport_type::unknown;

      SInt32 proto = 0;
      MIDIEndpointRef ref = eps.second ? eps.second : eps.first;
      if (MIDIObjectGetIntegerProperty(ref, kMIDIPropertyProtocolID, &proto) == noErr
          && proto == kMIDIProtocol_2_0)
      {
        info.active_protocol = midi_protocol::midi2;
        info.supported_protocols = midi_protocol::both;
      }
      else
      {
        info.active_protocol = midi_protocol::midi1;
        info.supported_protocols = midi_protocol::midi1;
      }

      info.endpoint_id = std::to_string(eps.first) + ":" + std::to_string(eps.second);

      function_block_info fb;
      fb.block_id = 0;
      fb.active = true;
      fb.groups = {0, 16};
      fb.name = name;

      if (eps.first && eps.second)
        fb.direction = function_block_direction::bidirectional;
      else if (eps.first)
        fb.direction = function_block_direction::output;
      else
        fb.direction = function_block_direction::input;

      fb.ui_hint = function_block_ui_hint::both;
      info.function_blocks.push_back(std::move(fb));

      cached_endpoints_.push_back(std::move(info));
    }
  }

  static std::string get_endpoint_name(MIDIEndpointRef ep)
  {
    if (!ep)
      return {};

    CFStringRef name = nullptr;
    if (MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &name) != noErr || !name)
      return {};

    char buf[512] = {};
    CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8);
    CFRelease(name);
    return std::string(buf);
  }

  static bool is_virtual_endpoint(MIDIEndpointRef ep)
  {
    if (!ep)
      return false;

    CFStringRef model = nullptr;
    MIDIObjectGetStringProperty(ep, kMIDIPropertyModel, &model);
    if (model)
    {
      CFRelease(model);
      return false;
    }
    return true;
  }

  MIDIClientRef m_client{0};
  std::vector<ump_endpoint_info> cached_endpoints_;
};

}
