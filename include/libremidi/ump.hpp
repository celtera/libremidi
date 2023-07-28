#pragma once
#include <libremidi/config.hpp>
#include <libremidi/cmidi2.hpp>

#include <span>

namespace libremidi
{
struct ump
{
  alignas(4) uint32_t bytes[4];
  int64_t timestamp{};

  constexpr operator std::span<const uint32_t>() const noexcept {
    return {bytes, size()}; }

  constexpr ump() noexcept = default;
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

  constexpr std::size_t size() const noexcept {
    auto res = cmidi2_ump_get_num_bytes(bytes[0]);
    if(res == 0xFF)
      return 0;
    return res / 4;
  }

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
