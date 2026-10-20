#pragma once

#include "apsis_drift/moon_terrain.hpp"
#include "apsis_drift/surface_landmarks.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kMoonSurfaceLandmarkVersion{1};

struct MoonSurfaceLandmarkIdentity {
  SurfaceLandmarkId id;
  SurfaceLandmarkCell cell;
  SurfaceLandmarkKind kind{};
  MoonFixedPositionMetres reference_position_metres;
  double radius_metres{};
  Rgb8 tint;
  friend auto operator==(const MoonSurfaceLandmarkIdentity&,
                         const MoonSurfaceLandmarkIdentity&) -> bool = default;
};
struct MoonSurfaceLandmarkSite {
  MoonSurfaceLandmarkIdentity identity;
  MoonFixedPositionMetres position_metres;
  friend auto operator==(const MoonSurfaceLandmarkSite&,
                         const MoonSurfaceLandmarkSite&) -> bool = default;
};

// Authenticated origin Moon identity only: no terrain query, generated site
// position or walking/support certificate. Signed16 cells retain the existing
// 512m reference-sphere scheme but use an independent appended seed domain.
[[nodiscard]] auto identify_moon_surface_landmark(
    const PhysicalMoonDescriptor&, SurfaceLandmarkCell,
    std::uint32_t version = kMoonSurfaceLandmarkVersion)
    -> std::expected<std::optional<MoonSurfaceLandmarkIdentity>,
                     SurfaceLandmarkError>;

// Owns the actual source and unchanged terrain recipe. Only the centre's
// genuine top facet is qualified by the current35degree Moon walker policy;
// neither the whole tint footprint nor a route to it is certified here.
class MoonSurfaceLandmarkField {
 public:
  [[nodiscard]] static auto create(
      const PhysicalMoonDescriptor&,
      std::uint32_t terrain_recipe = kMoonTerrainRecipeVersion,
      std::uint32_t version = kMoonSurfaceLandmarkVersion)
      -> std::expected<MoonSurfaceLandmarkField, SurfaceLandmarkError>;
  [[nodiscard]] auto site(SurfaceLandmarkCell)
      -> std::expected<std::optional<MoonSurfaceLandmarkSite>,
                       SurfaceLandmarkError>;
  // At most27 cell candidates, at most8 actual centres within384m, sorted by
  // actual range then ID. Distant queries return empty without terrain work.
  [[nodiscard]] auto nearby(MoonFixedPositionMetres)
      -> std::expected<std::vector<MoonSurfaceLandmarkSite>,
                       SurfaceLandmarkError>;
  [[nodiscard]] auto owner() const noexcept -> const PhysicalMoonDescriptor& {
    return sampler_.owner();
  }
  [[nodiscard]] auto cache_size() const noexcept -> std::size_t {
    return memo_count_;
  }

 private:
  struct Memo {
    bool used{};
    SurfaceLandmarkCell cell;
    std::optional<MoonSurfaceLandmarkSite> result;
  };
  MoonSurfaceLandmarkField(MoonTerrainSampler sampler, std::uint32_t version)
      : sampler_(std::move(sampler)), version_(version) {}
  [[nodiscard]] auto build_site(SurfaceLandmarkCell)
      -> std::expected<std::optional<MoonSurfaceLandmarkSite>,
                       SurfaceLandmarkError>;
  MoonTerrainSampler sampler_;
  std::uint32_t version_{};
  std::array<Memo, kSurfaceLandmarkMemoCells> memo_{};
  std::size_t memo_count_{}, next_memo_{};
};
} // namespace apsis_drift
