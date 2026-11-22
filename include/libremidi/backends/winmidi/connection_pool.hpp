#pragma once
#include <libremidi/config.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

NAMESPACE_LIBREMIDI::winmidi
{
// Never hold a registry or subscriber lock across application code. A close
// from outside a callback drains the current delivery. Callback-originated
// closes must not wait on each other; the invocation owns its receiver state.
struct receive_scope
{
  inline static thread_local bool inside{};
  bool previous = std::exchange(inside, true);
  ~receive_scope() { inside = previous; }
};
// A weak cache alone permits a new open to race the last owner's disconnect.
// Keep the key reserved until destruction of the native resource has completed.
template <typename Key, typename Value>
class connection_pool
{
  struct state
  {
    std::mutex mutex;
    std::condition_variable changed;
    std::map<Key, std::weak_ptr<Value>> entries;
  };
  std::shared_ptr<state> table = std::make_shared<state>();

public:
  struct reservation
  {
    std::shared_ptr<state> table;
    typename decltype(state::entries)::iterator entry;
    reservation(std::shared_ptr<state> t, decltype(entry) e) : table{std::move(t)}, entry{e} { }
    reservation(reservation&&) = default;
    ~reservation()
    {
      if (table)
      {
        { std::lock_guard lock{table->mutex}; table->entries.erase(entry); }
        table->changed.notify_all();
      }
    }
  };

  // The factory retains the reservation until *after* physical teardown. Neither
  // SDK construction nor destruction happens under the registry mutex.
  std::shared_ptr<Value> acquire(const Key& key, auto&& create)
  {
    std::unique_lock lock{table->mutex};
    for (;;)
    {
      auto [it, inserted] = table->entries.try_emplace(key);
      if (inserted)
      {
        lock.unlock();
        auto value = create(reservation{table, it});
        lock.lock();
        it->second = value;
        lock.unlock();
        table->changed.notify_all();
        return value;
      }
      if (auto existing = it->second.lock())
        return existing;
      // The opening/closing SDK call might itself be waiting on this callback.
      if (receive_scope::inside) return {};
      table->changed.wait(lock);
    }
  }
};

// A transport invokes one callback at a time per connection. Changes to its
// subscriber set allocate on open only, never on a receive or a close.
template <typename Receiver>
class endpoint_receivers
{
  using list = std::vector<std::weak_ptr<Receiver>>;
  std::shared_ptr<const list> receivers;
  std::mutex mutex;

public:
  void add(const std::shared_ptr<Receiver>& receiver)
  {
    std::lock_guard lock{mutex};
    auto updated = std::make_shared<list>();
    if (receivers)
    {
      updated->reserve(receivers->size() + 1);
      for (const auto& r : *receivers)
        if (!r.expired()) updated->push_back(r);
    }
    updated->push_back(receiver);
    receivers = std::move(updated);
  }

  void dispatch(auto&& invoke)
  {
    std::shared_ptr<const list> snapshot;
    { std::lock_guard lock{mutex}; snapshot = receivers; }
    if (snapshot)
      for (const auto& weak : *snapshot)
        if (auto receiver = weak.lock())
          invoke(*receiver);
  }
};


class receive_gate
{
  enum { running = 1, closed = 2 };
  std::atomic<unsigned> state{};

public:
  bool enabled() const { return !(state.load() & closed); }
  void invoke(auto&& f)
  {
    unsigned idle{};
    if (state.compare_exchange_strong(idle, running))
    {
      struct finish
      {
        std::atomic<unsigned>& s;
        ~finish() { s.fetch_and(~unsigned(running)); s.notify_all(); }
      } done{state};
      receive_scope scope;
      f();
    }
  }
  void stop()
  {
    const auto previous = state.fetch_or(closed);
    if ((previous & running) && !receive_scope::inside)
      state.wait(running | closed);
  }
};
}
