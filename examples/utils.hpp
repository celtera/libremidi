#pragma once

#include <libremidi/libremidi.hpp>
// Credits to https://raw.githubusercontent.com/atsushieno/cmidi2
#include <libremidi/cmidi2.hpp>
#if LIBREMIDI_USE_NI_MIDI2
  #include <midi/universal_packet.h>
#endif

#include "3rdparty/args.hxx"

#include <cstdlib>
#include <iostream>

inline std::ostream& operator<<(std::ostream& s, const libremidi::message& message)
{
  auto nBytes = message.size();
  s << "[ ";
  for (auto i = 0U; i < nBytes; i++)
    s << std::hex << (int)message[i] << std::dec << " ";
  s << "]";
  if (nBytes > 0)
    s << " ; stamp = " << message.timestamp;
  return s;
}

inline std::ostream& operator<<(std::ostream& s, const libremidi::ump& message)
{
  const cmidi2_ump* b = message;
  int bytes = cmidi2_ump_get_num_bytes(message.data[0]);
  int group = cmidi2_ump_get_group(b);
  int status = cmidi2_ump_get_status_code(b);
  int channel = cmidi2_ump_get_channel(b);
  s << "[ " << bytes << " | " << group;

  switch ((libremidi::message_type)status)
  {
    case libremidi::message_type::NOTE_ON:
      s << " | note on: " << channel << (int)cmidi2_ump_get_midi2_note_note(b) << " | "
        << cmidi2_ump_get_midi2_note_velocity(b);
      break;
    case libremidi::message_type::NOTE_OFF:
      s << " | note off: " << channel << (int)cmidi2_ump_get_midi2_note_note(b) << " | "
        << cmidi2_ump_get_midi2_note_velocity(b);
      break;
    case libremidi::message_type::CONTROL_CHANGE:
      s << " | cc: " << channel << (int)cmidi2_ump_get_midi2_cc_index(b) << " | "
        << cmidi2_ump_get_midi2_cc_data(b);
      break;

    default:
      break;
  }
  s << " ]";
#if LIBREMIDI_USE_NI_MIDI2
  s << " :: " << midi::universal_packet(message);
#endif
  return s;
}

inline std::ostream& operator<<(std::ostream& s, const libremidi::port_information& rhs)
{
  s << "[ client: " << rhs.client << ", port: " << rhs.port;
  if (!rhs.manufacturer.empty())
    s << ", manufacturer: " << rhs.manufacturer;
  if (!rhs.device_name.empty())
    s << ", device: " << rhs.device_name;
  if (!rhs.port_name.empty())
    s << ", portname: " << rhs.port_name;
  if (!rhs.display_name.empty())
    s << ", display: " << rhs.display_name;
  return s << "]";
}

namespace libremidi::examples
{
struct arguments
{
  libremidi::API api{libremidi::API::UNSPECIFIED};
  int input_port{0};
  int output_port{0};
  int count{50};
  bool virtual_port{};

  static std::string api_list()
  {
    std::string ret;
    ret.reserve(64);
    auto apis = libremidi::available_apis();
    for (auto api : apis)
    {
      ret += libremidi::get_api_name(api);
      ret += ", ";
    }
    if (apis.size() > 0)
      ret.resize(ret.size() - 2);
    return ret;
  }

  arguments(int argc, const char** argv)
  {
    args::ArgumentParser parser("libremidi example");
    args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});

    args::ValueFlag<std::string> opt_api(parser, "api", "API to use (" + api_list() + ")", {'a'});
    args::ValueFlag<int> opt_port(parser, "input", "Input port to open", {'i'});
    args::ValueFlag<int> opt_out_port(parser, "output", "Output port to open", {'o'});
    args::ValueFlag<int> opt_count(parser, "count", "Number of bytes", {'n'});
    args::Flag opt_virt(
        parser, "virtual", "Open a virtual port instead of an existing one", {'v'});

    args::CompletionFlag completion(parser, {"complete"});

    try
    {
      parser.ParseCLI(argc, argv);
    }
    catch (args::Help)
    {
      std::cout << parser;
      std::exit(1);
    }
    catch (args::ParseError e)
    {
      std::cerr << e.what() << std::endl;
      std::cerr << parser;
      std::exit(1);
    }
    catch (args::ValidationError e)
    {
      std::cerr << e.what() << std::endl;
      std::cerr << parser;
      std::exit(1);
    }

    if (opt_api)
      api = libremidi::get_compiled_api_by_name(opt_api.Get());
    if (opt_port)
      input_port = opt_port.Get();
    if (opt_out_port)
      output_port = opt_out_port.Get();
    if (opt_count)
      count = opt_count.Get();
    if (opt_virt)
      virtual_port = true;
  }

  template <typename T>
  inline bool open_port(T& midi)
  {
    if (this->virtual_port)
    {
      const auto err = midi.open_virtual_port();
      return err == stdx::error{};
    }

    const auto obs = libremidi::observer{
        {.track_hardware = 1, .track_virtual = 1},
        observer_configuration_for(midi.get_current_api())};
    auto [ports, index] = get_ports(obs, midi);
    if (ports.empty())
    {
      std::cout << "No ports available!" << std::endl;
      return false;
    }

    if (index >= 0 && index < ports.size())
    {
      std::cout << "Opening " << ports[index].display_name << std::endl;
      const auto err = midi.open_port(ports[index]);
      return err == stdx::error{};
    }
    else
    {
      std::cout << "Cannot open port " << index << std::endl;
      return false;
    }
  }

private:
  auto get_ports(const libremidi::observer& obs, const libremidi::midi_in&)
  {
    return std::make_pair(obs.get_input_ports(), this->input_port);
  }
  auto get_ports(const libremidi::observer& obs, const libremidi::midi_out&)
  {
    return std::make_pair(obs.get_output_ports(), this->output_port);
  }
};
}
