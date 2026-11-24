#pragma once
#include <libremidi/backends/net/helpers.hpp>
#include <libremidi/protocols/network_midi2.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
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

  ~midi2_udp_endpoint() { close(); }

  midi2_udp_endpoint(const midi2_udp_endpoint&) = delete;
  midi2_udp_endpoint& operator=(const midi2_udp_endpoint&) = delete;

  /// Waits for clients on this address and port
  stdx::error open_host(const std::string& address, int port)
  {
    boost::system::error_code ec;
    const auto ip = boost::asio::ip::make_address(address.empty() ? "0.0.0.0" : address, ec);
    if (ec)
      return std::errc::invalid_argument;

    if (auto err = bind(udp::endpoint{ip, static_cast<unsigned short>(port)}); err != stdx::error{})
      return err;

    m_role = network_midi2::role::host;
    start();
    return stdx::error{};
  }

  /// Invites the host at this address and port.
  /// Returns at once: see wait_until_established.
  stdx::error open_client(const std::string& host, int port, int local_port = 0)
  {
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

  /// Waits until a session is established: true when one is.
  /// Only with a context of our own; otherwise the application has to run its context.
  bool wait_until_established(std::chrono::milliseconds timeout)
  {
    std::unique_lock lock{m_mutex};
    return m_changed.wait_for(lock, timeout, [&] {
      for (const auto& [ep, s] : m_sessions)
        if (s->established())
          return true;
      return false;
    });
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

  stdx::error bind(const udp::endpoint& local)
  {
    boost::system::error_code ec;
    m_socket.open(local.protocol(), ec);
    if (ec)
      return std::errc::address_family_not_supported;
    m_socket.set_option(udp::socket::reuse_address(true), ec);
    m_socket.bind(local, ec);
    if (ec)
    {
      boost::system::error_code ignored;
      m_socket.close(ignored);
      return std::errc::address_in_use;
    }
    return stdx::error{};
  }

  void start()
  {
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
        [this](boost::system::error_code ec, std::size_t size) {
      if (ec == boost::asio::error::operation_aborted || !m_socket.is_open())
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

      if (m_sessions.size() >= max_sessions)
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
    m_timer.async_wait([this](boost::system::error_code ec) {
      if (ec || !m_socket.is_open())
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
            it = m_sessions.erase(it);
          else
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

  std::thread m_thread;
  udp::endpoint m_from;
  uint8_t m_data[65535];
};
}
