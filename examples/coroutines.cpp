#include "utils.hpp"

#include <libremidi/libremidi.hpp>

#include <boost/asio/detached.hpp>
#include <boost/cobalt.hpp>

namespace asio = boost::asio;
namespace cobalt = boost::cobalt;

// Could go in a libremidi/cobalt.hpp helper
namespace libremidi
{
struct channel
{
  cobalt::channel<libremidi::message>& impl;
  void operator()(const libremidi::message& message)
  {
    cobalt::spawn(
        impl.get_executor(),
        [&impl = impl, message]() -> cobalt::task<void> { co_await impl.write(message); }(),
        asio::detached);
  }
};
}

cobalt::main co_main(int argc, char** argv)
{
  cobalt::channel<libremidi::message> channel_impl{64};
  libremidi::channel channel{channel_impl};
  libremidi::midi_in midiin{{.on_message = channel}};
  midiin.open_port(*libremidi::midi1::in_default_port());

  for (;;)
  {
    auto msg = co_await channel_impl.read();
    std::cerr << msg << "\n";
  }
  co_return 0;
}
