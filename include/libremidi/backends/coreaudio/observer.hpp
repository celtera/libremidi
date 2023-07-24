#pragma once
#include <libremidi/backends/coreaudio/config.hpp>
#include <libremidi/backends/coreaudio/helpers.hpp>
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
    if(configuration.client_name.empty())
      configuration.client_name = "libremidi observer";

    auto result = MIDIClientCreate(
        toCFString(configuration.client_name).get(),
        +[] (const MIDINotification* message, void* ctx) { ((observer_core*)ctx)->notify(message); },
        this,
        &client);

    if(result != noErr)
    {
      error<driver_error>(
          this->configuration, "midi_in_core: error creating MIDI client object: "
                                   + std::to_string(result));
      return;
    }

    if(configuration.on_create_context)
      configuration.on_create_context(client);
  }

  void notify(const MIDINotification* message)
  {
    switch(message->messageID)
    {
      case kMIDIMsgObjectAdded:
      {
        auto obj = reinterpret_cast<const MIDIObjectAddRemoveNotification*>(message);

        std::string name = get_string_property(obj->child, kMIDIPropertyName);
        std::string dname = get_string_property(obj->child, kMIDIPropertyDisplayName);
        int32_t uid = get_int_property(obj->child, kMIDIPropertyUniqueID);

        switch(obj->childType)
        {
          case kMIDIObjectType_Source:
            if(auto& cb = configuration.input_added)
              cb(uid, dname);
            break;
          case kMIDIObjectType_Destination:
            if(auto& cb = configuration.output_added)
              cb(uid, dname);
            break;
          default:
            break;
        }

        break;
      }

      case kMIDIMsgObjectRemoved:
      {
        auto obj = reinterpret_cast<const MIDIObjectAddRemoveNotification*>(message);

        std::string name = get_string_property(obj->child, kMIDIPropertyName);
        std::string dname = get_string_property(obj->child, kMIDIPropertyDisplayName);
        int32_t uid = get_int_property(obj->child, kMIDIPropertyUniqueID);

        switch(obj->childType)
        {
          case kMIDIObjectType_Source:
            if(auto& cb = configuration.input_removed)
              cb(uid, dname);
            break;
          case kMIDIObjectType_Destination:
            if(auto& cb = configuration.output_removed)
              cb(uid, dname);
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
