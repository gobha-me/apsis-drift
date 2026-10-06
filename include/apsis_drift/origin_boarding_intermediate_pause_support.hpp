#pragma once
#include "apsis_drift/origin_boarding_boot_support.hpp"
#include "apsis_drift/origin_boarding_route_foot_phase.hpp"
namespace apsis_drift {
namespace detail {
struct BoardingIntermediatePauseSupportAccess;
}
inline constexpr std::uint32_t kBoardingIntermediatePauseSupportVersion{1};
inline constexpr std::size_t kBoardingIntermediatePauseSourceBytes{4096},
    kBoardingIntermediatePauseOutputBytes{4096},
    kBoardingIntermediatePauseConstructorScratchBytes{8192},
    kBoardingIntermediatePauseScratchBytes{49152};
enum class BoardingIntermediatePauseCondition : std::uint8_t {
  none,
  invalid_limits,
  invalid_binding,
  unsupported_arithmetic,
  source_capacity,
  base_capacity,
  metadata_capacity,
  face_capacity,
  quad_capacity,
  quad_edge_capacity,
  quad_side_capacity,
  vertex_capacity,
  corner_capacity,
  winding_capacity,
  incidence_capacity,
  diagonal_capacity,
  source_identity,
  source_geometry,
  allocation,
  phase_prerequisite,
  projection_capacity,
  projection_identity,
  pressure_capacity,
  edge_capacity,
  output_capacity,
  sole_plane,
  sole_disk,
  source_disk
};
enum class BoardingIntermediatePauseDiskStatus : std::uint8_t {
  not_run,
  contained,
  refuted,
  unresolved
};
struct BoardingIntermediatePauseConstructorCounters {
  std::size_t source_bytes{}, base_guards{}, metadata_rows{}, face_reads{},
      quad_records{}, quad_edges{}, quad_sides{}, face_vertices{},
      corner_matches{}, face_windings{}, incidence{}, diagonals{};
};
struct BoardingIntermediatePauseSourceIdentity {
  std::array<LowerCockpitTriangleKey, 2> keys;
  std::array<std::optional<std::uint32_t>, 2> evaluated_faces;
  std::string_view name;
  std::uint32_t object{};
  std::optional<std::uint32_t> inventory_provenance;
  double plane{};
};
struct BoardingIntermediatePauseQuadEvidence {
  BoardingFootSiteScalarBounds minimum_edge_squared, minimum_signed_side;
  std::size_t checked_edges{}, checked_sides{}, checked_vertices{},
      checked_windings{}, checked_incidence{};
  bool evaluated{}, arithmetic_supported{}, horizontal{}, convex{},
      nondegenerate{}, upward{}, corner_membership{}, distinct_face_vertices{},
      incidence_valid{}, diagonal_valid{}, complete{};
};
struct BoardingIntermediatePauseConstructorEvidence {
  std::uint32_t version{kBoardingIntermediatePauseSupportVersion};
  BoardingIntermediatePauseConstructorCounters work;
  std::size_t required_source_bytes{}, actual_source_bytes{};
  std::array<BoardingIntermediatePauseSourceIdentity, 2> sources;
  std::array<BoardingIntermediatePauseQuadEvidence, 2> quads;
  std::array<bool, 2> identity_checked{}, identity_valid{},
      geometry_evaluated{};
  BoardingIntermediatePauseCondition condition{};
  std::optional<std::size_t> side;
  bool arithmetic_supported{}, complete{};
};
class OriginBoardingIntermediatePauseSupport {
 public:
  struct Data;
  OriginBoardingIntermediatePauseSupport(
      const OriginBoardingIntermediatePauseSupport&) = default;
  OriginBoardingIntermediatePauseSupport(
      OriginBoardingIntermediatePauseSupport&&) noexcept = default;
  auto operator=(const OriginBoardingIntermediatePauseSupport&)
      -> OriginBoardingIntermediatePauseSupport& = default;
  auto operator=(OriginBoardingIntermediatePauseSupport&&) noexcept
      -> OriginBoardingIntermediatePauseSupport& = default;
  [[nodiscard]] auto summary() const
      -> const BoardingIntermediatePauseConstructorEvidence*;

 private:
  explicit OriginBoardingIntermediatePauseSupport(std::shared_ptr<const Data>);
  std::shared_ptr<const Data> data_;
  friend struct detail::BoardingIntermediatePauseSupportAccess;
};
struct BoardingIntermediatePauseCounters {
  std::size_t phase_calls{}, projection_guards{}, pressure_candidates{},
      disk_edges{};
  BoardingRouteFootPhaseCounters phase;
};
struct BoardingIntermediatePauseSiteEvidence {
  std::array<BoardingFootSiteEdgeEvidence, 4> sole_edges{}, source_edges{};
  std::array<bool, 4> sole_evaluated{}, source_evaluated{};
  std::array<BoardingPlantedLegScalarBounds, 2> pressure_xz;
  BoardingIntermediatePauseDiskStatus sole_status{}, source_status{};
  bool loaded{}, plane_identity{}, evaluated{}, complete{};
};
struct BoardingIntermediatePauseRefusal {
  BoardingIntermediatePauseCondition condition{};
  std::optional<std::size_t> side, edge;
  bool source_edge{};
  BoardingPlantedLegScalarBounds limiting_bound;
  BoardingRouteFootPhaseRefusal phase;
};
struct BoardingIntermediatePauseSupportDiagnostic {
  OriginBoardingIntermediatePauseSupport source;
  std::uint32_t version{kBoardingIntermediatePauseSupportVersion};
  BoardingRouteFootPhaseRequest request;
  std::array<BoardingPlantedLegScalarBounds, 2> com_xz, barycenter_xz, delta_xz;
  std::array<std::array<BoardingPlantedLegScalarBounds, 2>, 2> boot_centers_xz;
  std::array<double, 2> source_planes{}, reactions{.25, .75};
  std::array<BoardingIntermediatePauseSiteEvidence, 2> sites;
  BoardingIntermediatePauseCounters work;
  std::optional<BoardingIntermediatePauseRefusal> first_refusal;
  // A later capacity/unsupported stop does not erase the first disk finding.
  BoardingIntermediatePauseCondition stop_condition{};
  std::size_t output_bytes{};
  double duration_seconds{2}, disk_radius_metres{.020},
      edge_margin_metres{.010}, load_margin_metres{.010};
  bool reverse{}, arithmetic_supported{}, kinematic_complete{},
      constant_state{}, projection_complete{}, nominal_equilibrium{},
      finite_contact_supported{}, nominal_load_supported{}, complete{};
  explicit BoardingIntermediatePauseSupportDiagnostic(
      const OriginBoardingIntermediatePauseSupport& p)
      : source(p) {}
  static constexpr bool self_qualified{false}, material_qualified{false},
      world_qualified{false}, route_qualified{false}, seat_qualified{false},
      actor_qualified{false}, save_qualified{false}, strength_qualified{false},
      friction_qualified{false}, dynamics_qualified{false},
      first_flight_qualified{false};
};
[[nodiscard]] auto make_origin_boarding_intermediate_pause_support(
    const NativeCraftBinding&, const OriginBoardingBootSupport&)
    -> std::expected<OriginBoardingIntermediatePauseSupport, std::string>;
[[nodiscard]] auto assess_origin_boarding_intermediate_pause_support(
    const OriginBoardingIntermediatePauseSupport&, bool reverse = false)
    -> std::expected<BoardingIntermediatePauseSupportDiagnostic, std::string>;
} // namespace apsis_drift
