#pragma once
#include <libremidi/detail/midi_api.hpp>
#include <libremidi/libremidi.hpp>

#include <CoreMIDI/CoreMIDI.h>
#include <CoreServices/CoreServices.h>

#include <cmath>

#if TARGET_OS_IPHONE
  #include <CoreAudio/CoreAudioTypes.h>
  #include <mach/mach_time.h>
  #define AudioGetCurrentHostTime mach_absolute_time
#else
  #include <CoreAudio/HostTime.h>
#endif

namespace libremidi
{
namespace
{
#if TARGET_OS_IPHONE
inline uint64_t AudioConvertHostTimeToNanos(uint64_t hostTime)
{
  static const struct mach_timebase_info timebase = [] {
    struct mach_timebase_info theTimeBaseInfo;
    mach_timebase_info(&theTimeBaseInfo);
    return theTimeBaseInfo;
  }();
  const auto numer = timebase.numer;
  const auto denom = timebase.denom;

  __uint128_t res = hostTime;
  if (numer != denom)
  {
    res *= numer;
    res /= denom;
  }
  return static_cast<uint64_t>(res);
}
#endif
// This function was submitted by Douglas Casey Tucker and apparently
// derived largely from PortMidi.
inline CFStringRef EndpointName(MIDIEndpointRef endpoint, bool isExternal)
{
  CFMutableStringRef result = CFStringCreateMutable(nullptr, 0);
  CFStringRef str;

  // Begin with the endpoint's name.
  str = nullptr;
  MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName, &str);
  if (str != nullptr)
  {
    CFStringAppend(result, str);
    CFRelease(str);
  }

  MIDIEntityRef entity = 0;
  MIDIEndpointGetEntity(endpoint, &entity);
  if (entity == 0)
    // probably virtual
    return result;

  if (CFStringGetLength(result) == 0)
  {
    // endpoint name has zero length -- try the entity
    str = nullptr;
    MIDIObjectGetStringProperty(entity, kMIDIPropertyName, &str);
    if (str != nullptr)
    {
      CFStringAppend(result, str);
      CFRelease(str);
    }
  }
  // now consider the device's name
  MIDIDeviceRef device = 0;
  MIDIEntityGetDevice(entity, &device);
  if (device == 0)
    return result;

  str = nullptr;
  MIDIObjectGetStringProperty(device, kMIDIPropertyName, &str);
  if (CFStringGetLength(result) == 0)
  {
    CFRelease(result);
    return str;
  }
  if (str != nullptr)
  {
    // if an external device has only one entity, throw away
    // the endpoint name and just use the device name
    if (isExternal && MIDIDeviceGetNumberOfEntities(device) < 2)
    {
      CFRelease(result);
      return str;
    }
    else
    {
      if (CFStringGetLength(str) == 0)
      {
        CFRelease(str);
        return result;
      }
      // does the entity name already start with the device name?
      // (some drivers do this though they shouldn't)
      // if so, do not prepend
      if (CFStringCompareWithOptions(
              result, /* endpoint name */
              str /* device name */, CFRangeMake(0, CFStringGetLength(str)), 0)
          != kCFCompareEqualTo)
      {
        // prepend the device name to the entity name
        if (CFStringGetLength(result) > 0)
          CFStringInsert(result, 0, CFSTR(" "));
        CFStringInsert(result, 0, str);
      }
      CFRelease(str);
    }
  }
  return result;
}

// This function was submitted by Douglas Casey Tucker and apparently
// derived largely from PortMidi.
inline CFStringRef ConnectedEndpointName(MIDIEndpointRef endpoint)
{
  CFMutableStringRef result = CFStringCreateMutable(nullptr, 0);
  CFStringRef str;
  OSStatus err;
  int i;

  // Does the endpoint have connections?
  CFDataRef connections = nullptr;
  int nConnected = 0;
  bool anyStrings = false;
  err = MIDIObjectGetDataProperty(endpoint, kMIDIPropertyConnectionUniqueID, &connections);
  if (connections != nullptr)
  {
    // It has connections, follow them
    // Concatenate the names of all connected devices
    nConnected = CFDataGetLength(connections) / sizeof(MIDIUniqueID);
    if (nConnected)
    {
      const SInt32* pid = (const SInt32*)(CFDataGetBytePtr(connections));
      for (i = 0; i < nConnected; ++i, ++pid)
      {
        MIDIUniqueID id = CFSwapInt32BigToHost(*pid);
        MIDIObjectRef connObject;
        MIDIObjectType connObjectType;
        err = MIDIObjectFindByUniqueID(id, &connObject, &connObjectType);
        if (err == noErr)
        {
          if (connObjectType == kMIDIObjectType_ExternalSource
              || connObjectType == kMIDIObjectType_ExternalDestination)
          {
            // Connected to an external device's endpoint (10.3 and later).
            str = EndpointName((MIDIEndpointRef)(connObject), true);
          }
          else
          {
            // Connected to an external device (10.2) (or something else,
            // catch-
            str = nullptr;
            MIDIObjectGetStringProperty(connObject, kMIDIPropertyName, &str);
          }
          if (str != nullptr)
          {
            if (anyStrings)
              CFStringAppend(result, CFSTR(", "));
            else
              anyStrings = true;
            CFStringAppend(result, str);
            CFRelease(str);
          }
        }
      }
    }
    CFRelease(connections);
  }
  if (anyStrings)
    return result;

  CFRelease(result);

  // Here, either the endpoint had no connections, or we failed to obtain names
  return EndpointName(endpoint, false);
}

}

// A structure to hold variables related to the CoreMIDI API
// implementation.
struct coremidi_data
{
  MIDIClientRef client{};
  MIDIPortRef port{};
  MIDIEndpointRef endpoint{};

  using CFString_handle = unique_handle<const __CFString, CFRelease>;
  static inline CFString_handle toCFString(std::string_view str) noexcept
  {
    return CFString_handle{CFStringCreateWithCString(nullptr, str.data(), kCFStringEncodingASCII) };
  }
};

}
