#include <libremidi/libremidi.hpp>

#include <windows.h>
#include <winrt/Windows.Devices.Midi2.h>

#include <algorithm>
#include <cstdio>
#include <type_traits>

int main()
{
  using namespace libremidi;
  namespace sdk = winrt::Windows::Devices::Midi2;
  static_assert(std::is_same_v<decltype(winmidi::input_configuration::context), sdk::MidiSession*>);
  static_assert(std::is_same_v<decltype(winmidi::output_configuration::context), sdk::MidiSession*>);

  winrt::init_apartment();
  OSVERSIONINFOW os{};
  os.dwOSVersionInfoSize = sizeof(os);
  const auto get_version = reinterpret_cast<LONG (WINAPI*)(OSVERSIONINFOW*)>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
  if (!get_version || get_version(&os) != 0)
    return 1;

  // Query the SDK independently of libremidi, including on supported machines
  // where the API is not installed. This test never opens a device.
  const auto factory = winrt::try_get_activation_factory<sdk::MidiApi, sdk::IMidiApiStatics>();
  const bool usable = os.dwBuildNumber >= 26200 && factory && factory.EnsureServiceAvailable();
  std::printf("Windows %lu, MidiApi factory: %d, usable: %d\n", os.dwBuildNumber, !!factory, usable);
  constexpr auto requested = API::WINDOWS_MIDI_SERVICES;
  const auto apis = available_ump_apis();
  if ((std::find(apis.begin(), apis.end(), requested) != apis.end()) != usable)
    return 2;

  int errors = 0;
  const auto report = [&](auto&&...) { ++errors; };
  ump_input_configuration ic{.on_message = [](ump&&) {}};
  ic.on_error = report;
  output_configuration oc;
  oc.on_error = report;
  observer_configuration wc;
  wc.on_error = report;
  const auto expected = usable ? requested : API::DUMMY;
  return !(midi_in{ic, requested}.get_current_api() == expected
           && midi_in{ic, winmidi::input_configuration{}}.get_current_api() == expected
           && midi_out{oc, requested}.get_current_api() == expected
           && midi_out{oc, winmidi::output_configuration{}}.get_current_api() == expected
           && observer{wc, requested}.get_current_api() == expected
           && observer{wc, winmidi::observer_configuration{}}.get_current_api() == expected
           && midi_in{ic, midi_in_configuration_for(requested)}.get_current_api() == expected
           && midi_out{oc, midi_out_configuration_for(requested)}.get_current_api() == expected
           && observer{wc, observer_configuration_for(requested)}.get_current_api() == expected
           && errors == (usable ? 0 : 9));
}
