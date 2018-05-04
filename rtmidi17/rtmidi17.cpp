#include <cmath>
#include <sstream>

#if !defined(RTMIDI17_HEADER_ONLY)
#  include <rtmidi17/rtmidi17.hpp>
#endif

#include <rtmidi17/detail/midi_api.hpp>

#if !defined(RTMIDI_ALSA) && !defined(RTMIDI_JACK) && !defined(RTMIDI_COREAUDIO) \
    && !defined(RTMIDI_WINMM)
#  define RTMIDI_DUMMY
#endif

#if defined(RTMIDI_ALSA)
#  include <rtmidi17/detail/alsa.hpp>
#endif

#if defined(RTMIDI_JACK)
#  include <rtmidi17/detail/jack.hpp>
#endif

#if defined(RTMIDI_COREAUDIO)
#  include <rtmidi17/detail/coreaudio.hpp>
#endif

#if defined(RTMIDI_WINMM)
#  include <rtmidi17/detail/winmm.hpp>
#endif

#if defined(RTMIDI_DUMMY)
#  include <rtmidi17/detail/dummy.hpp>
#endif

namespace rtmidi
{
RTMIDI17_INLINE midi_exception::~midi_exception() = default;
RTMIDI17_INLINE no_devices_found_error::~no_devices_found_error() = default;
RTMIDI17_INLINE invalid_device_error::~invalid_device_error() = default;
RTMIDI17_INLINE memory_error::~memory_error() = default;
RTMIDI17_INLINE invalid_parameter_error::~invalid_parameter_error() = default;
RTMIDI17_INLINE invalid_use_error::~invalid_use_error() = default;
RTMIDI17_INLINE driver_error::~driver_error() = default;
RTMIDI17_INLINE system_error::~system_error() = default;
RTMIDI17_INLINE thread_error::~thread_error() = default;

RTMIDI17_INLINE midi_in::~midi_in() = default;
RTMIDI17_INLINE midi_out::~midi_out() = default;

[[nodiscard]] RTMIDI17_INLINE std::vector<rtmidi::API> available_apis() noexcept
{
  std::vector<rtmidi::API> apis;

  // The order here will control the order of RtMidi's API search in
  // the constructor.
#if defined(RTMIDI_COREAUDIO)
  apis.push_back(rtmidi::API::MACOSX_CORE);
#endif
#if defined(RTMIDI_ALSA)
  apis.push_back(rtmidi::API::LINUX_ALSA);
#endif
#if defined(RTMIDI_JACK)
  apis.push_back(rtmidi::API::UNIX_JACK);
#endif
#if defined(RTMIDI_WINMM)
  apis.push_back(rtmidi::API::WINDOWS_MM);
#endif
#if defined(RTMIDI_DUMMY)
  apis.push_back(rtmidi::API::DUMMY);
#endif
  return apis;
}

[[nodiscard]] RTMIDI17_INLINE std::unique_ptr<observer_api>
open_midi_observer(rtmidi::API api, observer::callbacks&& cb) {
#if defined(RTMIDI_JACK)
  if (api == rtmidi::API::UNIX_JACK)
    return std::make_unique<observer_jack>(std::move(cb));
#endif
#if defined(RTMIDI_ALSA)
  if (api == rtmidi::API::LINUX_ALSA)
    return std::make_unique<observer_alsa>(std::move(cb));
#endif
#if defined(RTMIDI_WINMM)
  if (api == rtmidi::API::WINDOWS_MM)
    return std::make_unique<observer_winmm>(std::move(cb));
#endif
#if defined(RTMIDI_COREAUDIO)
  if (api == rtmidi::API::MACOSX_CORE)
    return std::make_unique<observer_core>(std::move(cb));
#endif
  return {};
}

    [[nodiscard]] RTMIDI17_INLINE std::unique_ptr<midi_in_api> open_midi_in(
        rtmidi::API api, const std::string& clientName, unsigned int queueSizeLimit)
{
#if defined(RTMIDI_JACK)
  if (api == rtmidi::API::UNIX_JACK)
    return std::make_unique<midi_in_jack>(clientName, queueSizeLimit);
#endif
#if defined(RTMIDI_ALSA)
  if (api == rtmidi::API::LINUX_ALSA)
    return std::make_unique<midi_in_alsa>(clientName, queueSizeLimit);
#endif
#if defined(RTMIDI_WINMM)
  if (api == rtmidi::API::WINDOWS_MM)
    return std::make_unique<MidiInWinMM>(clientName, queueSizeLimit);
#endif
#if defined(RTMIDI_COREAUDIO)
  if (api == rtmidi::API::MACOSX_CORE)
    return std::make_unique<midi_in_core>(clientName, queueSizeLimit);
#endif
#if defined(RTMIDI_DUMMY)
  if (api == rtmidi::API::DUMMY)
  {
    return std::make_unique<midi_in_dummy>(clientName, queueSizeLimit);
  }
#endif
  return {};
}

[[nodiscard]] RTMIDI17_INLINE std::unique_ptr<midi_out_api>
open_midi_out(rtmidi::API api, const std::string& clientName) {
#if defined(RTMIDI_JACK)
  if (api == rtmidi::API::UNIX_JACK)
    return std::make_unique<midi_out_jack>(clientName);
#endif
#if defined(RTMIDI_ALSA)
  if (api == rtmidi::API::LINUX_ALSA)
    return std::make_unique<midi_out_alsa>(clientName);
#endif
#if defined(RTMIDI_WINMM)
  if (api == rtmidi::API::WINDOWS_MM)
    return std::make_unique<MidiOutWinMM>(clientName);
#endif
#if defined(RTMIDI_COREAUDIO)
  if (api == rtmidi::API::MACOSX_CORE)
    return std::make_unique<midi_out_core>(clientName);
#endif
#if defined(RTMIDI_DUMMY)
  if (api == rtmidi::API::DUMMY)
  {
    return std::make_unique<midi_out_dummy>(clientName);
  }
#endif
  return {};
}

RTMIDI17_INLINE observer::observer(rtmidi::API api, observer::callbacks cbs)
    : impl_{open_midi_observer(api, std::move(cbs))}
{
}

RTMIDI17_INLINE
observer::~observer() = default;

RTMIDI17_INLINE
rtmidi::API midi_in::get_current_api() const noexcept
{
  return rtapi_->get_current_api();
}

RTMIDI17_INLINE
void midi_in::open_port(unsigned int portNumber, const std::string& portName)
{
  rtapi_->open_port(portNumber, portName);
}

RTMIDI17_INLINE
void midi_in::open_virtual_port(const std::string& portName)
{
  rtapi_->open_virtual_port(portName);
}

RTMIDI17_INLINE
void midi_in::close_port()
{
  rtapi_->close_port();
}

RTMIDI17_INLINE
bool midi_in::is_port_open() const noexcept
{
  return rtapi_->is_port_open();
}

RTMIDI17_INLINE
void midi_in::set_callback(message_callback callback)
{
  (static_cast<midi_in_api*>(rtapi_.get()))->set_callback(std::move(callback));
}

RTMIDI17_INLINE
void midi_in::cancel_callback()
{
  (static_cast<midi_in_api*>(rtapi_.get()))->cancel_callback();
}

RTMIDI17_INLINE
unsigned int midi_in::get_port_count()
{
  return rtapi_->get_port_count();
}

RTMIDI17_INLINE
std::string midi_in::get_port_name(unsigned int portNumber)
{
  return rtapi_->get_port_name(portNumber);
}

RTMIDI17_INLINE
void midi_in::ignore_types(bool midiSysex, bool midiTime, bool midiSense)
{
  (static_cast<midi_in_api*>(rtapi_.get()))->ignore_types(midiSysex, midiTime, midiSense);
}

RTMIDI17_INLINE
message midi_in::get_message()
{
  return (static_cast<midi_in_api*>(rtapi_.get()))->get_message();
}

RTMIDI17_INLINE
void midi_in::set_error_callback(midi_error_callback errorCallback)
{
  rtapi_->set_error_callback(std::move(errorCallback));
}

RTMIDI17_INLINE
rtmidi::API midi_out::get_current_api() noexcept
{
  return rtapi_->get_current_api();
}

RTMIDI17_INLINE
void midi_out::open_port(unsigned int portNumber, const std::string& portName)
{
  rtapi_->open_port(portNumber, portName);
}

RTMIDI17_INLINE
void midi_out::open_virtual_port(const std::string& portName)
{
  rtapi_->open_virtual_port(portName);
}

RTMIDI17_INLINE
void midi_out::close_port()
{
  rtapi_->close_port();
}

RTMIDI17_INLINE
bool midi_out::is_port_open() const noexcept
{
  return rtapi_->is_port_open();
}

RTMIDI17_INLINE
unsigned int midi_out::get_port_count()
{
  return rtapi_->get_port_count();
}

RTMIDI17_INLINE
std::string midi_out::get_port_name(unsigned int portNumber)
{
  return rtapi_->get_port_name(portNumber);
}

RTMIDI17_INLINE
void midi_out::send_message(const std::vector<unsigned char>& message)
{
  (static_cast<midi_out_api*>(rtapi_.get()))->send_message(message.data(), message.size());
}

RTMIDI17_INLINE
void midi_out::send_message(const unsigned char* message, size_t size)
{
  (static_cast<midi_out_api*>(rtapi_.get()))->send_message(message, size);
}

RTMIDI17_INLINE
void midi_out::set_error_callback(midi_error_callback errorCallback) noexcept
{
  rtapi_->set_error_callback(std::move(errorCallback));
}

RTMIDI17_INLINE
std::string get_version() noexcept
{
  return std::string{RTMIDI17_VERSION};
}

RTMIDI17_INLINE
midi_in::midi_in(rtmidi::API api, const std::string& clientName, unsigned int queueSizeLimit)
{
  if (api != rtmidi::API::UNSPECIFIED)
  {
    // Attempt to open the specified API.
    rtapi_ = open_midi_in(api, clientName, queueSizeLimit);
    if (rtapi_)
    {
      return;
    }

    // No compiled support for specified API value.  Issue a warning
    // and continue as if no API was specified.
    std::cerr << "\nRtMidiIn: no compiled support for specified API argument!\n\n" << std::endl;
  }

  // Iterate through the compiled APIs and return as soon as we find
  // one with at least one port or we reach the end of the list.
  for (const auto& api : available_apis())
  {
    rtapi_ = open_midi_in(api, clientName, queueSizeLimit);
    if (rtapi_ && rtapi_->get_port_count() != 0)
    {
      break;
    }
  }

  if (rtapi_)
  {
    return;
  }

  // It should not be possible to get here because the preprocessor
  // definition RTMIDI_DUMMY is automatically defined if no
  // API-specific definitions are passed to the compiler. But just in
  // case something weird happens, we'll throw an error.

  throw midi_exception("RtMidiIn: no compiled API support found ... critical error!!");
}

RTMIDI17_INLINE
void midi_in::set_client_name(const std::string& clientName)
{
  rtapi_->set_client_name(clientName);
}

void midi_in::set_port_name(const std::string& portName)
{
  rtapi_->set_port_name(portName);
}

RTMIDI17_INLINE
midi_out::midi_out(rtmidi::API api, const std::string& clientName)
{
  if (api != rtmidi::API::UNSPECIFIED)
  {
    // Attempt to open the specified API.
    rtapi_ = open_midi_out(api, clientName);
    if (rtapi_)
    {
      return;
    }

    // No compiled support for specified API value.  Issue a warning
    // and continue as if no API was specified.
    std::cerr << "\nRtMidiOut: no compiled support for specified API argument!\n\n" << std::endl;
  }

  // Iterate through the compiled APIs and return as soon as we find
  // one with at least one port or we reach the end of the list.
  for (const auto& api : available_apis())
  {
    rtapi_ = open_midi_out(api, clientName);
    if (rtapi_ && rtapi_->get_port_count() != 0)
    {
      break;
    }
  }

  if (rtapi_)
  {
    return;
  }

  // It should not be possible to get here because the preprocessor
  // definition RTMIDI_DUMMY is automatically defined if no
  // API-specific definitions are passed to the compiler. But just in
  // case something weird happens, we'll thrown an error.
  throw midi_exception{"RtMidiOut: no compiled API support found ... critical error!!"};
}

RTMIDI17_INLINE
void midi_out::set_client_name(const std::string& clientName)
{
  rtapi_->set_client_name(clientName);
}

RTMIDI17_INLINE
void midi_out::set_port_name(const std::string& portName)
{
  rtapi_->set_port_name(portName);
}
}
