// SPDX-License-Identifier: BSL-1.0
//
// Regression test for the PipeWire MIDI backends' port lifecycle.
//
// The filter used to be connected in the constructor and the local port added
// in open_port(). With node.always-process the daemon schedules the node as
// soon as it is exported, so process() ran against a missing port token: an
// unopened midi_in/midi_out crashed within milliseconds, and close_port()
// freed the port under a live filter (pw_filter_remove_port does not
// synchronize with the data loop). Now the filter connects only once the port
// exists and is disconnected before the token is cleared.
//
// Requires a running PipeWire daemon; skips (exit 0) if none is reachable.

#include <libremidi/backends/linux/pipewire/context.hpp>
#include <libremidi/backends/pipewire.hpp>
#include <libremidi/libremidi.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace lpw = libremidi::pipewire;
using api = libremidi::pipewire::backend;

static void arm_watchdog(int seconds)
{
  std::thread(
      [seconds]
      {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        std::fprintf(stderr, "FAIL: watchdog timeout (%ds) - likely deadlock\n", seconds);
        std::fflush(stderr);
        std::_Exit(EXIT_FAILURE);
      })
      .detach();
}

static libremidi::input_configuration in_conf()
{
  libremidi::input_configuration c;
  c.on_message = [](const libremidi::message&) {};
  return c;
}

int main()
{
  auto& pw = lpw::load();
  if (!pw.thread_available || !pw.filter_available)
  {
    std::printf("libpipewire thread-loop/filter not available; skipping\n");
    return 0;
  }

  auto ctx = lpw::shared_context();
  if (!ctx || !ctx->ok())
  {
    std::printf("cannot connect to pipewire daemon; skipping\n");
    return 0;
  }

  arm_watchdog(120);
  int failures = 0;

  // 1. Constructed but never opened: process() must not fire against a
  //    missing port. This used to crash within a few graph cycles.
  {
    libremidi::midi_in in{in_conf(), api::midi_in_configuration{}};
    libremidi::midi_out out{libremidi::output_configuration{}, api::midi_out_configuration{}};
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::printf("ok: unopened midi_in/midi_out idled for 500 ms\n");
  }

  // 2. Repeated open/close on one object, with the close racing the data loop
  //    at various phases of the cycle.
  {
    libremidi::midi_in in{in_conf(), api::midi_in_configuration{}};
    libremidi::midi_out out{libremidi::output_configuration{}, api::midi_out_configuration{}};
    for (int i = 0; i < 100; ++i)
    {
      if (in.open_virtual_port("lifecycle-in") != stdx::error{})
      {
        std::printf("FAIL: midi_in open_virtual_port failed at cycle %d\n", i);
        ++failures;
      }
      if (out.open_virtual_port("lifecycle-out") != stdx::error{})
      {
        std::printf("FAIL: midi_out open_virtual_port failed at cycle %d\n", i);
        ++failures;
      }
      const unsigned char note[3] = {0x90, 60, 100};
      out.send_message(note, 3);
      std::this_thread::sleep_for(std::chrono::milliseconds(i % 5));
      in.close_port();
      out.close_port();
    }
    std::printf("ok: 100 open/close cycles on midi_in and midi_out\n");
  }

  // 3. Whole objects created, opened and destroyed in a loop, like a host
  //    rebuilding its device list.
  for (int i = 0; i < 30; ++i)
  {
    libremidi::midi_in in{in_conf(), api::midi_in_configuration{}};
    if (in.open_virtual_port("lifecycle-tmp") != stdx::error{})
    {
      std::printf("FAIL: open_virtual_port failed in construct/destroy cycle %d\n", i);
      ++failures;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(i % 3));
  }
  std::printf("ok: 30 construct/open/destroy cycles\n");

  if (ctx->state() != lpw::connection_state::connected)
  {
    std::printf("FAIL: shared context no longer connected (state %d)\n", (int)ctx->state());
    ++failures;
  }

  if (failures)
  {
    std::printf("FAIL: pipewire_midi_port_lifecycle (%d failures)\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("PASS: pipewire_midi_port_lifecycle\n");
  return 0;
}
