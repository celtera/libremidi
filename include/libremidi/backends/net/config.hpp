#pragma once
#include <libremidi/config.hpp>

#include <boost/asio.hpp>

#include <string>
namespace boost::asio
{
struct io_context;
}

namespace libremidi
{
enum class net_protocol
{
  RTPMIDI,
  APPLEMIDI,
  LIBREMIDI_DGRAM_MIDI1,
  LIBREMIDI_DGRAM_MIDI2
};

struct net_dgram_input_configuration
{
  std::string client_name = "libremidi client";

  net_protocol protocol = net_protocol::LIBREMIDI_DGRAM_MIDI1;
  std::string accept = "0.0.0.0";
  int port{};

  boost::asio::io_context* io_context{};
};

struct net_dgram_output_configuration
{
  std::string client_name = "libremidi client";

  net_protocol protocol = net_protocol::LIBREMIDI_DGRAM_MIDI1;
  std::string host = "127.0.0.1";
  int port{};

  boost::asio::io_context* io_context{};
};

struct net_dgram_observer_configuration
{
  std::string client_name = "libremidi client";

  boost::asio::io_context* io_context{};
};

}
