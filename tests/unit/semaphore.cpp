#include "../include_catch.hpp"

#include <libremidi/detail/semaphore.hpp>

#include <atomic>
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

TEST_CASE("the callback can run any number of cycles without a waiter", "[semaphore]")
{
  // Used to release a binary semaphore on every cycle.
  semaphore_pair_lock lock;
  for (int i = 0; i < 100000; i++)
    lock.check_client_released();

  REQUIRE(lock.acknowledged.load() == 0);
  REQUIRE_FALSE(lock.wakeup.try_acquire());
}

TEST_CASE("a release waits for a cycle that started after it", "[semaphore]")
{
  semaphore_pair_lock lock;
  std::atomic<int> value{0};
  std::atomic<int> seen{-1};
  std::atomic<bool> stop{false};

  std::thread callback{[&] {
    while (!stop.load())
    {
      seen.store(value.load());
      lock.check_client_released();
      std::this_thread::sleep_for(100us);
    }
  }};

  // Let the callback run many idle cycles first: none of them may count as an answer.
  std::this_thread::sleep_for(20ms);

  for (int i = 1; i <= 500; i++)
  {
    value.store(i);
    lock.prepare_release_client();
    // The callback acknowledged in a cycle that read the new value.
    REQUIRE(seen.load() == i);
  }

  stop.store(true);
  callback.join();
}

TEST_CASE("a release gives up when the callback does not run", "[semaphore]")
{
  semaphore_pair_lock lock;

  const auto t0 = std::chrono::steady_clock::now();
  lock.prepare_release_client();
  const auto waited = std::chrono::steady_clock::now() - t0;
  CHECK(waited >= 900ms);
  CHECK(waited < 5s);

  // The late answer to that request is not taken for the answer to the next one.
  lock.check_client_released();

  std::atomic<bool> answered{false};
  std::thread callback{[&] {
    std::this_thread::sleep_for(200ms);
    answered.store(true);
    lock.check_client_released();
  }};
  lock.prepare_release_client();
  CHECK(answered.load());
  callback.join();
}

TEST_CASE("a cycle does not answer a request made after it started", "[semaphore]")
{
  semaphore_pair_lock lock;

  // The cycle starts, then the other thread asks: this cycle did not see its change.
  const auto cycle = lock.begin_cycle();
  std::thread waiter{[&] { lock.prepare_release_client(); }};
  while (lock.requested.load() == 0)
    std::this_thread::yield();

  lock.end_cycle(cycle);
  CHECK(lock.acknowledged.load() == 0);

  // The next one did.
  lock.end_cycle(lock.begin_cycle());
  waiter.join();
  CHECK(lock.acknowledged.load() == 1);
}
