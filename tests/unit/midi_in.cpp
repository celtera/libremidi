#include "../include_catch.hpp"

#include <libremidi/libremidi.hpp>

#include <chrono>
#include <thread>

#if defined(LIBREMIDI_JACK)
  #include <jack/jack.h>

#include <libremidi/backends/jack/config.hpp>
TEST_CASE("poly aftertouch", "[midi_in]")
{
#if !defined(LIBREMIDI_CI)
  std::vector<libremidi::message> queue;
  std::mutex qmtx;

  libremidi::midi_out midi_out{libremidi::API::UNIX_JACK, "libremidi-test-out"};
  midi_out.open_port();

  libremidi::midi_in midi{
      libremidi::input_configuration{
          .on_message
          = [&](libremidi::message&& msg) {
    std::lock_guard _{qmtx};
    queue.push_back(std::move(msg));
          }},
      libremidi::jack_input_configuration{.client_name = "libremidi-test"}};

  midi.open_port();

  jack_options_t opt = JackNullOption;
  jack_status_t status;
  auto jack_client = jack_client_open("libremidi-tester", opt, &status);
  jack_activate(jack_client);

  jack_connect(
      jack_client, "libremidi-test-out:libremidi Output", "libremidi-test:libremidi Input");

  // Flush potentially initial messages
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  {
    std::lock_guard _{qmtx};
    queue.clear();
  }

  // Send a message
  midi_out.send_message(libremidi::message::poly_pressure(0, 60, 100));
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Check that we receive it correctly
  {
    std::lock_guard _{qmtx};
    REQUIRE(queue.size() == 1);
    libremidi::message mess = queue.back();
    REQUIRE(mess.bytes == libremidi::message::poly_pressure(0, 60, 100).bytes);
  }
#endif
}
#endif
