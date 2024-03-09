#include "utils.hpp"

#include <libremidi/libremidi.hpp>

#include <cstdlib>
#include <iostream>
#include <map>

#if defined(__APPLE__)
  #include <CoreFoundation/CoreFoundation.h>
#endif

int main()
try
{
  std::vector<libremidi::observer> observers;
  for (auto api : libremidi::available_apis())
  {
    std::string_view api_name = libremidi::get_api_display_name(api);

    std::cout << "Displaying ports for: " << api_name << std::endl;
    libremidi::observer_configuration cbs;
    cbs.input_added = [=](const libremidi::input_port& p) {
      std::cout << api_name << " : input added " << p << "\n";
    };
    cbs.input_removed = [=](const libremidi::input_port& p) {
      std::cout << api_name << " : input removed " << p << "\n";
    };
    cbs.output_added = [=](const libremidi::output_port& p) {
      std::cout << api_name << " : output added " << p << "\n";
    };
    cbs.output_removed = [=](const libremidi::output_port& p) {
      std::cout << api_name << " : output removed " << p << "\n";
    };
    observers.emplace_back(cbs, libremidi::observer_configuration_for(api));
    std::cout << "\n" << std::endl;
  }

  std::cout << "... waiting for hotplug events ...\n";

#if defined(__APPLE__)
  // On macOS, observation can *only* be done in the main thread
  // with an active CFRunLoop.
  CFRunLoopRun();
#else
  getchar();
#endif
  return 0;
}
catch (const std::exception& error)
{
  std::cerr << error.what() << std::endl;
  return EXIT_FAILURE;
}
