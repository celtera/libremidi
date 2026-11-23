#pragma once
#include <libremidi/config.hpp>

#include <exception>
#include <memory>
#include <tuple>
#include <type_traits>

#if !__has_include(<jack/jack.h>)
  #if defined(LIBREMIDI_JACK)
    #undef LIBREMIDI_JACK
  #endif
#endif
#if !defined(LIBREMIDI_ALSA) && !defined(LIBREMIDI_JACK) && !defined(LIBREMIDI_COREMIDI) \
    && !defined(LIBREMIDI_WINMM)
  #define LIBREMIDI_DUMMY
#endif

#if defined(LIBREMIDI_ALSA)
  #include <libremidi/backends/alsa_seq.hpp>
  #include <libremidi/backends/linux/alsa.hpp>

  #if LIBREMIDI_ALSA_HAS_RAMWIDI
    #include <libremidi/backends/alsa_raw.hpp>
  #endif

  #if LIBREMIDI_ALSA_HAS_UMP
    #include <libremidi/backends/alsa_raw_ump.hpp>
    #include <libremidi/backends/alsa_seq_ump.hpp>
  #endif
#endif

#if defined(LIBREMIDI_JACK)
  #include <libremidi/backends/jack.hpp>
  #if defined(LIBREMIDI_HAS_JACK_GET_VERSION)
    #include <libremidi/backends/jack_ump.hpp>
  #endif
#endif

#if defined(LIBREMIDI_PIPEWIRE)
  #include <libremidi/backends/pipewire.hpp>
  #if defined(LIBREMIDI_PIPEWIRE_UMP)
    #include <libremidi/backends/pipewire_ump.hpp>
  #endif
#endif

#if defined(LIBREMIDI_COREMIDI)
  #include <libremidi/backends/coremidi.hpp>
  #include <libremidi/backends/coremidi_ump.hpp>
#endif

#if defined(LIBREMIDI_WINMM)
  #include <libremidi/backends/winmm.hpp>
#endif

#if defined(LIBREMIDI_KDMAPI)
  #include <libremidi/backends/kdmapi.hpp>
#endif

#if defined(LIBREMIDI_WINUWP)
  #include <libremidi/backends/winuwp.hpp>
#endif

#if defined(LIBREMIDI_WINMIDI)
  #include <libremidi/backends/winmidi.hpp>
#endif

#if defined(LIBREMIDI_EMSCRIPTEN)
  #include <libremidi/backends/emscripten.hpp>
#endif

#include <libremidi/backends/dummy.hpp>

#if defined(LIBREMIDI_KEYBOARD)
  #include <libremidi/backends/keyboard.hpp>
#endif

#if defined(LIBREMIDI_NETWORK)
  #include <libremidi/backends/network.hpp>
  #include <libremidi/backends/network_ump.hpp>
#endif

#include <libremidi/backends/rawio.hpp>
#include <libremidi/backends/rawio_ump.hpp>

#if defined(LIBREMIDI_ANDROID)
  #include <libremidi/backends/android/android.hpp>
#endif

NAMESPACE_LIBREMIDI
{
// The order here will control the order of the API search in
// the constructor.
template <typename unused, typename... Args>
constexpr auto make_tl(unused, Args...)
{
  return std::tuple<Args...>{};
}

namespace midi1
{
LIBREMIDI_STATIC constexpr auto available_backends = make_tl(
    0
#if defined(LIBREMIDI_ALSA)
    ,
    alsa_seq::backend{}
  #if LIBREMIDI_ALSA_HAS_RAMWIDI
    ,
    alsa_raw::backend{}
  #endif
#endif
#if defined(LIBREMIDI_COREMIDI)
    ,
    core_backend{}
#endif
#if defined(LIBREMIDI_WINMM)
    ,
    winmm_backend{}
#endif
#if defined(LIBREMIDI_KDMAPI)
    ,
    kdmapi_backend{}
#endif
#if defined(LIBREMIDI_WINUWP)
    ,
    winuwp_backend{}
#endif
#if defined(LIBREMIDI_EMSCRIPTEN)
    ,
    emscripten_backend{}
#endif
#if defined(LIBREMIDI_JACK)
    ,
    jack_backend{}
#endif
#if defined(LIBREMIDI_PIPEWIRE)
    ,
    pipewire::backend{}
#endif
#if defined(LIBREMIDI_KEYBOARD)
    ,
    kbd_backend{}
#endif
#if defined(LIBREMIDI_NETWORK)
    ,
    net::backend{}
#endif
#if defined(LIBREMIDI_ANDROID)
    ,
    android::backend{}
#endif
    ,
    rawio::backend{},
    dummy_backend{});

// There should always be at least one back-end.
static_assert(std::tuple_size_v<decltype(available_backends)> >= 1);

template <typename F>
void for_available_backends(F&& f)
{
  std::apply([&](auto&&... x) { ((x.available() && (f(x), true)), ...); }, available_backends);
}
}

namespace midi2
{
LIBREMIDI_STATIC constexpr auto available_backends = make_tl(
    0
#if defined(LIBREMIDI_ALSA) && LIBREMIDI_ALSA_HAS_UMP
    ,
    alsa_seq_ump::backend{}, alsa_raw_ump::backend{}
#endif
#if defined(LIBREMIDI_COREMIDI)
    ,
    coremidi_ump::backend{}
#endif
#if defined(LIBREMIDI_WINMIDI)
    ,
    winmidi::backend{}
#endif
#if defined(LIBREMIDI_NETWORK)
    ,
    net_ump::backend{}
#endif
#if defined(LIBREMIDI_JACK) && defined(LIBREMIDI_HAS_JACK_GET_VERSION)
    ,
    jack_ump::backend{}
#endif
#if defined(LIBREMIDI_PIPEWIRE_UMP)
    ,
    pipewire_ump::backend{}
#endif
    ,
    rawio_ump::backend{},
    dummy_backend{});

// There should always be at least one back-end.
static_assert(std::tuple_size_v<decltype(available_backends)> >= 1);

template <typename F>
void for_available_backends(F&& f)
{
  std::apply([&](auto&&... x) { ((x.available() && (f(x), true)), ...); }, available_backends);
}
}

namespace midi_any
{

template <typename F>
void for_compiled_backends(F&& f)
{
  auto visit = [&](auto&&... b) { (f(b), ...); };
  std::apply(visit, midi1::available_backends);
  std::apply(visit, midi2::available_backends);
}

template <typename F>
void for_compiled_backend(libremidi::API api, F&& f)
{
  auto visit = [&](auto&&... b) { return ((b.API == api && (f(b), true)) || ...); };
  if (!std::apply(visit, midi1::available_backends))
    std::apply(visit, midi2::available_backends);
}

// available() only tells whether a backend can be listed and selected. A
// backend with a service to start does it in ready(), which is only asked when
// an object is about to be created.
template <typename Backend>
bool ready() noexcept
{
  if constexpr (requires { requires std::is_convertible_v<decltype(Backend::ready()), bool>; })
    return Backend::ready();
  else
    return Backend::available();
}

inline bool is_available(libremidi::API api) noexcept
{
  bool ret = false;
  for_compiled_backend(api, [&](auto& b) { ret = b.available(); });
  return ret;
}

// Automatic selection tries backends in preference order. In particular, don't
// activate services for later candidates once one has succeeded.
template <typename Api, typename F, typename... Backends>
std::unique_ptr<Api> make_first_available(F make, const Backends&... backends)
{
  std::unique_ptr<Api> result;
  auto try_backend = [&](auto backend) {
    if (ready<decltype(backend)>())
    {
      // Not only std::exception: backends also throw their platform's types.
      try
      {
        result = make(backend);
      }
      catch (...)
      {
      }
    }
    return bool(result);
  };
  auto visit = [&](auto&&... b) { return (try_backend(b) || ...); };
  (void)(std::apply(visit, backends) || ...);
  return result;
}

void for_input_configuration(auto f, libremidi::input_api_configuration& api_conf)
{
  auto from_api = [&]<typename T>(T& /*backend*/) mutable {
    if (auto conf = get_if<typename T::midi_in_configuration>(&api_conf))
    {
      f(*conf);
      return true;
    }
    return false;
  };
  if (std::apply([&](auto&&... b) { return (from_api(b) || ...); }, midi1::available_backends))
    return;
  std::apply([&](auto&&... b) { return (from_api(b) || ...); }, midi2::available_backends);
}

void for_output_configuration(auto f, libremidi::output_api_configuration& api_conf)
{
  auto from_api = [&]<typename T>(T& /*backend*/) mutable {
    if (auto conf = get_if<typename T::midi_out_configuration>(&api_conf))
    {
      f(*conf);
      return true;
    }
    return false;
  };
  if (std::apply([&](auto&&... b) { return (from_api(b) || ...); }, midi1::available_backends))
    return;
  std::apply([&](auto&&... b) { return (from_api(b) || ...); }, midi2::available_backends);
}

void for_observer_configuration(auto f, libremidi::observer_api_configuration& api_conf)
{
  auto from_api = [&]<typename T>(T& /*backend*/) mutable {
    if (auto conf = get_if<typename T::midi_observer_configuration>(&api_conf))
    {
      f(*conf);
      return true;
    }
    return false;
  };
  if (std::apply([&](auto&&... b) { return (from_api(b) || ...); }, midi1::available_backends))
    return;
  std::apply([&](auto&&... b) { return (from_api(b) || ...); }, midi2::available_backends);
}
}
}
