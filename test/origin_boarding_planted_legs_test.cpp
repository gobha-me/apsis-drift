#include "apsis_drift/origin_boarding_planted_legs.hpp"
#include "origin_boarding_planted_legs_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#if defined(__SSE2__) && defined(__x86_64__)
#include <xmmintrin.h>
#endif
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <utility>

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
struct Point {
  long double x{}, y{}, z{};
};
auto add(Point a, Point b) -> Point {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(Point a, Point b) -> Point {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(Point p, long double f) -> Point {
  return {p.x * f, p.y * f, p.z * f};
}
auto dot(Point a, Point b) -> long double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
auto length(Point p) -> long double {
  return std::sqrt(dot(p, p));
}
struct OracleLeg {
  Point hip, knee, ankle;
  long double hip_flex{}, knee_flex{}, ankle_pitch{}, leg_roll{}, abduction{};
};
// Independent two-circle/sphere-intersection construction in the registered
// vertical leg plane. All constants are the actual stored binary64 recipe
// values promoted to long double, not ideal decimal substitutes. Samples only
// corroborate interval enclosures; they never establish continuous permission.
auto oracle(double parameter, std::size_t side) -> OracleLeg {
  const auto t = static_cast<long double>(parameter);
  const auto root_x = static_cast<long double>(.32) * t * t * (3 - 2 * t);
  const auto plane = side == 0 ? 0.L : static_cast<long double>(-.16);
  const Point hip{root_x + (side == 0 ? -static_cast<long double>(.14)
                                      : static_cast<long double>(.14)),
                  static_cast<long double>(.72),
                  static_cast<long double>(-.16)};
  const Point ankle{static_cast<long double>(side == 0 ? .02 : .34),
                    plane + static_cast<long double>(.1),
                    static_cast<long double>(side == 0 ? -.35 : -.65)};
  const auto delta = sub(ankle, hip);
  const auto distance = length(delta);
  const auto rho = std::hypot(delta.x, delta.y);
  const auto l1 = static_cast<long double>(.47285),
             l2 = static_cast<long double>(.47478);
  if (!(distance > 0 && rho > 0 && distance < l1 + l2 &&
        distance > std::abs(l1 - l2)))
    throw std::runtime_error("Registered sample outside strict analytic reach");
  const auto along = (l1 * l1 - l2 * l2 + distance * distance) / (2 * distance);
  const auto height = std::sqrt(l1 * l1 - along * along);
  const Point direction = scale(delta, 1 / distance);
  const Point forward{delta.x * delta.z / (rho * distance),
                      delta.y * delta.z / (rho * distance), -rho / distance};
  const auto knee =
      add(hip, add(scale(direction, along), scale(forward, height)));
  const auto thigh = sub(knee, hip), shin = sub(ankle, knee);
  const auto degrees = 180 / std::numbers::pi_v<long double>;
  const auto hip_angle =
      std::atan2(-thigh.z, std::hypot(thigh.x, thigh.y)) * degrees;
  const auto knee_angle = std::acos(dot(thigh, shin) / (l1 * l2)) * degrees;
  const auto roll = std::atan2(delta.x, -delta.y) * degrees;
  return {hip,        knee,
          ankle,      hip_angle,
          knee_angle, knee_angle - hip_angle,
          roll,       side == 0 ? -roll : roll};
}
template <class T, class E> auto require(std::expected<T, E> value) -> T {
  if (!value)
    throw std::runtime_error(
        "Registered public planted-leg request refused API input");
  return std::move(*value);
}
auto bits(double a, double b) -> bool {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
auto valid(BoardingPlantedLegScalarBounds b) -> bool {
  return std::isfinite(b.lower) && std::isfinite(b.upper) && b.lower <= b.upper;
}
auto contains(BoardingPlantedLegScalarBounds b, long double x) -> bool {
  return valid(b) && static_cast<long double>(b.lower) <= x &&
         x <= static_cast<long double>(b.upper);
}
auto component(RigidVector3 p, std::size_t axis) -> double {
  return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
}
auto component(Point p, std::size_t axis) -> long double {
  return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
}
auto point_bounds(const BoardingPlantedLegPointBounds& bounds, Point p,
                  double placement) -> void {
  for (std::size_t axis = 0; axis < 3; ++axis) {
    const auto local = component(p, axis) -
                       (axis == 1 ? static_cast<long double>(placement) : 0.L);
    check(
        contains({component(bounds.lower, axis), component(bounds.upper, axis)},
                 local),
        "Independent sphere-intersection point lies in canonical outward "
        "coordinate bounds");
  }
}
auto near_report(double report, long double oracle_value) -> bool {
  // Display-only inverse angles; this quantified double rounding allowance
  // never feeds the joint-limit or interval-cover acceptance checks.
  const auto allowance = 4096 * std::numeric_limits<double>::epsilon() *
                         std::max(1.L, std::abs(oracle_value));
  return std::isfinite(report) &&
         std::abs(static_cast<long double>(report) - oracle_value) <= allowance;
}
auto corroborate_leaf(const BoardingPlantedLegLeaf& leaf, double placement)
    -> void {
  const auto middle = leaf.first + (leaf.last - leaf.first) * .5;
  for (const auto sample : std::array{leaf.first, middle, leaf.last}) {
    const auto t = static_cast<long double>(sample);
    const auto root_x = static_cast<long double>(.32) * t * t * (3 - 2 * t);
    check(contains(leaf.root_x, root_x),
          "Registered monotone polynomial enclosed at corroborating sample");
    for (std::size_t side = 0; side < 2; ++side) {
      const auto expected = oracle(sample, side);
      const auto& e = leaf.legs[side];
      point_bounds(e.hip, expected.hip, placement);
      point_bounds(e.knee, expected.knee, placement);
      point_bounds(e.ankle, expected.ankle, placement);
      auto center = expected.ankle;
      const auto plane = side == 0 ? 0.L : static_cast<long double>(-.16);
      center.y = plane + static_cast<long double>(.05);
      point_bounds(e.boot_center, center, placement);
      const auto delta = sub(expected.ankle, expected.hip);
      const auto thigh = sub(expected.knee, expected.hip),
                 shin = sub(expected.ankle, expected.knee);
      const auto l1 = static_cast<long double>(.47285),
                 l2 = static_cast<long double>(.47478);
      const auto rho = std::hypot(delta.x, delta.y);
      const auto round_bound =
          512 * std::numeric_limits<long double>::epsilon();
      check(std::abs(length(thigh) - l1) <= round_bound &&
                std::abs(length(shin) - l2) <= round_bound,
            "Independent law-of-cosines knee preserves both nominal link "
            "lengths");
      check(std::abs(delta.y * thigh.x - delta.x * thigh.y) <= round_bound &&
                expected.knee.z < expected.hip.z,
            "Independent knee retains selected vertical plane and forward "
            "negative-Z branch");
      const auto D = dot(delta, delta);
      const auto alpha = (l1 * l1 - l2 * l2 + D) / (2 * D);
      const auto gamma = std::sqrt(l1 * l1 / D - alpha * alpha);
      check(contains(e.alpha, alpha) && contains(e.gamma, gamma),
            "Dimensionless along/perpendicular closure coefficients "
            "independently enclosed");
      check(contains(e.distance_squared, dot(delta, delta)) &&
                contains(e.rho, rho),
            "Distance and derived vertical-plane norm independently enclosed");
      check(contains(e.hip_sine, -thigh.z / l1) &&
                contains(e.hip_cosine, std::hypot(thigh.x, thigh.y) / l1) &&
                contains(e.shin_sine, -shin.z / l2) &&
                contains(e.shin_cosine,
                         (shin.x * delta.x + shin.y * delta.y) / (rho * l2)) &&
                contains(e.knee_cosine, dot(thigh, shin) / (l1 * l2)) &&
                contains(e.roll_sine, delta.x / rho) &&
                contains(e.roll_cosine, -delta.y / rho),
            "Independent reconstructed sectors enclosed without treating "
            "display angles as proofs");
      if (bits(sample, middle))
        check(near_report(e.reporting_hip_flex_degrees, expected.hip_flex) &&
                  near_report(e.reporting_knee_flex_degrees,
                              expected.knee_flex) &&
                  near_report(e.reporting_ankle_pitch_degrees,
                              expected.ankle_pitch) &&
                  near_report(e.reporting_hip_abduction_degrees,
                              expected.abduction),
              "Reporting inverse angles corroborate the same fixed midpoint "
              "branch");
    }
  }
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& e = leaf.legs[side];
    check(e.plane_identity && e.link_identities && e.limits_certified &&
              e.limiting_condition == BoardingPlantedLegCondition::none,
          "Every accepted leaf retains all domain/identity/limit certificates");
    const auto plane = side == 0 ? 0. : -.16;
    check(bits(e.plane_metres, plane) && bits(e.ankle_y_terms[0], plane) &&
              bits(e.ankle_y_terms[1], .1) && bits(e.ankle_y_terms[2], -.72) &&
              bits(e.boot_center_y_terms[0], plane) &&
              bits(e.boot_center_y_terms[1], .05) &&
              bits(e.boot_center_y_terms[2], -.72) && bits(placement, .72),
          "Exact retained plane/height/-placement terms cancel one common "
          "placement algebraically");
    for (const auto b :
         std::array{e.distance_squared, e.rho, e.alpha, e.gamma, e.hip_sine,
                    e.hip_cosine, e.shin_sine, e.shin_cosine, e.knee_cosine,
                    e.roll_sine, e.roll_cosine})
      check(valid(b), "All accepted numeric enclosures finite and ordered");
  }
}
auto diagnostic(double first, double last) -> BoardingPlantedLegDiagnostic {
  auto d = require(assess_origin_boarding_planted_legs(first, last));
  check(d.recipe_version == 1 && bits(d.requested_first, first) &&
            bits(d.requested_last, last) && d.reverse == (first > last) &&
            bits(d.duration_seconds, 12) && bits(d.placement_y_metres, .72),
        "Exact version/endpoint/direction/duration/common-placement metadata");
  check(!d.dynamics_qualified && !d.self_qualified && !d.load_qualified &&
            !d.world_qualified && !d.route_qualified && !d.actor_qualified &&
            !d.seat_qualified,
        "Source-free analytic closure grants no deferred physical/gameplay "
        "permission");
  check(d.examined_nodes >= 1 && d.examined_nodes <= 8191 &&
            d.maximum_depth <= 12 && d.leaves.size() <= 4096,
        "Registered complete subdivision workspace stays bounded");
  const auto low = std::min(first, last), high = std::max(first, last);
  auto endpoint = low;
  for (const auto& leaf : d.leaves) {
    check(bits(leaf.first, endpoint) && leaf.first <= leaf.last &&
              leaf.last <= high,
          "Accepted ascending interval cover has no omitted "
          "endpoint/overlap/gap");
    endpoint = leaf.last;
    corroborate_leaf(leaf, d.placement_y_metres);
  }
  if (d.complete) {
    check(!d.first_refusal && !d.leaves.empty() && bits(endpoint, high) &&
              d.plane_identities && d.link_identities &&
              d.joint_limits_certified &&
              d.examined_nodes == 2 * d.leaves.size() - 1,
          "Complete closed cover and full binary-tree counters required for "
          "acceptance");
  } else {
    check(d.first_refusal && !d.joint_limits_certified,
          "Refused prefix cannot claim a complete joint-limited interval");
    if (d.first_refusal) {
      const auto& r = *d.first_refusal;
      check(bits(r.first, endpoint) && r.first <= r.last && r.last <= high &&
                r.side < 2 && r.depth <= 12 &&
                r.condition != BoardingPlantedLegCondition::none,
            "First refusal owns exact next uncovered interval and genuine "
            "bounded cause");
    }
  }
  std::cout << "planted " << first << " -> " << last
            << " complete=" << d.complete << " leaves=" << d.leaves.size()
            << " nodes=" << d.examined_nodes << " depth=" << d.maximum_depth;
  if (d.first_refusal)
    std::cout << " refusal=" << static_cast<int>(d.first_refusal->condition)
              << " limiting="
              << static_cast<int>(d.first_refusal->limiting_condition)
              << " side=" << d.first_refusal->side
              << " interval=" << d.first_refusal->first << ':'
              << d.first_refusal->last;
  std::cout << '\n';
  return d;
}
auto snapshot(const BoardingPlantedLegDiagnostic& d)
    -> std::vector<std::uint64_t> {
  std::vector<std::uint64_t> result;
  const auto number = [&](double x) {
    result.push_back(std::bit_cast<std::uint64_t>(x));
  };
  const auto integer = [&](auto x) {
    result.push_back(static_cast<std::uint64_t>(x));
  };
  const auto scalar = [&](BoardingPlantedLegScalarBounds b) {
    number(b.lower);
    number(b.upper);
  };
  const auto point = [&](BoardingPlantedLegPointBounds b) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      number(component(b.lower, axis));
      number(component(b.upper, axis));
    }
  };
  integer(d.recipe_version);
  number(d.duration_seconds);
  number(d.placement_y_metres);
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
    for (const auto& e : leaf.legs) {
      for (const auto b : std::array{e.hip, e.knee, e.ankle, e.boot_center})
        point(b);
      for (const auto b :
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
  return result;
}
auto public_intervals() -> void {
  const auto full = diagnostic(0, 1), reverse = diagnostic(1, 0);
  check(full.complete && reverse.complete,
        "Registered unchanged full/reverse fixture retains complete continuous "
        "closure and limits");
  check(
      snapshot(full) == snapshot(reverse),
      "Reverse preserves exact same ascending geometric evidence and counters");
  check(snapshot(full) == snapshot(diagnostic(0, 1)),
        "No history dependence in full bounded interval");
  for (const auto range : std::array{std::array{0., .5}, std::array{.5, 1.},
                                     std::array{.125, .875}}) {
    const auto forward = diagnostic(range[0], range[1]);
    const auto backward = diagnostic(range[1], range[0]);
    check(forward.complete && backward.complete,
          "Registered unchanged subinterval observations retain complete "
          "acceptance");
    check(snapshot(forward) == snapshot(backward),
          "Subinterval reversal preserves all leaf/first-refusal bits");
  }
  for (const auto t : std::array{0., .125, .25, .5, .75, .875, 1.}) {
    const auto point = diagnostic(t, t);
    check(point.complete && point.leaves.size() == 1 &&
              point.examined_nodes == 1 && point.maximum_depth == 0,
          "Complete degenerate closed interval includes its sole endpoint "
          "exactly once");
  }
  const auto owned = [] {
    return require(assess_origin_boarding_planted_legs(.125, .875));
  }();
  auto copied = owned;
  const auto before = snapshot(owned);
  copied.leaves.clear();
  check(snapshot(owned) == before,
        "Returned interval evidence owns independent copied leaf storage");
  const auto inf = std::numeric_limits<double>::infinity();
  for (const auto bad :
       std::array{std::nextafter(0., -inf), std::nextafter(1., inf), -1., 2.,
                  inf, -inf, std::numeric_limits<double>::quiet_NaN()}) {
    check(!assess_origin_boarding_planted_legs(bad, .5) &&
              !assess_origin_boarding_planted_legs(.5, bad),
          "Nonfinite or nextafter out-of-domain endpoint refuses before "
          "geometric evaluation");
  }
}
struct NumericInput {
  RigidVector3 hip{};
  double ankle_x{}, ankle_z{}, plane{}, thigh{.47285}, shin{.47478};
  bool forward{true};
};
auto angles(double hip, double knee, double roll) -> NumericInput {
  const auto radians = std::numbers::pi_v<long double> / 180;
  const auto h = static_cast<long double>(hip) * radians;
  const auto s = static_cast<long double>(hip - knee) * radians;
  const auto r = static_cast<long double>(roll) * radians;
  const auto l1 = static_cast<long double>(.47285),
             l2 = static_cast<long double>(.47478);
  const auto down = l1 * std::cos(h) + l2 * std::cos(s);
  // Toy expression inputs, not supplied joints/angles in the public API. Plane
  // encodes the actual ankle-height expression used by the private evaluator.
  return {
      {0, 0, 0},
      static_cast<double>(down * std::sin(r)),
      static_cast<double>(-l1 * std::sin(h) - l2 * std::sin(s)),
      static_cast<double>(-down * std::cos(r) - static_cast<long double>(.1))};
}
auto numeric(NumericInput input) -> BoardingPlantedLegEvidence {
  return detail::boarding_planted_leg_numeric(
      input.hip, input.ankle_x, input.ankle_z, input.plane, input.thigh,
      input.shin, input.forward);
}
auto refusal(NumericInput input, BoardingPlantedLegCondition reason) -> void {
  const auto e = numeric(input);
  check(!e.limits_certified && e.limiting_condition == reason,
        "Independent private arithmetic/domain/branch/joint adversary refuses "
        "exact cause");
}
auto private_numeric() -> void {
  using Condition = BoardingPlantedLegCondition;
  const auto good = angles(45, 60, 0);
  const auto e = numeric(good);
  check(e.plane_identity && e.link_identities && e.limits_certified &&
            e.limiting_condition == Condition::none &&
            near_report(e.reporting_hip_flex_degrees, 45) &&
            near_report(e.reporting_knee_flex_degrees, 60) &&
            near_report(e.reporting_ankle_pitch_degrees, 15),
        "Independent deep-sector two-link toy has all closure certificates");
  refusal(angles(70, 90, 0), Condition::hip_flex);
  refusal(angles(-25, 1, 0), Condition::hip_flex);
  refusal(angles(60, 140, 0), Condition::knee_flex);
  refusal(angles(30, 61, 0), Condition::ankle_pitch);
  refusal(angles(50, 19, 0), Condition::ankle_pitch);
  for (const auto roll : std::array{-36., -16., 16., 36.})
    refusal(angles(45, 60, roll), Condition::hip_roll);
  // The fixed branch has nonnegative knee flex by construction. Roll15 is
  // tighter than abduction35; neither test manufactures a separate freedom.
  auto reverse = good;
  reverse.forward = false;
  refusal(reverse, Condition::forward_branch);
  NumericInput above{{0, 0, 0}, 0, -.4, 0};
  refusal(above, Condition::forward_branch);
  NumericInput singular{{0, .1, 0}, 0, -.4, 0};
  refusal(singular, Condition::singular_plane);
  NumericInput too_far{{0, 0, 0}, 0, 0, -2.1};
  refusal(too_far, Condition::reach);
  NumericInput too_close{{0, 0, 0}, 0, 0, -.1001};
  refusal(too_close, Condition::reach);
  const auto inf = std::numeric_limits<double>::infinity();
  for (const auto invalid :
       std::array{inf, -inf, std::numeric_limits<double>::quiet_NaN()}) {
    auto bad = good;
    bad.hip.x = invalid;
    refusal(bad, Condition::unsupported_arithmetic);
    bad = good;
    bad.ankle_z = invalid;
    refusal(bad, Condition::unsupported_arithmetic);
    bad = good;
    bad.plane = invalid;
    refusal(bad, Condition::unsupported_arithmetic);
    bad = good;
    bad.thigh = invalid;
    refusal(bad, Condition::unsupported_arithmetic);
  }
  for (const auto length :
       std::array{0., -1., std::nextafter(1e-6, 0.), std::nextafter(4., inf)}) {
    auto bad = good;
    bad.thigh = length;
    refusal(bad, Condition::unsupported_arithmetic);
    bad = good;
    bad.shin = length;
    refusal(bad, Condition::unsupported_arithmetic);
  }
  auto workspace = good;
  workspace.hip.y = std::nextafter(8., inf);
  refusal(workspace, Condition::unsupported_arithmetic);
  workspace = good;
  workspace.plane = std::nextafter(-8., -inf);
  refusal(workspace, Condition::unsupported_arithmetic);
}
auto bounded_workspace() -> void {
  using Condition = BoardingPlantedLegCondition;
  const auto query = [](double first, double last, std::size_t depth,
                        std::size_t nodes, std::size_t leaves) {
    return require(detail::boarding_planted_legs_bounded(first, last, depth,
                                                         nodes, leaves));
  };
  const auto zero_nodes = query(0, 1, 12, 0, 4096);
  check(!zero_nodes.complete && zero_nodes.examined_nodes == 0 &&
            zero_nodes.leaves.empty() && zero_nodes.first_refusal &&
            zero_nodes.first_refusal->condition ==
                Condition::subdivision_capacity,
        "Zero node budget refuses before any node or partial-success claim");
  const auto zero_leaves = query(.5, .5, 12, 8191, 0);
  check(!zero_leaves.complete && zero_leaves.leaves.empty() &&
            zero_leaves.first_refusal &&
            zero_leaves.first_refusal->condition ==
                Condition::subdivision_capacity,
        "Zero leaf reservation cannot install an accepted point interval");
  const auto depth = query(0, 1, 0, 8191, 4096);
  check(!depth.complete && depth.examined_nodes == 1 &&
            depth.maximum_depth == 0 && depth.first_refusal &&
            depth.first_refusal->condition == Condition::subdivision_capacity &&
            depth.first_refusal->limiting_condition != Condition::none,
        "Uncertified broad interval cannot bypass fixed depth budget");
  const auto one_node = query(0, 1, 12, 1, 4096);
  check(!one_node.complete && one_node.examined_nodes == 1 &&
            one_node.leaves.empty() && one_node.first_refusal &&
            one_node.first_refusal->condition ==
                Condition::subdivision_capacity,
        "Exactly consumed node budget cannot enter a second interval");
  for (const auto budget :
       std::array{std::array<std::size_t, 3>{13, 8191, 4096},
                  std::array<std::size_t, 3>{12, 8192, 4096},
                  std::array<std::size_t, 3>{12, 8191, 4097}})
    check(!detail::boarding_planted_legs_bounded(0, 1, budget[0], budget[1],
                                                 budget[2]),
          "Private budget cannot widen registered capacities");
  const auto defaults = query(0, 1, 12, 8191, 4096);
  check(snapshot(defaults) ==
            snapshot(require(assess_origin_boarding_planted_legs())),
        "Registered private bounds exactly reproduce public immutable recipe");
  for (const auto boundary : std::array{0., 1.}) {
    const auto adjacent = std::nextafter(boundary, boundary == 0 ? 1. : 0.);
    const auto d = query(boundary, adjacent, 12, 8191, 4096);
    check(
        d.complete ||
            (d.first_refusal &&
             (d.first_refusal->condition == Condition::unsplittable_interval ||
              d.first_refusal->condition == Condition::subdivision_capacity)),
        "Adjacent parameter interval completes or truthfully refuses without "
        "hidden midpoint");
  }
}
struct SavedEnvironment {
  std::fenv_t original{};
  SavedEnvironment() {
    if (std::fegetenv(&original) != 0)
      throw std::runtime_error("Unable to save arithmetic environment");
  }
  SavedEnvironment(const SavedEnvironment&) = delete;
  auto operator=(const SavedEnvironment&) -> SavedEnvironment& = delete;
  ~SavedEnvironment() {
    check(std::fesetenv(&original) == 0,
          "Restore original floating-point environment after adversary");
  }
};
auto denied_environment() -> void {
  const auto e = numeric(angles(45, 60, 0));
  check(!e.limits_certified && !e.plane_identity && !e.link_identities &&
            e.limiting_condition ==
                BoardingPlantedLegCondition::unsupported_arithmetic,
        "Unsupported arithmetic environment refuses before any expression "
        "certificate");
  const auto d = assess_origin_boarding_planted_legs(.5, .5);
  check(d.has_value(), "Valid endpoint request retains diagnostic for "
                       "unsupported arithmetic environment");
  if (d)
    check(!d->complete && !d->plane_identities && !d->link_identities &&
              !d->joint_limits_certified && d->examined_nodes == 0 &&
              d->leaves.empty() && d->first_refusal &&
              d->first_refusal->condition ==
                  BoardingPlantedLegCondition::unsupported_arithmetic &&
              d->first_refusal->limiting_condition ==
                  BoardingPlantedLegCondition::unsupported_arithmetic,
          "Unsupported public arithmetic refuses before any node with no "
          "summary success");
}
auto arithmetic_environment() -> void {
  for (const auto mode : std::array{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    SavedEnvironment saved;
    check(std::fesetround(mode) == 0,
          "Select standard directed rounding adversary");
    denied_environment();
  }
#if defined(__SSE2__) && defined(__x86_64__)
  struct SavedControl {
    unsigned original{_mm_getcsr()};
    ~SavedControl() { _mm_setcsr(original); }
  };
  for (const auto flags :
       std::array{1U << 15, 1U << 6, (1U << 15) | (1U << 6)}) {
    SavedEnvironment environment;
    SavedControl control;
    _mm_setcsr(control.original | flags);
    denied_environment();
  }
#endif
#if defined(__SSE2__) && defined(__x86_64__)
  // x86-64 uses SSE for scalar double operations. Alter only its RC field,
  // independently of the x87/environment API, and restore both saved states.
  for (const auto rounding : std::array{1U, 2U, 3U}) {
    SavedEnvironment environment;
    SavedControl control;
    _mm_setcsr((control.original & ~(3U << 13)) | (rounding << 13));
    denied_environment();
  }
#endif
  check(std::fegetround() == FE_TONEAREST,
        "Directed rounding controls restore nearest before oracle/fixture "
        "checks");
}
} // namespace
int main() {
  try {
    arithmetic_environment();
    public_intervals();
    private_numeric();
    bounded_workspace();
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << "FAIL: " << error.what() << '\n';
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
