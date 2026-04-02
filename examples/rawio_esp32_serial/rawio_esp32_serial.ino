/// ESP32 Serial MIDI example using libremidi's raw I/O backend.
///
/// Wiring: MIDI IN on Serial2 RX (GPIO 16), MIDI OUT on Serial2 TX (GPIO 17)
/// Standard MIDI baud rate is 31250.
///
/// This sketch receives MIDI on Serial2, parses it through libremidi
/// (giving you structured messages with sysex filtering, etc.),
/// sends MIDI back out on the same serial port,
/// and broadcasts every received message as an OSC /midi UDP packet
/// to the local network (192.168.1.255:5678).

#include <WiFi.h>
#include <WiFiUdp.h>

// Note: you need to have the "include/libremidi" folder copied or 
// symlinked into the sketch folder for this to work, or change the 
// Arduino platform configuration files to add the proper include path.
#define LIBREMIDI_HEADER_ONLY 1
#include <libremidi/configurations.hpp>
#include <libremidi/libremidi.hpp>

// --- Configuration ---
static constexpr int MIDI_BAUD = 31250;
static constexpr int MIDI_RX_PIN = 16;
static constexpr int MIDI_TX_PIN = 17;

static const char* WIFI_SSID = "your-ssid";
static const char* WIFI_PASS = "your-password";

static const IPAddress UDP_BROADCAST{192, 168, 1, 255};
static constexpr uint16_t UDP_PORT = 49444;
static const char* OSC_PATH = "/midi"; // must be 5 chars (+ padding) for the packet layout below

// --- Globals ---

// The callback that libremidi gives us to feed incoming bytes
static libremidi::rawio_input_configuration::receive_callback g_feed_input;

// The libremidi objects need to survive across loop() calls
static std::optional<libremidi::midi_in> g_midi_in;
static std::optional<libremidi::midi_out> g_midi_out;

static WiFiUDP g_udp;

/// Build and send an OSC message containing a single 3-byte MIDI event.
///
/// OSC /midi packet layout (all 4-byte aligned):
///   "/midi\0\0\0"   (8 bytes: pattern + null + padding)
///   ",m\0\0"         (4 bytes: typetag string)
///   [port, b0, b1, b2] (4 bytes: OSC MIDI atom)
void osc_broadcast_midi(uint8_t status, uint8_t d1, uint8_t d2)
{
  // Pre-built packet: only the last 3 bytes change
  uint8_t pkt[16] = {
      // OSC address pattern "/midi" + null + 2 padding zeros
      '/', 'm', 'i', 'd', 'i', 0, 0, 0,
      // OSC type tag ",m" + null + 1 padding zero
      ',', 'm', 0, 0,
      // OSC MIDI data: port (0), status, data1, data2
      0, status, d1, d2};

  g_udp.beginPacket(UDP_BROADCAST, UDP_PORT);
  g_udp.write(pkt, sizeof(pkt));
  g_udp.endPacket();
}

// Called by libremidi whenever a complete MIDI message is received and parsed
void on_midi_message(const libremidi::message& msg)
{
  // Print to USB serial for debugging
  Serial.printf("MIDI [%02X", msg.bytes[0]);
  for (size_t i = 1; i < msg.bytes.size(); i++)
    Serial.printf(" %02X", msg.bytes[i]);
  Serial.println("]");

  // Broadcast as OSC over UDP (channel messages are always 3 bytes)
  if (msg.bytes.size() == 3)
    osc_broadcast_midi(msg.bytes[0], msg.bytes[1], msg.bytes[2]);

  // Echo note-on messages back on serial with velocity halved
  if (msg.get_message_type() == libremidi::message_type::NOTE_ON && g_midi_out)
  {
    uint8_t velocity = msg.bytes[2] / 2;
    g_midi_out->send_message(msg.bytes[0], msg.bytes[1], velocity);
  }
}

void setup()
{
  // USB serial for debug output
  Serial.begin(115200);

  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConnected, IP: %s\n", WiFi.localIP().toString().c_str());

  // Start UDP for OSC broadcast
  g_udp.begin(UDP_PORT);

  // MIDI serial port
  Serial2.begin(MIDI_BAUD, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);

  // Create MIDI input
  g_midi_in.emplace(
      libremidi::input_configuration{
          .on_message = on_midi_message,
          .ignore_sysex = true,
          .ignore_timing = true,
          .ignore_sensing = true,
      },
      libremidi::rawio_input_configuration{
          .set_receive_callback = [](auto cb) { g_feed_input = std::move(cb); },
          .stop_receive = [] { g_feed_input = nullptr; },
      });

  // Create MIDI output (serial)
  g_midi_out.emplace(
      libremidi::output_configuration{},
      libremidi::rawio_output_configuration{
          .write_bytes = [](std::span<const uint8_t> bytes) -> stdx::error {
    Serial2.write(bytes.data(), bytes.size());
    return {};
  }});

  // Open ports to activate the callbacks
  g_midi_in->open_virtual_port("esp32_in");
  g_midi_out->open_virtual_port("esp32_out");

  Serial.println("MIDI ready, broadcasting OSC to " + UDP_BROADCAST.toString() + ":" + UDP_PORT);
}

void loop()
{
  // Read all available bytes from the MIDI serial port
  // and feed them into libremidi for parsing
  int avail = Serial2.available();
  if (avail > 0 && g_feed_input)
  {
    uint8_t buf[64];
    int n = Serial2.readBytes(buf, min(avail, (int)sizeof(buf)));
    g_feed_input({buf, static_cast<size_t>(n)}, 0);
  }
}
