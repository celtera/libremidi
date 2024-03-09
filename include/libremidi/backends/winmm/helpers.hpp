#pragma once
#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN 1
#include <libremidi/detail/midi_api.hpp>

#include <algorithm>

// clang-format off
#include <windows.h>
#include <mmsystem.h>
// clang-format on

namespace libremidi
{

struct winmm_error_domain : public stdx::error_domain
{
public:
  constexpr winmm_error_domain() noexcept
      : error_domain{{0xa32b080ac770514eULL, 0xef59a407f921da43ULL}}
  {
  }

  stdx::string_ref name() const noexcept override { return "winmm"; }

  bool equivalent(const stdx::error& lhs, const stdx::error& rhs) const noexcept override
  {
    if (lhs.domain() == rhs.domain())
      return error_cast<int>(lhs) == error_cast<int>(rhs);

    return false;
  }

  stdx::string_ref message(const stdx::error& e) const noexcept override
  {
    switch (error_cast<int>(e))
    {
      case MMSYSERR_NOERROR: return "No error";
      case MMSYSERR_ERROR: return "Error";
      case MMSYSERR_BADDEVICEID: return "Bad device ID";
      case MMSYSERR_NOTENABLED: return "Not enabled";
      case MMSYSERR_ALLOCATED: return "Allocated";
      case MMSYSERR_INVALHANDLE: return "Invalid handle";
      case MMSYSERR_NODRIVER: return "No driver";
      case MMSYSERR_NOMEM: return "No memory";
      case MMSYSERR_NOTSUPPORTED: return "Not supported";
      case MMSYSERR_BADERRNUM: return "Bad errnum";
      case MMSYSERR_INVALFLAG: return "Invalid flag";
      case MMSYSERR_INVALPARAM: return "Invalid parameter";
      case MMSYSERR_HANDLEBUSY: return "Handle busy";
      case MMSYSERR_INVALIDALIAS: return "Invalid alias";
      case MMSYSERR_BADDB: return "Bad database";
      case MMSYSERR_KEYNOTFOUND: return "Key not found";
      case MMSYSERR_READERROR: return "Read error";
      case MMSYSERR_WRITEERROR: return "Write error";
      case MMSYSERR_DELETEERROR: return "Delete error";
      case MMSYSERR_VALNOTFOUND: return "Value not found";
      case MMSYSERR_NODRIVERCB: return "No driver callback";
      case MMSYSERR_MOREDATA: return "More data";
    }
    return "Unknown error code";
  }
};

inline stdx::error from_mmerr(int ret) noexcept
{
  static constexpr winmm_error_domain domain;
  return {ret, domain};
}

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
    u8str.assign(static_cast<std::string::size_type>(length - 1), 0);
    /*length =*/WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &u8str[0], length, nullptr, nullptr);
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
