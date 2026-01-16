/*
 * UMP Endpoint API Example
 *
 * This example demonstrates the new MIDI 2.0 bidirectional endpoint API.
 * Unlike the MIDI 1.0 paradigm of separate midi_in/midi_out objects,
 * the ump_endpoint can both send AND receive on the same connection.
 */

#include "libremidi/libremidi.hpp"

#include <libremidi/cmidi2.hpp> // For building UMP messages
#include <libremidi/ump_endpoint.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace libremidi;
using namespace std::chrono_literals;

#if 0
//=============================================================================
// Example 1: Basic Bidirectional Echo
//=============================================================================
// Open an endpoint and echo received messages back (like a MIDI thru)

void example_bidirectional_echo()
{
  std::cout << "\n=== Example 1: Bidirectional Echo ===\n";

  // Configure the endpoint with a message callback
  ump_endpoint_configuration config;
  config.client_name = "libremidi Echo Example";

  // This callback receives messages - the key MIDI 2.0 feature is that
  // we can use the SAME endpoint object to send the echo back!
  ump_endpoint* endpoint_ptr{};
  config.on_message = [&endpoint_ptr](ump msg) {
    std::cout << "Received UMP: type=" << static_cast<int>(msg.get_type())
              << " group=" << static_cast<int>(msg.get_group())
              << " channel=" << static_cast<int>(msg.get_channel())
              << std::endl;
    if (endpoint_ptr)
    {
      endpoint_ptr->send_ump(msg);
    }
  };

  config.on_error
      = [](const auto& err, const auto& loc) { std::cerr << "Error: " << err << std::endl; };

  // Create endpoint and observer
  ump_endpoint endpoint{config};
  endpoint_ptr = &endpoint;
  libremidi::observer observer;

  // Find a bidirectional endpoint
  auto endpoints = observer.get_endpoints();
  if (endpoints.empty())
  {
    std::cout << "No bidirectional endpoints found\n";
    return;
  }

  // Print endpoint info
  const auto& ep = endpoints[1];
  std::cout << "Opening endpoint: " << ep.name << std::endl;
  std::cout << "  Function blocks: " << ep.function_blocks.size() << std::endl;
  for (const auto& fb : ep.function_blocks)
  {
    std::cout << "    Block " << static_cast<int>(fb.block_id) << ": " << fb.name;
    switch (fb.direction)
    {
      case function_block_direction::input:
        std::cout << " (input)";
        break;
      case function_block_direction::output:
        std::cout << " (output)";
        break;
      case function_block_direction::bidirectional:
        std::cout << " (bidirectional)";
        break;
    }
    std::cout << " groups " << static_cast<int>(fb.groups.first_group)
              << "-" << static_cast<int>(fb.groups.last_group()) << std::endl;
  }

  // Open the endpoint
  if (auto err = endpoint.open(ep); err.is_set())
  {
    err.throw_exception();
    return;
  }

  std::cout << "Endpoint open. Press Ctrl+C to exit.\n";

  // Keep running until interrupted
  // In a real application, you'd have proper signal handling
  std::this_thread::sleep_for(30s);

  endpoint.close();
}

//=============================================================================
// Example 2: Discovery with Detailed Information
//=============================================================================
// Enumerate all UMP endpoints and their function blocks

void example_endpoint_discovery()
{
  std::cout << "\n=== Example 2: Endpoint Discovery ===\n";

  observer_configuration config;
  config.track_hardware = true;
  config.track_virtual = true;
  config.track_network = true;

  // Set up callbacks for hotplug events
  config.endpoint_added = [](const ump_endpoint_info& ep) {
    std::cout << "[ADDED] " << ep.name << std::endl;
  };
  config.endpoint_removed = [](const ump_endpoint_info& ep) {
    std::cout << "[REMOVED] " << ep.name << std::endl;
  };

  observer observer{config};

  auto endpoints = observer.get_endpoints();
  std::cout << "Found " << endpoints.size() << " UMP endpoints:\n\n";

  for (const auto& ep : endpoints)
  {
    std::cout << "Endpoint: " << ep.name << std::endl;
    std::cout << "  Display name: " << ep.display_name << std::endl;
    std::cout << "  Manufacturer: " << ep.manufacturer << std::endl;

    // Protocol support
    std::cout << "  Protocols: ";
    if (ep.supports_midi1())
      std::cout << "MIDI1 ";
    if (ep.supports_midi2())
      std::cout << "MIDI2 ";
    std::cout << "(active: "
              << (ep.active_protocol == midi_protocol::midi2 ? "MIDI2" : "MIDI1")
              << ")" << std::endl;

    // JR Timestamps
    if (ep.jr_timestamps.can_receive || ep.jr_timestamps.can_transmit)
    {
      std::cout << "  JR Timestamps: ";
      if (ep.jr_timestamps.can_receive)
        std::cout << "RX ";
      if (ep.jr_timestamps.can_transmit)
        std::cout << "TX";
      std::cout << std::endl;
    }

    // Device identity
    if (ep.device_identity)
    {
      std::cout << "  Device Identity:" << std::endl;
      std::cout << "    Manufacturer: 0x"
                << std::hex << ep.device_identity->manufacturer_id << std::dec << std::endl;
      std::cout << "    Family: 0x"
                << std::hex << ep.device_identity->family_id << std::dec << std::endl;
      std::cout << "    Model: 0x"
                << std::hex << ep.device_identity->model_id << std::dec << std::endl;
    }

    // Function blocks
    std::cout << "  Function Blocks (" << ep.function_blocks.size() << "):" << std::endl;
    for (const auto& fb : ep.function_blocks)
    {
      std::cout << "    [" << static_cast<int>(fb.block_id) << "] " << fb.name;
      if (!fb.active)
        std::cout << " (inactive)";
      std::cout << std::endl;

      const char* dir_str = "unknown";
      switch (fb.direction)
      {
        case function_block_direction::input:
          dir_str = "INPUT";
          break;
        case function_block_direction::output:
          dir_str = "OUTPUT";
          break;
        case function_block_direction::bidirectional:
          dir_str = "BIDIRECTIONAL";
          break;
      }
      std::cout << "      Direction: " << dir_str << std::endl;
      std::cout << "      Groups: " << static_cast<int>(fb.groups.first_group)
                << "-" << static_cast<int>(fb.groups.last_group())
                << " (" << static_cast<int>(fb.groups.num_groups) << " groups)" << std::endl;

      if (fb.supports_midi_ci())
        std::cout << "      MIDI-CI: v" << static_cast<int>(fb.midi_ci_version) << std::endl;
    }
    std::cout << std::endl;
  }
}

//=============================================================================
// Example 3: Function Block Filtering
//=============================================================================
// Open an endpoint filtered to a specific function block

void example_function_block_filtering()
{
  std::cout << "\n=== Example 3: Function Block Filtering ===\n";

  ump_endpoint_observer observer{};
  auto endpoints = observer.get_endpoints();

  // Find an endpoint with multiple function blocks
  const ump_endpoint_info* multi_block_ep = nullptr;
  for (const auto& ep : endpoints)
  {
    if (ep.function_blocks.size() > 1)
    {
      multi_block_ep = &ep;
      break;
    }
  }

  if (!multi_block_ep)
  {
    std::cout << "No endpoint with multiple function blocks found\n";
    return;
  }

  std::cout << "Using endpoint: " << multi_block_ep->name << std::endl;
  std::cout << "Function blocks: " << multi_block_ep->function_blocks.size() << std::endl;

  // Configure to filter messages for a specific function block
  ump_endpoint_configuration config;
  config.client_name = "Function Block Filter Example";

  // Only receive messages from function block 0
  config.input_filter = group_filter::block(0);

  // Only send to function block 1 (if it exists and is output-capable)
  if (multi_block_ep->function_blocks.size() > 1 &&
      multi_block_ep->function_blocks[1].can_receive())
  {
    config.output_filter = group_filter::block(1);
  }

  config.on_message = [](ump msg) {
    std::cout << "Message from block 0, group " << static_cast<int>(msg.get_group())
              << std::endl;
  };

  ump_endpoint endpoint{config};
  if (auto err = endpoint.open(*multi_block_ep); err.is_set())
  {
    err.throw_exception();
    return;
  }

  std::cout << "Filtering active. Messages from block 0 only.\n";
  std::this_thread::sleep_for(10s);
}

//=============================================================================
// Example 4: Sending MIDI 2.0 Messages
//=============================================================================
// Demonstrate sending high-resolution MIDI 2.0 messages

void example_midi2_messages()
{
  std::cout << "\n=== Example 4: MIDI 2.0 Messages ===\n";

  ump_endpoint_observer observer{};
  auto endpoints = observer.get_endpoints_with_output();

  if (endpoints.empty())
  {
    std::cout << "No output endpoints found\n";
    return;
  }

  // Prefer MIDI 2.0 protocol
  ump_endpoint_configuration config;
  config.protocol = protocol_preference::prefer_midi2;
  config.client_name = "MIDI 2.0 Sender Example";

  ump_endpoint endpoint{config};
  if (auto err = endpoint.open(endpoints[0]); err.is_set())
  {
    err.throw_exception();
    return;
  }

  std::cout << "Connected to: " << endpoints[0].name << std::endl;
  std::cout << "Active protocol: "
            << (endpoint.get_active_protocol() == midi_protocol::midi2 ? "MIDI 2.0" : "MIDI 1.0")
            << std::endl;

  // Build MIDI 2.0 messages using cmidi2
  uint8_t group = 0;
  uint8_t channel = 0;

  // MIDI 2.0 Note On with full velocity resolution (16-bit)
  // and per-note pitch (via attribute)
  uint32_t note_on[2];
  note_on[0] = cmidi2_ump_midi2_note_on(group, channel, 60, 0, 0xC000, 0);
  note_on[1] = 0;  // velocity in word 1 for MIDI 2.0

  if (auto err = endpoint.send_ump(note_on[0], note_on[1]); err.is_set())
  {
    err.throw_exception();
    return;
  }
  else
  {
    std::cout << "Sent MIDI 2.0 Note On (note 60, velocity 0xC000)\n";
  }

  std::this_thread::sleep_for(500ms);

  // MIDI 2.0 Pitch Bend with 32-bit resolution
  uint32_t pitch_bend[2];
  pitch_bend[0] = cmidi2_ump_midi2_pitch_bend_direct(group, channel, 0x80000000);
  pitch_bend[1] = 0;

  endpoint.send_ump(pitch_bend[0], pitch_bend[1]);
  std::cout << "Sent MIDI 2.0 Pitch Bend (center)\n";

  std::this_thread::sleep_for(500ms);

  // Note Off
  uint32_t note_off[2];
  note_off[0] = cmidi2_ump_midi2_note_off(group, channel, 60, 0, 0x8000, 0);
  note_off[1] = 0;

  endpoint.send_ump(note_off[0], note_off[1]);
  std::cout << "Sent MIDI 2.0 Note Off\n";
}

//=============================================================================
// Example 5: Virtual Endpoint Creation
//=============================================================================
// Create a virtual endpoint that other applications can connect to

void example_virtual_endpoint()
{
  std::cout << "\n=== Example 5: Virtual Endpoint ===\n";

  ump_endpoint_configuration config;
  config.client_name = "libremidi Virtual Synth";
  config.create_virtual = true;
  config.virtual_direction = function_block_direction::bidirectional;

  config.on_message = [](ump msg) {
    // Process incoming messages
    if (msg.get_type() == midi2::message_type::MIDI_2_CHANNEL)
    {
      auto status = msg.get_status_code();
      if (status == 0x90)
      {
        // Note On
        std::cout << "Virtual synth received Note On: note="
                  << static_cast<int>((msg.data[0] >> 8) & 0x7F) << std::endl;
      }
    }
  };

  ump_endpoint endpoint{config};

  // Create virtual endpoint
  if (auto err = endpoint.open_virtual("Virtual MIDI 2.0 Synth"); err.is_set())
  {
    err.throw_exception();
    return;
  }

  std::cout << "Virtual endpoint created. Connect to it from another app.\n";
  std::cout << "Waiting for connections...\n";

  // Run for a while
  std::this_thread::sleep_for(60s);
}

//=============================================================================
// Main
//=============================================================================

int main(int argc, char* argv[])
{
  std::cout << "libremidi UMP Endpoint API Examples\n";
  std::cout << "====================================\n";

  // Check if UMP backends are available
  if (!ump_backend_available())
  {
    std::cerr << "No UMP backend available on this system.\n";
    std::cerr << "Requirements:\n";
    std::cerr << "  - Windows 11 with MIDI Services\n";
    std::cerr << "  - macOS 11+\n";
    std::cerr << "  - Linux kernel 6.5+ with UMP support\n";
    return 1;
  }

  std::cout << "Default UMP API: " << static_cast<int>(default_ump_api()) << std::endl;

  auto apis = available_ump_apis();
  std::cout << "Available UMP APIs: " << apis.size() << std::endl;

  // Run examples
  try
  {
    example_endpoint_discovery();

    // Uncomment to run interactive examples:
    example_bidirectional_echo();
    // example_function_block_filtering();
    // example_midi2_messages();
    // example_virtual_endpoint();
  }
  catch (const std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
#endif
int main() { }
