#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>

#include "apsis_drift/terrain_tiles.hpp"

namespace apsis_drift {

// Surface-signal generation is generated-world compatibility data. Changing
// stream ordinals, placement rules, attribute ranges, or catalog size requires
// a new version.
inline constexpr std::uint32_t kSurfaceSignalGeneratorVersion{1};
inline constexpr std::size_t kSurfaceSignalCount{6};
inline constexpr std::uint8_t kSurfaceSignalPlacementLod{12};
inline constexpr std::uint16_t kSurfaceSignalPlacementAttempts{64};
inline constexpr std::int32_t kSurfaceSignalMaximumReliefMetres{750};
inline constexpr std::int32_t kSurfaceSignalApproachClearanceMetres{1'000};
inline constexpr std::uint16_t kSurfaceSignalMinimumStrengthBasisPoints{4'000};
inline constexpr std::uint16_t kSurfaceSignalMaximumStrengthBasisPoints{10'000};
inline constexpr std::uint16_t kSurfaceSignalMinimumRewardPoints{1};
inline constexpr std::uint16_t kSurfaceSignalMaximumRewardPoints{3};

// These ordinals name independent children of one signal identity. Never
// renumber an existing stream; add a new value instead.
enum class SurfaceSignalStream : std::uint64_t {
  placement = 1,
  attributes = 2,
};

struct SurfaceSignalId {
  std::uint64_t value{};

  friend auto operator==(const SurfaceSignalId&, const SurfaceSignalId&)
      -> bool = default;
};

enum class SurfaceSignalKind : std::uint8_t {
  survey,
  recovery,
  anomaly,
};

struct SurfaceSignalReward {
  std::uint16_t discovery_points{};

  friend auto operator==(const SurfaceSignalReward&,
                         const SurfaceSignalReward&) -> bool = default;
};

struct SurfaceSignal {
  SurfaceSignalId id;
  std::uint32_t ordinal{};
  SurfaceSignalKind kind{};
  TerrainTileAddress anchor;
  std::int32_t surface_elevation_metres{};
  std::int32_t approach_altitude_metres{};
  std::uint16_t strength_basis_points{};
  SurfaceSignalReward reward;
  std::uint16_t placement_attempt{};

  friend auto operator==(const SurfaceSignal&, const SurfaceSignal&)
      -> bool = default;
};

struct SurfaceSignalCatalog {
  PlanetId planet;
  std::array<SurfaceSignal, kSurfaceSignalCount> signals;

  friend auto operator==(const SurfaceSignalCatalog&,
                         const SurfaceSignalCatalog&) -> bool = default;
};

enum class SurfaceSignalError : std::uint8_t {
  invalid_planet,
  terrain_failure,
  placement_exhausted,
};

[[nodiscard]] auto derive_surface_signal_seed(
    Seed signal_seed, SurfaceSignalStream stream) noexcept -> Seed;

[[nodiscard]] auto generate_surface_signals(const PlanetDescriptor& planet,
                                            TerrainTileCache& cache)
    -> std::expected<SurfaceSignalCatalog, SurfaceSignalError>;

[[nodiscard]] auto surface_signal_id_string(SurfaceSignalId id) -> std::string;

}  // namespace apsis_drift
