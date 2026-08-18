#pragma once

#include <cstdint>

namespace apsis_drift {

// Seed derivation is part of generated-world compatibility. Changing the
// algorithm or its encoded inputs requires a new version.
inline constexpr std::uint32_t kSeedDerivationVersion{1};

struct Seed {
  std::uint64_t value{};

  friend auto operator==(const Seed&, const Seed&) -> bool = default;
};

// These explicit identifiers are serialized inputs to seed derivation. Never
// renumber an existing domain; add a new value instead.
enum class SeedDomain : std::uint64_t {
  universe = 1,
  system = 2,
  planet = 3,
  terrain = 4,
  weather = 5,
  settlement = 6,
  encounter = 7,
  star = 8,
  orbit = 9,
  mission = 10,
  jump_alignment = 11,
};

// Derives one child seed without consuming or exposing mutable random state.
// The ordinal identifies siblings within the named domain.
[[nodiscard]] auto derive_seed(Seed parent, SeedDomain domain,
                               std::uint64_t ordinal = 0) noexcept -> Seed;

}  // namespace apsis_drift
