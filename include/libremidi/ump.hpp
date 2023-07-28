#pragma once
#include <libremidi/config.hpp>
#include <libremidi/cmidi2.hpp>

#include <span>

namespace libremidi
{
struct ump
{
  alignas(4) uint32_t bytes[4] = {};
  int64_t timestamp{};

  constexpr operator std::span<const uint32_t>() const noexcept {
    return {bytes, size()}; }

  constexpr ump() noexcept = default;
  constexpr ~ump() = default;
  constexpr ump(uint32_t b0) noexcept
      : bytes{b0}
  {
  }
  constexpr ump(uint32_t b0, uint32_t b1) noexcept
      : bytes{b0, b1}
  {
  }
  constexpr ump(uint32_t b0, uint32_t b1, uint32_t b2) noexcept
      : bytes{b0, b1, b2}
  {
  }
  constexpr ump(uint32_t b0, uint32_t b1, uint32_t b2, uint32_t b3) noexcept
      : bytes{b0, b1, b2, b3}
  {
  }

  constexpr std::size_t size() const noexcept
  {
    switch (((bytes[0] & 0xF0000000) >> 28) & 0xF)
    {
      case CMIDI2_MESSAGE_TYPE_UTILITY:
      case CMIDI2_MESSAGE_TYPE_SYSTEM:
      case CMIDI2_MESSAGE_TYPE_MIDI_1_CHANNEL:
        return 1;
      case CMIDI2_MESSAGE_TYPE_MIDI_2_CHANNEL:
      case CMIDI2_MESSAGE_TYPE_SYSEX7:
        return 2;
      case CMIDI2_MESSAGE_TYPE_SYSEX8_MDS:
        return 4;
      default:
        return 0;
    }
  }

  constexpr void clear() noexcept { bytes[0] = 0; }

  constexpr auto& operator[](int i) const noexcept { return bytes[i]; }
  constexpr auto& operator[](int i) noexcept { return bytes[i]; }

  constexpr auto begin() const noexcept { return bytes; }
  constexpr auto end() const noexcept { return bytes + size(); }
  constexpr auto begin() noexcept { return bytes; }
  constexpr auto end() noexcept { return bytes + size(); }
  constexpr auto cbegin() const noexcept { return bytes; }
  constexpr auto cend() const noexcept { return bytes + size(); }
  constexpr auto cbegin() noexcept { return bytes; }
  constexpr auto cend() noexcept { return bytes + size(); }
};
}
