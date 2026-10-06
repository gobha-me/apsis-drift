#include "origin_boarding_intermediate_endpoint01_internal.hpp"
#include "origin_boarding_intermediate_pause_support02_internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
namespace apsis_drift {
namespace {
using D = BoardingIntermediateEndpoint01Diagnostic;
using C = BoardingIntermediateEndpoint01Cell;
using R = BoardingIntermediateEndpoint01Refusal;
using W = BoardingIntermediateEndpoint01Condition;
using S = BoardingIntermediateEndpoint01State;
using Stage = BoardingIntermediateEndpoint01SelfStage;
using L = detail::BoardingIntermediateEndpoint01Limits;
using E = BoardingIntermediateEndpoint01Expected;
using Request = BoardingRouteFootPhaseRequest;
using A = detail::BoardingIntermediatePauseSupportAccess;
using P = BoardingPlantedBodyPointId;
constexpr std::size_t fixed_output = 2 * sizeof(E);
static_assert(sizeof(E) <= kBoardingIntermediateEndpoint01MaximumExpectedBytes);
static_assert(sizeof(C) <= kBoardingIntermediateEndpoint01MaximumCellBytes);
static_assert(sizeof(R) <= 256);
static_assert(sizeof(L) == 184);
constexpr auto graph_live =
    32768 + fixed_output + 4096 + sizeof(C) -
    sizeof(BoardingRouteFootPhaseCell) + 4 * sizeof(L) + 2048 +
    sizeof(detail::BoardingRouteFootPhaseLimits) +
    sizeof(BoardingRouteFootPhaseRefusal) + sizeof(std::optional<Request>) +
    sizeof(Request) + sizeof(R) +
    sizeof(detail::BoardingIntermediateEndpoint01AdmissionContext) +
    sizeof(detail::BoardingIntermediateEndpoint01CurrentToken) + 32;
static_assert(graph_live <= kBoardingIntermediateEndpoint01MaximumScratchBytes);
static_assert(fixed_output + sizeof(C) <=
              kBoardingIntermediateEndpoint01MaximumOutputBytes);
auto cap(W w) -> bool {
  return w == W::output_capacity || w == W::source_capacity ||
         w == W::projection_capacity || w == W::definition_capacity ||
         w == W::sole_extrema_capacity || w == W::source_coordinate_capacity ||
         w == W::intersection_capacity || w == W::midpoint_capacity ||
         w == W::allocation_capacity || w == W::pressure_capacity ||
         w == W::edge_capacity || w == W::self_body_capacity ||
         w == W::unit_axis_capacity || w == W::self_pair_capacity ||
         w == W::self_axis_capacity || w == W::self_signed_capacity ||
         w == W::self_owner_capacity || w == W::self_hip_capacity;
}
auto same(const BoardingRoutePhaseConstant& a,
          const BoardingRoutePhaseConstant& b) -> bool {
  return a.count == b.count && a.terms == b.terms;
}
auto constant(const BoardingRoutePhasePointConstant& a,
              const BoardingRoutePhasePointConstant& b) -> bool {
  for (std::size_t j = 0; j < 3; ++j)
    if (!same(a.coordinates[j], b.coordinates[j])) return false;
  return true;
}
auto valid_limits(const L& l) -> bool {
  const L max;
  return l.phase.graphs <= max.phase.graphs && l.phase.legs <= max.phase.legs &&
         l.phase.bodies <= max.phase.bodies &&
         l.phase.sectors <= max.phase.sectors &&
         l.phase.timing <= max.phase.timing &&
         l.source_guards <= max.source_guards &&
         l.projection_guards <= max.projection_guards &&
         l.definition_guards <= max.definition_guards &&
         l.sole_extrema <= max.sole_extrema &&
         l.source_coordinates <= max.source_coordinates &&
         l.intersection_operations <= max.intersection_operations &&
         l.midpoint_operations <= max.midpoint_operations &&
         l.allocation_operations <= max.allocation_operations &&
         l.pressure_candidates <= max.pressure_candidates &&
         l.disk_edges <= max.disk_edges &&
         l.self_body_guards <= max.self_body_guards &&
         l.self_pairs <= max.self_pairs && l.self_axes <= max.self_axes &&
         l.self_signed_trials <= max.self_signed_trials &&
         l.self_owners <= max.self_owners &&
         l.self_hip_complements <= max.self_hip_complements &&
         l.unit_axis_operations <= max.unit_axis_operations &&
         l.output_bytes <= max.output_bytes;
}
auto coord(const RigidVector3& v, std::size_t i) -> double {
  return i == 0 ? v.x : i == 1 ? v.y : v.z;
}
auto projection_charge(D& d, const L& l, R& r) -> bool {
  r = R{};
  r.self_stage = Stage::projection;
  r.operation = static_cast<std::uint8_t>(d.work.projection_guards);
  return detail::intermediate_endpoint01_charge(d, r, d.work.projection_guards,
                                                l.projection_guards,
                                                W::projection_capacity);
}
auto point_constant(const BoardingRoutePhaseConstant& x,
                    const std::array<double, 3>& terms, std::size_t count)
    -> bool {
  return x.count == count && x.terms == terms;
}
[[gnu::noinline]] auto enroll(D& d, Request& request, const L& l, R& r)
    -> bool {
  const BoardingIntermediatePauseConstructorEvidence* summary = nullptr;
  const BoardingBootSourcePartition *port = nullptr, *star = nullptr;
  for (std::uint8_t i = 0; i < 33; ++i) {
    r = R{};
    r.self_stage = Stage::source;
    r.operation = i;
    if (!detail::intermediate_endpoint01_charge(
            d, r, d.work.source_guards, l.source_guards, W::source_capacity))
      return false;
    d.source_evaluated |= std::uint64_t{1} << i;
    bool ok = false;
    switch (i) {
      case 0:
        ok = A::valid(d.source);
        if (!ok)
          return detail::intermediate_endpoint01_refuse(d, r,
                                                        W::invalid_binding);
        break;
      case 1:
        ok = detail::boarding_route_foot_phase_environment();
        if (!ok)
          return detail::intermediate_endpoint01_refuse(
              d, r, W::unsupported_arithmetic);
        d.arithmetic_supported = true;
        break;
      case 2:
        summary = d.source.summary();
        ok = summary && summary->complete && summary->version == 1 &&
             summary->actual_source_bytes == 4096;
        break;
      case 3: ok = A::data(d.source) != nullptr; break;
      case 4:
        port = A::partition(d.source, 0);
        ok = port != nullptr;
        break;
      case 5:
        star = A::partition(d.source, 1);
        ok = star != nullptr;
        break;
      case 6:
        ok = summary->sources[0].object == 10 &&
             summary->sources[0].evaluated_faces ==
                 std::array<std::optional<std::uint32_t>, 2>{182, 183} &&
             summary->sources[0].keys ==
                 std::array<LowerCockpitTriangleKey, 2>{
                     {{LowerCockpitContactBuffer::halo, 0, 662},
                      {LowerCockpitContactBuffer::halo, 0, 663}}} &&
             port->faces[0].key == summary->sources[0].keys[0] &&
             port->faces[1].key == summary->sources[0].keys[1];
        break;
      case 7:
        ok = summary->sources[1].object == 124 &&
             summary->sources[1].keys ==
                 std::array<LowerCockpitTriangleKey, 2>{
                     {{LowerCockpitContactBuffer::original, 0, 62119},
                      {LowerCockpitContactBuffer::original, 0, 62120}}} &&
             star->faces[0].key == summary->sources[1].keys[0] &&
             star->faces[1].key == summary->sources[1].keys[1];
        break;
      case 8:
        ok = summary->sources[0].name ==
                 "CRAFT | pilot transition intermediate step" &&
             summary->sources[0].inventory_provenance == 735 &&
             summary->sources[1].name == "CABIN | cockpit transition step";
        break;
      case 9:
        ok = port->plane_metres == static_cast<double>(-230000) * 1e-6 &&
             star->plane_metres == static_cast<double>(-160000) * 1e-6 &&
             summary->sources[0].plane == port->plane_metres &&
             summary->sources[1].plane == star->plane_metres;
        break;
      case 10:
      case 11: {
        const auto& q = summary->quads[i - 10];
        ok = q.complete && q.arithmetic_supported && q.horizontal && q.upward &&
             q.convex && q.nondegenerate && q.corner_membership &&
             q.distinct_face_vertices && q.incidence_valid && q.diagonal_valid;
        if (i == 10) {
          const auto& v = port->perimeter_metres;
          ok = ok && v[0].x == v[3].x && v[1].x == v[2].x && v[0].z == v[1].z &&
               v[2].z == v[3].z && v[1].x < v[0].x && v[0].z < v[2].z;
        }
        break;
      }
      case 12:
        ok = kBoardingBootPressureRadiusMetres == .020 &&
             kBoardingBootDiskEdgeMarginMetres == .010 &&
             kBoardingBootLoadMarginMetres == .010;
        break;
      case 13:
        d.parts = detail::boarding_route_foot_phase_parts();
        ok = true;
        break;
      case 14:
        ok = d.parts.size() == 15;
        for (std::size_t j = 0; j < 15; ++j)
          ok = ok && static_cast<std::size_t>(d.parts[j].id) == j;
        break;
      case 15:
        ok = d.candidate == BoardingIntermediateEndpoint01Candidate::
                                authored_descent_midpoint &&
             d.version == 1;
        break;
      case 16: ok = kBoardingRouteFootPhaseVersion == 1; break;
      case 17: {
        const auto value = detail::intermediate_endpoint01_request(d.candidate);
        ok = value.has_value();
        if (value) request = *value;
        break;
      }
      case 18:
      case 19: {
        const std::size_t a = i == 18 ? 0 : 2;
        const std::array<double, 3> t =
            i == 18 ? std::array<double, 3>{.16, .16, -.0075}
                    : std::array<double, 3>{-.55, -.17, .19};
        ok = point_constant(request.root[0].coordinates[a], t, 3) &&
             point_constant(request.root[1].coordinates[a], t, 3);
        break;
      }
      case 20:
        ok = point_constant(request.root[0].coordinates[1],
                            {.847, .012, (-.359) / 2.0}, 3) &&
             point_constant(request.root[1].coordinates[1],
                            {.847, .012, (-.359) / 2.0}, 3);
        break;
      case 21:
        ok = constant(request.root[0], request.root[1]);
        for (const auto& end : request.root)
          for (const auto& value : end.coordinates)
            for (std::size_t j = value.count; j < 3; ++j)
              ok = ok && value.terms[j] == 0;
        break;
      case 22:
      case 23: {
        const std::size_t side = i - 22;
        const std::array<std::array<double, 3>, 3> t =
            side == 0
                ? std::array<std::array<double, 3>,
                             3>{{{.16, -.14, .16},
                                 {static_cast<double>(-230000) * 1e-6, 0, 0},
                                 {-1.14, 0, 0}}}
                : std::array<std::array<double, 3>, 3>{
                      {{.16, .14, 0},
                       {static_cast<double>(-160000) * 1e-6, 0, 0},
                       {-.8, 0, 0}}};
        const std::array<std::size_t, 3> n =
            side == 0 ? std::array<std::size_t, 3>{3, 1, 1}
                      : std::array<std::size_t, 3>{2, 1, 1};
        ok = true;
        for (const auto& end : request.feet[side].sole)
          for (std::size_t a = 0; a < 3; ++a)
            ok = ok && point_constant(end.coordinates[a], t[a], n[a]);
        break;
      }
      case 24:
        ok = request.root_yaw_half == std::array<double, 2>{.125, .125};
        break;
      case 25:
        ok = request.torso_lean_half == std::array<double, 2>{-.30, -.30};
        break;
      case 26:
        ok = request.feet[0].yaw_half == std::array<double, 2>{0, 0} &&
             request.feet[1].yaw_half == std::array<double, 2>{0, 0};
        break;
      case 27:
        ok = request.feet[0].swing_height_metres == 0 &&
             request.feet[1].swing_height_metres == 0;
        break;
      case 28:
        ok = request.port_reaction_fraction ==
                 std::array<double, 2>{.0625, .0625} &&
             1 - .0625 == .9375;
        break;
      case 29: ok = request.seconds_per_parameter == 2; break;
      case 30:
      case 31: {
        const std::size_t side = i - 30;
        const auto* src = side == 0 ? port : star;
        ok = true;
        for (const auto& end : request.feet[side].sole)
          ok = ok &&
               point_constant(end.coordinates[1], {src->plane_metres, 0, 0}, 1);
        break;
      }
      case 32:
        ok = detail::intermediate_endpoint01_source_request_valid(request);
        break;
      default: break;
    }
    if (!ok)
      return detail::intermediate_endpoint01_refuse(d, r, W::source_identity);
  }
  if (!detail::intermediate_endpoint01_enroll_self_source(d, l, r))
    return false;
  d.source_enrolled =
      d.source_evaluated == std::numeric_limits<std::uint64_t>::max();
  if (!d.source_enrolled)
    return detail::intermediate_endpoint01_refuse(d, r, W::source_identity);
  d.reporting_elapsed_seconds = 2;
  return true;
}
auto carrier_value(const C& c, std::size_t index)
    -> const BoardingPlantedBodyPointEvidence& {
  if (index == 0) return c.phase.center_of_mass;
  if (index < 5) {
    const std::array<P, 4> ids{P::port_boot_center, P::starboard_boot_center,
                               P::port_ankle, P::starboard_ankle};
    return c.phase.points[static_cast<std::size_t>(ids[index - 1])];
  }
  return c.phase.frames[2 + (index - 5) / 3].columns[(index - 5) % 3];
}
auto carrier(D& d, C& c, const L& l, R& r, std::size_t index) -> bool {
  for (std::size_t kind = 0; kind < 3; ++kind)
    for (std::size_t a = 0; a < 3; ++a) {
      if (!projection_charge(d, l, r)) return false;
      const auto& value = carrier_value(c, index);
      const auto& b = kind == 0   ? value.value
                      : kind == 1 ? value.derivatives.velocity
                                  : value.derivatives.acceleration;
      const auto low = coord(b.lower, a);
      if (!std::isfinite(low) || (kind == 0 && std::abs(low) > 8) ||
          (kind != 0 && low > 0))
        return detail::intermediate_endpoint01_refuse(
            d, r, W::unsupported_arithmetic);
      if (!projection_charge(d, l, r)) return false;
      const auto high = coord(b.upper, a);
      if (!std::isfinite(high) || low > high ||
          (kind == 0 && std::abs(high) > 8) || (kind != 0 && high < 0))
        return detail::intermediate_endpoint01_refuse(
            d, r, W::unsupported_arithmetic);
    }
  c.projected_carrier_complete[index] = true;
  return true;
}
[[gnu::noinline]] auto projection(D& d, C& c, const Request& request,
                                  const L& l, R& r) -> bool {
  const auto& p = c.phase;
  for (std::size_t i = 0; i < 7; ++i) {
    if (!projection_charge(d, l, r)) return false;
    const bool ok = i == 0   ? p.complete
                    : i == 1 ? p.arithmetic_supported
                    : i == 2 ? p.nominal_links
                    : i == 3 ? p.target_sole_identities
                    : i == 4 ? p.joint_sectors
                    : i == 5 ? p.derivative_domains
                             : p.timing_complete;
    if (!ok)
      return detail::intermediate_endpoint01_refuse(d, r,
                                                    W::phase_prerequisite);
  }
  if (!projection_charge(d, l, r)) return false;
  if (p.first != 0)
    return detail::intermediate_endpoint01_refuse(d, r, W::projection_identity);
  if (!projection_charge(d, l, r)) return false;
  if (p.last != 1)
    return detail::intermediate_endpoint01_refuse(d, r, W::projection_identity);
  std::optional<Request> canonical;
  for (std::size_t a = 0; a < 3; ++a) {
    if (!projection_charge(d, l, r)) return false;
    if (!canonical)
      canonical = detail::intermediate_endpoint01_request(d.candidate);
    if (!canonical ||
        !same(request.root[0].coordinates[a],
              canonical->root[0].coordinates[a]) ||
        !same(request.root[1].coordinates[a],
              canonical->root[1].coordinates[a]) ||
        !same(request.root[0].coordinates[a], request.root[1].coordinates[a]))
      return detail::intermediate_endpoint01_refuse(d, r,
                                                    W::projection_identity);
  }
  for (std::size_t side = 0; side < 2; ++side) {
    for (std::size_t a = 0; a < 3; ++a) {
      if (!projection_charge(d, l, r)) return false;
      if (!same(request.feet[side].sole[0].coordinates[a],
                canonical->feet[side].sole[0].coordinates[a]) ||
          !same(request.feet[side].sole[1].coordinates[a],
                canonical->feet[side].sole[1].coordinates[a]) ||
          !same(request.feet[side].sole[0].coordinates[a],
                request.feet[side].sole[1].coordinates[a]))
        return detail::intermediate_endpoint01_refuse(d, r,
                                                      W::projection_identity);
    }
    if (!projection_charge(d, l, r)) return false;
    if (request.feet[side].yaw_half != std::array<double, 2>{0, 0})
      return detail::intermediate_endpoint01_refuse(d, r,
                                                    W::projection_identity);
    if (!projection_charge(d, l, r)) return false;
    if (request.feet[side].swing_height_metres != 0)
      return detail::intermediate_endpoint01_refuse(d, r,
                                                    W::projection_identity);
  }
  for (std::size_t i = 0; i < 4; ++i) {
    if (!projection_charge(d, l, r)) return false;
    const bool ok =
        i == 0   ? request.root_yaw_half == canonical->root_yaw_half
        : i == 1 ? request.torso_lean_half == canonical->torso_lean_half
        : i == 2
            ? request.port_reaction_fraction ==
                  canonical->port_reaction_fraction
            : request.seconds_per_parameter == canonical->seconds_per_parameter;
    if (!ok)
      return detail::intermediate_endpoint01_refuse(d, r,
                                                    W::projection_identity);
  }
  for (std::size_t i = 0; i < 15; ++i) {
    if (!projection_charge(d, l, r)) return false;
    if (static_cast<std::size_t>(d.parts[i].id) != i)
      return detail::intermediate_endpoint01_refuse(d, r,
                                                    W::projection_identity);
  }
  for (std::size_t side = 0; side < 2; ++side) {
    const auto index = static_cast<std::size_t>(
        side == 0 ? BoardingBodyPartId::port_boot
                  : BoardingBodyPartId::starboard_boot);
    if (!projection_charge(d, l, r)) return false;
    const auto* box =
        std::get_if<BoardingRoutePhaseBoxBinding>(&d.parts[index].reservation);
    if (!box)
      return detail::intermediate_endpoint01_refuse(d, r,
                                                    W::projection_identity);
    if (!projection_charge(d, l, r)) return false;
    if (box->center !=
        (side == 0 ? P::port_boot_center : P::starboard_boot_center))
      return detail::intermediate_endpoint01_refuse(d, r,
                                                    W::projection_identity);
    if (!projection_charge(d, l, r)) return false;
    if (box->half_size_metres != RigidVector3{.06, .05, .14})
      return detail::intermediate_endpoint01_refuse(d, r,
                                                    W::projection_identity);
    if (!projection_charge(d, l, r)) return false;
    if (box->frame != (side == 0 ? BoardingRoutePhaseFrame::port_sole
                                 : BoardingRoutePhaseFrame::starboard_sole))
      return detail::intermediate_endpoint01_refuse(d, r,
                                                    W::projection_identity);
    if (!projection_charge(d, l, r)) return false;
    const auto* source = A::partition(d.source, side);
    if (!source ||
        !point_constant(request.feet[side].sole[0].coordinates[1],
                        {source->plane_metres, 0, 0}, 1) ||
        !point_constant(request.feet[side].sole[1].coordinates[1],
                        {source->plane_metres, 0, 0}, 1))
      return detail::intermediate_endpoint01_refuse(d, r, W::sole_plane);
    c.sites[side].plane_identity = true;
  }
  if (!projection_charge(d, l, r)) return false;
  const auto share = p.port_reaction_fraction;
  if (!std::isfinite(share.lower) || !std::isfinite(share.upper) ||
      share.lower > share.upper || share.lower > .0625 || share.upper < .0625)
    return detail::intermediate_endpoint01_refuse(d, r,
                                                  W::unsupported_arithmetic);
  c.constant_state = true;
  for (std::size_t i = 0; i < 11; ++i)
    if (!carrier(d, c, l, r, i)) return false;
  c.projection_complete = true;
  return true;
}
[[gnu::noinline]] auto reset(C& c) -> void {
  c = C{};
}
} // namespace

auto detail::intermediate_endpoint01_refuse(D& d, R& r, W w) -> bool {
  r.condition = r.predicate_condition = w;
  if (!d.first_refusal) d.first_refusal = r;
  const bool hard =
      cap(w) || w == W::unsupported_arithmetic || w == W::invalid_binding;
  if (hard) {
    d.stop_condition = w;
    d.stop_stage = r.self_stage;
    d.stop_operation = r.operation;
    d.stop_self_pair = r.self_pair;
    d.stop_self_region = r.self_region;
    d.stop_self_axis = r.self_axis;
    d.stop_self_sign = r.self_sign;
    d.state = cap(w)                           ? S::capacity
              : w == W::unsupported_arithmetic ? S::unsupported
                                               : S::unresolved;
    if (w == W::unsupported_arithmetic) d.arithmetic_supported = false;
  } else if (d.state != S::capacity && d.state != S::unsupported) {
    d.state = S::unresolved;
    if (w != W::sole_disk && w != W::source_disk) {
      d.stop_condition = w;
      d.stop_stage = r.self_stage;
      d.stop_operation = r.operation;
      d.stop_self_pair = r.self_pair;
      d.stop_self_region = r.self_region;
      d.stop_self_axis = r.self_axis;
      d.stop_self_sign = r.self_sign;
    }
  }
  return false;
}
auto detail::intermediate_endpoint01_charge(D& d, R& r, std::uint64_t& work,
                                            std::size_t limit, W why) -> bool {
  if (work >= limit) return intermediate_endpoint01_refuse(d, r, why);
  ++work;
  return true;
}
auto detail::intermediate_endpoint01_definition_charge(D& d, C& c, const L& l,
                                                       R& r) -> bool {
  r = R{};
  r.self_stage = Stage::definition;
  r.operation = static_cast<std::uint8_t>(d.work.definition_guards);
  if (!intermediate_endpoint01_charge(d, r, d.work.definition_guards,
                                      l.definition_guards,
                                      W::definition_capacity))
    return false;
  c.definition_evaluated |= std::uint32_t{1} << r.operation;
  return true;
}
[[gnu::noinline]] auto detail::intermediate_endpoint01_request(
    BoardingIntermediateEndpoint01Candidate candidate)
    -> std::optional<Request> {
  if (candidate !=
      BoardingIntermediateEndpoint01Candidate::authored_descent_midpoint)
    return {};
  auto r = intermediate_pause_support02_request();
  if (r)
    for (auto& root : r->root)
      root.coordinates[1] = {{.847, .012, (-.359) / 2.0}, 3};
  return r;
}
auto detail::intermediate_endpoint01_source_request_valid(const Request& r)
    -> bool {
  return boarding_route_foot_phase_request_valid(r).has_value();
}
auto detail::intermediate_endpoint01_current_cell(
    const BoardingIntermediateEndpoint01AdmissionContext& context, D& d, C& c,
    const L& l, R& r) -> S {
  if (context.owner() != &d || !context.request() ||
      context.parts() != &d.parts || context.data() != A::data(d.source) ||
      !A::valid(d.source) || d.cells.size() != 1 || d.cells.capacity() != 1 ||
      &d.cells[0] != &c || !d.source_enrolled) {
    r = R{};
    r.self_stage = Stage::phase;
    intermediate_endpoint01_refuse(d, r, W::invalid_binding);
    return d.state;
  }
  BoardingRouteFootPhaseLimits phase_limits;
  phase_limits.graphs = l.phase.graphs;
  phase_limits.legs = l.phase.legs;
  phase_limits.bodies = l.phase.bodies;
  phase_limits.sectors = l.phase.sectors;
  phase_limits.timing = l.phase.timing;
  BoardingRouteFootPhaseRefusal original;
  ++d.work.phase_calls;
  const auto phase = boarding_route_foot_phase_cell(
      *context.request(), 0, 1, false, phase_limits, d.work.phase, c.phase,
      original);
  if (!intermediate_endpoint01_definition_charge(d, c, l, r)) return d.state;
  if (phase != BoardingRouteFootPhaseCellResult::accepted) {
    r.self_stage = Stage::phase;
    r.operation = 255;
    r.phase = original;
    intermediate_endpoint01_refuse(d, r, W::phase_prerequisite);
    if (phase == BoardingRouteFootPhaseCellResult::capacity)
      d.state = S::capacity;
    if (phase == BoardingRouteFootPhaseCellResult::unsupported) {
      d.state = S::unsupported;
      d.arithmetic_supported = false;
    }
    d.stop_condition = W::phase_prerequisite;
    d.stop_stage = Stage::phase;
    return d.state;
  }
  c.arithmetic_supported = c.kinematic_complete = true;
  // P01 is a charged revalidation of this actual fresh call, not the earlier
  // precompiler admission check or a caller-written accepted flag.
  if (!projection_charge(d, l, r)) return d.state;
  if (context.owner() != &d || !context.request() ||
      context.parts() != &d.parts || !A::valid(d.source) || !context.data() ||
      context.data() != A::data(d.source) || d.cells.size() != 1 ||
      d.cells.capacity() != 1 || &d.cells[0] != &c || d.work.phase_calls != 1 ||
      !d.source_enrolled) {
    intermediate_endpoint01_refuse(d, r, W::projection_identity);
    return d.state;
  }
  if (!projection(d, c, *context.request(), l, r)) return d.state;
  const BoardingIntermediateEndpoint01CurrentToken token(context, d, c,
                                                         *context.request());
  const auto pressure =
      intermediate_endpoint01_pressure_bridge(token, d, c, l, r);
  if (pressure != S::accepted || !c.nominal_support_complete) return d.state;
  c.complete = false;
  return intermediate_endpoint01_self_bridge(token, d, c, l, r);
}
[[gnu::noinline]] auto detail::intermediate_endpoint01_bounded(
    const OriginBoardingIntermediatePauseSupport& provider,
    BoardingIntermediateEndpoint01Candidate candidate, L limits) -> E {
  if (candidate !=
      BoardingIntermediateEndpoint01Candidate::authored_descent_midpoint)
    return std::unexpected("Endpoint01 invalid candidate");
  if (!valid_limits(limits))
    return std::unexpected("Endpoint01 lowered caps required");
  if (limits.output_bytes < fixed_output)
    return std::unexpected("Endpoint01 fixed output capacity refused");
  if (!boarding_route_foot_phase_environment())
    return std::unexpected("Endpoint01 unsupported FP environment");
  E result(std::in_place, provider);
  auto& d = *result;
  d.candidate = candidate;
  d.output_capacity_bytes = fixed_output;
  Request request;
  R reason;
  if (!enroll(d, request, limits, reason)) return result;
  std::uint32_t definition_prefix{};
  for (std::uint8_t row = 0; row < 3; ++row) {
    reason = R{};
    reason.self_stage = Stage::definition;
    reason.operation = row;
    if (!intermediate_endpoint01_charge(d, reason, d.work.definition_guards,
                                        limits.definition_guards,
                                        W::definition_capacity))
      return result;
    definition_prefix |= std::uint32_t{1} << row;
    // The D mask belongs to the actual Cell; accumulate this charged prefix
    // without creating a full temporary Cell before the output room check.
    const bool good = row == 0 ? A::valid(d.source) && A::valid(provider) &&
                                     A::data(d.source) != nullptr &&
                                     A::data(d.source) == A::data(provider)
                      : row == 1
                          ? boarding_route_foot_phase_environment()
                          : request.seconds_per_parameter == 2 &&
                                request.port_reaction_fraction ==
                                    std::array<double, 2>{.0625, .0625} &&
                                d.source_enrolled &&
                                d.source_evaluated ==
                                    std::numeric_limits<std::uint64_t>::max() &&
                                d.version == 1 &&
                                d.candidate ==
                                    BoardingIntermediateEndpoint01Candidate::
                                        authored_descent_midpoint;
    if (!good) {
      intermediate_endpoint01_refuse(d, reason,
                                     row == 0   ? W::invalid_binding
                                     : row == 1 ? W::unsupported_arithmetic
                                                : W::phase_prerequisite);
      return result;
    }
  }
  if (limits.output_bytes - fixed_output < sizeof(C)) {
    reason = R{};
    intermediate_endpoint01_refuse(d, reason, W::output_capacity);
    return result;
  }
  try {
    d.cells.reserve(1);
  } catch (const std::bad_alloc&) {
    reason = R{};
    intermediate_endpoint01_refuse(d, reason, W::output_capacity);
    return result;
  }
  if (d.cells.capacity() != 1) {
    std::vector<C>().swap(d.cells);
    reason = R{};
    intermediate_endpoint01_refuse(d, reason, W::output_capacity);
    return result;
  }
  d.output_capacity_bytes = fixed_output + sizeof(C);
  d.cells.emplace_back();
  reset(d.cells.front());
  auto& cell = d.cells.front();
  cell.definition_evaluated = definition_prefix;
  const BoardingIntermediateEndpoint01AdmissionContext context(d, request);
  const auto status =
      intermediate_endpoint01_current_cell(context, d, cell, limits, reason);
  cell.state = status;
  cell.arithmetic_supported = d.arithmetic_supported;
  d.kinematic_complete = cell.kinematic_complete;
  d.constant_state = cell.constant_state;
  d.projection_complete = cell.projection_complete;
  d.plane_identities = cell.plane_identities;
  d.nominal_equilibrium = cell.nominal_equilibrium;
  d.finite_contact_supported = cell.finite_contact_supported;
  d.nominal_load_supported = cell.nominal_load_supported;
  d.nominal_support_complete = cell.nominal_support_complete;
  d.self_body_complete = cell.self_body_complete;
  d.self_complete = cell.self_complete;
  d.complete = cell.complete && cell.self_complete &&
               cell.nominal_support_complete && status == S::accepted;
  d.state = d.complete ? S::accepted : status;
  return result;
}
auto assess_origin_boarding_intermediate_endpoint01(
    const OriginBoardingIntermediatePauseSupport& source,
    BoardingIntermediateEndpoint01Candidate candidate) -> E {
  return detail::intermediate_endpoint01_bounded(source, candidate);
}
} // namespace apsis_drift
