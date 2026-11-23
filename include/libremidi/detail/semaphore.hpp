#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <semaphore>

// Lets a thread wait until a periodic callback has run once more:
// what the thread changed before asking is then visible to the callback,
// and what the callback was using before can be released.
struct semaphore_pair_lock
{
  std::atomic<std::uint64_t> requested{0};
  std::atomic<std::uint64_t> acknowledged{0};
  std::counting_semaphore<> wakeup{0};

  // Called by the thread that releases the resource.
  // Gives up after one second, for when the callback does not run anymore.
  void prepare_release_client()
  {
    using namespace std::literals;

    // FIXME if jack is not running we can skip this
    const auto request = this->requested.fetch_add(1, std::memory_order_acq_rel) + 1;
    const auto deadline = std::chrono::steady_clock::now() + 1s;

    // A wake-up can answer an earlier request that timed out: check which one it was.
    while (this->acknowledged.load(std::memory_order_acquire) < request)
      if (!this->wakeup.try_acquire_until(deadline))
        break;
  }

  // Called by the callback when a cycle starts, before it reads what the other thread changes...
  std::uint64_t begin_cycle() const noexcept
  {
    return this->requested.load(std::memory_order_acquire);
  }

  // ... and when it ends: only the requests made before the cycle started are answered.
  // Does nothing unless someone waits.
  void end_cycle(std::uint64_t request)
  {
    if (this->acknowledged.load(std::memory_order_relaxed) != request)
    {
      this->acknowledged.store(request, std::memory_order_release);
      this->wakeup.release();
    }
  }

  void check_client_released() { end_cycle(begin_cycle()); }
};
