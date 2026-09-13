// SPDX-License-Identifier: BSL-1.0
//
// Regression test: a per-object error raised on the core resource must leave
// the context connected. The daemon reports "you asked about an object I no
// longer have" with the same event it uses for the loss of the connection, and
// both arrive with id == PW_ID_CORE, so only res tells them apart. This is the
// case pipewire_context_error_scope does not cover, which tests id != PW_ID_CORE.
//
// Provoked by asking for a factory that does not exist and destroying the proxy
// before the loop can deliver the daemon's remove_id.
//
// Requires a running PipeWire daemon; skips (exit 0) if none is reachable.

#include <libremidi/backends/linux/pipewire/context.hpp>
#include <libremidi/backends/linux/pipewire/instance.hpp>
#include <libremidi/backends/linux/pipewire/loader.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace lpw = libremidi::pipewire;

static void arm_watchdog(int seconds)
{
  std::thread(
      [seconds]
      {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        std::fprintf(stderr, "FAIL: watchdog timeout (%ds) - likely deadlock\n", seconds);
        std::fflush(stderr);
        std::_Exit(EXIT_FAILURE);
      })
      .detach();
}

static bool check(bool cond, const char* what)
{
  if (!cond)
    std::fprintf(stderr, "FAIL: %s\n", what);
  return cond;
}

// The classification on its own, so a regression is named rather than merely
// timed out on. Only a socket failure ends the connection, and the protocol
// reports that one on the core.
static bool test_classification()
{
  bool ok = true;
  ok &= check(
      !lpw::context::connection_lost(PW_ID_CORE, -ENOENT),
      "-ENOENT on the core is a missing object, not a lost connection");
  ok &= check(
      !lpw::context::connection_lost(PW_ID_CORE, -ESTALE),
      "-ESTALE on the core is a global that went away, not a lost connection");
  ok &= check(
      !lpw::context::connection_lost(PW_ID_CORE, -EPERM),
      "-EPERM on the core is a refused request, not a lost connection");
  ok &= check(
      !lpw::context::connection_lost(42, -EPIPE), "an error on an object is never the connection");
  ok &= check(
      lpw::context::connection_lost(PW_ID_CORE, -EPIPE), "-EPIPE on the core is the connection");
  ok &= check(
      lpw::context::connection_lost(PW_ID_CORE, -ECONNRESET),
      "-ECONNRESET on the core is the connection");
  return ok;
}

int main()
{
  auto& pw = lpw::load();
  if (!pw.thread_available)
  {
    std::printf("libpipewire thread-loop not available; skipping\n");
    return 0;
  }

  if (!test_classification())
    return EXIT_FAILURE;

  auto inst = lpw::shared_instance();
  if (!inst)
  {
    std::printf("pw_init failed; skipping\n");
    return 0;
  }

  auto ctx = lpw::context::make(inst);
  if (!ctx || !ctx->ok())
  {
    std::printf("cannot connect to pipewire daemon; skipping\n");
    return 0;
  }

  arm_watchdog(60);

  const std::uint32_t gen0 = ctx->generation();

  for (int i = 0; i < 8; ++i)
  {
    // Two errors per round trip: the daemon refuses the factory (-ENOENT on the
    // new id), then answers the destroy of an id it has already dropped
    // (-ENOENT on the core, "unknown resource").
    pw_proxy* proxy = nullptr;
    ctx->with_lock(
        [&]
        {
          auto* props = pw.properties_new("libremidi.test", "object-errors", nullptr);
          if (!props)
            return;
          proxy = reinterpret_cast<pw_proxy*>(pw_core_create_object(
              ctx->pw_core_ptr(), "libremidi-no-such-factory", PW_TYPE_INTERFACE_Link,
              PW_VERSION_LINK, &props->dict, 0));
          pw.properties_free(props);

          // Deliberately no synchronize() in between: the destroy has to reach
          // the daemon before its remove_id reaches us, which is what makes the
          // daemon answer on the core rather than on the object.
          if (proxy && pw.proxy_destroy)
            pw.proxy_destroy(proxy);
        });

    if (!proxy)
    {
      std::printf("daemon did not hand out a proxy for a bad factory; skipping\n");
      return 0;
    }

    if (!ctx->synchronize())
      return check(false, "synchronize() failed after a per-object error") ? 0 : EXIT_FAILURE;

    if (!ctx->ok())
      return check(false, "context went to broken on a per-object error") ? 0 : EXIT_FAILURE;
  }

  // The connection is the one it started with: nothing reconnected under us.
  if (!check(ctx->generation() == gen0, "context reconnected instead of staying up"))
    return EXIT_FAILURE;

  std::printf("PASS: pipewire_context_object_errors\n");
  return 0;
}
