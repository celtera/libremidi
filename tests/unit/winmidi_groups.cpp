#include <libremidi/backends/winmidi/group_ports.hpp>
#include <libremidi/detail/conversion.hpp>
#include <libremidi/detail/ump_stream.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string_view>

#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #expr); std::exit(1); } } while (false)

namespace
{
using namespace libremidi;
using namespace libremidi::winmidi;

enum class direction { Undefined, BlockInput, BlockOutput, Bidirectional };
struct group_index { unsigned value; unsigned Index() const { return value; } };
struct block
{
  unsigned number, first, count;
  direction dir;
  bool active;
  std::string name;
  unsigned Number() const { return number; }
  group_index FirstGroup() const { return {first}; }
  unsigned GroupCount() const { return count; }
  direction Direction() const { return dir; }
  bool IsActive() const { return active; }
};
struct blocks : std::vector<block>
{
  using vector::vector;
  auto Size() const { return size(); }
};
struct description
{
  blocks functions, terminals;
  // What the endpoint announced, when it is more than what the service has read.
  std::size_t declared_functions = 0;
  bool discovered = true;
};
struct declared_info
{
  std::size_t count;
  std::size_t DeclaredFunctionBlockCount() const { return count; }
};
struct endpoint
{
  const description* value;
  explicit operator bool() const { return value != nullptr; }
  const blocks& GetDeclaredFunctionBlocks() const { return value->functions; }
  const blocks& GetGroupTerminalBlocks() const { return value->terminals; }
  bool IsEndpointDiscoveryComplete() const { return value->discovered; }
  declared_info GetDeclaredEndpointInfo() const
  {
    return {std::max(value->declared_functions, value->functions.size())};
  }
};

const std::string id = R"(\\?\swd#midisrv#midiu_test#{endpoint-a})";
const port_information base{.api = API::WINDOWS_MIDI_SERVICES, .device = id, .device_name = "Synth"};
group_ports project(const description& data, bool inputs = true, bool outputs = true)
{
  return make_group_ports(endpoint{&data}, base, inputs, outputs, [](const block& b) { return b.name; });
}

void projection_and_open()
{
  // #254: Iridium FB 0 and GTB 1 describe the same bidirectional group.
  description data{{{0, 0, 1, direction::Bidirectional, true, "Synth"}},
                   {{1, 0, 1, direction::Bidirectional, true, "USB"}}};
  auto ports = project(data);
  CHECK(ports.inputs.size() == 1 && ports.outputs.size() == 1);
  auto lookup = [&](const std::string& key) { return endpoint{key == id ? &data : nullptr}; };
  CHECK(resolve_group(ports.inputs.front(), port_direction::input, lookup));
  CHECK(resolve_group(ports.outputs.front(), port_direction::output, lookup));
  CHECK(ports.inputs[0].port == 0 && ports.inputs[0].display_name == "Synth (Group 1, Synth)");
  CHECK(get<std::string>(ports.outputs[0].device) == id);

  // #200: non-USB endpoints need no GTBs to list and open in both directions.
  data.terminals.clear();
  data.functions = {{0, 3, 2, direction::BlockOutput, true, "A"},
                    {1, 4, 1, direction::Bidirectional, true, "B"},
                    {2, 15, 255, direction::BlockInput, true, "C"},
                    {3, 16, 1, direction::Bidirectional, true, "outside"},
                    {4, 0, 0, direction::BlockOutput, true, "empty"},
                    {5, 0, 1, direction::Undefined, true, "unknown"}};
  ports = project(data);
  CHECK(ports.inputs.size() == 2 && ports.outputs.size() == 2);
  CHECK(ports.inputs[0].port == 3 && ports.inputs[1].port == 4);
  CHECK(ports.outputs[0].port == 4 && ports.outputs[1].port == 15);
  CHECK(ports.inputs[1].port_name == "A" && ports.outputs[0].port_name == "B");
  CHECK(project(data, false, true).inputs.empty());
  for (const auto& p : ports.inputs) CHECK(resolve_group(p, port_direction::input, lookup));
  for (const auto& p : ports.outputs) CHECK(resolve_group(p, port_direction::output, lookup));
  CHECK(!resolve_group(ports.inputs.front(), port_direction::output, lookup));
  auto stale = ports.outputs[0];
  stale.client = 0;
  CHECK(!resolve_group(stale, port_direction::output, lookup));
  stale = ports.outputs[0];
  stale.device = "Synth";
  CHECK(!resolve_group(stale, port_direction::output, lookup));
  stale = ports.outputs[0];
  stale.port += (uint64_t{1} << 32);
  CHECK(!resolve_group(stale, port_direction::output, lookup));

  data.terminals = {{8, 5, 2, direction::BlockInput, true, "USB"}};
  for (auto& fb : data.functions) fb.active = false;
  CHECK(project(data).empty());
  CHECK(!resolve_group(ports.outputs[0], port_direction::output, lookup));
  data.functions.clear();
  ports = project(data);
  CHECK(ports.inputs.empty() && ports.outputs.size() == 2);
  CHECK(ports.outputs[0].port == 5 && ports.outputs[1].port == 6);
  CHECK(resolve_group(ports.outputs[1], port_direction::output, lookup));
}

void incomplete_metadata()
{
  auto lookup = [](const description& data) {
    return [&data](const std::string& key) { return endpoint{key == id ? &data : nullptr}; };
  };

  // One function block out of three arrived before the service stopped waiting:
  // the groups of the other two are still those of the group terminal blocks.
  description data{{{0, 0, 1, direction::Bidirectional, true, "Keys"}},
                   {{1, 0, 3, direction::Bidirectional, true, "USB"}}};
  data.declared_functions = 3;
  auto ports = project(data);
  CHECK(ports.inputs.size() == 3 && ports.outputs.size() == 3);
  CHECK(ports.inputs[2].port == 2 && ports.inputs[0].port_name == "USB");
  CHECK(resolve_group(ports.outputs[2], port_direction::output, lookup(data)));

  // All of them, but discovery is still running.
  data.functions = {{0, 0, 1, direction::Bidirectional, true, "Keys"},
                    {1, 1, 1, direction::BlockOutput, true, "Pads"},
                    {2, 2, 1, direction::BlockInput, true, "Lights"}};
  data.discovered = false;
  CHECK(project(data).outputs.size() == 3);
  data.discovered = true;
  ports = project(data);
  CHECK(ports.inputs.size() == 2 && ports.outputs.size() == 2);
  CHECK(ports.inputs[1].port_name == "Pads" && ports.outputs[1].port == 2);
  CHECK(!resolve_group(ports.inputs[1], port_direction::output, lookup(data)));

  // Without group terminal blocks, the function blocks read so far are all there is.
  data.terminals.clear();
  data.functions.resize(1);
  data.declared_functions = 3;
  ports = project(data);
  CHECK(ports.inputs.size() == 1 && ports.outputs.size() == 1 && ports.inputs[0].port_name == "Keys");

  // No block of any kind: group 1, in both directions.
  const description bare;
  ports = project(bare);
  CHECK(ports.inputs.size() == 1 && ports.outputs.size() == 1);
  CHECK(ports.inputs[0].port == 0 && ports.outputs[0].port == 0);
  CHECK(ports.inputs[0].port_name.empty() && ports.inputs[0].display_name == "Synth (Group 1)");
  CHECK(resolve_group(ports.inputs[0], port_direction::input, lookup(bare)));
  CHECK(resolve_group(ports.outputs[0], port_direction::output, lookup(bare)));
  auto other = ports.outputs[0];
  other.port = 1;
  CHECK(!resolve_group(other, port_direction::output, lookup(bare)));
  CHECK(project(bare, true, false).outputs.empty());
}

void watcher_diffs()
{
  description data{{}, {{1, 3, 2, direction::Bidirectional, true, "Before discovery"}}};
  group_ports cached;
  std::vector<std::string> events;
  auto record = [&](std::string action, const auto& p) {
    events.push_back(action + std::to_string(p.port) + ":" + p.port_name);
  };
  libremidi::observer_configuration conf;
  conf.input_added = [&](const auto& p) { record("in+", p); };
  conf.output_removed = [&](const auto& p) { record("out-", p); }; // no output-added handler
  auto update = [&](bool notify = true) { replace_groups(cached, project(data), conf, notify).notify(conf); };
  update(false); // notify_in_constructor = false
  update(); // watcher's asynchronous initial Added is not a second arrival
  CHECK(events.empty());
  data.functions = {{0, 3, 1, direction::Bidirectional, true, "Discovered"},
                    {1, 5, 1, direction::BlockOutput, true, "Keys"}};
  update();
  // The output of group 4 is gone, and the one of group 3 has a new name.
  CHECK((events == std::vector<std::string>{"out-3:Before discovery", "out-4:Before discovery",
                                            "in+3:Discovered", "in+5:Keys"}));
  // Only a name changed: the port leaves under the old one and comes back
  // under the new one, and the port of the other block is left alone.
  data.functions[0].name = "Renamed";
  update();
  CHECK(events.size() == 6 && events[4] == "out-3:Discovered" && events[5] == "in+3:Renamed");
  CHECK(cached.outputs.front().port_name == "Renamed");
  update(); // nothing changed
  CHECK(events.size() == 6);
  replace_groups(cached, {}, conf).notify(conf);
  CHECK(events.size() == 7 && events.back() == "out-3:Renamed");
  update(); // notify_in_constructor = true on an empty cache
  CHECK(events.size() == 9 && events[7] == "in+3:Renamed" && events[8] == "in+5:Keys");

  // An endpoint that announces its blocks after it appeared.
  cached = {};
  events.clear();
  data = {};
  update();
  CHECK((events == std::vector<std::string>{"in+0:"}));
  data.functions = {{0, 0, 1, direction::Bidirectional, true, "Late"}};
  update();
  CHECK(events.size() == 3 && events[1] == "out-0:" && events[2] == "in+0:Late");
}

void transports()
{
  using enum transport_type;
  CHECK(transport_from_code("KS") == hardware && transport_from_code("KSA") == hardware);
  CHECK(transport_from_code("BLEMIDI") == transport_type(hardware | bluetooth));
  CHECK(transport_from_code("APP") == software && transport_from_code("GMSYNTH") == software);
  for (auto code : {"LOOP", "BLOOP", "DIAG"})
    CHECK(transport_from_code(code) == transport_type(software | loopback));
  CHECK(transport_from_code("NET2UDP") == network && transport_from_code("RTPMIDI") == network);
  CHECK(transport_from_code("") == unknown && transport_from_code("FUTURE") == unknown);

  libremidi::observer_configuration conf;
  CHECK(accepts_endpoint(conf, transport_from_code("KSA")));
  CHECK(accepts_endpoint(conf, transport_from_code("RTPMIDI")));
  CHECK(accepts_endpoint(conf, transport_from_code("FUTURE"))); // counted as hardware
  for (auto code : {"APP", "GMSYNTH", "BLOOP"})
    CHECK(!accepts_endpoint(conf, transport_from_code(code)));
  conf.track_virtual = true;
  conf.track_hardware = conf.track_network = false;
  for (auto code : {"APP", "GMSYNTH", "LOOP", "BLOOP"})
    CHECK(accepts_endpoint(conf, transport_from_code(code)));
  CHECK(!accepts_endpoint(conf, transport_from_code("NET2UDP")));
  CHECK(!accepts_endpoint(conf, transport_from_code("RTPMIDI")));
  CHECK(!accepts_endpoint(conf, transport_from_code("BLEMIDI")));
  conf.track_any = true;
  CHECK(accepts_endpoint(conf, transport_from_code("BLEMIDI")));
}

void group_io()
{
  // #284: complete contiguous group runs, including a payload that resembles
  // another group's header. Utility/Stream and the truncated tail are skipped.
  const uint32_t batch[]{0x41903c00, 0x30160001, 0x30160102, 0x03040506,
                         0x30260708, 0x090a0b0c, 0x00000000, 0xf0000000, 0, 0, 0,
                         0x30310d00, 0, 0x50000000, 0};
  std::span<const uint32_t> remaining{batch};
  const auto begin = next_ump_group_run(remaining, 0);
  CHECK(begin.data() == batch + 2 && begin.size() == 4);
  const auto end = next_ump_group_run(remaining, 0);
  CHECK(end.data() == batch + 11 && end.size() == 2);
  CHECK(next_ump_group_run(remaining, 0).empty());
  remaining = batch;
  CHECK(next_ump_group_run(remaining, -1).size() == 13);
  CHECK(!ump_matches_group(0x01000000, 1) && !ump_matches_group(0x61000000, 1));
  CHECK(ump_matches_group(0xdf000000, 15));

  midi1_to_midi2 converter;
  converter.context.group = 11;
  const unsigned char bytes[]{0xf0, 1, 2, 3, 4, 5, 6, 7, 0xf7};
  unsigned packets{};
  auto result = converter.convert_packets(bytes, sizeof(bytes), 0, [&](const auto& packet) {
    CHECK(ump_matches_group(packet.data[0], 11));
    ++packets;
  });
  CHECK(!result.is_set() && packets == 2);
}
}

int main()
{
  projection_and_open();
  incomplete_metadata();
  watcher_diffs();
  transports();
  group_io();
  std::puts("WinMIDI projection/open, watcher diffs, and group I/O: passed");
}
