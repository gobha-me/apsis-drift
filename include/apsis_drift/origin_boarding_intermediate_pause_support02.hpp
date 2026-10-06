#pragma once
#include "apsis_drift/origin_boarding_intermediate_pause_support.hpp"
namespace apsis_drift {
inline constexpr std::uint32_t kBoardingIntermediatePauseSupport02Version{2};
inline constexpr std::size_t kBoardingIntermediatePauseSupport02OutputBytes{
    4096},
    kBoardingIntermediatePauseSupport02ScratchBytes{49152},
    kBoardingIntermediatePauseSupport02ProjectionGuards{256},
    kBoardingIntermediatePauseSupport02DefinitionGuards{31};
enum class BoardingIntermediatePauseSupport02State : std::uint8_t {
  not_run,
  prerequisite_refused,
  capacity,
  unsupported,
  witness_refused,
  unresolved,
  supported
};
enum class BoardingIntermediatePauseSupport02Condition : std::uint8_t {
  none,
  invalid_limits,
  output_capacity,
  invalid_binding,
  unsupported_arithmetic,
  phase_prerequisite,
  projection_capacity,
  projection_identity,
  sole_plane,
  source_rectangle,
  empty_intersection,
  denominator,
  definition_capacity,
  sole_extrema_capacity,
  source_coordinate_capacity,
  intersection_capacity,
  midpoint_capacity,
  allocation_capacity,
  pressure_capacity,
  edge_capacity,
  symbolic_equilibrium,
  sole_disk,
  source_disk
};
struct BoardingIntermediatePauseSupport02Refusal {
  BoardingIntermediatePauseSupport02Condition condition{};
  std::optional<std::size_t> side, axis, vertex, edge, operation;
  bool source_edge{};
  BoardingFootSiteScalarBounds limiting_bound;
  BoardingRouteFootPhaseRefusal phase;
};
struct BoardingIntermediatePauseSupport02Counters {
  std::size_t phase_calls{};
  BoardingRouteFootPhaseCounters phase;
  std::size_t projection_guards{}, definition_guards{}, sole_extrema{},
      source_coordinates{}, intersection_operations{}, midpoint_operations{},
      allocation_operations{}, pressure_candidates{}, disk_edges{};
};
struct BoardingIntermediatePauseSupport02Source {
  std::string_view name;
  std::array<LowerCockpitTriangleKey, 2> keys;
  double plane{};
};
struct BoardingIntermediatePauseSupport02Diagnostic {
  OriginBoardingIntermediatePauseSupport source;
  std::uint32_t version{kBoardingIntermediatePauseSupport02Version};
  bool reverse{};
  double duration_seconds{2};
  std::array<double, 2> reactions{.0625, .9375};
  std::array<BoardingFootSiteScalarBounds, 2> com_xz;
  std::array<std::array<BoardingFootSiteScalarBounds, 2>, 2> boot_centers_xz;
  std::array<double, 2> source_planes{};
  std::array<BoardingFootSiteScalarBounds, 2> port_sole_lower_xz,
      port_sole_upper_xz, port_source_lower_xz, port_source_upper_xz,
      intersection_lower_xz, intersection_upper_xz;
  std::array<std::array<BoardingFootSiteScalarBounds, 2>, 2> pressure_xz;
  std::array<std::array<bool, 2>, 4> source_coordinate_evaluated{};
  std::array<std::array<bool, 2>, 2> sole_extrema_evaluated{},
      intersection_evaluated{}, midpoint_evaluated{};
  std::array<std::array<bool, 3>, 2> allocation_evaluated{};
  std::array<bool, 2> pressure_evaluated{};
  std::array<BoardingIntermediatePauseSupport02Source, 2> sources;
  std::array<BoardingIntermediatePauseSiteEvidence, 2> sites;
  BoardingIntermediatePauseSupport02Counters work;
  std::array<bool, kBoardingIntermediatePauseSupport02DefinitionGuards>
      definition_evaluated{};
  std::array<bool, 11> projected_carrier_complete{};
  std::optional<BoardingIntermediatePauseSupport02Refusal> first_refusal;
  BoardingIntermediatePauseSupport02Condition stop_condition{};
  std::size_t output_bytes{};
  BoardingIntermediatePauseSupport02State state{};
  bool arithmetic_supported{}, kinematic_complete{}, constant_state{},
      projection_complete{}, intersection_complete{}, nominal_equilibrium{},
      finite_contact_supported{}, nominal_load_supported{}, complete{};
  explicit BoardingIntermediatePauseSupport02Diagnostic(
      const OriginBoardingIntermediatePauseSupport& p)
      : source(p) {}
  static constexpr bool self_qualified{false}, material_qualified{false},
      world_qualified{false}, route_qualified{false}, seat_qualified{false},
      actor_qualified{false}, save_qualified{false}, strength_qualified{false},
      friction_qualified{false}, dynamics_qualified{false},
      first_flight_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_intermediate_pause_support02(
    const OriginBoardingIntermediatePauseSupport&, bool reverse = false)
    -> std::expected<BoardingIntermediatePauseSupport02Diagnostic, std::string>;
} // namespace apsis_drift
