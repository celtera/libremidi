#include <cmath>
#include <sstream>

#if !defined(REMIDI_HEADER_ONLY)
#  include <remidi/remidi.hpp>
#endif

#include <remidi/detail/midi_api.hpp>
#if !__has_include(<weak_libjack.h>) && !__has_include(<jack/jack.h>)
#  if defined(REMIDI_JACK)
#    undef REMIDI_JACK
#  endif
#endif
#if !defined(REMIDI_ALSA) && !defined(REMIDI_JACK) && !defined(REMIDI_COREAUDIO) \
    && !defined(REMIDI_WINMM)
#  define REMIDI_DUMMY
#endif

#if defined(REMIDI_ALSA)
#  include <remidi/detail/alsa.hpp>
#endif

#if defined(REMIDI_JACK)
#  include <remidi/detail/jack.hpp>
#endif

#if defined(REMIDI_COREAUDIO)
#  include <remidi/detail/coreaudio.hpp>
#endif

#if defined(REMIDI_WINMM)
#  include <remidi/detail/winmm.hpp>
#endif

#if defined(REMIDI_WINUWP)
#  include <remidi/detail/winuwp.hpp>
#endif

#if defined(REMIDI_DUMMY)
#  include <remidi/detail/dummy.hpp>
#endif

namespace remidi
{

// The order here will control the order of the API search in
// the constructor.
template <typename unused, typename... Args>
constexpr auto make_tl(unused, Args...)
{
  return std::tuple<Args...>{};
}
static constexpr auto available_backends = make_tl(
    0
#if defined(REMIDI_ALSA)
    ,
    alsa_backend {}
#endif
#if defined(REMIDI_COREAUDIO)
    ,
    core_backend {}
#endif
#if defined(REMIDI_JACK)
    ,
    jack_backend {}
#endif
#if defined(REMIDI_WINMM)
    ,
    winmm_backend {}
#endif
#if defined(REMIDI_WINUWP)
    ,
    winuwp_backend {}
#endif
#if defined(REMIDI_DUMMY)
    ,
    dummy_backend {}
#endif
);

// There should always be at least one back-end.
static_assert(std::tuple_size_v<decltype(available_backends)> >= 1);

template <typename F>
auto for_all_backends(F&& f)
{
  std::apply([&](auto&&... x) { (f(x), ...); }, available_backends);
}

template <typename F>
auto for_backend(remidi::API api, F&& f)
{
  for_all_backends([&](auto b) {
    if (b.API == api)
      f(b);
  });
}

REMIDI_INLINE midi_exception::~midi_exception() = default;
REMIDI_INLINE no_devices_found_error::~no_devices_found_error() = default;
REMIDI_INLINE invalid_device_error::~invalid_device_error() = default;
REMIDI_INLINE memory_error::~memory_error() = default;
REMIDI_INLINE invalid_parameter_error::~invalid_parameter_error() = default;
REMIDI_INLINE invalid_use_error::~invalid_use_error() = default;
REMIDI_INLINE driver_error::~driver_error() = default;
REMIDI_INLINE system_error::~system_error() = default;
REMIDI_INLINE thread_error::~thread_error() = default;

REMIDI_INLINE midi_in::~midi_in() = default;
REMIDI_INLINE midi_out::~midi_out() = default;

[[nodiscard]] REMIDI_INLINE std::vector<remidi::API> available_apis() noexcept
{
  std::vector<remidi::API> apis;
  for_all_backends([&](auto b) { apis.push_back(b.API); });
  return apis;
}

[[nodiscard]] REMIDI_INLINE std::unique_ptr<observer_api>
open_midi_observer(remidi::API api, observer::callbacks&& cb)
{
  std::unique_ptr<observer_api> ptr;

  for_backend(api, [&](auto b) {
    ptr = std::make_unique<typename decltype(b)::midi_observer>(std::move(cb));
  });

  return ptr;
}

[[nodiscard]] REMIDI_INLINE std::unique_ptr<midi_in_api>
open_midi_in(remidi::API api, std::string_view clientName, unsigned int queueSizeLimit)
{
  std::unique_ptr<midi_in_api> ptr;

  for_backend(api, [&](auto b) {
    ptr = std::make_unique<typename decltype(b)::midi_in>(clientName, queueSizeLimit);
  });

  return ptr;
}

[[nodiscard]] REMIDI_INLINE std::unique_ptr<midi_out_api>
open_midi_out(remidi::API api, std::string_view clientName)
{

  std::unique_ptr<midi_out_api> ptr;

  for_backend(
      api, [&](auto b) { ptr = std::make_unique<typename decltype(b)::midi_out>(clientName); });

  return ptr;
}

REMIDI_INLINE observer::observer(remidi::API api, observer::callbacks cbs)
    : impl_{open_midi_observer(api, std::move(cbs))}
{
}

REMIDI_INLINE
observer::~observer() = default;

REMIDI_INLINE
remidi::API midi_in::get_current_api() const noexcept
{
  return rtapi_->get_current_api();
}

REMIDI_INLINE
void midi_in::open_port(unsigned int portNumber, std::string_view portName)
{
  rtapi_->open_port(portNumber, portName);
}

REMIDI_INLINE
void midi_in::open_virtual_port(std::string_view portName)
{
  rtapi_->open_virtual_port(portName);
}

REMIDI_INLINE
void midi_in::close_port()
{
  rtapi_->close_port();
}

REMIDI_INLINE
bool midi_in::is_port_open() const noexcept
{
  return rtapi_->is_port_open();
}

REMIDI_INLINE
void midi_in::set_callback(message_callback callback)
{
  (static_cast<midi_in_api*>(rtapi_.get()))->set_callback(std::move(callback));
}

REMIDI_INLINE
void midi_in::cancel_callback()
{
  (static_cast<midi_in_api*>(rtapi_.get()))->cancel_callback();
}

REMIDI_INLINE
unsigned int midi_in::get_port_count()
{
  return rtapi_->get_port_count();
}

REMIDI_INLINE
std::string midi_in::get_port_name(unsigned int portNumber)
{
  return rtapi_->get_port_name(portNumber);
}

REMIDI_INLINE
void midi_in::ignore_types(bool midiSysex, bool midiTime, bool midiSense)
{
  (static_cast<midi_in_api*>(rtapi_.get()))->ignore_types(midiSysex, midiTime, midiSense);
}

REMIDI_INLINE
message midi_in::get_message()
{
  return (static_cast<midi_in_api*>(rtapi_.get()))->get_message();
}

REMIDI_INLINE
bool midi_in::get_message(message& msg)
{
  return (static_cast<midi_in_api*>(rtapi_.get()))->get_message(msg);
}

REMIDI_INLINE
void midi_in::set_error_callback(midi_error_callback errorCallback)
{
  rtapi_->set_error_callback(std::move(errorCallback));
}

REMIDI_INLINE
remidi::API midi_out::get_current_api() noexcept
{
  return rtapi_->get_current_api();
}

REMIDI_INLINE
void midi_out::open_port(unsigned int portNumber, std::string_view portName)
{
  rtapi_->open_port(portNumber, portName);
}

REMIDI_INLINE
void midi_out::open_virtual_port(std::string_view portName)
{
  rtapi_->open_virtual_port(portName);
}

REMIDI_INLINE
void midi_out::close_port()
{
  rtapi_->close_port();
}

REMIDI_INLINE
bool midi_out::is_port_open() const noexcept
{
  return rtapi_->is_port_open();
}

REMIDI_INLINE
unsigned int midi_out::get_port_count()
{
  return rtapi_->get_port_count();
}

REMIDI_INLINE
std::string midi_out::get_port_name(unsigned int portNumber)
{
  return rtapi_->get_port_name(portNumber);
}

REMIDI_INLINE
void midi_out::send_message(const std::vector<unsigned char>& message)
{
  send_message(message.data(), message.size());
}

REMIDI_INLINE
void midi_out::send_message(const remidi::message& message)
{
  send_message(message.bytes.data(), message.bytes.size());
}

#if REMIDI_HAS_SPAN
REMIDI_INLINE
void midi_out::send_message(std::span<unsigned char> message)
{
  send_message(message.data(), message.size());
}

#endif
REMIDI_INLINE
void midi_out::send_message(const unsigned char* message, size_t size)
{
  (static_cast<midi_out_api*>(rtapi_.get()))->send_message(message, size);
}

REMIDI_INLINE
void midi_out::set_error_callback(midi_error_callback errorCallback) noexcept
{
  rtapi_->set_error_callback(std::move(errorCallback));
}

REMIDI_INLINE
std::string get_version() noexcept
{
  return std::string{REMIDI_VERSION};
}

REMIDI_INLINE
midi_in::midi_in(remidi::API api, std::string_view clientName, unsigned int queueSizeLimit)
{
  if (api != remidi::API::UNSPECIFIED)
  {
    // Attempt to open the specified API.
    if ((rtapi_ = open_midi_in(api, clientName, queueSizeLimit)))
    {
      return;
    }

    // No compiled support for specified API value.  Issue a warning
    // and continue as if no API was specified.
    std::cerr << "\nremidiIn: no compiled support for specified API argument!\n\n" << std::endl;
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
}

REMIDI_INLINE
void midi_in::set_client_name(std::string_view clientName)
{
  rtapi_->set_client_name(clientName);
}

REMIDI_INLINE
void midi_in::set_port_name(std::string_view portName)
{
  rtapi_->set_port_name(portName);
}

REMIDI_INLINE
midi_out::midi_out(remidi::API api, std::string_view clientName)
{
  if (api != remidi::API::UNSPECIFIED)
  {
    // Attempt to open the specified API.
    rtapi_ = open_midi_out(api, clientName);
    if (rtapi_)
    {
      return;
    }

    // No compiled support for specified API value.  Issue a warning
    // and continue as if no API was specified.
    std::cerr << "\nremidiOut: no compiled support for specified API argument!\n\n" << std::endl;
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
  // definition REMIDI_DUMMY is automatically defined if no
  // API-specific definitions are passed to the compiler. But just in
  // case something weird happens, we'll thrown an error.
  throw midi_exception{"remidiOut: no compiled API support found ... critical error!!"};
}

REMIDI_INLINE
void midi_out::set_client_name(std::string_view clientName)
{
  rtapi_->set_client_name(clientName);
}

REMIDI_INLINE
void midi_out::set_port_name(std::string_view portName)
{
  rtapi_->set_port_name(portName);
}
}
