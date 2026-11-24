#pragma once

#include <libremidi/config.hpp>
#include <libremidi/error.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Network MIDI 2.0 (UDP)
// MIDI Association, M2-124-UM "User Datagram Protocol for Universal MIDI Packets", v1.0
//
// This file is the protocol itself, without any I/O: datagrams go in through
// session::on_datagram, come out through session::send_datagram, and time is
// given by the caller. The network backend drives it with sockets and timers;
// it can as well be driven by hand on platforms that have neither.
NAMESPACE_LIBREMIDI::network_midi2
{
// DNS-SD service type under which hosts are advertised
inline constexpr std::string_view dns_sd_service_type = "_midi2._udp";

// Every datagram starts with "MIDI"
inline constexpr std::array<uint8_t, 4> signature{0x4D, 0x49, 0x44, 0x49};

// A UMP Data command carries at most this many 32-bit words
inline constexpr std::size_t max_ump_words_per_command = 64;

// What we put at most in one datagram: fits an Ethernet frame without fragmentation
inline constexpr std::size_t max_datagram_bytes = 1400;

enum class command : uint8_t
{
  invitation = 0x01,
  invitation_with_authentication = 0x02,
  invitation_with_user_authentication = 0x03,
  invitation_reply_accepted = 0x10,
  invitation_reply_pending = 0x11,
  invitation_reply_authentication_required = 0x12,
  invitation_reply_user_authentication_required = 0x13,
  ping = 0x20,
  ping_reply = 0x21,
  retransmit_request = 0x80,
  retransmit_error = 0x81,
  session_reset = 0x82,
  session_reset_reply = 0x83,
  nak = 0x8F,
  bye = 0xF0,
  bye_reply = 0xF1,
  ump_data = 0xFF,
};

enum class bye_reason : uint8_t
{
  unknown = 0x00,
  user_terminated = 0x01,
  power_down = 0x02,
  too_many_missing_packets = 0x03,
  timeout = 0x04,
  session_not_established = 0x05,
  no_pending_session = 0x06,
  protocol_error = 0x07,

  // Host to client
  too_many_sessions = 0x40,
  authentication_without_prior_invitation = 0x41,
  rejected_by_user = 0x42,
  authentication_failed = 0x43,
  username_not_found = 0x44,
  no_matching_authentication_method = 0x45,

  // Client to host
  invitation_canceled = 0x80,
};

enum class nak_reason : uint8_t
{
  other = 0x00,
  command_not_supported = 0x01,
  command_not_expected = 0x02,
  command_malformed = 0x03,
  bad_ping_reply = 0x20,
};

// Capabilities announced in an Invitation
enum invitation_capabilities : uint8_t
{
  supports_authentication = 0x01,
  supports_user_authentication = 0x02,
};

enum class role : uint8_t
{
  // Waits for invitations
  host,
  // Sends the invitation
  client,
};

enum class session_state : uint8_t
{
  // Nothing going on: a host waits here, a client has not invited yet
  idle,
  // Client: invitation sent, no answer yet (or the host answered "pending")
  inviting,
  // Host: asked the client to authenticate
  authenticating,
  // UMP can flow
  established,
  // Bye sent, waiting for its reply
  closing,
  // Over: see session::close_reason()
  closed,
};

/// Number of 32-bit words of the UMP that starts with this word
constexpr std::size_t ump_word_count(uint32_t first_word) noexcept
{
  constexpr uint8_t sizes[16] = {1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4};
  return sizes[first_word >> 28];
}

/// Sequence numbers are 16 bits and wrap: a is after b when it is less than half a turn ahead
constexpr bool sequence_after(uint16_t a, uint16_t b) noexcept
{
  return a != b && static_cast<uint16_t>(a - b) < 0x8000;
}

/// SHA-256 (FIPS 180-4), for the authentication digests
struct sha256
{
  using digest = std::array<uint8_t, 32>;

  static digest hash(std::span<const uint8_t> data) noexcept
  {
    static constexpr uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    static constexpr auto rotr
        = [](uint32_t x, int n) constexpr noexcept { return (x >> n) | (x << (32 - n)); };

    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    // Message, then 0x80, zeros, and the length in bits on 64 bits
    std::vector<uint8_t> msg(data.begin(), data.end());
    const uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56)
      msg.push_back(0);
    for (int i = 7; i >= 0; i--)
      msg.push_back(static_cast<uint8_t>(bits >> (8 * i)));

    for (std::size_t block = 0; block < msg.size(); block += 64)
    {
      uint32_t w[64];
      for (int i = 0; i < 16; i++)
      {
        const uint8_t* p = msg.data() + block + 4 * i;
        w[i] = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
      }
      for (int i = 16; i < 64; i++)
      {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
      }

      uint32_t v[8];
      std::copy_n(h, 8, v);
      for (int i = 0; i < 64; i++)
      {
        const uint32_t s1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25);
        const uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
        const uint32_t t1 = v[7] + s1 + ch + k[i] + w[i];
        const uint32_t s0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22);
        const uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        const uint32_t t2 = s0 + maj;
        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + t1;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = t1 + t2;
      }
      for (int i = 0; i < 8; i++)
        h[i] += v[i];
    }

    digest out;
    for (int i = 0; i < 8; i++)
      for (int j = 0; j < 4; j++)
        out[4 * i + j] = static_cast<uint8_t>(h[i] >> (24 - 8 * j));
    return out;
  }

  /// SHA-256 of a nonce followed by text: what a client answers to an authentication request
  static digest hash(std::span<const uint8_t> nonce, std::string_view secret) noexcept
  {
    std::vector<uint8_t> data(nonce.begin(), nonce.end());
    data.insert(data.end(), secret.begin(), secret.end());
    return hash(data);
  }
};

/// One command of a datagram, pointing into it
struct command_view
{
  command code{};
  uint8_t specific_1{};
  uint8_t specific_2{};
  // Always a multiple of four bytes
  std::span<const uint8_t> payload;
  // The four header bytes, as a NAK has to quote them
  std::array<uint8_t, 4> header{};

  uint16_t specific_16() const noexcept { return static_cast<uint16_t>((specific_1 << 8) | specific_2); }
};

/// Goes through the commands of a datagram
struct datagram_reader
{
  std::span<const uint8_t> data;
  std::size_t offset = signature.size();

  explicit datagram_reader(std::span<const uint8_t> datagram) noexcept
      : data{datagram}
  {
  }

  /// False for anything that is not Network MIDI 2.0
  bool valid() const noexcept
  {
    return data.size() >= signature.size()
           && std::equal(signature.begin(), signature.end(), data.begin());
  }

  /// The next command; false at the end, or when what remains is cut short
  bool next(command_view& out) noexcept
  {
    if (offset + 4 > data.size())
      return false;
    const std::size_t payload_bytes = std::size_t{data[offset + 1]} * 4;
    if (offset + 4 + payload_bytes > data.size())
      return false;

    out.code = static_cast<command>(data[offset]);
    out.specific_1 = data[offset + 2];
    out.specific_2 = data[offset + 3];
    out.payload = data.subspan(offset + 4, payload_bytes);
    std::copy_n(data.begin() + offset, 4, out.header.begin());
    offset += 4 + payload_bytes;
    return true;
  }
};

/// Builds a datagram command by command
struct datagram_writer
{
  std::vector<uint8_t> bytes{signature.begin(), signature.end()};

  bool empty() const noexcept { return bytes.size() == signature.size(); }
  std::size_t size() const noexcept { return bytes.size(); }

  /// Starts a command; the payload is then appended with the functions below
  /// and has to add up to payload_words * 4 bytes.
  void begin(command code, std::size_t payload_words, uint8_t specific_1 = 0, uint8_t specific_2 = 0)
  {
    bytes.push_back(static_cast<uint8_t>(code));
    bytes.push_back(static_cast<uint8_t>(payload_words));
    bytes.push_back(specific_1);
    bytes.push_back(specific_2);
  }

  void word(uint32_t w)
  {
    bytes.push_back(static_cast<uint8_t>(w >> 24));
    bytes.push_back(static_cast<uint8_t>(w >> 16));
    bytes.push_back(static_cast<uint8_t>(w >> 8));
    bytes.push_back(static_cast<uint8_t>(w));
  }

  void raw(std::span<const uint8_t> data) { bytes.insert(bytes.end(), data.begin(), data.end()); }

  /// Text, padded with zeros to a multiple of four bytes
  void text(std::string_view s)
  {
    bytes.insert(bytes.end(), s.begin(), s.end());
    for (std::size_t i = s.size(); i % 4 != 0; i++)
      bytes.push_back(0);
  }

  static constexpr std::size_t text_words(std::string_view s) noexcept { return (s.size() + 3) / 4; }

  /// The commands that carry a UMP Endpoint name and a Product Instance Id,
  /// optionally after a nonce
  void identity(
      command code, uint8_t specific_2, std::string_view name, std::string_view product_instance_id,
      std::span<const uint8_t> nonce = {})
  {
    begin(
        code, nonce.size() / 4 + text_words(name) + text_words(product_instance_id),
        static_cast<uint8_t>(text_words(name)), specific_2);
    raw(nonce);
    text(name);
    text(product_instance_id);
  }

  void ump_data(uint16_t sequence, std::span<const uint32_t> words)
  {
    begin(
        command::ump_data, words.size(), static_cast<uint8_t>(sequence >> 8),
        static_cast<uint8_t>(sequence));
    for (uint32_t w : words)
      word(w);
  }

  void ping(command code, uint32_t id)
  {
    begin(code, 1);
    word(id);
  }

  void retransmit_request(uint16_t sequence, uint16_t count)
  {
    begin(
        command::retransmit_request, 1, static_cast<uint8_t>(sequence >> 8),
        static_cast<uint8_t>(sequence));
    word(uint32_t{count} << 16);
  }

  /// reason: 0 unknown, 1 the data is not in the retransmit buffer anymore.
  /// first_available: the oldest sequence number that can still be asked for.
  void retransmit_error(uint8_t reason, uint16_t first_available)
  {
    begin(command::retransmit_error, 1, reason, 0);
    word(uint32_t{first_available} << 16);
  }

  void nak(nak_reason reason, const std::array<uint8_t, 4>& refused_header)
  {
    begin(command::nak, 1, static_cast<uint8_t>(reason), 0);
    raw(refused_header);
  }

  void bye(bye_reason reason) { begin(command::bye, 0, static_cast<uint8_t>(reason), 0); }

  void simple(command code) { begin(code, 0); }
};

/// The limits of the name fields
inline constexpr std::size_t max_endpoint_name_bytes = 98;
inline constexpr std::size_t max_product_instance_id_bytes = 42;

struct session_configuration
{
  // UMP Endpoint name, UTF-8, at most 98 bytes
  std::string endpoint_name = "libremidi";

  // Product Instance Id, ASCII, at most 42 bytes
  std::string product_instance_id = "libremidi";

  // Host: when set, clients have to prove they know it.
  // Client: used when the host asks for authentication.
  std::string shared_secret;

  // How many of the previous UMP Data commands are repeated in each datagram,
  // so that a lost datagram costs nothing as long as the next one arrives.
  int forward_error_correction = 2;

  // How many sent UMP Data commands are kept to answer retransmit requests
  std::size_t retransmit_buffer_commands = 128;

  // All in milliseconds
  uint32_t invitation_retry = 1000;
  // Client: gives up on a host that does not answer at all
  uint32_t invitation_timeout = 10000;
  // A Ping is sent when nothing was received for this long
  uint32_t ping_interval = 10000;
  // The session is over when nothing was received for this long
  uint32_t timeout = 30000;
  // How long a missing command is waited for before asking for it again
  uint32_t recovery_wait = 10;
  int retransmit_attempts = 3;
  // An empty UMP Data command follows the last data after this long,
  // so that the receiver can notice that the last datagram was lost
  uint32_t idle_first = 250;
  uint32_t idle_repeat = 10;
};

/// One Network MIDI 2.0 session with one peer.
///
/// Not thread-safe: call everything from one thread, or under one lock.
class session
{
public:
  /// Sends a datagram to the peer
  std::function<void(std::span<const uint8_t>)> send_datagram;

  /// Complete UMPs received from the peer, in order, each exactly once
  std::function<void(std::span<const uint32_t>)> on_ump;

  /// The state changed
  std::function<void(session_state)> on_state;

  session(role r, session_configuration conf)
      : configuration{std::move(conf)}
      , m_role{r}
  {
    configuration.endpoint_name.resize(
        std::min(configuration.endpoint_name.size(), max_endpoint_name_bytes));
    configuration.product_instance_id.resize(
        std::min(configuration.product_instance_id.size(), max_product_instance_id_bytes));
  }

  session_configuration configuration;

  role get_role() const noexcept { return m_role; }
  session_state state() const noexcept { return m_state; }
  bool established() const noexcept { return m_state == session_state::established; }

  /// Why the session closed, once state() is closed
  bye_reason close_reason() const noexcept { return m_close_reason; }
  /// Whether the peer closed it
  bool closed_by_peer() const noexcept { return m_closed_by_peer; }

  const std::string& remote_endpoint_name() const noexcept { return m_remote_name; }
  const std::string& remote_product_instance_id() const noexcept { return m_remote_piid; }

  /// Client: starts inviting the host. Call tick() regularly afterwards.
  void invite(uint64_t now)
  {
    if (m_role != role::client)
      return;
    reset_streams();
    m_invite_started = now;
    m_last_received = now;
    set_state(session_state::inviting);
    send_invitation(now);
  }

  /// Ends the session, telling the peer. The state goes to closed when the
  /// peer acknowledges, or after a few attempts.
  void close(uint64_t now, bye_reason reason = bye_reason::user_terminated)
  {
    switch (m_state)
    {
      case session_state::idle:
      case session_state::closed:
        finish(reason, false);
        return;
      case session_state::closing:
        return;
      default:
        break;
    }
    if (m_state == session_state::inviting)
      reason = bye_reason::invitation_canceled;
    m_close_reason = reason;
    m_bye_attempts = 1;
    m_bye_sent = now;
    set_state(session_state::closing);
    send_bye(reason);
  }

  /// Sends whole UMPs. Fails when the session is not established, or when
  /// the words do not end on a UMP boundary.
  stdx::error send_ump(const uint32_t* words, std::size_t count, uint64_t now)
  {
    if (m_state != session_state::established)
      return std::errc::not_connected;

    // Check before sending anything
    for (std::size_t i = 0; i < count;)
    {
      const std::size_t n = ump_word_count(words[i]);
      if (i + n > count)
        return std::errc::bad_message;
      i += n;
    }

    for (std::size_t i = 0; i < count;)
    {
      // As many whole UMPs as a command takes
      std::size_t end = i;
      while (end < count)
      {
        const std::size_t n = ump_word_count(words[end]);
        if (end + n - i > max_ump_words_per_command)
          break;
        end += n;
      }
      send_data_command(std::span<const uint32_t>(words + i, end - i), now);
      i = end;
    }
    return stdx::error{};
  }

  /// A datagram arrived from the peer
  void on_datagram(std::span<const uint8_t> datagram, uint64_t now)
  {
    datagram_reader reader{datagram};
    if (!reader.valid())
      return;

    m_last_received = now;
    m_ping_pending = false;

    datagram_writer reply;
    command_view cmd;
    while (reader.next(cmd))
    {
      on_command(cmd, reply, now);
      if (m_state == session_state::closed)
        break;
    }
    flush(reply);

    if (m_state == session_state::established)
      deliver_in_order(now);
  }

  /// Timers: call every few milliseconds
  void tick(uint64_t now)
  {
    switch (m_state)
    {
      case session_state::inviting:
        if (now - m_invite_started >= configuration.invitation_timeout && !m_invitation_pending)
        {
          // Nobody there
          datagram_writer w;
          w.bye(bye_reason::invitation_canceled);
          flush(w);
          finish(bye_reason::timeout, false);
        }
        else if (now - m_invite_sent >= configuration.invitation_retry)
        {
          send_invitation(now);
        }
        break;

      case session_state::closing:
        if (now - m_bye_sent >= 200)
        {
          if (m_bye_attempts >= 3)
          {
            finish(m_close_reason, false);
          }
          else
          {
            m_bye_attempts++;
            m_bye_sent = now;
            send_bye(m_close_reason);
          }
        }
        break;

      case session_state::authenticating:
        if (now - m_last_received >= configuration.timeout)
          finish(bye_reason::timeout, false);
        break;

      case session_state::established:
        tick_established(now);
        break;

      default:
        break;
    }
  }

private:
  struct sent_command
  {
    uint16_t sequence;
    std::vector<uint32_t> words;
  };

  struct held_command
  {
    uint16_t sequence;
    std::vector<uint32_t> words;
  };

  void set_state(session_state s)
  {
    if (m_state == s)
      return;
    m_state = s;
    if (on_state)
      on_state(s);
  }

  void finish(bye_reason reason, bool by_peer)
  {
    m_close_reason = reason;
    m_closed_by_peer = by_peer;
    set_state(session_state::closed);
  }

  void flush(const datagram_writer& w)
  {
    if (!w.empty() && send_datagram)
      send_datagram(w.bytes);
  }

  void reset_streams()
  {
    m_next_sequence = 0;
    m_expected = 0;
    m_sent.clear();
    m_held.clear();
    m_gap_since.reset();
    m_gap_attempts = 0;
    m_idle_commands = 0;
    m_has_sent_data = false;
  }

  void send_invitation(uint64_t now)
  {
    m_invite_sent = now;
    datagram_writer w;
    w.identity(
        command::invitation,
        configuration.shared_secret.empty() ? uint8_t{0} : uint8_t{supports_authentication},
        configuration.endpoint_name, configuration.product_instance_id);
    flush(w);
  }

  void send_bye(bye_reason reason)
  {
    datagram_writer w;
    w.bye(reason);
    flush(w);
  }

  // Appends the previous commands that still fit, oldest first, then the new one
  void send_data_command(std::span<const uint32_t> words, uint64_t now)
  {
    const uint16_t sequence = m_next_sequence++;

    datagram_writer w;
    const std::size_t fec = std::min<std::size_t>(
        static_cast<std::size_t>(std::max(configuration.forward_error_correction, 0)),
        m_sent.size());
    std::size_t first = m_sent.size() - fec;
    std::size_t budget = max_datagram_bytes - w.size() - 4 - words.size() * 4;
    // Drop the oldest repeats when they do not all fit
    for (;;)
    {
      std::size_t needed = 0;
      for (std::size_t i = first; i < m_sent.size(); i++)
        needed += 4 + m_sent[i].words.size() * 4;
      if (needed <= budget || first == m_sent.size())
        break;
      first++;
    }
    for (std::size_t i = first; i < m_sent.size(); i++)
      w.ump_data(m_sent[i].sequence, m_sent[i].words);
    w.ump_data(sequence, words);
    flush(w);

    m_sent.push_back({sequence, std::vector<uint32_t>(words.begin(), words.end())});
    while (m_sent.size() > configuration.retransmit_buffer_commands)
      m_sent.pop_front();

    m_last_sent_data = now;
    if (!words.empty())
    {
      m_has_sent_data = true;
      m_idle_commands = 0;
    }
  }

  void tick_established(uint64_t now)
  {
    // Peer gone
    if (now - m_last_received >= configuration.timeout)
    {
      send_bye(bye_reason::timeout);
      finish(bye_reason::timeout, false);
      return;
    }

    // Peer silent: ask
    if (now - m_last_received >= configuration.ping_interval
        && (!m_ping_pending || now - m_ping_sent >= configuration.ping_interval))
    {
      m_ping_pending = true;
      m_ping_sent = now;
      m_ping_id = m_random();
      datagram_writer w;
      w.ping(command::ping, m_ping_id);
      flush(w);
    }

    // After the last data, empty commands carry its repeats and let the
    // receiver see that it has everything.
    if (m_has_sent_data && configuration.forward_error_correction > 0
        && m_idle_commands < configuration.forward_error_correction)
    {
      const uint32_t wait = m_idle_commands == 0 ? configuration.idle_first
                                                 : configuration.idle_repeat;
      if (now - m_last_sent_data >= wait)
      {
        m_idle_commands++;
        send_data_command({}, now);
      }
    }

    // A command is missing and what follows it is waiting
    if (m_gap_since && now - *m_gap_since >= configuration.recovery_wait)
    {
      if (m_gap_attempts >= configuration.retransmit_attempts)
      {
        // It will not come: go on without it
        skip_gap(now);
      }
      else
      {
        m_gap_attempts++;
        m_gap_since = now;
        request_missing();
      }
    }
  }

  void request_missing()
  {
    if (m_held.empty())
      return;
    const uint16_t count = static_cast<uint16_t>(m_held.front().sequence - m_expected);
    datagram_writer w;
    w.retransmit_request(m_expected, count);
    flush(w);
  }

  void skip_gap(uint64_t now)
  {
    if (!m_held.empty())
      m_expected = m_held.front().sequence;
    m_gap_since.reset();
    m_gap_attempts = 0;
    deliver_in_order(now);
  }

  // Gives the application what is next in sequence
  void deliver_in_order(uint64_t now)
  {
    while (!m_held.empty() && m_held.front().sequence == m_expected)
    {
      auto words = std::move(m_held.front().words);
      m_held.pop_front();
      m_expected++;
      if (!words.empty() && on_ump)
        on_ump(words);
    }

    if (m_held.empty())
    {
      m_gap_since.reset();
      m_gap_attempts = 0;
    }
    else if (!m_gap_since)
    {
      m_gap_since = now;
      m_gap_attempts = 0;
    }
  }

  void on_ump_data(const command_view& cmd)
  {
    const uint16_t sequence = cmd.specific_16();

    // Already delivered: a repeat
    if (sequence != m_expected && !sequence_after(sequence, m_expected))
      return;

    // Already waiting
    auto it = std::find_if(m_held.begin(), m_held.end(), [&](const held_command& h) {
      return h.sequence == sequence || sequence_after(h.sequence, sequence);
    });
    if (it != m_held.end() && it->sequence == sequence)
      return;

    held_command h{sequence, {}};
    h.words.reserve(cmd.payload.size() / 4);
    for (std::size_t i = 0; i + 4 <= cmd.payload.size(); i += 4)
    {
      const uint8_t* p = cmd.payload.data() + i;
      h.words.push_back(
          (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]);
    }
    m_held.insert(it, std::move(h));

    // Too much waiting behind one missing command: let it go
    if (m_held.size() > max_held_commands)
    {
      m_expected = m_held.front().sequence;
      m_gap_since.reset();
      m_gap_attempts = 0;
    }
  }

  void on_retransmit_request(const command_view& cmd, datagram_writer& reply)
  {
    if (cmd.payload.size() < 4)
    {
      reply.nak(nak_reason::command_malformed, cmd.header);
      return;
    }
    const uint16_t from = cmd.specific_16();
    uint16_t count = static_cast<uint16_t>((cmd.payload[0] << 8) | cmd.payload[1]);

    auto it = std::find_if(m_sent.begin(), m_sent.end(), [&](const sent_command& s) {
      return s.sequence == from;
    });
    if (it == m_sent.end())
    {
      reply.retransmit_error(1, m_sent.empty() ? m_next_sequence : m_sent.front().sequence);
      return;
    }

    // Zero: everything from there on
    if (count == 0)
      count = static_cast<uint16_t>(m_sent.end() - it);

    datagram_writer w;
    for (; it != m_sent.end() && count > 0; ++it, --count)
    {
      if (w.size() + 4 + it->words.size() * 4 > max_datagram_bytes)
      {
        flush(w);
        w = datagram_writer{};
      }
      w.ump_data(it->sequence, it->words);
    }
    flush(w);
  }

  static std::string text_of(std::span<const uint8_t> bytes)
  {
    std::string s(bytes.begin(), bytes.end());
    if (const auto zero = s.find('\0'); zero != std::string::npos)
      s.resize(zero);
    return s;
  }

  // Name and Product Instance Id, after `skip` bytes of something else
  bool read_identity(const command_view& cmd, std::size_t skip)
  {
    const std::size_t name_bytes = std::size_t{cmd.specific_1} * 4;
    if (cmd.payload.size() < skip + name_bytes)
      return false;
    m_remote_name = text_of(cmd.payload.subspan(skip, name_bytes));
    m_remote_piid = text_of(cmd.payload.subspan(skip + name_bytes));
    return true;
  }

  void new_nonce()
  {
    for (std::size_t i = 0; i < m_nonce.size(); i += 4)
    {
      const uint32_t r = m_random();
      for (std::size_t j = 0; j < 4; j++)
        m_nonce[i + j] = static_cast<uint8_t>(r >> (8 * j));
    }
  }

  void on_command(const command_view& cmd, datagram_writer& reply, uint64_t now)
  {
    switch (cmd.code)
    {
      case command::invitation:
        on_invitation(cmd, reply, now);
        break;

      case command::invitation_with_authentication:
        on_invitation_with_authentication(cmd, reply, now);
        break;

      case command::invitation_with_user_authentication:
        // We never ask for it
        if (m_role == role::host)
        {
          reply.bye(bye_reason::no_matching_authentication_method);
          if (m_state != session_state::established)
            finish(bye_reason::no_matching_authentication_method, false);
        }
        else
          reply.nak(nak_reason::command_not_expected, cmd.header);
        break;

      case command::invitation_reply_accepted:
        if (m_role == role::client && m_state == session_state::inviting)
        {
          read_identity(cmd, 0);
          reset_streams();
          set_state(session_state::established);
        }
        else if (!(m_role == role::client && m_state == session_state::established))
          reply.nak(nak_reason::command_not_expected, cmd.header);
        break;

      case command::invitation_reply_pending:
        if (m_role == role::client && m_state == session_state::inviting)
        {
          // A person has to accept on the other side: keep waiting
          read_identity(cmd, 0);
          m_invitation_pending = true;
        }
        break;

      case command::invitation_reply_authentication_required:
        on_authentication_required(cmd, reply);
        break;

      case command::invitation_reply_user_authentication_required:
        if (m_role == role::client && m_state == session_state::inviting)
        {
          // We have no user name and password to give
          reply.bye(bye_reason::invitation_canceled);
          finish(bye_reason::no_matching_authentication_method, false);
        }
        break;

      case command::ping:
        if (cmd.payload.size() >= 4)
        {
          reply.begin(command::ping_reply, 1);
          reply.raw(cmd.payload.first(4));
        }
        else
          reply.nak(nak_reason::command_malformed, cmd.header);
        break;

      case command::ping_reply:
        // Having received anything already says the peer is there
        break;

      case command::ump_data:
        if (m_state == session_state::established)
          on_ump_data(cmd);
        else if (m_state != session_state::closing)
          reply.bye(bye_reason::session_not_established);
        break;

      case command::retransmit_request:
        if (m_state == session_state::established)
          on_retransmit_request(cmd, reply);
        else
          reply.nak(nak_reason::command_not_expected, cmd.header);
        break;

      case command::retransmit_error:
        // The peer does not have it anymore: go on with what we hold
        if (m_state == session_state::established)
          skip_gap(now);
        break;

      case command::session_reset:
        if (m_state == session_state::established)
        {
          reset_streams();
          reply.simple(command::session_reset_reply);
        }
        else
          reply.nak(nak_reason::command_not_expected, cmd.header);
        break;

      case command::session_reset_reply:
        break;

      case command::nak:
        break;

      case command::bye:
        reply.simple(command::bye_reply);
        if (m_state != session_state::idle && m_state != session_state::closed)
          finish(static_cast<bye_reason>(cmd.specific_1), true);
        break;

      case command::bye_reply:
        if (m_state == session_state::closing)
          finish(m_close_reason, false);
        break;

      default:
        reply.nak(nak_reason::command_not_supported, cmd.header);
        break;
    }
  }

  void on_invitation(const command_view& cmd, datagram_writer& reply, uint64_t)
  {
    if (m_role != role::host)
    {
      reply.nak(nak_reason::command_not_expected, cmd.header);
      return;
    }
    if (!read_identity(cmd, 0))
    {
      reply.nak(nak_reason::command_malformed, cmd.header);
      return;
    }

    if (m_state == session_state::established)
    {
      // Our answer was lost: say it again, the session goes on
      reply.identity(
          command::invitation_reply_accepted, 0, configuration.endpoint_name,
          configuration.product_instance_id);
      return;
    }

    if (!configuration.shared_secret.empty())
    {
      if (!(cmd.specific_2 & supports_authentication))
      {
        reply.bye(bye_reason::no_matching_authentication_method);
        finish(bye_reason::no_matching_authentication_method, false);
        return;
      }
      if (m_state != session_state::authenticating)
      {
        new_nonce();
        m_authentication_attempts = 0;
      }
      set_state(session_state::authenticating);
      reply.identity(
          command::invitation_reply_authentication_required, 0, configuration.endpoint_name,
          configuration.product_instance_id, m_nonce);
      return;
    }

    reset_streams();
    reply.identity(
        command::invitation_reply_accepted, 0, configuration.endpoint_name,
        configuration.product_instance_id);
    set_state(session_state::established);
  }

  void on_invitation_with_authentication(const command_view& cmd, datagram_writer& reply, uint64_t)
  {
    if (m_role != role::host)
    {
      reply.nak(nak_reason::command_not_expected, cmd.header);
      return;
    }
    if (m_state == session_state::established)
    {
      reply.identity(
          command::invitation_reply_accepted, 0, configuration.endpoint_name,
          configuration.product_instance_id);
      return;
    }
    if (m_state != session_state::authenticating)
    {
      reply.bye(bye_reason::authentication_without_prior_invitation);
      return;
    }
    if (cmd.payload.size() < 32)
    {
      reply.nak(nak_reason::command_malformed, cmd.header);
      return;
    }

    const auto expected = sha256::hash(m_nonce, configuration.shared_secret);
    if (std::equal(expected.begin(), expected.end(), cmd.payload.begin()))
    {
      reset_streams();
      reply.identity(
          command::invitation_reply_accepted, 0, configuration.endpoint_name,
          configuration.product_instance_id);
      set_state(session_state::established);
    }
    else if (++m_authentication_attempts >= 3)
    {
      reply.bye(bye_reason::authentication_failed);
      finish(bye_reason::authentication_failed, false);
    }
    else
    {
      // Wrong: once more, with a new nonce
      new_nonce();
      reply.identity(
          command::invitation_reply_authentication_required, 1, configuration.endpoint_name,
          configuration.product_instance_id, m_nonce);
    }
  }

  void on_authentication_required(const command_view& cmd, datagram_writer& reply)
  {
    if (m_role != role::client || m_state != session_state::inviting)
      return;
    if (cmd.payload.size() < 16 || !read_identity(cmd, 16))
    {
      reply.nak(nak_reason::command_malformed, cmd.header);
      return;
    }

    // No secret, or the one we have was refused
    if (configuration.shared_secret.empty() || cmd.specific_2 != 0)
    {
      reply.bye(bye_reason::invitation_canceled);
      finish(bye_reason::authentication_failed, false);
      return;
    }

    const auto digest = sha256::hash(cmd.payload.first(16), configuration.shared_secret);
    reply.begin(command::invitation_with_authentication, 8);
    reply.raw(digest);
    // The plain invitation must not be sent again meanwhile
    m_invitation_pending = true;
  }

  static constexpr std::size_t max_held_commands = 256;

  role m_role;
  session_state m_state = session_state::idle;
  bye_reason m_close_reason = bye_reason::unknown;
  bool m_closed_by_peer = false;

  std::string m_remote_name;
  std::string m_remote_piid;

  // Invitation
  uint64_t m_invite_started = 0;
  uint64_t m_invite_sent = 0;
  bool m_invitation_pending = false;
  std::array<uint8_t, 16> m_nonce{};
  int m_authentication_attempts = 0;

  // Liveness
  uint64_t m_last_received = 0;
  bool m_ping_pending = false;
  uint64_t m_ping_sent = 0;
  uint32_t m_ping_id = 0;

  // Closing
  uint64_t m_bye_sent = 0;
  int m_bye_attempts = 0;

  // Sending
  uint16_t m_next_sequence = 0;
  std::deque<sent_command> m_sent;
  uint64_t m_last_sent_data = 0;
  bool m_has_sent_data = false;
  int m_idle_commands = 0;

  // Receiving
  uint16_t m_expected = 0;
  std::deque<held_command> m_held;
  std::optional<uint64_t> m_gap_since;
  int m_gap_attempts = 0;

  std::minstd_rand m_random{std::random_device{}()};
};
}
