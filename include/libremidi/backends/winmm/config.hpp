#pragma once
#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN 1
#include <libremidi/detail/midi_api.hpp>
#include <libremidi/libremidi.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

// clang-format off
#include <windows.h>
#include <mmsystem.h>
// clang-format on

namespace libremidi
{

#define RT_WINMM_OBSERVER_POLL_PERIOD_MS 100

#define RT_SYSEX_BUFFER_SIZE 1024
#define RT_SYSEX_BUFFER_COUNT 4

// A structure to hold variables related to the WinMM API
// implementation.
struct winmm_in_data
{
  HMIDIIN inHandle; // Handle to Midi Input Device

  DWORD lastTime;
  LPMIDIHDR sysexBuffer[RT_SYSEX_BUFFER_COUNT];
  CRITICAL_SECTION
  _mutex; // [Patrice] see
          // https://groups.google.com/forum/#!topic/mididev/6OUjHutMpEo
};

struct winmm_out_data
{
  HMIDIOUT outHandle; // Handle to Midi Output Device
};

// The Windows MM API is based on the use of a callback function for
// MIDI input.  We convert the system specific time stamps to delta
// time values.

// Convert a nullptr-terminated wide string or ANSI-encoded string to UTF-8.
inline std::string ConvertToUTF8(const TCHAR* str)
{
  std::string u8str;
  const WCHAR* wstr = L"";
#if defined(UNICODE) || defined(_UNICODE)
  wstr = str;
#else
  // Convert from ANSI encoding to wide string
  int wlength = MultiByteToWideChar(CP_ACP, 0, str, -1, nullptr, 0);
  std::wstring wstrtemp;
  if (wlength)
  {
    wstrtemp.assign(wlength - 1, 0);
    MultiByteToWideChar(CP_ACP, 0, str, -1, &wstrtemp[0], wlength);
    wstr = &wstrtemp[0];
  }
#endif
  // Convert from wide string to UTF-8
  int length = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
  if (length)
  {
    u8str.assign(length - 1, 0);
    length = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &u8str[0], length, nullptr, nullptr);
  }
  return u8str;
}

// Next functions add the portNumber to the name so that
// the device's names are sure to be listed with individual names
// even when they have the same brand name
inline void MakeUniqueInPortName(std::string& deviceName, unsigned int portNumber)
{
  int x = 1;
  for (unsigned int i = 0; i < portNumber; i++)
  {
    MIDIINCAPS deviceCaps;
    midiInGetDevCaps(i, &deviceCaps, sizeof(MIDIINCAPS));
    auto stringName = ConvertToUTF8(deviceCaps.szPname);
    if (deviceName == stringName)
    {
      x++;
    }
  }
  deviceName += " ";
  deviceName += std::to_string(x);
}

inline void MakeUniqueOutPortName(std::string& deviceName, unsigned int portNumber)
{
  int x = 1;
  for (unsigned int i = 0; i < portNumber; i++)
  {
    MIDIOUTCAPS deviceCaps;
    midiOutGetDevCaps(i, &deviceCaps, sizeof(MIDIOUTCAPS));
    auto stringName = ConvertToUTF8(deviceCaps.szPname);
    if (deviceName == stringName)
    {
      x++;
    }
  }
  deviceName += " ";
  deviceName += std::to_string(x);
}

}
