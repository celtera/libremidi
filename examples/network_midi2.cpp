// Network MIDI 2.0 (UDP) host or client, for trying libremidi against other implementations.
//
//   network_midi2 host   <port> [options]
//   network_midi2 client <host ip> <port> [options]
//
// Options:
//   --secret <text>   shared secret (host: required from clients; client: given when asked)
//   --name <text>     UMP Endpoint name
//   --notes <n>       once connected, send n MIDI 2 note-ons, one every 10 ms
//   --sysex <bytes>   once connected, send one SysEx7 of that many data bytes
//   --seconds <n>     quit after n seconds (default: run until killed)
//
// Everything received is counted; the first packets are printed.

#include <libremidi/backends/net/midi2_udp.hpp>
#include <libremidi/libremidi.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace
{
// SysEx7 packets for `bytes` data bytes with a recognisable pattern
std::vector<uint32_t> sysex7(std::size_t bytes)
{
  std::vector<uint32_t> out;
  for (std::size_t pos = 0; pos < bytes;)
  {
    const std::size_t n = std::min<std::size_t>(6, bytes - pos);
    const uint32_t status
        = (pos == 0 && n == bytes) ? 0 : pos == 0 ? 1 : (pos + n == bytes) ? 3 : 2;
    uint8_t b[6]{};
    for (std::size_t k = 0; k < n; k++)
      b[k] = static_cast<uint8_t>((pos + k) & 0x7F);
    out.push_back(
        (0x3u << 28) | (status << 20) | (static_cast<uint32_t>(n) << 16) | (b[0] << 8) | b[1]);
    out.push_back((uint32_t(b[2]) << 24) | (b[3] << 16) | (b[4] << 8) | b[5]);
    pos += n;
  }
  return out;
}

int usage()
{
  std::puts(
      "usage: network_midi2 host <port> [options]\n"
      "       network_midi2 client <host ip> <port> [options]\n"
      "options: --secret <text> --name <text> --notes <n> --sysex <bytes> --seconds <n>");
  return 2;
}
}

int main(int argc, char** argv)
{
  if (argc < 3)
    return usage();

  const bool host = std::strcmp(argv[1], "host") == 0;
  const bool client = std::strcmp(argv[1], "client") == 0;
  if (!host && !client)
    return usage();
  if (client && argc < 4)
    return usage();

  const std::string address = client ? argv[2] : "0.0.0.0";
  const int port = std::atoi(argv[client ? 3 : 2]);

  libremidi::network_midi2::session_configuration conf;
  conf.endpoint_name = host ? "libremidi host" : "libremidi client";
  conf.product_instance_id = "libremidi-example";
  int notes = 0, seconds = 0;
  std::size_t sysex_bytes = 0;
  for (int i = client ? 4 : 3; i + 1 < argc; i += 2)
  {
    const std::string opt = argv[i];
    if (opt == "--secret")
      conf.shared_secret = argv[i + 1];
    else if (opt == "--name")
      conf.endpoint_name = argv[i + 1];
    else if (opt == "--notes")
      notes = std::atoi(argv[i + 1]);
    else if (opt == "--sysex")
      sysex_bytes = static_cast<std::size_t>(std::atoll(argv[i + 1]));
    else if (opt == "--seconds")
      seconds = std::atoi(argv[i + 1]);
    else
      return usage();
  }

  std::atomic<std::size_t> received_words{0}, received_packets{0};
  std::atomic<uint32_t> checksum{0};

  libremidi::net_ump::midi2_udp_endpoint endpoint{nullptr, conf};
  endpoint.on_ump = [&](std::span<const uint32_t> words) {
    for (std::size_t i = 0; i < words.size();)
    {
      const std::size_t n = libremidi::network_midi2::ump_word_count(words[i]);
      if (i + n > words.size())
        break;
      if (received_packets++ < 8)
      {
        std::printf("received:");
        for (std::size_t k = 0; k < n; k++)
          std::printf(" %08X", words[i + k]);
        std::printf("\n");
        std::fflush(stdout);
      }
      for (std::size_t k = 0; k < n; k++)
        checksum = checksum * 31 + words[i + k];
      i += n;
    }
    received_words += words.size();
  };

  if (host)
  {
    if (auto err = endpoint.open_host(address, port); err != stdx::error{})
    {
      std::printf("could not listen on port %d\n", port);
      return 1;
    }
    std::printf("host: waiting for clients on UDP port %d\n", endpoint.local_port());
  }
  else
  {
    if (auto err = endpoint.open_client(address, port); err != stdx::error{})
    {
      std::printf("could not open a socket towards %s:%d\n", address.c_str(), port);
      return 1;
    }
    std::printf("client: inviting %s:%d\n", address.c_str(), port);
  }
  std::fflush(stdout);

  const auto start = std::chrono::steady_clock::now();
  const auto expired = [&] {
    return seconds > 0 && std::chrono::steady_clock::now() - start > std::chrono::seconds(seconds);
  };

  // Wait for a session
  while (!endpoint.wait_until_established(200ms))
  {
    if (expired())
    {
      std::printf("no session was established\n");
      return 1;
    }
  }
  std::printf("session established\n");
  std::fflush(stdout);

  int failed_sends = 0;
  for (int i = 0; i < notes; i++)
  {
    // Note number i, and i again in the low bits of the velocity word to tell them apart
    const uint32_t ump[2]
        = {0x40900000u | ((uint32_t(i) & 0x7F) << 8), 0x80000000u | uint32_t(i)};
    if (endpoint.send_ump(ump, 2) != stdx::error{})
      failed_sends++;
    std::this_thread::sleep_for(10ms);
  }
  if (notes > 0)
    std::printf("sent %d notes (%d refused)\n", notes, failed_sends);

  if (sysex_bytes > 0)
  {
    const auto words = sysex7(sysex_bytes);
    // In pieces, as an application would hand them over a few at a time
    std::size_t refused = 0;
    for (std::size_t i = 0; i < words.size(); i += 32)
    {
      if (endpoint.send_ump(words.data() + i, std::min<std::size_t>(32, words.size() - i))
          != stdx::error{})
        refused++;
      std::this_thread::sleep_for(1ms);
    }
    std::printf(
        "sent a SysEx7 of %zu bytes: %zu words (%zu sends refused)\n", sysex_bytes, words.size(),
        refused);
  }
  std::fflush(stdout);

  while (!expired())
  {
    std::this_thread::sleep_for(100ms);
    if (seconds == 0 && endpoint.established_sessions() == 0 && client)
      break;
  }

  std::printf(
      "received %zu words in %zu packets, checksum %08X; sessions still open: %zu\n",
      received_words.load(), received_packets.load(), checksum.load(),
      endpoint.established_sessions());
  endpoint.close();
  return 0;
}
