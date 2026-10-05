#include "apsis_drift/origin_boarding_planted_legs.hpp"
#include "origin_boarding_planted_legs_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#if defined(__SSE2__) && defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace {
using namespace apsis_drift;
std::size_t checks{};
int failures{};
auto check(bool value, std::string_view label) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
template <class T, class E> auto require(std::expected<T, E> value) -> T {
  if (!value)
    throw std::runtime_error("Required finite hip preflight API refused");
  return std::move(*value);
}
auto bits(double a, double b) -> bool {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
struct Point {
  long double x{}, y{}, z{};
};
auto add(Point a, Point b) -> Point {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(Point a, Point b) -> Point {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(Point p, long double s) -> Point {
  return {p.x * s, p.y * s, p.z * s};
}
auto dot(Point a, Point b) -> long double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
auto component(RigidVector3 p, std::size_t axis) -> double {
  return axis == 0 ? p.x : (axis == 1 ? p.y : p.z);
}
auto component(Point p, std::size_t axis) -> long double {
  return axis == 0 ? p.x : (axis == 1 ? p.y : p.z);
}
auto valid(BoardingPlantedLegScalarBounds b) -> bool {
  return std::isfinite(b.lower) && std::isfinite(b.upper) && b.lower <= b.upper;
}
auto contains(BoardingPlantedLegScalarBounds b, long double value) -> void {
  // Only corroborating a sampled analytic oracle, never granting a leaf. This
  // allowance bounds LD oracle arithmetic; production bounds remain unchanged.
  const auto error = 8192 * std::numeric_limits<long double>::epsilon() *
                     std::max(1.L, std::abs(value));
  check(valid(b) && static_cast<long double>(b.lower) <= value + error &&
            value - error <= static_cast<long double>(b.upper),
        "Independent unfactored hip geometry belongs to outward enclosure");
}
auto point_contains(const BoardingPlantedLegPointBounds& b, Point value)
    -> void {
  for (std::size_t axis = 0; axis < 3; ++axis)
    contains({component(b.lower, axis), component(b.upper, axis)},
             component(value, axis));
}
// Field-wise snapshot includes all runtime old fields; padding is never read.
struct Snapshot {
  std::vector<std::uint64_t> values;
  auto number(double n) -> void {
    values.push_back(std::bit_cast<std::uint64_t>(n));
  }
  template <class T> auto integer(T n) -> void {
    values.push_back(static_cast<std::uint64_t>(n));
  }
  auto scalar(BoardingPlantedLegScalarBounds b) -> void {
    number(b.lower);
    number(b.upper);
  }
  auto point(const BoardingPlantedLegPointBounds& b) -> void {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      number(component(b.lower, axis));
      number(component(b.upper, axis));
    }
  }
  auto derivatives(const BoardingPlantedLegPointDerivatives& e) -> void {
    point(e.velocity);
    point(e.acceleration);
  }
  auto closure(const BoardingPlantedLegEvidence& e) -> void {
    for (const auto& p : std::array{e.hip, e.knee, e.ankle, e.boot_center})
      point(p);
    for (const auto& b :
         std::array{e.distance_squared, e.rho, e.alpha, e.gamma, e.hip_sine,
                    e.hip_cosine, e.shin_sine, e.shin_cosine, e.knee_cosine,
                    e.roll_sine, e.roll_cosine})
      scalar(b);
    for (const auto x : e.ankle_y_terms)
      number(x);
    for (const auto x : e.boot_center_y_terms)
      number(x);
    number(e.plane_metres);
    number(e.reporting_hip_flex_degrees);
    number(e.reporting_knee_flex_degrees);
    number(e.reporting_ankle_pitch_degrees);
    number(e.reporting_hip_abduction_degrees);
    integer(e.plane_identity);
    integer(e.link_identities);
    integer(e.limits_certified);
    integer(e.limiting_condition);
  }
  auto closure(const BoardingPlantedLegDiagnostic& d) -> void {
    integer(d.recipe_version);
    number(d.requested_first);
    number(d.requested_last);
    number(d.duration_seconds);
    number(d.placement_y_metres);
    integer(d.reverse);
    integer(d.complete);
    integer(d.plane_identities);
    integer(d.link_identities);
    integer(d.joint_limits_certified);
    integer(d.examined_nodes);
    integer(d.maximum_depth);
    integer(d.leaves.size());
    for (const auto& leaf : d.leaves) {
      number(leaf.first);
      number(leaf.last);
      scalar(leaf.root_x);
      for (const auto& e : leaf.legs)
        closure(e);
    }
    integer(d.first_refusal.has_value());
    if (d.first_refusal) {
      const auto& r = *d.first_refusal;
      number(r.first);
      number(r.last);
      integer(r.side);
      integer(r.depth);
      integer(r.condition);
      integer(r.limiting_condition);
    }
  }
  auto timing(const BoardingPlantedLegTimingEvidence& e) -> void {
    closure(e.closure);
    for (const auto& p : std::array{e.hip, e.knee, e.ankle, e.boot_center})
      derivatives(p);
    for (const auto& a :
         std::array{e.hip_pitch, e.shin_pitch, e.knee_flex, e.ankle_pitch,
                    e.hip_abduction, e.ankle_roll}) {
      scalar(a.rate);
      scalar(a.coordinate_second);
    }
    for (const auto& s : std::array{e.hip_speed, e.knee_speed, e.ankle_speed}) {
      scalar(s.speed_radians_per_second);
      integer(s.supported);
      integer(s.certified);
    }
    integer(e.derivative_domains_certified);
    integer(e.joint_speed_certified);
    integer(e.limiting_condition);
    integer(e.limiting_joint);
    scalar(e.limiting_bound);
  }
  auto timing(const BoardingPlantedLegTimingDiagnostic& d) -> void {
    integer(d.timing_version);
    closure(d.closure);
    number(d.requested_first);
    number(d.requested_last);
    number(d.seconds_per_parameter);
    number(d.reporting_elapsed_seconds);
    integer(d.reverse);
    integer(d.complete);
    integer(d.derivative_domains_certified);
    integer(d.root_speed_certified);
    integer(d.root_acceleration_certified);
    integer(d.leg_joint_speed_certified);
    integer(d.examined_nodes);
    integer(d.maximum_depth);
    integer(d.leaves.size());
    for (const auto& leaf : d.leaves) {
      number(leaf.first);
      number(leaf.last);
      derivatives(leaf.root);
      scalar(leaf.root_speed);
      scalar(leaf.root_acceleration);
      for (const auto& e : leaf.legs)
        timing(e);
    }
    integer(d.first_refusal.has_value());
    if (d.first_refusal) {
      const auto& r = *d.first_refusal;
      number(r.first);
      number(r.last);
      integer(r.side);
      integer(r.depth);
      integer(r.condition);
      integer(r.limiting_condition);
      integer(r.joint);
      scalar(r.limiting_bound);
    }
  }
};
auto snapshot(const BoardingPlantedBodyDiagnostic& d)
    -> std::vector<std::uint64_t> {
  Snapshot s;
  s.integer(d.recipe_version);
  s.timing(d.timing);
  s.number(d.common_translation_y_metres);
  s.integer(d.assembled_leaves);
  s.integer(d.complete);
  s.integer(d.reservations_complete);
  s.integer(d.mass_model_complete);
  s.integer(d.com_derivatives_complete);
  for (const auto& p : d.parts) {
    s.integer(p.id);
    s.integer(p.reservation.index());
    if (const auto* b =
            std::get_if<BoardingPlantedBodyBoxBinding>(&p.reservation)) {
      s.integer(b->center);
      for (std::size_t axis = 0; axis < 3; ++axis) {
        s.number(component(b->half_size_metres, axis));
        for (std::size_t row = 0; row < 3; ++row)
          s.number(component(b->frame.columns[axis], row));
      }
    } else {
      const auto& c =
          std::get<BoardingPlantedBodyCapsuleBinding>(p.reservation);
      s.integer(c.start);
      s.integer(c.end);
      s.number(c.radius_metres);
    }
    s.integer(p.mass.first);
    s.integer(p.mass.second);
    s.integer(p.mass.weight);
  }
  s.integer(d.leaves.size());
  const auto evidence = [&](const BoardingPlantedBodyPointEvidence& e) {
    s.point(e.value);
    s.derivatives(e.derivatives);
  };
  for (const auto& leaf : d.leaves) {
    s.number(leaf.first);
    s.number(leaf.last);
    for (const auto& p : leaf.points)
      evidence(p);
    for (const auto& p : leaf.mass_points)
      evidence(p);
    evidence(leaf.center_of_mass);
  }
  s.integer(d.first_refusal.has_value());
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    s.integer(r.condition);
    s.integer(r.leaf_index);
    s.number(r.first);
    s.number(r.last);
  }
  return std::move(s.values);
}
constexpr double registered_hip_limit = 0x1.bb0cd605d7512p-3;
struct Reference {
  // root is canonical; hip/knee are ROOT-relative, so adding root maps
  // them to the owned body chart. Axis/unit/w are translation-invariant.
  Point root, hip, knee, axis, unit, w;
  long double projection{}, radial_squared{}, radius_squared{};
  std::array<long double, 3> pelvis_margins;
};
auto reference(RigidVector3 relative) -> Reference {
  // t0 forward knee, unfactored sphere intersection; no producer IK/report
  // helpers and no normalized legacy frame constructs expected geometry.
  const Point H{-static_cast<long double>(.14), static_cast<long double>(.72),
                static_cast<long double>(-.16)},
      A{static_cast<long double>(.02), static_cast<long double>(.1),
        static_cast<long double>(-.35)};
  const auto d = sub(A, H);
  const auto D = dot(d, d), rho = std::hypot(d.x, d.y);
  const auto L1 = static_cast<long double>(.47285),
             L2 = static_cast<long double>(.47478);
  const auto alpha = (L1 * L1 - L2 * L2 + D) / (2 * D),
             gamma = std::sqrt(L1 * L1 / D - alpha * alpha);
  const Point q{d.x * d.z / rho, d.y * d.z / rho, -rho};
  const auto K = add(H, add(scale(d, alpha), scale(q, gamma)));
  const auto v = sub(K, H), shin = sub(A, K);
  const auto error = 65536 * std::numeric_limits<long double>::epsilon();
  check(std::abs(dot(v, v) - L1 * L1) <= error &&
            std::abs(dot(shin, shin) - L2 * L2) <= error,
        "Independent unfactored forward knee retains exact nominal link "
        "identities");
  const Point root{0, 0, static_cast<long double>(-.16)}, hip{H.x, 0, 0};
  const Point p{static_cast<long double>(relative.x),
                static_cast<long double>(relative.y),
                static_cast<long double>(relative.z)};
  const auto u = scale(v, 1 / L1), w = sub(p, hip);
  const auto s = dot(u, w);
  return {root,
          hip,
          sub(K, add(root, {0, static_cast<long double>(.72), 0})),
          v,
          u,
          w,
          s,
          dot(w, w) - s * s,
          static_cast<long double>(.105) * static_cast<long double>(.105),
          {static_cast<long double>(.24) - std::abs(p.x),
           static_cast<long double>(.12) - std::abs(p.y),
           static_cast<long double>(.18) - std::abs(p.z)}};
}
auto candidate_on_axis(long double fraction) -> RigidVector3 {
  const auto r = reference({});
  const auto p = add(r.hip, scale(r.axis, fraction));
  return {static_cast<double>(p.x), static_cast<double>(p.y),
          static_cast<double>(p.z)};
}
auto print_bound(std::string_view name, BoardingPlantedLegScalarBounds b)
    -> void {
  std::cout << ' ' << name << "=[" << b.lower << ',' << b.upper << ']';
}
auto inspect(const BoardingPlantedHipPreflightDiagnostic& d,
             RigidVector3 candidate) -> void {
  check(d.preflight_version == 1 &&
            d.first_part == BoardingBodyPartId::pelvis &&
            d.second_part == BoardingBodyPartId::port_thigh,
        "Preflight selects only fixed pelvis/port-thigh pair, never opposite "
        "hip or full self");
  for (std::size_t axis = 0; axis < 3; ++axis)
    check(bits(component(d.candidate_relative_to_root, axis),
               component(candidate, axis)),
          "Exact selected candidate remains relative to same root expression");
  check(!d.full_self_qualified && !d.self_qualified && !d.dynamics_qualified &&
            !d.free_foot_swing_qualified && !d.load_qualified &&
            !d.world_qualified && !d.crop_qualified && !d.sweep_qualified &&
            !d.route_qualified && !d.actor_qualified && !d.seat_qualified &&
            !d.save_qualified && !d.first_flight_qualified,
        "Fixed witness never grants broad geometric/gameplay qualification");
  const auto conjunction =
      d.body.complete && d.axis_link_identity && d.arithmetic_supported &&
      d.pelvis_strict_interior && d.projection_strict_interior &&
      d.thigh_strict_interior && d.hip_region_strict_exclusion;
  check(d.strict_unowned_conflict == conjunction,
        "Strict unowned conflict requires every separate strict certificate "
        "and parent");
  if (d.strict_unowned_conflict)
    check(!d.first_refusal,
          "Verified strict conjunction has no unresolved-refusal label");
  else
    check(d.first_refusal.has_value(), "One failed candidate stays explicitly "
                                       "refused/unresolved, never clearance");
  if (!d.body.complete || !d.arithmetic_supported) {
    check(!d.strict_unowned_conflict && !d.pelvis_strict_interior &&
              !d.projection_strict_interior && !d.thigh_strict_interior &&
              !d.hip_region_strict_exclusion,
          "Incomplete/unsupported prerequisite cannot retain partial strict "
          "success flags");
    return;
  }
  if (!d.strict_unowned_conflict) {
    const auto expected =
        !d.pelvis_strict_interior
            ? BoardingPlantedHipPreflightCondition::pelvis_interior
            : (!d.projection_strict_interior
                   ? BoardingPlantedHipPreflightCondition::segment_projection
                   : (!d.thigh_strict_interior
                          ? BoardingPlantedHipPreflightCondition::thigh_interior
                          : BoardingPlantedHipPreflightCondition::
                                hip_region_exclusion));
    check(d.first_refusal && *d.first_refusal == expected,
          "Supported failed candidate retains first insufficient predicate in "
          "declared order");
  }
  check(d.axis_link_identity && bits(d.axis_length_metres, .47285) &&
            bits(d.thigh_radius_metres, .105) &&
            bits(d.hip_limit_metres, registered_hip_limit),
        "Axis/radius/finite original ownership constants remain unchanged");
  const auto r = reference(candidate);
  const Point p{static_cast<long double>(candidate.x),
                static_cast<long double>(candidate.y),
                static_cast<long double>(candidate.z)};
  const auto canonical = add(r.root, p),
             placed = add(canonical, {0, static_cast<long double>(.72), 0});
  point_contains(d.candidate_canonical, canonical);
  point_contains(d.candidate_placed, placed);
  point_contains(d.axis, r.axis);
  point_contains(d.unit_axis, r.unit);
  point_contains(d.witness_from_hip, r.w);
  check(
      bits(d.body.common_translation_y_metres, .72) &&
          bits(d.body.timing.requested_first, 0) &&
          bits(d.body.timing.requested_last, 0),
      "Witness parent remains exactly t0 with immutable common placement once");
  if (!d.body.leaves.empty()) {
    const auto& points = d.body.leaves[0].points;
    point_contains(points[0].value, r.root);
    point_contains(points[4].value, add(r.root, r.hip));
    point_contains(points[5].value, add(r.root, r.knee));
  }
  bool pelvis_proved = true;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    contains(d.pelvis_gaps[axis], r.pelvis_margins[axis]);
    pelvis_proved = pelvis_proved && d.pelvis_gaps[axis].lower > 0;
  }
  check(d.pelvis_strict_interior == pelvis_proved,
        "Pelvis success means three separately strict complete open bands");
  contains(d.projection, r.projection);
  contains(d.segment_upper_gap,
           static_cast<long double>(.47285) - r.projection);
  contains(d.hip_region_gap,
           r.projection - static_cast<long double>(registered_hip_limit));
  contains(d.radius_squared, r.radius_squared);
  contains(d.witness_norm_squared, dot(r.w, r.w));
  check(d.projection_strict_interior ==
            (d.projection.lower > 0 && d.segment_upper_gap.lower > 0),
        "Finite original segment requires both strict endpoint gaps");
  check(
      d.hip_region_strict_exclusion == (d.hip_region_gap.lower > 0),
      "Slab exclusion is separate strict sign, not absence of ownership proof");
  if (d.projection_strict_interior) {
    check(d.radial_distance_squared && d.thigh_gap,
          "Only genuine interior projection permits perpendicular-distance "
          "evidence");
    if (d.radial_distance_squared && d.thigh_gap) {
      contains(*d.radial_distance_squared, r.radial_squared);
      check(d.radial_distance_squared->lower >= 0,
            "Exact unit-axis identity retains nonnegative radial squared norm");
      contains(*d.thigh_gap, r.radius_squared - r.radial_squared);
      check(d.thigh_strict_interior == (d.thigh_gap->lower > 0),
            "Original capsule interior has separate strict "
            "radius-minus-distance gap");
    }
  } else
    check(!d.radial_distance_squared && !d.thigh_gap &&
              !d.thigh_strict_interior,
          "Failed endpoint projection cannot certify distance to an infinite "
          "line as capsule membership");
  if (d.pelvis_strict_interior)
    for (const auto margin : r.pelvis_margins)
      check(margin > 0,
            "Independent reference corroborates each strict pelvis band");
  if (d.hip_region_strict_exclusion)
    check(r.projection > static_cast<long double>(registered_hip_limit),
          "Independent unit-axis projection corroborates strict finite-slab "
          "exclusion");
  if (d.thigh_strict_interior)
    check(r.projection > 0 && r.projection < static_cast<long double>(.47285) &&
              r.radial_squared < r.radius_squared,
          "Independent unfactored geometry corroborates strict original "
          "capsule membership");
}
auto public_observation() -> void {
  const auto d = require(assess_origin_boarding_planted_hip_preflight());
  std::cout << std::setprecision(17)
            << "frozen hip preflight body=" << d.body.complete
            << " arithmetic=" << d.arithmetic_supported
            << " link=" << d.axis_link_identity
            << " pelvis=" << d.pelvis_strict_interior
            << " projection=" << d.projection_strict_interior
            << " thigh=" << d.thigh_strict_interior
            << " slab_exclusion=" << d.hip_region_strict_exclusion
            << " strict_unowned=" << d.strict_unowned_conflict;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    std::cout << " pelvis_gap" << axis;
    print_bound("", d.pelvis_gaps[axis]);
  }
  print_bound("projection", d.projection);
  print_bound("segment_upper", d.segment_upper_gap);
  print_bound("hip_region", d.hip_region_gap);
  print_bound("radius_squared", d.radius_squared);
  print_bound("witness_norm_squared", d.witness_norm_squared);
  if (d.radial_distance_squared)
    print_bound("radial_squared", *d.radial_distance_squared);
  if (d.thigh_gap) print_bound("thigh_gap", *d.thigh_gap);
  if (d.first_refusal)
    std::cout << " refusal=" << static_cast<int>(*d.first_refusal);
  std::cout << '\n';
  inspect(d, {-.04, -.117, -.177});
  const auto& timing = d.body.timing;
  const auto& closure = timing.closure;
  check(d.body.complete && d.body.reservations_complete &&
            d.body.mass_model_complete && d.body.com_derivatives_complete &&
            !d.body.first_refusal && timing.complete &&
            timing.derivative_domains_certified &&
            timing.root_speed_certified && timing.root_acceleration_certified &&
            timing.leg_joint_speed_certified && !timing.first_refusal &&
            closure.complete && closure.plane_identities &&
            closure.link_identities && closure.joint_limits_certified &&
            !closure.first_refusal,
        "Observed fixed witness retains every separately complete parent "
        "certificate");
  check(d.arithmetic_supported && d.axis_link_identity &&
            d.pelvis_strict_interior && d.projection_strict_interior &&
            d.thigh_strict_interior && d.hip_region_strict_exclusion &&
            d.strict_unowned_conflict && !d.first_refusal &&
            d.radial_distance_squared && d.thigh_gap,
        "Observed unchanged fixed candidate remains a strict unowned connected "
        "conflict");
  if (d.thigh_gap)
    check(std::min({d.pelvis_gaps[0].lower, d.pelvis_gaps[1].lower,
                    d.pelvis_gaps[2].lower, d.projection.lower,
                    d.segment_upper_gap.lower, d.hip_region_gap.lower,
                    d.thigh_gap->lower}) > 0,
          "Every observed strict outward gap has a positive lower bound "
          "without tolerance");
  check(snapshot(d.body) ==
            snapshot(require(assess_origin_boarding_planted_body(0, 0))),
        "Owned point body preserves every old shape/mass/COM/closure/timing "
        "bit and refusal");
}
auto candidate(RigidVector3 p) -> BoardingPlantedHipPreflightDiagnostic {
  auto d = require(detail::boarding_planted_hip_preflight_bounded(p));
  inspect(d, p);
  return d;
}
auto candidate_controls() -> void {
  const auto pelvis_only = candidate({0, 0, 0});
  check(pelvis_only.pelvis_strict_interior &&
            !pelvis_only.thigh_strict_interior &&
            !pelvis_only.strict_unowned_conflict,
        "Pelvis-only interior cannot become a common-interior witness");
  const auto owned = candidate(candidate_on_axis(.25L));
  check(owned.pelvis_strict_interior && owned.projection_strict_interior &&
            owned.thigh_strict_interior && !owned.hip_region_strict_exclusion &&
            !owned.strict_unowned_conflict,
        "Genuine common interior inside finite owned region is not unowned "
        "conflict");
  const auto thigh_only = candidate(candidate_on_axis(.75L));
  check(!thigh_only.pelvis_strict_interior &&
            thigh_only.projection_strict_interior &&
            thigh_only.thigh_strict_interior &&
            !thigh_only.strict_unowned_conflict,
        "Thigh-only membership cannot masquerade as pelvis overlap");
  for (const auto fraction : std::array{-.25L, 1.25L}) {
    const auto d = candidate(candidate_on_axis(fraction));
    check(!d.projection_strict_interior && !d.radial_distance_squared &&
              !d.thigh_gap && !d.strict_unowned_conflict,
          "Before/after original segment refuses radial membership inference");
  }
  const auto endpoint = candidate({-.14, 0, 0});
  check(!endpoint.projection_strict_interior &&
            !endpoint.strict_unowned_conflict,
        "Exact shared hip endpoint does not satisfy strict segment-interior "
        "projection");
  // Uncertainty around the exact unchanged slab is exposed, never epsilon-
  // repaired. Rounded private candidates are only candidate tests, not a new
  // rounded region axis or a public witness replacement.
  const auto ratio = static_cast<long double>(registered_hip_limit) /
                     static_cast<long double>(.47285);
  const auto boundary = candidate_on_axis(ratio);
  for (const auto direction : std::array{-1, 0, 1}) {
    auto p = boundary;
    if (direction != 0)
      p.z = std::nextafter(p.z, direction < 0
                                    ? -std::numeric_limits<double>::infinity()
                                    : std::numeric_limits<double>::infinity());
    const auto d = candidate(p);
    if (d.hip_region_gap.lower <= 0)
      check(!d.hip_region_strict_exclusion && !d.strict_unowned_conflict,
            "Uncertain finite ownership sign remains unresolved without "
            "tolerance");
  }
}
auto set_component(RigidVector3& p, std::size_t axis, double v) -> void {
  if (axis == 0)
    p.x = v;
  else if (axis == 1)
    p.y = v;
  else
    p.z = v;
}
auto boundary_controls() -> void {
  const std::array halves{.24, .12, .18};
  const auto inf = std::numeric_limits<double>::infinity();
  for (std::size_t axis = 0; axis < 3; ++axis)
    for (const auto sign : std::array{-1., 1.}) {
      RigidVector3 p;
      set_component(p, axis, sign * halves[axis]);
      const auto on = candidate(p);
      check(!on.pelvis_strict_interior && !on.strict_unowned_conflict &&
                on.pelvis_gaps[axis].lower <= 0,
            "Every exact open pelvis boundary refuses strict interior without "
            "epsilon");
      set_component(p, axis, std::nextafter(sign * halves[axis], sign * inf));
      const auto outside = candidate(p);
      check(!outside.pelvis_strict_interior && !outside.strict_unowned_conflict,
            "Nextafter outward of every pelvis face never acquires strict "
            "interior");
      set_component(p, axis, std::nextafter(sign * halves[axis], 0.));
      const auto inside = candidate(p);
      // A one-ULP interior point may be conservatively uncertain; it is never
      // admitted by a caller epsilon or treated as contact forgiveness.
      if (inside.pelvis_gaps[axis].lower <= 0)
        check(!inside.pelvis_strict_interior && !inside.strict_unowned_conflict,
              "Uncertain adjacent strict interior remains a truthful refusal");
    }
  for (std::size_t axis = 0; axis < 3; ++axis)
    for (const auto sign : std::array{-1., 1.}) {
      RigidVector3 p;
      set_component(p, axis, sign * 8);
      const auto d = candidate(p);
      check(d.body.complete && !d.pelvis_strict_interior &&
                !d.strict_unowned_conflict,
            "Exact finite workspace endpoints remain diagnostic candidates, "
            "never pelvis witnesses");
    }
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  for (std::size_t axis = 0; axis < 3; ++axis)
    for (const auto bad : std::array{nan, inf, -inf, std::nextafter(8., inf),
                                     std::nextafter(-8., -inf)}) {
      RigidVector3 p;
      set_component(p, axis, bad);
      check(!detail::boarding_planted_hip_preflight_bounded(p),
            "Nonfinite or excessive candidate component is an API error");
    }
}
auto capacity_controls() -> void {
  const RigidVector3 p{-.04, -.117, -.177};
  const auto capped =
      require(detail::boarding_planted_hip_preflight_bounded(p, 0));
  inspect(capped, p);
  check(!capped.body.complete && capped.body.timing.complete &&
            capped.body.leaves.empty() && capped.first_refusal &&
            *capped.first_refusal ==
                BoardingPlantedHipPreflightCondition::body_prerequisite &&
            !capped.arithmetic_supported && !capped.radial_distance_squared &&
            !capped.thigh_gap && !capped.strict_unowned_conflict,
        "Missing body assembly preserves complete old timing but grants no "
        "candidate graph/conflict");
  check(snapshot(capped.body) ==
            snapshot(require(detail::boarding_planted_body_bounded(0, 0, 0))),
        "Lowered body prerequisite retains every original body/timing/refusal "
        "field");
  for (const auto& caps : std::array{std::array<std::size_t, 3>{12, 0, 4096},
                                     std::array<std::size_t, 3>{12, 8191, 0}}) {
    const auto d = require(detail::boarding_planted_hip_preflight_bounded(
        p, 4096, caps[0], caps[1], caps[2]));
    inspect(d, p);
    check(!d.body.complete && !d.body.timing.complete &&
              d.body.leaves.empty() && !d.strict_unowned_conflict &&
              d.first_refusal &&
              *d.first_refusal ==
                  BoardingPlantedHipPreflightCondition::body_prerequisite,
          "Incomplete inherited timing refuses before witness evaluation "
          "without a new retry budget");
    check(snapshot(d.body) ==
              snapshot(require(detail::boarding_planted_body_bounded(
                  0, 0, 4096, caps[0], caps[1], caps[2]))),
          "Reduced timing body evidence/refusal remains unchanged inside "
          "preflight");
  }
  check(!detail::boarding_planted_hip_preflight_bounded(p, 4097),
        "Cannot enlarge body storage for a candidate");
  for (const auto& caps :
       std::array{std::array<std::size_t, 3>{13, 8191, 4096},
                  std::array<std::size_t, 3>{12, 8192, 4096},
                  std::array<std::size_t, 3>{12, 8191, 4097}})
    check(!detail::boarding_planted_hip_preflight_bounded(p, 4096, caps[0],
                                                          caps[1], caps[2]),
          "Cannot enlarge any inherited subdivision/work budget");
}
struct SavedEnvironment {
  std::fenv_t original{};
  SavedEnvironment() {
    if (std::fegetenv(&original) != 0)
      throw std::runtime_error("Cannot save arithmetic environment");
  }
  SavedEnvironment(const SavedEnvironment&) = delete;
  auto operator=(const SavedEnvironment&) -> SavedEnvironment& = delete;
  ~SavedEnvironment() {
    check(std::fesetenv(&original) == 0,
          "Restore arithmetic environment after hip adversary");
  }
};
auto denied_environment() -> void {
  const auto d = require(assess_origin_boarding_planted_hip_preflight());
  check(!d.arithmetic_supported && !d.axis_link_identity &&
            !d.pelvis_strict_interior && !d.projection_strict_interior &&
            !d.thigh_strict_interior && !d.hip_region_strict_exclusion &&
            !d.strict_unowned_conflict && !d.radial_distance_squared &&
            !d.thigh_gap && d.first_refusal &&
            *d.first_refusal ==
                BoardingPlantedHipPreflightCondition::unsupported_arithmetic,
        "Unsupported environment cannot retain any strict conflict predicate "
        "or perpendicular-distance authority");
  check(snapshot(d.body) ==
            snapshot(require(assess_origin_boarding_planted_body(0, 0))),
        "Unsupported arithmetic retains truthful old body/closure/timing "
        "diagnostic without laundering success");
}
auto environment_controls() -> void {
  for (const auto mode : std::array{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    SavedEnvironment saved;
    check(std::fesetround(mode) == 0, "Set directed rounding hip adversary");
    denied_environment();
  }
#if defined(__SSE2__) && defined(__x86_64__)
  struct SavedControl {
    unsigned original{_mm_getcsr()};
    ~SavedControl() { _mm_setcsr(original); }
  };
  for (const auto flags :
       std::array{1U << 15, 1U << 6, (1U << 15) | (1U << 6)}) {
    SavedEnvironment saved;
    SavedControl control;
    _mm_setcsr(control.original | flags);
    denied_environment();
  }
  for (const auto rc : std::array{1U, 2U, 3U}) {
    SavedEnvironment saved;
    SavedControl control;
    _mm_setcsr((control.original & ~(3U << 13)) | (rc << 13));
    denied_environment();
  }
#endif
  check(std::fegetround() == FE_TONEAREST,
        "Arithmetic environment restored before subsequent independent "
        "observation");
}
auto ownership_controls() -> void {
  const auto retained = []() {
    auto d = require(assess_origin_boarding_planted_hip_preflight());
    auto copied = d;
    return copied;
  }();
  const auto original_body = snapshot(retained.body);
  const auto projection = retained.projection;
  const auto canonical = retained.candidate_canonical;
  {
    auto copied = retained;
    auto moved = std::move(copied);
    moved.candidate_relative_to_root = {8, 8, 8};
    moved.projection.lower = 100;
    moved.body.common_translation_y_metres = 100;
    if (!moved.body.leaves.empty())
      moved.body.leaves[0].points[5].value.lower.x = 100;
  }
  check(snapshot(retained.body) == original_body &&
            bits(retained.projection.lower, projection.lower) &&
            bits(retained.projection.upper, projection.upper),
        "Copied/moved diagnostic owns all parent/projection evidence after "
        "other lifetime ends");
  for (std::size_t axis = 0; axis < 3; ++axis)
    check(
        bits(component(retained.candidate_canonical.lower, axis),
             component(canonical.lower, axis)) &&
            bits(component(retained.candidate_canonical.upper, axis),
                 component(canonical.upper, axis)),
        "Witness bounds survive destruction and mutation of copied diagnostic");
  const auto next = require(assess_origin_boarding_planted_hip_preflight());
  check(snapshot(next.body) == original_body &&
            bits(next.projection.lower, projection.lower) &&
            bits(next.projection.upper, projection.upper) &&
            next.strict_unowned_conflict == retained.strict_unowned_conflict,
        "A new fixed preflight does not inherit mutations or cached candidate "
        "state");
}
} // namespace
int main() {
  try {
    public_observation();
    candidate_controls();
    boundary_controls();
    capacity_controls();
    environment_controls();
    ownership_controls();
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "FAIL: " << e.what() << '\n';
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
