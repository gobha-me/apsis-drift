#pragma once
#include "apsis_drift/origin_boarding_route_intermediate_unloaded01.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"
namespace apsis_drift::detail {
struct BoardingRouteIntermediateUnloaded01Limits {
  BoardingRouteFootPhaseLimits phase;
  std::size_t source_guards{32}, projection_guards{std::size_t{251} * 2047},
      definition_guards{std::size_t{32} * 2047}, pressure_candidates{2047},
      disk_edges{std::size_t{8} * 2047},
      upper_geometry_edges{std::size_t{8} * 2047},
      intermediate_coordinates{std::size_t{8} * 2047},
      endpoint_operations{std::size_t{12} * 2047},
      event_guards{std::size_t{16} * 2047};
};
enum class BoardingRouteIntermediateUnloaded01GeometryMode : std::uint8_t {
  upper_point,
  upper_departure,
  intermediate_overlap,
  upper_cell
};
struct BoardingRouteIntermediateUnloaded01GeometryInput {
  BoardingRouteIntermediateUnloaded01GeometryMode mode{};
  std::array<RigidVector3, 4> source_vertices;
  double source_plane{}, sole_plane{};
  std::array<BoardingFootSiteScalarBounds, 2> center_xz;
};
struct BoardingRouteIntermediateUnloaded01GeometryLimits {
  std::size_t upper_edges{8}, source_coordinates{8}, operations{12};
};
struct BoardingRouteIntermediateUnloaded01GeometryCounters {
  std::size_t upper_edges{}, source_coordinates{}, operations{},
      validation_guards{};
};
struct BoardingRouteIntermediateUnloaded01GeometryMath {
  BoardingRouteIntermediateUnloaded01GeometryMode mode{};
  std::array<BoardingFootSiteScalarBounds, 2> center_xz;
  std::array<BoardingFootSiteScalarBounds, 4> signed_sides;
  std::array<bool, 4> side_evaluated{};
  BoardingFootSiteScalarBounds minimum_signed_side, departure_gap;
  std::array<std::array<BoardingFootSiteScalarBounds, 2>, 2> sole_extrema,
      source_extrema, intersection;
  std::array<bool, 8> source_coordinate_evaluated{};
  std::array<bool, 12> operation_evaluated{};
  std::array<bool, 3> input_validation_evaluated{};
  std::array<bool, 2> positive_overlap{};
  BoardingRouteIntermediateUnloaded01GeometryCounters work;
  BoardingRouteIntermediateUnloaded01State state{};
  BoardingRouteIntermediateUnloaded01Condition condition{};
  BoardingRouteIntermediateUnloaded01UpperClass classification{};
  bool arithmetic_supported{}, input_validated{}, complete{};
  static constexpr bool source_qualified{false}, body_qualified{false},
      contact_qualified{false}, load_qualified{false}, support_qualified{false},
      self_qualified{false}, material_qualified{false}, world_qualified{false},
      route_qualified{false}, actor_qualified{false}, seat_qualified{false},
      save_qualified{false}, dynamics_qualified{false};
};
[[nodiscard]] auto boarding_route_intermediate_unloaded01_upper_partition(
    const OriginBoardingIntermediatePauseSupport&)
    -> const BoardingBootSourcePartition*;
[[nodiscard]] auto boarding_route_intermediate_unloaded01_controls(std::size_t)
    -> std::optional<BoardingRouteFootPhaseRequest>;
[[nodiscard]] auto boarding_route_intermediate_unloaded01_phase(double, double)
    -> std::optional<std::size_t>;
[[nodiscard]] auto boarding_route_intermediate_unloaded01_local(std::size_t,
                                                                double)
    -> double;
[[nodiscard]] auto boarding_route_intermediate_unloaded01_clock(double)
    -> double;
[[nodiscard]] auto boarding_route_intermediate_unloaded01_join_math(
    const BoardingRouteFootPhaseRequest&, const BoardingRouteFootPhaseRequest&)
    -> bool;
[[nodiscard]] auto boarding_route_intermediate_unloaded01_bounded(
    const OriginBoardingIntermediatePauseSupport&, double, double,
    BoardingRouteIntermediateUnloaded01Limits = {})
    -> std::expected<BoardingRouteIntermediateUnloaded01Diagnostic,
                     std::string>;
[[nodiscard]] auto boarding_route_intermediate_unloaded01_geometry_math(
    const BoardingRouteIntermediateUnloaded01GeometryInput&,
    BoardingRouteIntermediateUnloaded01GeometryLimits = {})
    -> BoardingRouteIntermediateUnloaded01GeometryMath;
class BoardingRouteIntermediateUnloaded01AdmissionContext {
 private:
  BoardingRouteIntermediateUnloaded01AdmissionContext(
      const BoardingRouteIntermediateUnloaded01Diagnostic& d,
      const BoardingRouteFootPhaseRequest& r)
      : owner_(&d),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)),
        parts_(&d.parts), request_(&r) {}
  const BoardingRouteIntermediateUnloaded01Diagnostic* owner_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  const std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount>*
      parts_;
  const BoardingRouteFootPhaseRequest* request_;
  friend auto boarding_route_intermediate_unloaded01_bounded(
      const OriginBoardingIntermediatePauseSupport&, double, double,
      BoardingRouteIntermediateUnloaded01Limits)
      -> std::expected<BoardingRouteIntermediateUnloaded01Diagnostic,
                       std::string>;
  friend auto boarding_route_intermediate_unloaded01_current_cell(
      const BoardingRouteIntermediateUnloaded01AdmissionContext&,
      BoardingRouteIntermediateUnloaded01Diagnostic&,
      BoardingRouteIntermediateUnloaded01Cell&,
      const BoardingRouteIntermediateUnloaded01Limits&,
      BoardingRouteIntermediateUnloaded01Refusal&)
      -> BoardingRouteIntermediateUnloaded01State;
  friend auto boarding_route_intermediate_unloaded01_pressure_bridge(
      const class BoardingRouteIntermediateUnloaded01CurrentCellToken&,
      BoardingRouteIntermediateUnloaded01Diagnostic&,
      BoardingRouteIntermediateUnloaded01Cell&,
      const BoardingRouteIntermediateUnloaded01Limits&,
      BoardingRouteIntermediateUnloaded01Refusal&)
      -> BoardingRouteIntermediateUnloaded01State;
};
[[nodiscard]] auto boarding_route_intermediate_unloaded01_current_cell(
    const BoardingRouteIntermediateUnloaded01AdmissionContext&,
    BoardingRouteIntermediateUnloaded01Diagnostic&,
    BoardingRouteIntermediateUnloaded01Cell&,
    const BoardingRouteIntermediateUnloaded01Limits&,
    BoardingRouteIntermediateUnloaded01Refusal&)
    -> BoardingRouteIntermediateUnloaded01State;
class BoardingRouteIntermediateUnloaded01CurrentCellToken {
 private:
  BoardingRouteIntermediateUnloaded01CurrentCellToken(
      const BoardingRouteIntermediateUnloaded01AdmissionContext& context,
      const BoardingRouteIntermediateUnloaded01Diagnostic& d,
      const BoardingRouteIntermediateUnloaded01Cell& c,
      const BoardingRouteFootPhaseRequest& r)
      : context_(&context), owner_(&d), cell_(&c), request_(&r),
        data_(BoardingIntermediatePauseSupportAccess::data(d.source)) {}
  const BoardingRouteIntermediateUnloaded01AdmissionContext* context_;
  const BoardingRouteIntermediateUnloaded01Diagnostic* owner_;
  const BoardingRouteIntermediateUnloaded01Cell* cell_;
  const BoardingRouteFootPhaseRequest* request_;
  const OriginBoardingIntermediatePauseSupport::Data* data_;
  friend auto boarding_route_intermediate_unloaded01_current_cell(
      const BoardingRouteIntermediateUnloaded01AdmissionContext&,
      BoardingRouteIntermediateUnloaded01Diagnostic&,
      BoardingRouteIntermediateUnloaded01Cell&,
      const BoardingRouteIntermediateUnloaded01Limits&,
      BoardingRouteIntermediateUnloaded01Refusal&)
      -> BoardingRouteIntermediateUnloaded01State;
  friend auto boarding_route_intermediate_unloaded01_pressure_bridge(
      const BoardingRouteIntermediateUnloaded01CurrentCellToken&,
      BoardingRouteIntermediateUnloaded01Diagnostic&,
      BoardingRouteIntermediateUnloaded01Cell&,
      const BoardingRouteIntermediateUnloaded01Limits&,
      BoardingRouteIntermediateUnloaded01Refusal&)
      -> BoardingRouteIntermediateUnloaded01State;
};
auto boarding_route_intermediate_unloaded01_definition_charge(
    BoardingRouteIntermediateUnloaded01Diagnostic&,
    BoardingRouteIntermediateUnloaded01Cell&,
    const BoardingRouteIntermediateUnloaded01Limits&,
    BoardingRouteIntermediateUnloaded01Refusal&) -> bool;
auto boarding_route_intermediate_unloaded01_pressure_bridge(
    const BoardingRouteIntermediateUnloaded01CurrentCellToken&,
    BoardingRouteIntermediateUnloaded01Diagnostic&,
    BoardingRouteIntermediateUnloaded01Cell&,
    const BoardingRouteIntermediateUnloaded01Limits&,
    BoardingRouteIntermediateUnloaded01Refusal&)
    -> BoardingRouteIntermediateUnloaded01State;
} // namespace apsis_drift::detail
