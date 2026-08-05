#pragma once
#include <libremidi/config.hpp>

#include <string>

#if !defined(BOOST_ASIO_IO_CONTEXT_HPP)
// Asio may put its types in an inline version namespace: since Boost 1.91,
// BOOST_ASIO_ENABLE_VERSION_NAMESPACE tags them with the configuration they
// were built with, so that two differently-configured Asios cannot silently
// share a mangled name. Declaring io_context in plain boost::asio is then a
// second, different type, and every use of boost::asio::io_context becomes
// ambiguous.
//
// Declare it in whichever namespace Asio itself would use. Asio's config
// header defines the namespace macros - as empty, when the feature is off - so
// pulling it in first both answers the question and costs far less than the
// io_context.hpp this declaration exists to avoid. Boost need not be present
// at all: this header is reachable in builds without it, hence __has_include,
// and the macros only exist from 1.91 on, hence the second check.
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
