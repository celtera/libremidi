#include "../utils.hpp"

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

using mcu = libremidi::remote_control_protocol;
using mcu_proc = libremidi::remote_control_processor;

struct my_xtouch_app
{
  static constexpr auto api = libremidi::API::UNSPECIFIED;
  static constexpr char kDeviceName[] = "X-TOUCH_INT";
  static constexpr mcu::device_type kDeviceType = mcu::device_type::mackie_control_xt;

  enum class State : uint8_t
  {
    Off,Starting, Running, Stopping
  };

  struct vpot_st
  {
    mcu::channel_index index;
    mcu::led_state state = mcu::led_state::off;
    mcu::led_ring_mode mode = mcu::led_ring_mode::mode_0;
    uint8_t value = 0;
    void change_value(int change){
      if (change > 0 && value < mcu::vpot_max_value-1) value++;
      else if (change < 0 && value > 0) value--;
    }
    void toggle_led_state(){
      if (state == mcu::led_state::off)
        state = mcu::led_state::on;
      else
        state = mcu::led_state::off;
    }
  };


  State state = State::Off;

  MIDIClientRef handle;

  libremidi::midi_out * midi_out;
  libremidi::midi_in * midi_in;

  libremidi::remote_control_processor * rcp;

  struct {
    bool rec[8] = {false,false,false,false,false,false,false,false};
    bool mute[8] = {false,false,false,false,false,false,false,false};
  } buttons;

  struct vpot_st vpots[8] = {
      {.index = mcu::channel_index::channel_1, .mode = mcu::led_ring_mode::mode_0, .state = mcu::led_state::off},
      {.index = mcu::channel_index::channel_2, .mode = mcu::led_ring_mode::mode_1, .state = mcu::led_state::off},
      {.index = mcu::channel_index::channel_3, .mode = mcu::led_ring_mode::mode_2, .state = mcu::led_state::on},
      {.index = mcu::channel_index::channel_4, .mode = mcu::led_ring_mode::mode_3, .state = mcu::led_state::on},
      {.index = mcu::channel_index::channel_5, .mode = mcu::led_ring_mode::mode_0, .value = 6},
      {.index = mcu::channel_index::channel_6, .mode = mcu::led_ring_mode::mode_1, .value = 6},
      {.index = mcu::channel_index::channel_7, .mode = mcu::led_ring_mode::mode_2, .value = 6},
      {.index = mcu::channel_index::channel_8, .mode = mcu::led_ring_mode::mode_3, .value = 6}
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
    if (rcp)
      delete rcp;
    if (midi_in)
      delete midi_in;
    if (midi_out)
      delete midi_out;

    std::cout << "Disposing of MIDIClient" << std::endl;
    MIDIClientDispose(handle);
  }

  void _update_buttons(int i = -1)
  {
    assert(rcp);

    if (i == -1){
      rcp->command((mcu::mixer_command)((int)(mcu::mixer_command::rec_0) + i), buttons.rec[i]);
      rcp->command((mcu::mixer_command)((int)(mcu::mixer_command::mute_0) + i), buttons.mute[i]);
    }
    else {
        for (int i = 0; i < 8; i++){
          rcp->command((mcu::mixer_command)((int)(mcu::mixer_command::rec_0) + i), buttons.rec[i]);
          rcp->command((mcu::mixer_command)((int)(mcu::mixer_command::mute_0) + i), buttons.mute[i]);
        }
    }
  }

  void _update_vpots(int i = -1)
  {
    assert(rcp);

    if (i == -1){
      rcp->vpot(vpots[i].index, vpots[i].state, vpots[i].mode, vpots[i].value);
    } else {
      for (i = 0; i < 8; i++){
        rcp->vpot(vpots[i].index, vpots[i].state, vpots[i].mode, vpots[i].value);
      }
    }
  }

  void _start_impl()
  {
    std::cout << "Starting app.." << std::endl;
    state = State::Starting;

//    auto callback = [&](int port, const libremidi::message& msg) {
//      std::cout << msg << std::endl;
//      midiout[port].send_message(msg);
//    };



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
            .on_message = [&](const libremidi::message& message) {
              rcp->on_midi(message);
            }
        },
        api
    };

    // Set-up the remote control API.
    // Here we only do some logging, this is where commands sqall be handled.
//    libremidi::remote_control_processor rcp{
    rcp = new libremidi::remote_control_processor{
        {
            .device_type = kDeviceType,
            .midi_out = [&](libremidi::message&& msg){
              midi_out->send_message(msg);
            },
            .on_command = [&](mcu::mixer_command cmd, bool pressed){
              std::cerr << "command: " << magic_enum::enum_name(cmd) << " -> " << (pressed ? "pressed" : "released") << "\n";

              auto type = mcu::which_mixer_command_type(cmd);
              auto index = mcu::which_mixer_command_index(type, cmd);
              switch(type){
                case mcu::mixer_command::type_rec:
                  if (!pressed) return;
                  // toggle button state
                  buttons.rec[index] = !buttons.rec[index];
                  _update_buttons(index);
                  break;

                case mcu::mixer_command::type_mute:
                  if (!pressed) return;
                  // toggle button state
                  buttons.mute[index] = !buttons.mute[index];
                  _update_buttons(index);
                  break;

                case mcu::mixer_command::type_solo:
                case mcu::mixer_command::type_sel:
                  // just light up button while being pressed
                  rcp->command(cmd, pressed);
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
                  switch(cmd) {
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
                  switch(cmd){
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
            .on_control = [&](mcu::mixer_control ctl, int v) {
              std::cerr << "control: " << magic_enum::enum_name(ctl) << " -> " << v << "\n";

              auto type = mcu::which_mixer_control_type(ctl);
              auto index = mcu::which_mixer_control_index(type, ctl);
              auto val = 0;
              switch(type)
              {
                case mcu::mixer_control::type_vpot_rotation:
                  val = mcu::relative_midi_to_value(v);
                  std::cerr << "-> vpot "  << index << " relative value= " << val << std::endl;

                  vpots[index].change_value(val);

                  _update_vpots(index);
                  break;

                default:
                  break;
              }
            },
            .on_fader = [](uint8_t fader, uint16_t v) {
              std::cerr << "fader: " << (int)fader << " -> " << v << "\n";
            }
        }
    };
//rcp.command()

    // Open the ports
    if (auto err = midi_in->open_port(ip); err != stdx::error{})
      err.throw_exception();

    if (auto err = midi_out->open_port(op); err != stdx::error{})
      err.throw_exception();


    state = State::Running;

    // Start communication
    rcp->start();


    // reset interface
    _update_buttons();
    _update_vpots();

    // Blast messages :)

    mcu::channel_color_xt colors[8] = {
        mcu::channel_color_xt::black,   mcu::channel_color_xt::red,  mcu::channel_color_xt::yellow,
        mcu::channel_color_xt::green,   mcu::channel_color_xt::cyan, mcu::channel_color_xt::blue,
        mcu::channel_color_xt::magenta, mcu::channel_color_xt::white
    };
    std::string labels1[8] = {"1", "21", "321", "4321", "54321", "654321", "7654321", "87654321"};
    std::string labels2[8] = {"ch1", "ch2", "ch3", "ch4", "ch5", "ch6", "ch7", "ch8"};

    for (uint8_t i = 0; state == State::Running;i++)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      std::time_t result = std::time(nullptr);
      auto ctime = std::localtime(&result);

      rcp->update_timecode(ctime->tm_hour, ctime->tm_min, ctime->tm_sec, 0);

      rcp->update_lcd_ch_line(labels1[ (i/8 + i%8) % 8 ], i % 8, 0);
      rcp->update_lcd_ch_line(labels2[ (i/8 + i%8) % 8 ], i % 8, 1);

      rcp->set_channel_color(i % 8, colors[(i/8 + i%8) % 8]);
      rcp->update_channel_colors();

      rcp->fader(i % 8, (200 * i) % 16384);

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