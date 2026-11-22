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
  struct slot
  {
    std::weak_ptr<Value> value;
    unsigned long long owner{};
  };
  struct state
  {
    std::mutex mutex;
    std::condition_variable changed;
    std::map<Key, slot> entries;
    unsigned long long owners{};
  };
  std::shared_ptr<state> table = std::make_shared<state>();

public:
  struct reservation
  {
    std::shared_ptr<state> table;
    Key key;
    unsigned long long owner;
    reservation(std::shared_ptr<state> t, Key k, unsigned long long o)
        : table{std::move(t)}, key{std::move(k)}, owner{o} { }
    reservation(reservation&&) = default;
    ~reservation()
    {
      if (table)
      {
        {
          std::lock_guard lock{table->mutex};
          // A value that replaced a dead one keeps the key after it.
          const auto entry = table->entries.find(key);
          if (entry != table->entries.end() && entry->second.owner == owner)
            table->entries.erase(entry);
        }
        table->changed.notify_all();
      }
    }
  };

  // create(reservation, dead) retains the reservation until *after* physical
  // teardown. dead is the value it replaces: one that its users still hold but
  // that reusable() refused, or null. Neither SDK construction nor destruction,
  // nor reusable(), happens under the registry mutex.
  std::shared_ptr<Value> acquire(const Key& key, auto&& create, auto&& reusable)
  {
    std::unique_lock lock{table->mutex};
    for (;;)
    {
      std::shared_ptr<Value> dead;
      auto [it, inserted] = table->entries.try_emplace(key);
      if (!inserted)
      {
        auto existing = it->second.value.lock();
        if (!existing)
        {
          // The opening/closing SDK call might itself be waiting on this callback.
          if (receive_scope::inside) return {};
          table->changed.wait(lock);
          continue;
        }
        const auto owner = it->second.owner;
        lock.unlock(); // Also to release what may be the last reference unlocked.
        if (reusable(std::as_const(*existing))) return existing;
        lock.lock();
        it = table->entries.find(key);
        if (it == table->entries.end() || it->second.owner != owner)
        {
          lock.unlock();
          existing.reset();
          lock.lock();
          continue;
        }
        dead = std::move(existing);
      }
      const auto owner = ++table->owners;
      it->second = slot{{}, owner};
      lock.unlock();
      auto value = create(reservation{table, key, owner}, std::move(dead));
      lock.lock();
      if (const auto entry = table->entries.find(key);
          entry != table->entries.end() && entry->second.owner == owner)
        entry->second.value = value;
      lock.unlock();
      table->changed.notify_all();
      return value;
    }
  }
};

// A transport invokes one callback at a time per connection. dispatch() runs
// on its time-critical thread: a change of the subscriber set builds and
// releases its lists outside the mutex, and a receive allocates nothing.
template <typename Receiver>
class endpoint_receivers
{
  using list = std::vector<std::weak_ptr<Receiver>>;
  std::shared_ptr<const list> receivers;
  std::mutex mutex;

  // Every change also drops the expired subscribers.
  void change(const std::shared_ptr<Receiver>* added, const std::shared_ptr<Receiver>* removed)
  {
    for (;;)
    {
      std::shared_ptr<const list> current;
      { std::lock_guard lock{mutex}; current = receivers; }
      auto updated = std::make_shared<list>();
      updated->reserve((current ? current->size() : 0) + (added ? 1 : 0));
      if (current)
        for (const auto& r : *current)
          if (!r.expired() && !(removed && !r.owner_before(*removed) && !removed->owner_before(r)))
            updated->push_back(r);
      if (added) updated->push_back(*added);
      std::shared_ptr<const list> previous = std::move(updated);
      {
        std::lock_guard lock{mutex};
        if (receivers != current) continue; // Changed meanwhile: start again from it.
        receivers.swap(previous);
      }
      return;
    }
  }

public:
  void add(const std::shared_ptr<Receiver>& receiver) { change(&receiver, nullptr); }
  void remove(const std::shared_ptr<Receiver>& receiver) { change(nullptr, &receiver); }

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
