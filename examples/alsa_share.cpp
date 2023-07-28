#include "utils.hpp"

#include <libremidi/configurations.hpp>
#include <libremidi/libremidi.hpp>

#include <alsa/asoundlib.h>

#include <poll.h>

int main()
{
  std::vector<libremidi::midi_in> midiin;
  std::vector<libremidi::midi_out> midiout;

  auto callback = [&](int port, const libremidi::message& msg) {
    std::cout << msg << std::endl;
    midiout[port].send_message(msg);
  };

  // Create an alsa client which will be shared across objects
  snd_seq_t* clt{};
  if (int err = snd_seq_open(&clt, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK); err < 0)
    return err;
  libremidi::unique_handle<snd_seq_t, snd_seq_close> handle{clt};

  snd_seq_set_client_name(clt, "My MIDI app");

  // We share the polling across the midi-ins: see poll_share.cpp for more details
  std::vector<std::function<int(std::span<pollfd>)>> callbacks;
  std::vector<pollfd> fds;

  auto register_fds = [&](const libremidi::manual_poll_parameters& params) {
    fds.insert(fds.end(), params.fds.begin(), params.fds.end());
    callbacks.push_back(params.callback);
    return true;
  };
  auto api_input_config
      = libremidi::alsa_seq::input_configuration{.context = clt, .manual_poll = register_fds};
  auto api_output_config = libremidi::alsa_seq::output_configuration{.context = clt};

  // Create 16 inputs and 16 outputs
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

  // Poll
  for (;;)
  {
    int err = poll(fds.data(), fds.size(), -1);
    if (err < 0)
      return err;

    // Look for who's ready
    for (int i = 0; i < fds.size(); i++)
    {
      if (fds[i].revents & POLLIN)
      {
        auto err = callbacks[i]({fds.data() + i, 1});
        if (err < 0 && err != -EAGAIN)
          return -err;
      }
    }
  }
}
