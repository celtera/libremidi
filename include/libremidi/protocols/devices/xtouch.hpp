/**
* libremidi
* Copyright (C) 2026  Philip Tschiemer
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU Affero General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Affero General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <libremidi/protocols/remote_control.hpp>

NAMESPACE_LIBREMIDI
{

  /**
  * X-Touch screen colors RE from extender unit:
  * Command Format := <mackie control xt type> <set colors command> 8*<color>
  *  <set colors command> := 0x72
  *  <color> := [0 - 7] (on-off bitmap of RGB colors LSB = R)
  * Example (setting each screen a different color): 15720001020304050607
  */
  struct remote_control_protocol_xtouch : remote_control_protocol {

    enum class model_type : uint8_t
    {
      base = 0, ///< X-Touch
      extender = 1, ///< X-Touch Extender
      one = 2, ///< X-Touch One
      compact = 3, ///< X-Touch Compact
    };

    static inline bool model_type_is_valid(model_type model)
    {
      return (model == model_type::base ||
              model == model_type::extender ||
              model == model_type::one ||
              model == model_type::compact);
    }

    static constexpr remote_control_protocol::device_type device_type_by_model[4] = {
        remote_control_protocol::device_type::mackie_control, // base
        remote_control_protocol::device_type::mackie_control_xt, // extender
        remote_control_protocol::device_type::mackie_control, // one (guessed)
        remote_control_protocol::device_type::mackie_control, // compact (guessed)
    };


    enum class command_to_device : uint8_t
    {
      update_channel_colors = 0x72
    };

    /**
   * X-Touch specific
   * 3-bit RGB encoding
   */
    enum class channel_color : uint8_t
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

    typedef channel_color channel_color_list_t[8];

    static inline bool channel_color_is_valid(channel_color color)
    {
      return channel_color::black <= color && color <= channel_color::white;
    };


    auto update_channel_colors(channel_color_list_t list)
    {
      return make_command(
          static_cast<remote_control_protocol::command_to_device>(command_to_device::update_channel_colors),
          std::span(reinterpret_cast<uint8_t*>(list),8)
      );
    }

  };

  struct remote_control_client_processor_xtouch : remote_control_client_processor
  {
    struct configuration : public rcp_configuration
    {
      remote_control_protocol_xtouch::model_type model_type;
    };

    struct configuration configuration;

    remote_control_protocol_xtouch impl;

    remote_control_protocol_xtouch::model_type model;

    remote_control_protocol_xtouch::channel_color_list_t channel_color_list = {
        remote_control_protocol_xtouch::channel_color::white,
        remote_control_protocol_xtouch::channel_color::white,
        remote_control_protocol_xtouch::channel_color::white,
        remote_control_protocol_xtouch::channel_color::white,
        remote_control_protocol_xtouch::channel_color::white,
        remote_control_protocol_xtouch::channel_color::white,
        remote_control_protocol_xtouch::channel_color::white,
        remote_control_protocol_xtouch::channel_color::white
    };


    explicit remote_control_client_processor_xtouch(struct configuration conf) : remote_control_client_processor(conf), configuration(std::move(conf)){

      if (!remote_control_protocol_xtouch::model_type_is_valid(conf.model_type))
        throw new std::invalid_argument("invalid xtouch model type");

      impl.type = remote_control_protocol_xtouch::device_type_by_model[libremidi::to_underlying(conf.model_type)];
    }

    void set_channel_color(int channel, remote_control_protocol_xtouch::channel_color color)
    {
      if (channel < 0 || 7 < channel)
        throw new std::invalid_argument("channel index must be in [0,7]]");

      channel_color_list[channel] = color;
    }

    void update_channel_colors()
    {
      auto res = impl.update_channel_colors(channel_color_list);
      if (!res.empty())
        configuration.midi_out(std::move(res));
    }

    inline void update_channel_color(int channel, remote_control_protocol_xtouch::channel_color color)
    {
      set_channel_color(channel, color);
      update_channel_colors();
    }

  };
}
