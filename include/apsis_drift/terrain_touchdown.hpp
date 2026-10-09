#pragma once

#include "apsis_drift/godot/saved_contact_geometry.hpp"
#include "apsis_drift/touchdown.hpp"

namespace apsis_drift {

// Gameplay support policy, independent of historical experimental recipes and
// saves: current source LOD8 / relief0 / finest top triangulation, dry bedrock,
// no water landing. Changing any of these semantics requires a new version.
inline constexpr std::uint32_t kTerrainTouchdownPolicyVersion{1};

enum class TerrainSupportError : std::uint8_t {
  unresolved_geometry,
  invalid_geometry,
  first_surface_not_certified
};

struct TerrainTouchdownResult {
  std::uint32_t policy_version{kTerrainTouchdownPolicyVersion};
  godot_spike::SavedContactGeometryBatch geometry;
  std::array<std::optional<
                 std::expected<TouchdownPadObservation, TerrainSupportError>>,
             4>
      supports{};
  // Absent when any footprint cannot be certified. Unknown geometry must not
  // become a fabricated clear/unsafe/ready assessment. Indexed diagnostics
  // above retain the actual reason, including the underlying patch refusal.
  std::optional<TouchdownAssessment> assessment;
  std::optional<TouchdownError> envelope_error;
  friend auto operator==(const TerrainTouchdownResult&,
                         const TerrainTouchdownResult&) -> bool = default;
};

// Immutable, same-tick C++ session snapshot. Factory validates ownership and
// reframes into planet_fixed before any terrain access; only its bounded local
// cache changes on query. Geometry and policy provenance are retained together.
//
// Each registered deployed rectangle must fit one finest top triangle, and
// every normal segment from pad to its projection must stay inside that
// triangle's radial cone. Ambiguous ridges, seams and ill-conditioned queries
// return unknown. Dry facets are assumed rigid bedrock for this game policy;
// neither shader colours nor renderer/camera/cache residency confer support.
//
// gear_deployed is caller-declared, including prospective inspection. This
// does not create actual deployment state. Results are PAD assessments, not
// hull/swept clearance, collision response, landed state or floor-guard
// removal.
class TerrainTouchdownSnapshot {
 public:
  [[nodiscard]] static auto create(
      const NativeFreedomFlightSession&, bool gear_deployed,
      std::uint32_t policy_version = kTerrainTouchdownPolicyVersion,
      std::size_t cache_capacity = kDefaultTerrainTileCacheCapacity)
      -> std::expected<TerrainTouchdownSnapshot,
                       godot_spike::SavedContactError>;
  [[nodiscard]] auto query() -> TerrainTouchdownResult;
  [[nodiscard]] auto cache_size() const -> std::size_t {
    return geometry_.cache_size();
  }

 private:
  TerrainTouchdownSnapshot(godot_spike::SavedContactGeometry geometry,
                           bool gear_deployed)
      : geometry_(std::move(geometry)), gear_deployed_(gear_deployed) {}
  godot_spike::SavedContactGeometry geometry_;
  bool gear_deployed_{};
};
} // namespace apsis_drift
