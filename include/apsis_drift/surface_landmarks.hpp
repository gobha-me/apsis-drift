#pragma once

#include "apsis_drift/body_ephemeris.hpp"
#include "apsis_drift/terrain_tiles.hpp"

#include <array>
#include <compare>
#include <optional>
#include <vector>

namespace apsis_drift {
inline constexpr std::uint32_t kSurfaceLandmarkVersion{1};
inline constexpr std::size_t kMaximumNearbySurfaceLandmarks{8};
inline constexpr std::size_t kSurfaceLandmarkMemoCells{64};
inline constexpr double kSurfaceLandmarkCellMetres{512};
inline constexpr double kSurfaceLandmarkNearbyMetres{384};

struct SurfaceLandmarkCell {
  std::int32_t x{}, y{}, z{};
  friend auto operator<=>(const SurfaceLandmarkCell&,
                          const SurfaceLandmarkCell&) = default;
};
struct SurfaceLandmarkId {
  std::uint64_t value{};
  friend auto operator<=>(const SurfaceLandmarkId&,
                          const SurfaceLandmarkId&) = default;
};
enum class SurfaceLandmarkKind : std::uint8_t {
  pale_exposure,
  ochre_exposure,
  dark_exposure
};
struct SurfaceLandmarkIdentity {
  SurfaceLandmarkId id;
  SurfaceLandmarkCell cell;
  SurfaceLandmarkKind kind{};
  PlanetFixedPositionMetres reference_position_metres;
  double radius_metres{};
  Rgb8 tint;
  friend auto operator==(const SurfaceLandmarkIdentity&,
                         const SurfaceLandmarkIdentity&) -> bool = default;
};
struct SurfaceLandmarkSite {
  SurfaceLandmarkIdentity identity;
  PlanetFixedPositionMetres position_metres;
  friend auto operator==(const SurfaceLandmarkSite&, const SurfaceLandmarkSite&)
      -> bool = default;
};
enum class SurfaceLandmarkError : std::uint8_t {
  unsupported_version,
  invalid_owner,
  invalid_body,
  invalid_policy,
  invalid_cell,
  invalid_query,
  terrain_failure,
  unsafe_arithmetic
};

// Lightweight recipe identity only. The caller authenticates this descriptor
// through its generated owner. No terrain query or support claim is made.
// Cells use floor(reference-sphere coordinate / 512m), in [-32768,32767].
// Ordinal packs (x+32768), (y+32768), (z+32768) into low/middle/high16 bits.
[[nodiscard]] auto identify_surface_landmark(
    const PlanetDescriptor&, SurfaceLandmarkCell,
    std::uint32_t version = kSurfaceLandmarkVersion)
    -> std::expected<std::optional<SurfaceLandmarkIdentity>,
                     SurfaceLandmarkError>;

// Owns one authenticated physical body/policy. Sites lie on the unchanged
// selected contact TOP. Dry, walkable centre qualification is not clearance
// for the whole material footprint or a route to it. No extra solid is added.
class SurfaceLandmarkField {
 public:
  [[nodiscard]] static auto create(
      const PhysicalLocalSystem&, BodyId, std::uint32_t terrain_policy,
      std::uint32_t version = kSurfaceLandmarkVersion)
      -> std::expected<SurfaceLandmarkField, SurfaceLandmarkError>;
  [[nodiscard]] auto site(SurfaceLandmarkCell)
      -> std::expected<std::optional<SurfaceLandmarkSite>,
                       SurfaceLandmarkError>;
  // At most27 reference-cell candidates; at most8 sites within384m of the
  // actual query, ordered by distance then identity. Distant space
  // queries return an empty vector without terrain work.
  [[nodiscard]] auto nearby(PlanetFixedPositionMetres actual_query)
      -> std::expected<std::vector<SurfaceLandmarkSite>, SurfaceLandmarkError>;
  [[nodiscard]] auto cache_size() const noexcept -> std::size_t {
    return memo_count_;
  }
  [[nodiscard]] auto terrain_cache_size() const noexcept -> std::size_t {
    return cache_.size();
  }

 private:
  struct Memo {
    bool used{};
    SurfaceLandmarkCell cell;
    std::optional<SurfaceLandmarkSite> result;
  };
  SurfaceLandmarkField(PhysicalLocalSystem owner, std::size_t planet_index,
                       std::uint32_t terrain_policy, std::uint32_t version,
                       TerrainTileCache cache)
      : owner_(std::move(owner)), planet_index_(planet_index),
        terrain_policy_(terrain_policy), version_(version),
        cache_(std::move(cache)) {}
  [[nodiscard]] auto build_site(SurfaceLandmarkCell)
      -> std::expected<std::optional<SurfaceLandmarkSite>,
                       SurfaceLandmarkError>;
  PhysicalLocalSystem owner_;
  std::size_t planet_index_{};
  std::uint32_t terrain_policy_{}, version_{};
  TerrainTileCache cache_;
  std::array<Memo, kSurfaceLandmarkMemoCells> memo_{};
  std::size_t memo_count_{}, next_memo_{};
};
} // namespace apsis_drift
