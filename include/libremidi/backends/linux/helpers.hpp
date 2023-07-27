#pragma once
#include <sys/eventfd.h>
#include <sys/timerfd.h>

#include <poll.h>
#include <unistd.h>

#include <iostream>

namespace libremidi
{
struct eventfd_notifier
{
  eventfd_notifier() { this->fd = eventfd(0, EFD_SEMAPHORE | EFD_NONBLOCK); }
  ~eventfd_notifier() { close(this->fd); }

  eventfd_notifier(const eventfd_notifier&) = delete;
  eventfd_notifier(eventfd_notifier&&) = delete;
  eventfd_notifier& operator=(const eventfd_notifier&) = delete;
  eventfd_notifier& operator=(eventfd_notifier&&) = delete;

  void notify() noexcept
  {
    ++notify_counts;
    eventfd_write(fd, 1);
  }
  static bool ready(pollfd res) noexcept { return res.revents & POLLIN; }
  void consume() noexcept
  {
    eventfd_t val;
    eventfd_read(fd, &val);
  }

  operator int() const noexcept { return fd; }
  operator pollfd() const noexcept { return {.fd = fd, .events = POLLIN}; }
  int fd{-1};
  uint32_t notify_counts{};
};

struct timerfd_timer
{
  timerfd_timer() { this->fd = timerfd_create(CLOCK_MONOTONIC, 0); }
  ~timerfd_timer() { close(this->fd); }

  timerfd_timer(const timerfd_timer&) = delete;
  timerfd_timer(timerfd_timer&&) = delete;
  timerfd_timer& operator=(const timerfd_timer&) = delete;
  timerfd_timer& operator=(timerfd_timer&&) = delete;

  void oneshot(int64_t nsec)
  {
    itimerspec t{};
    t.it_value.tv_nsec = nsec;
    timerfd_settime(this->fd, 0, &t, nullptr);
  }

  void restart(int64_t nsec)
  {
    itimerspec t{};
    t.it_value.tv_nsec = nsec;
    t.it_interval.tv_nsec = nsec;
    timerfd_settime(this->fd, 0, &t, nullptr);
  }

  void cancel()
  {
    itimerspec t{};
    timerfd_settime(this->fd, 0, &t, nullptr);
  }

  operator int() const noexcept { return fd; }
  operator pollfd() const noexcept { return {.fd = fd, .events = POLLIN}; }
  int fd{-1};
};
}
