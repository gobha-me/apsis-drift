#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

#include "apsis_drift/freedom_save.hpp"
#include "apsis_drift/rigid_body.hpp"
#include "apsis_drift/universe_navigation.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomJumpTargetingVersion{1};
inline constexpr std::uint64_t kFreedomJumpRatedLightSeconds{360'000};
inline constexpr std::uint64_t kFreedomJumpRatedMetres{
    kFreedomJumpRatedLightSeconds * kMetresPerLightSecond};
enum class FreedomJumpTargetingError : std::uint8_t {
  unsupported_policy,
  invalid_owner,
  invalid_source,
  invalid_destination,
  invalid_geometry,
  invalid_profile,
  invalid_attempt,
  tick_overflow,
  beyond_qualified_policy,
  invalid_frozen_solution
};
enum class FreedomJumpReach : std::uint8_t { rated, beyond_rated };
struct FreedomJumpDistanceAssessment {
  FreedomJumpReach reach{};
  // Beyond-rated risk is not selected by policy 1; there is no fake envelope.
  std::optional<std::uint32_t> distance_millionths;
  std::optional<std::uint32_t> envelope_radius_metres;
  friend auto operator==(const FreedomJumpDistanceAssessment&,
                         const FreedomJumpDistanceAssessment&)
      -> bool = default;
};
struct FreedomJumpRequest {
  std::uint32_t version{kFreedomJumpTargetingVersion};
  Seed universe_seed;
  StarterCraftId craft;
  RigidBodyState source;
  SystemId destination;
  IntersystemRuleProfile profile{IntersystemRuleProfile::assisted};
  std::uint64_t attempt{1};
  std::optional<FreedomCraftLineage> lineage{};
  friend auto operator==(const FreedomJumpRequest&, const FreedomJumpRequest&)
      -> bool = default;
};
struct FreedomArrivalVolumeAssessment {
  bool intersects_star{};
  std::vector<PlanetId> intersecting_planets;
  [[nodiscard]] auto clear() const -> bool {
    return !intersects_star && intersecting_planets.empty();
  }
  friend auto operator==(const FreedomArrivalVolumeAssessment&,
                         const FreedomArrivalVolumeAssessment&)
      -> bool = default;
};
struct FreedomJumpPreview {
  FreedomJumpRequest request;
  SimulationTick arrival_tick{};
  PlanetId reference_planet;
  SystemPositionMetres source_position, nominal_arrival;
  SystemVelocityMetresPerSecond arrival_velocity;
  double distance_metres{};
  IntersystemArrivalAssessment alignment;
  FreedomJumpDistanceAssessment distance;
  std::optional<FreedomArrivalVolumeAssessment> volume;
  friend auto operator==(const FreedomJumpPreview&, const FreedomJumpPreview&)
      -> bool = default;
};
struct FrozenFreedomJumpArrival {
  FreedomJumpPreview preview;
  Seed sample_seed;
  SystemPositionMetres point;
  FreedomArrivalVolumeAssessment point_assessment;
  friend auto operator==(const FrozenFreedomJumpArrival&,
                         const FrozenFreedomJumpArrival&) -> bool = default;
};
// Pure geometry, not a charge/spool owner or a permission to commit hazardous
// travel. Presentation must project through knowledge and never expose the
// hidden point before commitment. Existing jumps/saves remain unchanged.
[[nodiscard]] auto assess_freedom_jump_distance(double metres,
                                                IntersystemArrivalQuality)
    -> std::expected<FreedomJumpDistanceAssessment, FreedomJumpTargetingError>;
[[nodiscard]] auto assess_freedom_arrival_volume(const PhysicalLocalSystem&,
                                                 SimulationTick,
                                                 SystemPositionMetres,
                                                 double radius_metres)
    -> std::expected<FreedomArrivalVolumeAssessment, FreedomJumpTargetingError>;
[[nodiscard]] auto preview_freedom_jump(const FreedomJumpRequest&)
    -> std::expected<FreedomJumpPreview, FreedomJumpTargetingError>;
[[nodiscard]] auto freeze_freedom_jump_arrival(const FreedomJumpRequest&)
    -> std::expected<FrozenFreedomJumpArrival, FreedomJumpTargetingError>;
[[nodiscard]] auto validate_frozen_freedom_jump_arrival(
    const FrozenFreedomJumpArrival&)
    -> std::expected<void, FreedomJumpTargetingError>;
} // namespace apsis_drift
