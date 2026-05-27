// Embed libremidi MIDI in a host that owns its pipewire setup; the
// host passes raw pointers via the borrow fields and libremidi
// adopts them without taking ownership.

#include "utils.hpp"

#include <libremidi/backends/linux/pipewire/loader.hpp>
#include <libremidi/configurations.hpp>
#include <libremidi/libremidi.hpp>

#include <pipewire/context.h>
#include <pipewire/core.h>
#include <pipewire/pipewire.h>
#include <pipewire/thread-loop.h>

#include <chrono>
#include <thread>

auto& pw = libremidi::pipewire::load();

struct my_app
{
  pw_thread_loop* tl{};
  pw_context* ctx{};
  pw_core* core{};

  std::optional<libremidi::observer> observer;
  std::vector<libremidi::midi_in> midiin;
  std::vector<libremidi::midi_out> midiout;

  my_app()
  {
    tl = pw.thread_loop_new("my-app", nullptr);
    if (!tl)
      throw std::runtime_error("thread_loop_new failed");

    auto* lp = pw.thread_loop_get_loop(tl);
    ctx = pw.context_new(lp, nullptr, 0);
    if (!ctx)
      throw std::runtime_error("context_new failed");

    core = pw.context_connect(ctx, nullptr, 0);
    if (!core)
      throw std::runtime_error("context_connect failed");

    if (pw.thread_loop_start(tl) < 0)
      throw std::runtime_error("thread_loop_start failed");

    auto callback = [&](int port, const libremidi::message& message) {
      std::cerr << port << ": " << message << '\n';
      midiout[port].send_message(message);
    };

    libremidi::pipewire_observer_configuration api_observer_config{};
    api_observer_config.thread_loop = tl;
    api_observer_config.core = core;

    observer
        = libremidi::observer{libremidi::observer_configuration{}, api_observer_config};

    libremidi::pipewire_input_configuration api_input_config{};
    api_input_config.thread_loop = tl;
    api_input_config.core = core;

    libremidi::pipewire_output_configuration api_output_config{};
    api_output_config.thread_loop = tl;
    api_output_config.core = core;

    for (int i = 0; i < 16; i++)
    {
      midiin.emplace_back(
          libremidi::input_configuration{
              .on_message = [=](const libremidi::message& msg) { callback(i, msg); }},
          api_input_config);
      midiin[i].open_virtual_port("Input: " + std::to_string(i));

      midiout.emplace_back(libremidi::output_configuration{}, api_output_config);
      midiout[i].open_virtual_port("Output: " + std::to_string(i));
    }
  }

  ~my_app()
  {
    midiin.clear();
    midiout.clear();
    observer.reset();

    if (tl)
      pw.thread_loop_stop(tl);
    if (core)
      pw.core_disconnect(core);
    if (ctx)
      pw.context_destroy(ctx);
    if (tl)
      pw.thread_loop_destroy(tl);
  }

  void run()
  {
    std::this_thread::sleep_for(std::chrono::seconds(60));
  }
};

int main(int argc, char** argv)
{
  pw.init(&argc, &argv);
  try
  {
    my_app app{};
    app.run();
  }
  catch (const std::exception& e)
  {
    std::cerr << "error: " << e.what() << "\n";
  }
  pw.deinit();
}
