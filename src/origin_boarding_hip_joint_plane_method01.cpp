#include "origin_boarding_hip_joint_plane_method01_internal.hpp"
#include "origin_boarding_route_foot_phase_internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>
namespace apsis_drift {
namespace {
using D = BoardingHipJointPlaneMethod01Diagnostic;
using Data = BoardingHipJointPlaneMethod01Data;
using State = BoardingHipJointPlaneMethod01State;
using Outcome = BoardingHipJointPlaneMethod01Outcome;
using Condition = BoardingHipJointPlaneMethod01Condition;
using Stage = BoardingHipJointPlaneMethod01Stage;
using Bound = BoardingFootSiteScalarBounds;
using Limits = detail::BoardingHipJointPlaneMethod01Limits;
using Context = detail::BoardingHipJointPlaneMethod01FreshCallContext;
struct Interval {
  double low{}, high{};
  bool supported{};
};
using Point = std::array<Interval, 3>;
auto valid(Interval a) -> bool {
  constexpr auto largest = std::numeric_limits<double>::max();
  return a.supported && std::isfinite(a.low) && std::isfinite(a.high) &&
         a.low <= a.high && a.low > -largest && a.high < largest;
}
auto point(double x) -> Interval {
  return {x, x, std::isfinite(x)};
}
auto read(const Bound& b) -> Interval {
  return {b.lower, b.upper, b.supported};
}
auto write(Interval a) -> Bound {
  return {a.low, a.high, a.supported};
}
auto zero(Interval a) -> bool {
  return a.low == 0 && a.high == 0;
}
auto down(double x) -> double {
  return std::nextafter(x, -std::numeric_limits<double>::infinity());
}
auto up(double x) -> double {
  return std::nextafter(x, std::numeric_limits<double>::infinity());
}
auto add(Interval a, Interval b) -> Interval {
  if (!valid(a) || !valid(b)) return {};
  if (zero(a) && zero(b)) return point(0);
  if (zero(a)) return b;
  if (zero(b)) return a;
  return {down(a.low + b.low), up(a.high + b.high), true};
}
auto sub(Interval a, Interval b) -> Interval {
  if (!valid(a) || !valid(b)) return {};
  if (zero(a) && zero(b)) return point(0);
  if (zero(b)) return a;
  if (zero(a)) return {-b.high, -b.low, true};
  return {down(a.low - b.high), up(a.high - b.low), true};
}
auto mul(Interval a, Interval b) -> Interval {
  if (!valid(a) || !valid(b)) return {};
  if (zero(a) || zero(b)) return point(0);
  const double ll = a.low * b.low, lh = a.low * b.high, hl = a.high * b.low,
               hh = a.high * b.high;
  if (!std::isfinite(ll) || !std::isfinite(lh) || !std::isfinite(hl) ||
      !std::isfinite(hh))
    return {};
  return {down(std::min(std::min(std::min(ll, lh), hl), hh)),
          up(std::max(std::max(std::max(ll, lh), hl), hh)), true};
}
auto square(Interval a) -> Interval {
  if (!valid(a)) return {};
  if (zero(a)) return point(0);
  const double l = a.low * a.low, h = a.high * a.high;
  if (!std::isfinite(l) || !std::isfinite(h)) return {};
  return {a.low <= 0 && a.high >= 0 ? 0 : down(std::min(l, h)),
          up(std::max(l, h)), true};
}
auto divide(Interval a, Interval b) -> Interval {
  if (!valid(a) || !valid(b) || (b.low <= 0 && b.high >= 0)) return {};
  return mul(a, {down(1 / b.high), up(1 / b.low), true});
}
auto absolute(Interval a) -> Interval {
  if (!valid(a)) return {};
  if (a.low >= 0) return a;
  if (a.high <= 0) return {-a.high, -a.low, true};
  return {0, std::max(-a.low, a.high), true};
}
auto minimum(Interval a, Interval b) -> Interval {
  if (!valid(a) || !valid(b)) return {};
  return {std::min(a.low, b.low), std::min(a.high, b.high), true};
}
auto maximum(Interval a, Interval b) -> Interval {
  if (!valid(a) || !valid(b)) return {};
  return {std::max(a.low, b.low), std::max(a.high, b.high), true};
}
auto clamp(Interval a) -> Interval {
  if (!valid(a)) return {};
  return {std::clamp(a.low, 0.0, 1.0), std::clamp(a.high, 0.0, 1.0), true};
}
auto component(const RigidVector3& p, std::size_t k) -> double {
  return k == 0 ? p.x : k == 1 ? p.y : p.z;
}
auto component(const BoardingPlantedLegPointBounds& p, std::size_t k)
    -> Interval {
  return {component(p.lower, k), component(p.upper, k), true};
}
auto point_valid(const BoardingPlantedLegPointBounds& p) -> bool {
  return valid(component(p, 0)) && valid(component(p, 1)) &&
         valid(component(p, 2));
}
auto vertex(const Data& p, std::uint8_t i, std::size_t k) -> Interval {
  const double h = component(p.pelvis_half_size_metres, k);
  return point((i & (std::uint8_t{1} << k)) != 0 ? h : -h);
}
void stop(D& d, State s, Condition c) {
  d.state = s;
  d.condition = c;
  if (s == State::unsupported) d.arithmetic_supported = false;
}
void row(D& d, Stage stage, std::uint16_t index) {
  d.stage = stage;
  d.operation_stage = stage;
  d.operation_row = index;
  d.operation_cursor = 0;
  d.operation_attempted = 0;
  d.operation_written = 0;
}
template <class F>
auto operation(D& d, const Limits& l, std::uint8_t slot, Interval& destination,
               F&& deferred) -> bool {
  d.operation_cursor = slot;
  if (d.work.operations >= l.operations) {
    stop(d, State::capacity, Condition::operation_capacity);
    return false;
  }
  ++d.work.operations;
  d.operation_attempted |= std::uint64_t{1} << slot;
  const Interval value = deferred();
  if (!valid(value)) {
    stop(d, State::unsupported, Condition::unsupported_arithmetic);
    return false;
  }
  destination = value;
  d.operation_written |= std::uint64_t{1} << slot;
  return true;
}
template <class F>
auto capture(D& d, const Limits& l, std::uint8_t index, Condition condition,
             F&& deferred) -> bool {
  d.stage = Stage::capture;
  d.capture_cursor = index;
  if (d.work.capture_checks >= l.capture_checks) {
    stop(d, State::capacity, Condition::capture_capacity);
    return false;
  }
  ++d.work.capture_checks;
  d.capture_attempted |= static_cast<std::uint8_t>(std::uint8_t{1} << index);
  const bool result = deferred();
  d.capture_written |= static_cast<std::uint8_t>(std::uint8_t{1} << index);
  if (!result) {
    stop(d,
         condition == Condition::unsupported_arithmetic ? State::unsupported
                                                        : State::unresolved,
         condition);
    return false;
  }
  return true;
}
auto feature_charge(D& d, const Limits& l, std::uint8_t index) -> bool {
  d.feature_cursor = index;
  if (d.work.feature_checks >= l.feature_checks) {
    stop(d, State::capacity, Condition::feature_capacity);
    return false;
  }
  ++d.work.feature_checks;
  d.feature_attempted |= std::uint32_t{1} << index;
  return true;
}
auto hard(const BoardingIntermediateEndpoint04Diagnostic& o) -> bool {
  return o.state == BoardingIntermediateEndpoint04State::capacity ||
         o.state == BoardingIntermediateEndpoint04State::unsupported;
}
auto same(const Context& c, const D& d) -> bool {
  return &c.owner() == &d && c.provider().summary() != nullptr &&
         c.provider().summary() == d.source.summary() &&
         c.original().source.summary() == c.provider().summary();
}
void metadata(D& d, const BoardingIntermediateEndpoint04Diagnostic& o) {
  d.original_available = true;
  d.original_state = o.state;
  d.original_condition = o.stop_condition;
  d.original_stage = o.stop_stage;
  d.original_pair = o.stop_self_pair;
  d.original_region = o.stop_self_region;
  d.original_axis = o.stop_self_axis;
  d.original_sign = o.stop_self_sign;
#define FLAG(bit, value)                                                       \
  if (value) d.original_flags |= std::uint32_t{1} << bit
  FLAG(0, o.arithmetic_supported);
  FLAG(1, o.source_enrolled);
  FLAG(2, o.kinematic_complete);
  FLAG(3, o.constant_state);
  FLAG(4, o.projection_complete);
  FLAG(5, o.plane_identities);
  FLAG(6, o.nominal_equilibrium);
  FLAG(7, o.finite_contact_supported);
  FLAG(8, o.nominal_load_supported);
  FLAG(9, o.nominal_support_complete);
  FLAG(10, o.self_body_complete);
  FLAG(11, o.self_complete);
  FLAG(12, o.complete);
  FLAG(13, o.slice.arithmetic_supported);
  FLAG(14, o.slice.complete);
#undef FLAG
}
auto captures(const Context& c, D& d, Data& p, const Limits& l) -> bool {
  const auto& o = c.original();
  bool source_unavailable = false;
  if (!capture(d, l, 0, Condition::capture_identity, [&] {
        const auto* summary = c.provider().summary();
        if (summary == nullptr || !summary->complete) {
          source_unavailable = true;
          return false;
        }
        if (!same(c, d)) return false;
        p.source_identities = summary->sources;
        p.original_first_refusal = o.first_refusal;
        p.availability |= (1U << 0) | (1U << 3);
        return true;
      })) {
    if (source_unavailable)
      stop(d, State::unresolved, Condition::original_unavailable);
    return false;
  }
  if (!capture(d, l, 1, Condition::unsupported_arithmetic,
               [] { return detail::boarding_route_foot_phase_environment(); }))
    return false;
  if (!capture(d, l, 2, Condition::original_unavailable, [&] {
        if (o.version != 4 || o.self_version != 1 ||
            o.candidate != BoardingIntermediateEndpoint04Candidate::
                               root_y_both_ankle_reach_roll_slice ||
            !o.source_enrolled || o.work.source_guards != 64 ||
            o.source_evaluated != UINT64_MAX || !o.slice.complete ||
            !o.slice.arithmetic_supported || o.work.construction_guards != 37 ||
            o.work.construction_operations != 226 ||
            o.slice.operation_written[0] != UINT64_MAX ||
            o.slice.operation_written[1] != UINT64_MAX ||
            o.slice.operation_written[2] != UINT64_MAX ||
            o.slice.operation_written[3] != 0x3ffffffffULL ||
            o.slice.guard_written != 0x1fffffffffULL ||
            o.reporting_elapsed_seconds != 2)
          return false;
        p.original_work = o.work;
        p.original_slice = o.slice;
        p.availability |= (1U << 1) | (1U << 2);
        return true;
      }))
    return false;
  if (!capture(d, l, 3, Condition::original_unavailable, [&] {
        if (o.cells.size() != 1 || o.cells.capacity() != 1 ||
            o.work.phase_calls != 1 || o.work.phase.graphs != 1 ||
            o.work.phase.legs != 2 || o.work.phase.bodies != 1 ||
            o.work.phase.sectors != 3 || o.work.phase.timing != 6 ||
            !o.kinematic_complete || !o.constant_state ||
            !o.projection_complete || !o.plane_identities ||
            !o.nominal_equilibrium || !o.finite_contact_supported ||
            !o.nominal_load_supported || !o.nominal_support_complete)
          return false;
        const auto& x = o.cells[0];
        const auto& q = x.phase;
        return x.kinematic_complete && x.constant_state &&
               x.projection_complete && x.plane_identities &&
               x.nominal_equilibrium && x.finite_contact_supported &&
               x.nominal_load_supported && x.nominal_support_complete &&
               q.complete && q.arithmetic_supported && q.nominal_links &&
               q.target_sole_identities && q.joint_sectors &&
               q.derivative_domains && q.timing_complete && q.first == 0 &&
               q.last == 1;
      }))
    return false;
  if (!capture(d, l, 4, Condition::original_unavailable, [&] {
        const auto& x = o.cells[0];
        return o.self_body_complete && x.self_body_complete &&
               x.self_body_evaluated == 0x7fffffffffffffffULL &&
               x.unit_axis_attempted == 0xffffffU &&
               x.unit_axis_written == 0xffffffU &&
               x.unit_axis_complete_mask == 15 && x.unit_axis_cursor == 23 &&
               valid(read(x.unit_axes[0][0])) &&
               valid(read(x.unit_axes[0][1])) && valid(read(x.unit_axes[0][2]));
      }))
    return false;
  if (!capture(d, l, 5, Condition::capture_identity, [&] {
        const auto* box =
            std::get_if<BoardingRoutePhaseBoxBinding>(&o.parts[0].reservation);
        const auto* cap = std::get_if<BoardingPlantedBodyCapsuleBinding>(
            &o.parts[3].reservation);
        const auto& q = o.cells[0].phase;
        if (o.parts[0].id != BoardingBodyPartId::pelvis ||
            o.parts[3].id != BoardingBodyPartId::port_thigh || box == nullptr ||
            cap == nullptr || box->center != BoardingPlantedBodyPointId::root ||
            box->frame != BoardingRoutePhaseFrame::root ||
            box->half_size_metres != RigidVector3{.24, .12, .18} ||
            cap->start != BoardingPlantedBodyPointId::port_hip ||
            cap->end != BoardingPlantedBodyPointId::port_knee ||
            cap->radius_metres != .105 ||
            !(0 < .14 && 0 < .47285 && 0 < .105 &&
              .105 <= kBoardingSelfHipLengthMetres &&
              kBoardingSelfHipLengthMetres < .47285) ||
            !point_valid(q.points[0].value) ||
            !point_valid(q.points[4].value) ||
            !point_valid(q.points[5].value) ||
            !point_valid(q.frames[0].columns[0].value) ||
            !point_valid(q.frames[0].columns[1].value) ||
            !point_valid(q.frames[0].columns[2].value))
          return false;
        // C03/C04 authenticate the unchanged generated nominal chart, including
        // the proper root frame and H = ROOT - .14*RX; enclosure orthogonality
        // is not assumed.
        p.selected_parts[0] = o.parts[0];
        p.selected_parts[1] = o.parts[3];
        p.pelvis_half_size_metres = box->half_size_metres;
        p.hip_offset_metres = .14;
        p.thigh_length_metres = .47285;
        p.radius_metres = cap->radius_metres;
        p.axial_limit_metres = kBoardingSelfHipLengthMetres;
        p.points[0] = q.points[0].value;
        p.points[1] = q.points[4].value;
        p.points[2] = q.points[5].value;
        for (std::size_t k = 0; k < 3; ++k) {
          p.root_columns[k] = q.frames[0].columns[k].value;
          p.unit_axis[k] = o.cells[0].unit_axes[0][k];
        }
        p.availability |= (1U << 4) | (1U << 5) | (1U << 6) | (1U << 7);
        return true;
      }))
    return false;
  return capture(d, l, 6, Condition::original_unavailable, [&] {
    return same(c, d) && d.capture_written == 63 && !hard(o) &&
           (o.state == BoardingIntermediateEndpoint04State::accepted ||
            o.state == BoardingIntermediateEndpoint04State::unresolved ||
            o.state == BoardingIntermediateEndpoint04State::witness_refused);
  });
}
// Every destination belongs to the single named fixed helper or owned payload.
// No Point/Data aggregate is returned by these routines.
auto fold(D& d, Data& p, const Limits& l, Interval& candidate,
          std::uint8_t slot, std::uint8_t kind, std::uint16_t index) -> bool {
  Interval result;
  if (!operation(d, l, slot, result, [&] {
        const Interval previous = read(p.minimum_distance_squared);
        if (!previous.supported) {
          p.minimum_lower_kind = kind;
          p.minimum_upper_kind = kind;
          p.minimum_lower_index = index;
          p.minimum_upper_index = index;
          return candidate;
        }
        if (candidate.low < previous.low) {
          p.minimum_lower_kind = kind;
          p.minimum_lower_index = index;
        }
        if (candidate.high < previous.high) {
          p.minimum_upper_kind = kind;
          p.minimum_upper_index = index;
        }
        return minimum(previous, candidate);
      }))
    return false;
  p.minimum_distance_squared = write(result);
  return true;
}
auto point_distance(D& d, Data& p, const Limits& l, const Point& j,
                    const Point& v, std::uint8_t index,
                    std::array<Interval, 20>& w) -> bool {
  row(d, Stage::vertex_distance, index);
  for (std::uint8_t k = 0; k < 3; ++k)
    if (!operation(d, l, k, w[k], [&] { return sub(j[k], v[k]); }))
      return false;
  for (std::uint8_t k = 0; k < 3; ++k)
    if (!operation(d, l, static_cast<std::uint8_t>(3 + k), w[3 + k],
                   [&] { return square(w[k]); }))
      return false;
  if (!operation(d, l, 6, w[6], [&] { return add(w[3], w[4]); }) ||
      !operation(d, l, 7, w[7], [&] { return add(w[6], w[5]); }) ||
      !fold(d, p, l, w[7], 8, 0, index))
    return false;
  p.feature_distances[index] = write(w[7]);
  ++p.feature_distance_count;
  return true;
}
void append(Data& p, const Point& v, std::uint8_t source, std::uint8_t kind,
            std::uint8_t a, std::uint8_t b) {
  auto& f = p.features[p.feature_count];
  for (std::size_t k = 0; k < 3; ++k)
    f.point[k] = write(v[k]);
  f.source_feature = source;
  f.kind = kind;
  f.endpoints = {a, b};
  f.ready = true;
  ++p.feature_count;
}
auto finish(const Context& c, D& d, Data& p, const Limits& l,
            const Point& abs_j) -> bool {
  if (!capture(d, l, 7, Condition::evidence_identity, [&] {
        if (!same(c, d) || d.capture_written != 127 || hard(c.original()))
          return false;
        const Interval extent = read(p.owner_extent);
        if (!valid(extent)) return false;
        if (p.owner_branch) {
          if (!(extent.high <= p.axial_limit_metres) ||
              (p.availability & (1U << 10)) == 0)
            return false;
          d.outcome = Outcome::contained;
          return true;
        }
        if (!(extent.low > p.axial_limit_metres) || !p.minimum_complete ||
            (p.availability & ((1U << 9) | (1U << 11) | (1U << 12))) !=
                ((1U << 9) | (1U << 11) | (1U << 12)) ||
            !valid(read(p.radius_squared)) ||
            !valid(read(p.minimum_distance_squared)))
          return false;
        if (p.center_branch) {
          if (read(p.radius_squared).low <= 0 ||
              !zero(read(p.minimum_distance_squared)))
            return false;
          for (std::size_t k = 0; k < 3; ++k)
            if (abs_j[k].high > component(p.pelvis_half_size_metres, k))
              return false;
        } else {
          if (!p.feature_branch || d.feature_written != 0xfffffU ||
              p.feature_count < 3 || p.feature_count > 20 ||
              p.feature_distance_count != p.feature_count ||
              p.chord_distance_count !=
                  std::size_t{p.feature_count} * (p.feature_count - 1) / 2 ||
              d.work.chord_checks != p.chord_distance_count)
            return false;
          bool outside = false;
          for (std::size_t k = 0; k < 3; ++k)
            outside |= abs_j[k].low > component(p.pelvis_half_size_metres, k);
          if (!outside) return false;
          for (std::size_t i = 0; i < p.feature_count; ++i)
            if (!p.features[i].ready || !valid(read(p.feature_distances[i])) ||
                !valid(read(p.features[i].point[0])) ||
                !valid(read(p.features[i].point[1])) ||
                !valid(read(p.features[i].point[2])))
              return false;
          for (std::size_t i = 0; i < p.chord_distance_count; ++i)
            if (!valid(read(p.chord_distances[i]))) return false;
        }
        const Interval m = read(p.minimum_distance_squared),
                       r = read(p.radius_squared);
        d.outcome = m.high < r.low    ? Outcome::strict_unowned_exists
                    : m.low >= r.high ? Outcome::contained
                                      : Outcome::unresolved;
        return true;
      }))
    return false;
  d.stage = Stage::complete;
  d.state = State::evidence_complete;
  d.evidence_complete = true;
  d.condition = d.outcome == Outcome::unresolved
                    ? Condition::classification_unresolved
                    : Condition::none;
  return true;
}
auto geometry(const Context& c, D& d, Data& p, const Limits& l) -> bool {
  std::array<Point, 20> v{};
  std::array<Interval, 8> t{}, f{};
  Point xi{}, j{}, abs_j{};
  std::array<Interval, 20> w{};
  std::array<std::uint8_t, 8>
      signs{}; // negative0, positive1, exact-zero2, unknown3
  row(d, Stage::chart, 0);
  for (std::uint8_t k = 0; k < 3; ++k) {
    for (std::uint8_t m = 0; m < 3; ++m)
      if (!operation(d, l, static_cast<std::uint8_t>(5 * k + m), w[m], [&] {
            return mul(component(p.root_columns[k], m), read(p.unit_axis[m]));
          }))
        return false;
    if (!operation(d, l, static_cast<std::uint8_t>(5 * k + 3), w[3],
                   [&] { return add(w[0], w[1]); }) ||
        !operation(d, l, static_cast<std::uint8_t>(5 * k + 4), xi[k],
                   [&] { return add(w[3], w[2]); }))
      return false;
    p.xi[k] = write(xi[k]);
  }
  p.availability |= 1U << 8;
  d.arithmetic_supported = true;
  for (std::uint8_t i = 0; i < 8; ++i) {
    d.stage = Stage::vertex;
    if (!feature_charge(d, l, i)) return false;
    row(d, Stage::vertex, i);
    if (!operation(d, l, 0, w[0],
                   [&] {
                     return sub(vertex(p, i, 0), point(-p.hip_offset_metres));
                   }) ||
        !operation(d, l, 1, w[1], [&] { return mul(w[0], xi[0]); }) ||
        !operation(d, l, 2, w[2],
                   [&] { return mul(vertex(p, i, 1), xi[1]); }) ||
        !operation(d, l, 3, w[3],
                   [&] { return mul(vertex(p, i, 2), xi[2]); }) ||
        !operation(d, l, 4, w[4], [&] { return add(w[1], w[2]); }) ||
        !operation(d, l, 5, t[i], [&] { return add(w[4], w[3]); }) ||
        !operation(d, l, 6, f[i],
                   [&] { return sub(t[i], point(p.axial_limit_metres)); }) ||
        !operation(d, l, 7, w[5], [&] {
          return i == 0 ? t[i] : maximum(read(p.owner_extent), t[i]);
        }))
      return false;
    p.owner_extent = write(w[5]);
    signs[i] = f[i].high < 0 ? 0 : f[i].low > 0 ? 1 : zero(f[i]) ? 2 : 3;
    d.feature_written |= std::uint32_t{1} << i;
  }
  p.availability |= 1U << 10;
  d.stage = Stage::plane_gate;
  if (read(p.owner_extent).high <= p.axial_limit_metres) {
    p.owner_branch = true;
    return finish(c, d, p, l, abs_j);
  }
  if (!(read(p.owner_extent).low > p.axial_limit_metres)) {
    stop(d, State::unresolved, Condition::topology_unresolved);
    return false;
  }
  for (std::uint8_t i = 0; i < 8; ++i)
    if (signs[i] == 3) {
      stop(d, State::unresolved, Condition::topology_unresolved);
      return false;
    }
  row(d, Stage::center, 0);
  if (!operation(d, l, 0, w[0], [&] { return square(point(p.radius_metres)); }))
    return false;
  p.radius_squared = write(w[0]);
  p.availability |= 1U << 11;
  if (!operation(d, l, 1, w[1],
                 [&] { return mul(point(p.axial_limit_metres), xi[0]); }) ||
      !operation(d, l, 2, j[0],
                 [&] { return add(w[1], point(-p.hip_offset_metres)); }) ||
      !operation(d, l, 3, j[1],
                 [&] { return mul(point(p.axial_limit_metres), xi[1]); }) ||
      !operation(d, l, 4, j[2],
                 [&] { return mul(point(p.axial_limit_metres), xi[2]); }))
    return false;
  for (std::uint8_t k = 0; k < 3; ++k) {
    if (!operation(d, l, static_cast<std::uint8_t>(5 + k), abs_j[k],
                   [&] { return absolute(j[k]); }))
      return false;
    p.center[k] = write(j[k]);
  }
  p.availability |= 1U << 9;
  if (abs_j[0].high <= p.pelvis_half_size_metres.x &&
      abs_j[1].high <= p.pelvis_half_size_metres.y &&
      abs_j[2].high <= p.pelvis_half_size_metres.z) {
    p.center_branch = true;
    row(d, Stage::center, 1);
    if (!operation(d, l, 0, w[0], [] { return point(0); })) return false;
    p.minimum_distance_squared = write(w[0]);
    p.minimum_complete = true;
    p.availability |= 1U << 12;
    return finish(c, d, p, l, abs_j);
  }
  if (!(abs_j[0].low > p.pelvis_half_size_metres.x ||
        abs_j[1].low > p.pelvis_half_size_metres.y ||
        abs_j[2].low > p.pelvis_half_size_metres.z)) {
    stop(d, State::unresolved, Condition::classification_unresolved);
    return false;
  }
  for (std::uint8_t i = 0; i < 8; ++i)
    if (signs[i] == 2) {
      const std::uint8_t n = p.feature_count;
      for (std::size_t k = 0; k < 3; ++k)
        v[n][k] = vertex(p, i, k);
      append(p, v[n], i, 0, i, i);
      if (!point_distance(d, p, l, j, v[n], n, w)) return false;
    }
  static constexpr std::array<std::array<std::uint8_t, 2>, 12> edges{{{0, 1},
                                                                      {0, 2},
                                                                      {0, 4},
                                                                      {1, 3},
                                                                      {1, 5},
                                                                      {2, 3},
                                                                      {2, 6},
                                                                      {3, 7},
                                                                      {4, 5},
                                                                      {4, 6},
                                                                      {5, 7},
                                                                      {6, 7}}};
  for (std::uint8_t e = 0; e < 12; ++e) {
    d.stage = Stage::edge;
    if (!feature_charge(d, l, static_cast<std::uint8_t>(8 + e))) return false;
    const std::uint8_t a = edges[e][0], b = edges[e][1];
    if (signs[a] == 2 || signs[b] == 2 || signs[a] == signs[b]) {
      d.feature_written |= std::uint32_t{1} << (8 + e);
      continue;
    }
    row(d, Stage::edge, e);
    if (!operation(d, l, 0, w[0], [&] { return sub(f[a], f[b]); }))
      return false;
    if (w[0].low <= 0 && w[0].high >= 0) {
      stop(d, State::unresolved, Condition::denominator_unresolved);
      return false;
    }
    if (!operation(d, l, 1, w[1], [&] { return divide(f[a], w[0]); }))
      return false;
    if (w[1].low < 0 || w[1].high > 1) {
      stop(d, State::unresolved, Condition::classification_unresolved);
      return false;
    }
    const std::uint8_t n = p.feature_count;
    for (std::uint8_t k = 0; k < 3; ++k)
      if (!operation(d, l, static_cast<std::uint8_t>(2 + k), w[2 + k],
                     [&] { return sub(vertex(p, b, k), vertex(p, a, k)); }))
        return false;
    for (std::uint8_t k = 0; k < 3; ++k)
      if (!operation(d, l, static_cast<std::uint8_t>(5 + k), w[5 + k],
                     [&] { return mul(w[1], w[2 + k]); }))
        return false;
    for (std::uint8_t k = 0; k < 3; ++k)
      if (!operation(d, l, static_cast<std::uint8_t>(8 + k), v[n][k],
                     [&] { return add(vertex(p, a, k), w[5 + k]); }))
        return false;
    append(p, v[n], static_cast<std::uint8_t>(8 + e), 1, a, b);
    d.feature_written |= std::uint32_t{1} << (8 + e);
    if (!point_distance(d, p, l, j, v[n], n, w)) return false;
  }
  if (d.feature_written != 0xfffffU || p.feature_count < 3 ||
      p.feature_count > 20) {
    stop(d, State::unresolved, Condition::topology_unresolved);
    return false;
  }
  p.feature_branch = true;
  std::uint16_t ordinal = 0;
  for (std::uint8_t a = 0; a < p.feature_count; ++a)
    for (std::uint8_t b = static_cast<std::uint8_t>(a + 1); b < p.feature_count;
         ++b) {
      d.stage = Stage::chord;
      d.chord_cursor = ordinal;
      if (d.work.chord_checks >= l.chord_checks) {
        stop(d, State::capacity, Condition::chord_capacity);
        return false;
      }
      ++d.work.chord_checks;
      row(d, Stage::chord, ordinal);
      for (std::uint8_t k = 0; k < 3; ++k)
        if (!operation(d, l, k, w[k], [&] { return sub(v[b][k], v[a][k]); }))
          return false;
      for (std::uint8_t k = 0; k < 3; ++k)
        if (!operation(d, l, static_cast<std::uint8_t>(3 + k), w[3 + k],
                       [&] { return sub(j[k], v[a][k]); }))
          return false;
      for (std::uint8_t k = 0; k < 3; ++k)
        if (!operation(d, l, static_cast<std::uint8_t>(6 + k), w[6 + k],
                       [&] { return square(w[k]); }))
          return false;
      if (!operation(d, l, 9, w[9], [&] { return add(w[6], w[7]); }) ||
          !operation(d, l, 10, w[10], [&] { return add(w[9], w[8]); }))
        return false;
      for (std::uint8_t k = 0; k < 3; ++k)
        if (!operation(d, l, static_cast<std::uint8_t>(11 + k), w[11 + k],
                       [&] { return mul(w[3 + k], w[k]); }))
          return false;
      if (!operation(d, l, 14, w[14], [&] { return add(w[11], w[12]); }) ||
          !operation(d, l, 15, w[15], [&] { return add(w[14], w[13]); }))
        return false;
      for (std::uint8_t k = 0; k < 3; ++k)
        if (!operation(d, l, static_cast<std::uint8_t>(16 + k), w[16 + k],
                       [&] { return square(w[3 + k]); }))
          return false;
      if (!operation(d, l, 19, w[19], [&] { return add(w[16], w[17]); }) ||
          !operation(d, l, 20, w[19], [&] { return add(w[19], w[18]); }))
        return false;
      if (w[10].high == 0) {
        if (!operation(d, l, 21, w[19], [&] { return w[19]; })) return false;
      } else {
        if (!(w[10].low > 0)) {
          stop(d, State::unresolved, Condition::denominator_unresolved);
          return false;
        }
        if (!operation(d, l, 21, w[11], [&] { return divide(w[15], w[10]); }) ||
            !operation(d, l, 22, w[12], [&] { return clamp(w[11]); }))
          return false;
        for (std::uint8_t k = 0; k < 3; ++k)
          if (!operation(d, l, static_cast<std::uint8_t>(23 + k), w[13 + k],
                         [&] { return mul(w[12], w[k]); }))
            return false;
        for (std::uint8_t k = 0; k < 3; ++k)
          if (!operation(d, l, static_cast<std::uint8_t>(26 + k), w[16 + k],
                         [&] { return sub(w[3 + k], w[13 + k]); }))
            return false;
        for (std::uint8_t k = 0; k < 3; ++k)
          if (!operation(d, l, static_cast<std::uint8_t>(29 + k), w[13 + k],
                         [&] { return square(w[16 + k]); }))
            return false;
        if (!operation(d, l, 32, w[16], [&] { return add(w[13], w[14]); }) ||
            !operation(d, l, 33, w[19], [&] { return add(w[16], w[15]); }))
          return false;
      }
      if (!fold(d, p, l, w[19], 34, 1, ordinal)) return false;
      p.chord_distances[ordinal] = write(w[19]);
      ++p.chord_distance_count;
      ++ordinal;
    }
  p.minimum_complete = true;
  p.availability |= 1U << 12;
  d.stage = Stage::classification;
  return finish(c, d, p, l, abs_j);
}
} // namespace
namespace detail {
[[gnu::noinline]] auto hip_joint_plane_method01_bounded(
    const OriginBoardingIntermediatePauseSupport& provider,
    BoardingHipJointPlaneMethod01Limits limits)
    -> BoardingHipJointPlaneMethod01Expected {
  if (!boarding_route_foot_phase_environment())
    return std::unexpected("hip joint plane01 unsupported floating point");
  const Limits ceiling;
  if (limits.capture_checks > ceiling.capture_checks ||
      limits.feature_checks > ceiling.feature_checks ||
      limits.chord_checks > ceiling.chord_checks ||
      limits.operations > ceiling.operations ||
      limits.output_bytes > ceiling.output_bytes)
    return std::unexpected("hip joint plane01 invalid limits");
  if (limits.output_bytes < 2 * sizeof(BoardingHipJointPlaneMethod01Expected))
    return std::unexpected("hip joint plane01 output headers");
  BoardingHipJointPlaneMethod01Expected result(std::in_place, provider);
  auto& d = *result;
  d.work.preflight_guards = 1;
  d.required_output_bytes =
      boarding_hip_joint_plane_method01_required_output_bytes();
  d.output_capacity_bytes = 2 * sizeof(BoardingHipJointPlaneMethod01Expected);
  if (limits.output_bytes < d.required_output_bytes) {
    stop(d, State::capacity, Condition::output_capacity);
    return result;
  }
  d.stage = Stage::original_call;
  ++d.work.endpoint_calls;
  auto original = assess_origin_boarding_intermediate_endpoint04(
      provider, BoardingIntermediateEndpoint04Candidate::
                    root_y_both_ankle_reach_roll_slice);
  ++d.work.return_metadata_checks;
  if (!original) return std::unexpected(std::move(original.error()));
  metadata(d, *original);
  if (hard(*original)) {
    d.state = original->state == BoardingIntermediateEndpoint04State::capacity
                  ? State::capacity
                  : State::unsupported;
    d.condition = Condition::original_unavailable;
    return result;
  }
  // Lexical private issuer: no exported old-report input, no context during old
  // graph.
  const auto& old = *original;
  auto postcall = [&provider, &old, &d, &limits] [[gnu::noinline]] () {
    const BoardingHipJointPlaneMethod01FreshCallContext context(old, d,
                                                                provider);
    Data* mutable_data = nullptr;
    try {
      d.data.reset(mutable_data = new Data{});
    } catch (const std::bad_alloc&) {
      stop(d, State::capacity, Condition::payload_allocation);
      return;
    }
    d.output_capacity_bytes += sizeof(Data);
    if (!captures(context, d, *mutable_data, limits)) return;
    static_cast<void>(geometry(context, d, *mutable_data, limits));
  };
  postcall();
  return result;
}
} // namespace detail
auto assess_origin_boarding_hip_joint_plane_method01(
    const OriginBoardingIntermediatePauseSupport& provider)
    -> BoardingHipJointPlaneMethod01Expected {
  return detail::hip_joint_plane_method01_bounded(provider);
}
} // namespace apsis_drift
