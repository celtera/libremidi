// Network MIDI 2.0 through midi_in / midi_out, over the loopback interface.

#include "../include_catch.hpp"

#include <libremidi/configurations.hpp>
#include <libremidi/libremidi.hpp>
#include <libremidi/protocols/network_midi2.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>

#include <chrono>
#include <mutex>
#include <random>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
using namespace std::chrono_literals;

// Somewhere in the dynamic range, different on each run and for each test
int free_port()
{
  static std::minstd_rand rng{std::random_device{}()};
  return 49152 + static_cast<int>(rng() % 16000);
}

struct received
{
  std::mutex mutex;
  std::vector<uint32_t> words;

  auto callback()
  {
    return [this](const libremidi::ump& m) {
      std::lock_guard lock{mutex};
      words.insert(words.end(), m.data, m.data + m.size());
    };
  }

  // As it came: SysEx kept, MIDI 1 channel messages left as they are
  libremidi::ump_input_configuration configuration()
  {
    libremidi::ump_input_configuration c;
    c.on_message = callback();
    c.ignore_sysex = false;
    c.midi1_channel_events_to_midi2 = false;
    return c;
  }

  std::vector<uint32_t> wait_for(std::size_t count, std::chrono::milliseconds timeout = 3000ms)
  {
    const auto end = std::chrono::steady_clock::now() + timeout;
    for (;;)
    {
      {
        std::lock_guard lock{mutex};
        if (words.size() >= count || std::chrono::steady_clock::now() > end)
          return words;
      }
      std::this_thread::sleep_for(5ms);
    }
  }
};

libremidi::net_ump::dgram_input_configuration input(int port)
{
  libremidi::net_ump::dgram_input_configuration c;
  c.client_name = "libremidi test input";
  c.protocol = libremidi::net_ump::protocol::MIDI2_UDP;
  c.accept = "127.0.0.1";
  c.host = "127.0.0.1";
  c.port = port;
  return c;
}

libremidi::net_ump::dgram_output_configuration output(int port)
{
  libremidi::net_ump::dgram_output_configuration c;
  c.client_name = "libremidi test output";
  c.protocol = libremidi::net_ump::protocol::MIDI2_UDP;
  c.host = "127.0.0.1";
  c.port = port;
  return c;
}

// There is nothing to enumerate: the address is in the configuration,
// the port only has to name the right API.
template <typename Port>
Port port_of(const auto& midi)
{
  Port p;
  p.api = midi.get_current_api();
  return p;
}

const std::vector<uint32_t> some_ump{
    0x40903C00, 0xFFFF0000,            // MIDI 2 note on
    0x20B00740,                        // MIDI 1 control change
    0x30160102, 0x03040506,            // SysEx7, start
    0x30330708, 0x09000000,            // SysEx7, end
    0x40803C00, 0x00000000,            // MIDI 2 note off
};
}

TEST_CASE("an output that invites an input that hosts", "[network_midi2][api]")
{
  const int port = free_port();
  received r;
  libremidi::midi_in in{r.configuration(), input(port)};
  REQUIRE(in.open_virtual_port("host") == stdx::error{});

  libremidi::midi_out out{{}, output(port)};
  REQUIRE(out.open_port(port_of<libremidi::output_port>(out), "client") == stdx::error{});

  // open_port has waited for the session: this goes through at once
  REQUIRE(out.send_ump(some_ump.data(), some_ump.size()) == stdx::error{});
  CHECK(r.wait_for(some_ump.size()) == some_ump);

  // One packet at a time as well
  REQUIRE(out.send_ump(0x40903D00u, 0x12340000u) == stdx::error{});
  CHECK(r.wait_for(some_ump.size() + 2).size() == some_ump.size() + 2);

  CHECK(out.close_port() == stdx::error{});
  CHECK(in.close_port() == stdx::error{});
}

TEST_CASE("an input that invites an output that hosts", "[network_midi2][api]")
{
  const int port = free_port();
  libremidi::midi_out out{{}, output(port)};
  REQUIRE(out.open_virtual_port("host") == stdx::error{});

  // Nobody is there yet
  CHECK(out.send_ump(some_ump.data(), some_ump.size()) != stdx::error{});

  received r;
  libremidi::midi_in in{r.configuration(), input(port)};
  REQUIRE(in.open_port(port_of<libremidi::input_port>(in), "client") == stdx::error{});

  // The input does not wait for the host: try until the session is there
  bool sent = false;
  for (int i = 0; i < 400 && !sent; i++)
  {
    sent = out.send_ump(some_ump.data(), some_ump.size()) == stdx::error{};
    if (!sent)
      std::this_thread::sleep_for(5ms);
  }
  REQUIRE(sent);
  CHECK(r.wait_for(some_ump.size()) == some_ump);
}

TEST_CASE("a host with a secret only lets in those who know it", "[network_midi2][api]")
{
  const int port = free_port();
  received r;
  auto in_conf = input(port);
  in_conf.shared_secret = "open sesame";
  libremidi::midi_in in{r.configuration(), in_conf};
  REQUIRE(in.open_virtual_port("host") == stdx::error{});

  SECTION("the right one")
  {
    auto conf = output(port);
    conf.shared_secret = "open sesame";
    libremidi::midi_out out{{}, conf};
    REQUIRE(out.open_port(port_of<libremidi::output_port>(out), "client") == stdx::error{});
    REQUIRE(out.send_ump(some_ump.data(), some_ump.size()) == stdx::error{});
    CHECK(r.wait_for(some_ump.size()) == some_ump);
  }
  SECTION("a wrong one")
  {
    auto conf = output(port);
    conf.shared_secret = "abracadabra";
    conf.connection_timeout_ms = 500;
    libremidi::midi_out out{{}, conf};
    CHECK(out.open_port(port_of<libremidi::output_port>(out), "client") != stdx::error{});
    CHECK(out.send_ump(some_ump.data(), some_ump.size()) != stdx::error{});
    CHECK(r.wait_for(1, 100ms).empty());
  }
  SECTION("none")
  {
    auto conf = output(port);
    conf.connection_timeout_ms = 500;
    libremidi::midi_out out{{}, conf};
    CHECK(out.open_port(port_of<libremidi::output_port>(out), "client") != stdx::error{});
  }
}

TEST_CASE("a host on every interface takes IPv4 and IPv6 clients", "[network_midi2][api]")
{
  const int port = free_port();
  received r;
  auto in_conf = input(port);
  in_conf.accept = "0.0.0.0";
  libremidi::midi_in in{r.configuration(), in_conf};
  REQUIRE(in.open_virtual_port("host") == stdx::error{});

  std::size_t expected = 0;
  for (const char* address : {"127.0.0.1", "::1"})
  {
    auto conf = output(port);
    conf.host = address;
    conf.connection_timeout_ms = 1000;
    libremidi::midi_out out{{}, conf};
    if (out.open_port(port_of<libremidi::output_port>(out), "client") != stdx::error{})
    {
      // Machines without IPv6 exist; none without IPv4 runs these tests
      REQUIRE(std::string_view{address} == "::1");
      WARN("no IPv6 loopback here");
      continue;
    }
    REQUIRE(out.send_ump(some_ump.data(), some_ump.size()) == stdx::error{});
    expected += some_ump.size();
    CHECK(r.wait_for(expected).size() == expected);
  }
}

TEST_CASE("nobody at the other end: the output says so", "[network_midi2][api]")
{
  auto conf = output(free_port());
  conf.connection_timeout_ms = 200;
  libremidi::midi_out out{{}, conf};
  CHECK(out.open_port(port_of<libremidi::output_port>(out), "client") != stdx::error{});
  CHECK(out.send_ump(some_ump.data(), some_ump.size()) != stdx::error{});
}

TEST_CASE("an output finds its host again after the host went away", "[network_midi2][api]")
{
  const int port = free_port();
  libremidi::midi_out out{{}, output(port)};

  {
    received r;
    libremidi::midi_in in{r.configuration(), input(port)};
    REQUIRE(in.open_virtual_port("host") == stdx::error{});
    REQUIRE(out.open_port(port_of<libremidi::output_port>(out), "client") == stdx::error{});
    REQUIRE(out.send_ump(some_ump.data(), some_ump.size()) == stdx::error{});
    CHECK(r.wait_for(some_ump.size()) == some_ump);
  }

  // The host said goodbye: nowhere to send to
  bool refused = false;
  for (int i = 0; i < 200 && !refused; i++)
  {
    refused = out.send_ump(some_ump.data(), some_ump.size()) != stdx::error{};
    if (!refused)
      std::this_thread::sleep_for(5ms);
  }
  REQUIRE(refused);

  // Another one comes up at the same place: the output invites it by itself
  received r;
  libremidi::midi_in in{r.configuration(), input(port)};
  REQUIRE(in.open_virtual_port("host") == stdx::error{});

  bool sent = false;
  for (int i = 0; i < 1500 && !sent; i++)
  {
    sent = out.send_ump(some_ump.data(), some_ump.size()) == stdx::error{};
    if (!sent)
      std::this_thread::sleep_for(10ms);
  }
  REQUIRE(sent);
  CHECK(r.wait_for(some_ump.size()) == some_ump);
}

TEST_CASE("invitations from nowhere do not keep real clients out", "[network_midi2][api]")
{
  const int port = free_port();
  received r;
  libremidi::midi_in in{r.configuration(), input(port)};
  REQUIRE(in.open_virtual_port("host") == stdx::error{});

  // More invitations than a host has room for, each from its own address,
  // none of them ever followed by anything
  namespace asio = boost::asio;
  using udp = asio::ip::udp;
  asio::io_context ctx;
  libremidi::network_midi2::datagram_writer invitation;
  invitation.identity(libremidi::network_midi2::command::invitation, 0, "nobody", "nobody");
  const udp::endpoint host{asio::ip::make_address("127.0.0.1"), static_cast<unsigned short>(port)};
  std::vector<udp::socket> strangers;
  for (int i = 0; i < 40; i++)
  {
    strangers.emplace_back(ctx, udp::endpoint{asio::ip::make_address("127.0.0.1"), 0});
    strangers.back().send_to(asio::buffer(invitation.bytes), host);
  }
  std::this_thread::sleep_for(100ms);

  libremidi::midi_out out{{}, output(port)};
  REQUIRE(out.open_port(port_of<libremidi::output_port>(out), "client") == stdx::error{});
  REQUIRE(out.send_ump(some_ump.data(), some_ump.size()) == stdx::error{});
  CHECK(r.wait_for(some_ump.size()) == some_ump);
}

TEST_CASE("two hosts cannot have the same port", "[network_midi2][api]")
{
  const int port = free_port();
  received r;
  libremidi::midi_in first{r.configuration(), input(port)};
  REQUIRE(first.open_virtual_port("host") == stdx::error{});

  libremidi::midi_in second{r.configuration(), input(port)};
  CHECK(second.open_virtual_port("host") != stdx::error{});
}
