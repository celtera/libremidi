#include <libremidi/libremidi.hpp>
#include <libremidi/protocols/remote_control.hpp>

#if defined(_WIN32) && __has_include(<winrt/base.h>)
  #include <winrt/base.h>
#endif

#if __has_include(<magic_enum_all.hpp>)
  #include <magic_enum_all.hpp>
#else
  #include <iomanip>
  #include <iostream>
  #include <sstream>
namespace magic_enum
{
std::string enum_name(auto cmd)
{
  std::stringstream ss;
  ss << "0x" << std::setbase(16) << static_cast<uint32_t>(cmd);
  return ss.str();
}
}
#endif

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <thread>

// for debugging/testing purposes
const bool observe_incoming_messages = false;
const bool observe_outgoing_messages = false;

void help(char * argv0){

  printf(
      "Usage: %s <portname> [<device-type>]\n"
      "\nOptions:\n"
      "  <portname>     Also see example midiprobe for currently connected MIDI ports and their names\n"
      "  <device-type>  Type of device (in hex) to control, will not work if wrong (default = 14)\n"
      "                 05 (mackie hui)\n"
      "                 10 (logic control)\n"
      "                 11 (logic control xt)\n"
      "                 14 (mackie control)\n"
      "                 15 (mackie control xt)\n"
      , argv0);
}

int main(int argc, char * argv[])
{
  if (argc < 2 || 3 < argc){
    help(argv[0]);
    return EXIT_FAILURE;
  }

  char * port_name = argv[1];
  libremidi::remote_control_protocol::device_type device_type = libremidi::remote_control_protocol::device_type::mackie_control;

  if (argc == 3){
    int i = 0;
    if (!sscanf(argv[2], "%02X", &i)){
      std::cerr << "Error: argument <device-type> invalid" << std::endl;
      return EXIT_FAILURE;
    } else if (!libremidi::remote_control_protocol::device_type_is_valid(i)){
      std::cerr << "Error: argument <device-type> not a recognized type" << std::endl;
      help(argv[0]);
      return EXIT_FAILURE;
    }
    device_type = static_cast<libremidi::remote_control_protocol::device_type>(i);
  }

#if defined(_WIN32) && __has_include(<winrt/base.h>)
  winrt::init_apartment();
#endif

  auto api = libremidi::API::UNSPECIFIED;
  libremidi::observer observer{{.track_any = true}, api};
  if (observer.get_input_ports().empty())
    return EXIT_FAILURE;
  if (observer.get_output_ports().empty())
    return EXIT_FAILURE;

  libremidi::input_port ip;
  libremidi::output_port op;

  // Tested with https://github.com/NicoG60/TouchMCU
  for (auto& p : observer.get_input_ports())
    if (p.port_name == port_name)
      ip = p;
  for (auto& p : observer.get_output_ports())
    if (p.port_name == port_name)
      op = p;

  if (ip.port_name.empty() || op.port_name.empty())
  {
    std::cerr << "No device found !";
    return EXIT_FAILURE;
  }

  // Create the midi out port
  libremidi::midi_out midi_out{{}, api};

  // Set-up the remote control API.
  // Here we only do some logging, this is where commands sqall be handled.
  libremidi::remote_control_processor rcp{{
      .device_type = device_type,
      .midi_out = [&](libremidi::message&& message) {

        if (observe_outgoing_messages){
          fprintf(stderr, "MIDI OUT (%zu) ", message.size());
          for(int i = 0; i < message.size(); i++)
            fprintf(stderr, "%02x ", message.bytes[i]);
          fprintf(stderr, "\n");
        }

        midi_out.send_message(message);
      },
      .on_connected = [](libremidi::remote_control_protocol::device_type deviceType){
        fprintf(stderr, "connected: device type: %02X\n", libremidi::to_underlying(deviceType));
      },
      .on_command = [](libremidi::remote_control_protocol::mixer_command cmd, bool pressed) {
        std::cerr << "command: " << magic_enum::enum_name(cmd) << " -> " << (pressed ? "pressed" : "released") << "\n";
      },
      .on_control = [](libremidi::remote_control_protocol::mixer_control ctl, int v) {
        std::cerr << "control: " << magic_enum::enum_name(ctl) << " -> " << v << "\n";
      },
      .on_fader = [&](uint8_t fader, uint16_t v) {
        std::cerr << "fader: " << (int)fader << " -> " << v << "\n";

        // NOTE
        // device are generally do not change their state by themselves, so unless the fader level
        // is confirmed (or sent to the device) the fader may return to the last sent state
        // you might also want to rely on the fader_touched released command (although the touch
        // detection doesn't seem to always react)
//        rcp.fader(fader, v);
      },
      .on_version = [&](libremidi::remote_control_protocol::device_type device_type, std::span<const uint8_t,5> version){
        std::cerr << "version reply: ";
        for (int i = 1; i < version.size(); i++){
          fprintf(stderr, "%02X", version[i]);
        }
        std::cerr << std::endl;
      }
  }};

  // Initialize the midi in port
  libremidi::midi_in midi_in{
      {
          // Note, it's important to allow for sysex to pass through, otherwise
          // not all messages from the device come through
          .ignore_sysex = false,
          .on_message = [&](const libremidi::message& message) {

            if (observe_incoming_messages){
              fprintf(stderr, "MIDI IN (%zu) ", message.size());
              for(int i = 0; i < message.size(); i++)
              fprintf(stderr, "%02x ", message.bytes[i]);
              fprintf(stderr, "\n");
            }

            rcp.on_midi(message);
          }
    },
api
  };

  // Open the ports
  if (auto err = midi_in.open_port(ip); err != stdx::error{})
    err.throw_exception();

  if (auto err = midi_out.open_port(op); err != stdx::error{})
    err.throw_exception();

  // Start communication
  rcp.start();

  // Blast messages :)
  using proto = libremidi::remote_control_protocol;

  for (unsigned i = 0;;i++)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::time_t result = std::time(nullptr);
    auto ctime = std::localtime(&result);
    rcp.update_timecode(ctime->tm_hour, ctime->tm_min, ctime->tm_sec, 0);

    rcp.update_lcd(std::string(1, '\0' + i % 127), i % 112);

    // light up buttons :)
    proto::mixer_command mc[4] = {
        static_cast<proto::mixer_command>(libremidi::to_underlying(proto::mixer_command::rec_0) + (i%8)),
        static_cast<proto::mixer_command>(libremidi::to_underlying(proto::mixer_command::solo_0) + (i%8)),
        static_cast<proto::mixer_command>(libremidi::to_underlying(proto::mixer_command::mute_0) + (i%8)),
        static_cast<proto::mixer_command>(libremidi::to_underlying(proto::mixer_command::sel_0) + (i%8)),
    };
    for (int j = 0; j < sizeof(mc); j++)
      rcp.command(mc[j], i%3);

    rcp.fader(i % 8, (200 * i) % 16384);

  }

  return EXIT_SUCCESS;
}
