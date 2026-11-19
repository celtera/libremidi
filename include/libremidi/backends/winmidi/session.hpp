#pragma once
#include <libremidi/backends/winmidi/connection_pool.hpp>
#include <libremidi/backends/winmidi/helpers.hpp>
#include <libremidi/backends/winmidi/raw_output.hpp>
#include <libremidi/detail/midi_stream_decoder.hpp>
#include <libremidi/detail/ump_stream.hpp>

#include <bit>
#include <memory>
#include <type_traits>

NAMESPACE_LIBREMIDI::winmidi
{
// MIDI runtime classes are agile. Keep an MTA available on uninitialized host
// threads without changing a UI thread's apartment or balancing its COM calls.
class midi_apartment
{
  using increment_fn = HRESULT (WINAPI*)(void**);
  using decrement_fn = HRESULT (WINAPI*)(void*);

  // Keep the entry points valid until this particular cookie is released,
  // including when a host destroys its MIDI objects during static teardown.
  std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)> module{
      LoadLibraryExW(L"combase.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), &FreeLibrary};
  decrement_fn decrement;
  void* cookie{};

public:
  midi_apartment()
  {
    if (!module) winrt::throw_last_error();
    // SDK headers hide the Windows 8 declarations for a Windows 7 target.
    const auto increment = std::bit_cast<increment_fn>(
        GetProcAddress(module.get(), "CoIncrementMTAUsage"));
    decrement = std::bit_cast<decrement_fn>(GetProcAddress(module.get(), "CoDecrementMTAUsage"));
    if (!increment || !decrement)
      throw winrt::hresult_error{HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND)};
    winrt::check_hresult(increment(&cookie));
  }
  ~midi_apartment() { decrement(cookie); }
  midi_apartment(const midi_apartment&) = delete;
  midi_apartment& operator=(const midi_apartment&) = delete;
};

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
  midi_apartment apartment;
  MidiSession session;
  explicit owned_session(const winrt::hstring& name) : session{MidiSession::Create(name)}
  {
    if (!session) throw winrt::hresult_error{E_FAIL};
  }
};

class session_handle
{
  midi_apartment apartment;
  MidiSession supplied;
  bool has_context;
  std::shared_ptr<owned_session> named;

public:
  explicit session_handle(MidiSession* context)
      : supplied{context ? *context : nullptr}, has_context{context != nullptr} { }

  MidiSession get(const std::string& name)
  {
    if (has_context) return supplied; // The caller owns Close(), even when two wrappers share it.
    if (name.empty())
    {
      static owned_session host{host_session_name()};
      return host.session;
    }
    if (!named)
    {
      static std::mutex mutex;
      static std::map<std::string, std::weak_ptr<owned_session>> sessions;
      std::lock_guard lock{mutex};
      std::erase_if(sessions, [](const auto& e) { return e.second.expired(); });
      auto& weak = sessions[name];
      if (!(named = weak.lock())) weak = named = std::make_shared<owned_session>(to_hstring(name));
    }
    return named->session;
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

  void receive(std::span<const uint32_t> words, uint64_t ticks)
  {
    gate.invoke([&] {
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
  midi_apartment apartment;
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
  std::shared_ptr<receivers> inputs = std::make_shared<receivers>();
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

  ordinary_connection(endpoint_pool::reservation ticket, const MidiSession& s)
      : reservation{std::move(ticket)}, session{s}
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

  static std::shared_ptr<ordinary_connection> acquire(
      const MidiSession& session, const winrt::hstring& canonical_id,
      const std::shared_ptr<port_receiver>& input = {})
  {
    if (!session || !session.IsOpen()) throw winrt::hresult_illegal_method_call{};
    static endpoint_pool pool;
    bool attached = false;
    auto connection = pool.acquire({session.SessionId(), canonical_id}, [&](auto ticket) {
      std::shared_ptr<ordinary_connection> c{
          new ordinary_connection{std::move(ticket), session}, [](ordinary_connection* p) {
            // Disconnect may join the SDK receive thread. Never join it from its own callback.
            if (receive_scope::inside) SubmitThreadpoolWork(p->retire);
            else delete p;
          }};
      c->endpoint = session.CreateEndpointConnection(canonical_id);
      if (!c->endpoint) throw winrt::hresult_error{E_FAIL};
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
      if (!c->endpoint.Open()) throw winrt::hresult_error{E_FAIL};
#if LIBREMIDI_WINMIDI_HAS_COM_EXTENSIONS
      c->output = {
          c->raw->GetSupportedMaxMidiWordsPerTransmission(),
          MidiClock::TimestampConstantSendImmediately()};
#endif
      return c;
    });
    if (!connection) throw winrt::hresult_error{HRESULT_FROM_WIN32(ERROR_BUSY)};
    if (input && !attached) connection->inputs->add(input);
    return connection;
  }
};
}
