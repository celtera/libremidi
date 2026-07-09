#pragma once

#include <libremidi/detail/polyfill.hpp>
#include <libremidi/error_handler.hpp>
#include <libremidi/message.hpp>

#include <cmath>

#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <span>

NAMESPACE_LIBREMIDI
{
// A clean-room reverse-engineered remote control protocol compatible with many hardware devices.
// Thanks https://github.com/NicoG60/TouchMCU !
struct remote_control_protocol
{
  static constexpr int channel_count = 8;

  enum class device_type : uint8_t
  {
    mackie_hui = 0x05,
    logic_control = 0x10,
    logic_control_xt = 0x11,
    mackie_control = 0x14,
    mackie_control_xt = 0x15
  };

  enum class command_to_device : uint8_t
  {
    device_query = 0x00,
    host_connection_reply = 0x02,

    transport_click = 0x0A,
    lcd_backlight_save = 0x0B,
    touchless_movable_fader = 0x0C,

    faders_touch_sensitivity = 0x0E,
    go_offline = 0x0F,
    update_tc_display = 0x10,
    update_assignment_display = 0x11,
    update_lcd = 0x12,
    firmware_version_request = 0x13,
    version_reply = 0x14,

    firmware_update = 0x18,

    channel_meter_mode = 0x20,
    global_lcd_meter_mode = 0x21,

    faders_to_minimum = 0x61,
    all_leds_off = 0x62,
    reset = 0x63,

    update_channel_colors_xt = 0x72  // xtouch specific (?)
  };

  enum class command_from_device : uint8_t
  {
    host_connection_query = 0x01,
    host_connection_confirmation = 0x03,
    host_connection_error = 0x04,
    version_reply = 0x14,
  };

  enum class lcd_meter_mode : uint8_t
  {
    horizontal = 0x00,
    vertical = 0x01,
  };

  static constexpr uint8_t lcd_channel_line_n = 2;
  static constexpr uint8_t lcd_channel_line_len = 7;
  static constexpr int lcd_total_len = 112;

  static constexpr uint8_t lcd_channel_offset[8][2] = {
      {0x00, 0x38},
      {0x00 + 7, 0x38 + 7},
      {0x00 + 14, 0x38 + 14},
      {0x00 + 21, 0x38 + 21},
      {0x00 + 28, 0x38 + 28},
      {0x00 + 35, 0x38 + 35},
      {0x00 + 42, 0x38 + 42},
      {0x00 + 49, 0x38 + 49},
  };

  /**
   * X-Touch specific
   * 3-bit RGB encoding
   */
  enum class channel_color_xt : uint8_t
  {
    black = 0b000,
    red = 0b001,
    green = 0b010,
    yellow = 0b011,
    blue = 0b100,
    magenta = 0b101,
    cyan = 0b110,
    white = 0b111
  };

  static inline bool channel_color_is_valid(channel_color_xt color)
  {
    return channel_color_xt::black <= color && color <= channel_color_xt::white;
  };

  enum class fader_sensitivity : uint8_t
  {
    sensitivity_0 = 0x00,
    sensitivity_1 = 0x01,
    sensitivity_2 = 0x02,
    sensitivity_3 = 0x03,
    sensitivity_4 = 0x04,

    sensitivity_default = sensitivity_3,
  };

  // CC Message:
  // 0b0LMMVVVV
  // L: toggle underneath LED
  // MM: mode as led_ring_mode
  // VVVV: value in [0,11]

  enum class led_state : uint8_t
  {
    off = 0,
    on = 0b01000000
  };

  enum class led_ring_mode : uint8_t
  {
    mode_0 = 0b000000, // one led only
    mode_1 = 0b010000, // pan pot
    mode_2 = 0b100000, // fill leds from left
    mode_3 = 0b110000, // fill leds from middle
  };

  enum class pot : uint8_t
  {
    pot_0 = 0x00,
    pot_1 = 0x01,
    pot_2 = 0x02,
    pot_3 = 0x03,
    pot_4 = 0x04,
    pot_5 = 0x05,
    pot_6 = 0x06,
    pot_7 = 0x07,
  };

//  struct vpot
//  {
//    pot index;
//    led_state state;
//    led_ring_mode mode;
//    uint8_t value;
//  };

  static constexpr int vpot_max_value = 11;

  enum class fader : uint8_t
  {
    fader_0 = 0x00,
    fader_1 = 0x01,
    fader_2 = 0x02,
    fader_3 = 0x03,
    fader_4 = 0x04,
    fader_5 = 0x05,
    fader_6 = 0x06,
    fader_7 = 0x07,
    fader_master = 0x08,
  };

  enum class mixer_control_type : uint16_t
  {
    vpot_rotation = 0x10,
    vpot_led = 0x30,
    jog_wheel = 0x3C,
    timecode_digit = 0x40,
    assignment_digit = 0x5A,
    other = 0xFFFF
  };


  // control changes
  enum class mixer_control : uint8_t
  {
    // rotation: CC
    // 0b00000001 : clockwise
    // 0b01000001 : counter-clockwise
    vpot_rotation_0 = 0x10 + 0x00,
    vpot_rotation_1 = 0x10 + 0x01,
    vpot_rotation_2 = 0x10 + 0x02,
    vpot_rotation_3 = 0x10 + 0x03,
    vpot_rotation_4 = 0x10 + 0x04,
    vpot_rotation_5 = 0x10 + 0x05,
    vpot_rotation_6 = 0x10 + 0x06,
    vpot_rotation_7 = 0x10 + 0x07,

    external_control = 0x2E,

    // led ring: CC
    vpot_led_0 = 0x30 + 0x00,
    vpot_led_1 = 0x30 + 0x01,
    vpot_led_2 = 0x30 + 0x02,
    vpot_led_3 = 0x30 + 0x03,
    vpot_led_4 = 0x30 + 0x04,
    vpot_led_5 = 0x30 + 0x05,
    vpot_led_6 = 0x30 + 0x06,
    vpot_led_7 = 0x30 + 0x07,

    jog_wheel = 0x3C,

    timecode_digit_0 = 0x40 + 0x00,
    timecode_digit_1 = 0x40 + 0x01,
    timecode_digit_2 = 0x40 + 0x02,
    timecode_digit_3 = 0x40 + 0x03,
    timecode_digit_4 = 0x40 + 0x04,
    timecode_digit_5 = 0x40 + 0x05,
    timecode_digit_6 = 0x40 + 0x06,
    timecode_digit_7 = 0x40 + 0x07,
    timecode_digit_8 = 0x40 + 0x08,
    timecode_digit_9 = 0x40 + 0x09,

    assignment_digit_0 = 0x4A,
    assignment_digit_1 = 0x4B,
  };

  static mixer_control_type which_mixer_control_type(mixer_control ctl)
  {
    if (mixer_control::vpot_rotation_0 <= ctl && ctl <= mixer_control::vpot_rotation_7) return mixer_control_type::vpot_rotation;
    if (mixer_control::vpot_led_0 <= ctl && ctl <= mixer_control::vpot_led_7) return mixer_control_type::vpot_led;
    if (mixer_control::timecode_digit_0 <= ctl && ctl <= mixer_control::timecode_digit_9) return mixer_control_type::timecode_digit;
    if (mixer_control::assignment_digit_0 <= ctl && ctl <= mixer_control::assignment_digit_1) return mixer_control_type::assignment_digit;

    return mixer_control_type::other;
  }

  static inline int relative_value_to_midi(int value){
    if (value < 0)
      return 0b01000000 - value;
    return value;
  }
  static inline int relative_midi_to_value(int midi){
    if (midi < 0b010000)
      return midi;

    return (0b01000000 - midi);
  }

  // note events

  enum class mixer_command_type : uint16_t
  {
    vpot_click = 0x20,
    rec = 0x00,
    solo = 0x08,
    mute = 0x10,
    sel = 0x18,
    assign = 0x28,
    channels = 0x2E,
    f = 0x36,
    page = 0x3E,
    meta = 0x46,
    control = 0x50,
    transport = 0x54,
    user = 0x66,
    fader_touched = 0x68,
    leds = 0x71,
    other = 0xffff
  };

  enum class mixer_command : uint8_t
  {
    // vpot_click
    vpot_click_0 = 0x20 + 0x00,
    vpot_click_1 = 0x20 + 0x01,
    vpot_click_2 = 0x20 + 0x02,
    vpot_click_3 = 0x20 + 0x03,
    vpot_click_4 = 0x20 + 0x04,
    vpot_click_5 = 0x20 + 0x05,
    vpot_click_6 = 0x20 + 0x06,
    vpot_click_7 = 0x20 + 0x07,

    // record arm
    rec_0 = 0x00 + 0x00,
    rec_1 = 0x00 + 0x01,
    rec_2 = 0x00 + 0x02,
    rec_3 = 0x00 + 0x03,
    rec_4 = 0x00 + 0x04,
    rec_5 = 0x00 + 0x05,
    rec_6 = 0x00 + 0x06,
    rec_7 = 0x00 + 0x07,

    // solo
    solo_0 = 0x08 + 0x00,
    solo_1 = 0x08 + 0x01,
    solo_2 = 0x08 + 0x02,
    solo_3 = 0x08 + 0x03,
    solo_4 = 0x08 + 0x04,
    solo_5 = 0x08 + 0x05,
    solo_6 = 0x08 + 0x06,
    solo_7 = 0x08 + 0x07,

    // mute
    mute_0 = 0x10 + 0x00,
    mute_1 = 0x10 + 0x01,
    mute_2 = 0x10 + 0x02,
    mute_3 = 0x10 + 0x03,
    mute_4 = 0x10 + 0x04,
    mute_5 = 0x10 + 0x05,
    mute_6 = 0x10 + 0x06,
    mute_7 = 0x10 + 0x07,

    // sel(ection)
    sel_0 = 0x18 + 0x00,
    sel_1 = 0x18 + 0x01,
    sel_2 = 0x18 + 0x02,
    sel_3 = 0x18 + 0x03,
    sel_4 = 0x18 + 0x04,
    sel_5 = 0x18 + 0x05,
    sel_6 = 0x18 + 0x06,
    sel_7 = 0x18 + 0x07,

    // TODO metering
    // assign
    assign_track = 0x28,
    assign_send = 0x29,
    assign_pan = 0x2A,
    assign_plugin = 0x2B,
    assign_eq = 0x2C,
    assign_instrument = 0x2D,

    // channels
    bank_left = 0x2E,
    bank_right = 0x2F,
    channel_left = 0x30,
    channel_right = 0x31,
    flip = 0x32,
    global = 0x33,

    name_value_button = 0x34,
    smpte_beats_button = 0x35,

    // f(unction)
    f1 = 0x36 + 0x00,
    f2 = 0x36 + 0x01,
    f3 = 0x36 + 0x02,
    f4 = 0x36 + 0x03,
    f5 = 0x36 + 0x04,
    f6 = 0x36 + 0x05,
    f7 = 0x36 + 0x06,
    f8 = 0x36 + 0x07,

    // page
    midi_tracks = 0x3E,
    inputs = 0x3F,
    audio_tracks = 0x40,
    audio_instruments = 0x41,
    aux = 0x42,
    busses = 0x43,
    outputs = 0x44,
    user = 0x45,

    // meta
    shift = 0x46,
    option = 0x47,
    control = 0x48,
    alt = 0x49,

    // control
    save = 0x50,
    undo = 0x51,
    cancel = 0x52,
    enter = 0x53,

    // transport
    markers = 0x54,
    nudge = 0x55,
    cycle = 0x56,
    drop = 0x57,
    replace = 0x58,
    click = 0x59,
    solo = 0x5a,

    rewind = 0x5b,
    forward = 0x5c,
    stop = 0x5d,
    play = 0x5e,
    record = 0x5f,

    up = 0x60,
    down = 0x61,
    left = 0x62,
    right = 0x63,
    zoom = 0x64,
    scrub = 0x65,

    // user
    user_switch_1 = 0x66,
    user_switch_2 = 0x67,

    // fader touched
    fader_touched_0 = 0x68,
    fader_touched_1 = 0x69,
    fader_touched_2 = 0x6a,
    fader_touched_3 = 0x6b,
    fader_touched_4 = 0x6c,
    fader_touched_5 = 0x6d,
    fader_touched_6 = 0x6e,
    fader_touched_7 = 0x6f,
    fader_touched_master = 0x70,

    // leds
    smpte_led = 0x71,
    beats_led = 0x72,
    rude_solo_led = 0x73,

    relay_click = 0x76,
  };

  static mixer_command_type which_mixer_command_type(mixer_command cmd)
  {
    if (mixer_command::vpot_click_0 <= cmd && cmd <= mixer_command::vpot_click_7) return mixer_command_type::vpot_click;
    if (mixer_command::rec_0 <= cmd && cmd <= mixer_command::rec_7) return mixer_command_type::rec;
    if (mixer_command::solo_0 <= cmd && cmd <= mixer_command::solo_7) return mixer_command_type::solo;
    if (mixer_command::mute_0 <= cmd && cmd <= mixer_command::mute_7) return mixer_command_type::mute;
    if (mixer_command::sel_0 <= cmd && cmd <= mixer_command::sel_7) return mixer_command_type::sel;
    if (mixer_command::assign_track <= cmd && cmd <= mixer_command::assign_instrument) return mixer_command_type::assign;
    if (mixer_command::bank_left <= cmd && cmd <= mixer_command::global) return mixer_command_type::channels;
    if (mixer_command::f1 <= cmd && cmd <= mixer_command::f8) return mixer_command_type::f;
    if (mixer_command::midi_tracks <= cmd && cmd <= mixer_command::user) return mixer_command_type::page;
    if (mixer_command::shift <= cmd && cmd <= mixer_command::alt) return mixer_command_type::meta;
    if (mixer_command::save <= cmd && cmd <= mixer_command::enter) return mixer_command_type::control;
    if (mixer_command::markers <= cmd && cmd <= mixer_command::scrub) return mixer_command_type::transport;
    if (mixer_command::user_switch_1 <= cmd && cmd <= mixer_command::user_switch_2) return mixer_command_type::user;
    if (mixer_command::fader_touched_0 <= cmd && cmd <= mixer_command::fader_touched_master) return mixer_command_type::fader_touched;
    if (mixer_command::smpte_led <= cmd && cmd <= mixer_command::rude_solo_led) return mixer_command_type::leds;

    return mixer_command_type::other;
  }

  template <std::size_t N>
  using arr = std::array<uint8_t, N>;

    device_type type = device_type::mackie_control_xt;

  channel_color_xt channel_colors[8] = {channel_color_xt::black, channel_color_xt::black, channel_color_xt::black, channel_color_xt::black, channel_color_xt::black, channel_color_xt::black, channel_color_xt::black, channel_color_xt::black};

  static libremidi::message make_command_impl(auto&&... data)
  {
    using namespace std;
    libremidi::message m;
    m.bytes.reserve((std::ssize(data) + ...));
    (m.bytes.insert(m.bytes.end(), begin(data), end(data)), ...);
    return m;
  }

  libremidi::message make_command(command_to_device c, auto&&... data)
  {
    using namespace std;
    const auto type = to_underlying(this->type);
    const auto cmd = to_underlying(c);
    const uint8_t header[6]{0xF0, 0x00, 0x00, 0x66, type, cmd};
    const uint8_t footer[1]{0xF7};
    return make_command_impl(header, data..., footer);
  }

  auto device_query() { return make_command(command_to_device::device_query); }

  auto response_to_challenge(arr<4> c)
  {
    arr<4> r;

    r[0] = 0x7F & (c[0] + (c[1] ^ 0x0A) - c[3]);
    r[1] = 0x7F & ((c[2] >> 4) ^ (c[0] + c[3]));
    r[2] = 0x7F & (c[3] - (c[2] << 2) ^ (c[0] | c[1]));
    r[3] = 0x7F & (c[1] - c[2] + (0xF0 ^ (c[3] << 4)));

    return r;
  }

  auto host_connection_reply(arr<7> serial, arr<4> challenge_code)
  {
    const auto res = response_to_challenge(challenge_code);
    return make_command(command_to_device::host_connection_reply, serial, res);
  }

  auto transport_click(bool enabled)
  {
    return make_command(
        command_to_device::transport_click, arr<1>{uint8_t(enabled ? 0x01 : 0x00)});
  }

  auto lcd_backlight_save(uint8_t timeout)
  {
    // 0: instant off otherwise timeout in minutes
    return make_command(command_to_device::lcd_backlight_save, arr<1>{timeout});
  }

  auto touchless_movable_fader(bool enabled)
  {
    return make_command(
        command_to_device::touchless_movable_fader, arr<1>{uint8_t(enabled ? 0x01 : 0x00)});
  }

  auto faders_touch_sensitivity(uint8_t fader_id, fader_sensitivity sens)
  {
    return make_command(
        command_to_device::faders_touch_sensitivity, arr<2>{fader_id, to_underlying(sens)});
  }

  auto go_offline() { return make_command(command_to_device::go_offline, arr<1>{0x7F}); }

  auto update_tc_display()
  {
    // FIXME 1 .. 10
    return make_command(command_to_device::update_tc_display, arr<10>{});
  }

  auto update_assignment_display()
  {
    // FIXME 1 .. 2
    return make_command(command_to_device::update_assignment_display, arr<2>{});
  }

  auto update_lcd(std::string_view txt, int pos)
  {
    // FIXME
    if (pos < 0 || lcd_total_len <= pos)
      return libremidi::message{};

    int len = int(std::ssize(txt));

    if (len > (lcd_total_len - pos))
    {
      txt = txt.substr(0, lcd_total_len - pos);
      len = lcd_total_len - pos;
    }

    uint8_t buf[128];
    const int N = std::min(len, lcd_total_len - pos);
    for (int i = 0; i < N; i++)
    {
      buf[i + pos] = charmap_lcd(txt[i]);
    }
//    buf[55] = '\n';
//    buf[111] = '\n';

    uint8_t cmd_pos = pos;

    return make_command(command_to_device::update_lcd, arr<1>{cmd_pos}, std::span(buf + pos, len));
  }

  auto update_lcd(std::string_view txt)
  {
    uint8_t buf[lcd_total_len] = {};
    for (int i = 0; i < std::min(int(std::ssize(txt)), lcd_total_len); i++)
    {
      buf[i] = charmap_lcd(txt[i]);
    }
//    buf[55] = '\n';
//    buf[111] = '\n';
    return make_command(command_to_device::update_lcd, arr<1>{0}, std::span(buf, lcd_total_len));
  }

  auto update_channel_colors()
  {
    return make_command(command_to_device::update_channel_colors_xt, std::span((uint8_t*)channel_colors, 8));
  }

  auto firmware_version_request()
  {
    return make_command(command_to_device::firmware_version_request, arr<1>{0});
  }

  auto firmware_update(std::span<uint8_t> firmware)
  {
    return make_command(command_to_device::firmware_update, firmware);
  }

  auto channel_meter_mode(uint8_t fader_id, bool level_meter, bool peak_hold, bool signal_led)
  {
    uint8_t mode = 0;

    if (signal_led)
      mode |= 0b1;
    if (peak_hold)
      mode |= 0b10;
    if (level_meter)
      mode |= 0b100;

    return make_command(command_to_device::channel_meter_mode, arr<2>{fader_id, mode});
  }

  auto global_lcd_meter_mode(lcd_meter_mode mode)
  {
    return make_command(command_to_device::global_lcd_meter_mode, arr<1>{to_underlying(mode)});
  }

  auto faders_to_minimum() { return make_command(command_to_device::faders_to_minimum); }

  auto all_leds_off() { return make_command(command_to_device::all_leds_off); }

  auto reset() { return make_command(command_to_device::reset); }

  static auto timecode(int hi, int mi, int si, int framei)
  {
    std::vector<libremidi::message> msg;
    auto h = std::to_string(hi);
    while (h.size() < 3)
      h.insert(h.begin(), '0');
    auto m = std::to_string(mi);
    while (m.size() < 2)
      m.insert(m.begin(), '0');
    auto s = std::to_string(si);
    while (s.size() < 2)
      s.insert(s.begin(), '0');
    auto f = std::to_string(framei);
    while (f.size() < 3)
      f.insert(f.begin(), '0');

    using ce = libremidi::channel_events;
    msg.push_back(ce::control_change(1, 0x49, charmap_7segment(h[0])));
    msg.push_back(ce::control_change(1, 0x48, charmap_7segment(h[1])));
    msg.push_back(ce::control_change(1, 0x47, charmap_7segment(h[2])));

    msg.push_back(ce::control_change(1, 0x46, charmap_7segment(m[0])));
    msg.push_back(ce::control_change(1, 0x45, charmap_7segment(m[1])));

    msg.push_back(ce::control_change(1, 0x44, charmap_7segment(s[0])));
    msg.push_back(ce::control_change(1, 0x43, charmap_7segment(s[1])));

    msg.push_back(ce::control_change(1, 0x42, charmap_7segment(f[0])));
    msg.push_back(ce::control_change(1, 0x41, charmap_7segment(f[1])));
    msg.push_back(ce::control_change(1, 0x40, charmap_7segment(f[2])));

    return msg;
  }

  static uint8_t charmap_7segment(char c, bool dot)
  {
    uint8_t res = charmap_7segment(c);
    if (dot)
      res |= 0b00100000;
    return res;
  };

  static uint8_t charmap_7segment(char c)
  {
    // FIXME there are some more characters but what to map them to ? :)
    if (c >= 'a' && c <= 'z')
      return c - 'a' + 1;
    else if (c >= 'A' && c <= 'Z')
      return c - 'A' + 1;
    else if (c >= '0' && c <= '9')
      return c - '0' + 0x30;
    else
      switch (c)
      {
        case '[':
        case '{':
          return 0x1B;
        case '\\':
        case '~': // yen too ?
          return 0x1C;
        case ']':
        case '}':
          return 0x1D;
        case '^':
          return 0x1E;
        case '_':
          return 0x1F;

        case '!':
          return 0x21;
        case '"':
          return 0x22;
        case '#':
          return 0x23;
        case '$':
          return 0x24;
        case '%':
          return 0x25;
        case '&':
          return 0x26;
        case '\'':
          return 0x27;
        case '(':
          return 0x28;
        case ')':
          return 0x29;
        case '*':
          return 0x2A;
        case '+':
          return 0x2B;
        case ',':
          return 0x2C;
        case '-':
          return 0x2D;
        case '.':
          return 0x2E;
        case '/':
          return 0x2F;

        case ':':
          return 0x3A;
        case ';':
          return 0x3B;
        case '<':
          return 0x3C;
        case '=':
          return 0x3D;
        case '>':
          return 0x3E;
        case '?':
          return 0x3F;

        default:
          return 0x00;
      }
  }
  static uint8_t charmap_lcd(char c)
  {
    // FIXME there are some more characters but what to map them to ? :)
    if (c >= 'a' && c <= 'z')
      return c - 'a' + 0x61;
    else if (c >= 'A' && c <= 'Z')
      return c - 'A' + 0x41;
    else if (c >= '0' && c <= '9')
      return c - '0' + 0x30;
    else
      switch (c)
      {
        case '!':
          return 0x21;
        case '"':
          return 0x22;
        case '#':
          return 0x23;
        case '$':
          return 0x24;
        case '%':
          return 0x25;
        case '&':
          return 0x26;
        case '\'':
          return 0x27;
        case '(':
          return 0x28;
        case ')':
          return 0x29;
        case '*':
          return 0x2A;
        case '+':
          return 0x2B;
        case ',':
          return 0x2C;
        case '-':
          return 0x2D;
        case '.':
          return 0x2E;
        case '/':
          return 0x2F;

        case ':':
          return 0x3A;
        case ';':
          return 0x3B;
        case '<':
          return 0x3C;
        case '=':
          return 0x3D;
        case '>':
          return 0x3E;
        case '?':
          return 0x3F;

        case '@':
          return 0x40;
        case '[':
          return 0x5B;
        case '~': // Yen symbol... builtin mojibake?
          return 0x5C;
        case ']':
          return 0x5D;
        case '^':
          return 0x5E;
        case '_':
          return 0x5F;
        case '`':
          return 0x60;
        case '{':
          return 0x7B;
        case '|':
          return 0x7C;
        case '}':
          return 0x7D;
        case '\u000E':
          return 0x7E;
        case '\u000F':
          return 0x7F;
        default:
          return c; // gives access to the bubble first row 0x00 > 0x0F
      }
  }
};

struct rcp_configuration
{
  //! How to send MIDI messages to the device.
  //! Note: this function *will* be called from different thread,
  //! thus it has to be thread-safe, for instance
  //! by storing the message in an event queue.
  std::function<void(libremidi::message&&)> midi_out;

  std::function<void(libremidi::remote_control_protocol::device_type)> on_connected;
  std::function<void(libremidi::remote_control_protocol::mixer_command, bool)> on_command;
  std::function<void(libremidi::remote_control_protocol::mixer_control, int)> on_control;
  std::function<void(libremidi::remote_control_protocol::fader, uint16_t)> on_fader;

  libremidi::midi_error_callback on_error{};
};

struct remote_control_processor : libremidi::error_handler
{
  using rcp = libremidi::remote_control_protocol;
  rcp_configuration configuration;
  rcp impl;

  explicit remote_control_processor(rcp_configuration conf)
      : configuration{std::move(conf)}
  {
    assert(configuration.midi_out);

    if (!configuration.on_error)
      configuration.on_error = [](std::string_view s, auto&&...) {
        std::fprintf(stderr, "libremidi: rcp error: %s\n", s.data());
      };

    if (!configuration.on_connected)
      configuration.on_connected
          = [this](auto&&...) { libremidi_handle_error(configuration, "Unhandled on_connected"); };

    if (!configuration.on_command)
      configuration.on_command
          = [this](auto&&...) { libremidi_handle_error(configuration, "Unhandled on_command"); };

    if (!configuration.on_control)
      configuration.on_control
          = [this](auto&&...) { libremidi_handle_error(configuration, "Unhandled on_control"); };

    if (!configuration.on_fader)
      configuration.on_fader
          = [this](auto&&...) { libremidi_handle_error(configuration, "Unhandled on_fader"); };
  }

  void start()
  {
    current_state = waiting_for_query;
    configuration.midi_out(impl.device_query());
    configuration.midi_out(impl.firmware_version_request());
  }

  void on_midi(const libremidi::message& message)
  {
    switch (message.get_message_type())
    {
      case libremidi::message_type::SYSTEM_EXCLUSIVE:
        if (auto N = message.size(); N >= 7)
        {
          const uint8_t* bytes = message.bytes.data();

          // strip 0xF0 & 0xF7
          bytes += 1;
          N -= 2;

          // Mackie manufacturer ID check
          if (bytes[0] == 0x00 && bytes[1] == 0x00 && bytes[2] == 0x66)
          {
            impl.type = static_cast<rcp::device_type>(bytes[3]);

            std::cerr << "device type " << (int) impl.type << std::endl;

            // strip header
            bytes += 4;
            N -= 4;
            on_rcp_command(std::span(bytes, N));
          }
        }
        else
        {
          libremidi_handle_error(configuration, "Invalid sysex");
        }
        break;
      case libremidi::message_type::NOTE_ON:
        configuration.on_command(static_cast<rcp::mixer_command>(message[1]), message[2] > 0);
        break;
      case libremidi::message_type::NOTE_OFF:
        break;
      case libremidi::message_type::CONTROL_CHANGE:
        configuration.on_control(static_cast<rcp::mixer_control>(message[1]), message[2]);
        break;
      case libremidi::message_type::PITCH_BEND: {
        uint16_t value = message.bytes[2] * 128 + message.bytes[1];
        configuration.on_fader(static_cast<rcp::fader>(uint8_t(message.get_channel() - 1)), value);
        break;
      }
      default:
        break;
    }
  }

  void on_rcp_command(std::span<const uint8_t> cmd)
  {
    if (cmd.empty())
    {
      libremidi_handle_error(configuration, "on_rcp_command: empty command");
      return;
    }

    auto command = static_cast<rcp::command_from_device>(cmd[0]);
    cmd = cmd.subspan(1);
    switch (command)
    {
      case rcp::command_from_device::host_connection_query: {
        if (cmd.size() == 11)
        {
          current_state = got_query;

          std::array<uint8_t, 7> serial;
          std::array<uint8_t, 4> challenge;
          std::copy_n(cmd.data(), 7, serial.begin());
          std::copy_n(cmd.data() + 7, 4, challenge.begin());

          configuration.midi_out(impl.host_connection_reply(serial, challenge));
        }
        else
          libremidi_handle_error(configuration, "host_connection_query: invalid size");
        break;
      }
      case rcp::command_from_device::host_connection_confirmation:
        current_state = connected;
        configuration.on_connected(impl.type);
        break;
      case rcp::command_from_device::host_connection_error:
        current_state = errored;
        libremidi_handle_error(configuration, "host_connection_error");
        break;
      case rcp::command_from_device::version_reply: {
        // TODO
        break;
      }
      default:
        break;
    }
  }

  void update_timecode(int h, int m, int s, int f)
  {
    for (auto&& m : rcp::timecode(h, m, s, f))
      configuration.midi_out(std::move(m));
  }

  void update_lcd(std::string_view v)
  {
    auto res = impl.update_lcd(v);
    if (!res.empty())
      configuration.midi_out(std::move(res));
  }

  void update_lcd(std::string_view v, int pos)
  {
    auto res = impl.update_lcd(v, pos);
    if (!res.empty())
      configuration.midi_out(std::move(res));
  }

  void update_lcd_ch_line(std::string_view v, uint ch, uint line)
  {
    // 8 channels, 2 lines
    // Show error msg?
    if (7 < ch || remote_control_protocol::lcd_channel_line_n < line)
      return;

    // Prefill segment with spaces
    char buf[8] = {0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0};

    int len = int(std::ssize(v));
    if (len > remote_control_protocol::lcd_channel_line_len)
      len = remote_control_protocol::lcd_channel_line_len;

    std::memcpy(buf, v.data(), len);

    auto res = impl.update_lcd(buf, remote_control_protocol::lcd_channel_offset[ch][line]);

    if (!res.empty())
      configuration.midi_out(std::move(res));
  }

  void set_channel_color(uint8_t ch, remote_control_protocol::channel_color_xt color)
  {
    if (7 < ch || ! remote_control_protocol::channel_color_is_valid(color))
      return;

    impl.channel_colors[ch] = color;
  }

  void update_channel_colors()
  {
    auto res = impl.update_channel_colors();
    if (!res.empty())
      configuration.midi_out(std::move(res));
  }

  void command(remote_control_protocol::mixer_command c, bool press)
  {
    using ce = libremidi::channel_events;
    configuration.midi_out(ce::note_on(1, to_underlying(c), press ? 127 : 0));
    configuration.midi_out(ce::note_off(1, to_underlying(c), press ? 127 : 0));
  }

  void control(remote_control_protocol::mixer_control c, int value)
  {
    using ce = libremidi::channel_events;
    configuration.midi_out(ce::control_change(1, to_underlying(c), value));
  }

  inline void vpot(remote_control_protocol::pot index, remote_control_protocol::led_state state, remote_control_protocol::led_ring_mode mode, uint8_t value)
  {
    control(
      (remote_control_protocol::mixer_control)((int)remote_control_protocol::mixer_control::vpot_led_0 + (int)index),
      (int)state | (int)mode | value
    );
  }

  void fader(remote_control_protocol::fader c, uint16_t value)
  {
    int idx = to_underlying(c);

    using ce = libremidi::channel_events;
    configuration.midi_out(ce::pitch_bend(idx + 1, value));
  }

  // State machine
  enum
  {
    waiting_for_query,
    got_query,
    connected,
    errored
  } current_state{waiting_for_query};
};
}
