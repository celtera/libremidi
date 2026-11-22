// The sharing rules of Windows MIDI Services connections, without the service.
#include <libremidi/backends/winmidi/connection_pool.hpp>

#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdio>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace w = libremidi::winmidi;
using namespace std::chrono_literals;

static void require(bool pass, const char* what)
{
  if (!pass) throw std::runtime_error{what};
}

struct connection;
using pool_type = w::connection_pool<std::string, connection>;

struct connection
{
  pool_type::reservation reservation;
  std::shared_ptr<connection> replaced;
  std::atomic<bool> open{true};
};

struct counting_pool
{
  pool_type pool;
  std::atomic<int> created{};

  std::shared_ptr<connection> acquire(const std::string& key = "endpoint")
  {
    return pool.acquire(key, [&](auto ticket, std::shared_ptr<connection> dead) {
      ++created;
      return std::make_shared<connection>(std::move(ticket), std::move(dead));
    }, [](const connection& c) { return c.open.load(); });
  }
};

static void concurrent_acquire_shares_one_connection()
{
  counting_pool p;
  constexpr int threads = 8;
  std::barrier start{threads};
  std::vector<std::future<std::shared_ptr<connection>>> opening;
  for (int i = 0; i < threads; ++i)
    opening.push_back(std::async(std::launch::async, [&] {
      start.arrive_and_wait();
      return p.acquire();
    }));
  const auto first = opening[0].get();
  for (int i = 1; i < threads; ++i)
    require(opening[i].get() == first, "concurrent opens got different connections");
  require(p.created == 1, "concurrent opens created more than one connection");
  require(p.acquire("other") != first && p.created == 2, "another endpoint shared the connection");
}

static void dead_connection_is_replaced()
{
  counting_pool p;
  auto dead = p.acquire();
  auto sibling = p.acquire();
  dead->open = false;

  auto fresh = p.acquire();
  require(fresh != dead && p.created == 2, "a dead connection was handed out");
  require(fresh->replaced == dead, "the replacement was not given the connection it replaces");
  require(p.acquire() == fresh && p.created == 2, "the replacement is not shared");

  // The last users of the dead connection must not release its replacement's key.
  fresh->replaced.reset();
  sibling.reset();
  dead.reset();
  require(p.acquire() == fresh && p.created == 2, "releasing the dead connection freed the key");

  fresh.reset();
  require(p.acquire() != nullptr && p.created == 3, "the key was not freed by its last owner");
}

static void failed_open_frees_the_key()
{
  pool_type pool;
  const auto open = [](const connection&) { return true; };
  try
  {
    pool.acquire("endpoint", [](auto, auto) -> std::shared_ptr<connection> {
      throw std::runtime_error{"open failed"};
    }, open);
    require(false, "the failure was swallowed");
  }
  catch (const std::runtime_error&) { }
  auto opened = std::async(std::launch::async, [&] {
    return pool.acquire("endpoint", [](auto ticket, auto) {
      return std::make_shared<connection>(std::move(ticket));
    }, open);
  });
  require(opened.wait_for(5s) == std::future_status::ready && opened.get(),
          "a failed open kept the key reserved");
}

// The SDK call that opens or closes an endpoint may be waiting for the receive
// callback that asks for the same endpoint: that request must not wait.
static void acquire_from_callback_does_not_wait()
{
  counting_pool p;
  std::promise<void> opening, finish;
  auto first = std::async(std::launch::async, [&] {
    return p.pool.acquire("endpoint", [&](auto ticket, auto) {
      opening.set_value();
      finish.get_future().wait();
      return std::make_shared<connection>(std::move(ticket));
    }, [](const connection&) { return true; });
  });
  opening.get_future().wait();

  auto reentrant = std::async(std::launch::async, [&] {
    w::receive_scope callback;
    return p.acquire();
  });
  require(reentrant.wait_for(5s) == std::future_status::ready, "acquire waited inside a callback");
  require(reentrant.get() == nullptr && p.created == 0, "acquire did not report the busy endpoint");

  auto outside = std::async(std::launch::async, [&] { return p.acquire(); });
  require(outside.wait_for(50ms) != std::future_status::ready, "acquire did not wait for the open");
  finish.set_value();
  require(outside.get() == first.get() && p.created == 0, "the waiting open did not share");
}

struct subscriber
{
  int received{};
};

static int deliver(w::endpoint_receivers<subscriber>& list)
{
  int count{};
  list.dispatch([&](subscriber& s) { ++s.received; ++count; });
  return count;
}

static void subscribers_change_during_dispatch()
{
  w::endpoint_receivers<subscriber> list;
  require(deliver(list) == 0, "delivery without subscribers");
  auto a = std::make_shared<subscriber>(), b = std::make_shared<subscriber>();
  auto late = std::make_shared<subscriber>();
  list.add(a);
  list.add(b);

  // A callback opens and closes ports of the same endpoint.
  int pass{};
  list.dispatch([&](subscriber& s) {
    ++s.received;
    if (pass++ == 0)
    {
      list.add(late);
      list.remove(b);
    }
  });
  require(a->received == 1 && b->received == 1 && late->received == 0,
          "a change altered the delivery in progress");
  require(deliver(list) == 2 && a->received == 2 && b->received == 1 && late->received == 1,
          "a change was not applied to the next delivery");

  // A closed port is neither delivered to nor kept.
  std::weak_ptr<subscriber> gone = late;
  late.reset();
  require(gone.expired() && deliver(list) == 1 && a->received == 3, "an expired subscriber was used");
  list.remove(a);
  require(deliver(list) == 0, "a removed subscriber was delivered to");
}

static void subscribers_change_while_receiving()
{
  w::endpoint_receivers<subscriber> list;
  auto permanent = std::make_shared<subscriber>();
  list.add(permanent);
  std::atomic<bool> stop{};
  int deliveries{};
  std::thread receiving{[&] {
    while (!stop)
    {
      list.dispatch([](subscriber& s) { ++s.received; });
      ++deliveries;
    }
  }};
  std::vector<std::thread> opening;
  for (int t = 0; t < 4; ++t)
    opening.emplace_back([&] {
      for (int i = 0; i < 2000; ++i)
      {
        auto port = std::make_shared<subscriber>();
        list.add(port);
        if (i % 2) list.remove(port);
      }
    });
  for (auto& t : opening) t.join();
  stop = true;
  receiving.join();
  require(permanent->received == deliveries, "a subscriber missed a delivery during changes");
  require(deliver(list) == 1, "closed subscribers were kept");
}

int main() try
{
  concurrent_acquire_shares_one_connection();
  dead_connection_is_replaced();
  failed_open_frees_the_key();
  acquire_from_callback_does_not_wait();
  subscribers_change_during_dispatch();
  subscribers_change_while_receiving();
  std::puts("PASS: one connection per key; dead connection replaced; failed open; "
            "busy inside a callback; subscriber changes during delivery");
  return 0;
}
catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
