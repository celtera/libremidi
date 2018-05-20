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

#include <rtmidi17/reader.hpp>
#include <rtmidi17/message.hpp>
#include <algorithm>

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

namespace rtmidi
{
RTMIDI17_INLINE
track_event parseEvent(int tick, int track, uint8_t const *& dataStart, message_type lastEventTypeByte)
{
  message_type type = (message_type) *dataStart++;

  track_event event{tick, track, message{}};

  if (((uint8_t) type & 0xF) == 0xF)
  {
    // Meta event
    if ((uint8_t) type == 0xFF)
    {
      meta_event_type subtype = (meta_event_type) *dataStart++;
      int length = read_variable_length(dataStart);

      event.m.bytes = midi_bytes(std::max(length, 3), (std::size_t) 0);
      event.m.bytes[0] = (uint8_t) type;
      event.m.bytes[1] = (uint8_t) subtype;
      event.m.bytes[2] = length;

      switch(subtype)
      {
        case meta_event_type::SEQUENCE_NUMBER:
        {
          if (length != 2) throw std::invalid_argument("Expected length for SEQUENCE_NUMBER event is 2");
          read_bytes(event.m.bytes, dataStart, 2);
          return event;
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
          if (length != 0) throw std::invalid_argument("Expected length for END_OF_TRACK event is 0");
          return event;
        }
        case meta_event_type::TEMPO_CHANGE:
        {
          if (length != 3) throw std::invalid_argument("Expected length for TEMPO_CHANGE event is 3");
          //event.m.bytes[3] = read_uint24_be(dataStart); // @dimitri TOFIX
          read_bytes(event.m.bytes, dataStart, length);
          return event;
        }
        case meta_event_type::SMPTE_OFFSET:
        {
          if (length != 5) throw std::invalid_argument("Expected length for SMPTE_OFFSET event is 5");
          read_bytes(event.m.bytes, dataStart, length);
          return event;
        }
        case meta_event_type::TIME_SIGNATURE:
        {
          if (length != 4) throw std::invalid_argument("Expected length for TIME_SIGNATURE event is 4");
          read_bytes(event.m.bytes, dataStart, length);
          return event;
        }
        case meta_event_type::KEY_SIGNATURE:
        {
          if (length != 2) throw std::invalid_argument("Expected length for KEY_SIGNATURE event is 2");
          read_bytes(event.m.bytes, dataStart, length);
          return event;
        }
        case meta_event_type::PROPRIETARY:
        {
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
      int length = read_variable_length(dataStart);
      read_bytes(event.m.bytes, dataStart, length);
      return event;
    }

    else if (type == message_type::EOX)
    {
      int length = read_variable_length(dataStart);
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
    event.m.bytes = midi_bytes(3, 0);
    event.m.bytes[0] = (uint8_t) type;

    // Running status...
    if (((uint8_t) type & 0x80) == 0)
    {
      // Reuse lastEventTypeByte as the event type.
      // eventTypeByte is actually the first parameter
      event.m.bytes[0] = (uint8_t) type;
      type = lastEventTypeByte;
    }
    else
    {
      event.m.bytes[1] = uint8_t(*dataStart++);
      lastEventTypeByte = type;
    }

    // Just in case
    event.m.bytes[2] = 0xFF;

    switch (message_type((uint8_t) type & 0xF0))
    {
      case message_type::NOTE_OFF:
        event.m.bytes[2] = uint8_t(*dataStart++);
        return event;
      case message_type::NOTE_ON:
        event.m.bytes[2] = uint8_t(*dataStart++);
        return event;
      case message_type::POLY_PRESSURE:
        event.m.bytes[2] = uint8_t(*dataStart++);
        return event;
      case message_type::CONTROL_CHANGE:
        event.m.bytes[2] = uint8_t(*dataStart++);
        return event;
      case message_type::PROGRAM_CHANGE:
        return event;
      case message_type::AFTERTOUCH:
        return event;
      case message_type::PITCH_BEND:
        event.m.bytes[2] = uint8_t(*dataStart++);
        return event;
      default:
        throw std::runtime_error("Unrecognised MIDI event type");
    }
  }
}

RTMIDI17_INLINE
reader::reader(bool useAbsolute)
  : tracks(0)
  , ticksPerBeat(480)
  , startingTempo(120)
  , useAbsoluteTicks(useAbsolute)
{

}

RTMIDI17_INLINE
reader::~reader()
{

}

RTMIDI17_INLINE
void reader::parse_impl(const std::vector<uint8_t> & buffer)
{
  const uint8_t * dataPtr = buffer.data();

  int headerId = read_uint32_be(dataPtr);
  int headerLength = read_uint32_be(dataPtr);

  if (headerId != 'MThd' || headerLength != 6)
  {
    std::cerr << "Bad .mid file - couldn't parse header" << std::endl;
    return;
  }

  read_uint16_be(dataPtr); //@tofix format type -> save for later eventually

  int trackCount = read_uint16_be(dataPtr);
  int timeDivision = read_uint16_be(dataPtr);

  // CBB: deal with the SMPTE style time coding
  // timeDivision is described here http://www.sonicspot.com/guide/midifiles.html
  if (timeDivision & 0x8000)
  {
    std::cerr << "Found SMPTE time frames" << std::endl;
    //int fps = (timeDivision >> 16) & 0x7f;
    //int ticksPerFrame = timeDivision & 0xff;
    // given beats per second, timeDivision should be derivable.
    return;
  }

  startingTempo = 120.0f; // midi default
  ticksPerBeat = float(timeDivision); // ticks per beat (a beat is defined as a quarter note)

  for (int i = 0; i < trackCount; ++i)
  {
    midi_track track;

    headerId = read_uint32_be(dataPtr);
    headerLength = read_uint32_be(dataPtr);

    if (headerId != 'MTrk')
    {
      throw std::runtime_error("Bad .mid file - couldn't find track header");
    }

    uint8_t const * dataEnd = dataPtr + headerLength;

    message_type runningEvent = message_type::INVALID;

    int tickCount = 0;

    while (dataPtr < dataEnd)
    {
      auto tick = read_variable_length(dataPtr);

      if (useAbsoluteTicks)
      {
        tickCount += tick;
      }
      else
      {
        tickCount = tick;
      }

      track_event ev = parseEvent(tickCount, i, dataPtr, runningEvent);

      if (ev.m.is_meta_event() == false)
      {
        runningEvent = message_type(ev.m.bytes[0]);
      }

      track.push_back(ev);
    }

    tracks.push_back(track);
  }
}

// In ticks
RTMIDI17_INLINE
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

RTMIDI17_INLINE
void reader::parse(const std::vector<uint8_t> & buffer)
{
  tracks.clear();
  parse_impl(buffer);
}

}
