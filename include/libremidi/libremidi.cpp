#include <cmath>

#include <array>
#include <thread>
#include <tuple>

#if !defined(LIBREMIDI_HEADER_ONLY)
  #include <libremidi/libremidi.hpp>
#endif

#include <libremidi/detail/midi_api.hpp>
#if !__has_include(<weak_libjack.h>) && !__has_include(<jack/jack.h>)
  #if defined(LIBREMIDI_JACK)
    #undef LIBREMIDI_JACK
  #endif
#endif
#if !defined(LIBREMIDI_ALSA) && !defined(LIBREMIDI_JACK) && !defined(LIBREMIDI_COREAUDIO) \
    && !defined(LIBREMIDI_WINMM)
  #define LIBREMIDI_DUMMY
#endif

#if defined(LIBREMIDI_ALSA)
  #include <libremidi/backends/alsa.hpp>
  #include <libremidi/backends/raw_alsa.hpp>
#endif

#if defined(LIBREMIDI_JACK)
  #include <libremidi/backends/jack.hpp>
#endif

#if defined(LIBREMIDI_COREAUDIO)
  #include <libremidi/backends/coreaudio.hpp>
#endif

#if defined(LIBREMIDI_WINMM)
  #include <libremidi/backends/winmm.hpp>
#endif

#if defined(LIBREMIDI_WINUWP)
  #include <libremidi/backends/winuwp.hpp>
#endif

#if defined(LIBREMIDI_EMSCRIPTEN)
  #include <libremidi/backends/emscripten.hpp>
#endif

#include <libremidi/backends/dummy.hpp>

namespace libremidi
{
namespace
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
#if defined(LIBREMIDI_ALSA)
    ,
    raw_alsa_backend{}, alsa_backend{}
#endif
#if defined(LIBREMIDI_COREAUDIO)
    ,
    core_backend{}
#endif
#if defined(LIBREMIDI_JACK)
    ,
    jack_backend{}
#endif
#if defined(LIBREMIDI_WINMM)
    ,
    winmm_backend{}
#endif
#if defined(LIBREMIDI_WINUWP)
    ,
    winuwp_backend{}
#endif
#if defined(LIBREMIDI_EMSCRIPTEN)
    ,
    emscripten_backend{}
#endif
    ,
    dummy_backend{});

// There should always be at least one back-end.
static_assert(std::tuple_size_v<decltype(available_backends)> >= 1);

template <typename F>
auto for_all_backends(F&& f)
{
  std::apply([&](auto&&... x) { (f(x), ...); }, available_backends);
}

template <typename F>
auto for_backend(libremidi::API api, F&& f)
{
  static constexpr auto is_api
      = [](auto& backend, libremidi::API api) { return backend.API == api; };
  std::apply([&](auto&&... b) { ((is_api(b, api) && (f(b), true)) || ...); }, available_backends);
}
}

LIBREMIDI_INLINE
std::string_view get_version() noexcept
{
  return LIBREMIDI_VERSION;
}

LIBREMIDI_INLINE std::string_view get_api_name(libremidi::API api)
{
  std::string_view ret;
  for_backend(api, [&](auto& b) { ret = b.name; });
  return ret;
}

LIBREMIDI_INLINE std::string_view get_api_display_name(libremidi::API api)
{
  std::string_view ret;
  for_backend(api, [&](auto& b) { ret = b.display_name; });
  return ret;
}

LIBREMIDI_INLINE libremidi::API get_compiled_api_by_name(std::string_view name)
{
  libremidi::API ret = libremidi::API::UNSPECIFIED;
  for_all_backends([&](auto& b) {
    if (name == b.name)
      ret = b.API;
  });
  return ret;
}

[[nodiscard]] LIBREMIDI_INLINE std::vector<libremidi::API> available_apis() noexcept
{
  std::vector<libremidi::API> apis;
  for_all_backends([&](auto b) { apis.push_back(b.API); });
  return apis;
}

LIBREMIDI_INLINE midi_exception::~midi_exception() = default;
LIBREMIDI_INLINE no_devices_found_error::~no_devices_found_error() = default;
LIBREMIDI_INLINE invalid_device_error::~invalid_device_error() = default;
LIBREMIDI_INLINE memory_error::~memory_error() = default;
LIBREMIDI_INLINE invalid_parameter_error::~invalid_parameter_error() = default;
LIBREMIDI_INLINE invalid_use_error::~invalid_use_error() = default;
LIBREMIDI_INLINE driver_error::~driver_error() = default;
LIBREMIDI_INLINE system_error::~system_error() = default;
LIBREMIDI_INLINE thread_error::~thread_error() = default;

[[nodiscard]] LIBREMIDI_INLINE std::unique_ptr<observer_api>
open_midi_observer(libremidi::API api, observer_configuration&& cb)
{
  std::unique_ptr<observer_api> ptr;

  for_backend(api, [&]<typename T>(T) {
    using conf_type = typename T::midi_observer_configuration;
    conf_type c;
    ptr = libremidi::make<typename T::midi_observer>(std::move(cb), std::move(c));
  });

  return ptr;
}

LIBREMIDI_INLINE observer::observer(libremidi::API api, observer_configuration cbs)
    : impl_{open_midi_observer(api, std::move(cbs))}
{
}

LIBREMIDI_INLINE
observer::~observer() = default;

LIBREMIDI_INLINE
libremidi::API observer::get_current_api() const noexcept
{
  return impl_->get_current_api();
}
LIBREMIDI_INLINE
std::vector<libremidi::port_information> observer::get_input_ports() const noexcept
{
  return impl_->get_input_ports();
}
LIBREMIDI_INLINE
std::vector<libremidi::port_information> observer::get_output_ports() const noexcept
{
  return impl_->get_input_ports();
}

LIBREMIDI_INLINE observer::observer(observer&& other) noexcept
    : impl_{std::move(other.impl_)}
{
  other.impl_ = std::make_unique<libremidi::observer_dummy>(
      observer_configuration{}, dummy_configuration{});
}
LIBREMIDI_INLINE observer& observer::operator=(observer&& other) noexcept
{
  this->impl_ = std::move(other.impl_);
  other.impl_ = std::make_unique<libremidi::observer_dummy>(
      observer_configuration{}, dummy_configuration{});
  return *this;
}

LIBREMIDI_INLINE midi_in::~midi_in() = default;
LIBREMIDI_INLINE midi_in::midi_in(midi_in&& other) noexcept
    : impl_{std::move(other.impl_)}
{
  other.impl_
      = std::make_unique<libremidi::midi_in_dummy>(input_configuration{}, dummy_configuration{});
}
LIBREMIDI_INLINE midi_in& midi_in::operator=(midi_in&& other) noexcept
{
  this->impl_ = std::move(other.impl_);
  other.impl_
      = std::make_unique<libremidi::midi_in_dummy>(input_configuration{}, dummy_configuration{});
  return *this;
}

LIBREMIDI_INLINE midi_out::~midi_out() = default;

LIBREMIDI_INLINE midi_out::midi_out(midi_out&& other) noexcept
    : impl_{std::move(other.impl_)}
{
  other.impl_
      = std::make_unique<libremidi::midi_out_dummy>(output_configuration{}, dummy_configuration{});
}
LIBREMIDI_INLINE midi_out& midi_out::operator=(midi_out&& other) noexcept
{
  this->impl_ = std::move(other.impl_);
  other.impl_
      = std::make_unique<libremidi::midi_out_dummy>(output_configuration{}, dummy_configuration{});
  return *this;
}

LIBREMIDI_INLINE
libremidi::API midi_in::get_current_api() const noexcept
{
  return impl_->get_current_api();
}

LIBREMIDI_INLINE
void midi_in::open_port(const port_information& port, std::string_view portName)
{
  if (impl_->is_port_open())
    return;

  if (impl_->open_port(port, portName))
  {
    impl_->connected_ = true;
    impl_->port_open_ = true;
  }
}

LIBREMIDI_INLINE
void midi_in::open_virtual_port(std::string_view portName)
{
  if (impl_->is_port_open())
    return;

  if (impl_->open_virtual_port(portName))
  {
    impl_->port_open_ = true;
  }
}

LIBREMIDI_INLINE
void midi_in::close_port()
{
  impl_->close_port();

  impl_->connected_ = false;
  impl_->port_open_ = false;
}

LIBREMIDI_INLINE
bool midi_in::is_port_open() const noexcept
{
  return impl_->is_port_open();
}

LIBREMIDI_INLINE
bool midi_in::is_port_connected() const noexcept
{
  return impl_->is_port_connected();
}

LIBREMIDI_INLINE
libremidi::API midi_out::get_current_api() noexcept
{
  return impl_->get_current_api();
}

LIBREMIDI_INLINE
void midi_out::open_port(const port_information& port, std::string_view portName)
{
  if (impl_->is_port_open())
    return;

  if (impl_->open_port(port, portName))
  {
    impl_->connected_ = true;
    impl_->port_open_ = true;
  }
}

LIBREMIDI_INLINE
void midi_out::open_virtual_port(std::string_view portName)
{
  if (impl_->is_port_open())
    return;

  if (impl_->open_virtual_port(portName))
  {
    impl_->port_open_ = true;
  }
}

LIBREMIDI_INLINE
void midi_out::close_port()
{
  impl_->close_port();
}

LIBREMIDI_INLINE
bool midi_out::is_port_open() const noexcept
{
  return impl_->is_port_open();
}

LIBREMIDI_INLINE
bool midi_out::is_port_connected() const noexcept
{
  return impl_->is_port_connected();
}

LIBREMIDI_INLINE
void midi_out::send_message(const libremidi::message& message)
{
  send_message(message.bytes.data(), message.bytes.size());
}

LIBREMIDI_INLINE
void midi_out::send_message(std::span<const unsigned char> message)
{
  send_message(message.data(), message.size());
}

LIBREMIDI_INLINE
void midi_out::send_message(unsigned char b0)
{
  send_message(&b0, 1);
}

LIBREMIDI_INLINE
void midi_out::send_message(unsigned char b0, unsigned char b1)
{
  send_message(std::to_array({b0, b1}));
}

LIBREMIDI_INLINE
void midi_out::send_message(unsigned char b0, unsigned char b1, unsigned char b2)
{
  send_message(std::to_array({b0, b1, b2}));
}

LIBREMIDI_INLINE
void midi_out::send_message(const unsigned char* message, size_t size)
{
  (static_cast<midi_out_api*>(impl_.get()))->send_message(message, size);
}

LIBREMIDI_INLINE
midi_in::midi_in(input_configuration base_conf, std::any api_conf)
{
  assert(api_conf.has_value());
  auto from_api = [&]<typename T>(T& backend) mutable {
    if (auto conf = std::any_cast<typename T::midi_in_configuration>(&api_conf))
    {
      this->impl_ = libremidi::make<typename T::midi_in>(std::move(base_conf), std::move(*conf));
      return true;
    }
    return false;
  };
  std::apply([&](auto&&... b) { (from_api(b) || ...); }, available_backends);

  if (!impl_)
    throw midi_exception{"midi_out: requested API not found"};
}

LIBREMIDI_INLINE
midi_in::midi_in(input_configuration conf) noexcept
{
  // First try to open the first API which has ports
  auto init1 = [&]<typename T>(T& backend) mutable {
    try
    {
      auto obs = libremidi::make<typename T::midi_observer>(
          observer_configuration{}, typename T::midi_observer_configuration{});
      if (!obs)
        return false;

      if (obs->get_input_ports().size() <= 0)
        return false;

      impl_ = libremidi::make<typename T::midi_in>(
          input_configuration{conf}, typename T::midi_in_configuration{});
      return true;
    }
    catch (...)
    {
    }
    return false;
  };
  std::apply([&](auto&&... b) { (init1(b) || ...); }, available_backends);

  if (impl_)
    return;

  // Then try to open simply the first available API if not possible
  auto init2 = [&]<typename T>(T& backend) mutable {
    try
    {
      impl_ = libremidi::make<typename T::midi_in>(
          input_configuration{conf}, typename T::midi_in_configuration{});
      return true;
    }
    catch (...)
    {
    }
    return false;
  };
  std::apply([&](auto&&... b) { (init2(b) || ...); }, available_backends);

  // If everything false return a dummy backend
  impl_ = std::make_unique<midi_in_dummy>(input_configuration{}, dummy_configuration{});
}

LIBREMIDI_INLINE
void midi_in::set_port_name(std::string_view portName)
{
  impl_->set_port_name(portName);
}

LIBREMIDI_INLINE
midi_out::midi_out(output_configuration base_conf, std::any api_conf)
{
  assert(api_conf.has_value());
  auto from_api = [&]<typename T>(T& backend) mutable {
    if (auto conf = std::any_cast<typename T::midi_out_configuration>(&api_conf))
    {
      this->impl_ = libremidi::make<typename T::midi_out>(std::move(base_conf), std::move(*conf));
      return true;
    }
    return false;
  };
  std::apply([&](auto&&... b) { (from_api(b) || ...); }, available_backends);

  if (!impl_)
    throw midi_exception{"midi_out: requested API not found"};
}

LIBREMIDI_INLINE
midi_out::midi_out(output_configuration conf) noexcept
{
  // First try to open the first API which has ports
  auto init1 = [&]<typename T>(T& backend) mutable {
    try
    {
      auto obs = libremidi::make<typename T::midi_observer>(
          observer_configuration{}, typename T::midi_observer_configuration{});
      if (!obs)
        return false;

      if (obs->get_output_ports().size() <= 0)
        return false;

      impl_ = libremidi::make<typename T::midi_out>(
          output_configuration{conf}, typename T::midi_out_configuration{});
      return true;
    }
    catch (...)
    {
    }
    return false;
  };
  std::apply([&](auto&&... b) { (init1(b) || ...); }, available_backends);

  if (impl_)
    return;

  // Then try to open simply the first available API if not possible
  auto init2 = [&]<typename T>(T& backend) mutable {
    try
    {
      impl_ = libremidi::make<typename T::midi_out>(
          output_configuration{conf}, typename T::midi_out_configuration{});
      return true;
    }
    catch (...)
    {
    }
    return false;
  };
  std::apply([&](auto&&... b) { (init2(b) || ...); }, available_backends);

  // If everything false return a dummy backend
  impl_ = std::make_unique<midi_out_dummy>(output_configuration{}, dummy_configuration{});
}

LIBREMIDI_INLINE
void midi_out::set_port_name(std::string_view portName)
{
  impl_->set_port_name(portName);
}
}
