#pragma once
#include <tuple>

#if !__has_include(<weak_libjack.h>) && !__has_include(<jack/jack.h>)
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
#endif

#if defined(LIBREMIDI_PIPEWIRE)
  #include <libremidi/backends/pipewire.hpp>
#endif

#if defined(LIBREMIDI_COREMIDI)
  #include <libremidi/backends/coremidi.hpp>
  #include <libremidi/backends/coremidi_ump.hpp>
#endif

#if defined(LIBREMIDI_WINMM)
  #include <libremidi/backends/winmm.hpp>
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

namespace libremidi
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
static constexpr auto available_backends = make_tl(
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
    ,
    dummy_backend{});

// There should always be at least one back-end.
static_assert(std::tuple_size_v<decltype(available_backends)> >= 1);

template <typename F>
auto for_all_backends(F&& f)
{
  std::apply([&](auto&&... x) { ((x.available() && (f(x), true)), ...); }, available_backends);
}

template <typename F>
auto for_backend(libremidi::API api, F&& f)
{
  static constexpr auto is_api = [](auto& backend, libremidi::API api) {
    return backend.available() && backend.API == api;
  };
  std::apply([&](auto&&... b) { ((is_api(b, api) && (f(b), true)) || ...); }, available_backends);
}
}

namespace midi2
{
static constexpr auto available_backends = make_tl(
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

namespace midi_any
{

template <typename F>
auto for_all_backends(F&& f)
{
  midi1::for_all_backends(f);
  midi2::for_all_backends(f);
}

template <typename F>
auto for_backend(libremidi::API api, F&& f)
{
  midi1::for_backend(api, f);
  midi2::for_backend(api, f);
}

}
}
