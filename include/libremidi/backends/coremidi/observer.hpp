#pragma once
#include <libremidi/backends/coremidi/config.hpp>
#include <libremidi/backends/coremidi/helpers.hpp>
#include <libremidi/detail/observer.hpp>

#include <CoreMIDI/CoreMIDI.h>

namespace libremidi
{
class observer_core final
    : public observer_api
    , public error_handler
{
public:
  struct
      : observer_configuration
      , coremidi_observer_configuration
  {
  } configuration;

  explicit observer_core(observer_configuration&& conf, coremidi_observer_configuration&& apiconf)
      : configuration{std::move(conf), std::move(apiconf)}
  {
    if (configuration.client_name.empty())
      configuration.client_name = "libremidi observer";

    if (!configuration.has_callbacks())
      return;

    auto result = MIDIClientCreate(
        toCFString(configuration.client_name).get(),
        +[](const MIDINotification* message, void* ctx) {
          ((observer_core*)ctx)->notify(message);
        },
        this, &client);

    if (result != noErr)
    {
      error<driver_error>(
          this->configuration,
          "midi_in_core: error creating MIDI client object: " + std::to_string(result));
      return;
    }

    if (configuration.on_create_context)
      configuration.on_create_context(client);
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::MACOSX_CORE; }

  std::vector<libremidi::port_information> get_input_ports() const noexcept override
  {
    std::vector<libremidi::port_information> ret;

    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);
    for (ItemCount i = 0; i < MIDIGetNumberOfSources(); i++)
    {
      if (auto p = to_port_info(MIDIGetSource(i)))
        ret.push_back(std::move(*p));
    }

    return ret;
  }

  std::vector<libremidi::port_information> get_output_ports() const noexcept override
  {
    std::vector<libremidi::port_information> ret;

    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);
    for (ItemCount i = 0; i < MIDIGetNumberOfDestinations(); i++)
    {
      if (auto p = to_port_info(MIDIGetDestination(i)))
        ret.push_back(std::move(*p));
    }
    return ret;
  }

  std::optional<port_information> to_port_info(MIDIObjectRef obj) const noexcept
  {
    MIDIEntityRef e{};
    MIDIEndpointGetEntity(obj, &e);
    bool physical = bool(e);

    bool ok = false;
    if (physical && this->configuration.track_hardware)
      ok = true;
    else if ((!physical) && this->configuration.track_virtual)
      ok = true;

    if (!ok)
      return {};

    return port_information{
        .client = (std::uintptr_t)this->client,
        .port = std::bit_cast<uint32_t>(get_int_property(obj, kMIDIPropertyUniqueID)),
        .manufacturer = get_string_property(obj, kMIDIPropertyManufacturer),
        .device_name = get_string_property(obj, kMIDIPropertyModel),
        .port_name = get_string_property(obj, kMIDIPropertyName),
        .display_name = get_string_property(obj, kMIDIPropertyDisplayName)};
  }

  void notify(const MIDINotification* message)
  {
    switch (message->messageID)
    {
      case kMIDIMsgObjectAdded: {
        auto obj = reinterpret_cast<const MIDIObjectAddRemoveNotification*>(message);

        switch (obj->childType)
        {
          case kMIDIObjectType_Source:
            if (auto& cb = configuration.input_added)
              if (auto p = to_port_info(obj->child))
                cb(std::move(*p));
            break;
          case kMIDIObjectType_Destination:
            if (auto& cb = configuration.output_added)
              if (auto p = to_port_info(obj->child))
                cb(std::move(*p));
            break;
          default:
            break;
        }

        break;
      }

      case kMIDIMsgObjectRemoved: {
        auto obj = reinterpret_cast<const MIDIObjectAddRemoveNotification*>(message);

        switch (obj->childType)
        {
          case kMIDIObjectType_Source:
            if (auto& cb = configuration.input_removed)
              if (auto p = to_port_info(obj->child))
                cb(std::move(*p));
            break;
          case kMIDIObjectType_Destination:
            if (auto& cb = configuration.output_removed)
              if (auto p = to_port_info(obj->child))
                cb(std::move(*p));
            break;
          default:
            break;
        }

        break;
      }

      default:
        break;
    }
  }

  ~observer_core() { MIDIClientDispose(this->client); }

private:
  MIDIClientRef client{};
};
}