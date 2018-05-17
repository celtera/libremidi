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

#pragma once

#include <rtmidi17/rtmidi17.hpp>

namespace rtmidi
{
// Read a MIDI-style variable-length integer (big-endian value in groups of 7 bits,
// with top bit set to signify that another byte follows).
inline uint32_t read_variable_length(uint8_t const *& data)
{
  uint32_t result = 0;
  while (true)
  {
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

inline void read_bytes(midi_bytes & buffer, uint8_t const *& data, int num)
{
  buffer.reserve(buffer.size() + num);
  for (int i = 0; i < num; ++i)
    buffer.push_back(uint8_t(*data++));
}

inline uint16_t read_uint16_be(uint8_t const *& data)
{
  uint16_t result = int(*data++) << 8;
  result += int(*data++);
  return result;
}

inline uint32_t read_uint24_be(uint8_t const *& data)
{
  uint32_t result = int(*data++) << 16;
  result += int(*data++) << 8;
  result += int(*data++);
  return result;
}

inline uint32_t read_uint32_be(uint8_t const *& data)
{
  uint32_t result = int(*data++) << 24;
  result += int(*data++) << 16;
  result += int(*data++) << 8;
  result += int(*data++);
  return result;
}

class reader
{
public:
  reader(bool useAbsolute = false);
  ~reader();

  void parse(const std::vector<uint8_t> & buffer);
  double get_end_time();

  float ticksPerBeat{}; // precision (number of ticks distinguishable per second)
  float startingTempo{};

  std::vector<midi_track> tracks;

private:
  void parse_impl(const std::vector<uint8_t> & buffer);
  bool useAbsoluteTicks{};
};

} // mm
