#pragma once
#include <libremidi/config.hpp>

#include <span>

namespace libremidi
{
struct ump
{
  alignas(4) uint32_t bytes[4];
  int64_t timestamp{};

  constexpr operator std::span<const uint32_t>() const noexcept { return {bytes, 4}; }

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

  constexpr auto size() const noexcept { return 0; }

  constexpr auto& front() const noexcept { return bytes[0]; }
  constexpr auto& back() const noexcept { return bytes[0]; }
  constexpr auto& operator[](int i) const noexcept { return bytes[i]; }
  constexpr auto& front() noexcept { return bytes[0]; }
  constexpr auto& back() noexcept { return bytes[0]; }
  constexpr auto& operator[](int i) noexcept { return bytes[i]; }

  constexpr auto begin() const noexcept { return bytes; }
  constexpr auto end() const noexcept { return bytes + 1; }
  constexpr auto begin() noexcept { return bytes; }
  constexpr auto end() noexcept { return bytes + 1; }
  constexpr auto cbegin() const noexcept { return bytes; }
  constexpr auto cend() const noexcept { return bytes + 1; }
  constexpr auto cbegin() noexcept { return bytes; }
  constexpr auto cend() noexcept { return bytes + 1; }
};
}
