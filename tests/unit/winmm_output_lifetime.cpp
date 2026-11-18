#include <libremidi/config.hpp>
#include <libremidi/backends/winmm/error_domain.hpp>

#include <cstdio>

namespace
{
unsigned closes{};
bool wrong_handle{};
int device{};
HMIDIOUT handle() { return reinterpret_cast<HMIDIOUT>(&device); }
MMRESULT open_output(HMIDIOUT* out, UINT id, DWORD_PTR, DWORD_PTR, DWORD)
{
  // The output parameter establishes ownership only on success.
  *out = handle();
  return id == 0 ? MMSYSERR_NOERROR : MMSYSERR_BADDEVICEID;
}
MMRESULT close_output(HMIDIOUT h)
{
  ++closes;
  wrong_handle |= h != handle();
  return MMSYSERR_NOERROR;
}
}

// Compile the backend in its own test executable, without opening a user's device.
#define midiOutOpen open_output
#define midiOutClose close_output
#define midiOutGetNumDevs() 0u
#include <libremidi/backends/winmm/midi_out.hpp>
#undef midiOutOpen
#undef midiOutClose
#undef midiOutGetNumDevs

int main()
{
  using libremidi::midi_out_winmm;
  {
    midi_out_winmm unopened{{}, {}};
  }
  {
    midi_out_winmm missing{{}, {}};
    if (missing.open_port({}, {}) != std::errc::invalid_argument)
      return 1;
  }
  {
    midi_out_winmm failed{{}, {}};
    if (failed.do_open(1) == stdx::error{})
      return 1;
  }
  if (closes != 0)
  {
    std::fprintf(stderr, "closed %u unowned handles\n", closes);
    return 1;
  }
  {
    midi_out_winmm opened{{}, {}};
    if (opened.do_open(0) != stdx::error{})
      return 1;
    opened.close_port();
    opened.close_port();
  }
  return closes == 1 && !wrong_handle ? 0 : 1;
}
