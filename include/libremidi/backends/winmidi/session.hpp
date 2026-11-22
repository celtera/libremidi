#pragma once
#include <libremidi/backends/winmidi/connection_pool.hpp>
#include <libremidi/backends/winmidi/helpers.hpp>
#include <libremidi/backends/winmidi/raw_output.hpp>
#include <libremidi/detail/midi_stream_decoder.hpp>
#include <libremidi/detail/ump_stream.hpp>

#include <bit>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>

NAMESPACE_LIBREMIDI::winmidi
{
// MIDI runtime classes are agile. Keep an MTA available on uninitialized host
// threads without changing a UI thread's apartment or balancing its COM calls.
// The usage is taken once and kept for the life of the process: it must not be
// released from DllMain, which is where a module embedding libremidi destroys
// its remaining objects when it is unloaded.
inline void keep_mta_alive()
{
  static const bool taken = [] {
    // SDK headers hide the Windows 8 declaration for a Windows 7 target.
    using increment_fn = HRESULT (WINAPI*)(void**);
    const auto combase = LoadLibraryExW(L"combase.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!combase) winrt::throw_last_error();
    const auto increment = std::bit_cast<increment_fn>(
        GetProcAddress(combase, "CoIncrementMTAUsage"));
    if (!increment) throw winrt::hresult_error{HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND)};
    void* cookie{};
    winrt::check_hresult(increment(&cookie));
    return true;
  }();
  (void)taken;
}

inline winrt::hstring host_session_name()
{
  std::wstring path(MAX_PATH, L'\0');
  for (;;)
  {
    const DWORD len = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!len) winrt::throw_last_error();
    if (len < path.size())
    {
      path.resize(len);
      return winrt::hstring{std::wstring_view{path}.substr(path.find_last_of(L"/\\") + 1)};
    }
    path.resize(path.size() * 2);
  }
}

struct owned_session
{
  MidiSession session;
  explicit owned_session(const winrt::hstring& name) : session{MidiSession::Create(name)}
  {
    if (!session) throw winrt::hresult_error{E_FAIL};
  }
};

// The endpoint is being opened or closed by the caller of this receive callback.
struct endpoint_busy { };

// What libremidi objects share: the session of the process, and named ones.
// make(name) creates a session; usable(session) tells if it can still be shared.
template <typename Session>
class session_registry
{
  std::mutex mutex;
  std::shared_ptr<Session> host;
  std::map<std::string, std::weak_ptr<Session>> named;

  std::shared_ptr<Session> find(const std::string& name)
  {
    if (name.empty()) return host;
    const auto it = named.find(name);
    return it == named.end() ? nullptr : it->second.lock();
  }
  void store(const std::string& name, const std::shared_ptr<Session>& session)
  {
    if (name.empty()) host = session;
    else named[name] = session;
  }

public:
  std::shared_ptr<Session> get(const std::string& name, auto&& make, auto&& usable)
  {
    std::lock_guard lock{mutex};
    std::erase_if(named, [](const auto& e) { return e.second.expired(); });
    auto session = find(name);
    if (!session || !usable(*session))
      store(name, session = make(name));
    return session;
  }

  // A shared session keeps the service connection it was created on. When an
  // attempt fails on it, try once on a new session, and share that one from
  // then on only if it succeeds where the previous one could not.
  // current is the session the caller keeps alive; busy is rethrown as is.
  template <typename Busy>
  auto use(std::shared_ptr<Session>& current, const std::string& name, auto&& make,
           auto&& usable, auto&& attempt)
  {
    const auto session = current = get(name, make, usable);
    std::exception_ptr failure;
    try { return attempt(*session); }
    catch (const Busy&) { throw; }
    catch (...) { failure = std::current_exception(); }
    try
    {
      auto fresh = make(name);
      auto result = attempt(*fresh);
      std::lock_guard lock{mutex};
      const auto shared = find(name);
      if (!shared || shared == session)
        store(name, fresh);
      current = std::move(fresh);
      return result;
    }
    catch (...) { std::rethrow_exception(failure); }
  }
};

class session_handle
{
  // Never destroyed. The process-wide session would otherwise be closed from a
  // static destructor, under the loader lock when the embedding module unloads.
  static session_registry<owned_session>& sessions()
  {
    static auto* const all = new session_registry<owned_session>;
    return *all;
  }
  static std::shared_ptr<owned_session> make(const std::string& name)
  {
    return std::make_shared<owned_session>(name.empty() ? host_session_name() : to_hstring(name));
  }
  static bool usable(const owned_session& s) { return s.session.IsOpen(); }

  MidiSession supplied;
  bool has_context;
  std::shared_ptr<owned_session> current;

public:
  explicit session_handle(MidiSession* context)
      : supplied{context ? *context : nullptr}, has_context{context != nullptr}
  {
    keep_mta_alive();
  }

  MidiSession get(const std::string& name)
  {
    if (has_context) return supplied; // The caller owns Close(), even when two wrappers share it.
    current = sessions().get(name, make, usable);
    return current->session;
  }

  // attempt(session) opens something in the session; see session_registry::use.
  auto use(const std::string& name, auto&& attempt)
  {
    if (has_context) return attempt(supplied);
    return sessions().use<endpoint_busy>(
        current, name, make, usable, [&](const owned_session& s) { return attempt(s.session); });
  }
};

struct port_receiver
{
  receive_gate gate;
  libremidi::ump_input_configuration config;
  midi2::input_state_machine processing{config};
  int group;
  uint64_t frequency;

  port_receiver(libremidi::ump_input_configuration c, int g)
      : config{std::move(c)}, group{g}, frequency{MidiClock::TimestampFrequency()}
  {
    if (!frequency) frequency = 10'000'000;
    // A callback may close its port partway through a multi-UMP batch.
    if (config.on_message)
      config.on_message = [this, cb = std::move(config.on_message)](libremidi::ump&& m) {
        if (gate.enabled()) cb(std::move(m));
      };
    if (config.on_raw_data)
      config.on_raw_data = [this, cb = std::move(config.on_raw_data)](auto words, auto ts) {
        if (gate.enabled()) cb(words, ts);
      };
  }

  void report_receive_error(std::string_view message) noexcept
  {
    // A callback may have closed its port before throwing.
    if (!gate.enabled()) return;
    try
    {
      if (config.on_error)
        config.on_error(message, libremidi::source_location::current());
      else
        LIBREMIDI_LOG(message);
    }
    catch (...) { } // A failing error handler is not an SDK receive failure either.
  }

  void receive(std::span<const uint32_t> words, uint64_t ticks)
  {
    gate.invoke([&] {
      // Keep application failures local to this subscriber, inside the draining gate.
      try
      {
        auto run = next_ump_group_run(words, group);
        if (run.empty()) return;
        constexpr timestamp_backend_info info{.has_absolute_timestamps = true};
        const auto ts = processing.timestamp<info>([&] {
          constexpr uint64_t billion = 1'000'000'000;
          return ticks / frequency * billion + (ticks % frequency) * billion / frequency;
        }, 0);
        do
        {
          processing.on_bytes(run, ts);
          run = next_ump_group_run(words, group);
        } while (!run.empty() && gate.enabled());
      }
      catch (const std::exception& e) { report_receive_error(e.what()); }
      catch (...) { report_receive_error("Unknown exception in Windows MIDI input callback"); }
    });
  }
};

inline void receive_event(const winrt::Windows::Devices::Midi2::MidiMessageReceivedEventArgs& args,
                          auto&& receive)
{
  uint32_t words[4];
  auto count = args.FillWords(words[0], words[1], words[2], words[3]);
  receive(std::span<const uint32_t>{words, count}, args.Timestamp());
}

class ordinary_connection;
using endpoint_pool = connection_pool<std::pair<winrt::guid, winrt::hstring>, ordinary_connection>;

class ordinary_connection
{
  endpoint_pool::reservation reservation; // destroyed last, after disconnect and COM releases
  MidiSession session;
  PTP_WORK retire{};
  winrt::event_token token{};
  struct receivers : endpoint_receivers<port_receiver>
  {
    void receive(std::span<const uint32_t> words, uint64_t ticks)
    {
      dispatch([&](port_receiver& input) { input.receive(words, ticks); });
    }
  };
  std::shared_ptr<receivers> inputs;
#if LIBREMIDI_WINMIDI_HAS_COM_EXTENSIONS
  struct raw_callback final : IMidiEndpointConnectionMessagesReceivedCallback
  {
    std::atomic<ULONG> refs{1};
    std::shared_ptr<receivers> inputs;
    explicit raw_callback(std::shared_ptr<receivers> r) : inputs{std::move(r)} { }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() noexcept override
    {
      const auto n = --refs;
      if (!n) delete this;
      return n;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) noexcept override
    {
      if (!out) return E_POINTER;
      if (iid == __uuidof(::IUnknown) || iid == ::IID_IMidiEndpointConnectionMessagesReceivedCallback)
      {
        *out = static_cast<IMidiEndpointConnectionMessagesReceivedCallback*>(this);
        AddRef();
        return S_OK;
      }
      *out = nullptr;
      return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE MessagesReceived(GUID, GUID, UINT64 ticks, UINT32 count,
                                                const UINT32* words) noexcept override
    {
      try
      {
        receive_scope scope;
        inputs->receive({words, count}, ticks);
        return S_OK;
      }
      catch (...) { return winrt::to_hresult(); }
    }
  };
  winrt::com_ptr<raw_callback> callback;
#endif

  ordinary_connection(
      endpoint_pool::reservation ticket, const MidiSession& s, std::shared_ptr<receivers> r)
      : reservation{std::move(ticket)}, session{s}, inputs{std::move(r)}
  {
    retire = CreateThreadpoolWork([](PTP_CALLBACK_INSTANCE, void* p, PTP_WORK) {
      delete static_cast<ordinary_connection*>(p);
    }, this, nullptr);
    if (!retire) winrt::throw_last_error();
  }

  ~ordinary_connection()
  {
    if (endpoint)
    {
#if LIBREMIDI_WINMIDI_HAS_COM_EXTENSIONS
      if (callback) raw->RemoveMessagesReceivedCallback();
#else
      if (token.value) endpoint.MessageReceived(token);
#endif
      if (lost.value) endpoint.EndpointDeviceDisconnected(lost);
      session.DisconnectEndpointConnection(endpoint.ConnectionId());
    }
    CloseThreadpoolWork(retire);
  }

#if LIBREMIDI_WINMIDI_HAS_COM_EXTENSIONS
  winrt::com_ptr<IMidiEndpointConnectionRaw> raw;
  raw_output output;
#endif

public:
  MidiEndpointConnection endpoint{nullptr};
#if LIBREMIDI_WINMIDI_HAS_COM_EXTENSIONS
  stdx::error send(std::span<const uint32_t> words) const
  {
    return output.send(*raw.get(), words);
  }
#endif

  winrt::event_token lost{};

  // A connection that lost its device, or that was disconnected underneath,
  // is closed and unknown to its session: nothing reopens it.
  bool usable() const noexcept
  {
    try
    {
      return endpoint.IsOpen() || session.Connections().HasKey(endpoint.ConnectionId());
    }
    catch (...) { return false; }
  }

  void detach(const std::shared_ptr<port_receiver>& input) noexcept
  {
    // Not from a receive callback: this allocates the remaining list.
    if (receive_scope::inside) return;
    try { inputs->remove(input); }
    catch (...) { } // It stays until the next change of the list, never invoked.
  }

  static std::shared_ptr<ordinary_connection> acquire(
      const MidiSession& session, const winrt::hstring& canonical_id,
      const std::shared_ptr<port_receiver>& input = {})
  {
    if (!session || !session.IsOpen()) throw winrt::hresult_illegal_method_call{};
    static endpoint_pool pool;
    bool attached = false;
    auto connection = pool.acquire(
        {session.SessionId(), canonical_id},
        [&](auto ticket, std::shared_ptr<ordinary_connection> dead) {
      // The inputs still open on a dead connection continue on its replacement.
      auto receiving = dead ? dead->inputs : std::make_shared<receivers>();
      std::shared_ptr<ordinary_connection> c{
          new ordinary_connection{std::move(ticket), session, std::move(receiving)},
          [](ordinary_connection* p) {
            // Disconnect may join the SDK receive thread. Never join it from its own callback.
            if (receive_scope::inside) SubmitThreadpoolWork(p->retire);
            else delete p;
          }};
      // Auto-reconnect is only asked for its notification: without it a
      // connection stays open and silent after its device left. The connection
      // the SDK reopens when the device returns carries no traffic, and keeps
      // every other connection to that endpoint silent as long as it exists.
      // Disconnect it as soon as the device leaves; the next open makes a new one.
      c->endpoint = session.CreateEndpointConnection(
          canonical_id, MidiEndpointConnectionSettings{false, true});
      if (!c->endpoint) throw winrt::hresult_error{E_FAIL};
      c->lost = c->endpoint.EndpointDeviceDisconnected(
          [session, id = c->endpoint.ConnectionId()](const auto&, const auto&) {
        session.DisconnectEndpointConnection(id);
      });
#if LIBREMIDI_WINMIDI_HAS_COM_EXTENSIONS
      c->endpoint.as(::IID_IMidiEndpointConnectionRaw, c->raw.put_void());
      c->callback.attach(new raw_callback{c->inputs});
      winrt::check_hresult(c->raw->SetMessagesReceivedCallback(c->callback.get()));
#else
      c->token = c->endpoint.MessageReceived([inputs = c->inputs](const auto&, const auto& args) {
        receive_scope scope;
        receive_event(args, [&](auto words, auto ticks) { inputs->receive(words, ticks); });
      });
#endif
      if (input)
      {
        c->inputs->add(input);
        attached = true;
      }
      if (!c->endpoint.Open())
      {
        if (std::exchange(attached, false)) c->inputs->remove(input);
        throw winrt::hresult_error{E_FAIL};
      }
#if LIBREMIDI_WINMIDI_HAS_COM_EXTENSIONS
      c->output = {
          batch_words(c->raw->GetSupportedMaxMidiWordsPerTransmission()),
          MidiClock::TimestampConstantSendImmediately()};
#endif
      return c;
    }, [](const ordinary_connection& c) { return c.usable(); });
    if (!connection) throw endpoint_busy{};
    if (input && !attached) connection->inputs->add(input);
    return connection;
  }
};
}
