#include "apsis_drift/origin_boarding_world_checkpoint.hpp"
#include "origin_boarding_world_checkpoint_internal.hpp"
#include "origin_lower_cockpit_contact_internal.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using namespace apsis_drift;
std::size_t checks{};
int failures{};
constexpr std::uint64_t effective_count{399187 - 456 + 8100 + 1508};
auto check(bool value, std::string_view label) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
template <class T, class E> auto require(std::expected<T, E> value) -> T {
  if (!value)
    throw std::runtime_error("Required selected world fixture refused");
  return std::move(*value);
}
struct Snapshot {
  std::vector<std::uint64_t> words;
  auto number(double x) -> void {
    words.push_back(std::bit_cast<std::uint64_t>(x));
  }
  template <class T> auto integer(T x) -> void {
    words.push_back(static_cast<std::uint64_t>(x));
  }
  auto point(RigidVector3 p) -> void {
    number(p.x);
    number(p.y);
    number(p.z);
  }
  auto frame(const BoardingBodyFrame& f) -> void {
    for (auto c : f.columns)
      point(c);
  }
  auto shape(const BoardingBodyBox& s) -> void {
    point(s.center_metres);
    point(s.half_size_metres);
    frame(s.frame);
  }
  auto shape(const BoardingBodyCapsule& s) -> void {
    point(s.start_metres);
    point(s.end_metres);
    number(s.radius_metres);
  }
  auto shape(const BoardingSelfEllipsoid& s) -> void {
    point(s.center_metres);
    point(s.semi_axes_metres);
    frame(s.frame);
  }
  auto shape(const BoardingSelfSphere& s) -> void {
    point(s.center_metres);
    number(s.radius_metres);
  }
  auto shape(const BoardingSelfEllipsoidCap& s) -> void {
    shape(s.ellipsoid);
    point(s.axial_origin_metres);
    frame(s.axial_frame);
    number(s.axial_limit_metres);
  }
  auto shape(const BoardingSelfAxialSlab03& s) -> void {
    shape(s.original_capsule);
    point(s.root_metres);
    point(s.toward_metres);
    number(s.axial_limit_metres);
  }
  template <class... T> auto variant(const std::variant<T...>& s) -> void {
    integer(s.index());
    std::visit([&](const auto& v) { shape(v); }, s);
  }
};
auto local_snapshot(const BoardingSelfDiagnostic04& self)
    -> std::vector<std::uint64_t> {
  Snapshot out;
  const auto& body = self.canonical;
  out.integer(self.self_model_version);
  out.integer(body.policy_version);
  for (const auto& p : body.parts) {
    out.integer(p.id);
    out.integer(p.mass_weight);
    out.point(p.mass_point_metres);
    out.variant(p.world_reservation);
  }
  for (const auto& j : body.joints) {
    for (auto p :
         {j.hip_metres, j.knee_metres, j.ankle_metres, j.sole_origin_metres,
          j.shoulder_metres, j.elbow_metres, j.wrist_metres})
      out.point(p);
    out.number(j.required_ankle_pitch_degrees);
    out.number(j.required_ankle_roll_degrees);
  }
  for (const auto& p : body.pairs) {
    out.integer(p.first);
    out.integer(p.second);
    out.integer(p.kind);
    out.integer(p.intersection);
    out.integer(p.witness.has_value());
    if (p.witness) {
      out.point(p.witness->point_metres);
      out.number(p.witness->first_depth_metres);
      out.number(p.witness->second_depth_metres);
    }
  }
  out.point(body.eye_metres);
  out.point(body.center_of_mass_metres);
  out.integer(body.self_conflict);
  out.integer(body.unresolved_intersection);
  for (const auto& p : self.self_parts) {
    out.integer(p.id);
    out.variant(p.solid);
  }
  for (const auto& r : self.regions) {
    out.integer(r.first);
    out.integer(r.second);
    out.integer(r.junction);
    out.variant(r.region);
  }
  for (const auto& p : self.pairs) {
    out.integer(p.first);
    out.integer(p.second);
    out.integer(p.connected_region_index.has_value());
    if (p.connected_region_index) out.integer(*p.connected_region_index);
    out.integer(p.outcome);
    out.integer(p.certificate);
    out.integer(p.reason);
    out.integer(p.strict_witness_metres.has_value());
    if (p.strict_witness_metres) out.point(*p.strict_witness_metres);
  }
  out.integer(self.self_checkpoint_passed);
  out.integer(self.strict_conflict);
  out.integer(self.unresolved_pair);
  return out.words;
}
auto pose(double direction, double z, bool dual = false) -> BoardingBodyPose {
  BoardingBodyPose result;
  result.policy_version = 2;
  result.hip_metres.z = z;
  for (auto& side : result.sides) {
    side.shoulder_flex_degrees = 45;
    side.elbow_flex_degrees = 135;
  }
  if (!dual) {
    result.hip_metres.y =
        .1 + (.47285 + .47478) * std::cos(10 * std::numbers::pi / 180);
    result.sides[0].hip_abduction_degrees = -10 * direction;
    result.sides[1].hip_abduction_degrees = 10 * direction;
    const std::size_t lifted = direction == 1 ? 1 : 0;
    result.sides[lifted].hip_flex_degrees = 30;
    result.sides[lifted].knee_flex_degrees = 30;
  }
  return result;
}
auto request(double direction, double z, bool dual = false)
    -> BoardingBootSupportRequest {
  return {pose(direction, z, dual),
          dual || direction == 1 ? std::size_t{0} : std::size_t{1},
          z == -.8 ? std::size_t{9} : std::size_t{8},
          dual             ? .5
          : direction == 1 ? 1.
                           : 0.};
}

auto boot_snapshot(const BoardingBootSupportDiagnostic& d)
    -> std::vector<std::uint64_t> {
  Snapshot out;
  out.words = local_snapshot(d.local_self);
  for (const auto x : d.stance.terms)
    out.number(x);
  out.integer(d.stance.anchor_side);
  out.integer(d.stance.anchor_partition);
  out.number(d.stance.reporting_metres);
  for (const auto p : d.pressure_witness.canonical_boot_centers)
    out.point(p);
  out.point(d.pressure_witness.canonical_center_of_mass);
  out.number(d.pressure_witness.port_fraction);
  for (const auto& b : d.boots) {
    out.integer(b.status);
    out.integer(b.loaded);
    out.integer(b.pressure_supported);
    out.integer(b.source_partition.has_value());
    if (b.source_partition) out.integer(*b.source_partition);
    for (auto relation : b.plane_relations)
      out.integer(relation);
    out.point(b.reporting_pressure_center_metres);
    for (auto p : b.pressure_center_bounds_metres)
      out.point(p);
    out.number(b.minimum_center_edge_distance_lower_metres);
    out.number(b.minimum_disk_edge_margin_lower_metres);
    out.number(b.reporting_sole_origin_bottom_difference_metres);
    for (auto x : b.sole_origin_bottom_difference_terms)
      out.number(x);
    out.integer(b.sole_origin_bottom_relation);
    out.integer(b.pieces.size());
    for (const auto& piece : b.pieces) {
      out.integer(piece.key.buffer);
      out.integer(piece.key.group);
      out.integer(piece.key.triangle);
      out.integer(piece.object);
      out.integer(piece.evaluated_source_triangle.has_value());
      if (piece.evaluated_source_triangle)
        out.integer(*piece.evaluated_source_triangle);
      out.number(piece.reporting_area_square_metres);
      out.integer(piece.reporting_polygon_metres.size());
      for (auto p : piece.reporting_polygon_metres)
        out.point(p);
    }
  }
  out.point(d.reporting_placed_center_of_mass_metres);
  out.number(d.projected_load_margin_lower_metres);
  out.integer(d.placement_nonpenetrating);
  out.integer(d.pressure_supported);
  out.integer(d.load_supported);
  out.integer(d.checkpoint_supported);
  return out.words;
}
class IntegerDyadicSum {
 public:
  auto add(double x) -> void {
    if (++terms_ > 32 || !std::isfinite(x))
      throw std::runtime_error("Integer oracle operand bound");
    const auto raw = std::bit_cast<std::uint64_t>(x);
    const auto exponent = static_cast<unsigned>((raw >> 52) & 0x7ffU);
    const auto fraction = raw & ((std::uint64_t{1} << 52) - 1);
    const auto significand =
        exponent == 0 ? fraction : fraction | (std::uint64_t{1} << 52);
    const auto shift = exponent == 0 ? 0U : exponent - 1;
    auto& magnitude = (raw >> 63) != 0 ? negative_ : positive_;
    for (unsigned bit = 0; bit < 53; ++bit) {
      if ((significand & (std::uint64_t{1} << bit)) == 0) continue;
      const auto position = shift + bit;
      auto index = static_cast<std::size_t>(position / 32);
      std::uint64_t carry = std::uint64_t{1} << (position % 32);
      while (carry != 0) {
        if (index >= magnitude.size())
          throw std::runtime_error("Integer oracle limb capacity");
        const auto sum = static_cast<std::uint64_t>(magnitude[index]) + carry;
        magnitude[index] = static_cast<std::uint32_t>(sum & 0xffffffffU);
        carry = sum >> 32;
        ++index;
      }
    }
  }
  [[nodiscard]] auto sign() const -> int {
    for (std::size_t i = positive_.size(); i > 0; --i) {
      if (positive_[i - 1] > negative_[i - 1]) return 1;
      if (positive_[i - 1] < negative_[i - 1]) return -1;
    }
    return 0;
  }

 private:
  std::array<std::uint32_t, 68> positive_{}, negative_{};
  unsigned terms_{};
};
// Independently accumulate actual affine XYZ extrema in integer dyadics.
// Products use a binary64 high part and its exact FMA residual; our finite
// canonical fixtures are normal and far from product underflow/overflow.
auto extremum_gap(const BoardingBodyPart& part,
                  const BoardingBootStanceTranslation& stance, std::size_t axis,
                  bool maximum, double endpoint) -> int {
  const auto component = [&](RigidVector3 p) {
    return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
  };
  IntegerDyadicSum sum;
  std::visit(
      [&](const auto& shape) {
        using T = std::decay_t<decltype(shape)>;
        if constexpr (std::is_same_v<T, BoardingBodyBox>) {
          sum.add(component(shape.center_metres));
          for (std::size_t i = 0; i < 3; ++i) {
            const auto a =
                (maximum ? 1. : -1.) * (i == 0   ? shape.half_size_metres.x
                                        : i == 1 ? shape.half_size_metres.y
                                                 : shape.half_size_metres.z);
            const auto b = std::abs(component(shape.frame.columns[i]));
            const auto product = a * b;
            sum.add(product);
            sum.add(std::fma(a, b, -product));
          }
        } else {
          const auto a = component(shape.start_metres),
                     b = component(shape.end_metres);
          sum.add(maximum ? std::max(a, b) : std::min(a, b));
          sum.add(maximum ? shape.radius_metres : -shape.radius_metres);
        }
      },
      part.world_reservation);
  if (axis == 1)
    for (auto t : stance.terms)
      sum.add(t);
  sum.add(-endpoint);
  return sum.sign();
}
struct Mask {
  std::uint32_t start, count;
};
constexpr std::array masks{Mask{108, 20},   Mask{2720, 108}, Mask{13940, 108},
                           Mask{14480, 20}, Mask{14500, 20}, Mask{20228, 108},
                           Mask{20336, 36}, Mask{20372, 36}};
auto masked(LowerCockpitTriangleKey key) -> bool {
  return key.buffer == LowerCockpitContactBuffer::original && key.group == 11 &&
         std::ranges::any_of(masks, [&](Mask m) {
           return key.triangle >= m.start && key.triangle - m.start < m.count;
         });
}
auto inventory(const OriginLowerCockpitContact& contact) -> void {
  std::array<std::uint64_t, 3> counts{};
  LowerCockpitTriangleKey previous{};
  bool first = true;
  const auto* catalog = contact.original_geometry()->support_catalog();
  const auto originals = catalog->objects();
  const auto groups = catalog->groups();
  std::vector<std::uint64_t> per_group(groups.size());
  std::uint64_t craft_total{}, station_total{}, removed_total{};
  for (const auto& g : groups) {
    if (g.owner == BoardingOwner::craft)
      craft_total += g.triangle_count;
    else
      station_total += g.triangle_count;
  }
  for (const auto row : masks)
    removed_total += row.count;
  check(craft_total == 399187 && station_total == 76025 &&
            craft_total + station_total == 475212 && removed_total == 456,
        "Independent catalog-owner totals distinguish craft from station");
  const auto visit = require(detail::visit_effective_lower_cockpit_contact(
      contact, [&](const detail::LowerCockpitEffectiveTriangle& t) {
        const auto ns = static_cast<std::size_t>(t.key.buffer);
        check(ns < counts.size() && t.obstacle != nullptr,
              "Borrowed finite namespace obstacle");
        if (ns >= counts.size() || !t.obstacle) return false;
        ++counts[ns];
        check(!masked(t.key),
              "Every removed source range absent from effective roster");
        check(first || ns >= static_cast<std::size_t>(previous.buffer),
              "Namespace source order");
        if (!first && previous.buffer == t.key.buffer) {
          check(t.key.group > previous.group ||
                    (t.key.group == previous.group &&
                     t.key.triangle > previous.triangle),
                "Strict ordered keys without duplicate obstacles");
        }
        if (ns == 0) {
          check(t.key.group < groups.size() &&
                    groups[t.key.group].owner == BoardingOwner::craft,
                "Station groups never enter craft checkpoint contact");
          if (t.key.group < groups.size()) ++per_group[t.key.group];
          check(t.object < originals.size(), "Original source object exists");
          if (t.object < originals.size()) {
            const auto& o = originals[t.object];
            check(o.group == t.key.group &&
                      t.key.triangle >= o.triangle_start &&
                      t.key.triangle - o.triangle_start < o.triangle_count &&
                      o.source_object == t.source_object &&
                      !t.evaluated_source_triangle,
                  "Whole original range/source-name provenance");
          }
        } else {
          const auto objects =
              ns == 1 ? contact.objects() : contact.replacement_objects();
          check(t.object < objects.size(), "Additive source object exists");
          if (t.object < objects.size()) {
            const auto& o = objects[t.object];
            const bool contained =
                t.key.triangle >= o.triangle_start &&
                t.key.triangle - o.triangle_start < o.triangle_count;
            check(t.key.group == 0 && t.key.triangle == counts[ns] - 1 &&
                      contained && o.source_object == t.source_object,
                  "Additive contiguous keys/ranges/name");
            if (contained)
              check(t.evaluated_source_triangle &&
                        *t.evaluated_source_triangle ==
                            o.evaluated_source_triangles[t.key.triangle -
                                                         o.triangle_start],
                    "Evaluated source-face namespace retained");
          }
        }
        check(t.obstacle->key.group == t.key.group &&
                  t.obstacle->key.triangle == t.key.triangle &&
                  t.obstacle->object == t.object,
              "Visitor reuses actual immutable obstacle key");
        previous = t.key;
        first = false;
        return true;
      }));
  check(visit.complete && visit.total_triangles == effective_count &&
            visit.visited_triangles == effective_count &&
            counts == std::array<std::uint64_t, 3>{398731, 8100, 1508},
        "All original-minus-eight/halo/replacement triangles exactly once");
  for (std::size_t i = 0; i < groups.size(); ++i) {
    const auto expected =
        groups[i].owner == BoardingOwner::craft
            ? groups[i].triangle_count - (i == 11 ? removed_total : 0)
            : 0;
    check(per_group[i] == expected,
          "Every complete craft group retained except exact mask; every "
          "station group excluded");
  }
  for (const auto stop :
       std::array<std::uint64_t, 4>{1, 398731, 398732, effective_count}) {
    std::uint64_t seen{};
    const auto prefix = require(detail::visit_effective_lower_cockpit_contact(
        contact, [&](const auto&) { return ++seen < stop; }));
    check(prefix.total_triangles == effective_count &&
              prefix.visited_triangles == stop && seen == stop &&
              !prefix.complete,
          "Stop includes stopping triangle, denominator remains complete");
  }
  check(!detail::visit_effective_lower_cockpit_contact(contact, {}),
        "Empty callback refuses");
}
auto coverage(const OriginLowerCockpitContact& contact) -> void {
  using Bounds = detail::CabinContactBox;
  const auto covered = [&](Bounds b) {
    return require(detail::covers_lower_cockpit_bounds(contact, b));
  };
  check(covered({{-1.05, -.2, -3.5}, {1.05, 2.97, 6.35}}),
        "Closed upper crop endpoints");
  check(covered({{-1.05, -.85, -3.5}, {1.05, -.2, -.5}}),
        "Closed lower crop endpoints");
  check(covered({{-.1, -.3, -1}, {.1, .1, -.5}}),
        "Join-straddling lower-front domain");
  check(covered({{0, -.2, 0}, {0, -.2, 0}}), "Join itself remains closed");
  check(!covered({{-.1, -.3, -1}, {.1, .1, 0}}),
        "Notch cannot be filled by crop AABB");
  const auto inf = std::numeric_limits<double>::infinity();
  check(!covered({{0, std::nextafter(-.2, -inf), 0}, {0, -.2, 0}}),
        "Nextafter below join outside lower-front domain");
  check(!covered({{0, -.3, -1}, {0, -.3, std::nextafter(-.5, inf)}}),
        "Nextafter beyond lower-front endpoint");
  check(!covered({{std::nextafter(-1.05, -inf), 0, 0}, {0, 0, 0}}),
        "Nextafter left crop edge");
  check(!covered({{0, 0, 0}, {std::nextafter(1.05, inf), 0, 0}}),
        "Nextafter right crop edge");
  check(!covered({{0, 0, 0}, {0, std::nextafter(2.97, inf), 0}}),
        "Nextafter upper crop edge");
  check(!covered({{0, std::nextafter(-.85, -inf), -1}, {0, -.5, -1}}),
        "Nextafter lower crop edge");
  check(!covered({{0, 0, std::nextafter(-3.5, -inf)}, {0, 0, 0}}),
        "Nextafter rear crop edge");
  check(!covered({{0, 0, 0}, {0, 0, std::nextafter(6.35, inf)}}),
        "Nextafter front crop edge");
  for (std::size_t axis = 0; axis < 3; ++axis) {
    Bounds b{{0, 0, 0}, {1, 1, 1}};
    auto* low = axis == 0 ? &b.low.x : axis == 1 ? &b.low.y : &b.low.z;
    *low = 2;
    check(!detail::covers_lower_cockpit_bounds(contact, b),
          "Inverted crop input refuses");
    *low = std::numeric_limits<double>::quiet_NaN();
    check(!detail::covers_lower_cockpit_bounds(contact, b),
          "Nonfinite crop input refuses");
  }
}
auto numerical_pairs() -> void {
  using Triangle = std::array<RigidVector3, 3>;
  const Triangle floor{RigidVector3{-.25, 0, -.25}, RigidVector3{.25, 0, -.25},
                       RigidVector3{0, 0, .25}};
  BoardingBootStanceTranslation t;
  const auto query = [&](const BoardingSelfSolid& s, const Triangle& triangle,
                         const BoardingBootStanceTranslation& translation) {
    const auto d =
        detail::boarding_world_checkpoint_pair(s, translation, triangle);
    check(d.axes_examined <= 16 && d.unsupported_axes <= d.axes_examined &&
              (!d.certified || d.axes_examined > 0),
          "Finite fixed axis budget/unsupported accounting");
    return d;
  };
  BoardingBodyBox box{{0, 1, 0}, {1, 1, 1}, {}};
  check(query(box, floor, t).certified,
        "Exact flat box boundary contact permitted");
  box.center_metres.y = std::nextafter(1., 2.);
  check(query(box, floor, t).certified, "Nextafter positive box/source gap");
  box.center_metres.y = std::nextafter(1., 0.);
  check(!query(box, floor, t).certified,
        "Nextafter penetration has no separating certificate");
  box.center_metres.y = 1;
  box.frame.columns[1].y = std::nextafter(1., 2.);
  check(!query(box, floor, t).certified,
        "Actual nonunit Y column forbids false exact-flat shortcut");
  box.frame = BoardingBodyFrame{};
  t.terms = {1, 0x1p-60, -1};
  t.reporting_metres = 0;
  check(query(box, floor, t).certified,
        "Positive uncancelled exact stance term");
  t.terms = {1, -0x1p-60, -1};
  check(!query(box, floor, t).certified,
        "Tiny exact negative stance not rounded into boundary permission");
  t = {};
  box.center_metres = {0, 0, 0};
  check(!query(box, floor, t).certified,
        "Triangle point strictly inside box prevents certificate");
  Triangle outside = floor;
  for (auto& p : outside)
    p.x += 4;
  check(query(box, outside, t).certified, "Obvious separated source plane");
  // Independent affine interior witness: each triangle vertex is A*q with
  // |q_i|<half_i, so no support direction can separate the triangle from A box.
  box.frame.columns = {RigidVector3{1, .25, 0}, RigidVector3{0, 1, .125},
                       RigidVector3{.125, 0, 1}};
  Triangle affine{};
  for (std::size_t i = 0; i < floor.size(); ++i) {
    const auto q = floor[i];
    affine[i] = {box.frame.columns[0].x * q.x + box.frame.columns[2].x * q.z,
                 box.frame.columns[0].y * q.x + box.frame.columns[2].y * q.z,
                 box.frame.columns[0].z * q.x + box.frame.columns[2].z * q.z};
  }
  check(!query(box, affine, t).certified,
        "Actual affine columns preserve interior witness");
  BoardingBodyCapsule sphere{{0, 3, 0}, {0, 3, 0}, 1};
  check(query(sphere, floor, t).certified,
        "Zero-length capsule uses separated sphere semantics");
  sphere.start_metres = sphere.end_metres = {0, 0, 0};
  check(!query(sphere, floor, t).certified,
        "Zero-length capsule interior sphere witness");
  BoardingBodyCapsule capsule{{-.4, 0, 0}, {.4, 0, 0}, .2};
  check(!query(capsule, floor, t).certified,
        "Triangle contains interior capsule segment point");
  const Triangle degenerate{RigidVector3{}, RigidVector3{}, RigidVector3{}};
  const auto zero = query(capsule, degenerate, t);
  check(!zero.certified && zero.unsupported_axes > 0,
        "Zero proposals skipped, never grant clearance");
  const auto denied = [&](const BoardingSelfSolid& shape, const Triangle& tr,
                          const BoardingBootStanceTranslation& shift) {
    const auto d = query(shape, tr, shift);
    check(!d.certified && d.axes_examined == 0 && d.unsupported_axes == 0,
          "Malformed private inputs refuse before axis evaluation");
  };
  const auto inf = std::numeric_limits<double>::infinity();
  box.frame = {};
  box.center_metres = {0, 0, 0};
  for (const auto bad :
       std::array{inf, std::numeric_limits<double>::quiet_NaN()}) {
    auto b = box;
    b.center_metres.x = bad;
    denied(b, floor, t);
    auto tr = floor;
    tr[0].x = bad;
    denied(box, tr, t);
    auto shift = t;
    shift.terms[0] = bad;
    denied(box, floor, shift);
  }
  box.half_size_metres.x = -1;
  denied(box, floor, t);
  capsule.radius_metres = -1;
  denied(capsule, floor, t);
  const BoardingSelfEllipsoid ellipsoid{{0, 3, 0}, {1, 1, 1}, {}};
  denied(ellipsoid, floor, t);
}
auto diagnostic_checks(const OriginBoardingBootSupport& provider,
                       const BoardingBootSupportRequest& input, bool frozen)
    -> BoardingWorldCheckpointDiagnostic {
  const auto expected =
      require(assess_origin_boarding_boot_support(provider, input));
  auto d = require(assess_origin_boarding_world_checkpoint(provider, input));
  check(boot_snapshot(d.boot_support) == boot_snapshot(expected),
        "Fresh retained boot/self/T evidence bit unchanged");
  check(d.effective_triangle_count == effective_count &&
            d.expected_pairs == 15 * effective_count,
        "Complete source denominator and checked canonical part count");
  check(!d.volume_qualified && !d.arbitrary_pose_qualified &&
            !d.sweep_qualified && !d.route_qualified && !d.actor_qualified &&
            !d.seat_qualified,
        "Surface checkpoint has no extra authority");
  check(d.certified_pairs <= d.examined_pairs &&
            d.examined_pairs <= d.expected_pairs,
        "Truthful bounded examined/certified prefix");
  if (frozen)
    check(d.boot_support.checkpoint_supported && d.checkpoint_supported &&
              d.coverage_complete && d.comparisons_complete &&
              d.nonpenetrating && !d.first_refusal &&
              d.examined_pairs == 6125085 && d.certified_pairs == 6125085,
          "Frozen six actual support/world positives retain all 6125085 "
          "certificates");
  const auto* contact = provider.contact();
  std::size_t coverage_examined{};
  bool stopped = false;
  for (std::size_t i = 0; i < d.coverage.size(); ++i) {
    const auto& c = d.coverage[i];
    check(c.part == d.boot_support.local_self.canonical.parts[i].id,
          "Coverage canonical part order");
    check(!stopped || !c.examined,
          "Coverage stops at first prerequisite refusal");
    if (!c.examined) {
      stopped = true;
      continue;
    }
    ++coverage_examined;
    if (c.arithmetic_supported) {
      const auto covered = require(detail::covers_lower_cockpit_bounds(
          *contact, {c.lower_metres, c.upper_metres}));
      check(c.covered == covered,
            "Reported outward bound coverage matches unchanged union");
      const auto& part = d.boot_support.local_self.canonical.parts[i];
      const std::array lower{c.lower_metres.x, c.lower_metres.y,
                             c.lower_metres.z};
      const std::array upper{c.upper_metres.x, c.upper_metres.y,
                             c.upper_metres.z};
      for (std::size_t axis = 0; axis < 3; ++axis)
        check(extremum_gap(part, d.boot_support.stance, axis, false,
                           lower[axis]) >= 0 &&
                  extremum_gap(part, d.boot_support.stance, axis, true,
                               upper[axis]) <= 0,
              "Exact affine/capsule extrema enclosed by reported coverage "
              "bounds");
    } else
      check(!c.covered, "Unsupported crop arithmetic cannot count covered");
  }
  if (d.checkpoint_supported) {
    check(d.boot_support.checkpoint_supported && d.coverage_complete &&
              d.comparisons_complete && d.nonpenetrating && !d.first_refusal &&
              d.examined_pairs == d.expected_pairs &&
              d.certified_pairs == d.expected_pairs,
          "Complete support/world requires every source pair");
  } else {
    check(!d.comparisons_complete && !d.nonpenetrating && d.first_refusal,
          "Incomplete checkpoint never claims nonpenetration");
    if (d.first_refusal) {
      const auto& r = *d.first_refusal;
      if (r.reason ==
          BoardingWorldCheckpointRefusal::no_separation_certificate) {
        check(d.coverage_complete && coverage_examined == 15 && r.key &&
                  d.examined_pairs == d.certified_pairs + 1 &&
                  r.axes_examined >= 1 && r.axes_examined <= 16 &&
                  r.unsupported_axes <= r.axes_examined,
              "Pair refusal preserves exactly one unresolved attempted pair");
        if (r.key) {
          const auto face =
              require(lookup_lower_cockpit_face(*contact, *r.key));
          check(face.object == r.object &&
                    face.source_object == r.source_object &&
                    face.evaluated_source_triangle ==
                        r.evaluated_source_triangle,
                "Owned refusal provenance matches selected source face");
          std::uint64_t offset{};
          bool found = false;
          const auto visit =
              require(detail::visit_effective_lower_cockpit_contact(
                  *contact, [&](const auto& triangle) {
                    if (triangle.key == *r.key) {
                      found = true;
                      return false;
                    }
                    ++offset;
                    return true;
                  }));
          check(found && !visit.complete,
                "Refusal key belongs to effective unmasked roster");
          const auto& parts = d.boot_support.local_self.canonical.parts;
          const auto part = std::ranges::find_if(
              parts, [&](const auto& p) { return p.id == r.part; });
          check(part != parts.end(), "Refusal names actual canonical part");
          if (part != parts.end())
            check(d.certified_pairs ==
                      static_cast<std::uint64_t>(part - parts.begin()) *
                              effective_count +
                          offset,
                  "Exact part-major/source-major refusal prefix");
        }
      } else {
        check(d.examined_pairs == 0 && d.certified_pairs == 0 && !r.key &&
                  r.axes_examined == 0 && r.unsupported_axes == 0 &&
                  !d.coverage_complete,
              "Failed prerequisite attempts no source comparison");
        if (r.reason == BoardingWorldCheckpointRefusal::boot_self_prerequisite)
          check(!d.boot_support.checkpoint_supported && coverage_examined == 0,
                "Boot/self refusal does not fabricate crop evidence");
      }
    }
  }
  std::cout << "world x=" << input.pose.hip_metres.x
            << " z=" << input.pose.hip_metres.z
            << " anchor=" << input.anchor_side
            << " w=" << input.port_load_fraction
            << " supported=" << d.checkpoint_supported
            << " examined=" << d.examined_pairs
            << " certified=" << d.certified_pairs << '/' << d.expected_pairs;
  if (d.first_refusal) {
    const auto& r = *d.first_refusal;
    std::cout << " refusal=" << static_cast<int>(r.reason)
              << " part=" << static_cast<int>(r.part)
              << " source=" << r.source_object;
    if (r.key)
      std::cout << " key=" << static_cast<int>(r.key->buffer) << ':'
                << r.key->group << ':' << r.key->triangle;
  }
  std::cout << '\n';
  return d;
}
struct RegisteredLateralOutcome {
  bool supported{};
  std::uint64_t examined{};
  BoardingBodyPartId part{};
  std::uint32_t source_triangle{};
  std::string_view source_object;
};
constexpr std::array lateral_outcomes{
    RegisteredLateralOutcome{true, 6125085, {}, 0, {}},
    RegisteredLateralOutcome{false, 4699741, BoardingBodyPartId::starboard_boot,
                             208011, "WF02 | deck flush pull 7"},
    RegisteredLateralOutcome{false, 2247209, BoardingBodyPartId::port_boot,
                             205513, "WF02 | deck bay index 7"},
    RegisteredLateralOutcome{false, 4699793, BoardingBodyPartId::starboard_boot,
                             208063, "WF02 | deck flush pull 7"},
    RegisteredLateralOutcome{false, 4697243, BoardingBodyPartId::starboard_boot,
                             205513, "WF02 | deck bay index 7"},
    RegisteredLateralOutcome{false, 2249759, BoardingBodyPartId::port_boot,
                             208063, "WF02 | deck flush pull 7"}};
auto registered_lateral_checks(const BoardingWorldCheckpointDiagnostic& d,
                               const RegisteredLateralOutcome& expected)
    -> void {
  check(d.boot_support.checkpoint_supported &&
            d.boot_support.local_self.self_checkpoint_passed &&
            d.coverage_complete,
        "Registered lateral cases retain boot/self/source-crop prerequisites");
  check(d.checkpoint_supported == expected.supported &&
            d.comparisons_complete == expected.supported &&
            d.nonpenetrating == expected.supported &&
            d.examined_pairs == expected.examined &&
            d.certified_pairs ==
                expected.examined - (expected.supported ? 0 : 1),
        "Registered observed lateral completion or exact unresolved prefix "
        "remains fixed");
  if (expected.supported) {
    check(!d.first_refusal && d.certified_pairs == d.expected_pairs,
          "Registered negative-X .30 observation remains complete world "
          "clearance");
  } else {
    check(d.first_refusal.has_value(),
          "Recorded unresolved source pair present");
    if (d.first_refusal) {
      const auto& r = *d.first_refusal;
      check(r.reason ==
                    BoardingWorldCheckpointRefusal::no_separation_certificate &&
                r.part == expected.part &&
                r.key ==
                    LowerCockpitTriangleKey{LowerCockpitContactBuffer::original,
                                            0, expected.source_triangle} &&
                r.source_object == expected.source_object &&
                !r.evaluated_source_triangle,
            "Exact registered part/source namespace/key/name refusal is not "
            "generic collision proof");
    }
  }
}
auto public_controls(const OriginBoardingBootSupport& provider) -> void {
  for (const auto z : std::array{-.35, -.8}) {
    for (const auto direction : std::array{-1., 1.})
      (void)diagnostic_checks(provider, request(direction, z), true);
    (void)diagnostic_checks(provider, request(1, z, true), true);
  }
  // Registered separately before any query (44a641d). These are finite
  // observations, not assumed collisions; frozen six above remain unchanged.
  std::size_t observed_index{};
  for (const auto x : std::array{-.30, .30, -.45, .45}) {
    auto lateral = request(1, -.35, true);
    lateral.pose.hip_metres.x = x;
    const auto observed = diagnostic_checks(provider, lateral, false);
    registered_lateral_checks(observed, lateral_outcomes[observed_index++]);
  }
  for (const auto direction : std::array{-1., 1.}) {
    auto lateral = request(direction, -.35);
    lateral.pose.hip_metres.x = direction * .58;
    const auto observed = diagnostic_checks(provider, lateral, false);
    registered_lateral_checks(observed, lateral_outcomes[observed_index++]);
  }
  auto straight = request(1, -.35);
  for (auto& s : straight.pose.sides) {
    s.shoulder_flex_degrees = 0;
    s.elbow_flex_degrees = 0;
  }
  const auto self = diagnostic_checks(provider, straight, false);
  check(self.first_refusal &&
            self.first_refusal->reason ==
                BoardingWorldCheckpointRefusal::boot_self_prerequisite,
        "Strict self refusal never launches world comparisons");
  auto wrong = request(1, -.35, true);
  wrong.anchor_partition = 9;
  const auto support = diagnostic_checks(provider, wrong, false);
  check(!support.boot_support.checkpoint_supported &&
            support.examined_pairs == 0,
        "Wrong actual source height blocks world comparison");
  for (std::size_t kind = 0; kind < 9; ++kind) {
    auto bad = request(1, -.35);
    switch (kind) {
      case 0: bad.pose.policy_version = 1; break;
      case 1: bad.pose.policy_version = 999; break;
      case 2: bad.anchor_side = 2; break;
      case 3: bad.anchor_partition = 10; break;
      case 4: bad.port_load_fraction = -.01; break;
      case 5: bad.port_load_fraction = 1.01; break;
      case 6:
        bad.port_load_fraction = std::numeric_limits<double>::quiet_NaN();
        break;
      case 7:
        bad.pose.hip_metres.x = std::numeric_limits<double>::infinity();
        break;
      case 8: bad.pose.sides[0].hip_abduction_degrees = 36; break;
    }
    check(!assess_origin_boarding_world_checkpoint(provider, bad),
          "Malformed request refuses without diagnostic");
  }
}
auto lifetime() -> void {
  auto provider = [] {
    auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    return require(make_origin_boarding_boot_support(binding));
  }();
  auto copy = provider;
  const auto* contact = copy.contact();
  const auto* name = &contact->replacement_objects()[0].source_object;
  auto moved = std::move(provider);
  // NOLINTBEGIN(bugprone-use-after-move,clang-analyzer-cplusplus.Move) -- Test
  // the documented empty moved-handle API.
  check(
      !provider.contact() && provider.selected_partitions().empty() &&
          !assess_origin_boarding_world_checkpoint(provider, request(1, -.35)),
      "Moved-from provider refuses");
  // NOLINTEND(bugprone-use-after-move,clang-analyzer-cplusplus.Move) -- End
  // moved-handle API test.
  check(copy.contact() == moved.contact() && !name->empty(),
        "Sharing contact/name survives caller binding lifetime");
  auto lower = *copy.contact();
  auto lower_moved = std::move(lower);
  // NOLINTBEGIN(bugprone-use-after-move) -- Test the documented empty
  // moved-handle API.
  check(!detail::covers_lower_cockpit_bounds(lower, {{0, 0, 0}, {0, 0, 0}}) &&
            !detail::visit_effective_lower_cockpit_contact(
                lower, [](const auto&) { return true; }),
        "Moved-from effective provider rejects both private queries");
  // NOLINTEND(bugprone-use-after-move) -- End moved-handle API test.
  check(require(detail::covers_lower_cockpit_bounds(lower_moved,
                                                    {{0, 0, 0}, {0, 0, 0}})),
        "Moved-to selected contact remains usable");
  // Refusal source strings and complete canonical snapshots belong to the
  // returned diagnostic, not to views in the caller's provider.
  static_assert(
      std::is_same_v<decltype(BoardingWorldRefusalEvidence{}.source_object),
                     std::string>);
  auto owned = [] {
    auto temporary_binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    auto temporary_provider =
        require(make_origin_boarding_boot_support(temporary_binding));
    auto input = request(1, -.35, true);
    input.pose.hip_metres.x = .30;
    return diagnostic_checks(temporary_provider, input, false);
  }();
  // Both creating handles above are destroyed before any returned source-name
  // or canonical data check. The refusal explicitly owns its string.
  registered_lateral_checks(owned, lateral_outcomes[1]);
  const auto snapshot = boot_snapshot(owned.boot_support);
  const auto refusal_name =
      owned.first_refusal ? owned.first_refusal->source_object : std::string{};
  copy = std::move(moved);
  check(boot_snapshot(owned.boot_support) == snapshot &&
            (!owned.first_refusal ||
             owned.first_refusal->source_object == refusal_name),
        "Returned diagnostic owns retained data");
}
} // namespace
int main() {
  try {
    const auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    const auto provider = require(make_origin_boarding_boot_support(binding));
    check(!make_origin_boarding_boot_support(NativeCraftBinding{}),
          "Legacy binding has no selected checkpoint contact");
    inventory(*provider.contact());
    coverage(*provider.contact());
    numerical_pairs();
    public_controls(provider);
    lifetime();
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << "FAIL: " << error.what() << '\n';
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
