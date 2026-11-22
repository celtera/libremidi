#include <libremidi/backends/winmidi/group_ports.hpp>
#include <libremidi/detail/conversion.hpp>
#include <libremidi/detail/ump_stream.hpp>

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
struct description { blocks functions, terminals; };
struct endpoint
{
  const description* value;
  explicit operator bool() const { return value != nullptr; }
  const blocks& GetDeclaredFunctionBlocks() const { return value->functions; }
  const blocks& GetGroupTerminalBlocks() const { return value->terminals; }
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
  CHECK((events == std::vector<std::string>{"out-4:Before discovery", "in+5:Keys"}));
  data.functions[0].name = "Renamed";
  update();
  CHECK(events.size() == 2 && cached.outputs.front().port_name == "Renamed");
  replace_groups(cached, {}, conf).notify(conf);
  CHECK(events.size() == 3 && events.back() == "out-3:Renamed");
  update(); // notify_in_constructor = true on an empty cache
  CHECK(events.size() == 5 && events[3] == "in+3:Renamed" && events[4] == "in+5:Keys");

  CHECK(!accepts_endpoint(conf, transport_from_code("APP")));
  conf.track_virtual = true;
  conf.track_hardware = conf.track_network = false;
  CHECK(accepts_endpoint(conf, transport_from_code("APP")));
  CHECK(!accepts_endpoint(conf, transport_from_code("NET2UDP")));
  CHECK(!accepts_endpoint(conf, transport_from_code("BLE20")));
  conf.track_any = true;
  CHECK(accepts_endpoint(conf, transport_from_code("BLE10")));
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
  watcher_diffs();
  group_io();
  std::puts("WinMIDI projection/open, watcher diffs, and group I/O: passed");
}
