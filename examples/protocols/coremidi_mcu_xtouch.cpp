#include "../utils.hpp"

#include <libremidi/protocols/devices/xtouch.hpp>

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

#if defined(__APPLE__)
  #include <CoreMIDI/CoreMIDI.h>
  #include <CoreFoundation/CoreFoundation.h>
#else
#error This example was written for CoreMIDI, so you will have to adapt it for your own system
#endif

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <thread>
#include <signal.h>

// for debugging/testing purposes
const bool observe_incoming_messages = false;
const bool observe_outgoing_messages = false;


struct my_xtouch_app
{
  using mcu = libremidi::remote_control_protocol;
  using xt = libremidi::remote_control_protocol_xtouch;

  static constexpr auto api = libremidi::API::COREMIDI;
  static constexpr char kDeviceName[] = "X-TOUCH_INT";
  static constexpr mcu::device_type kDeviceType = mcu::device_type::mackie_control_xt;
  static constexpr xt::model_type kModelType = xt::model_type::extender;

  enum class State : uint8_t
  {
    Off, Starting, Running, Stopping
  };

  struct vpot_st
  {
    uint8_t index;
    bool state = false;
    xt::led_ring_mode mode = xt::led_ring_mode::mode_0;
    uint8_t value = 0;
    void change_value(int change){
      if (change > 0 && value < xt::vpot_max_value[(uint8_t)mode])
        value++;
      else if (change < 0 && value > xt::vpot_min_value[(uint8_t)mode])
        value--;
    }
    void toggle_led_state(){
      state = !state;
    }
  };


  State state = State::Off;

  MIDIClientRef handle;

  libremidi::midi_out * midi_out;
  libremidi::midi_in * midi_in;

  libremidi::remote_control_client_processor_xtouch * xtouch;

  struct {
    bool rec[8] = {false,false,false,false,false,false,false,false};
    bool mute[8] = {false,false,false,false,false,false,false,false};
  } buttons;

  struct vpot_st vpots[8] = {
      {.index = 0, .mode = xt::led_ring_mode::mode_0, .value=1, .state = false},
      {.index = 1, .mode = xt::led_ring_mode::mode_1, .value=1, .state = false},
      {.index = 2, .mode = xt::led_ring_mode::mode_2, .value=1, .state = true},
      {.index = 3, .mode = xt::led_ring_mode::mode_3, .value=1, .state = true},
      {.index = 4, .mode = xt::led_ring_mode::mode_0, .value = 6},
      {.index = 5, .mode = xt::led_ring_mode::mode_1, .value = 6},
      {.index = 6, .mode = xt::led_ring_mode::mode_2, .value = 6},
      {.index = 7, .mode = xt::led_ring_mode::mode_3, .value = 6}
  };

      my_xtouch_app()
  {
    std::cout << "Creating MIDIClient.." << std::endl;

    auto res = MIDIClientCreate(CFSTR("My App"), 0, 0, &handle);
    if (res != noErr)
      throw std::runtime_error("Could not start CoreMIDI");


  }

  ~my_xtouch_app()
  {
    if (xtouch)
      delete xtouch;
    if (midi_in)
      delete midi_in;
    if (midi_out)
      delete midi_out;

    std::cout << "Disposing of MIDIClient" << std::endl;
    MIDIClientDispose(handle);
  }

  void _update_buttons(int i = -1)
  {
    assert(xtouch);

    if (i == -1){
      xtouch->command((xt::mixer_command)((int)(xt::mixer_command::rec_0) + i), buttons.rec[i]);
      xtouch->command((xt::mixer_command)((int)(xt::mixer_command::mute_0) + i), buttons.mute[i]);
    }
    else {
        for (int i = 0; i < 8; i++){
        xtouch->command((xt::mixer_command)((int)(xt::mixer_command::rec_0) + i), buttons.rec[i]);
        xtouch->command((xt::mixer_command)((int)(xt::mixer_command::mute_0) + i), buttons.mute[i]);
        }
    }
  }

  void _update_vpots(int i = -1)
  {
    assert(xtouch);

    if (i != -1){
        xtouch->vpot(vpots[i].index, vpots[i].state, vpots[i].mode, vpots[i].value);
    } else {
      for (i = 0; i < 8; i++){
        xtouch->vpot(vpots[i].index, vpots[i].state, vpots[i].mode, vpots[i].value);
      }
    }
  }

  void _start_impl()
  {
    std::cout << "Starting app.." << std::endl;
    state = State::Starting;


#if defined(_WIN32) && __has_include(<winrt/base.h>)
    winrt::init_apartment();
#endif


    // find devices
    libremidi::observer observer{{.track_any = true}, api};
    if (observer.get_input_ports().empty())
      return;
    if (observer.get_output_ports().empty())
      return;

    // scan for wanted device
    libremidi::input_port ip;
    libremidi::output_port op;

    // Tested with https://github.com/NicoG60/TouchMCU
    for (auto& p : observer.get_input_ports())
      if (p.port_name == kDeviceName)
        ip = p;
    for (auto& p : observer.get_output_ports())
      if (p.port_name == kDeviceName)
        op = p;

    if (ip.port_name.empty() || op.port_name.empty())
    {
      throw std::runtime_error("No device found");
    }

    // Create the midi out port
    midi_out = new libremidi::midi_out {{}, api};
    midi_in = new libremidi::midi_in {
        {

            .ignore_sysex = false,
            .on_message = [&](const libremidi::message& message) {

              if (observe_incoming_messages){
                fprintf(stderr, "MIDI IN (%zu) ", message.size());
                for(int i = 0; i < message.size(); i++)
                  fprintf(stderr, "%02x ", message.bytes[i]);
                fprintf(stderr, "\n");
              }

              xtouch->on_midi(message);
            }
        },
        api
    };

    // Set-up the remote control API.
    // Here we only do some logging, this is where commands sqall be handled.
//    struct libremidi::remote_control_client_processor_xtouch::configuration conf = ;

    xtouch = new libremidi::remote_control_client_processor_xtouch
    {
      {
        {.device_type = kDeviceType,

         .midi_out =
             [&](libremidi::message&& message) {
          if (observe_outgoing_messages)
          {
            fprintf(stderr, "MIDI OUT (%zu) ", message.size());
            for (int i = 0; i < message.size(); i++)
              fprintf(stderr, "%02x ", message.bytes[i]);
            fprintf(stderr, "\n");
          }

          midi_out->send_message(message);
         },

         .on_command =
             [&](mcu::mixer_command cmd, bool pressed) {
          std::cerr << "command: " << magic_enum::enum_name(cmd) << " -> "
                    << (pressed ? "pressed" : "released") << "\n";

          auto type = mcu::which_mixer_command_type(cmd);
          auto index = mcu::which_mixer_command_index(type, cmd);
          switch (type)
          {
            case mcu::mixer_command::type_rec:
              if (!pressed)
                return;
              // toggle button state
              buttons.rec[index] = !buttons.rec[index];
              _update_buttons(index);
              break;

            case mcu::mixer_command::type_mute:
              if (!pressed)
                return;
              // toggle button state
              buttons.mute[index] = !buttons.mute[index];
              _update_buttons(index);
              break;

            case mcu::mixer_command::type_solo:
            case mcu::mixer_command::type_sel:
              // just light up button while being pressed
              xtouch->command(cmd, pressed);
              break;

            case mcu::mixer_command::type_vpot_click:
              std::cerr << "-> vpot click " << index << std::endl;
              vpots[index].toggle_led_state();
              _update_vpots(index);
              break;

            case mcu::mixer_command::type_fader_touched:
              std::cerr << "-> fader touched " << index << std::endl;
              break;

            case mcu::mixer_command::type_f:
              std::cerr << "-> F" << index << std::endl;
              break;

            case mcu::mixer_command::type_channel:
              switch (cmd)
              {
                case mcu::mixer_command::bank_left:
                  std::cerr << "-> bank left" << std::endl;
                  break;
                case mcu::mixer_command::bank_right:
                  std::cerr << "-> bank right" << std::endl;
                  break;
                case mcu::mixer_command::channel_left:
                  std::cerr << "-> channel left" << std::endl;
                  break;
                case mcu::mixer_command::channel_right:
                  std::cerr << "-> channel right" << std::endl;
                  break;
                default:
                  break;
              }
              break;

            case mcu::mixer_command::type_transport:
              switch (cmd)
              {
                case mcu::mixer_command::stop:
                  std::cerr << " -> stop" << std::endl;
                  break;
                case mcu::mixer_command::play:
                  std::cerr << " -> play" << std::endl;
                  break;
                default:
                  std::cerr << " -> some transport function" << std::endl;
                  break;
              }
              break;

            case mcu::mixer_command::type_leds:
            case mcu::mixer_command::type_assign:
            case mcu::mixer_command::type_meta:
            case mcu::mixer_command::type_control:
            case mcu::mixer_command::type_page:
            case mcu::mixer_command::type_user:
            case mcu::mixer_command::type_other:
            default:
              break;
          }
         },
            .on_control
            = [&](mcu::mixer_control ctl, int v) {
          std::cerr << "control: " << magic_enum::enum_name(ctl) << " -> " << v << "\n";

          auto type = mcu::which_mixer_control_type(ctl);
          auto index = mcu::which_mixer_control_index(type, ctl);
          auto val = 0;
          switch (type)
          {
            case mcu::mixer_control::type_vpot_rotation:
              val = mcu::relative_midi_to_value(v);
              std::cerr << "-> vpot " << index << " relative value= " << val << std::endl;

              vpots[index].change_value(val);

              _update_vpots(index);
              break;

            default:
              break;
          }
            },
            .on_fader
            = [&](uint8_t fader, uint16_t v) {
          std::cerr << "fader: " << (int)fader << " -> " << v << "\n";
          // echo back
          xtouch->fader(fader, v);
            },
            .on_version
            = [&](libremidi::remote_control_protocol::device_type device_type,
                  std::span<const uint8_t, 5> version) {
          std::cerr << "version reply: ";
          for (int i = 1; i < version.size(); i++)
          {
            fprintf(stderr, "%02X", version[i]);
          }
          std::cerr << std::endl;
            }},

            .model_type = kModelType,
      }
    };

    // Open the ports
    if (auto err = midi_in->open_port(ip); err != stdx::error{})
      err.throw_exception();

    if (auto err = midi_out->open_port(op); err != stdx::error{})
      err.throw_exception();


    state = State::Running;

    // Start communication
    xtouch->start();

    // reset interface
    _update_buttons();
    _update_vpots();

    // Blast messages :)

    xt::channel_color color_palette[8] = {xt::channel_color::black,   xt::channel_color::red,  xt::channel_color::yellow,
           xt::channel_color::green,   xt::channel_color::cyan, xt::channel_color::blue,
           xt::channel_color::magenta, xt::channel_color::white
    };
    for (uint8_t i = 0; i < 8; i++){
      xtouch->set_channel_color(i, xt::channel_color::black);
    }
    xtouch->update_channel_colors();

    std::string labels1[8] = {"1", "21", "321", "4321", "54321", "654321", "7654321", "87654321"};
    std::string labels2[8] = {"ch1", "ch2", "ch3", "ch4", "ch5", "ch6", "ch7", "ch8"};

    for (uint8_t i = 0; state == State::Running;i++)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      std::time_t result = std::time(nullptr);
      auto ctime = std::localtime(&result);
      xtouch->update_timecode(ctime->tm_hour, ctime->tm_min, ctime->tm_sec, 0);

      xtouch->update_lcd_ch_line(labels1[ (i/8 + i%8) % 8 ], i % mcu::channel_count, 0);
      xtouch->update_lcd_ch_line(labels2[ (i/8 + i%8) % 8 ], i % mcu::channel_count, 1);

      xtouch->update_channel_color(i % mcu::channel_count, color_palette[(i/8 + i%8) % 8]);

      xtouch->channel_meter(i % mcu::channel_count, i % mcu::channel_meter_max_value);

      xtouch->fader(i % mcu::channel_count, (200 * i) % 16384);
    }

    state = State::Off;
  }

  void start(){
    try {
      _start_impl();
    } catch (std::exception e){
      state = State::Off;
      throw e;
    }
  }

  void stop(){
    state = State::Stopping;
  }

};

my_xtouch_app * my_app_instance = nullptr;

void SignalHandler(int signal)
{
  if (!my_app_instance)
    exit(EXIT_FAILURE);

  if (my_app_instance->state == my_xtouch_app::State::Running){
    // Attempt to gracefully stop process.
    my_app_instance->stop();
    return;
  }

  // Force quit if necessary
  exit(EXIT_FAILURE);
}

void setupSignalHandler(){
  // sigintTicks = 0;
  signal(SIGINT, SignalHandler);
}

int main()
{
  setupSignalHandler();

  my_app_instance = new my_xtouch_app();

  try {
    my_app_instance->start();
  } catch (std::exception e){
    std::cerr << "Failed to start: " << e.what() << std::endl;
  }

  while( my_app_instance->state == my_xtouch_app::State::Starting);
    // wait for app to change state (Running, or stopped, likely)

  // if failed to start..
  if (my_app_instance->state == my_xtouch_app::State::Off)
    return EXIT_FAILURE;

#if defined(__APPLE__)
  // On macOS, observation can *only* be done in the main thread
  // with an active CFRunLoop.
  CFRunLoopRun();
#else
  //   int c;
  //   while ((c = getchar()) != '\n' && c != EOF)
  //     ;

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
#endif

  return EXIT_SUCCESS;
}