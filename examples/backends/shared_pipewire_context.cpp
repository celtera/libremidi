// SPDX-License-Identifier: BSL-1.0
//
// Connect to the pipewire daemon, print the initial graph, then watch
// live additions / removals for --duration seconds.

#include <libremidi/backends/linux/pipewire/context.hpp>
#include <libremidi/backends/linux/pipewire/filter.hpp>
#include <libremidi/backends/linux/pipewire/format.hpp>
#include <libremidi/backends/linux/pipewire/instance.hpp>
#include <libremidi/backends/linux/pipewire/loader.hpp>
#include <libremidi/backends/linux/pipewire/stream.hpp>
#include <libremidi/backends/linux/pipewire/subscription.hpp>
#include <libremidi/backends/linux/pipewire/types.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace lpw = libremidi::pipewire;

static const char* kind_name(lpw::media_class k)
{
  switch (k)
  {
    case lpw::media_class::audio:
      return "audio";
    case lpw::media_class::midi:
      return "midi";
    case lpw::media_class::ump:
      return "ump";
    case lpw::media_class::video:
      return "video";
    default:
      return "other";
  }
}

static void print_node(const lpw::node_info& n)
{
  std::printf(
      "  node id=%u kind=%-5s name=%s%s%s\n", n.id, kind_name(n.kind),
      n.name.c_str(), n.description.empty() ? "" : "  desc=",
      n.description.c_str());
}

int main(int argc, char** argv)
{
  int seconds = 5;
  for (int i = 1; i < argc - 1; ++i)
  {
    if (std::string{argv[i]} == "--duration")
      seconds = std::atoi(argv[i + 1]);
  }

  auto& pw = lpw::load();
  if (!pw.available)
  {
    std::fprintf(
        stderr,
        "libpipewire-0.3 not available at runtime; aborting.\n"
        "(Install pipewire and re-run; the executable itself has no\n"
        " DT_NEEDED for libpipewire, so it can start anywhere.)\n");
    return 1;
  }
  std::printf(
      "loaded libpipewire-0.3 (%s)\n",
      pw.get_library_version ? pw.get_library_version() : "version unknown");
  std::printf(
      "  core_available=%d thread_available=%d stream_available=%d "
      "filter_available=%d\n",
      pw.core_available, pw.thread_available, pw.stream_available,
      pw.filter_available);

  auto ctx = lpw::shared_context();
  if (!ctx)
  {
    std::fprintf(stderr, "shared_context() returned empty\n");
    return 2;
  }
  if (!ctx->ok())
  {
    std::fprintf(
        stderr,
        "context not in connected state (state=%d) — daemon down?\n",
        static_cast<int>(ctx->state()));
    return 3;
  }
  std::printf("connected to pipewire daemon\n");

  // Subscribe before snapshot or we miss late globals.
  auto sub_state = ctx->on_state_changed([](lpw::connection_state s) {
    const char* names[] = {"connecting", "connected", "broken",
                           "disconnected"};
    std::printf(
        "[state] -> %s\n",
        names[static_cast<int>(s)]);
  });
  auto sub_add = ctx->on_node_added([](const lpw::node_info& n) {
    std::printf("[+node] id=%u %s (%s)\n", n.id, n.name.c_str(),
                kind_name(n.kind));
  });
  auto sub_rm = ctx->on_node_removed([](std::uint32_t id) {
    std::printf("[-node] id=%u\n", id);
  });

  auto snap = ctx->snapshot();
  std::printf("\n== Initial graph snapshot (%zu nodes) ==\n", snap.nodes.size());
  for (auto k : {lpw::media_class::audio, lpw::media_class::midi,
                 lpw::media_class::ump, lpw::media_class::video,
                 lpw::media_class::other})
  {
    auto subset = snap.nodes_of(k);
    if (subset.empty())
      continue;
    std::printf("[%s] %zu nodes\n", kind_name(k), subset.size());
    for (const auto& n : subset)
      print_node(n);
  }

  std::printf("\n== Watching for live changes for %d seconds ==\n", seconds);
  std::this_thread::sleep_for(std::chrono::seconds(seconds));

  std::printf("\nclean shutdown\n");
  return 0;
}
