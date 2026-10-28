#pragma once
// clang-format off
#include <libremidi/config.hpp>
// clang-format on

#include <libremidi/cmidi2.hpp>
#include <libremidi/error.hpp>
#include <libremidi/message.hpp>
#include <libremidi/ump.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>

NAMESPACE_LIBREMIDI
{
struct cmidi2_error_domain : public stdx::error_domain
{
public:
  constexpr cmidi2_error_domain() noexcept
      : error_domain{{0x636d696469325f5fULL, 0x636f6e7665727400ULL}}
  {
  }

  stdx::string_ref name() const noexcept override { return "cmidi2_conversion"; }

  bool equivalent(const stdx::error& lhs, const stdx::error& rhs) const noexcept override
  {
    if (lhs.domain() == rhs.domain())
      return error_cast<cmidi2_midi_conversion_result>(lhs)
             == error_cast<cmidi2_midi_conversion_result>(rhs);

    return false;
  }

  stdx::string_ref message(const stdx::error& e) const noexcept override
  {
    const auto status = error_cast<cmidi2_midi_conversion_result>(e);
    switch (status)
    {
      case CMIDI2_CONVERSION_RESULT_OK:
        return "Success";
      case CMIDI2_CONVERSION_RESULT_OUT_OF_SPACE:
        return "Destination buffer is too small";
      case CMIDI2_CONVERSION_RESULT_INVALID_SYSEX:
        return "Invalid System Exclusive message format";
      case CMIDI2_CONVERSION_RESULT_INVALID_DTE_SEQUENCE:
        return "Invalid Data Entry, RPN, or NRPN sequence";
      case CMIDI2_CONVERSION_RESULT_INVALID_STATUS:
        return "Invalid or unsupported MIDI status byte";
      case CMIDI2_CONVERSION_RESULT_INCOMPLETE_SYSEX7:
        return "Incomplete 7-bit System Exclusive message";
      case CMIDI2_CONVERSION_RESULT_INVALID_INPUT:
        return "Invalid input data length or content";
      default:
        return "Unknown cmidi2 conversion error";
    }
  }
};

inline stdx::error from_cmidi2_result(cmidi2_midi_conversion_result ret) noexcept
{
  static constexpr cmidi2_error_domain domain{};
  // We explicitly map OK to success (0 value error) just in case,
  // though usually error{} is default constructed for success.
  if (ret == CMIDI2_CONVERSION_RESULT_OK)
    return stdx::error{};

  return {ret, domain};
}
}

LIBREMIDI_STATIC void cmidi2_reverse(int64_t v, cmidi2_ump* output)
{
  union
  {
    uint32_t u[2];
    int64_t i;
  } e0, e1;
  e0.i = v;
  e1.u[0] = e0.u[1];
  e1.u[1] = e0.u[0];
  memcpy(output, &e1, 8);
}

LIBREMIDI_STATIC auto u7_to_u16(uint8_t in) -> uint16_t
{
  static constexpr auto ratio = float(std::numeric_limits<uint16_t>::max()) / 127.f;
  return std::clamp(std::round(in * ratio), 0.f, float(std::numeric_limits<uint16_t>::max()));
};

LIBREMIDI_STATIC auto u7_to_u32(uint8_t in) -> uint32_t
{
  static constexpr auto ratio = double(std::numeric_limits<uint32_t>::max()) / 127.;
  return std::clamp(std::round(in * ratio), 0., double(std::numeric_limits<uint32_t>::max()));
};

LIBREMIDI_STATIC auto u16_to_u32(uint16_t in) -> uint32_t
{
  static constexpr auto ratio = double(std::numeric_limits<uint32_t>::max())
                                / double(std::numeric_limits<uint16_t>::max());
  return std::clamp(std::round(in * ratio), 0., double(std::numeric_limits<uint32_t>::max()));
};

//! MIDI 2 min-center-max upscaling (UMP specification, Appendix D.3.2): 0,
//! the center and the maximum land on 0, the center and the maximum of 32 bits,
//! so that a centered MIDI 1 pitch bend (8192) stays centered (0x80000000).
LIBREMIDI_STATIC auto u14_to_u32(uint16_t in) -> uint32_t
{
  return cmidi2_upscale_14_to_32(in);
}

LIBREMIDI_STATIC bool
cmidi2_midi1_channel_voice_to_midi2(const uint8_t* bytes, std::size_t sz, cmidi2_ump* output)
{
  if (sz < 2)
    return false;
  if (sz > 3)
    return false;
  const auto status = libremidi::message_type(bytes[0] & 0xF0);
  const int channel = bytes[0] & 0x0F;
  const int group = 0;
  switch (status)
  {
    case libremidi::message_type::NOTE_OFF: {
      const int64_t msg
          = cmidi2_ump_midi2_note_off(group, channel, bytes[1], 0, u7_to_u16(bytes[2]), 0);
      cmidi2_reverse(msg, output);
      break;
    }
    case libremidi::message_type::NOTE_ON: {
      const int64_t msg
          = cmidi2_ump_midi2_note_on(group, channel, bytes[1], 0, u7_to_u16(bytes[2]), 0);
      cmidi2_reverse(msg, output);
      break;
    }
    case libremidi::message_type::POLY_PRESSURE: {
      const int64_t msg = cmidi2_ump_midi2_paf(group, channel, bytes[1], u7_to_u32(bytes[2]));
      cmidi2_reverse(msg, output);
      break;
    }
    case libremidi::message_type::CONTROL_CHANGE: {
      const int64_t msg = cmidi2_ump_midi2_cc(group, channel, bytes[1], u7_to_u32(bytes[2]));
      cmidi2_reverse(msg, output);
      break;
    }
    case libremidi::message_type::PROGRAM_CHANGE: {
      const int64_t msg = cmidi2_ump_midi2_program(group, channel, 0, bytes[1], 0, 0);
      cmidi2_reverse(msg, output);
      break;
    }
    case libremidi::message_type::AFTERTOUCH: {
      const int64_t msg = cmidi2_ump_midi2_caf(group, channel, u7_to_u32(bytes[1]));
      cmidi2_reverse(msg, output);
      break;
    }
    case libremidi::message_type::PITCH_BEND: {
      const auto pb = bytes[1] + bytes[2] * 0x80;
      const int64_t msg = cmidi2_ump_midi2_pitch_bend_direct(group, channel, u14_to_u32(pb));
      cmidi2_reverse(msg, output);
      break;
    }
    default:
      return false;
  }

  return true;
}

LIBREMIDI_STATIC bool
cmidi2_ump_upgrade_midi1_channel_voice_to_midi2(const cmidi2_ump* input, cmidi2_ump* output)
{
  if (cmidi2_ump_get_message_type(input) != CMIDI2_MESSAGE_TYPE_MIDI_1_CHANNEL)
    return false;

  const auto group = cmidi2_ump_get_group(input);
  const auto channel = cmidi2_ump_get_channel(input);
  const auto status = cmidi2_ump_get_status_code(input);
  const auto b1 = cmidi2_ump_get_midi1_byte2(input);
  const auto b2 = cmidi2_ump_get_midi1_byte3(input);

  switch (status)
  {
    // Not in midi 1:
    case CMIDI2_STATUS_PER_NOTE_RCC:
    case CMIDI2_STATUS_PER_NOTE_ACC:
    case CMIDI2_STATUS_RPN:
    case CMIDI2_STATUS_NRPN:
    case CMIDI2_STATUS_RELATIVE_RPN:
    case CMIDI2_STATUS_RELATIVE_NRPN:
    case CMIDI2_STATUS_PER_NOTE_PITCH_BEND:
    case CMIDI2_STATUS_PER_NOTE_MANAGEMENT:
      return false;

    case CMIDI2_STATUS_NOTE_OFF: {
      const int64_t msg = cmidi2_ump_midi2_note_off(group, channel, b1, 0, u7_to_u16(b2), 0);
      cmidi2_reverse(msg, output);
      break;
    }
    case CMIDI2_STATUS_NOTE_ON: {
      const int64_t msg = cmidi2_ump_midi2_note_on(group, channel, b1, 0, u7_to_u16(b2), 0);
      cmidi2_reverse(msg, output);
      break;
    }
    case CMIDI2_STATUS_PAF: {
      const int64_t msg = cmidi2_ump_midi2_paf(group, channel, b1, u7_to_u32(b2));
      cmidi2_reverse(msg, output);
      break;
    }
    case CMIDI2_STATUS_CC: {
      int64_t msg = cmidi2_ump_midi2_cc(group, channel, b1, u7_to_u32(b2));
      cmidi2_reverse(msg, output);
      break;
    }
    case CMIDI2_STATUS_PROGRAM: {
      const int64_t msg = cmidi2_ump_midi2_program(group, channel, 0, b1, 0, 0);
      cmidi2_reverse(msg, output);
      break;
    }
    case CMIDI2_STATUS_CAF: {
      const int64_t msg = cmidi2_ump_midi2_caf(group, channel, u7_to_u32(b1));
      cmidi2_reverse(msg, output);
      break;
    }
    case CMIDI2_STATUS_PITCH_BEND: {
      const auto pb = b1 + b2 * 0x80;
      const int64_t msg = cmidi2_ump_midi2_pitch_bend_direct(group, channel, u14_to_u32(pb));
      cmidi2_reverse(msg, output);
      break;
    }
  }

  return true;
}

NAMESPACE_LIBREMIDI
{
LIBREMIDI_STATIC libremidi::message midi1_from_ump(libremidi::ump u)
{
  libremidi::message ret;
  ret.bytes.resize(4);
  if (auto n = cmidi2_convert_single_ump_to_midi1((uint8_t*)ret.bytes.data(), 4, u.data))
  {
    ret.timestamp = u.timestamp;
    ret.bytes.resize(n);
  }
  else
  {
    ret.bytes.clear();
  }
  return ret;
}

LIBREMIDI_STATIC libremidi::ump ump_from_midi1(libremidi::message u)
{
  libremidi::ump ret;
  if (!u.bytes.empty())
  {
    ret.timestamp = u.timestamp;
    cmidi2_midi1_channel_voice_to_midi2(u.bytes.data(), u.bytes.size(), ret.data);
  }
  return ret;
}

//! The number of 32-bit words of the UMP packet that starts with @p first_word,
//! from its message type (UMP specification, section 2.1.4).
LIBREMIDI_STATIC constexpr int ump_packet_words(uint32_t first_word) noexcept
{
  constexpr int words[16]{1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4};
  return words[first_word >> 28];
}

/**
 * Calls @p on_packet with each packet of the UMP stream @p words, in order, as
 * one libremidi::ump stamped with @p timestamp.
 *
 * A message longer than one packet, system-exclusive, is a sequence of
 * packets: each is a libremidi::ump of its own. A last packet cut short by
 * the end of the stream is not given.
 */
LIBREMIDI_STATIC void for_each_ump_packet(
    const uint32_t* words, std::size_t count, int64_t timestamp, auto&& on_packet)
{
  std::size_t at = 0;
  while (at < count)
  {
    const auto n = std::size_t(ump_packet_words(words[at]));
    if (at + n > count)
      break;
    libremidi::ump packet;
    std::copy_n(words + at, n, packet.data);
    packet.timestamp = timestamp;
    on_packet(std::as_const(packet));
    at += n;
  }
}

struct midi1_to_midi2
{
  /**
   * Like convert(), but calls @p on_packet once per UMP packet: a
   * system-exclusive message gives every SysEx7 packet it is made of.
   */
  stdx::error convert_packets(
      const unsigned char* message, std::size_t size, int64_t timestamp, auto on_packet)
  {
    return convert(message, size, timestamp, [&](const uint32_t* words, std::size_t count, int64_t ts) {
      for_each_ump_packet(words, count, ts, on_packet);
      return stdx::error{};
    });
  }

  stdx::error
  convert(const unsigned char* message, std::size_t size, int64_t timestamp, auto on_ump)
  {
    context.midi1 = const_cast<unsigned char*>(message);
    context.midi1_num_bytes = size;
    context.midi1_proceeded_bytes = 0;
    context.ump = ump;
    context.ump_num_bytes = sizeof(ump);
    context.ump_proceeded_bytes = 0;
    context.skip_delta_time = true;

    switch (auto err = cmidi2_convert_midi1_to_ump(&context))
    {
      case CMIDI2_CONVERSION_RESULT_OK: {
        if (auto n = context.ump_proceeded_bytes; n > 0)
          return on_ump(context.ump, context.ump_proceeded_bytes / 4, timestamp);
        else
          return std::errc::no_message;
      }
      case CMIDI2_CONVERSION_RESULT_OUT_OF_SPACE:
      case CMIDI2_CONVERSION_RESULT_INVALID_SYSEX:
      case CMIDI2_CONVERSION_RESULT_INVALID_DTE_SEQUENCE:
      case CMIDI2_CONVERSION_RESULT_INVALID_STATUS:
      case CMIDI2_CONVERSION_RESULT_INCOMPLETE_SYSEX7:
      case CMIDI2_CONVERSION_RESULT_INVALID_INPUT:
        return from_cmidi2_result(err);
      default:
        return std::errc::operation_not_supported;
    }
  }

  cmidi2_midi_conversion_context context = [] {
    cmidi2_midi_conversion_context tmp;
    cmidi2_midi_conversion_context_initialize(&tmp);
    return tmp;
  }();

  uint32_t ump[65536 / 4];
};

struct midi2_to_midi1
{
  stdx::error convert(const uint32_t* message, std::size_t size, int64_t timestamp, auto on_midi)
  {
    context.midi1 = static_cast<unsigned char*>(midi);
    context.midi1_num_bytes = sizeof(midi);
    context.midi1_proceeded_bytes = 0;
    context.ump = const_cast<uint32_t*>(message);
    context.ump_num_bytes = size * sizeof(uint32_t);
    context.ump_proceeded_bytes = 0;
    context.skip_delta_time = true;

    switch (auto err = cmidi2_convert_ump_to_midi1(&context))
    {
      case CMIDI2_CONVERSION_RESULT_OK: {
        if (auto n = context.midi1_proceeded_bytes; n > 0)
          return on_midi(midi, n, timestamp);
        else
          return std::errc::no_message;
      }
      case CMIDI2_CONVERSION_RESULT_OUT_OF_SPACE:
      case CMIDI2_CONVERSION_RESULT_INVALID_SYSEX:
      case CMIDI2_CONVERSION_RESULT_INVALID_DTE_SEQUENCE:
      case CMIDI2_CONVERSION_RESULT_INVALID_STATUS:
      case CMIDI2_CONVERSION_RESULT_INCOMPLETE_SYSEX7:
      case CMIDI2_CONVERSION_RESULT_INVALID_INPUT:
        return from_cmidi2_result(err);
      default:
        return std::errc::operation_not_supported;
    }
  }

  cmidi2_midi_conversion_context context = [] {
    cmidi2_midi_conversion_context tmp;
    cmidi2_midi_conversion_context_initialize(&tmp);
    return tmp;
  }();

  uint8_t midi[65536];
};


/**
 * Turns a stream of UMP packets into MIDI 1 messages, one packet at a time,
 * joining the SysEx7 packets of a system-exclusive message into one
 * F0 ... F7 message.
 *
 * Packets come from a real-time thread: nothing here allocates. A
 * system-exclusive message longer than @p Capacity bytes, F0 and F7 included,
 * is dropped whole, as is one whose packets do not follow start, continue...,
 * end; dropped() counts them.
 *
 * One message is assembled at a time: SysEx7 packets of another group
 * interleaved with it break both. Packets of other types can come in between
 * and are converted as they come.
 */
template <std::size_t Capacity = 4096>
class ump_to_midi1_stream
{
  static_assert(Capacity >= 8, "a SysEx7 packet holds up to 6 bytes, plus F0 and F7");

public:
  /**
   * The MIDI 1 message @p packet completes: a channel or system message, a
   * system-exclusive one held in this packet, or the end of one started
   * earlier. Empty when it completes none.
   *
   * Valid until the next call.
   */
  std::span<const uint8_t> feed(const libremidi::ump& packet) noexcept
  {
    if (packet.get_type() != midi2::message_type::SYSEX7)
    {
      const auto n = cmidi2_convert_single_ump_to_midi1(
          m_short, sizeof(m_short), const_cast<uint32_t*>(packet.data));
      return {m_short, n};
    }

    const uint8_t status = (packet.data[0] >> 20) & 0xF;
    const uint8_t count = (packet.data[0] >> 16) & 0xF;
    if (count > 6)
    {
      abandon();
      ++m_dropped;
      return {};
    }

    switch (status)
    {
      case 0: // Complete in one packet
        abandon();
        m_size = 0;
        m_buffer[m_size++] = 0xF0;
        append(packet, count);
        m_buffer[m_size++] = 0xF7;
        return complete();

      case 1: // Start
        abandon();
        m_state = state::assembling;
        m_size = 0;
        m_buffer[m_size++] = 0xF0;
        append(packet, count);
        return {};

      case 2: // Continue
        if (m_state == state::idle)
        {
          ++m_dropped;
          return {};
        }
        append(packet, count);
        return {};

      case 3: // End
        if (m_state == state::idle)
        {
          ++m_dropped;
          return {};
        }
        append(packet, count);
        if (m_state == state::overflow)
        {
          m_state = state::idle;
          ++m_dropped;
          return {};
        }
        m_buffer[m_size++] = 0xF7;
        return complete();

      default:
        abandon();
        ++m_dropped;
        return {};
    }
  }

  //! Forgets a system-exclusive message in progress, without counting it.
  void reset() noexcept
  {
    m_state = state::idle;
    m_size = 0;
  }

  //! Whether a system-exclusive message has started and not ended.
  bool assembling() const noexcept { return m_state != state::idle; }

  //! System-exclusive messages dropped so far, too long or malformed.
  std::size_t dropped() const noexcept { return m_dropped; }

  static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
  enum class state : uint8_t
  {
    idle,
    assembling,
    overflow
  };

  void abandon() noexcept
  {
    if (m_state != state::idle)
      ++m_dropped;
    m_state = state::idle;
  }

  std::span<const uint8_t> complete() noexcept
  {
    m_state = state::idle;
    return {m_buffer, m_size};
  }

  void append(const libremidi::ump& packet, uint8_t count) noexcept
  {
    if (m_state == state::overflow)
      return;
    // F7 still has to fit after the bytes of the message.
    if (m_size + count + 1 > Capacity)
    {
      m_state = state::overflow;
      return;
    }
    for (uint8_t i = 0; i < count; i++)
    {
      const int byte = 2 + i;
      const uint32_t word = packet.data[byte / 4];
      m_buffer[m_size++] = (word >> (8 * (3 - byte % 4))) & 0x7F;
    }
  }

  uint8_t m_buffer[Capacity];
  uint8_t m_short[4];
  std::size_t m_size{};
  std::size_t m_dropped{};
  state m_state{state::idle};
};

/**
 * Converts UMP to MIDI 1 as midi2_to_midi1 does, for a stream given a few
 * packets at a time: the SysEx7 packets of a system-exclusive message give
 * one F0 ... F7 message once its end packet is in, whether they came in one
 * call or one per call.
 *
 * convert() calls @p on_midi once per system-exclusive message, and once for
 * each run of other packets, converted together as midi2_to_midi1 converts
 * them.
 *
 * Nothing allocates. A system-exclusive message longer than @p SysexCapacity
 * bytes, F0 and F7 included, or whose packets do not follow start,
 * continue..., end, is dropped whole: convert() returns
 * std::errc::message_size for the call that would have completed it, and
 * dropped() counts it.
 */
template <std::size_t SysexCapacity = 4096>
struct midi2_to_midi1_assembler
{
  stdx::error convert(const uint32_t* words, std::size_t count, int64_t timestamp, auto on_midi)
  {
    stdx::error ret{};
    const auto keep_first = [&ret](stdx::error e) {
      if (e != stdx::error{} && ret == stdx::error{})
        ret = e;
    };
    const auto convert_run = [&](std::size_t from, std::size_t to) {
      if (from < to)
        keep_first(converter.convert(words + from, to - from, timestamp, on_midi));
    };

    std::size_t run = 0;
    std::size_t at = 0;
    while (at < count)
    {
      const auto n = std::size_t(ump_packet_words(words[at]));
      if (at + n > count)
        break;

      if (midi2::message_type(words[at] >> 28) == midi2::message_type::SYSEX7)
      {
        convert_run(run, at);
        run = at + n;

        libremidi::ump packet;
        std::copy_n(words + at, n, packet.data);
        const auto dropped = sysex.dropped();
        if (auto midi = sysex.feed(packet); !midi.empty())
          keep_first(on_midi(midi.data(), midi.size(), timestamp));
        else if (sysex.dropped() != dropped)
          keep_first(std::errc::message_size);
      }
      at += n;
    }
    convert_run(run, count);
    return ret;
  }

  //! Forgets a system-exclusive message in progress.
  void reset() noexcept { sysex.reset(); }

  //! System-exclusive messages dropped so far, too long or malformed.
  std::size_t dropped() const noexcept { return sysex.dropped(); }

  static constexpr std::size_t sysex_capacity() noexcept { return SysexCapacity; }

  midi2_to_midi1 converter;
  ump_to_midi1_stream<SysexCapacity> sysex;
};
}
