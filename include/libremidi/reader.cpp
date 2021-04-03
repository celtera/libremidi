/*
Copyright (c) 2015, Dimitri Diakopoulos All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#if !defined(LIBREMIDI_HEADER_ONLY)
#  include <libremidi/reader.hpp>
#endif
#include <algorithm>
#include <iostream>
#include <libremidi/message.hpp>

// File Parsing Validation Todo:
// ==============================
// [] Bad file name
// [] Bad header
// [] Unknown header type
// [] Bad header size
// [] Bad type
// [] Bad tmecode
// [] Header too short
// [] Track too short
// [] Event too short
// ==============================

namespace libremidi
{
// Used when we know that we have enough space
namespace util_unchecked
{
inline void read_bytes(midi_bytes& buffer, uint8_t const*& data, int num)
{
  buffer.reserve(buffer.size() + num);
  for (int i = 0; i < num; ++i)
    buffer.push_back(uint8_t(*data++));
}

inline uint16_t read_uint16_be(uint8_t const*& data)
{
  uint16_t result = int(*data++) << 8;
  result += int(*data++);
  return result;
}

inline uint32_t read_uint24_be(uint8_t const*& data)
{
  uint32_t result = int(*data++) << 16;
  result += int(*data++) << 8;
  result += int(*data++);
  return result;
}

inline uint32_t read_uint32_be(uint8_t const*& data)
{
  uint32_t result = int(*data++) << 24;
  result += int(*data++) << 16;
  result += int(*data++) << 8;
  result += int(*data++);
  return result;
}
}

namespace util_checked
{
// Read a MIDI-style variable-length integer (big-endian value in groups of 7 bits,
// with top bit set to signify that another byte follows).

inline void ensure_size(const uint8_t* begin, const uint8_t* end, int64_t needed)
{
  if(int64_t available = (end - begin); available < needed)
    throw std::runtime_error("MIDI reader: not enough data to process");
}

inline uint32_t read_variable_length(uint8_t const*& data, uint8_t const* end)
{
  uint32_t result = 0;
  while (true)
  {
    ensure_size(data, end, 1);
    uint8_t b = *data++;
    if (b & 0x80)
    {
      result += (b & 0x7F);
      result <<= 7;
    }
    else
    {
      return result + b; // b is the last byte
    }
  }
}

inline void read_bytes(midi_bytes& buffer, uint8_t const*& data, int num, uint8_t const* end)
{
  ensure_size(data, end, num);
  util_unchecked::read_bytes(buffer, data, num);
}

inline uint16_t read_uint16_be(uint8_t const*& data, uint8_t const* end)
{
  ensure_size(data, end, 2);
  return util_unchecked::read_uint16_be(data);
}

inline uint32_t read_uint24_be(uint8_t const*& data, uint8_t const* end)
{
  ensure_size(data, end, 3);
  return util_unchecked::read_uint24_be(data);
}

inline uint32_t read_uint32_be(uint8_t const*& data, uint8_t const* end)
{
  ensure_size(data, end, 4);
  return util_unchecked::read_uint32_be(data);
}

}

LIBREMIDI_INLINE
track_event
parseEvent(int tick, int track, const uint8_t*& dataStart, const uint8_t* dataEnd, message_type lastEventTypeByte)
{
  using namespace libremidi::util_unchecked;
  message_type type = (message_type)*dataStart++;

  track_event event{tick, track, message{}};

  if (((uint8_t)type & 0xF0) == 0xF0)
  {
    // Meta event
    if ((uint8_t)type == 0xFF)
    {
      meta_event_type subtype = (meta_event_type)*dataStart++;
      uint32_t length = util_checked::read_variable_length(dataStart, dataEnd);

      if (length > 0x7F)
        throw(std::invalid_argument("Implementation does not allow meta event length over 127 bytes"));

      event.m.bytes.resize(3);
      event.m.bytes[0] = (uint8_t)type;
      event.m.bytes[1] = (uint8_t)subtype;
      event.m.bytes[2] = (uint8_t)length;

      switch (subtype)
      {
        case meta_event_type::SEQUENCE_NUMBER:
        {
          switch(length)
          {
            case 0:
              return event;
            case 2:
              read_bytes(event.m.bytes, dataStart, 2);
              return event;
            default:
              throw std::invalid_argument("Expected length for SEQUENCE_NUMBER event is 0 or 2");
          }
        }
        case meta_event_type::TEXT:
        case meta_event_type::COPYRIGHT:
        case meta_event_type::TRACK_NAME:
        case meta_event_type::INSTRUMENT:
        case meta_event_type::LYRIC:
        case meta_event_type::MARKER:
        case meta_event_type::CUE:
        case meta_event_type::PATCH_NAME:
        case meta_event_type::DEVICE_NAME:
        {
          read_bytes(event.m.bytes, dataStart, length);
          return event;
        }

        case meta_event_type::END_OF_TRACK:
        {
          if (length != 0)
            throw std::invalid_argument("Expected length for END_OF_TRACK event is 0");
          return event;
        }
        case meta_event_type::TEMPO_CHANGE:
        {
          if (length != 3)
            throw std::invalid_argument("Expected length for TEMPO_CHANGE event is 3");
          // event.m.bytes[3] = read_uint24_be(dataStart); // @dimitri TOFIX
          read_bytes(event.m.bytes, dataStart, length);
          return event;
        }
        case meta_event_type::SMPTE_OFFSET:
        {
          if (length != 5)
            throw std::invalid_argument("Expected length for SMPTE_OFFSET event is 5");
          read_bytes(event.m.bytes, dataStart, length);
          return event;
        }
        case meta_event_type::TIME_SIGNATURE:
        {
          if (length != 4)
            throw std::invalid_argument("Expected length for TIME_SIGNATURE event is 4");
          read_bytes(event.m.bytes, dataStart, length);
          return event;
        }
        case meta_event_type::KEY_SIGNATURE:
        {
          if (length != 2)
            throw std::invalid_argument("Expected length for KEY_SIGNATURE event is 2");
          read_bytes(event.m.bytes, dataStart, length);
          return event;
        }
        case meta_event_type::PROPRIETARY:
        {
          read_bytes(event.m.bytes, dataStart, length);
          return event;
        }
        case meta_event_type::CHANNEL_PREFIX:
        {
          if (length != 1)
            throw std::invalid_argument("Expected length for CHANNEL_PREFIX event is 1");
          read_bytes(event.m.bytes, dataStart, length);
          return event;
        }
        case meta_event_type::MIDI_PORT:
        {
          if (length != 1)
            throw std::invalid_argument("Expected length for MIDI_PORT event is 1");
          read_bytes(event.m.bytes, dataStart, length);
          return event;
        }
        case meta_event_type::UNKNOWN:
        default:
        {
          // Unknown events?
          read_bytes(event.m.bytes, dataStart, length);
          return event;
        }
      }
    }

    else if (type == message_type::SYSTEM_EXCLUSIVE)
    {
      int length = util_checked::read_variable_length(dataStart, dataEnd);
      event.m.bytes = { (uint8_t)type };
      read_bytes(event.m.bytes, dataStart, length);
      return event;
    }

    else if (type == message_type::EOX)
    {
      int length = util_checked::read_variable_length(dataStart, dataEnd);
      read_bytes(event.m.bytes, dataStart, length);
      return event;
    }
    else
    {
      throw std::runtime_error("Unrecognised MIDI event type byte");
    }
  }

  // Channel events
  else
  {
    event.m.bytes.clear();

    // Running status...
    if (((uint8_t)type & 0x80) == 0)
    {
      // Reuse lastEventTypeByte as the event type.
      // eventTypeByte is actually the first parameter
      event.m.bytes.push_back((uint8_t)lastEventTypeByte);
      event.m.bytes.push_back((uint8_t)type);
      type = lastEventTypeByte;
    }
    else
    {
      event.m.bytes.push_back((uint8_t)type);
      event.m.bytes.push_back((uint8_t)*dataStart++);
      lastEventTypeByte = type;
    }

    switch (message_type((uint8_t)type & 0xF0))
    {
      case message_type::NOTE_OFF:
        event.m.bytes.push_back(*dataStart++);
        return event;
      case message_type::NOTE_ON:
        event.m.bytes.push_back(*dataStart++);
        return event;
      case message_type::POLY_PRESSURE:
        event.m.bytes.push_back(*dataStart++);
        return event;
      case message_type::CONTROL_CHANGE:
        event.m.bytes.push_back(*dataStart++);
        return event;
      case message_type::PROGRAM_CHANGE:
        return event;
      case message_type::AFTERTOUCH:
        return event;
      case message_type::PITCH_BEND:
        event.m.bytes.push_back(*dataStart++);
        return event;

      case message_type::TIME_CODE:
        throw std::runtime_error("Unsupported MIDI event type TIME_CODE");
      case message_type::SONG_POS_POINTER:
        throw std::runtime_error("Unsupported MIDI event type SONG_POS_POINTER");
      case message_type::SONG_SELECT:
        throw std::runtime_error("Unsupported MIDI event type SONG_SELECT");
      case message_type::RESERVED1:
        throw std::runtime_error("Unsupported MIDI event type RESERVED1");
      case message_type::RESERVED2:
        throw std::runtime_error("Unsupported MIDI event type RESERVED2");
      case message_type::TUNE_REQUEST:
        throw std::runtime_error("Unsupported MIDI event type TUNE_REQUEST");
      case message_type::EOX:
        throw std::runtime_error("Unsupported MIDI event type EOX");
        // System Realtime Messages :
      case message_type::TIME_CLOCK:
        throw std::runtime_error("Unsupported MIDI event type TIME_CLOCK");
      case message_type::RESERVED3:
        throw std::runtime_error("Unsupported MIDI event type RESERVED3");
      case message_type::START:
        throw std::runtime_error("Unsupported MIDI event type START");
      case message_type::CONTINUE:
        throw std::runtime_error("Unsupported MIDI event type CONTINUE");
      case message_type::STOP:
        throw std::runtime_error("Unsupported MIDI event type STOP");
      case message_type::RESERVED4:
        throw std::runtime_error("Unsupported MIDI event type RESERVED4");
      case message_type::ACTIVE_SENSING:
        throw std::runtime_error("Unsupported MIDI event type ACTIVE_SENSING");
      case message_type::SYSTEM_RESET:
        throw std::runtime_error("Unsupported MIDI event type SYSTEM_RESET");
      case message_type::INVALID:
        throw std::runtime_error("Unsupported MIDI event type INVALID");
      case message_type::SYSTEM_EXCLUSIVE:
        throw std::runtime_error("Unsupported MIDI event type SYSTEM_EXCLUSIVE");
      default:
        throw std::runtime_error("Unsupported MIDI event type");
    }
  }
}

LIBREMIDI_INLINE
reader::reader(bool useAbsolute)
    : ticksPerBeat(480), startingTempo(120), useAbsoluteTicks(useAbsolute)
{
}

LIBREMIDI_INLINE
reader::~reader()
{
}

LIBREMIDI_INLINE
void reader::parse(const uint8_t* dataPtr, std::size_t size)
{
  using namespace libremidi;

  tracks.clear();

  if(size == 0)
  {
    std::cerr << "libremidi::reader: empty buffer passed to parse." << std::endl;
    return;
  }

  const uint8_t* const dataEnd = dataPtr + size;

  int headerId = util_checked::read_uint32_be(dataPtr, dataEnd);
  int headerLength = util_checked::read_uint32_be(dataPtr, dataEnd);

  if (headerId != 'MThd' || headerLength != 6)
  {
    std::cerr << "libremidi::reader: couldn't parse header" << std::endl;
    return;
  }

  util_checked::read_uint16_be(dataPtr, dataEnd); //@tofix format type -> save for later eventually

  int trackCount = util_checked::read_uint16_be(dataPtr, dataEnd);
  int timeDivision = util_checked::read_uint16_be(dataPtr, dataEnd);

  // CBB: deal with the SMPTE style time coding
  // timeDivision is described here http://www.sonicspot.com/guide/midifiles.html
  if (timeDivision & 0x8000)
  {
    std::cerr << "libremidi::reader: found SMPTE time frames (unsupported)" << std::endl;
    // int fps = (timeDivision >> 16) & 0x7f;
    // int ticksPerFrame = timeDivision & 0xff;
    // given beats per second, timeDivision should be derivable.
    return;
  }

  startingTempo = 120.0f;             // midi default
  ticksPerBeat = float(timeDivision); // ticks per beat (a beat is defined as a quarter note)

  for (int i = 0; i < trackCount; ++i)
  {
    midi_track track;

    headerId = util_checked::read_uint32_be(dataPtr, dataEnd);
    headerLength = util_checked::read_uint32_be(dataPtr, dataEnd);

    if (headerId != 'MTrk')
    {
      std::cerr << "libremidi::reader: couldn't find track header" << std::endl;
      return;
    }

    int64_t available = dataEnd - dataPtr;
    if(available < headerLength)
    {
      std::cerr << "libremidi::reader: not enough data available" << std::endl;
      return;
    }

    track.reserve(headerLength / 3);

    const uint8_t* const trackEnd = dataPtr + headerLength;

    message_type runningEvent = message_type::INVALID;

    int tickCount = 0;

    while (dataPtr < trackEnd)
    {
      auto tick = util_checked::read_variable_length(dataPtr, trackEnd);

      if (useAbsoluteTicks)
      {
        tickCount += tick;
      }
      else
      {
        tickCount = tick;
      }

      try
      {
        track_event ev = parseEvent(tickCount, i, dataPtr, trackEnd, runningEvent);

        if(!ev.m.bytes.empty())
        {
          if (!ev.m.is_meta_event())
          {
            runningEvent = message_type(ev.m.bytes[0]);
          }
        }
        else
        {
          std::cerr << "libremidi::reader: could not read event" << std::endl;
          return;
        }

        track.push_back(std::move(ev));
      }
      catch (const std::exception& e)
      {
        std::cerr << "libremidi::reader: " << e.what() << std::endl;
      }
    }

    tracks.push_back(std::move(track));
  }
}

// In ticks
LIBREMIDI_INLINE
double reader::get_end_time()
{
  double totalLength = 0;
  for (const auto& t : tracks)
  {
    double localLength = 0;
    for (const auto& e : t)
      localLength += e.tick;

    if (localLength > totalLength)
      totalLength = localLength;
  }
  return totalLength;
}

LIBREMIDI_INLINE
void reader::parse(const std::vector<uint8_t>& buffer)
{
  parse(buffer.data(), buffer.size());
}

#if defined(LIBREMIDI_HAS_SPAN)
LIBREMIDI_INLINE
void reader::parse(std::span<uint8_t> buffer)
{
  parse(buffer.data(), buffer.size());
}
#endif
}
