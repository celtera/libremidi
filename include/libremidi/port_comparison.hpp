#pragma once
#include <libremidi/observer_configuration.hpp>

#include <span>
#include <variant>
#include <limits>
#include <tuple>

namespace libremidi
{
// Simple compare-the-name equality. Useful for saving / reloading
struct port_name_equal
{
  bool operator()(const port_information& lhs, const port_information& rhs)
  {
    return lhs.api == rhs.api && lhs.port_name == rhs.port_name;
  }
};
struct port_name_less
{
  bool operator()(const port_information& lhs, const port_information& rhs)
  {
    return std::tie(lhs.api, lhs.port_name) < std::tie(rhs.api, rhs.port_name);
  }
};

// Compare an existing port with others.
// Note that on multiple APIs, this comparison method is only valid as long
// as no device gets connected / disconnected, as the port identifier / handle
// is sadly just the index in the list of devices returned by the OS, which will
// change as soon as a device changes.
// Thus, it should only ever be used to compare devices between a group obtained
// from a single call to get_input_ports / get_output_ports or in situations where we can
// be sure that there is no hot-plugging.
struct port_identity_equal
{
  bool operator()(const port_information& lhs, const port_information& rhs)
  {
    return lhs.api == rhs.api && lhs.port == rhs.port;
  }
};
struct port_identity_less
{
  bool operator()(const port_information& lhs, const port_information& rhs)
  {
    return std::tie(lhs.api, lhs.port) < std::tie(rhs.api, rhs.port);
  }
};

struct port_heuristic_matcher
{
  // Configuration for weights
  static constexpr int W_HARDWARE_ID = 1000; // Unique HW IDs (Container, Device)
  static constexpr int W_SERIAL      = 800;  // Serial Number
  static constexpr int W_NAME_EXACT  = 400;  // Display/Port/Device Names
  static constexpr int W_METADATA    = 100;  // Manufacturer/Product
  static constexpr int W_HANDLE      = 50;   // Port Index/Handle

  // Penalties for mismatches when data is present in both but differs
  static constexpr int P_HARDWARE_MISMATCH = -2000;
  static constexpr int P_SERIAL_MISMATCH   = -1000;
  static constexpr int P_NAME_MISMATCH     = -100;
  static constexpr int P_PRODUCT_MISMATCH     = -10;

  struct match_score
  {
    int score = 0;
    bool api_mismatch = false;

    bool is_match() const { return !api_mismatch && score > 0; }
    constexpr auto operator<=>(const match_score& other) const noexcept = default;
  };

  static inline constexpr bool chars_equal_ignore_case(char lhs, char rhs) {
    if(lhs >= 'A' && lhs <= 'Z')
      lhs -= 'A' - 'a';
    if(rhs >= 'A' && rhs <= 'Z')
      rhs -= 'A' - 'a';

    return lhs == rhs;
  }

  static inline double fuzzy_match_name(std::string_view s1, std::string_view s2)
  {
    const size_t len1 = s1.size();
    const size_t len2 = s2.size();
    if (len1 == 0 && len2 == 0)
      return 1.0;
    if (len1 == 0 || len2 == 0)
      return 0;
    if (len1 > 1024 || len2 > 1024)
      return 0;
    if (len1 > len2)
      return fuzzy_match_name(s2, s1);

    auto col = (size_t*)alloca(sizeof(size_t) * (len1 + 1));
    std::fill_n(col, len1 + 1, 0);

    // Initialize first column (0, 1, 2... len1)
    for (size_t i = 0; i <= len1; ++i) col[i] = i;

    // Compute Levenshtein distance
    for (size_t j = 1; j <= len2; ++j) {
      size_t prev_diag = col[0];
      col[0] = j;

      for (size_t i = 1; i <= len1; ++i) {
        size_t prev_col = col[i];
        size_t cost = chars_equal_ignore_case(s1[i - 1], s2[j - 1]) ? 0 : 1;

        col[i] = std::min({
            col[i] + 1,      // Deletion
            col[i - 1] + 1,  // Insertion
            prev_diag + cost // Substitution
        });

        prev_diag = prev_col;
      }
    }

    const size_t distance = col[len1];
    const size_t max_len = std::max(len1, len2);

    if (max_len == 0)
      return 1.0;

    return 1.0 - (static_cast<double>(distance) / static_cast<double>(max_len));
  }

  match_score calculate(const port_information& target, const port_information& candidate) const
  {
    match_score result;

    // 1. API Mismatch
    // It is impossible for a port to be the same if the API is different.
    if (target.api != libremidi::API::UNSPECIFIED)
    {
      if (target.api != candidate.api)
      {
        result.api_mismatch = true;
        result.score = std::numeric_limits<int>::min();
        return result;
      }
    }

    // 2. Hardware Identifiers & Serial Number
    // High value, but unreliable presence.
    switch(target.api)
    {
      case libremidi::API::COREMIDI:
      case libremidi::API::COREMIDI_UMP:
      case libremidi::API::WINDOWS_MM:
      {
        score_variant(result.score, target.device, candidate.device, W_HARDWARE_ID, P_HARDWARE_MISMATCH);
        break;
      }
      default:
        break;
    }

    score_string(result.score, target.manufacturer, candidate.manufacturer, W_METADATA, P_HARDWARE_MISMATCH);
    score_string(result.score, target.product, candidate.product, W_METADATA, P_HARDWARE_MISMATCH);
    score_string(result.score, target.serial, candidate.serial, W_SERIAL, P_SERIAL_MISMATCH);

    // 3. Names
    // We accumulate score for every name that matches.
    score_string(result.score, target.display_name, candidate.display_name, W_NAME_EXACT, P_NAME_MISMATCH);
    score_string(result.score, target.port_name, candidate.port_name, W_NAME_EXACT, P_NAME_MISMATCH);
    score_string(result.score, target.device_name, candidate.device_name, W_NAME_EXACT, P_NAME_MISMATCH);

    // 4. Port Handle (Index)
    // Only check if it's not the default -1.
    // We rely on this primarily as a tie-breaker if names/hardware IDs are identical
    // (e.g. two identical controllers plugged in).
    if (target.port != static_cast<port_handle>(-1))
    {
      if (target.port == candidate.port)
      {
        result.score += W_HANDLE;
      }
    }

    return result;
  }

private:
  void score_string(int& score, std::string_view target_s, std::string_view cand_s, int reward, int penalty) const
  {
    // If the target doesn't know this info, we can't judge. Skip.
    if (target_s.empty())
      return;

    const double res = fuzzy_match_name(cand_s, target_s);
    if (res >= 0.5)
    {
      score += res * reward;
    }
    else if (!cand_s.empty())
    {
      // If candidate value is empty, it's just missing info, not necessarily a mismatch.
      score += penalty;
    }
  }

  // Helper for std::variant fields (device / container identifiers)
  template <typename T>
  void score_variant(int& score, const T& target_v, const T& cand_v, int reward, int penalty) const
  {
    if (std::holds_alternative<std::monostate>(target_v))
      return;

    // For those we want an exact search
    if (target_v == cand_v)
    {
      score += reward;
    }
    else if (!std::holds_alternative<std::monostate>(cand_v))
    {
      // Candidate has a specific ID, and it differs from Target's specific ID.
      score += penalty;
    }
  }
};

struct input_port_search_result
{
  const input_port* port = nullptr;
  int score = 0;
  bool found = false;
};

inline input_port_search_result find_closest_port(
    const input_port& target,
    std::span<input_port> candidates)
{
  port_heuristic_matcher matcher{};

  const input_port* best_match = nullptr;
  port_heuristic_matcher::match_score best_score;
  best_score.score = -1;

  for (const auto& candidate : candidates)
  {
    port_heuristic_matcher::match_score current = matcher.calculate(target, candidate);

    if (current.is_match() && current > best_score)
    {
      best_score = current;
      best_match = &candidate;
    }
  }

  if (best_match)
    return { best_match, best_score.score, true };

  return { nullptr, 0, false };
}

struct output_port_search_result
{
  const output_port* port = nullptr;
  int score = 0;
  bool found = false;
};

inline output_port_search_result find_closest_port(
    const output_port& target,
    std::span<output_port> candidates)
{
  port_heuristic_matcher matcher{};

  const output_port* best_match = nullptr;
  port_heuristic_matcher::match_score best_score{};
  best_score.score = -1;

  for (const auto& candidate : candidates)
  {
    port_heuristic_matcher::match_score current = matcher.calculate(target, candidate);

    if (current.is_match() && current > best_score)
    {
      best_score = current;
      best_match = &candidate;
    }
  }

  if (best_match)
    return {best_match, best_score.score, true};

  return {nullptr, 0, false};
}
}
