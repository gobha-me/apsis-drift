#pragma once

#include "apsis_drift/origin_boarding_source_endpoint_load.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace apsis_drift {
inline constexpr std::uint32_t kBoardingSourceEndpointSurfaceCheckpointVersion{
    1};
inline constexpr std::uint64_t kBoardingSourceEndpointSurfaceMaximumTriangles{
    524288};
inline constexpr std::uint64_t kBoardingSourceEndpointSurfaceMaximumPairs{
    7864320};
inline constexpr std::size_t kBoardingSourceEndpointSurfaceMaximumAxes{16};
struct BoardingSourceEndpointSurfaceCheckpointCoverage {
  RigidVector3 lower_metres, upper_metres;
  BoardingBodyPartId part{};
  bool examined{}, arithmetic_supported{}, covered{};
};
enum class BoardingSourceEndpointSurfaceCheckpointCondition : std::uint8_t {
  load_prerequisite,
  invalid_binding,
  unsupported_arithmetic,
  source_capacity,
  source_roster_changed,
  crop_uncovered,
  pair_capacity,
  axis_capacity,
  no_separation_certificate
};
struct BoardingSourceEndpointSurfaceCheckpointRefusal {
  std::optional<LowerCockpitTriangleKey> key;
  // The owned load child retains immutable source/name storage across moves.
  std::string_view source_object;
  // Capacity points to the NEXT UNATTEMPTED pair; other pair failures include
  // their attempted stopping pair. Part/prerequisite failures have no index.
  std::optional<std::uint64_t> pair_index;
  std::uint64_t axes_examined{}, unsupported_axes{};
  std::uint32_t object{};
  std::optional<std::uint32_t> evaluated_source_triangle;
  BoardingBodyPartId part{};
  BoardingSourceEndpointSurfaceCheckpointCondition condition{};
};
struct BoardingSourceEndpointSurfaceCheckpointPayload {
  std::uint32_t surface_version{
      kBoardingSourceEndpointSurfaceCheckpointVersion};
  std::array<BoardingSourceEndpointSurfaceCheckpointCoverage,
             kBoardingBodyPartCount>
      coverage{};
  std::uint64_t effective_triangle_count{}, expected_pairs{}, examined_pairs{},
      certified_pairs{}, broad_certified_pairs{}, sole_certified_pairs{},
      support_certified_pairs{}, axes_examined{}, unsupported_axes{},
      visited_triangles{}, metadata_visited_triangles{};
  bool arithmetic_supported{}, coverage_complete{}, comparisons_complete{},
      surface_qualified{}, complete{};
  std::optional<BoardingSourceEndpointSurfaceCheckpointRefusal> first_refusal;
};
struct BoardingSourceEndpointSurfaceCheckpointDiagnostic {
  BoardingSourceEndpointLoadDiagnostic load;
  BoardingSourceEndpointSurfaceCheckpointPayload surface;
  // Triangle boundaries can miss an enclosing closed obstacle's interior.
  // This is surface separation only, never overall world/volume clearance.
  static constexpr bool world_qualified{false}, volume_qualified{false},
      material_qualified{false}, strength_qualified{false},
      dynamics_qualified{false}, sweep_qualified{false},
      continuous_qualified{false}, acquisition_qualified{false},
      free_foot_swing_qualified{false}, transfer_qualified{false},
      route_qualified{false}, actor_qualified{false}, seat_qualified{false},
      save_qualified{false}, first_flight_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_source_endpoint_surface_checkpoint(
    const OriginBoardingBootSupport&)
    -> std::expected<BoardingSourceEndpointSurfaceCheckpointDiagnostic,
                     std::string>;
} // namespace apsis_drift
