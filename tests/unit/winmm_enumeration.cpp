#include <libremidi/config.hpp>
#include <libremidi/backends/winmm/error_domain.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cwchar>
#include <typeinfo>
#include <vector>

namespace
{
struct device
{
  const wchar_t* name;
  WORD product{1};
};
struct direction
{
  std::vector<device> devices;
  std::atomic_uint counts{}, caps{};
  int opened{-1};

  UINT count()
  {
    ++counts;
    return static_cast<UINT>(devices.size());
  }
  MMRESULT read(UINT_PTR id, auto* out, UINT size)
  {
    ++caps;
    *out = {};
    if (size != sizeof(*out) || id >= devices.size() || !devices[id].name)
      return MMSYSERR_BADDEVICEID;
    std::wcsncpy(out->szPname, devices[id].name, MAXPNAMELEN - 1);
    out->wMid = 42;
    out->wPid = devices[id].product;
    return MMSYSERR_NOERROR;
  }
  MMRESULT open(auto* handle, UINT id)
  {
    opened = static_cast<int>(id);
    *handle = nullptr;
    return MMSYSERR_ALLOCATED;
  }
};
direction inputs, outputs;
int failures{};
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #expr); ++failures; } } while (false)
}

// Intercept only the native boundary. Exercise the actual observer factory, scans and
// open_port implementations, never opening or sending anything to a user's devices.
#define midiInGetNumDevs() inputs.count()
#define midiOutGetNumDevs() outputs.count()
#define midiInGetDevCapsW(i, caps, size) inputs.read(i, caps, size)
#define midiOutGetDevCapsW(i, caps, size) outputs.read(i, caps, size)
#define midiInOpen(h, i, ...) inputs.open(h, i)
#define midiOutOpen(h, i, ...) outputs.open(h, i)
#include <libremidi/backends/winmm.hpp>
#undef midiInGetNumDevs
#undef midiOutGetNumDevs
#undef midiInGetDevCapsW
#undef midiOutGetDevCapsW
#undef midiInOpen
#undef midiOutOpen

namespace
{
using namespace libremidi;
using namespace std::chrono_literals;

void passive_and_listings()
{
  inputs.devices = {{L"Keys"}, {L"M\u00e9tronome"}, {L"Keys"}, {L"Keys 2"}};
  outputs.devices = {{L"Out"}, {L"Out"}};
  bool registered{};
  {
    // Diagnostic callbacks alone are not subscriptions to port changes.
    auto passive = make<observer_winmm>(
        observer_configuration{.on_error = [](auto&&...) {}, .on_warning = [](auto&&...) {}},
        winmm_observer_configuration{.poll_period = 2ms});
    const observer_api* impl = passive.get();
    CHECK(typeid(*impl) == typeid(observer_winmm));
    std::this_thread::sleep_for(20ms);
  }
  auto manual = make<observer_winmm>(
      observer_configuration{},
      winmm_observer_configuration{.manual_poll = [&](auto&) { registered = true; }});
  CHECK(!registered);
  std::printf("idle count/caps: IN %u/%u, OUT %u/%u\n", inputs.counts.load(), inputs.caps.load(),
              outputs.counts.load(), outputs.caps.load());
  CHECK(inputs.counts == 0 && outputs.counts == 0 && inputs.caps == 0 && outputs.caps == 0);

  inputs.counts = inputs.caps = outputs.counts = outputs.caps = 0;
  const auto in = manual->get_input_ports();
  std::printf("4-input listing: %u count, %u caps\n", inputs.counts.load(), inputs.caps.load());
  CHECK(inputs.counts == 1 && inputs.caps == 4 && outputs.counts == 0 && outputs.caps == 0);
  CHECK(in.size() == 4);
  if (in.size() == 4)
  {
    CHECK(in[0].display_name == "Keys 1" && in[1].display_name == "M\xc3\xa9tronome 1");
    CHECK(in[2].display_name == "Keys 2" && in[3].display_name == "Keys 2 1");
    CHECK(in[2].port == 2 && in[2].port_name == "Keys");
    CHECK(get<usb_device_identifier>(in[2].device).vendor_id == 42);
  }
  const auto out = manual->get_output_ports();
  CHECK(out.size() == 2 && out.back().display_name == "Out 2");
  CHECK(outputs.counts == 1 && outputs.caps == 2);
  outputs.devices = {{L"New"}};
  const auto fresh = manual->get_output_ports();
  CHECK(fresh.size() == 1 && fresh.front().display_name == "New 1");
  inputs.devices[1].name = nullptr;
  const auto partial = manual->get_input_ports();
  CHECK(partial.size() == 1 && partial.front().display_name == "Keys 1");
}

void directional_notifications()
{
  std::function<void()> poll;
  winmm_observer_configuration manual{.manual_poll = [&](const auto& p) { poll = p.callback; }};
  inputs.counts = outputs.counts = 0;
  inputs.devices = {{L"Existing"}, {L"Other"}};
  std::vector<std::string> added, removed;
  {
    auto input = make<observer_winmm>(
        {.input_added = [&](const auto& p) { added.push_back(p.display_name); },
         .input_removed = [&](const auto& p) { removed.push_back(p.display_name); }},
        winmm_observer_configuration{manual});
    CHECK(inputs.counts == 1 && outputs.counts == 0);
    CHECK(added.size() == 2 && added[0] == "Existing 1");
    added.clear();
    inputs.devices[0].name = L"Replacement";
    inputs.devices[1].name = nullptr;
    CHECK(poll);
    if (!poll) return;
    poll();
    CHECK(added.empty() && removed.empty());
    inputs.devices[1].name = L"Other";
    poll();
    CHECK(added.size() == 1 && added[0] == "Replacement 1");
    CHECK(removed.size() == 1 && removed[0] == "Existing 1");
    CHECK(inputs.counts == 3 && outputs.counts == 0);
  }
  poll = {};
  inputs.counts = outputs.counts = 0;
  outputs.devices = {{L"Initial"}};
  added.clear();
  removed.clear();
  auto output = make<observer_winmm>(
      {.output_added = [&](const auto& p) { added.push_back(p.display_name); },
       .output_removed = [&](const auto& p) { removed.push_back(p.display_name); },
       .notify_in_constructor = false}, std::move(manual));
  CHECK(added.empty() && removed.empty());
  outputs.devices.clear();
  CHECK(poll);
  if (!poll) return;
  poll();
  CHECK(removed.size() == 1 && removed[0] == "Initial 1");
  CHECK(inputs.counts == 0 && outputs.counts == 2);
}

template <typename Port>
void openings(direction& d, auto& backend)
{
  auto open = [&](const Port& p) {
    d.opened = -1;
    const auto result = backend.open_port(p, {});
    if (d.opened == -1) CHECK(result == std::errc::invalid_argument);
    else CHECK(result == from_mmerr(MMSYSERR_ALLOCATED));
    return d.opened;
  };
  d.devices = {{L"Keys"}, {L"Keys", 7}, {L"Pad"}, {L"Keys 2"}};
  Port second{{.device = usb_device_identifier{42, 7}, .port = 1,
               .port_name = "Keys", .display_name = "Keys 2"}};
  d.caps = 0;
  CHECK(open(second) == 1);
  CHECK(d.caps == 2); // No list, no second read and no query beyond the match.
  CHECK(open(Port{{.port_name = "Keys"}}) == -1);
  CHECK(open(Port{{.device = usb_device_identifier{42, 7}, .port_name = "Keys"}}) == 1);
  std::swap(d.devices[1], d.devices[2]);
  CHECK(open(second) == 2); // Cached index now points to Pad.
  std::swap(d.devices[0], d.devices[2]);
  CHECK(open(second) == -1); // Same raw name and ordinal, wrong product.
  d.devices.erase(d.devices.begin() + 2);
  CHECK(open(second) == -1); // Missing second occurrence, don't open the first.
  CHECK(open(Port{{.port = 0, .port_name = "Pad"}}) == 1); // Legacy raw-only selector.
  CHECK(open(Port{{.display_name = "Keys 2 1"}}) == 2); // Name itself ends in a number.
  CHECK(open(Port{{.port_name = "Pad", .display_name = "Keys 1"}}) == -1);
  CHECK(open(Port{{.display_name = "Pad 01"}}) == -1);
  CHECK(open(Port{{.port = 0}}) == -1);
  d.devices[0].name = nullptr;
  CHECK(open(Port{{.display_name = "Pad 1"}}) == -1); // Unknown ordinal beyond a failed read.
}
}

int main()
{
  passive_and_listings();
  directional_notifications();
  libremidi::midi_in_winmm in{{.on_error = [](auto&&...) {}}, {}};
  libremidi::midi_out_winmm out{{.on_error = [](auto&&...) {}}, {}};
  openings<libremidi::input_port>(inputs, in);
  openings<libremidi::output_port>(outputs, out);
  std::printf("%d failed checks\n", failures);
  return failures != 0;
}
