#include "apsis_drift/origin_boarding_boot_support.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

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
    throw std::runtime_error("Required public boot-support fixture refused");
  return std::move(*value);
}
auto bits(double a, double b) -> bool {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
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
struct Point {
  long double x, z;
};
auto dot(Point a, Point b) -> long double {
  return a.x * b.x + a.z * b.z;
}
// Independent complete half-plane clipping, never an AABB or sampled circle.
auto clip(std::vector<Point> polygon, Point normal, long double threshold)
    -> std::vector<Point> {
  std::vector<Point> result;
  if (polygon.empty()) return result;
  auto previous = polygon.back();
  auto previous_gap = dot(previous, normal) - threshold;
  for (auto current : polygon) {
    const auto gap = dot(current, normal) - threshold;
    if ((gap >= 0) != (previous_gap >= 0)) {
      const auto t = previous_gap / (previous_gap - gap);
      result.push_back({previous.x + t * (current.x - previous.x),
                        previous.z + t * (current.z - previous.z)});
    }
    if (gap >= 0) result.push_back(current);
    previous = current;
    previous_gap = gap;
  }
  return result;
}
auto triangle_area(const std::array<RigidVector3, 3>& triangle,
                   const BoardingBodyBox& boot) -> long double {
  std::vector<Point> polygon;
  polygon.reserve(triangle.size());
  for (auto p : triangle)
    polygon.push_back({p.x, p.z});
  const auto u = boot.frame.columns[0], v = boot.frame.columns[2];
  const long double determinant =
      static_cast<long double>(u.x) * v.z - static_cast<long double>(u.z) * v.x;
  if (determinant <= 0)
    throw std::runtime_error("Oracle nonpositive flat frame determinant");
  const std::array<Point, 2> normals{Point{v.z, -v.x}, Point{-u.z, u.x}};
  for (std::size_t axis = 0; axis < 2; ++axis) {
    const auto n = normals[axis];
    const Point center{boot.center_metres.x, boot.center_metres.z};
    const long double half =
        (axis == 0 ? boot.half_size_metres.x : boot.half_size_metres.z) *
        determinant;
    polygon = clip(std::move(polygon), n, dot(center, n) - half);
    polygon = clip(std::move(polygon), {-n.x, -n.z}, -dot(center, n) - half);
  }
  long double area{};
  if (!polygon.empty()) {
    auto previous = polygon.back();
    for (auto p : polygon) {
      area += previous.x * p.z - p.x * previous.z;
      previous = p;
    }
  }
  return std::abs(area) / 2;
}
// Independent integer dyadics in units2^-1074. Separate unsigned magnitude
// sums avoid floating cancellation. At most32 finite binary64 operands fit
// in68 base2^32 limbs, including the largest finite exponent and carry bits.
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
auto integer_sign(std::initializer_list<double> terms) -> int {
  IntegerDyadicSum sum;
  for (double x : terms)
    sum.add(x);
  return sum.sign();
}
auto binary_scale(double x, double factor) -> double {
  if (factor == 0) return std::copysign(0., x);
  if (factor == 1) return x;
  if (factor != .5)
    throw std::runtime_error("Fixture fraction is not binary scaling");
  const auto half = std::ldexp(x, -1);
  if (!bits(std::ldexp(half, 1), x))
    throw std::runtime_error("Fixture half product loses dyadic bits");
  return half;
}
auto integer_oracle_controls() -> void {
  const auto tiny = std::numeric_limits<double>::denorm_min();
  const auto maximum = std::numeric_limits<double>::max();
  check(integer_sign({1., tiny, -1.}) == 1 &&
            integer_sign({1., -tiny, -1.}) == -1,
        "Integer oracle distinguishes subnormal residual across full exponent "
        "span");
  check(integer_sign({maximum, -maximum, tiny}) == 1 &&
            integer_sign({maximum, -maximum, -tiny}) == -1,
        "Integer oracle cancellation retains largest/smallest stored values");
  check(integer_sign({-.0, 0.}) == 0 && integer_sign({.5, .25, -.75}) == 0,
        "Integer oracle exact boundaries retain signed-zero equality");
  check(integer_sign({std::nextafter(1., 2.), -1.}) == 1 &&
            integer_sign({std::nextafter(1., 0.), -1.}) == -1,
        "Integer oracle distinguishes adjacent representable signs");
}
// The finite fixture operands occupy fewer than64 binary places, including
// carry headroom. This wider sum is exact for these stored dyadics; it is not
// a production tolerance or an oracle for arbitrary scalar ranges.
auto exact_sum(std::initializer_list<double> terms) -> long double {
  int low = 2048, high = -2048;
  for (double x : terms) {
    if (x == 0) continue;
    const auto raw = std::bit_cast<std::uint64_t>(x);
    const auto exponent = static_cast<int>((raw >> 52) & 0x7ffU);
    check(exponent > 0 && exponent < 2047,
          "Exact-sum oracle uses finite normal fixture operands");
    if (exponent == 0 || exponent == 2047)
      throw std::runtime_error("Unsupported test sum operand");
    const auto significand =
        (raw & ((std::uint64_t{1} << 52) - 1)) | (std::uint64_t{1} << 52);
    low = std::min(low, exponent - 1023 - 52 +
                            static_cast<int>(std::countr_zero(significand)));
    high = std::max(high, exponent - 1023);
  }
  if (high >= low && high - low + 4 > std::numeric_limits<long double>::digits)
    throw std::runtime_error("Fixture exceeds independent exact-sum precision");
  long double sum{};
  for (double x : terms)
    sum += static_cast<long double>(x);
  return sum;
}
auto relation(long double gap) -> BoardingBootPlaneRelation {
  if (gap < 0) return BoardingBootPlaneRelation::below;
  if (gap > 0) return BoardingBootPlaneRelation::above;
  return BoardingBootPlaneRelation::equal;
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
auto source_checks(const OriginBoardingBootSupport& provider) -> void {
  constexpr std::array<std::uint32_t, 10> first{
      85129, 85537, 85945, 86353, 86761, 87211, 87219, 87645, 88053, 62119};
  constexpr std::array<std::uint32_t, 10> objects{178, 181, 184, 187, 190,
                                                  193, 193, 196, 199, 124};
  constexpr std::array<std::string_view, 10> names{
      "CABIN | lift-out walking tile 00-1",
      "CABIN | lift-out walking tile 01-1",
      "CABIN | lift-out walking tile 02-1",
      "CABIN | lift-out walking tile 03-1",
      "CABIN | lift-out walking tile 04-1",
      "CABIN | lift-out walking tile 05-1",
      "CABIN | lift-out walking tile 05-1",
      "CABIN | lift-out walking tile 06-1",
      "CABIN | lift-out walking tile 07-1",
      "CABIN | cockpit transition step"};
  const auto parts = provider.selected_partitions();
  check(parts.size() == 10 && provider.contact() != nullptr,
        "Exact finite selected source roster");
  if (parts.size() != 10 || !provider.contact())
    throw std::runtime_error("Missing source selection");
  check(provider.contact()->stowed_partition() != nullptr &&
            provider.contact()->replacement_objects().size() == 13,
        "Provider retains actual effective stowed contact selection");
  for (std::size_t i = 0; i < parts.size(); ++i) {
    check(
        bits(parts[i].plane_metres, i == 9 ? -.16 : kCabinCorridorFloorMetres),
        "Exact registered source planes");
    for (std::size_t j = 0; j < 2; ++j) {
      const auto& face = parts[i].faces[j];
      const LowerCockpitTriangleKey expected{
          LowerCockpitContactBuffer::original, 0,
          first[i] + static_cast<std::uint32_t>(j)};
      check(face.key == expected && face.object == objects[i] &&
                face.source_object == names[i] &&
                !face.evaluated_source_triangle,
            "Original keys/object roles never become "
            "halo/replacement/evaluated IDs");
      const auto original =
          require(lookup_lower_cockpit_face(*provider.contact(), expected));
      check(original.points_current_metres == face.points_current_metres &&
                original.unit_normal_current == face.unit_normal_current,
            "Selected faces equal actual effective immutable contact lookup");
      for (auto p : face.points_current_metres)
        check(bits(p.y, parts[i].plane_metres),
              "Full horizontal source face plane identity");
      check(face.unit_normal_current.y > 0,
            "Registered top winding faces upward");
    }
  }
  constexpr std::array<RigidVector3, 4> transition{{{.498841, -.16, -.988},
                                                    {-.498841, -.16, -.988},
                                                    {-.500881, -.16, -.612},
                                                    {.500881, -.16, -.612}}};
  for (auto expected : transition)
    check(std::ranges::any_of(parts[9].perimeter_metres,
                              [&](auto p) { return p == expected; }),
          "Complete genuine transition trapezoid corners");
}
auto placement_checks(const OriginBoardingBootSupport& provider,
                      const BoardingBootSupportRequest& input,
                      const BoardingBootSupportDiagnostic& result) -> void {
  const auto expected =
      require(evaluate_origin_boarding_self_model04(input.pose));
  check(local_snapshot(result.local_self) == local_snapshot(expected),
        "Source stance never modifies any local body/self bit");
  const auto& body = result.local_self.canonical;
  const auto& anchor = std::get<BoardingBodyBox>(
      body.parts[5 + input.anchor_side * 6].world_reservation);
  const auto parts = provider.selected_partitions();
  const auto& t = result.stance.terms;
  check(result.stance.anchor_side == input.anchor_side &&
            result.stance.anchor_partition == input.anchor_partition,
        "Exact stance names the independently requested anchor side/component");
  check(bits(t[0], parts[input.anchor_partition].plane_metres) &&
            bits(t[1], -anchor.center_metres.y) &&
            bits(t[2], anchor.half_size_metres.y),
        "One global stance contains exact source/-anchorcenter/+half terms");
  check(exact_sum({anchor.center_metres.y, -anchor.half_size_metres.y, t[0],
                   t[1], t[2], -parts[input.anchor_partition].plane_metres}) ==
            0,
        "Actual anchor box bottom has exact source-plane identity");
  const auto displacement = exact_sum({t[0], t[1], t[2]});
  // Every point uses the identical four-term placement, without re-evaluating
  // body joints, foot snapping or accepting rounded reporting translation.
  auto point = [&](RigidVector3 p) {
    const auto placed = exact_sum({p.y, t[0], t[1], t[2]});
    check(placed - static_cast<long double>(p.y) == displacement,
          "Uniform exact stance translates every local point identically");
  };
  for (const auto& p : body.parts) {
    point(p.mass_point_metres);
    std::visit(
        [&](const auto& s) {
          using T = std::decay_t<decltype(s)>;
          if constexpr (std::is_same_v<T, BoardingBodyBox>)
            point(s.center_metres);
          else {
            point(s.start_metres);
            point(s.end_metres);
          }
        },
        p.world_reservation);
  }
  for (const auto& j : body.joints)
    for (auto p :
         {j.hip_metres, j.knee_metres, j.ankle_metres, j.sole_origin_metres,
          j.shoulder_metres, j.elbow_metres, j.wrist_metres})
      point(p);
  point(body.eye_metres);
  point(body.center_of_mass_metres);
  const auto reported = result.reporting_placed_center_of_mass_metres;
  check(bits(reported.x, body.center_of_mass_metres.x) &&
            bits(reported.z, body.center_of_mass_metres.z) &&
            std::abs(static_cast<long double>(reported.y) -
                     exact_sum({body.center_of_mass_metres.y, t[0], t[1],
                                t[2]})) < 2e-15L,
        "Display COM rounds same translation without replacing authority");
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& boot =
        std::get<BoardingBodyBox>(body.parts[5 + side * 6].world_reservation);
    for (std::size_t i = 0; i < parts.size(); ++i) {
      const auto gap =
          exact_sum({boot.center_metres.y, -boot.half_size_metres.y, t[0], t[1],
                     t[2], -parts[i].plane_metres});
      check(result.boots[side].plane_relations[i] == relation(gap),
            "Every loaded/unloaded source-plane sign equals independent exact "
            "dyadic sum");
    }
    const auto discrepancy =
        exact_sum({body.joints[side].sole_origin_metres.y,
                   -boot.center_metres.y, boot.half_size_metres.y});
    const auto& discrepancy_terms =
        result.boots[side].sole_origin_bottom_difference_terms;
    check(bits(discrepancy_terms[0], body.joints[side].sole_origin_metres.y) &&
              bits(discrepancy_terms[1], -boot.center_metres.y) &&
              bits(discrepancy_terms[2], boot.half_size_metres.y) &&
              result.boots[side].sole_origin_bottom_relation ==
                  relation(discrepancy),
          "Exact diagnostic sole/core discrepancy retains all operands and "
          "strict sign");
    check(
        std::abs(static_cast<long double>(
                     result.boots[side]
                         .reporting_sole_origin_bottom_difference_metres) -
                 discrepancy) < 2e-15L,
        "Separately rounded diagnostic sole cannot override actual box bottom");
  }
  const auto& w = result.pressure_witness;
  check(bits(w.port_fraction, input.port_load_fraction) &&
            w.canonical_center_of_mass == body.center_of_mass_metres,
        "Pressure witness binds selected fraction and actual canonical COM");
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& boot =
        std::get<BoardingBodyBox>(body.parts[5 + side * 6].world_reservation);
    check(w.canonical_boot_centers[side] == boot.center_metres,
          "Pressure expressions bind actual canonical boot centers");
  }
}
auto contact_checks(const OriginBoardingBootSupport& provider,
                    const BoardingBootSupportDiagnostic& result,
                    std::size_t side) -> void {
  const auto& evidence = result.boots[side];
  check(evidence.loaded && evidence.pressure_supported &&
            evidence.status == BoardingBootSupportStatus::supported &&
            evidence.source_partition.has_value(),
        "Positive foot requires source contact and full finite disk evidence");
  if (!evidence.source_partition)
    throw std::runtime_error("Missing positive partition");
  const auto& partition =
      provider.selected_partitions()[*evidence.source_partition];
  const auto& boot = std::get<BoardingBodyBox>(
      result.local_self.canonical.parts[5 + side * 6].world_reservation);
  long double expected_area{}, reported_area{};
  bool positive_piece{};
  for (const auto& face : partition.faces)
    expected_area += triangle_area(face.points_current_metres, boot);
  for (const auto& piece : evidence.pieces) {
    check(std::ranges::any_of(partition.faces,
                              [&](const auto& f) {
                                return f.key == piece.key &&
                                       f.object == piece.object &&
                                       f.evaluated_source_triangle ==
                                           piece.evaluated_source_triangle;
                              }),
          "Contact pieces retain complete selected source identity");
    check(std::isfinite(piece.reporting_area_square_metres) &&
              piece.reporting_area_square_metres >= 0,
          "Boundary reporting pieces are finite and never contribute negative "
          "area");
    positive_piece = positive_piece || piece.reporting_area_square_metres > 0;
    const auto face =
        require(lookup_lower_cockpit_face(*provider.contact(), piece.key));
    check(
        std::abs(static_cast<long double>(piece.reporting_area_square_metres) -
                 triangle_area(face.points_current_metres, boot)) < 2e-14L,
        "Reported contact area equals independent complete triangle/sole "
        "clipping");
    reported_area += piece.reporting_area_square_metres;
  }
  check(positive_piece && std::abs(expected_area - reported_area) < 2e-14L &&
            std::abs(expected_area -
                     4.L * .06L * .14L *
                         (static_cast<long double>(boot.frame.columns[0].x) *
                              boot.frame.columns[2].z -
                          static_cast<long double>(boot.frame.columns[0].z) *
                              boot.frame.columns[2].x)) < 2e-14L,
        "Full real sole covered without double counting the internal diagonal");
  const auto& witness = result.pressure_witness;
  const long double weight = witness.port_fraction;
  const auto c0 = witness.canonical_boot_centers[0],
             c1 = witness.canonical_boot_centers[1],
             m = witness.canonical_center_of_mass;
  check(weight == 0 || weight == .5L || weight == 1,
        "Fixture pressure products are exact binary scaling");
  using Expression = std::array<double, 3>;
  const auto expression_at = [&](std::size_t foot, bool x_axis) -> Expression {
    const auto center = foot == 0 ? c0 : c1;
    const auto other = foot == 0 ? c1 : c0;
    const auto factor = static_cast<double>(foot == 0 ? 1 - weight : weight);
    return {x_axis ? m.x : m.z,
            binary_scale(x_axis ? center.x : center.z, factor),
            -binary_scale(x_axis ? other.x : other.z, factor)};
  };
  for (const bool x_axis : {true, false}) {
    IntegerDyadicSum identity;
    for (double term : expression_at(0, x_axis))
      identity.add(binary_scale(term, static_cast<double>(weight)));
    for (double term : expression_at(1, x_axis))
      identity.add(binary_scale(term, static_cast<double>(1 - weight)));
    identity.add(-(x_axis ? m.x : m.z));
    check(identity.sign() == 0,
          "Exact symbolic weighted pressure expressions equal canonical COM");
  }
  const auto x_expression = expression_at(side, true),
             z_expression = expression_at(side, false);
  const auto& bounds = evidence.pressure_center_bounds_metres;
  const auto contained = [](const Expression& expression, double lower,
                            double upper) {
    IntegerDyadicSum lower_gap, upper_gap;
    for (double term : expression) {
      lower_gap.add(term);
      upper_gap.add(-term);
    }
    lower_gap.add(-lower);
    upper_gap.add(upper);
    return lower_gap.sign() >= 0 && upper_gap.sign() >= 0;
  };
  check(contained(x_expression, bounds[0].x, bounds[1].x) &&
            contained(z_expression, bounds[0].z, bounds[1].z),
        "Integer dyadic oracle strictly contains exact pressure in reported "
        "interval");
  // Approximation below is exclusively a deep-margin geometric diagnostic;
  // all expression equality/containment signs above use full integer dyadics.
  const auto approximate = [](const Expression& expression) {
    long double result{};
    for (double term : expression)
      result += static_cast<long double>(term);
    return result;
  };
  const Point pressure{approximate(x_expression), approximate(z_expression)};
  const auto center = side == 0 ? c0 : c1;
  const Point offset{pressure.x - center.x, pressure.z - center.z};
  const auto u = boot.frame.columns[0], v = boot.frame.columns[2];
  const long double determinant =
      static_cast<long double>(u.x) * v.z - static_cast<long double>(u.z) * v.x;
  const Point nx{v.z, -v.x}, nz{-u.z, u.x};
  long double margin = std::min(
      (.06L * determinant - std::abs(dot(offset, nx))) / std::hypot(nx.x, nx.z),
      (.14L * determinant - std::abs(dot(offset, nz))) /
          std::hypot(nz.x, nz.z));
  check(std::abs(pressure.x) < 1 && std::abs(pressure.z) < 1 &&
            determinant > .5L,
        "Focused wider edge oracle retains its declared conditioning domain");
  for (std::size_t i = 0; i < 4; ++i) {
    const auto a = partition.perimeter_metres[i],
               b = partition.perimeter_metres[(i + 1) % 4];
    const long double dx = static_cast<long double>(b.x) - a.x,
                      dz = static_cast<long double>(b.z) - a.z;
    check(std::abs(a.x) < 1 && std::abs(a.z) < 1 && std::abs(b.x) < 1 &&
              std::abs(b.z) < 1 && std::hypot(dx, dz) > .25L,
          "Focused source edges have bounded coordinates and nonzero length");
    const auto distance = -(dx * (pressure.z - a.z) - dz * (pressure.x - a.x)) /
                          std::hypot(dx, dz);
    margin = std::min(margin, distance);
  }
  // Conservative wider-operation reporting bound for the checked fixture
  // domain above: <128 operations, coordinates<1, denominator>.25, D>.5.
  // This affects only oracle corroboration, never production admission.
  check(margin > .03L &&
            evidence.minimum_center_edge_distance_lower_metres >= .03 &&
            static_cast<long double>(
                evidence.minimum_center_edge_distance_lower_metres) <=
                margin + 4096 * std::numeric_limits<long double>::epsilon() &&
            evidence.minimum_disk_edge_margin_lower_metres >= .01 &&
            static_cast<long double>(
                evidence.minimum_disk_edge_margin_lower_metres) <=
                margin - .02L +
                    4096 * std::numeric_limits<long double>::epsilon(),
        "Complete source+sole edge oracle verifies20mm disk with10mm margin");
}
auto positives(const OriginBoardingBootSupport& provider) -> void {
  for (double z : {-.35, -.8}) {
    for (double direction : {-1., 1.}) {
      const auto input = request(direction, z);
      const auto result =
          require(assess_origin_boarding_boot_support(provider, input));
      placement_checks(provider, input, result);
      check(result.local_self.self_checkpoint_passed &&
                result.checkpoint_supported &&
                result.placement_nonpenetrating && result.pressure_supported &&
                result.load_supported,
            "Frozen mirrored self04 checkpoint composes genuine source support "
            "and load");
      contact_checks(provider, result, input.anchor_side);
      const auto& lifted = result.boots[1 - input.anchor_side];
      check(!lifted.loaded && !lifted.pressure_supported &&
                lifted.status == BoardingBootSupportStatus::unloaded &&
                lifted.pieces.empty(),
            "Lifted unloaded boot supplies no invented source support");
      check(lifted.plane_relations[input.anchor_partition] ==
                BoardingBootPlaneRelation::above,
            "Actual lifted boot retains positive source gap");
    }
    const auto input = request(1, z, true);
    const auto result =
        require(assess_origin_boarding_boot_support(provider, input));
    placement_checks(provider, input, result);
    check(result.checkpoint_supported && result.load_supported,
          "Neutral folded dual stance has source support");
    contact_checks(provider, result, 0);
    contact_checks(provider, result, 1);
    if (z == -.8)
      for (const auto& foot : result.boots)
        check(foot.pieces.size() == 2, "Real transition diagonal is internal "
                                       "contact seam, not missing support");
  }
  // Arbitrary yaw is a frame-arithmetic control, not another registered route
  // checkpoint. The oracle uses actual columns/cofactors, never ideal axes.
  auto rotated = request(1, -.25, true);
  rotated.pose.yaw_degrees = 37;
  const auto result =
      require(assess_origin_boarding_boot_support(provider, rotated));
  placement_checks(provider, rotated, result);
  check(result.pressure_supported && result.load_supported,
        "Actual affine yaw frame retains finite contact/load evidence");
  contact_checks(provider, result, 0);
  contact_checks(provider, result, 1);
}
auto refusals(const OriginBoardingBootSupport& provider) -> void {
  auto input = request(1, .0775, true);
  const auto gap =
      require(assess_origin_boarding_boot_support(provider, input));
  check(!gap.checkpoint_supported && !gap.pressure_supported,
        "Actual tile gap cannot gain disk support from convex hull");
  input = request(1, -.35, true);
  input.port_load_fraction = 1;
  const auto grounded_unloaded =
      require(assess_origin_boarding_boot_support(provider, input));
  placement_checks(provider, input, grounded_unloaded);
  check(!grounded_unloaded.checkpoint_supported &&
            !grounded_unloaded.boots[1].loaded &&
            !grounded_unloaded.boots[1].pressure_supported &&
            grounded_unloaded.boots[1].status ==
                BoardingBootSupportStatus::unloaded &&
            grounded_unloaded.boots[1].plane_relations[8] ==
                BoardingBootPlaneRelation::equal,
        "Grounded unloaded foot cannot enlarge load support or gain pressure "
        "permission");
  input = request(1, -.35);
  input.port_load_fraction = .5;
  const auto lifted =
      require(assess_origin_boarding_boot_support(provider, input));
  placement_checks(provider, input, lifted);
  check(!lifted.checkpoint_supported && !lifted.boots[1].pressure_supported &&
            lifted.boots[1].status == BoardingBootSupportStatus::plane_gap,
        "Positive fractional load on genuinely lifted foot refuses contact");
  input = request(1, -.35, true);
  input.pose.sides[1].hip_flex_degrees = .1;
  input.pose.sides[1].knee_flex_degrees = .1;
  const auto tiny =
      require(assess_origin_boarding_boot_support(provider, input));
  placement_checks(provider, input, tiny);
  const auto& a = std::get<BoardingBodyBox>(
      tiny.local_self.canonical.parts[5].world_reservation);
  const auto& b = std::get<BoardingBodyBox>(
      tiny.local_self.canonical.parts[11].world_reservation);
  const auto small_gap = exact_sum({b.center_metres.y, -b.half_size_metres.y,
                                    -a.center_metres.y, a.half_size_metres.y});
  check(small_gap > 0 && small_gap < .000002L && !tiny.checkpoint_supported &&
            tiny.boots[1].status == BoardingBootSupportStatus::plane_gap,
        "Real sub2micrometre lifted boot refuses without old point tolerance");
  input.anchor_side = 1;
  const auto below =
      require(assess_origin_boarding_boot_support(provider, input));
  placement_checks(provider, input, below);
  check(!below.checkpoint_supported && !below.placement_nonpenetrating &&
            below.boots[0].status ==
                BoardingBootSupportStatus::plane_penetration,
        "Anchoring higher boot preserves exact lower-foot penetration refusal");
  for (double toward : {-std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::infinity()}) {
    auto p = request(1, -.35, true);
    p.pose.sides[1].hip_flex_degrees = std::nextafter(.1, toward);
    p.pose.sides[1].knee_flex_degrees = .1;
    const auto adjacent =
        require(assess_origin_boarding_boot_support(provider, p));
    placement_checks(provider, p, adjacent);
    check(!adjacent.checkpoint_supported,
          "Adjacent representable lift does not become epsilon contact");
    p = request(1, -.35, true);
    p.pose.hip_metres.y = std::nextafter(p.pose.hip_metres.y, toward);
    const auto exact_anchor =
        require(assess_origin_boarding_boot_support(provider, p));
    placement_checks(provider, p, exact_anchor);
    check(exact_anchor.boots[0].plane_relations[8] ==
                  BoardingBootPlaneRelation::equal &&
              exact_anchor.boots[1].plane_relations[8] ==
                  BoardingBootPlaneRelation::equal &&
              exact_anchor.checkpoint_supported,
          "Adjacent root heights are rigidly reanchored, not rounded per-foot "
          "snaps");
  }
  input = request(1, -.35);
  input.anchor_partition = 9;
  const auto wrong_lower =
      require(assess_origin_boarding_boot_support(provider, input));
  placement_checks(provider, input, wrong_lower);
  check(!wrong_lower.checkpoint_supported &&
            !wrong_lower.placement_nonpenetrating &&
            wrong_lower.boots[0].plane_relations[8] ==
                BoardingBootPlaneRelation::below,
        "Wrong transition anchor cannot waive the actual upper floor "
        "below-plane overlap");
  input = request(1, -.8);
  input.anchor_partition = 8;
  const auto wrong_upper =
      require(assess_origin_boarding_boot_support(provider, input));
  placement_checks(provider, input, wrong_upper);
  check(!wrong_upper.checkpoint_supported && !wrong_upper.pressure_supported &&
            !wrong_upper.load_supported &&
            wrong_upper.boots[0].plane_relations[9] ==
                BoardingBootPlaneRelation::above,
        "Wrong upper anchor cannot invent an invisible floor over transition "
        "tread");
  input = request(1, -.35);
  input.port_load_fraction = 0;
  check(!assess_origin_boarding_boot_support(provider, input),
        "Unloaded anchor refuses before granting stance support");
  input = request(1, -.35);
  input.anchor_side = 2;
  check(!assess_origin_boarding_boot_support(provider, input),
        "Invalid anchor side refuses");
  input = request(1, -.35);
  input.anchor_partition = 10;
  check(!assess_origin_boarding_boot_support(provider, input),
        "Out-of-roster anchor refuses");
  for (double value : {std::nextafter(0., -1.), std::nextafter(1., 2.),
                       std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity()}) {
    input = request(1, -.35);
    input.port_load_fraction = value;
    check(!assess_origin_boarding_boot_support(provider, input),
          "Invalid or nonfinite load fraction refuses");
  }
  for (double value : {std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity()}) {
    input = request(1, -.35);
    input.pose.hip_metres.y = value;
    check(!assess_origin_boarding_boot_support(provider, input),
          "Nonfinite canonical pose refuses before source load");
  }
  input = request(1, -.35);
  input.pose.policy_version = 1;
  check(!assess_origin_boarding_boot_support(provider, input),
        "Legacy body policy never gains lateral source permission");
  input = request(1, -.35);
  input.pose.sides[0].shoulder_flex_degrees = 0;
  input.pose.sides[0].elbow_flex_degrees = 0;
  const auto strict =
      require(assess_origin_boarding_boot_support(provider, input));
  check(!strict.checkpoint_supported && strict.local_self.strict_conflict,
        "Source support cannot erase exact self refusal");
}
auto source_edge_controls(const OriginBoardingBootSupport& provider) -> void {
  const auto& partition = provider.selected_partitions()[8];
  const auto front =
      std::ranges::min_element(partition.perimeter_metres, {}, &RigidVector3::z)
          ->z;
  auto touch = request(1, front - .14, true);
  const auto touched =
      require(assess_origin_boarding_boot_support(provider, touch));
  placement_checks(provider, touch, touched);
  check(integer_sign({touch.pose.hip_metres.z, .14, -front}) == 0,
        "Registered boundary fixture has exact real sole-edge/source-front "
        "equality");
  check(!touched.checkpoint_supported && !touched.pressure_supported &&
            !touched.load_supported,
        "Zero-area source contact never supplies finite pressure/load support");
  for (const auto& foot : touched.boots) {
    check(!foot.pieces.empty(),
          "Exact source edge touch retains boundary-only reporting pieces");
    for (const auto& piece : foot.pieces)
      check(piece.reporting_area_square_metres == 0,
            "Source edge-touch has zero area, not a sampled disk");
  }
  // Fixed translations are arithmetic boundary controls, not adaptive fitting.
  // Pi.Z at neutral w=.5 equals M.Z exactly; actual outputs still determine
  // its precise relationship to the registered20mm+10mm source margin.
  auto neutral = pose(1, 0, true);
  const auto local = require(evaluate_origin_boarding_body02(neutral));
  const auto middle = (front + kBoardingBootPressureRadiusMetres +
                       kBoardingBootDiskEdgeMarginMetres) -
                      local.center_of_mass_metres.z;
  auto before = middle, after = middle;
  for (unsigned i = 0; i < 8; ++i) {
    before = std::nextafter(before, -std::numeric_limits<double>::infinity());
    after = std::nextafter(after, std::numeric_limits<double>::infinity());
  }
  bool negative{}, positive{};
  for (double z :
       {before,
        std::nextafter(middle, -std::numeric_limits<double>::infinity()),
        middle, std::nextafter(middle, std::numeric_limits<double>::infinity()),
        after}) {
    auto input = request(1, z, true);
    const auto result =
        require(assess_origin_boarding_boot_support(provider, input));
    placement_checks(provider, input, result);
    const auto& witness = result.pressure_witness;
    for (std::size_t foot = 0; foot < 2; ++foot) {
      const auto center = witness.canonical_boot_centers[foot];
      const auto other = witness.canonical_boot_centers[1 - foot];
      const auto sign =
          integer_sign({witness.canonical_center_of_mass.z,
                        binary_scale(center.z, .5), -binary_scale(other.z, .5),
                        -front, -kBoardingBootPressureRadiusMetres,
                        -kBoardingBootDiskEdgeMarginMetres});
      negative = negative || sign < 0;
      positive = positive || sign > 0;
      if (sign < 0)
        check(!result.boots[foot].pressure_supported &&
                  !result.checkpoint_supported,
              "Exact source disk-edge margin deficit forbids acceptance, "
              "including adjacent rootZ");
      std::cout << "MARGIN rootZ=" << std::hexfloat << z << std::defaultfloat
                << " foot=" << foot << " exact_sign=" << sign
                << " accepted=" << result.boots[foot].pressure_supported
                << '\n';
    }
  }
  check(negative && positive,
        "Fixed boundary controls bracket the exact source pressure margin");
}
auto lifetime() -> void {
  check(!make_origin_boarding_boot_support(NativeCraftBinding{}),
        "Unknown legacy binding refuses source provider");
  auto provider = [] {
    auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    auto result = require(make_origin_boarding_boot_support(binding));
    auto moved_binding = std::move(binding);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Empty-handle API contract.
    check(!make_origin_boarding_boot_support(binding),
          "Moved-from binding refuses");
    check(moved_binding.contact() != nullptr,
          "Binding move retains authoritative contact");
    return result;
  }();
  const auto* first = &provider.selected_partitions()[9];
  const auto name = first->faces[0].source_object;
  auto copy = provider;
  auto moved = std::move(provider);
  // NOLINTBEGIN(bugprone-use-after-move,clang-analyzer-cplusplus.Move) -- Test
  // documented empty moved-from provider.
  check(provider.selected_partitions().empty() &&
            provider.contact() == nullptr &&
            !assess_origin_boarding_boot_support(provider, request(1, -.35)),
        "Moved-from source provider has no queries or views");
  // NOLINTEND(bugprone-use-after-move,clang-analyzer-cplusplus.Move) -- End
  // moved-from provider contract.
  check(&copy.selected_partitions()[9] == first &&
            &moved.selected_partitions()[9] == first &&
            name == "CABIN | cockpit transition step",
        "Sharing handles preserve source face/name view lifetime");
  check(
      require(assess_origin_boarding_boot_support(copy, request(1, -.8)))
              .checkpoint_supported &&
          require(assess_origin_boarding_boot_support(moved, request(-1, -.35)))
              .checkpoint_supported,
      "Provider survives destruction and movement of original binding");
}
} // namespace
int main() {
  try {
    static_assert(!BoardingBootSupportDiagnostic::world_qualified &&
                  !BoardingBootSupportDiagnostic::sweep_qualified &&
                  !BoardingBootSupportDiagnostic::route_qualified &&
                  !BoardingBootSupportDiagnostic::actor_qualified &&
                  !BoardingBootSupportDiagnostic::seat_qualified);
    if (std::numeric_limits<long double>::digits < 64)
      throw std::runtime_error(
          "Independent finite sum oracle requires64 mantissa bits");
    const auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    const auto provider = require(make_origin_boarding_boot_support(binding));
    integer_oracle_controls();
    source_checks(provider);
    positives(provider);
    refusals(provider);
    source_edge_controls(provider);
    lifetime();
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << "EXCEPTION: " << error.what() << '\n';
  }
  std::cout << checks << " finite boot support checks, " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
