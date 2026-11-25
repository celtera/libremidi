#include "../include_catch.hpp"

#include <libremidi/protocols/network_midi2.hpp>

#include <deque>
#include <random>
#include <vector>

namespace nm2 = libremidi::network_midi2;

namespace
{
using bytes = std::vector<uint8_t>;

// Two sessions and what is in flight between them.
// Not "link": that is a POSIX function.
struct test_link
{
  struct datagram
  {
    bytes data;
    uint64_t arrival;
  };

  nm2::session host;
  nm2::session client;
  std::deque<datagram> to_host, to_client;
  std::vector<uint32_t> host_received, client_received;
  std::vector<bytes> sent_by_client, sent_by_host;
  uint64_t now = 1000;

  // Network conditions
  std::minstd_rand rng{1234};
  double loss = 0.;
  double duplication = 0.;
  uint32_t jitter = 0;

  explicit test_link(nm2::session_configuration host_conf = {}, nm2::session_configuration client_conf = {})
      : host{nm2::role::host, std::move(host_conf)}
      , client{nm2::role::client, std::move(client_conf)}
  {
    host.send_datagram = [this](std::span<const uint8_t> d) {
      sent_by_host.emplace_back(d.begin(), d.end());
      transmit(to_client, d);
    };
    client.send_datagram = [this](std::span<const uint8_t> d) {
      sent_by_client.emplace_back(d.begin(), d.end());
      transmit(to_host, d);
    };
    host.on_ump = [this](std::span<const uint32_t> w) {
      host_received.insert(host_received.end(), w.begin(), w.end());
    };
    client.on_ump = [this](std::span<const uint32_t> w) {
      client_received.insert(client_received.end(), w.begin(), w.end());
    };
  }

  double chance() { return std::uniform_real_distribution<double>{0., 1.}(rng); }

  void transmit(std::deque<datagram>& queue, std::span<const uint8_t> d)
  {
    if (chance() < loss)
      return;
    const int copies = chance() < duplication ? 2 : 1;
    for (int i = 0; i < copies; i++)
    {
      const uint64_t delay = jitter ? rng() % (jitter + 1) : 0;
      queue.push_back({bytes(d.begin(), d.end()), now + delay});
    }
  }

  void deliver(std::deque<datagram>& queue, nm2::session& to)
  {
    // What has arrived by now, in order of arrival
    for (;;)
    {
      auto it = queue.end();
      for (auto i = queue.begin(); i != queue.end(); ++i)
        if (i->arrival <= now && (it == queue.end() || i->arrival < it->arrival))
          it = i;
      if (it == queue.end())
        break;
      auto d = std::move(it->data);
      queue.erase(it);
      to.on_datagram(d, now);
    }
  }

  void run(uint64_t milliseconds)
  {
    for (uint64_t end = now + milliseconds; now < end; now++)
    {
      deliver(to_host, host);
      deliver(to_client, client);
      host.tick(now);
      client.tick(now);
    }
  }

  void connect()
  {
    client.invite(now);
    run(50);
  }
};

std::vector<uint32_t> notes(int count)
{
  std::vector<uint32_t> words;
  for (int i = 0; i < count; i++)
  {
    words.push_back(0x40903C00u | (uint32_t(i) & 0x7F));
    words.push_back(uint32_t(i));
  }
  return words;
}

const uint8_t M = 0x4D, I = 0x49, D = 0x44;
}

TEST_CASE("SHA-256 test vectors", "[network_midi2]")
{
  const auto hex = [](const nm2::sha256::digest& d) {
    std::string s;
    for (uint8_t b : d)
    {
      s += "0123456789abcdef"[b >> 4];
      s += "0123456789abcdef"[b & 15];
    }
    return s;
  };
  const auto of = [&](std::string_view text) {
    return hex(nm2::sha256::hash(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size())));
  };

  // FIPS 180-4 examples
  CHECK(of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(
      of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
      == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("the invitation has the layout of the specification", "[network_midi2]")
{
  test_link l{{}, {.endpoint_name = "Synth", .product_instance_id = "ABC-1"}};
  l.loss = 1.;
  l.client.invite(l.now);

  REQUIRE(l.sent_by_client.size() == 1);
  // Invitation, 4 payload words, name on 2 words, no capability
  const bytes expected{M,    I,    D,    I,    0x01, 4,    2,    0,    0x53, 0x79, 0x6E, 0x74,
                       0x68, 0,    0,    0,    0x41, 0x42, 0x43, 0x2D, 0x31, 0,    0,    0};
  CHECK(l.sent_by_client[0] == expected);
}

TEST_CASE("a UMP Data command carries its sequence number and big-endian words", "[network_midi2]")
{
  test_link l{{}, {.forward_error_correction = 0}};
  l.connect();
  REQUIRE(l.client.established());
  l.sent_by_client.clear();

  const uint32_t ump[2] = {0x40903C00, 0x12345678};
  REQUIRE(l.client.send_ump(ump, 2, l.now) == stdx::error{});
  REQUIRE(l.sent_by_client.size() == 1);
  CHECK(
      l.sent_by_client[0]
      == bytes{M, I, D, I, 0xFF, 2, 0, 0, 0x40, 0x90, 0x3C, 0x00, 0x12, 0x34, 0x56, 0x78});

  REQUIRE(l.client.send_ump(ump, 2, l.now) == stdx::error{});
  CHECK(l.sent_by_client[1][7] == 1);
}

TEST_CASE("each datagram repeats the previous commands", "[network_midi2]")
{
  test_link l;
  l.connect();
  l.sent_by_client.clear();

  for (uint32_t i = 0; i < 3; i++)
  {
    const uint32_t ump = 0x20903C00 | i;
    l.client.send_ump(&ump, 1, l.now);
  }
  // "MIDI" + commands of 8 bytes: 1, then 2, then 3 (two repeats and the new one)
  REQUIRE(l.sent_by_client.size() == 3);
  CHECK(l.sent_by_client[0].size() == 4 + 8);
  CHECK(l.sent_by_client[1].size() == 4 + 16);
  CHECK(l.sent_by_client[2].size() == 4 + 24);
  // Oldest first
  CHECK(l.sent_by_client[2][7] == 0);
  CHECK(l.sent_by_client[2][15] == 1);
  CHECK(l.sent_by_client[2][23] == 2);
}

TEST_CASE("a session opens, carries UMP both ways and closes", "[network_midi2]")
{
  test_link l{{.endpoint_name = "The host", .product_instance_id = "H1"},
         {.endpoint_name = "The client", .product_instance_id = "C1"}};
  l.connect();

  REQUIRE(l.host.established());
  REQUIRE(l.client.established());
  CHECK(l.host.remote_endpoint_name() == "The client");
  CHECK(l.host.remote_product_instance_id() == "C1");
  CHECK(l.client.remote_endpoint_name() == "The host");
  CHECK(l.client.remote_product_instance_id() == "H1");

  const auto sent = notes(500);
  REQUIRE(l.client.send_ump(sent.data(), sent.size(), l.now) == stdx::error{});
  REQUIRE(l.host.send_ump(sent.data(), 20, l.now) == stdx::error{});
  l.run(50);
  CHECK(l.host_received == sent);
  CHECK(l.client_received == std::vector<uint32_t>(sent.begin(), sent.begin() + 20));

  l.client.close(l.now);
  l.run(50);
  CHECK(l.client.state() == nm2::session_state::closed);
  CHECK(l.host.state() == nm2::session_state::closed);
  CHECK(l.host.closed_by_peer());
  CHECK(l.host.close_reason() == nm2::bye_reason::user_terminated);

  // And the host takes a new invitation afterwards
  l.connect();
  CHECK(l.host.established());
  CHECK(l.client.established());
}

TEST_CASE("sending needs an established session and whole packets", "[network_midi2]")
{
  test_link l;
  const uint32_t ump[2] = {0x40903C00, 0};
  CHECK(l.client.send_ump(ump, 2, l.now) == std::errc::not_connected);
  l.connect();
  CHECK(l.client.send_ump(ump, 1, l.now) == std::errc::bad_message);
  l.run(20);
  CHECK(l.host_received.empty());
}

TEST_CASE("a shared secret is checked by the host", "[network_midi2]")
{
  SECTION("the right one")
  {
    test_link l{{.shared_secret = "opensesame"}, {.shared_secret = "opensesame"}};
    l.connect();
    CHECK(l.host.established());
    CHECK(l.client.established());
  }
  SECTION("the wrong one")
  {
    test_link l{{.shared_secret = "opensesame"}, {.shared_secret = "letmein"}};
    l.connect();
    CHECK_FALSE(l.host.established());
    CHECK(l.client.state() == nm2::session_state::closed);
    CHECK(l.client.close_reason() == nm2::bye_reason::authentication_failed);
  }
  SECTION("none on the client")
  {
    test_link l{{.shared_secret = "opensesame"}, {}};
    l.connect();
    CHECK_FALSE(l.host.established());
    CHECK(l.client.state() == nm2::session_state::closed);
  }
  SECTION("none asked by the host")
  {
    test_link l{{}, {.shared_secret = "unused"}};
    l.connect();
    CHECK(l.host.established());
    CHECK(l.client.established());
  }
}

TEST_CASE("the invitation is repeated until the host answers", "[network_midi2]")
{
  SECTION("then accepted")
  {
    test_link l;
    l.loss = 1.;
    l.client.invite(l.now);
    l.run(2500);
    CHECK(l.client.state() == nm2::session_state::inviting);
    CHECK(l.sent_by_client.size() == 3);

    l.loss = 0.;
    l.run(1100);
    CHECK(l.client.established());
  }
  SECTION("or given up after a while")
  {
    test_link l;
    l.loss = 1.;
    l.client.invite(l.now);
    l.run(11000);
    CHECK(l.client.state() == nm2::session_state::closed);
    CHECK(l.client.close_reason() == nm2::bye_reason::timeout);
  }
}

TEST_CASE("lost, repeated and reordered datagrams do not show in the stream", "[network_midi2]")
{
  // Enough attempts that nothing is given up on: see the next test for that case
  test_link l{{.retransmit_attempts = 20}, {.retransmit_attempts = 20}};
  l.connect();
  REQUIRE(l.client.established());

  l.loss = 0.2;
  l.duplication = 0.2;
  l.jitter = 5;

  // A few notes per millisecond for a while, both ways
  std::vector<uint32_t> sent_up, sent_down;
  for (int i = 0; i < 2000; i++)
  {
    const auto up = notes(1 + i % 3);
    l.client.send_ump(up.data(), up.size(), l.now);
    sent_up.insert(sent_up.end(), up.begin(), up.end());

    const uint32_t down[1] = {0x20B00000u | uint32_t(i & 0x7F)};
    l.host.send_ump(down, 1, l.now);
    sent_down.push_back(down[0]);

    l.run(1);
  }
  l.loss = 0.;
  l.run(1000);

  CHECK(l.host_received.size() == sent_up.size());
  CHECK(l.client_received.size() == sent_down.size());
  // Not CHECK(a == b): a failure would print megabytes
  CHECK(bool(l.host_received == sent_up));
  CHECK(bool(l.client_received == sent_down));
}

TEST_CASE("what cannot be recovered is skipped, and the stream goes on", "[network_midi2]")
{
  // No repeats, and a buffer too small to answer the retransmit request
  test_link l{{}, {.forward_error_correction = 0, .retransmit_buffer_commands = 1}};
  l.connect();

  const uint32_t a = 0x20903C01, b = 0x20903C02, c = 0x20903C03, d = 0x20903C04;
  l.client.send_ump(&a, 1, l.now);
  l.run(5);
  l.loss = 1.;
  l.client.send_ump(&b, 1, l.now);
  l.loss = 0.;
  l.client.send_ump(&c, 1, l.now);
  l.client.send_ump(&d, 1, l.now);
  l.run(200);

  CHECK(l.host_received == std::vector<uint32_t>{a, c, d});
}

TEST_CASE("a silent peer is pinged, then the session times out", "[network_midi2]")
{
  test_link l;
  l.connect();
  l.sent_by_host.clear();

  SECTION("a peer that answers stays connected")
  {
    l.run(65000);
    CHECK(l.host.established());
    CHECK(l.client.established());
  }
  SECTION("a peer that is gone does not")
  {
    l.loss = 1.;
    l.run(31000);
    CHECK(l.host.state() == nm2::session_state::closed);
    CHECK(l.host.close_reason() == nm2::bye_reason::timeout);
    CHECK(l.client.state() == nm2::session_state::closed);
    // It was asked first
    bool pinged = false;
    for (const auto& d : l.sent_by_host)
      pinged |= d.size() >= 12 && d[4] == 0x20;
    CHECK(pinged);
  }
}

TEST_CASE("pings are answered with their identifier", "[network_midi2]")
{
  test_link l;
  l.connect();
  l.sent_by_host.clear();

  const bytes ping{M, I, D, I, 0x20, 1, 0, 0, 0xDE, 0xAD, 0xBE, 0xEF};
  l.host.on_datagram(ping, l.now);
  REQUIRE(l.sent_by_host.size() == 1);
  CHECK(l.sent_by_host[0] == bytes{M, I, D, I, 0x21, 1, 0, 0, 0xDE, 0xAD, 0xBE, 0xEF});
}

TEST_CASE("an unknown command is refused with a NAK that quotes it", "[network_midi2]")
{
  test_link l;
  l.connect();
  l.sent_by_host.clear();

  const bytes unknown{M, I, D, I, 0x55, 1, 0xAA, 0xBB, 1, 2, 3, 4};
  l.host.on_datagram(unknown, l.now);
  REQUIRE(l.sent_by_host.size() == 1);
  CHECK(l.sent_by_host[0] == bytes{M, I, D, I, 0x8F, 1, 0x01, 0, 0x55, 1, 0xAA, 0xBB});
  CHECK(l.host.established());
}

TEST_CASE("what is not Network MIDI 2.0 is ignored", "[network_midi2]")
{
  test_link l;
  l.connect();
  l.sent_by_host.clear();

  l.host.on_datagram(bytes{0x2F, 0x6D, 0x69, 0x64, 0x69, 0, 0, 0}, l.now);
  l.host.on_datagram(bytes{}, l.now);
  // Cut in the middle of a command
  l.host.on_datagram(bytes{M, I, D, I, 0xFF, 2, 0, 0, 0x40, 0x90}, l.now);
  CHECK(l.sent_by_host.empty());
  CHECK(l.host_received.empty());
  CHECK(l.host.established());
}

TEST_CASE("UMP data outside a session is answered with Bye", "[network_midi2]")
{
  test_link l;
  const bytes data{M, I, D, I, 0xFF, 1, 0, 0, 0x20, 0x90, 0x3C, 0x40};
  l.host.on_datagram(data, l.now);
  REQUIRE(l.sent_by_host.size() == 1);
  CHECK(l.sent_by_host[0] == bytes{M, I, D, I, 0xF0, 0, 0x05, 0});
  CHECK(l.host_received.empty());
}

TEST_CASE("a ping reply nobody asked for is refused", "[network_midi2]")
{
  test_link l;
  l.connect();
  l.sent_by_host.clear();

  const bytes reply{M, I, D, I, 0x21, 1, 0, 0, 0, 0, 0, 0x99};
  l.host.on_datagram(reply, l.now);
  REQUIRE(l.sent_by_host.size() == 1);
  CHECK(l.sent_by_host[0] == bytes{M, I, D, I, 0x8F, 1, 0x20, 0, 0x21, 1, 0, 0});
  CHECK(l.host.established());
}

TEST_CASE("retransmit requests are answered with what they name", "[network_midi2]")
{
  test_link l{{.forward_error_correction = 0}, {}};
  l.connect();
  for (uint32_t i = 0; i < 4; i++)
  {
    const uint32_t ump = 0x20903C00 | i;
    l.host.send_ump(&ump, 1, l.now);
  }
  l.run(5);
  l.sent_by_host.clear();

  SECTION("no count: that command")
  {
    const bytes request{M, I, D, I, 0x80, 0, 0, 2};
    l.host.on_datagram(request, l.now);
    REQUIRE(l.sent_by_host.size() == 1);
    CHECK(l.sent_by_host[0] == bytes{M, I, D, I, 0xFF, 1, 0, 2, 0x20, 0x90, 0x3C, 2});
  }
  SECTION("a count: that many")
  {
    const bytes request{M, I, D, I, 0x80, 1, 0, 1, 0, 2, 0, 0};
    l.host.on_datagram(request, l.now);
    REQUIRE(l.sent_by_host.size() == 1);
    CHECK(
        l.sent_by_host[0]
        == bytes{M, I, D, I, 0xFF, 1, 0, 1, 0x20, 0x90, 0x3C, 1, 0xFF, 1, 0, 2, 0x20, 0x90, 0x3C, 2});
  }
  SECTION("a count of zero: everything from there on")
  {
    const bytes request{M, I, D, I, 0x80, 1, 0, 2, 0, 0, 0, 0};
    l.host.on_datagram(request, l.now);
    REQUIRE(l.sent_by_host.size() == 1);
    CHECK(
        l.sent_by_host[0]
        == bytes{M, I, D, I, 0xFF, 1, 0, 2, 0x20, 0x90, 0x3C, 2, 0xFF, 1, 0, 3, 0x20, 0x90, 0x3C, 3});
  }
  SECTION("what is not there anymore: an error naming what is")
  {
    const bytes request{M, I, D, I, 0x80, 1, 0x12, 0x34, 0, 1, 0, 0};
    l.host.on_datagram(request, l.now);
    REQUIRE(l.sent_by_host.size() == 1);
    CHECK(l.sent_by_host[0] == bytes{M, I, D, I, 0x81, 1, 1, 0, 0, 0, 0, 0});
  }
}

TEST_CASE("every missing command is asked for by name, in one datagram", "[network_midi2]")
{
  test_link l{{}, {.forward_error_correction = 0}};
  l.connect();
  l.sent_by_host.clear();

  // 0 arrives, 1 to 3 are lost, 4 arrives
  const uint32_t ump = 0x20903C00;
  l.client.send_ump(&ump, 1, l.now);
  l.loss = 1.;
  for (int i = 0; i < 3; i++)
    l.client.send_ump(&ump, 1, l.now);
  l.loss = 0.;
  l.client.send_ump(&ump, 1, l.now);

  // Until the host asks
  for (int i = 0; i < 30 && l.sent_by_host.empty(); i++)
  {
    l.deliver(l.to_host, l.host);
    l.host.tick(l.now++);
  }
  REQUIRE(!l.sent_by_host.empty());
  CHECK(
      l.sent_by_host[0]
      == bytes{M,    I, D, I, 0x80, 1, 0, 1, 0, 1, 0, 0, 0x80, 1, 0, 2,
               0,    1, 0, 0, 0x80, 1, 0, 3, 0, 1, 0, 0});

  l.run(50);
  CHECK(l.host_received.size() == 5);
}

TEST_CASE("a gap that is being filled is not given up on", "[network_midi2]")
{
  test_link l;
  l.connect();
  l.sent_by_host.clear();

  const auto data = [](uint8_t sequence) {
    return bytes{M, I, D, I, 0xFF, 1, 0, sequence, 0x20, 0x90, 0x3C, sequence};
  };

  // 0 arrives, 1 to 6 are lost, 7 arrives: more missing than there are attempts
  l.host.on_datagram(data(0), l.now);
  l.host.on_datagram(data(7), l.now);

  // A peer that resends one command per datagram of requests, whatever was asked
  uint8_t next = 1;
  for (int i = 0; i < 500 && next < 7; i++)
  {
    l.host.tick(l.now++);
    if (!l.sent_by_host.empty())
    {
      l.sent_by_host.clear();
      l.host.on_datagram(data(next++), l.now);
    }
  }

  CHECK(
      l.host_received
      == std::vector<uint32_t>{0x20903C00, 0x20903C01, 0x20903C02, 0x20903C03, 0x20903C04,
                               0x20903C05, 0x20903C06, 0x20903C07});
}

TEST_CASE("names always end with a zero", "[network_midi2]")
{
  // Four bytes exactly: a fifth, zero, has to follow
  test_link l{{}, {.endpoint_name = "ABCD", .product_instance_id = "1234"}};
  l.loss = 1.;
  l.client.invite(l.now);

  REQUIRE(l.sent_by_client.size() == 1);
  CHECK(
      l.sent_by_client[0]
      == bytes{M,    I,    D,    I,    0x01, 4, 2, 0, 0x41, 0x42, 0x43, 0x44, 0, 0, 0, 0,
               0x31, 0x32, 0x33, 0x34, 0,    0, 0, 0});
}

TEST_CASE("a host with a secret asks for it whatever the client announces", "[network_midi2]")
{
  test_link l{{.shared_secret = "opensesame"}, {}};
  l.sent_by_host.clear();

  // An invitation that announces no capability
  const bytes invitation{M, I, D, I, 0x01, 2, 1, 0, 0x41, 0, 0, 0, 0x42, 0, 0, 0};
  l.host.on_datagram(invitation, l.now);
  REQUIRE(l.sent_by_host.size() == 1);
  CHECK(l.sent_by_host[0][4] == 0x12);
  CHECK(l.host.state() == nm2::session_state::authenticating);
}

TEST_CASE("a side that only listens still shows that it is there", "[network_midi2]")
{
  test_link l;
  l.connect();
  l.sent_by_client.clear();

  // The host sends data all along; the client has nothing to say
  for (int i = 0; i < 250; i++)
  {
    const uint32_t ump = 0x20903C00;
    l.host.send_ump(&ump, 1, l.now);
    l.run(100);
  }

  int pings = 0;
  for (const auto& d : l.sent_by_client)
    pings += d.size() >= 12 && d[4] == 0x20;
  CHECK(pings >= 2);
  CHECK(pings <= 3);
  CHECK(l.host.established());
  CHECK(l.client.established());
}

// ---- What a peer, or somebody writing its address on datagrams, can do

namespace
{
// One UMP Data command with one word that tells its sequence number
bytes data_command(uint16_t sequence)
{
  const auto hi = static_cast<uint8_t>(sequence >> 8);
  const auto lo = static_cast<uint8_t>(sequence);
  return bytes{M, I, D, I, 0xFF, 1, hi, lo, 0x20, 0x90, hi, lo};
}

std::size_t total_bytes(const std::vector<bytes>& datagrams)
{
  std::size_t n = 0;
  for (const auto& d : datagrams)
    n += d.size();
  return n;
}

bool has_command(const std::vector<bytes>& datagrams, uint8_t code)
{
  for (const auto& d : datagrams)
  {
    nm2::datagram_reader reader{d};
    nm2::command_view cmd;
    while (reader.next(cmd))
      if (static_cast<uint8_t>(cmd.code) == code)
        return true;
  }
  return false;
}
}

TEST_CASE("retransmit requests cannot be made to send without end", "[network_midi2][hostile]")
{
  test_link l;
  l.connect();
  const auto words = notes(32);
  for (int i = 0; i < 128; i++)
    l.host.send_ump(words.data(), words.size(), l.now);
  l.run(5);
  l.sent_by_host.clear();

  // One datagram, eight thousand times "everything from the start on"
  bytes request{M, I, D, I};
  for (int i = 0; i < 8000; i++)
    request.insert(request.end(), {0x80, 1, 0, 0, 0, 0, 0, 0});
  l.host.on_datagram(request, l.now);

  // The buffer twice at most, not eight thousand times
  CHECK(total_bytes(l.sent_by_host) <= 2 * 128 * (4 + 64 * 4) + 4 * 64);

  // And the allowance comes back with time
  l.sent_by_host.clear();
  l.now += 1000;
  const bytes one{M, I, D, I, 0x80, 1, 0, 5, 0, 1, 0, 0};
  l.host.on_datagram(one, l.now);
  REQUIRE(l.sent_by_host.size() == 1);
  CHECK(l.sent_by_host[0][4] == 0xFF);
}

TEST_CASE("the answer to a datagram stays one datagram", "[network_midi2][hostile]")
{
  test_link l;
  l.connect();
  l.sent_by_host.clear();

  // Sixteen thousand commands nobody knows
  bytes junk{M, I, D, I};
  for (int i = 0; i < 16000; i++)
    junk.insert(junk.end(), {0x55, 0, 0, 0});
  l.host.on_datagram(junk, l.now);

  REQUIRE(l.sent_by_host.size() == 1);
  CHECK(l.sent_by_host[0].size() <= nm2::max_datagram_bytes + 8);
  CHECK(l.host.established());
}

TEST_CASE("a sequence number from far ahead is not waited for", "[network_midi2][hostile]")
{
  test_link l;
  l.connect();

  l.host.on_datagram(data_command(0x5000), l.now);
  l.run(200);
  CHECK(l.host_received.empty());

  // The stream is where it was
  l.host.on_datagram(data_command(0), l.now);
  l.run(5);
  CHECK(l.host_received == std::vector<uint32_t>{0x20900000});
}

TEST_CASE("a stream that lost its place is started again", "[network_midi2][hostile]")
{
  test_link l;
  l.connect();

  // Close enough to be waited for, then skipped to: the host is now ahead of its client
  l.host.on_datagram(data_command(1000), l.now);
  l.run(200);
  REQUIRE(l.host_received == std::vector<uint32_t>{0x209003E8});
  l.host_received.clear();
  l.sent_by_host.clear();

  // The client knows nothing of it and goes on from zero
  for (uint32_t i = 0; i < 300; i++)
  {
    const uint32_t ump = 0x20800000 | i;
    l.client.send_ump(&ump, 1, l.now);
    l.run(2);
  }
  l.run(500);

  // The host asked to start again, and what was sent after that arrived
  CHECK(has_command(l.sent_by_host, 0x82));
  REQUIRE(!l.host_received.empty());
  CHECK(l.host_received.back() == (0x20800000u | 299));
  CHECK(l.host_received.size() >= 200);
  CHECK(l.host.established());
  CHECK(l.client.established());
}

TEST_CASE("a client that starts again from the same address is heard", "[network_midi2]")
{
  test_link l;
  l.connect();
  const auto invitation = l.sent_by_client.at(0);

  const auto words = notes(50);
  for (int i = 0; i < 50; i++)
    l.client.send_ump(words.data() + 2 * i, 2, l.now);
  l.run(20);
  REQUIRE(l.host_received == words);
  l.host_received.clear();
  l.sent_by_host.clear();

  // It was killed and started again: no goodbye, the same invitation, and data from zero
  l.host.on_datagram(invitation, l.now);
  REQUIRE(l.sent_by_host.size() == 1);
  CHECK(l.sent_by_host[0][4] == 0x10);
  CHECK(has_command(l.sent_by_host, 0x82));
  CHECK(l.host.established());

  l.host.on_datagram(data_command(0), l.now);
  l.host.tick(l.now);
  CHECK(l.host_received == std::vector<uint32_t>{0x20900000});
}

TEST_CASE("an invitation that comes twice does not disturb a session without data", "[network_midi2]")
{
  test_link l;
  l.connect();
  const auto invitation = l.sent_by_client.at(0);
  l.sent_by_host.clear();

  l.host.on_datagram(invitation, l.now);
  REQUIRE(l.sent_by_host.size() == 1);
  CHECK(l.sent_by_host[0][4] == 0x10);
  CHECK_FALSE(has_command(l.sent_by_host, 0x82));
}

TEST_CASE("a client that is still inviting does not refuse early data", "[network_midi2]")
{
  test_link l;
  l.loss = 1.;
  l.client.invite(l.now);
  l.sent_by_client.clear();

  // The host accepted, its answer was lost, and it already sends
  l.client.on_datagram(data_command(0), l.now);
  CHECK(l.sent_by_client.empty());
  CHECK(l.client.state() == nm2::session_state::inviting);
}

TEST_CASE("a host that said to wait and then went away is given up on", "[network_midi2]")
{
  test_link l;
  l.loss = 1.;
  l.client.invite(l.now);

  const bytes pending{M, I, D, I, 0x11, 0, 0, 0};
  l.client.on_datagram(pending, l.now);

  // Longer than without that answer
  l.run(15000);
  CHECK(l.client.state() == nm2::session_state::inviting);
  // But not forever
  l.run(20000);
  CHECK(l.client.state() == nm2::session_state::closed);
  CHECK(l.client.close_reason() == nm2::bye_reason::timeout);
}

TEST_CASE("every authentication request has its own nonce", "[network_midi2][hostile]")
{
  nm2::datagram_writer invitation;
  invitation.identity(nm2::command::invitation, 1, "somebody", "id");
  bytes wrong{M, I, D, I, 0x02, 8, 0, 0};
  wrong.resize(wrong.size() + 32);

  std::vector<bytes> nonces;
  for (int session = 0; session < 8; session++)
  {
    nm2::session host{nm2::role::host, {.shared_secret = "opensesame"}};
    host.send_datagram = [&](std::span<const uint8_t> d) {
      if (d.size() >= 24 && d[4] == 0x12)
        nonces.emplace_back(d.begin() + 8, d.begin() + 24);
    };
    // The same time for all of them: the clock is not what tells them apart
    host.on_datagram(invitation.bytes, 1000);
    host.on_datagram(wrong, 1000);
    host.on_datagram(wrong, 1000);
  }

  REQUIRE(nonces.size() == 24);
  for (std::size_t a = 0; a < nonces.size(); a++)
  {
    for (std::size_t b = a + 1; b < nonces.size(); b++)
    {
      CHECK(nonces[a] != nonces[b]);
      // Not even a word in common
      for (int w = 0; w < 4; w++)
        CHECK_FALSE(std::equal(
            nonces[a].begin() + 4 * w, nonces[a].begin() + 4 * w + 4, nonces[b].begin() + 4 * w));
    }
  }
}

TEST_CASE("a client that never answers the authentication request is forgotten", "[network_midi2]")
{
  test_link l{{.shared_secret = "opensesame"}, {}};
  nm2::datagram_writer invitation;
  invitation.identity(nm2::command::invitation, 1, "somebody", "id");
  l.host.on_datagram(invitation.bytes, l.now);
  REQUIRE(l.host.state() == nm2::session_state::authenticating);

  for (int i = 0; i < 11000; i++)
    l.host.tick(l.now++);
  CHECK(l.host.state() == nm2::session_state::closed);
}

TEST_CASE("the end of the data survives a burst of loss", "[network_midi2]")
{
  test_link l;
  l.connect();

  const auto words = notes(10);
  for (int i = 0; i < 9; i++)
    l.client.send_ump(words.data() + 2 * i, 2, l.now);
  l.run(5);

  // The last command, and the two empty ones that repeat it, are all lost
  l.loss = 1.;
  l.client.send_ump(words.data() + 18, 2, l.now);
  l.run(300);
  CHECK(l.host_received.size() == 18);

  // The empty commands that still follow show that something is missing
  l.loss = 0.;
  l.run(1000);
  CHECK(l.host_received == words);
}

TEST_CASE("six empty commands follow the last data, then nothing", "[network_midi2]")
{
  test_link l;
  l.connect();
  const uint32_t ump = 0x20903C00;
  l.client.send_ump(&ump, 1, l.now);
  l.sent_by_client.clear();

  // At 250, 260, 510, 1010, 2010 and 4010 ms
  std::vector<uint64_t> at;
  const uint64_t start = l.now;
  for (int i = 0; i < 9000; i++)
  {
    l.run(1);
    if (!l.sent_by_client.empty())
    {
      at.push_back(l.now - 1 - start);
      l.sent_by_client.clear();
    }
  }
  CHECK(at == std::vector<uint64_t>{250, 260, 510, 1010, 2010, 4010});
}

TEST_CASE("a host that started again and answers pings anyway is found out", "[network_midi2]")
{
  test_link l;
  l.connect();
  REQUIRE(l.client.established());

  // A new host at the same address: it knows of no session, but answers pings
  l.host = nm2::session{nm2::role::host, {}};
  l.host.send_datagram = [&l](std::span<const uint8_t> d) { l.transmit(l.to_client, d); };

  l.run(9000);
  CHECK(l.client.established());
  l.run(2000);
  CHECK(l.client.state() == nm2::session_state::closed);
  CHECK(l.client.closed_by_peer());
  CHECK(l.client.close_reason() == nm2::bye_reason::session_not_established);
}

TEST_CASE("a peer that keeps asking for what is gone gets a new start", "[network_midi2]")
{
  test_link l{{.forward_error_correction = 0, .retransmit_buffer_commands = 4}, {}};
  l.connect();
  for (uint32_t i = 0; i < 10; i++)
  {
    const uint32_t ump = 0x20903C00 | i;
    l.host.send_ump(&ump, 1, l.now);
  }
  l.run(5);
  l.sent_by_host.clear();

  const bytes gone{M, I, D, I, 0x80, 1, 0, 2, 0, 0, 0, 0};
  const bytes error{M, I, D, I, 0x81, 1, 1, 0, 0, 6, 0, 0};

  // Told once
  l.host.on_datagram(gone, l.now);
  REQUIRE(l.sent_by_host.size() == 1);
  CHECK(l.sent_by_host[0] == error);

  // Asking again at once is a repeat that crossed our answer
  l.now += 100;
  l.host.on_datagram(gone, l.now);
  REQUIRE(l.sent_by_host.size() == 2);
  CHECK(l.sent_by_host[1] == error);

  // Still asking: it is stuck there
  l.now += 900;
  l.host.on_datagram(gone, l.now);
  REQUIRE(l.sent_by_host.size() == 3);
  CHECK(l.sent_by_host[2] == bytes{M, I, D, I, 0x82, 0, 0, 0});

  SECTION("and sees the new numbering, although we have nothing to send")
  {
    l.sent_by_host.clear();
    for (int i = 0; i < 300; i++)
      l.host.tick(l.now++);
    REQUIRE(l.sent_by_host.size() == 1);
    CHECK(l.sent_by_host[0] == bytes{M, I, D, I, 0xFF, 0, 0, 0});
  }

  SECTION("what was never sent is no reason for it")
  {
    l.sent_by_host.clear();
    const bytes future{M, I, D, I, 0x80, 1, 0x01, 0x00, 0, 1, 0, 0};
    for (int i = 0; i < 5; i++)
    {
      l.host.on_datagram(future, l.now);
      l.now += 1000;
    }
    CHECK_FALSE(has_command(l.sent_by_host, 0x82));
  }
}

TEST_CASE("empty commands follow a retransmission as they follow data", "[network_midi2]")
{
  test_link l{{.forward_error_correction = 0}, {}};
  l.connect();
  const uint32_t ump = 0x20903C00;
  for (int i = 0; i < 4; i++)
    l.host.send_ump(&ump, 1, l.now);
  // The six that follow the data are long gone
  l.run(9000);
  l.sent_by_host.clear();

  const bytes request{M, I, D, I, 0x80, 0, 0, 2};
  l.host.on_datagram(request, l.now);
  REQUIRE(l.sent_by_host.size() == 1);

  // Should that answer be lost, a later sequence number tells the peer so
  for (int i = 0; i < 300; i++)
    l.host.tick(l.now++);
  REQUIRE(l.sent_by_host.size() >= 2);
  CHECK(l.sent_by_host[1] == bytes{M, I, D, I, 0xFF, 0, 0, 10});
}
