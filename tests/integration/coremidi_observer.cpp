#include "../include_catch.hpp"

#include <libremidi/configurations.hpp>
#include <libremidi/libremidi.hpp>

#include <CoreMIDI/CoreMIDI.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <optional>

namespace
{
struct midi_client
{
  MIDIClientRef ref{};
  ~midi_client()
  {
    if (ref)
      MIDIClientDispose(ref);
  }
};

bool run_until(auto ready)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!ready() && std::chrono::steady_clock::now() < deadline)
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false);
  return ready();
}
}

TEST_CASE("CoreMIDI removals identify disposed endpoints", "[observer][coremidi]")
{
  using namespace libremidi;
  const auto api = GENERATE(API::COREMIDI, API::COREMIDI_UMP);
  if (api == API::COREMIDI_UMP)
  {
    const auto apis = available_ump_apis();
    if (std::find(apis.begin(), apis.end(), api) == apis.end())
      SKIP("CoreMIDI UMP is not available");
  }

  midi_client owner;
  if (MIDIClientCreate(CFSTR("libremidi observer test"), nullptr, nullptr, &owner.ref) != noErr)
    SKIP("CoreMIDI is not available");

  MIDIEndpointRef source{};
  REQUIRE(MIDISourceCreate(owner.ref, CFSTR("libremidi test source"), &source) == noErr);
  SInt32 source_id{};
  REQUIRE(MIDIObjectGetIntegerProperty(source, kMIDIPropertyUniqueID, &source_id) == noErr);

  std::optional<input_port> removed_source;
  std::optional<output_port> added_dest, removed_dest;
  SInt32 dest_id{};
  observer_configuration conf;
  conf.track_virtual = true;
  conf.notify_in_constructor = false;
  // The existing source must be remembered without an add callback or a listing.
  conf.input_removed = [&](const input_port& p) {
    if (p.port == std::bit_cast<uint32_t>(source_id))
      removed_source = p;
  };
  conf.output_added = [&](const output_port& p) {
    if (p.port == std::bit_cast<uint32_t>(dest_id))
      added_dest = p;
  };
  conf.output_removed = [&](const output_port& p) {
    if (added_dest && p.port == added_dest->port)
      removed_dest = p;
  };
  observer obs{conf, api};

  MIDIEndpointRef dest{};
  REQUIRE(MIDIDestinationCreate(owner.ref, CFSTR("libremidi test destination"),
                                +[](const MIDIPacketList*, void*, void*) {}, nullptr, &dest) == noErr);
  REQUIRE(MIDIObjectGetIntegerProperty(dest, kMIDIPropertyUniqueID, &dest_id) == noErr);
  REQUIRE(run_until([&] { return added_dest.has_value(); }));

  // A listing must still read the live properties; changing the name does
  // not change the snapshot used to pair this removal with its original add.
  REQUIRE(MIDIObjectSetStringProperty(dest, kMIDIPropertyName, CFSTR("renamed test destination"))
          == noErr);
  REQUIRE(run_until([&] {
    const auto ports = obs.get_output_ports();
    return std::any_of(ports.begin(), ports.end(), [&](const auto& p) {
      return p.port == added_dest->port && p.port_name == "renamed test destination";
    });
  }));

  REQUIRE(MIDIEndpointDispose(source) == noErr);
  REQUIRE(MIDIEndpointDispose(dest) == noErr);
  REQUIRE(run_until([&] { return removed_source && removed_dest; }));
  CHECK(removed_source->api == api);
  CHECK(removed_source->port_name == "libremidi test source");
  CHECK(removed_source->type == transport_type::software);
  CHECK(removed_dest->api == api);
  CHECK(removed_dest->client == added_dest->client);
  CHECK(removed_dest->port_name == added_dest->port_name);
  CHECK(removed_dest->display_name == added_dest->display_name);
  CHECK(removed_dest->type == added_dest->type);
}
