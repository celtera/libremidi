#pragma once
#include <libremidi/config.hpp>

#include <string>

#if !defined(BOOST_ASIO_IO_CONTEXT_HPP)
#if __has_include(<boost/asio/detail/config.hpp>)
  #include <boost/asio/detail/config.hpp>
#endif

namespace boost::asio
{
#if defined(BOOST_ASIO_INLINE_NAMESPACE_BEGIN)
BOOST_ASIO_INLINE_NAMESPACE_BEGIN
class io_context;
BOOST_ASIO_INLINE_NAMESPACE_END
#else
class io_context;
#endif
}
#endif

NAMESPACE_LIBREMIDI::net
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

NAMESPACE_LIBREMIDI::net_ump
{
enum class protocol
{
  // One UMP per OSC message, without any session
  OSC_MIDI2,

  // Network MIDI 2.0 (UDP), MIDI Association M2-124-UM.
  // - open_virtual_port makes a host: it waits for clients on "port"
  //   (and on the "accept" address for an input).
  // - open_port makes a client: it invites the host at "host":"port".
  // A session carries UMP both ways; an input only reads it, an output only writes it.
  MIDI2_UDP,
};

struct dgram_input_configuration
{
  std::string client_name = "libremidi client";

  enum protocol protocol = protocol::OSC_MIDI2;
  std::string accept = "0.0.0.0";
  int port{};

  boost::asio::io_context* io_context{};

  // MIDI2_UDP only.
  // The host to invite when opening a port as a client.
  std::string host;
  // Identifies this device across sessions: ASCII, at most 42 bytes.
  std::string product_instance_id = "libremidi";
  // A host with a secret only accepts the clients that know it.
  std::string shared_secret;
};

struct dgram_output_configuration
{
  std::string client_name = "libremidi client";

  enum protocol protocol = protocol::OSC_MIDI2;
  std::string host = "127.0.0.1";
  int port{};
  bool broadcast{};

  boost::asio::io_context* io_context{};

  // MIDI2_UDP only.
  // Identifies this device across sessions: ASCII, at most 42 bytes.
  std::string product_instance_id = "libremidi";
  // A host with a secret only accepts the clients that know it.
  std::string shared_secret;
  // open_port waits this long for the host to accept: sends fail until it has.
  // Zero does not wait.
  int connection_timeout_ms = 2000;
};

struct dgram_observer_configuration
{
  std::string client_name = "libremidi client";

  boost::asio::io_context* io_context{};
};

}
