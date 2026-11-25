#pragma once
#include <libremidi/backends/net/helpers.hpp>
#include <libremidi/protocols/network_midi2.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/ip/v6_only.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

NAMESPACE_LIBREMIDI::net_ump
{
/// A Network MIDI 2.0 endpoint on a UDP socket: either a host that takes the
/// invitations of any number of clients, or a client of one host.
///
/// The sessions run on the I/O context. With a context of our own, a thread
/// runs it; with the one of the application, the application does.
class midi2_udp_endpoint
{
public:
  using udp = boost::asio::ip::udp;

  // At most this many clients at a time for a host
  static constexpr std::size_t max_sessions = 32;

  // A client invites again this long after its session ended
  static constexpr uint64_t reinvite_delay_ms = 2000;

  // How often the timers of the sessions are looked at
  static constexpr std::chrono::milliseconds tick_period{2};

  /// UMPs received from any peer. Called from the thread that runs the context.
  std::function<void(std::span<const uint32_t>)> on_ump;

  midi2_udp_endpoint(
      boost::asio::io_context* context, network_midi2::session_configuration conf)
      : m_ctx{context}
      , m_socket{m_ctx.get()}
      , m_timer{m_ctx.get()}
      , m_configuration{std::move(conf)}
  {
  }

  ~midi2_udp_endpoint()
  {
    close();
    // With the context of the application, handlers of ours can still be
    // queued in it: they see this and leave.
    *m_alive = false;
  }

  midi2_udp_endpoint(const midi2_udp_endpoint&) = delete;
  midi2_udp_endpoint& operator=(const midi2_udp_endpoint&) = delete;

  /// Waits for clients on this address and port.
  /// No address, "0.0.0.0" or "::" is every interface, over IPv6 and IPv4 where
  /// the system has both: some clients (macOS) come over IPv6 link-local addresses.
  stdx::error open_host(const std::string& address, int port)
  {
    if (port < 0 || port > 65535)
      return std::errc::invalid_argument;
    const auto p = static_cast<unsigned short>(port);
    if (address.empty() || address == "0.0.0.0" || address == "::")
    {
      if (bind(udp::endpoint{udp::v6(), p}, true) != stdx::error{})
        if (auto err = bind(udp::endpoint{udp::v4(), p}); err != stdx::error{})
          return err;
    }
    else
    {
      boost::system::error_code ec;
      const auto ip = boost::asio::ip::make_address(address, ec);
      if (ec)
        return std::errc::invalid_argument;

      if (auto err = bind(udp::endpoint{ip, p}); err != stdx::error{})
        return err;
    }

    m_role = network_midi2::role::host;
    start();
    return stdx::error{};
  }

  /// Invites the host at this address and port.
  /// Returns at once: see wait_until_established.
  stdx::error open_client(const std::string& host, int port, int local_port = 0)
  {
    if (port < 1 || port > 65535 || local_port < 0 || local_port > 65535)
      return std::errc::invalid_argument;
    boost::system::error_code ec;
    const auto ip = boost::asio::ip::make_address(host, ec);
    if (ec)
      return std::errc::invalid_argument;
    const udp::endpoint remote{ip, static_cast<unsigned short>(port)};

    const udp::endpoint local{
        ip.is_v6() ? udp::v6() : udp::v4(), static_cast<unsigned short>(local_port)};
    if (auto err = bind(local); err != stdx::error{})
      return err;

    m_role = network_midi2::role::client;
    {
      std::lock_guard lock{m_mutex};
      auto& s = make_session(remote);
      s.invite(now());
    }
    start();
    return stdx::error{};
  }

  /// The port the socket is bound to
  int local_port() const
  {
    boost::system::error_code ec;
    const auto ep = m_socket.local_endpoint(ec);
    return ec ? 0 : ep.port();
  }

  /// Number of sessions over which UMP currently flows
  std::size_t established_sessions() const
  {
    std::lock_guard lock{m_mutex};
    std::size_t n = 0;
    for (const auto& [ep, s] : m_sessions)
      n += s->established();
    return n;
  }

  /// Waits until a session is established: true when one is. Does not wait
  /// further once the host refused us (see refused()).
  /// Only with a context of our own; otherwise the application has to run its context.
  bool wait_until_established(std::chrono::milliseconds timeout)
  {
    std::unique_lock lock{m_mutex};
    m_changed.wait_for(lock, timeout, [&] { return any_established() || refused_locked(); });
    return any_established();
  }

  /// Client: the host does not want us (wrong or missing secret, no room).
  /// It is not invited again; anything else that ends the session is
  /// followed by a new invitation every few seconds.
  bool refused() const
  {
    std::lock_guard lock{m_mutex};
    return refused_locked();
  }

  /// To every established session
  stdx::error send_ump(const uint32_t* words, std::size_t count)
  {
    std::lock_guard lock{m_mutex};
    bool sent = false;
    stdx::error result{};
    const auto t = now();
    for (auto& [ep, s] : m_sessions)
    {
      if (!s->established())
        continue;
      if (auto err = s->send_ump(words, count, t); err != stdx::error{})
        result = err;
      else
        sent = true;
    }
    if (result != stdx::error{})
      return result;
    return sent ? stdx::error{} : stdx::error{std::errc::not_connected};
  }

  /// Says goodbye to the peers and stops
  void close()
  {
    if (!m_socket.is_open())
      return;

    {
      std::lock_guard lock{m_mutex};
      m_closing = true;
      const auto t = now();
      for (auto& [ep, s] : m_sessions)
        s->close(t);
    }

    if (m_ctx.is_owned())
    {
      // Leave the peers a moment to acknowledge
      {
        std::unique_lock lock{m_mutex};
        m_changed.wait_for(lock, std::chrono::milliseconds(300), [&] {
          for (const auto& [ep, s] : m_sessions)
            if (s->state() != network_midi2::session_state::closed)
              return false;
          return true;
        });
      }
      m_ctx.get().stop();
      if (m_thread.joinable())
        m_thread.join();
    }

    boost::system::error_code ec;
    m_timer.cancel();
    m_socket.close(ec);

    std::lock_guard lock{m_mutex};
    m_sessions.clear();
  }

private:
  static uint64_t now() noexcept
  {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
  }

  // dual_stack: an IPv6 socket that takes IPv4 as well, or nothing
  stdx::error bind(const udp::endpoint& local, bool dual_stack = false)
  {
    boost::system::error_code ec;
    m_socket.open(local.protocol(), ec);
    if (ec)
      return std::errc::address_family_not_supported;
    if (dual_stack)
    {
      m_socket.set_option(boost::asio::ip::v6_only(false), ec);
      if (ec)
      {
        boost::system::error_code ignored;
        m_socket.close(ignored);
        return std::errc::address_family_not_supported;
      }
    }
    // Without reuse_address: a second host on the same port has to fail,
    // and on Windows that option lets any other process take the port over.
    m_socket.bind(local, ec);
    if (ec)
    {
      boost::system::error_code ignored;
      m_socket.close(ignored);
      return std::errc::address_in_use;
    }
    return stdx::error{};
  }

  // With m_mutex held
  bool any_established() const
  {
    for (const auto& [ep, s] : m_sessions)
      if (s->established())
        return true;
    return false;
  }

  static bool is_refusal(network_midi2::bye_reason r) noexcept
  {
    using enum network_midi2::bye_reason;
    return r == too_many_sessions || r == rejected_by_user || r == authentication_failed
           || r == username_not_found || r == no_matching_authentication_method
           || r == authentication_without_prior_invitation;
  }

  bool refused_locked() const
  {
    if (m_role != network_midi2::role::client)
      return false;
    for (const auto& [ep, s] : m_sessions)
      if (s->state() == network_midi2::session_state::closed && is_refusal(s->close_reason()))
        return true;
    return false;
  }

  // A full host makes room by forgetting whoever only ever sent an invitation,
  // the one silent for the longest: invitations cost nothing to forge.
  // With m_mutex held; false when everybody there has proven to be there.
  bool forget_an_unproven_session()
  {
    auto victim = m_sessions.end();
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ++it)
    {
      const auto& s = *it->second;
      if (s.established() && s.heard_since_established())
        continue;
      if (victim == m_sessions.end() || s.last_received() < victim->second->last_received())
        victim = it;
    }
    if (victim == m_sessions.end())
      return false;
    m_sessions.erase(victim);
    return true;
  }

  void start()
  {
    m_closing = false;
    m_reinvite_at = 0;
    receive();
    schedule_tick();

    if (m_ctx.is_owned())
    {
      m_ctx.get().restart();
      m_thread = std::thread{[this] { m_ctx.get().run(); }};
    }
  }

  // With m_mutex held
  network_midi2::session& make_session(const udp::endpoint& remote)
  {
    auto s = std::make_unique<network_midi2::session>(m_role, m_configuration);
    s->send_datagram = [this, remote](std::span<const uint8_t> datagram) {
      boost::system::error_code ec;
      m_socket.send_to(boost::asio::const_buffer(datagram.data(), datagram.size()), remote, 0, ec);
    };
    s->on_ump = [this](std::span<const uint32_t> words) {
      if (on_ump)
        on_ump(words);
    };
    s->on_state = [this](network_midi2::session_state) { m_changed.notify_all(); };
    auto& ref = *s;
    m_sessions[remote] = std::move(s);
    return ref;
  }

  void receive()
  {
    m_socket.async_receive_from(
        boost::asio::mutable_buffer(m_data, sizeof(m_data)), m_from,
        [this, alive = m_alive](boost::system::error_code ec, std::size_t size) {
      if (!*alive || ec == boost::asio::error::operation_aborted || !m_socket.is_open())
        return;

      if (!ec && size > 0)
        on_datagram({m_data, size});

      receive();
    });
  }

  void on_datagram(std::span<const uint8_t> datagram)
  {
    if (!network_midi2::datagram_reader{datagram}.valid())
      return;

    std::lock_guard lock{m_mutex};
    auto it = m_sessions.find(m_from);
    if (it == m_sessions.end())
    {
      // A client only talks to its host
      if (m_role != network_midi2::role::host)
        return;

      if (m_sessions.size() >= max_sessions && !forget_an_unproven_session())
      {
        network_midi2::datagram_writer w;
        w.bye(network_midi2::bye_reason::too_many_sessions);
        boost::system::error_code ignored;
        m_socket.send_to(
            boost::asio::const_buffer(w.bytes.data(), w.bytes.size()), m_from, 0, ignored);
        return;
      }
      make_session(m_from);
      it = m_sessions.find(m_from);
    }
    it->second->on_datagram(datagram, now());
  }

  void schedule_tick()
  {
    m_timer.expires_after(tick_period);
    m_timer.async_wait([this, alive = m_alive](boost::system::error_code ec) {
      if (!*alive || ec || !m_socket.is_open())
        return;

      {
        std::lock_guard lock{m_mutex};
        const auto t = now();
        for (auto it = m_sessions.begin(); it != m_sessions.end();)
        {
          it->second->tick(t);
          // A host forgets the clients that left and the strangers that never
          // invited; a client keeps its only session.
          const auto state = it->second->state();
          if (m_role == network_midi2::role::host
              && (state == network_midi2::session_state::closed
                  || state == network_midi2::session_state::idle))
          {
            it = m_sessions.erase(it);
            continue;
          }

          // A client whose session ended without us asking, and without the
          // host refusing us, invites again: the host may be back later.
          if (m_role == network_midi2::role::client
              && state == network_midi2::session_state::closed && !m_closing
              && !is_refusal(it->second->close_reason()))
          {
            if (m_reinvite_at == 0)
              m_reinvite_at = t + reinvite_delay_ms;
            else if (t >= m_reinvite_at)
            {
              m_reinvite_at = 0;
              it->second->invite(t);
            }
          }
          ++it;
        }
      }
      schedule_tick();
    });
  }

  libremidi::optionally_owned<boost::asio::io_context> m_ctx;
  udp::socket m_socket;
  boost::asio::steady_timer m_timer;
  network_midi2::session_configuration m_configuration;
  network_midi2::role m_role{network_midi2::role::host};

  mutable std::recursive_mutex m_mutex;
  std::condition_variable_any m_changed;
  std::map<udp::endpoint, std::unique_ptr<network_midi2::session>> m_sessions;

  bool m_closing = false;
  uint64_t m_reinvite_at = 0;

  // Shared with the handlers. Only read and written from the thread that runs
  // the context, or after our own thread has ended.
  std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

  std::thread m_thread;
  udp::endpoint m_from;
  uint8_t m_data[65535];
};
}
