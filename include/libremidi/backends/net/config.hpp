#pragma once
#include <libremidi/config.hpp>

#include <string>
namespace boost::asio
{
struct io_context;
}

namespace libremidi::net
{
enum class protocol
{
  OSC_MIDI,
};

struct dgram_input_configuration
{
  std::string client_name = "libremidi client";

  enum protocol protocol = protocol::OSC_MIDI;
  std::string accept = "0.0.0.0";
  int port{};

  boost::asio::io_context* io_context{};
};

struct dgram_output_configuration
{
  std::string client_name = "libremidi client";

  enum protocol protocol = protocol::OSC_MIDI;
  std::string host = "127.0.0.1";
  int port{};
  bool broadcast{};

  boost::asio::io_context* io_context{};
};

struct dgram_observer_configuration
{
  std::string client_name = "libremidi client";

  boost::asio::io_context* io_context{};
};

}

namespace libremidi::net_ump
{
enum class protocol
{
  OSC_MIDI2,
};

struct dgram_input_configuration
{
  std::string client_name = "libremidi client";

  enum protocol protocol = protocol::OSC_MIDI2;
  std::string accept = "0.0.0.0";
  int port{};

  boost::asio::io_context* io_context{};
};

struct dgram_output_configuration
{
  std::string client_name = "libremidi client";

  enum protocol protocol = protocol::OSC_MIDI2;
  std::string host = "127.0.0.1";
  int port{};
  bool broadcast{};

  boost::asio::io_context* io_context{};
};

struct dgram_observer_configuration
{
  std::string client_name = "libremidi client";

  boost::asio::io_context* io_context{};
};

}
