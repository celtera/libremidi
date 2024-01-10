#include "../include_catch.hpp"

#include <libremidi/libremidi.hpp>

#include <chrono>
#include <thread>

#if __has_include(<jack/jack.h>)
  #include <jack/jack.h>
#endif

#include <libremidi/backends/jack/config.hpp>
TEST_CASE("poly aftertouch", "[midi_in]")
{
#if __has_include(<jack/jack.h>)
  #if !defined(LIBREMIDI_CI)
  std::vector<libremidi::message> queue;
  std::mutex qmtx;

  libremidi::midi_out midi_out{
      {}, libremidi::jack_output_configuration{.client_name = "libremidi-test-out"}};
  midi_out.open_virtual_port("port");

  libremidi::midi_in midi{
      libremidi::input_configuration{
          .on_message
          = [&](libremidi::message&& msg) {
    std::lock_guard _{qmtx};
    queue.push_back(std::move(msg));
          }},
      libremidi::jack_input_configuration{.client_name = "libremidi-test"}};
  midi.open_virtual_port("port");

  jack_options_t opt = JackNullOption;
  jack_status_t status;
  auto jack_client = jack_client_open("libremidi-tester", opt, &status);
  int ret = jack_activate(jack_client);
  REQUIRE(ret == 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  ret = jack_connect(jack_client, "libremidi-test-out:port", "libremidi-test:port");
  REQUIRE(ret == 0);

  // Flush potentially initial messages
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  {
    std::lock_guard _{qmtx};
    queue.clear();
  }

  // Send a message
  midi_out.send_message(libremidi::channel_events::poly_pressure(0, 60, 100));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Check that we receive it correctly
  {
    std::lock_guard _{qmtx};
    REQUIRE(queue.size() == 1);
    libremidi::message mess = queue.back();
    REQUIRE(mess.bytes == libremidi::channel_events::poly_pressure(0, 60, 100).bytes);
  }
#endif
#endif
}
