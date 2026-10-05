#include "apsis_drift/origin_boarding_boot_support.hpp"
#include "origin_boarding_foot_sites_internal.hpp"
#include "origin_boarding_self_model02_internal.hpp"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <limits>
#include <utility>

namespace apsis_drift {
namespace {
using Vec = RigidVector3;
using Box = BoardingBodyBox;
using Relation = BoardingBootPlaneRelation;
using Status = BoardingBootSupportStatus;
constexpr auto infinity = std::numeric_limits<double>::infinity();
struct Interval {
  double low{}, high{};
};
struct Point {
  Interval x, z;
};
auto point(double x) -> Interval {
  return {x, x};
}
auto down(double x) -> double {
  return std::nextafter(x, -infinity);
}
auto up(double x) -> double {
  return std::nextafter(x, infinity);
}
auto add(Interval a, Interval b) -> Interval {
  return {down(a.low + b.low), up(a.high + b.high)};
}
auto negate(Interval a) -> Interval {
  return {-a.high, -a.low};
}
auto subtract(Interval a, Interval b) -> Interval {
  return add(a, negate(b));
}
auto multiply(Interval a, Interval b) -> Interval {
  const std::array values{a.low * b.low, a.low * b.high, a.high * b.low,
                          a.high * b.high};
  return {down(*std::ranges::min_element(values)),
          up(*std::ranges::max_element(values))};
}
auto square(Interval a) -> Interval {
  const auto low =
      a.low <= 0 && a.high >= 0 ? 0 : std::min(a.low * a.low, a.high * a.high);
  return {low == 0 ? 0 : down(low),
          up(std::max(a.low * a.low, a.high * a.high))};
}
auto finite(Interval a) -> bool {
  return std::isfinite(a.low) && std::isfinite(a.high) && a.low <= a.high;
}
auto wide(Vec p) -> Point {
  return {point(p.x), point(p.z)};
}
auto subtract(Point a, Point b) -> Point {
  return {subtract(a.x, b.x), subtract(a.z, b.z)};
}
// Clockwise X/Z edges have the positive side to their right. All operands
// enclose exact expressions in the stored binary64 inputs, not ideal trig.
auto edge_side(Point a, Point b, Point p) -> Interval {
  const auto e = subtract(b, a), d = subtract(p, a);
  return subtract(multiply(e.z, d.x), multiply(e.x, d.z));
}
auto edge_distance(Point a, Point b, Point p) -> Interval {
  const auto e = subtract(b, a);
  const auto side = edge_side(a, b, p);
  const auto length_squared = add(square(e.x), square(e.z));
  if (!finite(side) || !finite(length_squared) || length_squared.low <= 0)
    return {-infinity, infinity};
  const Interval length{down(std::sqrt(length_squared.low)),
                        up(std::sqrt(length_squared.high))};
  if (length.low <= 0) return {-infinity, infinity};
  const std::array ratios{side.low / length.low, side.low / length.high,
                          side.high / length.low, side.high / length.high};
  return {down(*std::ranges::min_element(ratios)),
          up(*std::ranges::max_element(ratios))};
}
auto boot_corners(const Box& b) -> std::array<Point, 4> {
  std::array<Point, 4> result;
  constexpr std::array signs{std::pair{1., -1.}, std::pair{-1., -1.},
                             std::pair{-1., 1.}, std::pair{1., 1.}};
  for (std::size_t i = 0; i < result.size(); ++i) {
    const auto sx = signs[i].first, sz = signs[i].second;
    const auto coordinate = [sx, sz, &b](double center, double u, double v) {
      return add(add(point(center),
                     multiply(point(sx * b.half_size_metres.x), point(u))),
                 multiply(point(sz * b.half_size_metres.z), point(v)));
    };
    result[i] = {coordinate(b.center_metres.x, b.frame.columns[0].x,
                            b.frame.columns[2].x),
                 coordinate(b.center_metres.z, b.frame.columns[0].z,
                            b.frame.columns[2].z)};
  }
  return result;
}
auto source_corners(const BoardingBootSourcePartition& p)
    -> std::array<Point, 4> {
  std::array<Point, 4> result;
  for (std::size_t i = 0; i < result.size(); ++i)
    result[i] = wide(p.perimeter_metres[i]);
  return result;
}
// Proved separating half-plane => no footprint overlap. Otherwise retain
// possible overlap, including unresolved/boundary cases, for strict refusal.
auto possible_overlap(const std::array<Point, 4>& a,
                      const std::array<Point, 4>& b) -> bool {
  const auto separated = [](const auto& first, const auto& second) {
    for (std::size_t i = 0; i < first.size(); ++i)
      if (std::ranges::all_of(second, [&](Point p) {
            return edge_side(first[i], first[(i + 1) % first.size()], p).high <
                   0;
          }))
        return true;
    return false;
  };
  return !separated(a, b) && !separated(b, a);
}
auto exact_relation(std::span<const double> terms) -> Relation {
  switch (detail::boarding_self_model02_linear_sign(terms)) {
    case detail::BoardingSelfSign::negative: return Relation::below;
    case detail::BoardingSelfSign::zero: return Relation::equal;
    case detail::BoardingSelfSign::positive: return Relation::above;
    case detail::BoardingSelfSign::unsupported: return Relation::unresolved;
  }
  return Relation::unresolved;
}
auto plane_relation(const BoardingBootStanceTranslation& t, const Box& b,
                    double plane) -> Relation {
  const std::array terms{b.center_metres.y, -b.half_size_metres.y,
                         t.terms[0],        t.terms[1],
                         t.terms[2],        -plane};
  return exact_relation(terms);
}
auto pressure_center(const BoardingBootPressureWitness& w, std::size_t side)
    -> Point {
  const auto coordinate = [&](double m, double c0, double c1) {
    // P0=M+(1-w)*(C0-C1); P1=M+w*(C1-C0). Never round 1-w
    // into an independently selected starboard load fraction.
    return side == 0 ? add(point(m),
                           multiply(subtract(point(1), point(w.port_fraction)),
                                    subtract(point(c0), point(c1))))
                     : add(point(m), multiply(point(w.port_fraction),
                                              subtract(point(c1), point(c0))));
  };
  return {
      coordinate(w.canonical_center_of_mass.x, w.canonical_boot_centers[0].x,
                 w.canonical_boot_centers[1].x),
      coordinate(w.canonical_center_of_mass.z, w.canonical_boot_centers[0].z,
                 w.canonical_boot_centers[1].z)};
}
auto minimum_distance(const std::array<Point, 4>& corners, Point p)
    -> Interval {
  Interval result{infinity, infinity};
  for (std::size_t i = 0; i < corners.size(); ++i) {
    const auto d =
        edge_distance(corners[i], corners[(i + 1) % corners.size()], p);
    result.low = std::min(result.low, d.low);
    result.high = std::min(result.high, d.high);
  }
  return result;
}
auto midpoint(Interval a) -> double {
  return a.low + (a.high - a.low) * .5;
}
auto reporting_corners(const std::array<Point, 4>& p, double y)
    -> std::array<Vec, 4> {
  std::array<Vec, 4> result;
  for (std::size_t i = 0; i < result.size(); ++i)
    result[i] = {midpoint(p[i].x), y, midpoint(p[i].z)};
  return result;
}
auto side(Vec a, Vec b, Vec p) -> double {
  return (b.z - a.z) * (p.x - a.x) - (b.x - a.x) * (p.z - a.z);
}
// Display-only clipping. Pressure admission never consumes these rounded
// intersections, areas or a convex hull across distinct source partitions.
auto reporting_clip(std::vector<Vec> polygon, const std::array<Vec, 4>& sole)
    -> std::vector<Vec> {
  for (std::size_t i = 0; i < sole.size() && !polygon.empty(); ++i) {
    const auto a = sole[i], b = sole[(i + 1) % sole.size()];
    std::vector<Vec> next;
    auto prior = polygon.back();
    auto prior_side = side(a, b, prior);
    for (const auto current : polygon) {
      const auto current_side = side(a, b, current);
      if ((prior_side >= 0) != (current_side >= 0)) {
        const auto ratio = prior_side / (prior_side - current_side);
        next.push_back({prior.x + ratio * (current.x - prior.x), current.y,
                        prior.z + ratio * (current.z - prior.z)});
      }
      if (current_side >= 0) next.push_back(current);
      prior = current;
      prior_side = current_side;
    }
    polygon = std::move(next);
  }
  return polygon;
}
auto reporting_area(const std::vector<Vec>& p) -> double {
  if (p.size() < 3) return 0;
  double area{};
  const auto origin = p.front();
  for (std::size_t i = 1; i + 1 < p.size(); ++i)
    area += std::abs(side(origin, p[i], p[i + 1])) * .5;
  return area;
}
auto transition_point(int x, int z) -> Vec {
  return {static_cast<double>(x) * 1e-6, -160000. * 1e-6,
          static_cast<double>(z) * 1e-6};
}
auto transition_partition(const OriginLowerCockpitContact& contact)
    -> std::expected<BoardingBootSourcePartition, std::string> {
  BoardingBootSourcePartition result;
  result.plane_metres = -160000. * 1e-6;
  result.perimeter_metres = {
      transition_point(498841, -988000), transition_point(-498841, -988000),
      transition_point(-500881, -612000), transition_point(500881, -612000)};
  std::array<unsigned, 4> counts{};
  for (std::size_t i = 0; i < result.faces.size(); ++i) {
    auto face = lookup_lower_cockpit_face(
        contact, {LowerCockpitContactBuffer::original, 0,
                  static_cast<std::uint32_t>(62119 + i)});
    if (!face || face->object != 124 ||
        face->source_object != "CABIN | cockpit transition step" ||
        face->evaluated_source_triangle || face->unit_normal_current.y <= 0)
      return std::unexpected(
          "Boot exact registered transition source required");
    std::array<bool, 4> seen{};
    for (const auto p : face->points_current_metres) {
      const auto found = std::ranges::find(result.perimeter_metres, p);
      if (found == result.perimeter_metres.end())
        return std::unexpected("Boot exact transition top corners required");
      const auto index =
          static_cast<std::size_t>(found - result.perimeter_metres.begin());
      if (seen[index])
        return std::unexpected("Boot distinct transition corners");
      seen[index] = true;
      ++counts[index];
    }
    result.faces[i] = *face;
  }
  std::array<std::size_t, 2> shared{};
  std::size_t shared_count{};
  for (std::size_t i = 0; i < counts.size(); ++i) {
    if (counts[i] == 2 && shared_count < shared.size())
      shared[shared_count++] = i;
    else if (counts[i] != 1)
      return std::unexpected("Boot complete transition source partition");
  }
  if (shared_count != 2 || (shared[1] - shared[0]) != 2)
    return std::unexpected("Boot transition shared diagonal required");
  return result;
}
} // namespace
struct OriginBoardingBootSupport::Data {
  OriginLowerCockpitContact lower;
  std::array<BoardingBootSourcePartition, kBoardingBootSourcePartitionCount>
      partitions;
  explicit Data(const OriginLowerCockpitContact& contact) : lower(contact) {}
};
OriginBoardingBootSupport::OriginBoardingBootSupport(
    std::shared_ptr<const Data> data)
    : data_(std::move(data)) {
}
auto OriginBoardingBootSupport::selected_partitions() const
    -> std::span<const BoardingBootSourcePartition> {
  return data_ ? std::span<const BoardingBootSourcePartition>{data_->partitions}
               : std::span<const BoardingBootSourcePartition>{};
}
auto OriginBoardingBootSupport::contact() const
    -> const OriginLowerCockpitContact* {
  return data_ ? &data_->lower : nullptr;
}
auto make_origin_boarding_boot_support(const NativeCraftBinding& binding)
    -> std::expected<OriginBoardingBootSupport, std::string> {
  const auto* lower = binding.contact();
  if (!binding.selection() ||
      *binding.selection() != NativeStartingAssemblySelection{} ||
      !binding.pose() || !lower || !lower->stowed_partition())
    return std::unexpected(
        "Boot selected immutable starting assembly required");
  auto corridor = make_origin_cabin_corridor_geometry(*lower);
  if (!corridor) return std::unexpected(corridor.error());
  auto data = std::make_shared<OriginBoardingBootSupport::Data>(*lower);
  const auto faces = corridor->selected_top_faces();
  if (faces.size() != 18)
    return std::unexpected("Boot upper source face count");
  for (std::size_t i = 0; i < 9; ++i) {
    auto& p = data->partitions[i];
    p.faces = {faces[2 * i], faces[2 * i + 1]};
    const auto& a = p.faces[0].points_current_metres;
    const auto& b = p.faces[1].points_current_metres;
    if (a[0] != b[0] || a[2] != b[1])
      return std::unexpected("Boot upper source shared diagonal required");
    p.perimeter_metres = {a[0], a[1], a[2], b[2]};
    p.plane_metres = kCabinCorridorFloorMetres;
  }
  auto transition = transition_partition(*lower);
  if (!transition) return std::unexpected(transition.error());
  data->partitions[9] = *transition;
  return OriginBoardingBootSupport{std::move(data)};
}
auto assess_origin_boarding_boot_support(
    const OriginBoardingBootSupport& provider,
    const BoardingBootSupportRequest& request)
    -> std::expected<BoardingBootSupportDiagnostic, std::string> {
  if (!provider.data_) return std::unexpected("Boot moved-from provider");
  const auto w = request.port_load_fraction;
  if (request.anchor_side >= 2 ||
      request.anchor_partition >= kBoardingBootSourcePartitionCount ||
      !std::isfinite(w) || w < 0 || w > 1 ||
      (request.anchor_side == 0 ? w == 0 : w == 1))
    return std::unexpected("Boot finite positive anchor/fraction required");
  auto self = evaluate_origin_boarding_self_model04(request.pose);
  if (!self) return std::unexpected("Boot body02/self04 pose refused");
  BoardingBootSupportDiagnostic result;
  result.local_self = *self;
  const auto& body = result.local_self.canonical;
  const std::array<const Box*, 2> boots{
      std::get_if<Box>(&body.parts[5].world_reservation),
      std::get_if<Box>(&body.parts[11].world_reservation)};
  if (!boots[0] || !boots[1])
    return std::unexpected("Boot canonical boxes required");
  const auto& anchor = *boots[request.anchor_side];
  const auto& source = provider.data_->partitions[request.anchor_partition];
  result.stance.terms = {source.plane_metres, -anchor.center_metres.y,
                         anchor.half_size_metres.y};
  result.stance.anchor_side = request.anchor_side;
  result.stance.anchor_partition = request.anchor_partition;
  result.stance.reporting_metres =
      (result.stance.terms[0] + result.stance.terms[1]) +
      result.stance.terms[2];
  result.pressure_witness = {{boots[0]->center_metres, boots[1]->center_metres},
                             body.center_of_mass_metres,
                             w};
  result.reporting_placed_center_of_mass_metres = body.center_of_mass_metres;
  result.reporting_placed_center_of_mass_metres.y +=
      result.stance.reporting_metres;
  result.placement_nonpenetrating = true;
  result.pressure_supported = true;
  result.projected_load_margin_lower_metres = infinity;
  const auto required = add(point(kBoardingBootPressureRadiusMetres),
                            point(kBoardingBootDiskEdgeMarginMetres));
  for (std::size_t side_index = 0; side_index < boots.size(); ++side_index) {
    auto& evidence = result.boots[side_index];
    const auto& boot = *boots[side_index];
    const auto corners = boot_corners(boot);
    evidence.loaded = side_index == 0 ? w > 0 : w < 1;
    const auto pressure = pressure_center(result.pressure_witness, side_index);
    evidence.reporting_pressure_center_metres = {
        midpoint(pressure.x),
        boot.center_metres.y + result.stance.reporting_metres -
            boot.half_size_metres.y,
        midpoint(pressure.z)};
    evidence.pressure_center_bounds_metres = {
        Vec{pressure.x.low, evidence.reporting_pressure_center_metres.y,
            pressure.z.low},
        Vec{pressure.x.high, evidence.reporting_pressure_center_metres.y,
            pressure.z.high}};
    evidence.sole_origin_bottom_difference_terms = {
        body.joints[side_index].sole_origin_metres.y, -boot.center_metres.y,
        boot.half_size_metres.y};
    evidence.sole_origin_bottom_relation =
        exact_relation(evidence.sole_origin_bottom_difference_terms);
    evidence.reporting_sole_origin_bottom_difference_metres =
        (evidence.sole_origin_bottom_difference_terms[0] +
         evidence.sole_origin_bottom_difference_terms[1]) +
        evidence.sole_origin_bottom_difference_terms[2];
    bool penetration{}, placement_unresolved{}, pressure_unresolved{}, equal{},
        overlapping_equal{}, margin_refused{};
    const auto sole_distance = minimum_distance(corners, pressure);
    for (std::size_t p = 0; p < provider.data_->partitions.size(); ++p) {
      const auto& partition = provider.data_->partitions[p];
      const auto relation =
          plane_relation(result.stance, boot, partition.plane_metres);
      evidence.plane_relations[p] = relation;
      const auto perimeter = source_corners(partition);
      const auto overlap = possible_overlap(corners, perimeter);
      if (overlap && relation == Relation::below) penetration = true;
      if (overlap && relation == Relation::unresolved)
        placement_unresolved = true;
      if (overlap && relation == Relation::equal) {
        const auto sole = reporting_corners(corners, partition.plane_metres);
        for (const auto& face : partition.faces) {
          auto polygon = reporting_clip({face.points_current_metres.begin(),
                                         face.points_current_metres.end()},
                                        sole);
          const auto area = reporting_area(polygon);
          evidence.pieces.push_back({face.key, face.object,
                                     face.evaluated_source_triangle,
                                     std::move(polygon), area});
        }
      }
      if (!evidence.loaded ||
          (side_index == request.anchor_side && p != request.anchor_partition))
        continue;
      if (relation != Relation::equal) continue;
      equal = true;
      if (!overlap) continue;
      overlapping_equal = true;
      const auto source_distance = minimum_distance(perimeter, pressure);
      const Interval distance{
          std::min(sole_distance.low, source_distance.low),
          std::min(sole_distance.high, source_distance.high)};
      if (!finite(distance)) {
        pressure_unresolved = true;
        continue;
      }
      if (distance.low < required.high) {
        if (distance.high >= required.low)
          pressure_unresolved = true;
        else
          margin_refused = true;
        continue;
      }
      if (!evidence.source_partition ||
          distance.low > evidence.minimum_center_edge_distance_lower_metres) {
        evidence.source_partition = p;
        evidence.minimum_center_edge_distance_lower_metres = distance.low;
        evidence.minimum_disk_edge_margin_lower_metres =
            down(distance.low - kBoardingBootPressureRadiusMetres);
      }
    }
    if (penetration || placement_unresolved) {
      evidence.status = penetration ? Status::plane_penetration
                                    : Status::numerical_unresolved;
      result.placement_nonpenetrating = false;
    } else if (!evidence.loaded)
      evidence.status = Status::unloaded;
    else if (evidence.source_partition) {
      evidence.status = Status::supported;
      evidence.pressure_supported = true;
      const auto& partition =
          provider.data_->partitions[*evidence.source_partition];
      evidence.reporting_pressure_center_metres.y = partition.plane_metres;
      for (auto& p : evidence.pressure_center_bounds_metres)
        p.y = partition.plane_metres;
      // Each accepted source/sole intersection contains a disk about its exact
      // Pi with this radius. Convex combination with exact weights implies a
      // disk of the minimum radius about M in the genuine combined support
      // hull. This does not fill source gaps for an individual pressure disk.
      result.projected_load_margin_lower_metres =
          std::min(result.projected_load_margin_lower_metres,
                   evidence.minimum_center_edge_distance_lower_metres);
    } else if (pressure_unresolved)
      evidence.status = Status::numerical_unresolved;
    else if (margin_refused)
      evidence.status = Status::disk_margin_refused;
    else
      evidence.status = equal && !overlapping_equal ? Status::no_finite_contact
                                                    : Status::plane_gap;
    if (evidence.loaded && !evidence.pressure_supported)
      result.pressure_supported = false;
  }
  result.load_supported =
      result.placement_nonpenetrating && result.pressure_supported &&
      std::isfinite(result.projected_load_margin_lower_metres) &&
      result.projected_load_margin_lower_metres >=
          kBoardingBootLoadMarginMetres;
  result.checkpoint_supported =
      result.load_supported && result.local_self.self_checkpoint_passed;
  return result;
}
namespace {
using SiteBounds = BoardingFootSiteScalarBounds;
using SiteCondition = BoardingFootSiteCondition;
constexpr double site_upper_plane{kCabinCorridorFloorMetres},
    site_transition_plane{-160000.0 * 1e-6};

auto site_supported_environment() -> bool {
  if (!std::numeric_limits<double>::is_iec559 ||
      std::numeric_limits<double>::radix != 2 ||
      std::numeric_limits<double>::digits != 53 ||
      std::fegetround() != FE_TONEAREST)
    return false;
  // Runtime bit probes also catch an arithmetic-unit rounding mode that differs
  // from fegetround(), and flush-to-zero/denormals-are-zero.
  volatile double normal = std::numeric_limits<double>::min(), half = .5;
  volatile double subnormal = std::numeric_limits<double>::denorm_min(),
                  one = 1.;
  volatile double tie = 0x1p-53, above_tie = 0x1.8p-53;
  const double tiny_output = normal * half, preserved_input = subnormal * one;
  const double tie_sum = one + tie, above_tie_sum = one + above_tie;
  return std::bit_cast<std::uint64_t>(tiny_output) == 0x0008000000000000ULL &&
         std::bit_cast<std::uint64_t>(preserved_input) == 1 &&
         std::bit_cast<std::uint64_t>(tie_sum) == 0x3ff0000000000000ULL &&
         std::bit_cast<std::uint64_t>(above_tie_sum) == 0x3ff0000000000001ULL;
}
auto site_bounds(Interval v, bool& supported) -> SiteBounds {
  const auto valid = finite(v);
  supported = supported && valid;
  return valid ? SiteBounds{v.low, v.high, true} : SiteBounds{};
}
auto site_point_bounds(Point p, Interval y, bool& supported)
    -> std::array<Vec, 2> {
  if (!finite(p.x) || !finite(p.z) || !finite(y)) {
    supported = false;
    return {};
  }
  return {Vec{p.x.low, y.low, p.z.low}, Vec{p.x.high, y.high, p.z.high}};
}
auto site_center(const BoardingFootSiteEvidence& evidence) -> Point {
  const auto& x = evidence.center_x_terms;
  const auto& z = evidence.center_z_terms;
  return {add(add(point(x[0]), point(x[1])), point(x[2])),
          add(point(z[0]), point(z[1]))};
}
auto site_sole_corners(Point center) -> std::array<Point, 4> {
  constexpr std::array signs{std::pair{1., -1.}, std::pair{-1., -1.},
                             std::pair{-1., 1.}, std::pair{1., 1.}};
  std::array<Point, 4> result{};
  for (std::size_t i = 0; i < result.size(); ++i)
    result[i] = {add(center.x, point(signs[i].first * .06)),
                 add(center.z, point(signs[i].second * .14))};
  return result;
}
auto site_edge(Point a, Point b, Point pressure, Interval required,
               bool& supported) -> BoardingFootSiteEdgeEvidence {
  const auto edge = subtract(b, a);
  const auto side = edge_side(a, b, pressure);
  const auto length_squared = add(square(edge.x), square(edge.z));
  const auto squared_gap =
      subtract(square(side), multiply(square(required), length_squared));
  BoardingFootSiteEdgeEvidence result;
  result.signed_side = site_bounds(side, supported);
  result.edge_length_squared = site_bounds(length_squared, supported);
  result.squared_margin_gap = site_bounds(squared_gap, supported);
  if (!finite(required) || required.low <= 0 ||
      !result.edge_length_squared.supported || length_squared.low <= 0)
    supported = false;
  result.disk_contained = supported && side.low > 0 && squared_gap.low >= 0;
  return result;
}
auto site_edges(const std::array<Point, 4>& corners, Point pressure,
                Interval required, bool& supported)
    -> std::array<BoardingFootSiteEdgeEvidence, 4> {
  std::array<BoardingFootSiteEdgeEvidence, 4> result{};
  for (std::size_t i = 0; i < result.size(); ++i)
    result[i] = site_edge(corners[i], corners[(i + 1) % corners.size()],
                          pressure, required, supported);
  return result;
}
auto site_disk_contained(
    const std::array<BoardingFootSiteEdgeEvidence, 4>& edges) -> bool {
  return std::ranges::all_of(
      edges, [](const auto& edge) { return edge.disk_contained; });
}
auto site_margin_unresolved(
    const std::array<BoardingFootSiteEdgeEvidence, 4>& edges) -> bool {
  // A proved failed edge refutes this disk even if a different edge is
  // uncertain.
  for (const auto& e : edges)
    if (e.signed_side.supported && e.squared_margin_gap.supported &&
        (e.signed_side.upper <= 0 || e.squared_margin_gap.upper < 0))
      return false;
  return !site_disk_contained(edges);
}
auto site_overlap(const std::array<Point, 4>& sole,
                  const std::array<Point, 4>& perimeter, bool& supported)
    -> bool {
  // Same complete polygon SAT as possible_overlap. Retain every arithmetic
  // failure, including one occurring after a different proved separating edge.
  const auto separated = [&](const auto& first, const auto& second) {
    bool any_separation{};
    for (std::size_t i = 0; i < first.size(); ++i) {
      bool all_outside = true;
      for (const auto p : second) {
        const auto side = edge_side(first[i], first[(i + 1) % first.size()], p);
        supported = supported && finite(side);
        all_outside = all_outside && finite(side) && side.high < 0;
      }
      any_separation = any_separation || all_outside;
    }
    return any_separation;
  };
  const auto sole_separates = separated(sole, perimeter);
  const auto source_separates = separated(perimeter, sole);
  return !sole_separates && !source_separates;
}
auto site_refuse(BoardingFootSiteEvidence& site, std::size_t index,
                 SiteCondition condition,
                 std::optional<std::size_t> partition = {}) -> void {
  if (!site.first_refusal)
    site.first_refusal = BoardingFootSiteRefusal{index, partition, condition};
}
auto finite_site_offsets(const BoardingFootSiteOffsets& offsets) -> bool {
  const std::array values{
      offsets.center_metres.x, offsets.center_metres.y, offsets.center_metres.z,
      offsets.pressure_xz_metres[0], offsets.pressure_xz_metres[1]};
  return std::ranges::all_of(values, [](double value) {
    return std::isfinite(value) && std::abs(value) <= 8;
  });
}
} // namespace

auto detail::boarding_foot_sites_bounded(
    const OriginBoardingBootSupport& provider,
    const std::array<BoardingFootSiteOffsets, 2>& offsets,
    std::size_t max_partitions)
    -> std::expected<BoardingFootSitesDiagnostic, std::string> {
  if (max_partitions > kBoardingBootSourcePartitionCount ||
      !std::ranges::all_of(offsets, finite_site_offsets))
    return std::unexpected(
        "Foot sites finite offsets <=8 and scan cap <=10 required");
  const auto partitions = provider.selected_partitions();
  if (!provider.contact() || !provider.contact()->stowed_partition() ||
      partitions.size() != kBoardingBootSourcePartitionCount)
    return std::unexpected("Foot sites immutable selected provider required");
  if (partitions[8].plane_metres != site_upper_plane ||
      partitions[9].plane_metres != site_transition_plane)
    return std::unexpected(
        "Foot sites exact registered source planes required");
  BoardingFootSitesDiagnostic result{provider};
  if (!site_supported_environment()) {
    for (std::size_t i = 0; i < result.sites.size(); ++i)
      site_refuse(result.sites[i], i, SiteCondition::unsupported_arithmetic);
    result.first_refusal = result.sites[0].first_refusal;
    return result;
  }
  const auto required = add(point(kBoardingBootPressureRadiusMetres),
                            point(kBoardingBootDiskEdgeMarginMetres));
  result.arithmetic_supported = true;
  result.coverage_complete = true;
  result.eligible = true;
  for (std::size_t i = 0; i < result.sites.size(); ++i) {
    auto& site = result.sites[i];
    const auto& offset = offsets[i];
    const auto base_plane = i == 0 ? site_upper_plane : site_transition_plane;
    site.center_x_terms = {.16, i == 0 ? -.14 : .14, offset.center_metres.x};
    site.center_y_terms = {base_plane, .05, offset.center_metres.y};
    site.center_z_terms = {i == 0 ? -.5 : -.8, offset.center_metres.z};
    site.pressure_offset_terms = {
        {{0, offset.pressure_xz_metres[0]},
         {i == 0 ? .040 : 0, offset.pressure_xz_metres[1]}}};
    site.arithmetic_supported = true;
    const auto center = site_center(site);
    const auto center_y =
        add(add(point(base_plane), point(.05)), point(offset.center_metres.y));
    const auto sole_y = add(point(base_plane), point(offset.center_metres.y));
    const Point pressure{
        add(center.x, add(point(site.pressure_offset_terms[0][0]),
                          point(site.pressure_offset_terms[0][1]))),
        add(center.z, add(point(site.pressure_offset_terms[1][0]),
                          point(site.pressure_offset_terms[1][1])))};
    const auto corners = site_sole_corners(center);
    site.center_bounds_metres =
        site_point_bounds(center, center_y, site.arithmetic_supported);
    site.pressure_bounds_metres =
        site_point_bounds(pressure, sole_y, site.arithmetic_supported);
    for (std::size_t c = 0; c < corners.size(); ++c)
      site.sole_corner_bounds_metres[c] =
          site_point_bounds(corners[c], sole_y, site.arithmetic_supported);
    site.sole_edges =
        site_edges(corners, pressure, required, site.arithmetic_supported);
    site.sole_disk_contained = site_disk_contained(site.sole_edges);
    if (!site.arithmetic_supported)
      site_refuse(site, i, SiteCondition::unsupported_arithmetic);
    else if (!site.sole_disk_contained)
      site_refuse(site, i,
                  site_margin_unresolved(site.sole_edges)
                      ? SiteCondition::numerical_unresolved
                      : SiteCondition::sole_disk_margin);
    bool penetration{}, matching_plane{}, source_uncertain{};
    for (std::size_t p = 0; p < max_partitions; ++p) {
      auto& record = site.partitions[p];
      const auto& partition = partitions[p];
      record.evaluated = true;
      const std::array plane_terms{base_plane, offset.center_metres.y,
                                   -partition.plane_metres};
      record.plane_relation = exact_relation(plane_terms);
      if (record.plane_relation == Relation::unresolved) {
        site.arithmetic_supported = false;
        site_refuse(site, i, SiteCondition::unsupported_arithmetic, p);
      }
      const auto perimeter = source_corners(partition);
      bool supported = true;
      record.footprint_overlap_possible =
          site_overlap(corners, perimeter, supported);
      record.edges = site_edges(perimeter, pressure, required, supported);
      site.arithmetic_supported = site.arithmetic_supported && supported;
      if (!supported)
        site_refuse(site, i, SiteCondition::unsupported_arithmetic, p);
      if (record.footprint_overlap_possible &&
          record.plane_relation == Relation::below) {
        penetration = true;
        site_refuse(site, i, SiteCondition::plane_penetration, p);
      }
      if (record.plane_relation == Relation::equal) {
        matching_plane = true;
        record.disk_contained = supported && site_disk_contained(record.edges);
        if (record.disk_contained && !site.source_partition)
          site.source_partition = p;
        source_uncertain =
            source_uncertain || site_margin_unresolved(record.edges);
      }
      ++site.scanned_partitions;
    }
    site.coverage_complete = site.scanned_partitions == partitions.size();
    site.placement_nonpenetrating =
        site.coverage_complete && site.arithmetic_supported && !penetration;
    site.source_disk_contained = site.source_partition.has_value();
    if (!site.coverage_complete)
      site_refuse(site, i, SiteCondition::incomplete_coverage);
    else if (!site.source_disk_contained)
      site_refuse(site, i,
                  !matching_plane    ? SiteCondition::no_matching_plane
                  : source_uncertain ? SiteCondition::numerical_unresolved
                                     : SiteCondition::source_disk_margin);
    site.eligible = site.arithmetic_supported && site.coverage_complete &&
                    site.placement_nonpenetrating && site.sole_disk_contained &&
                    site.source_disk_contained;
    result.arithmetic_supported =
        result.arithmetic_supported && site.arithmetic_supported;
    result.coverage_complete =
        result.coverage_complete && site.coverage_complete;
    result.eligible = result.eligible && site.eligible;
    if (!result.first_refusal && site.first_refusal)
      result.first_refusal = site.first_refusal;
  }
  return result;
}
auto assess_origin_boarding_foot_sites(
    const OriginBoardingBootSupport& provider)
    -> std::expected<BoardingFootSitesDiagnostic, std::string> {
  return detail::boarding_foot_sites_bounded(provider, {},
                                             kBoardingBootSourcePartitionCount);
}
} // namespace apsis_drift
