#pragma once
#include <libremidi/backends/coremidi/config.hpp>
#include <libremidi/backends/coremidi/helpers.hpp>
#include <libremidi/detail/observer.hpp>

#include <CoreMIDI/CoreMIDI.h>

#include <bit>
#include <map>
#include <memory>
#include <type_traits>
#include <utility>

NAMESPACE_LIBREMIDI
{
class observer_core
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
  }

  void finish_init()
  {
    if (configuration.client_name.empty())
      configuration.client_name = "libremidi observer";

    if (!configuration.has_callbacks())
      return;

    auto result = MIDIClientCreate(
        toCFString(configuration.client_name).get(),
        +[](const MIDINotification* message, void* ctx) {
      ((observer_core*)ctx)->notify(message);
    }, this, &client);

    if (result != noErr)
    {
      libremidi_handle_error(
          this->configuration, "error creating MIDI client object: " + std::to_string(result));
      return;
    }

    // Seed both directions before user code can pump the client's run loop.
    const auto initial_inputs
        = seed(inputs, configuration.input_added, configuration.input_removed);
    const auto initial_outputs
        = seed(outputs, configuration.output_added, configuration.output_removed);

    if (configuration.on_create_context)
      configuration.on_create_context(client);

    announce(initial_inputs, inputs, configuration.input_added);
    announce(initial_outputs, outputs, configuration.output_added);
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::COREMIDI; }

  template <typename Port>
  auto to_port_info(MIDIEndpointRef obj) const noexcept -> std::optional<Port>
  {
    MIDIEntityRef e{};
    MIDIEndpointGetEntity(obj, &e);

    /*
     * No entity means a virtual endpoint, one an application published. An
     * entity means a device port, and `hardware` is the answer unless the
     * driver below says something more specific: CoreMIDI only names the
     * transport for drivers this backend recognises, and a DIN interface behind
     * a third-party driver is still hardware.
     */
    libremidi::transport_type type
        = e ? libremidi::transport_type::hardware : libremidi::transport_type::software;
    libremidi::container_identifier usb_location_id{};
    libremidi::device_identifier usb_vendor_product{};
    {
      MIDIDeviceRef device{};
      if (MIDIEntityGetDevice(e, &device) == noErr)
      {
        if (device)
        {
          using enum libremidi::transport_type;
          SInt32 devid_res{};
          if (MIDIObjectGetIntegerProperty(device, CFSTR("USBLocationID"), &devid_res) == noErr)
          {
            type = (libremidi::transport_type)(hardware | usb);
            usb_location_id = (uint64_t)devid_res;
          }

          SInt32 vendor_res{};
          if (MIDIObjectGetIntegerProperty(device, CFSTR("USBVendorProduct"), &vendor_res)
              == noErr)
          {
            type = (libremidi::transport_type)(hardware | usb);
            usb_vendor_product = (uint64_t)vendor_res;
          }

          const auto driver = get_string_property(device, kMIDIPropertyDriverOwner);
          if (driver == "com.apple.AppleMIDIUSBDriver")
            type = (libremidi::transport_type)(hardware | usb);
          if (driver == "com.apple.AppleMIDIBluetoothDriver")
            type = (libremidi::transport_type)(hardware | bluetooth);
          if (driver == "com.apple.AppleMIDIIACDriver")
            type = (libremidi::transport_type)(software);
          if (driver == "com.apple.AppleMIDIRTPDriver")
            type = (libremidi::transport_type)(network);
        }
      }
    }

    // Filtered after the transport is known, not on the presence of an entity:
    // the IAC bus and a network session are both device-backed, and it is the
    // driver above that makes one software and the other network.
    if (!this->configuration.accepts(type))
      return {};

    SInt32 unique_id;
    if (MIDIObjectGetIntegerProperty(obj, kMIDIPropertyUniqueID, &unique_id) != noErr)
      return {}; // The endpoint may already be gone when processing an add.

    return Port{
        {.api = get_current_api(),
         .client = (std::uintptr_t)this->client,
         .container = usb_location_id,
         .device = usb_vendor_product,
         .port = std::bit_cast<uint32_t>(unique_id),
         .manufacturer = get_string_property(obj, kMIDIPropertyManufacturer),
         .device_name = get_string_property(obj, kMIDIPropertyModel),
         .port_name = get_string_property(obj, kMIDIPropertyName),
         .display_name = get_string_property(obj, kMIDIPropertyDisplayName),
         .type = type}};
  }

  std::vector<libremidi::input_port> get_input_ports() const noexcept override
  {
    return list<input_port>();
  }

  std::vector<libremidi::output_port> get_output_ports() const noexcept override
  {
    return list<output_port>();
  }

  void notify(const MIDINotification* message)
  {
    if (message->messageID != kMIDIMsgObjectAdded && message->messageID != kMIDIMsgObjectRemoved)
      return;

    auto& obj = *reinterpret_cast<const MIDIObjectAddRemoveNotification*>(message);
    if (obj.childType == kMIDIObjectType_Source)
      update(obj, inputs, configuration.input_added, configuration.input_removed);
    else if (obj.childType == kMIDIObjectType_Destination)
      update(obj, outputs, configuration.output_added, configuration.output_removed);
  }

  ~observer_core() { MIDIClientDispose(this->client); }

private:
  // MIDIClientCreate delivers notifications on the creating thread's run loop.
  // Public listings query CoreMIDI independently and never touch these maps.
  template <typename Port>
  using endpoints = std::map<MIDIEndpointRef, std::shared_ptr<const Port>>;
  endpoints<input_port> inputs;
  endpoints<output_port> outputs;

  template <typename Port>
  using initial_ports = std::vector<std::pair<MIDIEndpointRef, std::shared_ptr<const Port>>>;

  template <typename Port>
  static void for_each_endpoint(auto&& visit)
  {
    constexpr bool input = std::is_same_v<Port, input_port>;
    const auto count = input ? MIDIGetNumberOfSources() : MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < count; ++i)
    {
      if (auto endpoint = input ? MIDIGetSource(i) : MIDIGetDestination(i))
        visit(endpoint);
    }
  }

  template <typename Port>
  std::vector<Port> list() const noexcept
  {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);
    std::vector<Port> ports;
    for_each_endpoint<Port>([&](MIDIEndpointRef endpoint) {
      if (auto p = to_port_info<Port>(endpoint))
        ports.push_back(std::move(*p));
    });
    return ports;
  }

  template <typename Port>
  auto remember(endpoints<Port>& known, MIDIEndpointRef endpoint) -> std::shared_ptr<const Port>
  {
    if (!known.contains(endpoint))
      if (auto p = to_port_info<Port>(endpoint))
      {
        auto snapshot = std::make_shared<const Port>(std::move(*p));
        known.emplace(endpoint, snapshot);
        return snapshot;
      }
    return {};
  }

  template <typename Port>
  initial_ports<Port> seed(endpoints<Port>& known, const auto& added, const auto& removed)
  {
    initial_ports<Port> initial;
    const bool announce_existing = configuration.notify_in_constructor && added;
    if (removed || announce_existing)
      for_each_endpoint<Port>([&](MIDIEndpointRef endpoint) {
        if (auto p = remember(known, endpoint); p && announce_existing)
          initial.emplace_back(endpoint, std::move(p));
      });
    return initial;
  }

  static void announce(const auto& initial, const auto& known, const auto& added)
  {
    for (const auto& [endpoint, port] : initial)
    {
      // A previous callback may have delivered a removal, even reused the ref.
      auto it = known.find(endpoint);
      if (it != known.end() && it->second == port)
        added(*port);
    }
  }

  template <typename Port>
  void update(
      const MIDIObjectAddRemoveNotification& obj, endpoints<Port>& known,
      const auto& added, const auto& removed)
  {
    if (obj.messageID == kMIDIMsgObjectAdded)
    {
      if (added || removed)
        if (auto p = remember(known, obj.child); p && added)
          added(*p);
    }
    else
    {
      // The ref is already disposed. Extract before calling out so reentrant
      // notifications can't invalidate the snapshot or report a second removal.
      if (auto node = known.extract(obj.child); node && removed)
        removed(*node.mapped());
    }
  }

  MIDIClientRef client{};
};
}

NAMESPACE_LIBREMIDI::coremidi
{
struct observer_impl : observer_core
{
  observer_impl(observer_configuration&& conf, coremidi_observer_configuration&& apiconf)
      : observer_core{std::move(conf), std::move(apiconf)}
  {
    finish_init();
  }

  libremidi::API get_current_api() const noexcept override { return libremidi::API::COREMIDI; }
};
}
