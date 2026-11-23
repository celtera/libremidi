#include <libremidi/config.hpp>
#include <libremidi/backends/winmm/error_domain.hpp>

#include <cstdio>
#include <initializer_list>
#include <vector>

namespace
{
unsigned opens{}, closes{}, resets{};
bool wrong_handle{};
int device{};
// Results of the next midiOutClose calls; success once exhausted.
std::vector<MMRESULT> close_results;

HMIDIOUT handle() { return reinterpret_cast<HMIDIOUT>(&device); }
MMRESULT open_output(HMIDIOUT* out, UINT id, DWORD_PTR, DWORD_PTR, DWORD)
{
  // The output parameter establishes ownership only on success.
  *out = handle();
  if (id != 0)
    return MMSYSERR_BADDEVICEID;
  ++opens;
  return MMSYSERR_NOERROR;
}
MMRESULT close_output(HMIDIOUT h)
{
  wrong_handle |= h != handle();
  const auto n = closes++;
  return n < close_results.size() ? close_results[n] : MMSYSERR_NOERROR;
}
MMRESULT reset_output(HMIDIOUT h)
{
  wrong_handle |= h != handle();
  ++resets;
  return MMSYSERR_NOERROR;
}

void script(std::initializer_list<MMRESULT> results)
{
  opens = closes = resets = 0;
  close_results = results;
}

int failures{};
void check(bool ok, const char* what)
{
  if (!ok)
  {
    std::fprintf(stderr, "FAILED: %s (opens %u, closes %u, resets %u)\n", what, opens, closes, resets);
    ++failures;
  }
}
}

// Compile the backend in its own test executable, without opening a user's device.
#define midiOutOpen open_output
#define midiOutClose close_output
#define midiOutReset reset_output
#define midiOutGetNumDevs() 0u
#include <libremidi/backends/winmm/midi_out.hpp>
#undef midiOutOpen
#undef midiOutClose
#undef midiOutReset
#undef midiOutGetNumDevs

int main()
{
  using libremidi::from_mmerr;
  using libremidi::midi_out_winmm;
  const stdx::error ok{};

  script({});
  {
    midi_out_winmm unopened{{}, {}};
  }
  {
    midi_out_winmm missing{{}, {}};
    check(missing.open_port({}, {}) == std::errc::invalid_argument, "missing port is reported");
  }
  {
    midi_out_winmm failed{{}, {}};
    check(failed.do_open(1) != ok, "native open failure is reported");
  }
  check(closes == 0 && resets == 0, "no handle is closed before a successful open");

  script({});
  {
    midi_out_winmm opened{{}, {}};
    check(opened.do_open(0) == ok, "open");
    check(opened.do_open(0) == std::errc::device_or_resource_busy, "open while open is refused");
    check(opened.close_port() == ok, "close");
    check(opened.close_port() == ok, "second close is a no-op");
  }
  check(opens == 1 && closes == 1 && resets == 0, "one open, one close, no reset");

  // Pending buffers: reset once, then the close succeeds.
  script({MIDIERR_STILLPLAYING});
  {
    midi_out_winmm busy{{}, {}};
    check(busy.do_open(0) == ok, "open");
    check(busy.close_port() == ok, "close succeeds after a reset");
    check(closes == 2 && resets == 1, "busy close: reset once, close twice");
    check(busy.do_open(0) == ok, "reopen after a busy close");
  }
  check(opens == 2 && closes == 3 && resets == 1, "busy close then reopen and destroy");

  // The reset does not help: the error is reported, the handle is not kept.
  script({MIDIERR_STILLPLAYING, MIDIERR_STILLPLAYING});
  {
    midi_out_winmm stuck{{}, {}};
    check(stuck.do_open(0) == ok, "open");
    check(stuck.close_port() == from_mmerr(MIDIERR_STILLPLAYING), "still playing is reported");
    check(closes == 2 && resets == 1, "one retry only");
    check(stuck.close_port() == ok, "nothing left to close");
    check(stuck.do_open(0) == ok, "reopen after a failed close");
  }
  check(opens == 2 && closes == 3 && resets == 1, "failed busy close never closes again");

  // Any other close error: no reset, no retry, the object stays usable.
  script({MMSYSERR_NOMEM});
  {
    midi_out_winmm broken{{}, {}};
    check(broken.do_open(0) == ok, "open");
    check(broken.close_port() == from_mmerr(MMSYSERR_NOMEM), "close error is reported");
    check(closes == 1 && resets == 0, "no reset for other errors");
    check(broken.do_open(0) == ok, "reopen after a close error");
    check(broken.close_port() == ok, "close after reopen");
  }
  check(opens == 2 && closes == 2 && resets == 0, "close error: no double close");

  script({MMSYSERR_INVALHANDLE});
  {
    midi_out_winmm invalid{{}, {}};
    check(invalid.do_open(0) == ok, "open");
    check(invalid.close_port() == from_mmerr(MMSYSERR_INVALHANDLE), "invalid handle is reported");
  }
  check(closes == 1 && resets == 0, "invalid handle is not closed again");

  // Destroyed while the driver still holds buffers.
  script({MIDIERR_STILLPLAYING});
  {
    midi_out_winmm busy{{}, {}};
    check(busy.do_open(0) == ok, "open");
  }
  check(closes == 2 && resets == 1, "destructor resets a busy handle once");

  script({MIDIERR_STILLPLAYING, MIDIERR_STILLPLAYING});
  {
    midi_out_winmm stuck{{}, {}};
    check(stuck.do_open(0) == ok, "open");
  }
  check(closes == 2 && resets == 1, "destructor gives up after one retry");

  check(!wrong_handle, "only the owned handle is closed or reset");
  if (failures == 0)
    std::puts("WinMM output handle: closed once per successful open, never retained");
  return failures == 0 ? 0 : 1;
}
