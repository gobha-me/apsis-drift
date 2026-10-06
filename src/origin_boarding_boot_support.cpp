#include "apsis_drift/origin_boarding_boot_support.hpp"
#include "origin_boarding_foot_sites_internal.hpp"
#include "origin_boarding_lower_foot_transfer_internal.hpp"
#include "origin_boarding_self_model02_internal.hpp"
#include "origin_boarding_source_endpoint_load_internal.hpp"
#include "origin_boarding_source_endpoint_self_internal.hpp"
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

namespace apsis_drift {
namespace {
using LoadQuad = BoardingSourceEndpointLoadQuadMathEvidence;
using LoadPressure = BoardingSourceEndpointLoadPressureEvidence;
using LoadPayload = BoardingSourceEndpointLoadPayload;
using LoadCondition = BoardingSourceEndpointLoadCondition;
using LoadCandidate = BoardingSourceEndpointLoadCandidate;
using LoadStatus = BoardingSourceEndpointLoadCandidateStatus;
using LoadPointId = BoardingPlantedBodyPointId;
// Fixed storage only, excluding the independently owned child/output. All
// nested arithmetic temporaries are accounted separately below; no root or
// old 256-term exact-sign expansion enters this new assessment.
struct LoadScratch {
  std::array<Point, 2> centers{}, pressures{};
  Point com{}, barycenter{}, delta{};
  std::array<Point, 4> sole{}, source{};
  std::array<BoardingFootSiteEdgeEvidence, 4> edges{};
};
constexpr std::size_t load_nested_scratch =
    2 * sizeof(BoardingFootSiteEdgeEvidence) + sizeof(LoadQuad) +
    sizeof(LoadCandidate) + 24 * sizeof(Interval) + 16 * sizeof(std::size_t) +
    10 * sizeof(double) + 4 * sizeof(Vec) + 4 * sizeof(double);
// Live helpers reuse this pool; the categories above are NOT independent
// stack allocations. Include by-value Point parameters, not just Interval
// locals. Deep disk chain: fill pressure/required48 + site_edge parameters112
// and edge32 + edge_side parameters96 and e/d64 + retained product16 +
// multiply arguments32/products32/return16 =448, plus100 for remaining
// arithmetic/return temporaries =548. Add outer numeric Point copies96,
// reference/control slots128 and two edge-record returns160.
constexpr std::size_t load_disk_helper_scratch =
    548 + 3 * sizeof(Point) + 16 * sizeof(std::size_t) +
    2 * sizeof(BoardingFootSiteEdgeEvidence);
// Alternate quad path: two guard records, one supplied perimeter, Point
// locals/parameters/conversions, six intervals, controls, a corner return and
// the product array. It never nests inside the disk path.
constexpr std::size_t load_quad_helper_scratch =
    2 * sizeof(LoadQuad) + 4 * sizeof(Vec) + 9 * sizeof(Point) +
    6 * sizeof(Interval) + 16 * sizeof(std::size_t) + 4 * sizeof(Point) +
    4 * sizeof(double);
static_assert(load_disk_helper_scratch <= load_nested_scratch);
static_assert(load_quad_helper_scratch <= load_nested_scratch);
static_assert(sizeof(LoadPayload) <= 4096);
static_assert(sizeof(LoadScratch) + load_nested_scratch + 240 <= 2048);
static_assert(sizeof(LoadQuad) <= 72);
static_assert(sizeof(LoadCandidate) <= 56);
static_assert(sizeof(LoadPressure) <= 1312);

auto load_finite(double x) -> bool {
  return std::isfinite(x) && std::abs(x) <= 8;
}
auto load_finite(Vec p) -> bool {
  return load_finite(p.x) && load_finite(p.y) && load_finite(p.z);
}
auto load_input_bound(BoardingPlantedLegScalarBounds b) -> bool {
  return load_finite(b.lower) && load_finite(b.upper) && b.lower <= b.upper;
}
auto load_minimum(SiteBounds& accumulated, Interval next, bool first,
                  bool& supported) -> void {
  supported = supported && finite(next);
  if (!finite(next)) return;
  if (first)
    accumulated = {next.low, next.high, true};
  else {
    accumulated.lower = std::min(accumulated.lower, next.low);
    accumulated.upper = std::min(accumulated.upper, next.high);
  }
}
auto load_quad(const std::array<Vec, 4>& q, double plane) -> LoadQuad {
  LoadQuad result;
  if (!site_supported_environment() || !load_finite(plane) ||
      !std::ranges::all_of(q, [](Vec p) { return load_finite(p); }))
    return result;
  result.arithmetic_supported = true;
  result.horizontal =
      std::ranges::all_of(q, [plane](Vec p) { return p.y == plane; });
  result.nondegenerate = true;
  result.convex = true;
  for (std::size_t i = 0; i < q.size(); ++i) {
    const auto a = wide(q[i]), b = wide(q[(i + 1) % q.size()]);
    const auto edge = subtract(b, a);
    const auto length = add(square(edge.x), square(edge.z));
    load_minimum(result.minimum_edge_squared, length, result.checked_edges == 0,
                 result.arithmetic_supported);
    ++result.checked_edges;
    result.nondegenerate =
        result.nondegenerate && finite(length) && length.low > 0;
    for (std::size_t j = 2; j < q.size(); ++j) {
      const auto side = edge_side(a, b, wide(q[(i + j) % q.size()]));
      load_minimum(result.minimum_signed_side, side,
                   result.checked_side_signs == 0, result.arithmetic_supported);
      ++result.checked_side_signs;
      result.convex = result.convex && finite(side) && side.low > 0;
    }
  }
  result.minimum_edge_squared.supported = result.arithmetic_supported;
  result.minimum_signed_side.supported = result.arithmetic_supported;
  result.upward = result.horizontal && result.convex;
  result.valid = result.arithmetic_supported && result.horizontal &&
                 result.upward && result.convex && result.nondegenerate;
  return result;
}
auto load_source_quad(const BoardingBootSourcePartition& source) -> LoadQuad {
  auto result = load_quad(source.perimeter_metres, source.plane_metres);
  if (!result.arithmetic_supported) return result;
  // A geometric +Y perimeter is insufficient: retain exact emitted triangles
  // and prove they cover it once on the actual opposite-corner diagonal.
  std::array<std::size_t, 4> incidence{};
  bool triangles = true;
  for (const auto& face : source.faces) {
    std::array<bool, 4> seen{};
    bool finite_face = true;
    for (const auto vertex : face.points_current_metres) {
      if (!load_finite(vertex)) {
        result.arithmetic_supported = false;
        finite_face = false;
        triangles = false;
        continue;
      }
      result.horizontal = result.horizontal && vertex.y == source.plane_metres;
      if (vertex.y != source.plane_metres) triangles = false;
      const auto found = std::ranges::find(source.perimeter_metres, vertex);
      if (found == source.perimeter_metres.end()) {
        triangles = false;
        continue;
      }
      const auto index =
          static_cast<std::size_t>(found - source.perimeter_metres.begin());
      triangles = triangles && !seen[index];
      seen[index] = true;
      ++incidence[index];
    }
    if (!finite_face) continue;
    const auto winding = edge_side(wide(face.points_current_metres[0]),
                                   wide(face.points_current_metres[1]),
                                   wide(face.points_current_metres[2]));
    result.arithmetic_supported =
        result.arithmetic_supported && finite(winding);
    triangles = triangles && finite(winding) && winding.low > 0;
  }
  std::size_t shared_first{}, shared_last{}, shared_count{}, single_count{};
  for (std::size_t i = 0; i < incidence.size(); ++i) {
    if (incidence[i] == 2) {
      if (shared_count == 0) shared_first = i;
      shared_last = i;
      ++shared_count;
    } else if (incidence[i] == 1)
      ++single_count;
  }
  triangles = triangles && shared_count == 2 && single_count == 2 &&
              shared_last - shared_first == 2;
  result.upward = result.upward && triangles;
  result.valid = result.valid && result.arithmetic_supported && triangles;
  return result;
}
auto load_provider_valid(const OriginBoardingBootSupport& provider) -> bool {
  const auto source = provider.selected_partitions();
  return provider.contact() && provider.contact()->stowed_partition() &&
         source.size() == kBoardingBootSourcePartitionCount &&
         source[8].plane_metres == site_upper_plane &&
         source[9].plane_metres == site_transition_plane;
}
auto load_refuse(LoadPayload& out, LoadCondition condition,
                 std::optional<std::size_t> site = {},
                 std::optional<std::size_t> partition = {}) -> void {
  if (!out.first_refusal)
    out.first_refusal =
        BoardingSourceEndpointLoadRefusal{condition, site, partition};
}
auto load_fill_edges(const std::array<Point, 4>& corners, Point pressure,
                     std::array<BoardingFootSiteEdgeEvidence, 4>& edges,
                     std::size_t& attempts, bool& supported) -> void {
  const auto required = add(point(kBoardingBootPressureRadiusMetres),
                            point(kBoardingBootDiskEdgeMarginMetres));
  for (std::size_t i = 0; i < corners.size(); ++i) {
    edges[i] = site_edge(corners[i], corners[(i + 1) % corners.size()],
                         pressure, required, supported);
    ++attempts;
  }
}
auto load_summary(const std::array<BoardingFootSiteEdgeEvidence, 4>& edges,
                  bool supported) -> LoadCandidate {
  LoadCandidate result;
  result.arithmetic_supported = supported;
  result.quad_valid = true;
  for (std::size_t i = 0; i < edges.size(); ++i) {
    const auto& e = edges[i];
    result.arithmetic_supported =
        result.arithmetic_supported && e.signed_side.supported &&
        e.edge_length_squared.supported && e.squared_margin_gap.supported;
    load_minimum(result.minimum_signed_side,
                 {e.signed_side.lower, e.signed_side.upper}, i == 0,
                 result.arithmetic_supported);
    load_minimum(result.minimum_squared_gap,
                 {e.squared_margin_gap.lower, e.squared_margin_gap.upper},
                 i == 0, result.arithmetic_supported);
  }
  result.minimum_signed_side.supported = result.arithmetic_supported;
  result.minimum_squared_gap.supported = result.arithmetic_supported;
  result.status =
      !result.arithmetic_supported    ? LoadStatus::numerical_unresolved
      : site_disk_contained(edges)    ? LoadStatus::contained
      : site_margin_unresolved(edges) ? LoadStatus::numerical_unresolved
                                      : LoadStatus::margin_refused;
  return result;
}
auto load_geometry_pressure(std::size_t site, Point pressure,
                            const BoardingBootSourcePartition& source,
                            LoadPressure& result, LoadScratch& scratch,
                            std::size_t& edge_checks, const LoadQuad& quad)
    -> void {
  result.plane_metres = site == 0 ? site_upper_plane : site_transition_plane;
  result.arithmetic_supported = true;
  const Point center{add(point(.16), point(site == 0 ? -.14 : .14)),
                     point(site == 0 ? -.5 : -.8)};
  scratch.sole = site_sole_corners(center);
  result.pressure_bounds_metres = site_point_bounds(
      pressure, point(result.plane_metres), result.arithmetic_supported);
  load_fill_edges(scratch.sole, pressure, result.sole_edges, edge_checks,
                  result.arithmetic_supported);
  result.sole_disk_contained = site_disk_contained(result.sole_edges);
  result.coplanar = source.plane_metres == result.plane_metres;
  result.scanned_partitions = 1;
  if (quad.valid && result.coplanar) {
    scratch.source = source_corners(source);
    load_fill_edges(scratch.source, pressure, result.source_edges, edge_checks,
                    result.arithmetic_supported);
    result.source_disk_contained =
        result.arithmetic_supported && site_disk_contained(result.source_edges);
    result.source_keys = {source.faces[0].key, source.faces[1].key};
  }
  result.coverage_complete = true; // One selected arithmetic-only candidate.
  result.complete = quad.valid && result.coplanar &&
                    result.arithmetic_supported && result.sole_disk_contained &&
                    result.source_disk_contained;
}
} // namespace
namespace {
struct LoadMassIdentity {
  LoadPointId first, second;
  std::uint32_t weight;
};
constexpr std::array<LoadMassIdentity, kBoardingBodyPartCount> load_masses{
    {{LoadPointId::root, LoadPointId::root, 144},
     {LoadPointId::trunk_center, LoadPointId::trunk_center, 540},
     {LoadPointId::helmet_center, LoadPointId::helmet_center, 96},
     {LoadPointId::port_hip, LoadPointId::port_knee, 120},
     {LoadPointId::port_knee, LoadPointId::port_ankle, 48},
     {LoadPointId::port_boot_center, LoadPointId::port_boot_center, 12},
     {LoadPointId::port_shoulder, LoadPointId::port_elbow, 15},
     {LoadPointId::port_elbow, LoadPointId::port_wrist, 10},
     {LoadPointId::port_wrist, LoadPointId::port_wrist, 5},
     {LoadPointId::starboard_hip, LoadPointId::starboard_knee, 120},
     {LoadPointId::starboard_knee, LoadPointId::starboard_ankle, 48},
     {LoadPointId::starboard_boot_center, LoadPointId::starboard_boot_center,
      12},
     {LoadPointId::starboard_shoulder, LoadPointId::starboard_elbow, 15},
     {LoadPointId::starboard_elbow, LoadPointId::starboard_wrist, 10},
     {LoadPointId::starboard_wrist, LoadPointId::starboard_wrist, 5}}};
static_assert([] {
  std::uint32_t total{};
  for (const auto& mass : load_masses)
    total += mass.weight;
  return total == kBoardingBodyMassDenominator && 144 + 540 + 96 == 780 &&
         15 + 10 + 5 == 30 && 120 + 48 + 12 == 180;
}());
auto load_binding(const BoardingSourceEndpointSelfDiagnostic& self) -> bool {
  const auto& endpoint = self.endpoint;
  if (!self.complete || !self.bindings_complete || !self.arithmetic_supported ||
      !self.coverage_complete || !self.self_qualified ||
      self.certified_pairs != kBoardingBodyPairCount || !endpoint.complete ||
      !endpoint.body || !endpoint.arithmetic_supported ||
      !endpoint.plane_identities || !endpoint.link_identities ||
      !endpoint.joint_limits_certified || !endpoint.reservations_complete ||
      !endpoint.mass_model_complete || !endpoint.hip_regions_certified ||
      !endpoint.sites.eligible || !endpoint.sites.coverage_complete ||
      !endpoint.sites.arithmetic_supported ||
      endpoint.common_translation_y_metres != .847 ||
      endpoint.root_z_metres != -.55 ||
      endpoint.port_x_terms != std::array{.16, -.14} ||
      endpoint.starboard_x_terms != std::array{.16, .14})
    return false;
  for (std::size_t i = 0; i < load_masses.size(); ++i) {
    const auto& binding = endpoint.parts[i];
    const auto& expected = load_masses[i];
    if (static_cast<std::size_t>(binding.id) != i ||
        binding.mass.first != expected.first ||
        binding.mass.second != expected.second ||
        binding.mass.weight != expected.weight)
      return false;
  }
  // Self01 authenticates the entire original fixed body binding/frames before
  // certifying. The checks below bind the same sole and expression terms;
  // neither mass midpoints nor COM are rebuilt here.
  for (std::size_t side = 0; side < 2; ++side) {
    const auto plane = side == 0 ? site_upper_plane : site_transition_plane;
    const auto& site = endpoint.sites.sites[side];
    const auto& leg = endpoint.legs[side];
    const auto* boot = std::get_if<BoardingPlantedBodyBoxBinding>(
        &endpoint.parts[side == 0 ? 5 : 11].reservation);
    if (!boot ||
        boot->center != (side == 0 ? LoadPointId::port_boot_center
                                   : LoadPointId::starboard_boot_center) ||
        boot->half_size_metres != Vec{.06, .05, .14} ||
        boot->frame.columns != BoardingBodyFrame{}.columns ||
        site.half_size_metres != boot->half_size_metres ||
        site.center_x_terms != std::array{.16, side == 0 ? -.14 : .14, 0.} ||
        site.center_y_terms != std::array{plane, .05, 0.} ||
        site.center_z_terms != std::array{side == 0 ? -.5 : -.8, 0.} ||
        !site.placement_nonpenetrating || !site.coverage_complete ||
        !leg.plane_identity || !leg.link_identities || !leg.limits_certified ||
        leg.plane_metres != plane ||
        leg.boot_center_y_terms != std::array{plane, .05, -.847} ||
        leg.ankle_y_terms != std::array{plane, .1, -.847})
      return false;
  }
  const auto& com = endpoint.body->center_of_mass;
  return load_finite(com.lower) && load_finite(com.upper) &&
         com.lower.x <= com.upper.x && com.lower.y <= com.upper.y &&
         com.lower.z <= com.upper.z;
}
auto load_pressure_scan(std::size_t site,
                        std::span<const BoardingBootSourcePartition> sources,
                        std::size_t cap, LoadPayload& out, LoadScratch& scratch)
    -> void {
  auto& record = out.pressures[site];
  record.plane_metres = site == 0 ? site_upper_plane : site_transition_plane;
  record.arithmetic_supported = true;
  record.pressure_bounds_metres =
      site_point_bounds(scratch.pressures[site], point(record.plane_metres),
                        record.arithmetic_supported);
  scratch.sole = site_sole_corners(scratch.centers[site]);
  load_fill_edges(scratch.sole, scratch.pressures[site], record.sole_edges,
                  out.edge_checks, record.arithmetic_supported);
  record.sole_disk_contained =
      record.arithmetic_supported && site_disk_contained(record.sole_edges);
  if (!record.arithmetic_supported)
    load_refuse(out, LoadCondition::unsupported_arithmetic, site);
  else if (!record.sole_disk_contained)
    load_refuse(out,
                site_margin_unresolved(record.sole_edges)
                    ? LoadCondition::numerical_unresolved
                    : LoadCondition::sole_disk_margin,
                site);
  bool matching{}, uncertain{};
  for (std::size_t p = 0; p < cap; ++p) {
    ++out.pressure_candidates;
    ++record.scanned_partitions;
    auto& candidate = record.candidates[p];
    const auto& quad = out.source_quads[p];
    candidate.quad_valid = quad.valid;
    candidate.arithmetic_supported = quad.arithmetic_supported;
    if (!quad.valid) {
      candidate.status = LoadStatus::invalid_support;
      record.arithmetic_supported =
          record.arithmetic_supported && quad.arithmetic_supported;
      continue;
    }
    if (sources[p].plane_metres != record.plane_metres) {
      candidate.status = LoadStatus::noncoplanar;
      continue;
    }
    matching = true;
    scratch.source = source_corners(sources[p]);
    bool supported = true;
    load_fill_edges(scratch.source, scratch.pressures[site], scratch.edges,
                    out.edge_checks, supported);
    candidate = load_summary(scratch.edges, supported);
    record.arithmetic_supported = record.arithmetic_supported && supported;
    uncertain =
        uncertain || candidate.status == LoadStatus::numerical_unresolved;
    if (!supported)
      load_refuse(out, LoadCondition::unsupported_arithmetic, site, p);
    if (candidate.status == LoadStatus::contained && !record.source_partition) {
      record.source_partition = p;
      record.source_edges = scratch.edges;
      record.source_keys = {sources[p].faces[0].key, sources[p].faces[1].key};
    }
  }
  record.coplanar = matching;
  record.coverage_complete = cap == sources.size();
  record.source_disk_contained = record.source_partition.has_value();
  record.complete = record.arithmetic_supported && record.coverage_complete &&
                    record.sole_disk_contained && record.source_disk_contained;
  if (!record.coverage_complete)
    load_refuse(out, LoadCondition::pressure_capacity, site);
  else if (!record.source_disk_contained)
    load_refuse(out,
                !matching   ? LoadCondition::no_matching_support
                : uncertain ? LoadCondition::numerical_unresolved
                            : LoadCondition::source_disk_margin,
                site);
  out.arithmetic_supported =
      out.arithmetic_supported && record.arithmetic_supported;
}
} // namespace

auto detail::boarding_source_endpoint_load_quad_math(
    std::array<RigidVector3, 4> perimeter, double plane_metres)
    -> std::expected<BoardingSourceEndpointLoadQuadMathEvidence, std::string> {
  if (!load_finite(plane_metres) ||
      !std::ranges::all_of(perimeter, [](Vec p) { return load_finite(p); }))
    return std::unexpected("Load quad finite coordinates/plane <=8 required");
  return load_quad(perimeter, plane_metres);
}
auto detail::boarding_source_endpoint_load_pressure_math(
    const OriginBoardingBootSupport& provider, std::size_t site,
    std::size_t source_partition,
    std::array<BoardingPlantedLegScalarBounds, 2> pressure_xz)
    -> std::expected<BoardingSourceEndpointLoadPressureMathEvidence,
                     std::string> {
  if (!load_provider_valid(provider) || site > 1 ||
      source_partition >= kBoardingBootSourcePartitionCount ||
      !std::ranges::all_of(pressure_xz, load_input_bound))
    return std::unexpected("Load pressure selected provider/site/source and "
                           "finite bounds <=8 required");
  BoardingSourceEndpointLoadPressureMathEvidence result;
  if (!site_supported_environment()) return result;
  const auto& source = provider.selected_partitions()[source_partition];
  result.source_quad = load_source_quad(source);
  LoadScratch scratch;
  const Point pressure{{pressure_xz[0].lower, pressure_xz[0].upper},
                       {pressure_xz[1].lower, pressure_xz[1].upper}};
  load_geometry_pressure(site, pressure, source, result.pressure, scratch,
                         result.edge_checks, result.source_quad);
  auto& candidate = result.pressure.candidates[source_partition];
  if (!result.source_quad.valid) {
    candidate.status = LoadStatus::invalid_support;
    candidate.arithmetic_supported = result.source_quad.arithmetic_supported;
    result.pressure.arithmetic_supported =
        result.pressure.arithmetic_supported &&
        result.source_quad.arithmetic_supported;
  } else if (!result.pressure.coplanar) {
    candidate.status = LoadStatus::noncoplanar;
    candidate.arithmetic_supported = true;
    candidate.quad_valid = true;
  } else {
    candidate = load_summary(result.pressure.source_edges,
                             result.pressure.arithmetic_supported);
    if (result.pressure.source_disk_contained)
      result.pressure.source_partition = source_partition;
  }
  return result;
}
auto detail::boarding_source_endpoint_load_bounded(
    const OriginBoardingBootSupport& provider,
    std::size_t max_source_partitions, std::size_t max_body_records,
    std::size_t max_self_pairs, std::size_t max_self_axes,
    std::size_t max_pressure_partitions)
    -> std::expected<BoardingSourceEndpointLoadDiagnostic, std::string> {
  if (max_pressure_partitions > kBoardingBootSourcePartitionCount)
    return std::unexpected("Load pressure cap <=10 required");
  auto child = detail::boarding_source_endpoint_self_bounded(
      provider, max_source_partitions, max_body_records, max_self_pairs,
      max_self_axes);
  if (!child) return std::unexpected(child.error());
  BoardingSourceEndpointLoadDiagnostic result{std::move(*child), {}};
  auto& out = result.load;
  if (!result.self.complete) {
    load_refuse(out, LoadCondition::self_prerequisite);
    return result;
  }
  if (!site_supported_environment()) {
    load_refuse(out, LoadCondition::unsupported_arithmetic);
    return result;
  }
  if (!load_binding(result.self)) {
    load_refuse(out, LoadCondition::invalid_binding);
    return result;
  }
  const auto& endpoint = result.self.endpoint;
  const auto sources = endpoint.sites.source.selected_partitions();
  if (!load_provider_valid(endpoint.sites.source)) {
    load_refuse(out, LoadCondition::invalid_binding);
    return result;
  }
  LoadScratch scratch;
  // The authenticated full1200 COM record is the sole Z authority. Paired X
  // cancellation is structural in this fixed body: central780, arms30+30 at
  // root +/- .20265, legs180+180 at root +/- .14. No interval cancellation
  // or alternative COM reconstruction is used to grant this identity.
  scratch.com = {point(.16),
                 {endpoint.body->center_of_mass.lower.z,
                  endpoint.body->center_of_mass.upper.z}};
  scratch.centers[0] = {add(point(.16), point(-.14)), point(-.5)};
  scratch.centers[1] = {add(point(.16), point(.14)), point(-.8)};
  scratch.barycenter = {point(.16),
                        multiply(add(point(-.5), point(-.8)), point(.5))};
  scratch.delta = {point(0), subtract(scratch.com.z, scratch.barycenter.z)};
  bool supported = true;
  out.barycenter_z = site_bounds(scratch.barycenter.z, supported);
  out.common_pressure_delta_z = site_bounds(scratch.delta.z, supported);
  for (std::size_t site = 0; site < scratch.pressures.size(); ++site)
    scratch.pressures[site] = {scratch.centers[site].x,
                               add(scratch.centers[site].z, scratch.delta.z)};
  if (!supported || !finite(scratch.pressures[0].z) ||
      !finite(scratch.pressures[1].z)) {
    load_refuse(out, LoadCondition::unsupported_arithmetic);
    return result;
  }
  out.bindings_complete = true;
  out.arithmetic_supported = true;
  out.paired_com_x_identity = true;
  out.positive_reactions = true;
  out.projected_barycenter_identity = true;
  out.vertical_force_identity = true;
  out.vertical_moment_identity = true;
  out.placement_nonpenetrating = true;
  if (max_pressure_partitions == 0) {
    load_refuse(out, LoadCondition::pressure_capacity);
    return result;
  }
  bool all_quads = max_pressure_partitions == sources.size();
  for (std::size_t p = 0; p < max_pressure_partitions; ++p) {
    out.source_quads[p] = load_source_quad(sources[p]);
    ++out.checked_quads;
    const auto& quad = out.source_quads[p];
    out.arithmetic_supported =
        out.arithmetic_supported && quad.arithmetic_supported;
    all_quads = all_quads && quad.valid;
    if (!quad.valid)
      load_refuse(out,
                  !quad.arithmetic_supported
                      ? LoadCondition::unsupported_arithmetic
                      : LoadCondition::invalid_support_quad,
                  {}, p);
  }
  for (std::size_t site = 0; site < out.pressures.size(); ++site)
    load_pressure_scan(site, sources, max_pressure_partitions, out, scratch);
  out.contact_supported = all_quads && out.arithmetic_supported &&
                          out.pressures[0].complete &&
                          out.pressures[1].complete;
  out.projected_margin_certified = out.contact_supported;
  out.load_qualified = out.contact_supported && out.placement_nonpenetrating &&
                       out.vertical_force_identity &&
                       out.vertical_moment_identity;
  out.complete = out.load_qualified && !out.first_refusal;
  if (out.projected_margin_certified)
    out.certified_resultant_disk_radius_metres =
        kBoardingBootPressureRadiusMetres;
  return result;
}
auto assess_origin_boarding_source_endpoint_load(
    const OriginBoardingBootSupport& provider)
    -> std::expected<BoardingSourceEndpointLoadDiagnostic, std::string> {
  return detail::boarding_source_endpoint_load_bounded(provider);
}
namespace {
using TransferCondition = BoardingLowerFootTransferCondition;
using TransferResult = detail::BoardingLowerFootTransferCellResult;
using TransferLimits = detail::BoardingLowerFootTransferLimits;
using TransferWork = BoardingLowerFootTransferCounters;
using TransferCell = BoardingLowerFootTransferCell;
using TransferRefusal = BoardingLowerFootTransferRefusal;
static_assert(sizeof(LoadScratch) + load_nested_scratch + 240 <= 2048);

auto transfer_pressure_fail(TransferRefusal& refusal,
                            TransferCondition condition,
                            TransferResult result = TransferResult::unresolved)
    -> TransferResult {
  refusal.condition = condition;
  return result;
}
auto transfer_pressure_edges(const std::array<Point, 4>& corners,
                             Point pressure, const TransferLimits& limits,
                             TransferWork& work,
                             std::array<BoardingFootSiteEdgeEvidence, 4>& edges,
                             bool& supported, TransferRefusal& refusal)
    -> TransferResult {
  const auto required = add(point(kBoardingBootPressureRadiusMetres),
                            point(kBoardingBootDiskEdgeMarginMetres));
  for (std::size_t i = 0; i < corners.size(); ++i) {
    if (work.disk_edges >= limits.disk_edges)
      return transfer_pressure_fail(refusal, TransferCondition::edge_capacity,
                                    TransferResult::capacity);
    ++work.disk_edges;
    edges[i] = site_edge(corners[i], corners[(i + 1) % corners.size()],
                         pressure, required, supported);
    if (!supported)
      return transfer_pressure_fail(refusal,
                                    TransferCondition::unsupported_arithmetic,
                                    TransferResult::unsupported);
  }
  return TransferResult::accepted;
}
auto transfer_pressure_candidate(const LoadCandidate& candidate)
    -> BoardingLowerFootTransferPressureCandidate {
  return {{candidate.minimum_signed_side.lower,
           candidate.minimum_signed_side.upper},
          {candidate.minimum_squared_gap.lower,
           candidate.minimum_squared_gap.upper},
          candidate.status,
          candidate.arithmetic_supported,
          candidate.quad_valid};
}
} // namespace

auto detail::prepare_boarding_lower_foot_transfer_pressure(
    const BoardingSourceEndpointLoadDiagnostic& initial)
    -> std::expected<BoardingLowerFootTransferPressureContext, std::string> {
  if (!initial.load.complete || !initial.load.bindings_complete ||
      !initial.load.arithmetic_supported ||
      !initial.load.placement_nonpenetrating ||
      !initial.load.contact_supported ||
      !initial.load.projected_margin_certified ||
      initial.load.checked_quads != kBoardingBootSourcePartitionCount ||
      !load_binding(initial.self) ||
      !load_provider_valid(initial.self.endpoint.sites.source))
    return std::unexpected(
        "Transfer pressure requires unchanged genuine complete initial Load01");
  if (!std::ranges::all_of(initial.load.source_quads, [](const LoadQuad& q) {
        return q.valid && q.arithmetic_supported && q.horizontal && q.upward &&
               q.convex && q.nondegenerate && q.checked_edges == 4 &&
               q.checked_side_signs == 8;
      }))
    return std::unexpected(
        "Transfer pressure requires ten once-owned complete source guards");
  return BoardingLowerFootTransferPressureContext(initial);
}
auto detail::boarding_lower_foot_transfer_pressure_cell(
    const BoardingLowerFootTransferPressureContext& context,
    const TransferLimits& limits, TransferWork& work, TransferCell& out,
    TransferRefusal& refusal) -> TransferResult {
  out.complete = out.positive_reactions =
      out.nominal_vertical_equilibrium_complete = out.finite_pressure_complete =
          false;
  if (!site_supported_environment() || !context.initial_ ||
      !out.kinematics_complete || !out.arithmetic_supported) {
    out.arithmetic_supported = false;
    return transfer_pressure_fail(refusal,
                                  TransferCondition::unsupported_arithmetic,
                                  TransferResult::unsupported);
  }
  const auto& initial = *context.initial_;
  const auto sources = initial.self.endpoint.sites.source.selected_partitions();
  if (sources.size() != kBoardingBootSourcePartitionCount)
    return transfer_pressure_fail(refusal, TransferCondition::invalid_binding,
                                  TransferResult::unsupported);
  const auto w = Interval{out.port_reaction_fraction.lower,
                          out.port_reaction_fraction.upper};
  const auto complement = subtract(point(1), w);
  if (!finite(w) || !finite(complement) || w.low <= 0 || complement.low <= 0 ||
      w.high >= 1 || complement.high >= 1)
    return transfer_pressure_fail(refusal,
                                  TransferCondition::unsupported_arithmetic,
                                  TransferResult::unsupported);
  LoadScratch scratch;
  scratch.centers[0] = {add(point(.16), point(-.14)), point(-.5)};
  scratch.centers[1] = {add(point(.16), point(.14)), point(-.8)};
  scratch.com = {
      {out.center_of_mass.value.lower.x, out.center_of_mass.value.upper.x},
      {out.center_of_mass.value.lower.z, out.center_of_mass.value.upper.z}};
  scratch.barycenter = {add(multiply(w, scratch.centers[0].x),
                            multiply(complement, scratch.centers[1].x)),
                        add(multiply(w, scratch.centers[0].z),
                            multiply(complement, scratch.centers[1].z))};
  scratch.delta = subtract(scratch.com, scratch.barycenter);
  if (!finite(scratch.barycenter.x) || !finite(scratch.barycenter.z) ||
      !finite(scratch.delta.x) || !finite(scratch.delta.z))
    return transfer_pressure_fail(refusal,
                                  TransferCondition::unsupported_arithmetic,
                                  TransferResult::unsupported);
  out.barycenter_xz = {{{scratch.barycenter.x.low, scratch.barycenter.x.high},
                        {scratch.barycenter.z.low, scratch.barycenter.z.high}}};
  out.common_pressure_delta_xz = {
      {{scratch.delta.x.low, scratch.delta.x.high},
       {scratch.delta.z.low, scratch.delta.z.high}}};
  // Exact complementary weights and ONE shared delta prove vertical normalized
  // force/moment balance in XZ at these unequal heights. No
  // acceleration/friction force balance is asserted; this remains nominal
  // quasi-static evidence.
  out.positive_reactions = out.nominal_vertical_equilibrium_complete = true;
  for (std::size_t site = 0; site < out.pressures.size(); ++site) {
    refusal.side = site;
    auto& record = out.pressures[site];
    scratch.pressures[site] = {add(scratch.centers[site].x, scratch.delta.x),
                               add(scratch.centers[site].z, scratch.delta.z)};
    const auto pressure = scratch.pressures[site];
    if (!finite(pressure.x) || !finite(pressure.z) || pressure.x.low < -8 ||
        pressure.x.high > 8 || pressure.z.low < -8 || pressure.z.high > 8)
      return transfer_pressure_fail(refusal,
                                    TransferCondition::unsupported_arithmetic,
                                    TransferResult::unsupported);
    record.pressure_xz = {
        {{pressure.x.low, pressure.x.high}, {pressure.z.low, pressure.z.high}}};
    record.arithmetic_supported = true;
    scratch.sole = site_sole_corners(scratch.centers[site]);
    auto result = transfer_pressure_edges(scratch.sole, pressure, limits, work,
                                          scratch.edges,
                                          record.arithmetic_supported, refusal);
    if (result != TransferResult::accepted) return result;
    const auto sole = load_summary(scratch.edges, record.arithmetic_supported);
    record.sole_minimum_signed_side = {sole.minimum_signed_side.lower,
                                       sole.minimum_signed_side.upper};
    record.sole_minimum_squared_gap = {sole.minimum_squared_gap.lower,
                                       sole.minimum_squared_gap.upper};
    record.sole_disk_contained = sole.status == LoadStatus::contained;
    if (!record.sole_disk_contained)
      return transfer_pressure_fail(refusal,
                                    TransferCondition::sole_disk_margin);
    const auto plane = site == 0 ? site_upper_plane : site_transition_plane;
    for (std::size_t p = 0; p < sources.size(); ++p) {
      refusal.partition = p;
      if (work.pressure_candidates >= limits.pressure_candidates)
        return transfer_pressure_fail(refusal,
                                      TransferCondition::pressure_capacity,
                                      TransferResult::capacity);
      ++work.pressure_candidates;
      ++record.scanned_partitions;
      const auto& quad = initial.load.source_quads[p];
      auto& candidate = record.candidates[p];
      candidate.quad_valid = quad.valid;
      candidate.arithmetic_supported = quad.arithmetic_supported;
      if (!quad.valid) {
        candidate.status = LoadStatus::invalid_support;
        record.arithmetic_supported =
            record.arithmetic_supported && quad.arithmetic_supported;
        continue;
      }
      if (sources[p].plane_metres != plane) {
        candidate.status = LoadStatus::noncoplanar;
        continue;
      }
      record.coplanar = true;
      scratch.source = source_corners(sources[p]);
      bool supported{true};
      result = transfer_pressure_edges(scratch.source, pressure, limits, work,
                                       scratch.edges, supported, refusal);
      if (result != TransferResult::accepted) return result;
      const auto summary = load_summary(scratch.edges, supported);
      candidate = transfer_pressure_candidate(summary);
      record.arithmetic_supported = record.arithmetic_supported && supported;
      if (summary.status == LoadStatus::contained &&
          record.source_partition == 65535)
        record.source_partition = static_cast<std::uint16_t>(p);
    }
    record.coverage_complete = record.scanned_partitions == sources.size();
    record.source_disk_contained = record.source_partition != 65535;
    record.complete = record.arithmetic_supported &&
                      record.sole_disk_contained && record.coverage_complete &&
                      record.source_disk_contained;
    if (!record.complete)
      return transfer_pressure_fail(refusal,
                                    TransferCondition::source_disk_margin);
  }
  out.finite_pressure_complete = true;
  refusal.side.reset();
  refusal.partition.reset();
  refusal.condition = TransferCondition::none;
  return TransferResult::accepted;
}
} // namespace apsis_drift

#include "origin_boarding_route_port_unload_internal.hpp"
namespace apsis_drift {
namespace {
using UnloadCell = BoardingRoutePortUnloadCell;
using UnloadReason = BoardingRoutePortUnloadRefusal;
using UnloadWhy = BoardingRoutePortUnloadCondition;
using UnloadState = detail::BoardingRoutePortUnloadCellResult;
using UnloadLimits = detail::BoardingRoutePortUnloadLimits;
using UnloadWork = BoardingRoutePortUnloadCounters;
static_assert(sizeof(detail::BoardingRoutePortUnloadContext) <= 1024);
static_assert(sizeof(LoadScratch) + load_nested_scratch + std::size_t{512} <=
              4096);
auto unload_pressure_refuse(UnloadReason& r, UnloadWhy why) -> UnloadState {
  r.condition = why;
  return why == UnloadWhy::unsupported_arithmetic ||
                 why == UnloadWhy::invalid_binding
             ? UnloadState::unsupported
             : UnloadState::unresolved;
}
auto unload_disk_edges(const std::array<Point, 4>& corners, Point pressure,
                       const UnloadLimits& caps, UnloadWork& work,
                       LoadScratch& scratch, bool& supported,
                       UnloadReason& reason) -> UnloadState {
  const auto required = add(point(.020), point(.010));
  for (std::size_t i = 0; i < 4; ++i) {
    if (work.contact.disk_edges >= caps.edges) {
      reason.condition = UnloadWhy::edge_capacity;
      return UnloadState::capacity;
    }
    ++work.contact.disk_edges;
    scratch.edges[i] = site_edge(corners[i], corners[(i + 1) % 4], pressure,
                                 required, supported);
    if (!supported)
      return unload_pressure_refuse(reason, UnloadWhy::unsupported_arithmetic);
  }
  return UnloadState::accepted;
}
auto unload_pressure_algebra(LoadScratch& s, Interval w) -> bool {
  const auto other = subtract(point(1), w);
  s.barycenter = {
      add(multiply(w, s.centers[0].x), multiply(other, s.centers[1].x)),
      add(multiply(w, s.centers[0].z), multiply(other, s.centers[1].z))};
  s.delta = subtract(s.com, s.barycenter);
  for (std::size_t i = 0; i < 2; ++i) {
    s.pressures[i] = {add(s.centers[i].x, s.delta.x),
                      add(s.centers[i].z, s.delta.z)};
    if ((i == 1 && w.low == 0 && w.high == 0) ||
        (i == 0 && w.low == 1 && w.high == 1))
      s.pressures[i] = s.com;
    if (!finite(s.pressures[i].x) || !finite(s.pressures[i].z)) return false;
  }
  return finite(s.barycenter.x) && finite(s.barycenter.z) &&
         finite(s.delta.x) && finite(s.delta.z);
}
} // namespace
[[gnu::noinline]] auto detail::prepare_boarding_route_port_unload(
    const OriginBoardingBootSupport& source, const UnloadLimits& limits,
    BoardingRoutePortUnloadDiagnostic& out)
    -> std::expected<BoardingRoutePortUnloadContext, std::string> {
  const UnloadLimits original;
  if (limits.initial_source_partitions > original.initial_source_partitions ||
      limits.initial_body_records > original.initial_body_records ||
      limits.initial_self_pairs > original.initial_self_pairs ||
      limits.initial_self_axes > original.initial_self_axes ||
      limits.initial_pressure_partitions >
          original.initial_pressure_partitions ||
      limits.output_bytes > original.output_bytes ||
      limits.pairs > original.pairs || limits.axes > original.axes ||
      limits.signed_trials > original.signed_trials ||
      limits.owners > original.owners ||
      limits.candidates > original.candidates ||
      limits.edges > original.edges ||
      limits.phase.depth > original.phase.depth ||
      limits.phase.nodes > original.phase.nodes ||
      limits.phase.leaves > original.phase.leaves ||
      limits.phase.output_bytes > original.phase.output_bytes ||
      limits.phase.graphs > original.phase.graphs ||
      limits.phase.legs > original.phase.legs ||
      limits.phase.bodies > original.phase.bodies ||
      limits.phase.sectors > original.phase.sectors ||
      limits.phase.timing > original.phase.timing)
    return std::unexpected("Port unload preparation permits only lowered caps");
  if (limits.output_bytes <
      sizeof(std::expected<BoardingRoutePortUnloadDiagnostic, std::string>) +
          sizeof(BoardingSourceEndpointLoadDiagnostic))
    return std::unexpected("Port unload initial output exceeds capacity");
  auto initial = boarding_source_endpoint_load_bounded(
      source, limits.initial_source_partitions, limits.initial_body_records,
      limits.initial_self_pairs, limits.initial_self_axes,
      limits.initial_pressure_partitions);
  if (!initial) return std::unexpected(initial.error());
  try {
    out.initial = std::make_unique<BoardingSourceEndpointLoadDiagnostic>(
        std::move(*initial));
  } catch (const std::bad_alloc&) {
    return std::unexpected("Port unload initial allocation failed");
  }
  const auto& owned = *out.initial;
  if (!owned.load.complete || !owned.load.bindings_complete ||
      !owned.load.arithmetic_supported ||
      !owned.load.placement_nonpenetrating || !owned.load.contact_supported ||
      !owned.load.projected_margin_certified ||
      owned.load.checked_quads != 10 || !load_binding(owned.self) ||
      !load_provider_valid(owned.self.endpoint.sites.source))
    return std::unexpected(
        "Port unload requires genuine complete original Load01");
  if (!std::ranges::all_of(owned.load.source_quads, [](const LoadQuad& q) {
        return q.valid && q.arithmetic_supported && q.horizontal && q.upward &&
               q.convex && q.nondegenerate && q.checked_edges == 4 &&
               q.checked_side_signs == 8;
      }))
    return std::unexpected(
        "Port unload requires complete once-owned source guards");
  return BoardingRoutePortUnloadContext(owned);
}
auto detail::boarding_route_port_unload_pressure(
    const BoardingRoutePortUnloadCellToken& token, const UnloadLimits& caps,
    UnloadWork& work, UnloadCell& out, UnloadReason& reason) -> UnloadState {
  out.complete = out.nonnegative_reactions =
      out.nominal_vertical_equilibrium_complete = out.finite_pressure_complete =
          out.endpoint_zero_port_reaction = false;
  if (!site_supported_environment() || !token.context_ || token.cell_ != &out ||
      !out.phase.complete || !out.phase.arithmetic_supported ||
      !out.self_complete) {
    out.arithmetic_supported = false;
    return unload_pressure_refuse(reason, UnloadWhy::unsupported_arithmetic);
  }
  const auto& context = *token.context_;
  const auto sources = context.source_.selected_partitions();
  if (!load_provider_valid(context.source_) || sources.size() != 10) {
    out.arithmetic_supported = false;
    return unload_pressure_refuse(reason, UnloadWhy::invalid_binding);
  }
  const UnloadLimits original;
  if (caps.candidates > original.candidates || caps.edges > original.edges) {
    out.arithmetic_supported = false;
    return unload_pressure_refuse(reason, UnloadWhy::invalid_binding);
  }
  if (work.contact.pressure_candidates > caps.candidates) {
    reason.condition = UnloadWhy::pressure_capacity;
    return UnloadState::capacity;
  }
  if (work.contact.disk_edges > caps.edges) {
    reason.condition = UnloadWhy::edge_capacity;
    return UnloadState::capacity;
  }
  // Exact named carrier .5*(1-S) proves [0,.5] before intersection.
  const auto reported = out.phase.port_reaction_fraction;
  if (!std::isfinite(reported.lower) || !std::isfinite(reported.upper) ||
      reported.lower > reported.upper) {
    out.arithmetic_supported = false;
    return unload_pressure_refuse(reason, UnloadWhy::unsupported_arithmetic);
  }
  auto w = Interval{std::max(0., reported.lower), std::min(.5, reported.upper)};
  if (out.phase.first == out.phase.last && out.phase.first == 1)
    w = point(0);
  else if (out.phase.first == out.phase.last && out.phase.first == 0)
    w = point(.5);
  const auto other = subtract(point(1), w);
  if (!finite(w) || !finite(other)) {
    out.arithmetic_supported = false;
    return unload_pressure_refuse(reason, UnloadWhy::unsupported_arithmetic);
  }
  LoadScratch scratch;
  scratch.centers[0] = {add(point(.16), point(-.14)), point(-.5)};
  scratch.centers[1] = {add(point(.16), point(.14)), point(-.8)};
  scratch.com = {{out.phase.center_of_mass.value.lower.x,
                  out.phase.center_of_mass.value.upper.x},
                 {out.phase.center_of_mass.value.lower.z,
                  out.phase.center_of_mass.value.upper.z}};
  if (!unload_pressure_algebra(scratch, w)) {
    out.arithmetic_supported = false;
    return unload_pressure_refuse(reason, UnloadWhy::unsupported_arithmetic);
  }
  out.port_reaction_fraction = {w.low, w.high};
  out.barycenter_xz = {{{scratch.barycenter.x.low, scratch.barycenter.x.high},
                        {scratch.barycenter.z.low, scratch.barycenter.z.high}}};
  out.common_pressure_delta_xz = {
      {{scratch.delta.x.low, scratch.delta.x.high},
       {scratch.delta.z.low, scratch.delta.z.high}}};
  out.nonnegative_reactions = out.nominal_vertical_equilibrium_complete = true;
  out.endpoint_zero_port_reaction = out.phase.last == 1;
  for (std::size_t site = 0; site < 2; ++site) {
    reason.side = site;
    auto& record = out.pressures[site];
    const auto pressure = scratch.pressures[site];
    if (!finite(pressure.x) || !finite(pressure.z) || pressure.x.low < -8 ||
        pressure.x.high > 8 || pressure.z.low < -8 || pressure.z.high > 8) {
      out.arithmetic_supported = false;
      return unload_pressure_refuse(reason, UnloadWhy::unsupported_arithmetic);
    }
    record.pressure_xz = {
        {{pressure.x.low, pressure.x.high}, {pressure.z.low, pressure.z.high}}};
    record.arithmetic_supported = true;
    scratch.sole = site_sole_corners(scratch.centers[site]);
    auto state = unload_disk_edges(scratch.sole, pressure, caps, work, scratch,
                                   record.arithmetic_supported, reason);
    if (state != UnloadState::accepted) return state;
    const auto sole = load_summary(scratch.edges, record.arithmetic_supported);
    record.sole_minimum_signed_side = {sole.minimum_signed_side.lower,
                                       sole.minimum_signed_side.upper};
    record.sole_minimum_squared_gap = {sole.minimum_squared_gap.lower,
                                       sole.minimum_squared_gap.upper};
    record.sole_disk_contained = sole.status == LoadStatus::contained;
    if (!record.sole_disk_contained)
      return unload_pressure_refuse(reason, UnloadWhy::sole_disk_margin);
    const auto plane = site == 0 ? site_upper_plane : site_transition_plane;
    for (std::size_t p = 0; p < sources.size(); ++p) {
      reason.partition = p;
      if (work.contact.pressure_candidates >= caps.candidates) {
        reason.condition = UnloadWhy::pressure_capacity;
        return UnloadState::capacity;
      }
      ++work.contact.pressure_candidates;
      ++record.scanned_partitions;
      const auto& quad = context.guards_[p];
      auto& candidate = record.candidates[p];
      candidate.quad_valid = quad.valid;
      candidate.arithmetic_supported = quad.arithmetic_supported;
      if (!quad.valid) {
        candidate.status = LoadStatus::invalid_support;
        continue;
      }
      if (sources[p].plane_metres != plane) {
        candidate.status = LoadStatus::noncoplanar;
        continue;
      }
      record.coplanar = true;
      scratch.source = source_corners(sources[p]);
      bool supported = true;
      state = unload_disk_edges(scratch.source, pressure, caps, work, scratch,
                                supported, reason);
      if (state != UnloadState::accepted) return state;
      const auto summary = load_summary(scratch.edges, supported);
      candidate = transfer_pressure_candidate(summary);
      record.arithmetic_supported = record.arithmetic_supported && supported;
      if (summary.status == LoadStatus::contained &&
          record.source_partition == 65535)
        record.source_partition = static_cast<std::uint16_t>(p);
    }
    record.coverage_complete = record.scanned_partitions == 10;
    record.source_disk_contained = record.source_partition != 65535;
    record.complete = record.arithmetic_supported &&
                      record.sole_disk_contained && record.coverage_complete &&
                      record.source_disk_contained;
    if (!record.complete)
      return unload_pressure_refuse(reason, UnloadWhy::source_disk_margin);
  }
  out.finite_pressure_complete = true;
  reason.side.reset();
  reason.partition.reset();
  return UnloadState::accepted;
}
} // namespace apsis_drift

namespace apsis_drift {
auto detail::boarding_route_port_unload_pressure_math(
    std::array<BoardingPlantedLegScalarBounds, 2> com,
    std::array<std::array<BoardingPlantedLegScalarBounds, 2>, 2> centers,
    BoardingPlantedLegScalarBounds fraction, std::array<double, 2> planes,
    double source_plane, std::size_t side)
    -> std::expected<BoardingRoutePortUnloadPressureMath, std::string> {
  if (!load_input_bound(com[0]) || !load_input_bound(com[1]) ||
      !load_input_bound(fraction) || fraction.lower < 0 || fraction.upper > 1 ||
      side > 1 || !load_finite(source_plane) || !load_finite(planes[0]) ||
      !load_finite(planes[1]))
    return std::unexpected("Numeric unload pressure requires ordered finite "
                           "workspace and weight[0,1]");
  for (const auto& c : centers)
    for (const auto b : c)
      if (!load_input_bound(b))
        return std::unexpected(
            "Numeric unload sole coordinates require ordered finite workspace");
  BoardingRoutePortUnloadPressureMath out;
  if (!site_supported_environment()) return out;
  LoadScratch scratch;
  scratch.com = {{com[0].lower, com[0].upper}, {com[1].lower, com[1].upper}};
  for (std::size_t i = 0; i < 2; ++i)
    scratch.centers[i] = {{centers[i][0].lower, centers[i][0].upper},
                          {centers[i][1].lower, centers[i][1].upper}};
  if (!unload_pressure_algebra(scratch, {fraction.lower, fraction.upper}))
    return out;
  out.barycenter_xz = {{{scratch.barycenter.x.low, scratch.barycenter.x.high},
                        {scratch.barycenter.z.low, scratch.barycenter.z.high}}};
  out.delta_xz = {{{scratch.delta.x.low, scratch.delta.x.high},
                   {scratch.delta.z.low, scratch.delta.z.high}}};
  for (std::size_t i = 0; i < 2; ++i)
    out.pressure_xz[i] = {
        {{scratch.pressures[i].x.low, scratch.pressures[i].x.high},
         {scratch.pressures[i].z.low, scratch.pressures[i].z.high}}};
  out.arithmetic_supported = true;
  out.selected_plane_equal = planes[side] == source_plane;
  return out;
}
} // namespace apsis_drift

#include "origin_boarding_route_checkpoint_unload_internal.hpp"
namespace apsis_drift {
[[gnu::noinline]] auto detail::prepare_boarding_route_checkpoint_unload(
    const OriginBoardingBootSupport& source,
    const BoardingRouteCheckpointUnloadLimits& limits,
    BoardingRouteCheckpointUnloadDiagnostic& out)
    -> std::expected<BoardingRouteCheckpointUnloadContext, std::string> {
  if (limits.output_bytes <
      sizeof(
          std::expected<BoardingRouteCheckpointUnloadDiagnostic, std::string>) +
          sizeof(BoardingSourceEndpointLoadDiagnostic)) {
    out.initial.reset();
    return std::unexpected("Checkpoint unload initial output exceeds capacity");
  }
  BoardingRoutePortUnloadDiagnostic staged;
  auto prepared = prepare_boarding_route_port_unload(source, limits, staged);
  out.initial = std::move(staged.initial);
  if (!prepared) return std::unexpected(prepared.error());
  return std::move(*prepared);
}
auto detail::boarding_route_checkpoint_unload_pressure(
    const BoardingRouteCheckpointUnloadCellToken& token,
    const UnloadLimits& caps, UnloadWork& work, UnloadCell& out,
    UnloadReason& reason) -> UnloadState {
  out.complete = out.nonnegative_reactions =
      out.nominal_vertical_equilibrium_complete = out.finite_pressure_complete =
          out.endpoint_zero_port_reaction = false;
  if (token.phase_index_ > 2 || !token.context_ || token.cell_ != &out) {
    out.arithmetic_supported = false;
    return unload_pressure_refuse(reason, UnloadWhy::invalid_binding);
  }
  if (!site_supported_environment() || !token.context_ || token.cell_ != &out ||
      !out.phase.complete || !out.phase.arithmetic_supported ||
      !out.self_complete) {
    out.arithmetic_supported = false;
    return unload_pressure_refuse(reason, UnloadWhy::unsupported_arithmetic);
  }
  const auto& context = *token.context_;
  const auto sources = context.source_.selected_partitions();
  if (!load_provider_valid(context.source_) || sources.size() != 10) {
    out.arithmetic_supported = false;
    return unload_pressure_refuse(reason, UnloadWhy::invalid_binding);
  }
  const UnloadLimits original;
  if (caps.candidates > original.candidates || caps.edges > original.edges) {
    out.arithmetic_supported = false;
    return unload_pressure_refuse(reason, UnloadWhy::invalid_binding);
  }
  if (work.contact.pressure_candidates > caps.candidates) {
    reason.condition = UnloadWhy::pressure_capacity;
    return UnloadState::capacity;
  }
  if (work.contact.disk_edges > caps.edges) {
    reason.condition = UnloadWhy::edge_capacity;
    return UnloadState::capacity;
  }
  // Private immutable profile proves its range before enclosure intersection.
  // Phase0/1 local1 are .25; only phase2 local1 is zero.
  constexpr std::array<std::array<double, 2>, 3> profiles{
      {{{.5, .25}}, {{.25, .25}}, {{.25, 0}}}};
  const auto profile = profiles[token.phase_index_];
  const auto reported = out.phase.port_reaction_fraction;
  if (!std::isfinite(reported.lower) || !std::isfinite(reported.upper) ||
      reported.lower > reported.upper) {
    out.arithmetic_supported = false;
    return unload_pressure_refuse(reason, UnloadWhy::unsupported_arithmetic);
  }
  auto w = Interval{std::max(profile[1], reported.lower),
                    std::min(profile[0], reported.upper)};
  if (token.phase_index_ == 1) w = point(.25);
  if (out.phase.first == out.phase.last && out.phase.first == 1)
    w = point(profile[1]);
  else if (out.phase.first == out.phase.last && out.phase.first == 0)
    w = point(profile[0]);
  const auto other = subtract(point(1), w);
  if (!finite(w) || !finite(other)) {
    out.arithmetic_supported = false;
    return unload_pressure_refuse(reason, UnloadWhy::unsupported_arithmetic);
  }
  LoadScratch scratch;
  scratch.centers[0] = {add(point(.16), point(-.14)), point(-.5)};
  scratch.centers[1] = {add(point(.16), point(.14)), point(-.8)};
  scratch.com = {{out.phase.center_of_mass.value.lower.x,
                  out.phase.center_of_mass.value.upper.x},
                 {out.phase.center_of_mass.value.lower.z,
                  out.phase.center_of_mass.value.upper.z}};
  if (!unload_pressure_algebra(scratch, w)) {
    out.arithmetic_supported = false;
    return unload_pressure_refuse(reason, UnloadWhy::unsupported_arithmetic);
  }
  out.port_reaction_fraction = {w.low, w.high};
  out.barycenter_xz = {{{scratch.barycenter.x.low, scratch.barycenter.x.high},
                        {scratch.barycenter.z.low, scratch.barycenter.z.high}}};
  out.common_pressure_delta_xz = {
      {{scratch.delta.x.low, scratch.delta.x.high},
       {scratch.delta.z.low, scratch.delta.z.high}}};
  out.nonnegative_reactions = out.nominal_vertical_equilibrium_complete = true;
  out.endpoint_zero_port_reaction =
      token.phase_index_ == 2 && out.phase.last == 1;
  for (std::size_t site = 0; site < 2; ++site) {
    reason.side = site;
    auto& record = out.pressures[site];
    const auto pressure = scratch.pressures[site];
    if (!finite(pressure.x) || !finite(pressure.z) || pressure.x.low < -8 ||
        pressure.x.high > 8 || pressure.z.low < -8 || pressure.z.high > 8) {
      out.arithmetic_supported = false;
      return unload_pressure_refuse(reason, UnloadWhy::unsupported_arithmetic);
    }
    record.pressure_xz = {
        {{pressure.x.low, pressure.x.high}, {pressure.z.low, pressure.z.high}}};
    record.arithmetic_supported = true;
    scratch.sole = site_sole_corners(scratch.centers[site]);
    auto state = unload_disk_edges(scratch.sole, pressure, caps, work, scratch,
                                   record.arithmetic_supported, reason);
    if (state != UnloadState::accepted) return state;
    const auto sole = load_summary(scratch.edges, record.arithmetic_supported);
    record.sole_minimum_signed_side = {sole.minimum_signed_side.lower,
                                       sole.minimum_signed_side.upper};
    record.sole_minimum_squared_gap = {sole.minimum_squared_gap.lower,
                                       sole.minimum_squared_gap.upper};
    record.sole_disk_contained = sole.status == LoadStatus::contained;
    if (!record.sole_disk_contained)
      return unload_pressure_refuse(reason, UnloadWhy::sole_disk_margin);
    const auto plane = site == 0 ? site_upper_plane : site_transition_plane;
    for (std::size_t p = 0; p < sources.size(); ++p) {
      reason.partition = p;
      if (work.contact.pressure_candidates >= caps.candidates) {
        reason.condition = UnloadWhy::pressure_capacity;
        return UnloadState::capacity;
      }
      ++work.contact.pressure_candidates;
      ++record.scanned_partitions;
      const auto& quad = context.guards_[p];
      auto& candidate = record.candidates[p];
      candidate.quad_valid = quad.valid;
      candidate.arithmetic_supported = quad.arithmetic_supported;
      if (!quad.valid) {
        candidate.status = LoadStatus::invalid_support;
        continue;
      }
      if (sources[p].plane_metres != plane) {
        candidate.status = LoadStatus::noncoplanar;
        continue;
      }
      record.coplanar = true;
      scratch.source = source_corners(sources[p]);
      bool supported = true;
      state = unload_disk_edges(scratch.source, pressure, caps, work, scratch,
                                supported, reason);
      if (state != UnloadState::accepted) return state;
      const auto summary = load_summary(scratch.edges, supported);
      candidate = transfer_pressure_candidate(summary);
      record.arithmetic_supported = record.arithmetic_supported && supported;
      if (summary.status == LoadStatus::contained &&
          record.source_partition == 65535)
        record.source_partition = static_cast<std::uint16_t>(p);
    }
    record.coverage_complete = record.scanned_partitions == 10;
    record.source_disk_contained = record.source_partition != 65535;
    record.complete = record.arithmetic_supported &&
                      record.sole_disk_contained && record.coverage_complete &&
                      record.source_disk_contained;
    if (!record.complete)
      return unload_pressure_refuse(reason, UnloadWhy::source_disk_margin);
  }
  out.finite_pressure_complete = true;
  reason.side.reset();
  reason.partition.reset();
  return UnloadState::accepted;
}
} // namespace apsis_drift

#include "origin_boarding_intermediate_pause_support_internal.hpp"
namespace apsis_drift {
namespace {
using PauseWhy = BoardingIntermediatePauseCondition;
using PauseStatus = BoardingIntermediatePauseDiskStatus;
using PauseCtor = BoardingIntermediatePauseConstructorEvidence;
using PauseCtorLimits = detail::BoardingIntermediatePauseConstructorLimits;
using PauseDiag = BoardingIntermediatePauseSupportDiagnostic;
using PauseLimits = detail::BoardingIntermediatePauseLimits;
auto pause_ctor_charge(std::size_t& count, std::size_t maximum, PauseCtor& e,
                       PauseWhy why) -> bool {
  if (count >= maximum) {
    if (e.condition == PauseWhy::none) e.condition = why;
    return false;
  }
  ++count;
  return true;
}
auto pause_quad_failure(PauseCtor& e, PauseWhy why) -> bool {
  if (e.condition == PauseWhy::none) e.condition = why;
  return false;
}
auto pause_input(std::array<BoardingPlantedLegScalarBounds, 2> p) -> bool {
  return load_input_bound(p[0]) && load_input_bound(p[1]);
}
auto pause_edge_status(const std::array<BoardingFootSiteEdgeEvidence, 4>& edges)
    -> PauseStatus {
  bool contained = true, refuted = false;
  for (const auto& e : edges) {
    contained = contained && e.disk_contained;
    refuted =
        refuted || (e.signed_side.supported && e.signed_side.upper <= 0) ||
        (e.squared_margin_gap.supported && e.squared_margin_gap.upper < 0);
  }
  return contained ? PauseStatus::contained
         : refuted ? PauseStatus::refuted
                   : PauseStatus::unresolved;
}
auto pause_refuse(PauseDiag& d, PauseWhy why,
                  std::optional<std::size_t> site = {},
                  std::optional<std::size_t> edge = {}, bool source = false)
    -> void {
  if (why == PauseWhy::pressure_capacity || why == PauseWhy::edge_capacity ||
      why == PauseWhy::unsupported_arithmetic)
    d.stop_condition = why;
  if (!d.first_refusal) {
    BoardingIntermediatePauseRefusal r;
    r.condition = why;
    r.side = site;
    r.edge = edge;
    r.source_edge = source;
    d.first_refusal = r;
  }
}
constexpr std::size_t pause_pressure_scratch =
    sizeof(LoadScratch) + load_nested_scratch +
    2 * sizeof(BoardingIntermediatePauseRefusal) + 512;
static_assert(pause_pressure_scratch <= 4096);
constexpr std::size_t pause_quad_scratch =
    2 * sizeof(BoardingIntermediatePauseQuadEvidence) + 2 * sizeof(Point) +
    10 * sizeof(Interval) + 2 * sizeof(BoardingFootSiteEdgeEvidence) + 256;
static_assert(pause_quad_scratch <= 2048);
} // namespace
[[gnu::noinline]] auto detail::intermediate_pause_quad_bridge(
    const BoardingBootSourcePartition& p, const PauseCtorLimits& l,
    PauseCtor& e, std::size_t side) -> bool {
  if (side >= 2) return pause_quad_failure(e, PauseWhy::source_geometry);
  e.side = side;
  if (!pause_ctor_charge(e.work.quad_records, l.quad_records, e,
                         PauseWhy::quad_capacity))
    return false;
  auto& q = e.quads[side];
  q = {};
  q.evaluated = e.geometry_evaluated[side] = true;
  if (!pause_ctor_charge(e.work.base_guards, l.base_guards, e,
                         PauseWhy::base_capacity))
    return false;
  if (!site_supported_environment() || !load_finite(p.plane_metres) ||
      !std::ranges::all_of(p.perimeter_metres,
                           [](Vec v) { return load_finite(v); })) {
    e.arithmetic_supported = false;
    return pause_quad_failure(e, PauseWhy::unsupported_arithmetic);
  }
  q.arithmetic_supported = true;
  q.horizontal = true;
  q.convex = true;
  q.nondegenerate = true;
  q.corner_membership = q.distinct_face_vertices = true;
  for (const auto corner : p.perimeter_metres)
    q.horizontal = q.horizontal && corner.y == p.plane_metres;
  for (std::size_t i = 0; i < 4; ++i) {
    if (!pause_ctor_charge(e.work.quad_edges, l.quad_edges, e,
                           PauseWhy::quad_edge_capacity))
      return false;
    const auto a = wide(p.perimeter_metres[i]),
               b = wide(p.perimeter_metres[(i + 1) % 4]);
    const auto delta = subtract(b, a);
    const auto length = add(square(delta.x), square(delta.z));
    load_minimum(q.minimum_edge_squared, length, q.checked_edges == 0,
                 q.arithmetic_supported);
    ++q.checked_edges;
    q.nondegenerate = q.nondegenerate && finite(length) && length.low > 0;
    for (std::size_t j = 2; j < 4; ++j) {
      if (!pause_ctor_charge(e.work.quad_sides, l.quad_sides, e,
                             PauseWhy::quad_side_capacity))
        return false;
      const auto signed_side =
          edge_side(a, b, wide(p.perimeter_metres[(i + j) % 4]));
      load_minimum(q.minimum_signed_side, signed_side, q.checked_sides == 0,
                   q.arithmetic_supported);
      ++q.checked_sides;
      q.convex = q.convex && finite(signed_side) && signed_side.low > 0;
    }
  }
  std::array<std::size_t, 4> incidence{};
  bool upward = true;
  for (const auto& face : p.faces) {
    std::array<bool, 4> seen{};
    for (const auto vertex : face.points_current_metres) {
      if (!pause_ctor_charge(e.work.face_vertices, l.face_vertices, e,
                             PauseWhy::vertex_capacity))
        return false;
      ++q.checked_vertices;
      if (!load_finite(vertex)) {
        q.arithmetic_supported = e.arithmetic_supported = false;
        return pause_quad_failure(e, PauseWhy::unsupported_arithmetic);
      }
      q.horizontal = q.horizontal && vertex.y == p.plane_metres;
      bool found = false;
      for (std::size_t i = 0; i < 4; ++i) {
        if (!pause_ctor_charge(e.work.corner_matches, l.corner_matches, e,
                               PauseWhy::corner_capacity))
          return false;
        if (vertex == p.perimeter_metres[i]) {
          q.distinct_face_vertices = q.distinct_face_vertices && !seen[i];
          seen[i] = true;
          ++incidence[i];
          found = true;
          break;
        }
      }
      q.corner_membership = q.corner_membership && found;
    }
    if (!pause_ctor_charge(e.work.face_windings, l.face_windings, e,
                           PauseWhy::winding_capacity))
      return false;
    const auto winding = edge_side(wide(face.points_current_metres[0]),
                                   wide(face.points_current_metres[1]),
                                   wide(face.points_current_metres[2]));
    ++q.checked_windings;
    q.arithmetic_supported = q.arithmetic_supported && finite(winding);
    upward = upward && finite(winding) && winding.low > 0;
  }
  std::size_t first{}, last{}, shared{}, single{};
  for (std::size_t i = 0; i < 4; ++i) {
    if (!pause_ctor_charge(e.work.incidence, l.incidence, e,
                           PauseWhy::incidence_capacity))
      return false;
    ++q.checked_incidence;
    if (incidence[i] == 2) {
      if (shared == 0) first = i;
      last = i;
      ++shared;
    } else if (incidence[i] == 1)
      ++single;
  }
  q.incidence_valid = shared == 2 && single == 2;
  if (!pause_ctor_charge(e.work.diagonals, l.diagonals, e,
                         PauseWhy::diagonal_capacity))
    return false;
  q.diagonal_valid = q.incidence_valid && last - first == 2;
  q.upward = q.horizontal && q.convex && upward;
  q.minimum_edge_squared.supported = q.minimum_signed_side.supported =
      q.arithmetic_supported;
  q.complete = q.arithmetic_supported && q.horizontal && q.convex &&
               q.nondegenerate && q.upward && q.corner_membership &&
               q.distinct_face_vertices && q.incidence_valid &&
               q.diagonal_valid;
  if (!q.arithmetic_supported) {
    e.arithmetic_supported = false;
    return pause_quad_failure(e, PauseWhy::unsupported_arithmetic);
  }
  return q.complete || pause_quad_failure(e, PauseWhy::source_geometry);
}
auto detail::intermediate_pause_pressure_math(
    std::array<BoardingPlantedLegScalarBounds, 2> com,
    std::array<std::array<BoardingPlantedLegScalarBounds, 2>, 2> centers,
    BoardingPlantedLegScalarBounds w) -> BoardingIntermediatePausePressureMath {
  BoardingIntermediatePausePressureMath r;
  if (!site_supported_environment() || !pause_input(com) ||
      !pause_input(centers[0]) || !pause_input(centers[1]) ||
      !load_input_bound(w) || w.lower < 0 || w.upper > 1)
    return r;
  LoadScratch s;
  s.com = {{com[0].lower, com[0].upper}, {com[1].lower, com[1].upper}};
  for (std::size_t i = 0; i < 2; ++i)
    s.centers[i] = {{centers[i][0].lower, centers[i][0].upper},
                    {centers[i][1].lower, centers[i][1].upper}};
  if (!unload_pressure_algebra(s, {w.lower, w.upper})) return r;
  r.barycenter_xz = {{{s.barycenter.x.low, s.barycenter.x.high},
                      {s.barycenter.z.low, s.barycenter.z.high}}};
  r.delta_xz = {
      {{s.delta.x.low, s.delta.x.high}, {s.delta.z.low, s.delta.z.high}}};
  for (std::size_t i = 0; i < 2; ++i)
    r.pressure_xz[i] = {{{s.pressures[i].x.low, s.pressures[i].x.high},
                         {s.pressures[i].z.low, s.pressures[i].z.high}}};
  r.arithmetic_supported = true;
  return r;
}
auto detail::intermediate_pause_disk_math(
    std::array<RigidVector3, 4> p,
    std::array<BoardingPlantedLegScalarBounds, 2> pressure, std::size_t maximum)
    -> BoardingIntermediatePauseDiskMath {
  BoardingIntermediatePauseDiskMath r;
  if (maximum > 4) {
    r.condition = PauseWhy::invalid_limits;
    return r;
  }
  if (!site_supported_environment() || !pause_input(pressure) ||
      !std::ranges::all_of(p, [](Vec v) { return load_finite(v); })) {
    r.condition = PauseWhy::unsupported_arithmetic;
    return r;
  }
  const auto quad = load_quad(p, p[0].y);
  if (!quad.valid) {
    r.condition = PauseWhy::source_geometry;
    return r;
  }
  r.arithmetic_supported = true;
  const Point center{{pressure[0].lower, pressure[0].upper},
                     {pressure[1].lower, pressure[1].upper}};
  const auto required = add(point(.020), point(.010));
  for (std::size_t i = 0; i < 4; ++i) {
    if (r.edge_checks >= maximum) {
      r.condition = PauseWhy::edge_capacity;
      return r;
    }
    ++r.edge_checks;
    r.evaluated[i] = true;
    r.edges[i] = site_edge(wide(p[i]), wide(p[(i + 1) % 4]), center, required,
                           r.arithmetic_supported);
    if (!r.arithmetic_supported) {
      r.condition = PauseWhy::unsupported_arithmetic;
      return r;
    }
  }
  r.status = pause_edge_status(r.edges);
  return r;
}
[[gnu::noinline]] auto detail::intermediate_pause_pressure_bridge(
    const BoardingIntermediatePauseProjectionToken& token, const PauseLimits& l,
    PauseDiag& d) -> void {
  // Token was privately issued only after one fresh complete fixed body graph.
  // Its provider is retained by the owning report; no caller fixture enters.
  if (!token.provider_ || !token.cell_ ||
      !BoardingIntermediatePauseSupportAccess::valid(*token.provider_)) {
    pause_refuse(d, PauseWhy::invalid_binding);
    return;
  }
  const auto& cell = *token.cell_;
  LoadScratch s;
  const auto& com = cell.center_of_mass.value;
  s.com = {{com.lower.x, com.upper.x}, {com.lower.z, com.upper.z}};
  d.com_xz = {{{com.lower.x, com.upper.x}, {com.lower.z, com.upper.z}}};
  for (std::size_t i = 0; i < 2; ++i) {
    const auto id = i == 0 ? BoardingPlantedBodyPointId::port_boot_center
                           : BoardingPlantedBodyPointId::starboard_boot_center;
    const auto& b = cell.points[static_cast<std::size_t>(id)].value;
    s.centers[i] = {{b.lower.x, b.upper.x}, {b.lower.z, b.upper.z}};
    d.boot_centers_xz[i] = {{{b.lower.x, b.upper.x}, {b.lower.z, b.upper.z}}};
    d.sites[i].loaded = true;
  }
  // Shared algebra computes BOTH candidate positions. Eagerly charge each
  // selected obligation before entering it, including a cap1 partial refusal.
  for (std::size_t i = 0; i < 2; ++i) {
    if (d.work.pressure_candidates >= l.pressure_candidates) {
      pause_refuse(d, PauseWhy::pressure_capacity, i);
      return;
    }
    ++d.work.pressure_candidates;
  }
  if (!unload_pressure_algebra(s, {cell.port_reaction_fraction.lower,
                                   cell.port_reaction_fraction.upper})) {
    d.arithmetic_supported = false;
    pause_refuse(d, PauseWhy::unsupported_arithmetic);
    return;
  }
  d.barycenter_xz = {{{s.barycenter.x.low, s.barycenter.x.high},
                      {s.barycenter.z.low, s.barycenter.z.high}}};
  d.delta_xz = {
      {{s.delta.x.low, s.delta.x.high}, {s.delta.z.low, s.delta.z.high}}};
  d.nominal_equilibrium = true;
  const auto required = add(point(.020), point(.010));
  for (std::size_t i = 0; i < 2; ++i) {
    auto& record = d.sites[i];
    record.evaluated = true;
    const auto pressure = s.pressures[i];
    record.pressure_xz = {
        {{pressure.x.low, pressure.x.high}, {pressure.z.low, pressure.z.high}}};
    s.sole = site_sole_corners(s.centers[i]);
    const auto* source =
        BoardingIntermediatePauseSupportAccess::partition(*token.provider_, i);
    if (!source) {
      pause_refuse(d, PauseWhy::invalid_binding, i);
      return;
    }
    s.source = source_corners(*source);
    for (std::size_t which = 0; which < 2; ++which) {
      const auto& corners = which == 0 ? s.sole : s.source;
      auto& edges = which == 0 ? record.sole_edges : record.source_edges;
      auto& evaluated =
          which == 0 ? record.sole_evaluated : record.source_evaluated;
      for (std::size_t edge = 0; edge < 4; ++edge) {
        if (d.work.disk_edges >= l.disk_edges) {
          pause_refuse(d, PauseWhy::edge_capacity, i, edge, which != 0);
          return;
        }
        ++d.work.disk_edges;
        evaluated[edge] = true;
        edges[edge] = site_edge(corners[edge], corners[(edge + 1) % 4],
                                pressure, required, d.arithmetic_supported);
        if (!d.arithmetic_supported) {
          pause_refuse(d, PauseWhy::unsupported_arithmetic, i, edge,
                       which != 0);
          return;
        }
        if (!edges[edge].disk_contained) {
          pause_refuse(d,
                       which == 0 ? PauseWhy::sole_disk : PauseWhy::source_disk,
                       i, edge, which != 0);
          auto& refusal = *d.first_refusal;
          if (refusal.side == i && refusal.edge == edge &&
              refusal.source_edge == (which != 0)) {
            const auto b = edges[edge].signed_side.lower <= 0
                               ? edges[edge].signed_side
                               : edges[edge].squared_margin_gap;
            refusal.limiting_bound = {b.lower, b.upper};
          }
        }
      }
      const auto status = pause_edge_status(edges);
      (which == 0 ? record.sole_status : record.source_status) = status;
    }
    record.complete = record.plane_identity &&
                      record.sole_status == PauseStatus::contained &&
                      record.source_status == PauseStatus::contained;
  }
  d.finite_contact_supported = d.sites[0].complete && d.sites[1].complete;
  d.nominal_load_supported =
      d.nominal_equilibrium && d.finite_contact_supported;
  d.complete = d.arithmetic_supported && d.kinematic_complete &&
               d.constant_state && d.projection_complete &&
               d.nominal_load_supported && !d.first_refusal;
}
} // namespace apsis_drift

#include "origin_boarding_intermediate_pause_support02_internal.hpp"
namespace apsis_drift {
[[gnu::noinline]] auto detail::intermediate_pause_support02_pressure_bridge(
    const BoardingIntermediatePauseSupport02ProjectionToken& token,
    const BoardingIntermediatePauseSupport02Limits& l,
    BoardingIntermediatePauseSupport02Diagnostic& d) -> void {
  using Why = BoardingIntermediatePauseSupport02Condition;
  using State = BoardingIntermediatePauseSupport02State;
  // Owner/source identity precedes EVERY borrowed cell/request access. Only the
  // named fresh-v2 issuer can construct this synchronous capability.
  if (token.owner_ != &d || !token.provider_ || !token.cell_ ||
      !token.request_ ||
      !BoardingIntermediatePauseSupportAccess::valid(d.source) ||
      BoardingIntermediatePauseSupportAccess::data(d.source) !=
          BoardingIntermediatePauseSupportAccess::data(*token.provider_)) {
    intermediate_pause_support02_refuse(d, Why::invalid_binding);
    return;
  }
  const auto& cell = *token.cell_;
  for (std::size_t carrier = 0; carrier < 3; ++carrier)
    for (std::size_t a = 0; a < 2; ++a) {
      if (!intermediate_pause_support02_definition_charge(d)) return;
      const auto& b =
          carrier == 0
              ? cell.center_of_mass.value
              : cell
                    .points[static_cast<std::size_t>(
                        carrier == 1
                            ? BoardingPlantedBodyPointId::port_boot_center
                            : BoardingPlantedBodyPointId::
                                  starboard_boot_center)]
                    .value;
      const double lower = a == 0 ? b.lower.x : b.lower.z;
      const double upper = a == 0 ? b.upper.x : b.upper.z;
      if (!std::isfinite(lower) || !std::isfinite(upper) || lower > upper ||
          std::abs(lower) > 8 || std::abs(upper) > 8) {
        intermediate_pause_support02_refuse(d, Why::unsupported_arithmetic);
        return;
      }
      (carrier == 0 ? d.com_xz[a]
                    : d.boot_centers_xz[carrier - 1][a]) = {lower, upper, true};
    }
  if (!intermediate_pause_support02_allocate(d, l)) return;
  if (!intermediate_pause_support02_definition_charge(d)) return;
  // The original target-sole -> bootcenter+.05 -> BOX bottom-.05 identity is
  // semantic, authenticated in the fresh complete graph and projection. No
  // tolerance or rounded subtraction moves either genuine source plane.
  for (std::size_t side = 0; side < 2; ++side) {
    const auto& y = token.request_->feet[side].sole[0].coordinates[1];
    if (!d.sites[side].plane_identity || y.count != 1 ||
        y.terms != std::array<double, 3>{d.source_planes[side], 0, 0} ||
        !d.pressure_evaluated[side] || !d.nominal_equilibrium) {
      intermediate_pause_support02_refuse(d, Why::sole_plane, side);
      return;
    }
  }
  const auto required = add(point(.020), point(.010));
  for (std::size_t side = 0; side < 2; ++side) {
    auto& record = d.sites[side];
    record.loaded = true;
    record.evaluated = true;
    const Point pressure{
        {d.pressure_xz[side][0].lower, d.pressure_xz[side][0].upper},
        {d.pressure_xz[side][1].lower, d.pressure_xz[side][1].upper}};
    record.pressure_xz = {
        {{pressure.x.low, pressure.x.high}, {pressure.z.low, pressure.z.high}}};
    const Point center{
        {d.boot_centers_xz[side][0].lower, d.boot_centers_xz[side][0].upper},
        {d.boot_centers_xz[side][1].lower, d.boot_centers_xz[side][1].upper}};
    const auto sole = site_sole_corners(center);
    const auto* partition =
        BoardingIntermediatePauseSupportAccess::partition(d.source, side);
    if (!partition) {
      intermediate_pause_support02_refuse(d, Why::invalid_binding, side);
      return;
    }
    const auto source = source_corners(*partition);
    for (std::size_t which = 0; which < 2; ++which) {
      const auto& corners = which == 0 ? sole : source;
      auto& edges = which == 0 ? record.sole_edges : record.source_edges;
      auto& evaluated =
          which == 0 ? record.sole_evaluated : record.source_evaluated;
      for (std::size_t edge = 0; edge < 4; ++edge) {
        if (d.work.disk_edges >= l.disk_edges) {
          intermediate_pause_support02_refuse(d, Why::edge_capacity, side, edge,
                                              which != 0);
          return;
        }
        ++d.work.disk_edges;
        evaluated[edge] = true;
        edges[edge] = site_edge(corners[edge], corners[(edge + 1) % 4],
                                pressure, required, d.arithmetic_supported);
        if (!d.arithmetic_supported) {
          intermediate_pause_support02_refuse(d, Why::unsupported_arithmetic,
                                              side, edge, which != 0);
          return;
        }
        if (!edges[edge].disk_contained) {
          intermediate_pause_support02_refuse(
              d, which == 0 ? Why::sole_disk : Why::source_disk, side, edge,
              which != 0);
          auto& refusal = *d.first_refusal;
          if (refusal.side == side && refusal.edge == edge &&
              refusal.source_edge == (which != 0)) {
            const auto bound = edges[edge].signed_side.lower <= 0
                                   ? edges[edge].signed_side
                                   : edges[edge].squared_margin_gap;
            refusal.limiting_bound = {bound.lower, bound.upper, true};
          }
        }
      }
      (which == 0 ? record.sole_status : record.source_status) =
          pause_edge_status(edges);
    }
    record.complete = record.plane_identity &&
                      record.sole_status == PauseStatus::contained &&
                      record.source_status == PauseStatus::contained;
  }
  if (!intermediate_pause_support02_definition_charge(d)) return;
  if (d.work.disk_edges != 16 || !d.arithmetic_supported ||
      d.stop_condition != Why::none) {
    intermediate_pause_support02_refuse(d, Why::unsupported_arithmetic);
    return;
  }
  if (!intermediate_pause_support02_definition_charge(d)) return;
  d.finite_contact_supported = d.sites[0].complete && d.sites[1].complete;
  d.nominal_load_supported =
      d.nominal_equilibrium && d.finite_contact_supported;
  d.complete = d.arithmetic_supported && d.kinematic_complete &&
               d.constant_state && d.projection_complete &&
               d.nominal_load_supported && !d.first_refusal;
  if (d.complete)
    d.state = State::supported;
  else {
    const bool refuted = d.sites[0].sole_status == PauseStatus::refuted ||
                         d.sites[0].source_status == PauseStatus::refuted ||
                         d.sites[1].sole_status == PauseStatus::refuted ||
                         d.sites[1].source_status == PauseStatus::refuted;
    d.state = refuted ? State::witness_refused : State::unresolved;
  }
}
} // namespace apsis_drift

#include "origin_boarding_route_intermediate_load01_internal.hpp"
namespace apsis_drift {
[[gnu::noinline]] auto detail::
    boarding_route_intermediate_load01_pressure_bridge(
        const BoardingRouteIntermediateLoad01CurrentCellToken& token,
        BoardingRouteIntermediateLoad01Diagnostic& d,
        BoardingRouteIntermediateLoad01Cell& c,
        const BoardingRouteIntermediateLoad01Limits& limits,
        BoardingRouteIntermediateLoad01Refusal& refusal)
        -> BoardingRouteIntermediateLoad01State {
  using Why = BoardingRouteIntermediateLoad01Condition;

  using State = BoardingRouteIntermediateLoad01State;

  const auto stop = [&](Why why, std::optional<std::size_t> side = {},
                        std::optional<std::size_t> edge = {},
                        bool source_edge = false) {
    d.stop_condition = why;
    d.state = why == Why::edge_capacity            ? State::capacity
              : why == Why::unsupported_arithmetic ? State::unsupported
                                                   : State::unresolved;

    if (why == Why::unsupported_arithmetic) d.arithmetic_supported = false;

    if (refusal.condition == Why::none) {
      refusal.condition = refusal.predicate_condition = why;
      refusal.side = side;
      refusal.edge = edge;
      refusal.source_edge = source_edge;
    }
  };

  // This context exists only inside the named consumer, after real S18; its
  // owner and retained Data anchors precede EVERY borrowed request/cell access.
  if (!token.context_ || token.owner_ != &d || token.cell_ != &c ||
      !token.request_ ||
      !BoardingIntermediatePauseSupportAccess::valid(d.source) ||
      token.data_ != BoardingIntermediatePauseSupportAccess::data(d.source) ||
      token.context_->owner_ != &d || token.context_->data_ != token.data_ ||
      token.context_->controls_ != &d.controls ||
      token.context_->parts_ != &d.parts || c.phase_index < 3 ||
      c.phase_index > 4 || token.request_ != &d.controls[c.phase_index - 3]) {
    stop(Why::invalid_binding);
    return d.state;
  }
  if (!boarding_route_intermediate_load01_allocate(d, c, limits, refusal))
    return d.state;

  if (!boarding_route_intermediate_load01_definition_charge(d, c, limits,
                                                            refusal))
    return d.state;

  for (std::size_t side = 0; side < 2; ++side) {
    const auto* source =
        BoardingIntermediatePauseSupportAccess::partition(d.source, side);

    const auto& y = token.request_->feet[side].sole[0].coordinates[1];

    if (!source || !c.sites[side].plane_identity || y.count != 1 ||
        y.terms != std::array<double, 3>{source->plane_metres, 0, 0} ||
        !c.pressure_evaluated[side] || !c.nominal_equilibrium) {
      stop(Why::sole_plane, side);
      return d.state;
    }
  }
  c.plane_identities = true;

  const auto required = add(point(.020), point(.010));

  for (std::size_t side = 0; side < 2; ++side) {
    auto& record = c.sites[side];

    record.loaded =
        side == 1 ||
        c.port_force_state ==
            BoardingRouteIntermediateLoad01PortForceState::everywhere_positive;

    record.evaluated = true;

    const Point pressure{
        {c.pressure_xz[side][0].lower, c.pressure_xz[side][0].upper},
        {c.pressure_xz[side][1].lower, c.pressure_xz[side][1].upper}};

    record.pressure_xz = {
        {{pressure.x.low, pressure.x.high}, {pressure.z.low, pressure.z.high}}};

    const auto& boot =
        c.phase
            .points[static_cast<std::size_t>(
                side == 0 ? BoardingPlantedBodyPointId::port_boot_center
                          : BoardingPlantedBodyPointId::starboard_boot_center)]
            .value;

    const Point center{{boot.lower.x, boot.upper.x},
                       {boot.lower.z, boot.upper.z}};

    const auto sole = site_sole_corners(center);

    const auto* partition =
        BoardingIntermediatePauseSupportAccess::partition(d.source, side);

    if (!partition) {
      stop(Why::invalid_binding, side);
      return d.state;
    }
    const auto source = source_corners(*partition);

    for (std::size_t which = 0; which < 2; ++which) {
      const auto& corners = which == 0 ? sole : source;
      auto& edges = which == 0 ? record.sole_edges : record.source_edges;
      auto& evaluated =
          which == 0 ? record.sole_evaluated : record.source_evaluated;

      for (std::size_t edge = 0; edge < 4; ++edge) {
        if (d.work.disk_edges >= limits.disk_edges) {
          stop(Why::edge_capacity, side, edge, which != 0);
          return d.state;
        }
        ++d.work.disk_edges;
        evaluated[edge] = true;

        edges[edge] = site_edge(corners[edge], corners[(edge + 1) % 4],
                                pressure, required, d.arithmetic_supported);

        if (!d.arithmetic_supported) {
          stop(Why::unsupported_arithmetic, side, edge, which != 0);
          return d.state;
        }
        if (!edges[edge].disk_contained && refusal.condition == Why::none) {
          refusal.condition = refusal.predicate_condition =
              which == 0 ? Why::sole_disk : Why::source_disk;
          refusal.side = side;
          refusal.edge = edge;
          refusal.source_edge = which != 0;

          const auto b = edges[edge].signed_side.lower <= 0
                             ? edges[edge].signed_side
                             : edges[edge].squared_margin_gap;
          refusal.limiting_bound = {b.lower, b.upper, true};

          const auto* summary = d.source.summary();
          refusal.source_name = summary->sources[side].name;
          if (which != 0) refusal.source_key = summary->sources[side].keys[0];
        }
      }
      (which == 0 ? record.sole_status : record.source_status) =
          pause_edge_status(edges);
    }
    record.complete = record.plane_identity &&
                      record.sole_status == PauseStatus::contained &&
                      record.source_status == PauseStatus::contained;
  }
  if (!boarding_route_intermediate_load01_definition_charge(d, c, limits,
                                                            refusal))
    return d.state;

  if (!d.arithmetic_supported || d.stop_condition != Why::none ||
      !std::all_of(c.sites.begin(), c.sites.end(), [](const auto& s) {
        return std::all_of(s.sole_evaluated.begin(), s.sole_evaluated.end(),
                           [](bool b) { return b; }) &&
               std::all_of(s.source_evaluated.begin(), s.source_evaluated.end(),
                           [](bool b) { return b; });
      })) {
    stop(Why::unsupported_arithmetic);
    return d.state;
  }
  if (!boarding_route_intermediate_load01_definition_charge(d, c, limits,
                                                            refusal))
    return d.state;

  c.port_contact_geometry = c.sites[0].complete;
  c.finite_contact_complete = c.sites[0].complete && c.sites[1].complete;

  c.nominal_load_complete =
      c.nominal_equilibrium && c.finite_contact_complete &&
      c.star_everywhere_positive &&
      c.port_force_state !=
          BoardingRouteIntermediateLoad01PortForceState::not_run;

  c.complete = c.phase.complete && c.projection_complete &&
               c.plane_identities && c.nominal_load_complete &&
               refusal.condition == Why::none;

  if (c.complete)
    d.state = State::accepted;

  else {
    const bool refuted = c.sites[0].sole_status == PauseStatus::refuted ||
                         c.sites[0].source_status == PauseStatus::refuted ||
                         c.sites[1].sole_status == PauseStatus::refuted ||
                         c.sites[1].source_status == PauseStatus::refuted;
    d.state = refuted ? State::witness_refused : State::unresolved;
  }
  return d.state;
}
} // namespace apsis_drift

#include "origin_boarding_route_intermediate_unloaded01_internal.hpp"
namespace apsis_drift {
namespace {
using UD = BoardingRouteIntermediateUnloaded01Diagnostic;
using UC = BoardingRouteIntermediateUnloaded01Cell;
using UR = BoardingRouteIntermediateUnloaded01Refusal;
using UL = detail::BoardingRouteIntermediateUnloaded01Limits;
using UW = BoardingRouteIntermediateUnloaded01Condition;
using US = BoardingRouteIntermediateUnloaded01State;
using UB = BoardingFootSiteScalarBounds;
using UA = detail::BoardingIntermediatePauseSupportAccess;
using UP = BoardingPlantedBodyPointId;
using UClass = BoardingRouteIntermediateUnloaded01UpperClass;
using GM = detail::BoardingRouteIntermediateUnloaded01GeometryMode;
auto u_valid(UB b, double domain = 8) -> bool {
  return b.supported && std::isfinite(b.lower) && std::isfinite(b.upper) &&
         b.lower <= b.upper && std::abs(b.lower) <= domain &&
         std::abs(b.upper) <= domain;
}
auto u_interval(UB b) -> Interval {
  return {b.lower, b.upper};
}
auto u_bounds(Interval b) -> UB {
  bool supported = true;
  return site_bounds(b, supported);
}
auto u_hard(UW w) -> bool {
  return w == UW::pressure_capacity || w == UW::edge_capacity ||
         w == UW::event_guard_capacity || w == UW::upper_edge_capacity ||
         w == UW::source_coordinate_capacity ||
         w == UW::endpoint_operation_capacity || w == UW::definition_capacity;
}
auto u_refuse(UD& d, UR& r, UW w, UB b = {},
              std::optional<std::size_t> edge = {}, bool source = false)
    -> bool {
  if (r.condition == UW::none) {
    r.condition = r.predicate_condition = w;
    r.limiting_bound = b;
    r.edge = edge;
    r.side =
        source ? std::optional<std::size_t>{0} : std::optional<std::size_t>{1};
    r.source_edge = source;
  }
  if (u_hard(w) || w == UW::unsupported_arithmetic) {
    d.stop_condition = w;
    d.state = u_hard(w) ? US::capacity : US::unsupported;
    if (w == UW::unsupported_arithmetic) d.arithmetic_supported = false;
  } else if (d.state != US::capacity && d.state != US::unsupported)
    d.state = US::unresolved;
  return false;
}
auto u_charge(UD& d, UR& r, std::size_t& n, std::size_t cap, UW w) -> bool {
  if (n >= cap) return u_refuse(d, r, w);
  ++n;
  return true;
}
template <class F>
auto u_guard(UD& d, UC& c, const UL& l, UR& r, F f, UW w) -> bool {
  return detail::boarding_route_intermediate_unloaded01_definition_charge(
             d, c, l, r) &&
         (f() || u_refuse(d, r, w));
}
// A strict point-side proof is not a zero-radius pressure-disk query.
auto u_signed_side(Point a, Point b, Point center, bool& supported) -> UB {
  return site_bounds(edge_side(a, b, center), supported);
}
template <class Charge>
auto u_sides(const std::array<Vec, 4>& quad, Point center, Charge charge,
             std::array<bool, 4>& mask, UB& minimum,
             std::array<UB, 4>* saved = nullptr) -> bool {
  bool supported = true;
  for (std::size_t e = 0; e < 4; ++e) {
    if (!charge(e)) return false;
    mask[e] = true;
    const auto b = u_signed_side(wide(quad[e]), wide(quad[(e + 1) % 4]), center,
                                 supported);
    if (saved) (*saved)[e] = b;
    if (!supported) return false;
    minimum = e == 0 ? b
                     : UB{std::min(minimum.lower, b.lower),
                          std::min(minimum.upper, b.upper), true};
  }
  return true;
}
template <class Op, class Coord>
auto u_overlap(const std::array<Vec, 4>& quad, Point center, Op op,
               Coord coordinate, std::array<std::array<UB, 2>, 2>& intersection,
               std::array<bool, 2>& positive,
               std::array<std::array<UB, 2>, 2>* sole_saved = nullptr,
               std::array<std::array<UB, 2>, 2>* source_saved = nullptr)
    -> bool {
  std::array<std::array<UB, 2>, 2> sole, source;
  for (std::size_t a = 0; a < 2; ++a)
    for (std::size_t k = 0; k < 2; ++k) {
      if (!op()) return false;
      sole[a][k] =
          u_bounds(add(a == 0 ? center.x : center.z,
                       point((k == 0 ? -1 : 1) * (a == 0 ? .06 : .14))));
      if (!u_valid(sole[a][k], 32)) return false;
    }
  for (std::size_t v = 0; v < 4; ++v)
    for (std::size_t a = 0; a < 2; ++a) {
      if (!coordinate(v, a)) return false;
      const auto x = a == 0 ? quad[v].x : quad[v].z;
      if (!std::isfinite(x) || std::abs(x) > 8) return false;
      const UB b{x, x, true};
      if (v == 0)
        source[a] = {b, b};
      else {
        source[a][0] = {std::min(source[a][0].lower, x),
                        std::min(source[a][0].upper, x), true};
        source[a][1] = {std::max(source[a][1].lower, x),
                        std::max(source[a][1].upper, x), true};
      }
    }
  for (std::size_t a = 0; a < 2; ++a)
    for (std::size_t k = 0; k < 2; ++k) {
      if (!op()) return false;
      intersection[a][k] =
          k == 0 ? UB{std::max(sole[a][0].lower, source[a][0].lower),
                      std::max(sole[a][0].upper, source[a][0].upper), true}
                 : UB{std::min(sole[a][1].lower, source[a][1].lower),
                      std::min(sole[a][1].upper, source[a][1].upper), true};
      if (!u_valid(intersection[a][k], 32)) return false;
    }
  for (std::size_t a = 0; a < 2; ++a)
    positive[a] = intersection[a][0].upper < intersection[a][1].lower;
  if (sole_saved) *sole_saved = sole;
  if (source_saved) *source_saved = source;
  return true;
}
auto u_quad(const std::array<Vec, 4>& q, double plane, bool rectangle) -> bool {
  if (!std::isfinite(plane) || std::abs(plane) > 8) return false;
  for (const auto& p : q)
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
        std::abs(p.x) > 8 || std::abs(p.z) > 8 || p.y != plane)
      return false;
  for (std::size_t e = 0; e < 4; ++e) {
    const auto side =
        edge_side(wide(q[e]), wide(q[(e + 1) % 4]), wide(q[(e + 2) % 4]));
    if (!finite(side) || side.low <= 0) return false;
  }
  return !rectangle ||
         (q[0].z == q[1].z && q[1].x == q[2].x && q[2].z == q[3].z &&
          q[3].x == q[0].x && q[0].x > q[1].x && q[0].z < q[3].z);
}
auto u_min_z(const std::array<Vec, 4>& q) -> double {
  double z = q[0].z;
  for (std::size_t i = 1; i < 4; ++i)
    z = std::min(z, q[i].z);
  return z;
}
auto u_contains(const UC& c, double g) -> bool {
  return c.global_first <= g && c.global_last >= g;
}
template <class Op>
auto u_endpoint(const BoardingRoutePhasePointConstant& endpoint, Op op,
                Point& center, Interval& y) -> bool {
  for (std::size_t a = 0; a < 3; ++a) {
    const auto& e = endpoint.coordinates[a];
    if (e.count == 0 || e.count > 3) return false;
    Interval sum = point(e.terms[0]);
    for (std::size_t i = 1; i < e.count; ++i) {
      if (!op()) return false;
      sum = add(sum, point(e.terms[i]));
    }
    if (a == 0)
      center.x = sum;
    else if (a == 2)
      center.z = sum;
    else {
      if (!op()) return false;
      y = add(sum, point(.05));
    }
    if (!finite(sum)) return false;
  }
  return finite(center.x) && finite(center.z) && finite(y);
}
auto u_consistent(Point center, Interval y,
                  const BoardingPlantedLegPointBounds& actual) -> bool {
  return center.x.low <= actual.upper.x && center.x.high >= actual.lower.x &&
         center.z.low <= actual.upper.z && center.z.high >= actual.lower.z &&
         y.low <= actual.upper.y && y.high >= actual.lower.y;
}
} // namespace
[[gnu::noinline]] auto detail::
    boarding_route_intermediate_unloaded01_geometry_math(
        const BoardingRouteIntermediateUnloaded01GeometryInput& i,
        BoardingRouteIntermediateUnloaded01GeometryLimits l)
        -> BoardingRouteIntermediateUnloaded01GeometryMath {
  BoardingRouteIntermediateUnloaded01GeometryMath r;
  r.mode = i.mode;
  r.center_xz = i.center_xz;
  using Mode = BoardingRouteIntermediateUnloaded01GeometryMode;
  const auto reject = [&](UW w, US s) {
    r.condition = w;
    r.state = s;
  };
  if (l.upper_edges > 8 || l.source_coordinates > 8 || l.operations > 12 ||
      static_cast<std::size_t>(i.mode) > 3) {
    reject(UW::invalid_limits, US::unresolved);
    return r;
  }
  ++r.work.validation_guards;
  r.input_validation_evaluated[0] = true;
  if (!boarding_route_foot_phase_environment()) {
    reject(UW::unsupported_arithmetic, US::unsupported);
    return r;
  }
  r.arithmetic_supported = true;
  ++r.work.validation_guards;
  r.input_validation_evaluated[1] = true;
  if (!u_quad(i.source_vertices, i.source_plane,
              i.mode == Mode::intermediate_overlap)) {
    reject(UW::source_rectangle, US::unresolved);
    return r;
  }
  ++r.work.validation_guards;
  r.input_validation_evaluated[2] = true;
  if (!u_valid(i.center_xz[0]) || !u_valid(i.center_xz[1]) ||
      !std::isfinite(i.sole_plane) || std::abs(i.sole_plane) > 8 ||
      i.sole_plane != i.source_plane) {
    reject(UW::sole_plane, US::unresolved);
    return r;
  }
  r.input_validated = true;
  const auto op = [&]() {
    if (r.work.operations >= l.operations) {
      reject(UW::endpoint_operation_capacity, US::capacity);
      return false;
    }
    r.operation_evaluated[r.work.operations++] = true;
    return true;
  };
  const auto coordinate = [&](std::size_t v, std::size_t a) {
    if (r.work.source_coordinates >= l.source_coordinates) {
      reject(UW::source_coordinate_capacity, US::capacity);
      return false;
    }
    ++r.work.source_coordinates;
    r.source_coordinate_evaluated[2 * v + a] = true;
    return true;
  };
  const auto edge = [&](std::size_t) {
    if (r.work.upper_edges >= l.upper_edges) {
      reject(UW::upper_edge_capacity, US::capacity);
      return false;
    }
    ++r.work.upper_edges;
    return true;
  };
  const Point center{u_interval(i.center_xz[0]), u_interval(i.center_xz[1])};
  if (i.mode == Mode::intermediate_overlap) {
    if (!u_overlap(i.source_vertices, center, op, coordinate, r.intersection,
                   r.positive_overlap, &r.sole_extrema, &r.source_extrema)) {
      if (r.condition == UW::none)
        reject(UW::unsupported_arithmetic, US::unsupported);
      return r;
    }
    r.complete = r.positive_overlap[0] && r.positive_overlap[1];
    r.state = r.complete ? US::accepted : US::unresolved;
    if (!r.complete) r.condition = UW::endpoint_overlap;
    return r;
  }
  if (i.mode != Mode::upper_departure) {
    if (!u_sides(i.source_vertices, center, edge, r.side_evaluated,
                 r.minimum_signed_side, &r.signed_sides)) {
      if (r.condition == UW::none)
        reject(UW::unsupported_arithmetic, US::unsupported);
      return r;
    }
    if (r.minimum_signed_side.lower > 0) {
      r.complete = true;
      r.state = US::accepted;
      r.classification = UClass::strict_center_overlap;
      return r;
    }
    if (i.mode == Mode::upper_point) {
      r.state = r.minimum_signed_side.upper <= 0 ? US::witness_refused
                                                 : US::unresolved;
      r.condition = UW::upper_start_witness;
      return r;
    }
  }
  if (!op()) return r;
  const auto maximum = u_bounds(add(center.z, point(.14)));
  if (!u_valid(maximum, 32)) {
    reject(UW::unsupported_arithmetic, US::unsupported);
    return r;
  }
  const auto minimum = u_min_z(i.source_vertices);
  if (!op()) return r;
  r.departure_gap = u_bounds(subtract(point(minimum), u_interval(maximum)));
  if (!u_valid(r.departure_gap, 32)) {
    reject(UW::unsupported_arithmetic, US::unsupported);
    return r;
  }
  const bool disjoint = maximum.upper < minimum;
  if (i.mode == Mode::upper_cell) {
    r.complete = true;
    r.state = US::accepted;
    r.classification = disjoint ? UClass::strict_Z_disjoint
                                : UClass::mixed_possible_transition;
  } else {
    r.complete = disjoint;
    r.state = disjoint ? US::accepted : US::unresolved;
    r.condition = disjoint ? UW::none : UW::upper_departure;
    r.classification = disjoint ? UClass::strict_Z_disjoint
                                : UClass::mixed_possible_transition;
  }
  return r;
}
[[gnu::noinline]] auto detail::
    boarding_route_intermediate_unloaded01_pressure_bridge(
        const BoardingRouteIntermediateUnloaded01CurrentCellToken& token, UD& d,
        UC& c, const UL& l, UR& r) -> US {
  const auto* ctx = token.context_;
  if (!ctx || ctx->owner_ != &d || token.owner_ != &d || token.cell_ != &c ||
      token.request_ != ctx->request_ || ctx->parts_ != &d.parts ||
      !UA::valid(d.source) || ctx->data_ != UA::data(d.source) ||
      token.data_ != ctx->data_) {
    u_refuse(d, r, UW::invalid_binding);
    return d.state;
  }
  const auto& request = *token.request_;
  if (!u_guard(
          d, c, l, r,
          [&] {
            return u_charge(d, r, d.work.pressure_candidates,
                            l.pressure_candidates, UW::pressure_capacity);
          },
          UW::pressure_capacity))
    return d.state;
  if (!u_guard(
          d, c, l, r,
          [&] {
            const auto& v = c.phase.center_of_mass.value;
            c.com_xz = {UB{v.lower.x, v.upper.x, true},
                        UB{v.lower.z, v.upper.z, true}};
            return u_valid(c.com_xz[0]) && u_valid(c.com_xz[1]);
          },
          UW::unsupported_arithmetic))
    return d.state;
  Point starcenter;
  if (!u_guard(
          d, c, l, r,
          [&] {
            const auto& v =
                c.phase
                    .points[static_cast<std::size_t>(UP::starboard_boot_center)]
                    .value;
            starcenter = {{v.lower.x, v.upper.x}, {v.lower.z, v.upper.z}};
            return u_valid(u_bounds(starcenter.x)) &&
                   u_valid(u_bounds(starcenter.z));
          },
          UW::unsupported_arithmetic))
    return d.state;
  if (!u_guard(
          d, c, l, r,
          [&] {
            c.starcop_xz = c.com_xz;
            return c.projection_complete;
          },
          UW::event_identity))
    return d.state;
  if (!u_guard(
          d, c, l, r,
          [&] {
            return c.port_share.lower == 0 && c.port_share.upper == 0 &&
                   c.star_share.lower == 1 && c.star_share.upper == 1;
          },
          UW::event_identity))
    return d.state;
  if (!u_guard(
          d, c, l, r,
          [&] {
            c.nominal_equilibrium = c.port_zero_only && c.projection_complete;
            return c.nominal_equilibrium;
          },
          UW::event_identity))
    return d.state;
  if (!u_guard(
          d, c, l, r,
          [] {
            return kBoardingBootPressureRadiusMetres == .020 &&
                   kBoardingBootDiskEdgeMarginMetres == .010;
          },
          UW::event_identity))
    return d.state;
  const BoardingBootSourcePartition* star = nullptr;
  if (!u_guard(
          d, c, l, r,
          [&] {
            star = UA::partition(d.source, 1);
            const auto* s = d.source.summary();
            return star && s && s->quads[1].complete;
          },
          UW::source_identity))
    return d.state;
  const Point pressure{u_interval(c.com_xz[0]), u_interval(c.com_xz[1])};
  const auto required = add(point(.020), point(.010));
  bool supported = true;
  bool ordinary = false;
  const auto runedges = [&](bool source) {
    if (!boarding_route_intermediate_unloaded01_definition_charge(d, c, l, r))
      return false;
    const auto corners =
        source ? source_corners(*star) : site_sole_corners(starcenter);
    for (std::size_t e = 0; e < 4; ++e) {
      if (!u_charge(d, r, d.work.disk_edges, l.disk_edges, UW::edge_capacity))
        return false;
      auto& mask =
          source ? c.star_site.source_evaluated : c.star_site.sole_evaluated;
      auto& edges = source ? c.star_site.source_edges : c.star_site.sole_edges;
      mask[e] = true;
      edges[e] = site_edge(corners[e], corners[(e + 1) % 4], pressure, required,
                           supported);
      if (!supported) {
        u_refuse(d, r, UW::unsupported_arithmetic);
        return false;
      }
      const auto& b = edges[e];
      if (!b.disk_contained) {
        ordinary = true;
        const bool first_finding = r.condition == UW::none;
        u_refuse(d, r, source ? UW::source_disk : UW::sole_disk,
                 b.signed_side.lower <= 0 ? b.signed_side
                                          : b.squared_margin_gap,
                 e, source);
        if (first_finding) {
          r.side = 1;
          if (source) {
            const auto& face = star->faces[e < 2 ? 0 : 1];
            r.source_key = face.key;
            r.source_name = face.source_object;
          }
        }
      }
    }
    return true;
  };
  if (!runedges(false) || !runedges(true)) return d.state;
  if (!u_guard(d, c, l, r, [&] { return !ordinary; }, UW::sole_disk)) {
    const auto a = pause_edge_status(c.star_site.sole_edges);
    const auto b = pause_edge_status(c.star_site.source_edges);
    c.star_site.sole_status = a;
    c.star_site.source_status = b;
    d.state = (a == BoardingIntermediatePauseDiskStatus::refuted ||
               b == BoardingIntermediatePauseDiskStatus::refuted)
                  ? US::witness_refused
                  : US::unresolved;
    return d.state;
  }
  c.star_site.loaded = true;
  c.star_site.plane_identity = true;
  c.star_site.evaluated = true;
  c.star_site.complete = true;
  c.star_site.sole_status = c.star_site.source_status =
      BoardingIntermediatePauseDiskStatus::contained;
  c.star_site.pressure_xz = {
      BoardingPlantedLegScalarBounds{c.com_xz[0].lower, c.com_xz[0].upper},
      BoardingPlantedLegScalarBounds{c.com_xz[1].lower, c.com_xz[1].upper}};
  c.star_support = true;
  if (!u_guard(
          d, c, l, r, [&] { return c.phase_index < 3; }, UW::event_identity))
    return d.state;
  if (!u_guard(
          d, c, l, r,
          [&] {
            return request.feet[0].yaw_half == std::array<double, 2>{0, 0} &&
                   request.feet[1].yaw_half == std::array<double, 2>{0, 0};
          },
          UW::event_identity))
    return d.state;
  if (!u_guard(
          d, c, l, r,
          [&] {
            return request.feet[0].swing_height_metres ==
                   (c.phase_index == 1 ? .010 : 0);
          },
          UW::event_identity))
    return d.state;
  const BoardingBootSourcePartition *upper = nullptr, *port = nullptr;
  if (!u_guard(
          d, c, l, r,
          [&] {
            upper = boarding_route_intermediate_unloaded01_upper_partition(
                d.source);
            port = UA::partition(d.source, 0);
            return upper && port;
          },
          UW::source_identity))
    return d.state;
  if (!u_guard(
          d, c, l, r, [&] { return c.phase.target_sole_identities; },
          UW::event_identity))
    return d.state;
  if (!boarding_route_intermediate_unloaded01_definition_charge(d, c, l, r))
    return d.state;
  const auto op = [&]() {
    if (!u_charge(d, r, d.work.endpoint_operations, l.endpoint_operations,
                  UW::endpoint_operation_capacity))
      return false;
    if (c.endpoint_operation_count >= c.endpoint_operation_evaluated.size())
      return u_refuse(d, r, UW::endpoint_operation_capacity);
    c.endpoint_operation_evaluated[c.endpoint_operation_count++] = true;
    return true;
  };
  const auto coordinate = [&](std::size_t v, std::size_t a) {
    if (!u_charge(d, r, d.work.intermediate_coordinates,
                  l.intermediate_coordinates, UW::source_coordinate_capacity))
      return false;
    c.intermediate_coordinate_evaluated[2 * v + a] = true;
    return true;
  };
  std::array<UB, 4> upper_endpoint_sides{};
  const auto sides = [&](std::size_t index, Point center) {
    const auto edge = [&](std::size_t) {
      return u_charge(d, r, d.work.upper_geometry_edges, l.upper_geometry_edges,
                      UW::upper_edge_capacity);
    };
    return u_sides(upper->perimeter_metres, center, edge,
                   c.upper_side_evaluated[index],
                   c.upper_minimum_signed_side[index],
                   index == 0 ? &upper_endpoint_sides : nullptr);
  };
  const auto& actual =
      c.phase.points[static_cast<std::size_t>(UP::port_boot_center)].value;
  for (std::size_t e = 0; e < 16; ++e) {
    if (!u_charge(d, r, d.work.event_guards, l.event_guards,
                  UW::event_guard_capacity))
      return d.state;
    c.event_evaluated[e] = true;
    bool good = true;
    switch (e) {
      case 0: good = c.phase_index < 3; break;
      case 1: good = c.phase.first >= 0 && c.phase.last <= 1; break;
      case 2: break;
      case 3: good = c.port_zero_only && !c.port_loaded; break;
      case 4: {
        using PE = BoardingRouteIntermediateUnloaded01PlaneEvent;
        const bool singleton = c.phase.first == c.phase.last;
        c.upper_plane_event =
            (c.phase_index == 0 ||
             (singleton && ((c.phase_index == 1 &&
                             (c.phase.first == 0 || c.phase.first == 1)) ||
                            (c.phase_index == 2 && c.phase.first == 0))))
                ? PE::equal
            : c.phase_index == 1
                ? (c.phase.first == 0 || c.phase.last == 1
                       ? PE::equal_boundary_above_interior
                       : PE::above)
                : (c.phase.first == 0 ? PE::equal_boundary_below_interior
                                      : PE::below);
        break;
      }
      case 5: {
        using PE = BoardingRouteIntermediateUnloaded01PlaneEvent;
        c.intermediate_plane_event =
            c.phase_index == 2 && c.phase.first == 1 && c.phase.last == 1
                ? PE::equal
            : c.phase_index < 2 ? PE::above
            : c.phase.last == 1 ? PE::equal_boundary_above_interior
                                : PE::above;
        break;
      }
      case 6:
        good = c.phase.target_sole_identities &&
               request.feet[0].yaw_half == std::array<double, 2>{0, 0};
        break;
      case 7:
      case 8:
      case 9:
        c.endpoint_scope[e - 7] = u_contains(c, e == 7   ? 0
                                                : e == 8 ? .125
                                                         : .5);
        break;
      case 10:
        if (c.endpoint_scope[0]) {
          c.endpoint_evaluated[0] = true;
          Point center;
          Interval y;
          if (!u_endpoint(request.feet[0].sole[0], op, center, y))
            return d.state;
          if (!u_consistent(center, y, actual)) {
            u_refuse(d, r, UW::endpoint_consistency);
            return d.state;
          }
          if (!sides(0, center)) {
            if (d.stop_condition == UW::none)
              u_refuse(d, r, UW::unsupported_arithmetic);
            return d.state;
          }
          if (c.upper_minimum_signed_side[0].lower <= 0) {
            for (std::size_t edge = 0; edge < 4; ++edge)
              if (upper_endpoint_sides[edge].lower <= 0) {
                u_refuse(d, r, UW::upper_start_witness,
                         upper_endpoint_sides[edge], edge, true);
                if (upper_endpoint_sides[edge].upper <= 0)
                  d.state = US::witness_refused;
                break;
              }
            return d.state;
          }
          c.endpoint_earned[0] = true;
        }
        break;
      case 11:
        if (c.endpoint_scope[1]) {
          c.endpoint_evaluated[1] = true;
          Point center;
          Interval y;
          const auto& endpoint =
              request.feet[0].sole[c.phase_index == 0 ? 1 : 0];
          if (!u_endpoint(endpoint, op, center, y)) return d.state;
          if (!u_consistent(center, y, actual)) {
            u_refuse(d, r, UW::endpoint_consistency);
            return d.state;
          }
          if (!op()) return d.state;
          const auto maximum = add(center.z, point(.14));
          if (!u_valid(u_bounds(maximum), 32)) {
            u_refuse(d, r, UW::unsupported_arithmetic);
            return d.state;
          }
          if (!op()) return d.state;
          const auto gap = u_bounds(
              subtract(point(u_min_z(upper->perimeter_metres)), maximum));
          if (!u_valid(u_bounds(maximum), 32) || !u_valid(gap, 32)) {
            u_refuse(d, r, UW::unsupported_arithmetic);
            return d.state;
          }
          c.departure_gap = gap;
          if (maximum.high >= u_min_z(upper->perimeter_metres)) {
            u_refuse(d, r, UW::upper_departure, c.departure_gap);
            return d.state;
          }
          c.endpoint_earned[1] = true;
        }
        break;
      case 12:
        if (c.endpoint_scope[2]) {
          c.endpoint_evaluated[2] = true;
          Point center;
          Interval y;
          if (!u_endpoint(request.feet[0].sole[1], op, center, y))
            return d.state;
          if (!u_consistent(center, y, actual)) {
            u_refuse(d, r, UW::endpoint_consistency);
            return d.state;
          }
          if (!u_overlap(port->perimeter_metres, center, op, coordinate,
                         c.intermediate_intersection, c.positive_overlap)) {
            if (d.stop_condition == UW::none)
              u_refuse(d, r, UW::unsupported_arithmetic);
            return d.state;
          }
          if (!c.positive_overlap[0] || !c.positive_overlap[1]) {
            u_refuse(d, r, UW::endpoint_overlap);
            return d.state;
          }
          c.endpoint_earned[2] = true;
        }
        break;
      case 13:
        if (c.phase_index == 0) {
          const Point center{{actual.lower.x, actual.upper.x},
                             {actual.lower.z, actual.upper.z}};
          if (!sides(1, center)) {
            if (d.stop_condition == UW::none)
              u_refuse(d, r, UW::unsupported_arithmetic);
            return d.state;
          }
          if (c.upper_minimum_signed_side[1].lower > 0)
            c.upper_class = UClass::strict_center_overlap;
          else {
            if (!op()) return d.state;
            const auto maximum = add(center.z, point(.14));
            if (!u_valid(u_bounds(maximum), 32)) {
              u_refuse(d, r, UW::unsupported_arithmetic);
              return d.state;
            }
            c.upper_class = maximum.high < u_min_z(upper->perimeter_metres)
                                ? UClass::strict_Z_disjoint
                                : UClass::mixed_possible_transition;
          }
        }
        break;
      case 14:
        for (std::size_t k = 0; k < 3; ++k)
          good = good && (!c.endpoint_scope[k] || c.endpoint_earned[k]);
        break;
      case 15: break;
      default: good = false; break;
    }
    if (!good) {
      u_refuse(d, r, UW::event_identity);
      return d.state;
    }
  }
  if (!u_guard(
          d, c, l, r,
          [&] {
            return c.phase_index != 0 || c.upper_class != UClass::not_run;
          },
          UW::event_identity))
    return d.state;
  if (!u_guard(
          d, c, l, r, [&] { return c.nominal_equilibrium && c.port_zero_only; },
          UW::event_identity))
    return d.state;
  if (!u_guard(
          d, c, l, r,
          [&] {
            c.port_zero_geometry = true;
            return c.star_support && c.projection_complete;
          },
          UW::event_identity))
    return d.state;
  c.complete = true;
  c.state = d.state = US::accepted;
  return d.state;
}
} // namespace apsis_drift

#include "origin_boarding_route_intermediate_support01_internal.hpp"
namespace apsis_drift {
namespace {
using HD = BoardingRouteIntermediateSupport01Diagnostic;
using HC = BoardingRouteIntermediateSupport01Cell;
using HR = BoardingRouteIntermediateSupport01Refusal;
using HL = detail::BoardingRouteIntermediateSupport01Limits;
using HW = BoardingRouteIntermediateSupport01Condition;
using HS = BoardingRouteIntermediateSupport01State;
using UB = BoardingFootSiteScalarBounds;
using UA = detail::BoardingIntermediatePauseSupportAccess;
using UP = BoardingPlantedBodyPointId;
using HClass = BoardingRouteIntermediateSupport01UpperClass;
auto h_valid(UB b, double domain = 8) -> bool {
  return b.supported && std::isfinite(b.lower) && std::isfinite(b.upper) &&
         b.lower <= b.upper && std::abs(b.lower) <= domain &&
         std::abs(b.upper) <= domain;
}
auto h_interval(UB b) -> Interval {
  return {b.lower, b.upper};
}
auto h_bounds(Interval b) -> UB {
  bool supported = true;
  return site_bounds(b, supported);
}
auto h_hard(HW w) -> bool {
  return w == HW::pressure_capacity || w == HW::edge_capacity ||
         w == HW::event_guard_capacity || w == HW::upper_edge_capacity ||
         w == HW::source_coordinate_capacity ||
         w == HW::endpoint_operation_capacity || w == HW::definition_capacity;
}
auto h_refuse(HD& d, HR& r, HW w, UB b = {},
              std::optional<std::size_t> edge = {}, bool source = false)
    -> bool {
  if (r.condition == HW::none) {
    r.condition = r.predicate_condition = w;
    r.limiting_bound = b;
    r.edge = edge;
    r.side =
        source ? std::optional<std::size_t>{0} : std::optional<std::size_t>{1};
    r.source_edge = source;
  }
  if (h_hard(w) || w == HW::unsupported_arithmetic) {
    d.stop_condition = w;
    d.state = h_hard(w) ? HS::capacity : HS::unsupported;
    if (w == HW::unsupported_arithmetic) d.arithmetic_supported = false;
  } else if (d.state != HS::capacity && d.state != HS::unsupported)
    d.state = HS::unresolved;
  return false;
}
auto h_charge(HD& d, HR& r, std::size_t& n, std::size_t cap, HW w) -> bool {
  if (n >= cap) return h_refuse(d, r, w);
  ++n;
  return true;
}
template <class F>
auto h_guard(HD& d, HC& c, const HL& l, HR& r, F f, HW w) -> bool {
  return detail::boarding_route_intermediate_support01_definition_charge(
             d, c, l, r) &&
         (f() || h_refuse(d, r, w));
}
// A strict point-side proof is not a zero-radius pressure-disk query.
auto h_signed_side(Point a, Point b, Point center, bool& supported) -> UB {
  return site_bounds(edge_side(a, b, center), supported);
}
template <class Charge>
auto h_sides(const std::array<Vec, 4>& quad, Point center, Charge charge,
             std::array<bool, 4>& mask, UB& minimum,
             std::array<UB, 4>* saved = nullptr) -> bool {
  bool supported = true;
  for (std::size_t e = 0; e < 4; ++e) {
    if (!charge(e)) return false;
    mask[e] = true;
    const auto b = h_signed_side(wide(quad[e]), wide(quad[(e + 1) % 4]), center,
                                 supported);
    if (saved) (*saved)[e] = b;
    if (!supported) return false;
    minimum = e == 0 ? b
                     : UB{std::min(minimum.lower, b.lower),
                          std::min(minimum.upper, b.upper), true};
  }
  return true;
}
template <class Op, class Coord>
auto h_overlap(const std::array<Vec, 4>& quad, Point center, Op op,
               Coord coordinate, std::array<std::array<UB, 2>, 2>& intersection,
               std::array<bool, 2>& positive,
               std::array<std::array<UB, 2>, 2>* sole_saved = nullptr,
               std::array<std::array<UB, 2>, 2>* source_saved = nullptr)
    -> bool {
  std::array<std::array<UB, 2>, 2> sole, source;
  for (std::size_t a = 0; a < 2; ++a)
    for (std::size_t k = 0; k < 2; ++k) {
      if (!op()) return false;
      sole[a][k] =
          h_bounds(add(a == 0 ? center.x : center.z,
                       point((k == 0 ? -1 : 1) * (a == 0 ? .06 : .14))));
      if (!h_valid(sole[a][k], 32)) return false;
    }
  for (std::size_t v = 0; v < 4; ++v)
    for (std::size_t a = 0; a < 2; ++a) {
      if (!coordinate(v, a)) return false;
      const auto x = a == 0 ? quad[v].x : quad[v].z;
      if (!std::isfinite(x) || std::abs(x) > 8) return false;
      const UB b{x, x, true};
      if (v == 0)
        source[a] = {b, b};
      else {
        source[a][0] = {std::min(source[a][0].lower, x),
                        std::min(source[a][0].upper, x), true};
        source[a][1] = {std::max(source[a][1].lower, x),
                        std::max(source[a][1].upper, x), true};
      }
    }
  for (std::size_t a = 0; a < 2; ++a)
    for (std::size_t k = 0; k < 2; ++k) {
      if (!op()) return false;
      intersection[a][k] =
          k == 0 ? UB{std::max(sole[a][0].lower, source[a][0].lower),
                      std::max(sole[a][0].upper, source[a][0].upper), true}
                 : UB{std::min(sole[a][1].lower, source[a][1].lower),
                      std::min(sole[a][1].upper, source[a][1].upper), true};
      if (!h_valid(intersection[a][k], 32)) return false;
    }
  for (std::size_t a = 0; a < 2; ++a)
    positive[a] = intersection[a][0].upper < intersection[a][1].lower;
  if (sole_saved) *sole_saved = sole;
  if (source_saved) *source_saved = source;
  return true;
}
auto h_min_z(const std::array<Vec, 4>& q) -> double {
  double z = q[0].z;
  for (std::size_t i = 1; i < 4; ++i)
    z = std::min(z, q[i].z);
  return z;
}
auto h_contains(const HC& c, double g) -> bool {
  return c.global_first <= g && c.global_last >= g;
}
template <class Op>
auto h_endpoint(const BoardingRoutePhasePointConstant& endpoint, Op op,
                Point& center, Interval& y) -> bool {
  for (std::size_t a = 0; a < 3; ++a) {
    const auto& e = endpoint.coordinates[a];
    if (e.count == 0 || e.count > 3) return false;
    Interval sum = point(e.terms[0]);
    for (std::size_t i = 1; i < e.count; ++i) {
      if (!op()) return false;
      sum = add(sum, point(e.terms[i]));
      if (!h_valid(h_bounds(sum), 32)) return false;
    }
    if (a == 0)
      center.x = sum;
    else if (a == 2)
      center.z = sum;
    else {
      if (!op()) return false;
      y = add(sum, point(.05));
      if (!h_valid(h_bounds(y), 32)) return false;
    }
    if (!h_valid(h_bounds(sum), 32)) return false;
  }
  return finite(center.x) && finite(center.z) && finite(y);
}
auto h_consistent(Point center, Interval y,
                  const BoardingPlantedLegPointBounds& actual) -> bool {
  return center.x.low <= actual.upper.x && center.x.high >= actual.lower.x &&
         center.z.low <= actual.upper.z && center.z.high >= actual.lower.z &&
         y.low <= actual.upper.y && y.high >= actual.lower.y;
}
} // namespace
[[gnu::noinline]] auto detail::
    boarding_route_intermediate_support01_prefix_bridge(
        const BoardingRouteIntermediateSupport01CurrentCellToken& token, HD& d,
        HC& c, const HL& l, HR& r) -> HS {
  const auto* ctx = token.context_;
  if (!ctx || ctx->owner_ != &d || token.owner_ != &d || token.cell_ != &c ||
      token.request_ != ctx->request_ || ctx->parts_ != &d.parts ||
      !UA::valid(d.source) || ctx->data_ != UA::data(d.source) ||
      token.data_ != ctx->data_) {
    h_refuse(d, r, HW::invalid_binding);
    return d.state;
  }
  const auto& request = *token.request_;
  if (!h_guard(
          d, c, l, r,
          [&] {
            return h_charge(d, r, d.work.pressure_candidates,
                            l.pressure_candidates, HW::pressure_capacity);
          },
          HW::pressure_capacity))
    return d.state;
  if (!h_guard(
          d, c, l, r,
          [&] {
            const auto& v = c.phase.center_of_mass.value;
            c.pressure_xz[1] = {UB{v.lower.x, v.upper.x, true},
                                UB{v.lower.z, v.upper.z, true}};
            return h_valid(c.pressure_xz[1][0]) && h_valid(c.pressure_xz[1][1]);
          },
          HW::unsupported_arithmetic))
    return d.state;
  Point starcenter;
  if (!h_guard(
          d, c, l, r,
          [&] {
            const auto& v =
                c.phase
                    .points[static_cast<std::size_t>(UP::starboard_boot_center)]
                    .value;
            starcenter = {{v.lower.x, v.upper.x}, {v.lower.z, v.upper.z}};
            return h_valid(h_bounds(starcenter.x)) &&
                   h_valid(h_bounds(starcenter.z));
          },
          HW::unsupported_arithmetic))
    return d.state;
  if (!h_guard(
          d, c, l, r, [&] { return c.projection_complete; },
          HW::event_identity))
    return d.state;
  c.pressure_evaluated[1] = true;
  if (!h_guard(
          d, c, l, r,
          [&] {
            return c.port_share.lower == 0 && c.port_share.upper == 0 &&
                   c.star_share.lower == 1 && c.star_share.upper == 1;
          },
          HW::event_identity))
    return d.state;
  if (!h_guard(
          d, c, l, r,
          [&] {
            c.nominal_equilibrium =
                (c.port_force_state ==
                 BoardingRouteIntermediateSupport01PortForceState::zero_only) &&
                c.projection_complete;
            return c.nominal_equilibrium;
          },
          HW::event_identity))
    return d.state;
  if (!h_guard(
          d, c, l, r,
          [] {
            return kBoardingBootPressureRadiusMetres == .020 &&
                   kBoardingBootDiskEdgeMarginMetres == .010;
          },
          HW::event_identity))
    return d.state;
  const BoardingBootSourcePartition* star = nullptr;
  if (!h_guard(
          d, c, l, r,
          [&] {
            star = UA::partition(d.source, 1);
            const auto* s = d.source.summary();
            return star && s && s->quads[1].complete;
          },
          HW::source_identity))
    return d.state;
  const Point pressure{h_interval(c.pressure_xz[1][0]),
                       h_interval(c.pressure_xz[1][1])};
  const auto required = add(point(.020), point(.010));
  bool supported = true;
  bool ordinary = false;
  const auto runedges = [&](bool source) {
    if (!boarding_route_intermediate_support01_definition_charge(d, c, l, r))
      return false;
    const auto corners =
        source ? source_corners(*star) : site_sole_corners(starcenter);
    for (std::size_t e = 0; e < 4; ++e) {
      if (!h_charge(d, r, d.work.disk_edges, l.disk_edges, HW::edge_capacity))
        return false;
      auto& mask =
          source ? c.sites[1].source_evaluated : c.sites[1].sole_evaluated;
      auto& edges = source ? c.sites[1].source_edges : c.sites[1].sole_edges;
      mask[e] = true;
      edges[e] = site_edge(corners[e], corners[(e + 1) % 4], pressure, required,
                           supported);
      if (!supported) {
        h_refuse(d, r, HW::unsupported_arithmetic);
        return false;
      }
      const auto& b = edges[e];
      if (!b.disk_contained) {
        ordinary = true;
        const bool first_finding = r.condition == HW::none;
        h_refuse(d, r, source ? HW::source_disk : HW::sole_disk,
                 b.signed_side.lower <= 0 ? b.signed_side
                                          : b.squared_margin_gap,
                 e, source);
        if (first_finding) {
          r.side = 1;
          if (source) {
            const auto& face = star->faces[e < 2 ? 0 : 1];
            r.source_key = face.key;
            r.source_name = face.source_object;
          }
        }
      }
    }
    return true;
  };
  if (!runedges(false) || !runedges(true)) return d.state;
  if (!h_guard(d, c, l, r, [&] { return !ordinary; }, HW::sole_disk)) {
    if (d.state == HS::capacity || d.state == HS::unsupported) return d.state;
    const auto a = pause_edge_status(c.sites[1].sole_edges);
    const auto b = pause_edge_status(c.sites[1].source_edges);
    c.sites[1].sole_status = a;
    c.sites[1].source_status = b;
    d.state = (a == BoardingIntermediatePauseDiskStatus::refuted ||
               b == BoardingIntermediatePauseDiskStatus::refuted)
                  ? HS::witness_refused
                  : HS::unresolved;
    return d.state;
  }
  c.sites[1].loaded = true;
  c.sites[1].plane_identity = true;
  c.sites[1].evaluated = true;
  c.sites[1].complete = true;
  c.sites[1].sole_status = c.sites[1].source_status =
      BoardingIntermediatePauseDiskStatus::contained;
  c.sites[1].pressure_xz = {
      BoardingPlantedLegScalarBounds{c.pressure_xz[1][0].lower,
                                     c.pressure_xz[1][0].upper},
      BoardingPlantedLegScalarBounds{c.pressure_xz[1][1].lower,
                                     c.pressure_xz[1][1].upper}};
  c.star_support = true;
  if (!h_guard(
          d, c, l, r, [&] { return c.phase_index < 3; }, HW::event_identity))
    return d.state;
  if (!h_guard(
          d, c, l, r,
          [&] {
            return request.feet[0].yaw_half == std::array<double, 2>{0, 0} &&
                   request.feet[1].yaw_half == std::array<double, 2>{0, 0};
          },
          HW::event_identity))
    return d.state;
  if (!h_guard(
          d, c, l, r,
          [&] {
            return request.feet[0].swing_height_metres ==
                   (c.phase_index == 1 ? .010 : 0);
          },
          HW::event_identity))
    return d.state;
  const BoardingBootSourcePartition *upper = nullptr, *port = nullptr;
  if (!h_guard(
          d, c, l, r,
          [&] {
            upper = boarding_route_intermediate_unloaded01_upper_partition(
                d.source);
            port = UA::partition(d.source, 0);
            return upper && port;
          },
          HW::source_identity))
    return d.state;
  if (!h_guard(
          d, c, l, r, [&] { return c.phase.target_sole_identities; },
          HW::event_identity))
    return d.state;
  if (!boarding_route_intermediate_support01_definition_charge(d, c, l, r))
    return d.state;
  const auto op = [&]() {
    if (!h_charge(d, r, d.work.endpoint_operations, l.endpoint_operations,
                  HW::endpoint_operation_capacity))
      return false;
    if (c.endpoint_operation_count >= c.endpoint_operation_evaluated.size())
      return h_refuse(d, r, HW::endpoint_operation_capacity);
    c.endpoint_operation_evaluated[c.endpoint_operation_count++] = true;
    return true;
  };
  const auto coordinate = [&](std::size_t v, std::size_t a) {
    if (!h_charge(d, r, d.work.source_coordinates, l.source_coordinates,
                  HW::source_coordinate_capacity))
      return false;
    c.source_coordinate_evaluated[v][a] = true;
    return true;
  };
  std::array<UB, 4> upper_endpoint_sides{};
  const auto sides = [&](std::size_t index, Point center) {
    const auto edge = [&](std::size_t) {
      return h_charge(d, r, d.work.upper_geometry_edges, l.upper_geometry_edges,
                      HW::upper_edge_capacity);
    };
    return h_sides(upper->perimeter_metres, center, edge,
                   c.upper_side_evaluated[index],
                   c.upper_minimum_signed_side[index],
                   index == 0 ? &upper_endpoint_sides : nullptr);
  };
  const auto& actual =
      c.phase.points[static_cast<std::size_t>(UP::port_boot_center)].value;
  for (std::size_t e = 0; e < 16; ++e) {
    if (!h_charge(d, r, d.work.event_guards, l.event_guards,
                  HW::event_guard_capacity))
      return d.state;
    c.event_evaluated[e] = true;
    bool good = true;
    switch (e) {
      case 0: good = c.phase_index < 3; break;
      case 1: good = c.phase.first >= 0 && c.phase.last <= 1; break;
      case 2: break;
      case 3:
        good = (c.port_force_state ==
                BoardingRouteIntermediateSupport01PortForceState::zero_only) &&
               !c.sites[0].loaded;
        break;
      case 4: {
        using PE = BoardingRouteIntermediateSupport01PlaneEvent;
        const bool singleton = c.phase.first == c.phase.last;
        c.upper_plane_event =
            (c.phase_index == 0 ||
             (singleton && ((c.phase_index == 1 &&
                             (c.phase.first == 0 || c.phase.first == 1)) ||
                            (c.phase_index == 2 && c.phase.first == 0))))
                ? PE::equal
            : c.phase_index == 1
                ? (c.phase.first == 0 || c.phase.last == 1
                       ? PE::equal_boundary_above_interior
                       : PE::above)
                : (c.phase.first == 0 ? PE::equal_boundary_below_interior
                                      : PE::below);
        break;
      }
      case 5: {
        using PE = BoardingRouteIntermediateSupport01PlaneEvent;
        c.intermediate_plane_event =
            c.phase_index == 2 && c.phase.first == 1 && c.phase.last == 1
                ? PE::equal
            : c.phase_index < 2 ? PE::above
            : c.phase.last == 1 ? PE::equal_boundary_above_interior
                                : PE::above;
        break;
      }
      case 6:
        good = c.phase.target_sole_identities &&
               request.feet[0].yaw_half == std::array<double, 2>{0, 0};
        break;
      case 7:
      case 8:
      case 9:
        c.endpoint_scope[e - 7] = h_contains(c, e == 7   ? 0
                                                : e == 8 ? .125
                                                         : .5);
        break;
      case 10:
        if (c.endpoint_scope[0]) {
          c.endpoint_evaluated[0] = true;
          Point center;
          Interval y;
          if (!h_endpoint(request.feet[0].sole[0], op, center, y)) {
            if (d.state != HS::capacity && d.state != HS::unsupported)
              h_refuse(d, r, HW::unsupported_arithmetic);
            return d.state;
          }
          if (!h_consistent(center, y, actual)) {
            h_refuse(d, r, HW::endpoint_consistency);
            return d.state;
          }
          if (!sides(0, center)) {
            if (d.stop_condition == HW::none)
              h_refuse(d, r, HW::unsupported_arithmetic);
            return d.state;
          }
          if (c.upper_minimum_signed_side[0].lower <= 0) {
            for (std::size_t edge = 0; edge < 4; ++edge)
              if (upper_endpoint_sides[edge].lower <= 0) {
                h_refuse(d, r, HW::upper_start_witness,
                         upper_endpoint_sides[edge], edge, true);
                if (upper_endpoint_sides[edge].upper <= 0)
                  d.state = HS::witness_refused;
                break;
              }
            return d.state;
          }
          c.endpoint_earned[0] = true;
        }
        break;
      case 11:
        if (c.endpoint_scope[1]) {
          c.endpoint_evaluated[1] = true;
          Point center;
          Interval y;
          const auto& endpoint =
              request.feet[0].sole[c.phase_index == 0 ? 1 : 0];
          if (!h_endpoint(endpoint, op, center, y)) {
            if (d.state != HS::capacity && d.state != HS::unsupported)
              h_refuse(d, r, HW::unsupported_arithmetic);
            return d.state;
          }
          if (!h_consistent(center, y, actual)) {
            h_refuse(d, r, HW::endpoint_consistency);
            return d.state;
          }
          if (!op()) return d.state;
          const auto maximum = add(center.z, point(.14));
          if (!h_valid(h_bounds(maximum), 32)) {
            h_refuse(d, r, HW::unsupported_arithmetic);
            return d.state;
          }
          if (!op()) return d.state;
          const auto gap = h_bounds(
              subtract(point(h_min_z(upper->perimeter_metres)), maximum));
          if (!h_valid(h_bounds(maximum), 32) || !h_valid(gap, 32)) {
            h_refuse(d, r, HW::unsupported_arithmetic);
            return d.state;
          }
          c.departure_gap = gap;
          if (maximum.high >= h_min_z(upper->perimeter_metres)) {
            h_refuse(d, r, HW::upper_departure, c.departure_gap);
            return d.state;
          }
          c.endpoint_earned[1] = true;
        }
        break;
      case 12:
        if (c.endpoint_scope[2]) {
          c.endpoint_evaluated[2] = true;
          Point center;
          Interval y;
          if (!h_endpoint(request.feet[0].sole[1], op, center, y)) {
            if (d.state != HS::capacity && d.state != HS::unsupported)
              h_refuse(d, r, HW::unsupported_arithmetic);
            return d.state;
          }
          if (!h_consistent(center, y, actual)) {
            h_refuse(d, r, HW::endpoint_consistency);
            return d.state;
          }
          if (!h_overlap(port->perimeter_metres, center, op, coordinate,
                         c.intermediate_intersection, c.positive_overlap)) {
            if (d.stop_condition == HW::none)
              h_refuse(d, r, HW::unsupported_arithmetic);
            return d.state;
          }
          if (!c.positive_overlap[0] || !c.positive_overlap[1]) {
            h_refuse(d, r, HW::endpoint_overlap);
            return d.state;
          }
          c.endpoint_earned[2] = true;
        }
        break;
      case 13:
        if (c.phase_index == 0) {
          const Point center{{actual.lower.x, actual.upper.x},
                             {actual.lower.z, actual.upper.z}};
          if (!sides(1, center)) {
            if (d.stop_condition == HW::none)
              h_refuse(d, r, HW::unsupported_arithmetic);
            return d.state;
          }
          if (c.upper_minimum_signed_side[1].lower > 0)
            c.upper_class = HClass::strict_center_overlap;
          else {
            if (!op()) return d.state;
            const auto maximum = add(center.z, point(.14));
            if (!h_valid(h_bounds(maximum), 32)) {
              h_refuse(d, r, HW::unsupported_arithmetic);
              return d.state;
            }
            c.upper_class = maximum.high < h_min_z(upper->perimeter_metres)
                                ? HClass::strict_Z_disjoint
                                : HClass::mixed_possible_transition;
          }
        }
        break;
      case 14:
        for (std::size_t k = 0; k < 3; ++k)
          good = good && (!c.endpoint_scope[k] || c.endpoint_earned[k]);
        break;
      case 15: break;
      default: good = false; break;
    }
    if (!good) {
      h_refuse(d, r, HW::event_identity);
      return d.state;
    }
  }
  if (!h_guard(
          d, c, l, r,
          [&] {
            return c.phase_index != 0 || c.upper_class != HClass::not_run;
          },
          HW::event_identity))
    return d.state;
  if (!h_guard(
          d, c, l, r,
          [&] {
            return c.nominal_equilibrium &&
                   (c.port_force_state ==
                    BoardingRouteIntermediateSupport01PortForceState::
                        zero_only);
          },
          HW::event_identity))
    return d.state;
  if (!h_guard(
          d, c, l, r,
          [&] {
            c.prefix_zero_geometry_complete = true;
            return c.star_support && c.projection_complete;
          },
          HW::event_identity))
    return d.state;
  c.plane_identities =
      true; // PREFIX loaded STAR only; moving-port structural identity above.
  c.nominal_support_complete = true;
  c.complete = true;
  c.state = d.state = HS::accepted;
  return d.state;
}
} // namespace apsis_drift

namespace apsis_drift {
[[gnu::noinline]] auto detail::
    boarding_route_intermediate_support01_load_bridge(
        const BoardingRouteIntermediateSupport01CurrentCellToken& token,
        BoardingRouteIntermediateSupport01Diagnostic& d,
        BoardingRouteIntermediateSupport01Cell& c,
        const BoardingRouteIntermediateSupport01Limits& limits,
        BoardingRouteIntermediateSupport01Refusal& refusal)
        -> BoardingRouteIntermediateSupport01State {
  using Why = BoardingRouteIntermediateSupport01Condition;

  using State = BoardingRouteIntermediateSupport01State;

  const auto stop = [&](Why why, std::optional<std::size_t> side = {},
                        std::optional<std::size_t> edge = {},
                        bool source_edge = false) {
    if (why == Why::edge_capacity || why == Why::unsupported_arithmetic ||
        why == Why::invalid_binding) {
      d.stop_condition = why;
      d.state = why == Why::edge_capacity            ? State::capacity
                : why == Why::unsupported_arithmetic ? State::unsupported
                                                     : State::unresolved;
    } else if (d.state != State::capacity && d.state != State::unsupported)
      d.state = State::unresolved;

    if (why == Why::unsupported_arithmetic) d.arithmetic_supported = false;

    if (refusal.condition == Why::none) {
      refusal.condition = refusal.predicate_condition = why;
      refusal.side = side;
      refusal.edge = edge;
      refusal.source_edge = source_edge;
    }
  };

  // This context exists only inside the named consumer, after real S36; its
  // owner and retained Data anchors precede EVERY borrowed request/cell access.
  if (!token.context_ || token.owner_ != &d || token.cell_ != &c ||
      !token.request_ ||
      !BoardingIntermediatePauseSupportAccess::valid(d.source) ||
      token.data_ != BoardingIntermediatePauseSupportAccess::data(d.source) ||
      token.context_->owner_ != &d || token.context_->data_ != token.data_ ||
      token.context_->request_ != token.request_ ||
      token.context_->parts_ != &d.parts || c.phase_index < 3 ||
      c.phase_index > 4 || !token.request_) {
    stop(Why::invalid_binding);
    return d.state;
  }
  if (!boarding_route_intermediate_support01_allocate(d, c, limits, refusal))
    return d.state;

  if (!boarding_route_intermediate_support01_definition_charge(d, c, limits,
                                                               refusal))
    return d.state;

  for (std::size_t side = 0; side < 2; ++side) {
    const auto* source =
        BoardingIntermediatePauseSupportAccess::partition(d.source, side);

    const auto& y = token.request_->feet[side].sole[0].coordinates[1];

    if (!source || !c.sites[side].plane_identity || y.count != 1 ||
        y.terms != std::array<double, 3>{source->plane_metres, 0, 0} ||
        !c.pressure_evaluated[side] || !c.nominal_equilibrium) {
      stop(Why::sole_plane, side);
      return d.state;
    }
  }
  c.plane_identities = true;

  const auto required = add(point(.020), point(.010));

  for (std::size_t side = 0; side < 2; ++side) {
    auto& record = c.sites[side];

    record.loaded =
        side == 1 || c.port_force_state ==
                         BoardingRouteIntermediateSupport01PortForceState::
                             everywhere_positive;

    record.evaluated = true;

    const Point pressure{
        {c.pressure_xz[side][0].lower, c.pressure_xz[side][0].upper},
        {c.pressure_xz[side][1].lower, c.pressure_xz[side][1].upper}};

    record.pressure_xz = {
        {{pressure.x.low, pressure.x.high}, {pressure.z.low, pressure.z.high}}};

    const auto& boot =
        c.phase
            .points[static_cast<std::size_t>(
                side == 0 ? BoardingPlantedBodyPointId::port_boot_center
                          : BoardingPlantedBodyPointId::starboard_boot_center)]
            .value;

    const Point center{{boot.lower.x, boot.upper.x},
                       {boot.lower.z, boot.upper.z}};

    const auto sole = site_sole_corners(center);

    const auto* partition =
        BoardingIntermediatePauseSupportAccess::partition(d.source, side);

    if (!partition) {
      stop(Why::invalid_binding, side);
      return d.state;
    }
    const auto source = source_corners(*partition);

    for (std::size_t which = 0; which < 2; ++which) {
      const auto& corners = which == 0 ? sole : source;
      auto& edges = which == 0 ? record.sole_edges : record.source_edges;
      auto& evaluated =
          which == 0 ? record.sole_evaluated : record.source_evaluated;

      for (std::size_t edge = 0; edge < 4; ++edge) {
        if (d.work.disk_edges >= limits.disk_edges) {
          stop(Why::edge_capacity, side, edge, which != 0);
          return d.state;
        }
        ++d.work.disk_edges;
        evaluated[edge] = true;

        edges[edge] = site_edge(corners[edge], corners[(edge + 1) % 4],
                                pressure, required, d.arithmetic_supported);

        if (!d.arithmetic_supported) {
          stop(Why::unsupported_arithmetic, side, edge, which != 0);
          return d.state;
        }
        if (!edges[edge].disk_contained && refusal.condition == Why::none) {
          refusal.condition = refusal.predicate_condition =
              which == 0 ? Why::sole_disk : Why::source_disk;
          refusal.side = side;
          refusal.edge = edge;
          refusal.source_edge = which != 0;

          const auto b = edges[edge].signed_side.lower <= 0
                             ? edges[edge].signed_side
                             : edges[edge].squared_margin_gap;
          refusal.limiting_bound = {b.lower, b.upper, true};

          const auto* summary = d.source.summary();
          refusal.source_name = summary->sources[side].name;
          if (which != 0)
            refusal.source_key = summary->sources[side].keys[edge < 2 ? 0 : 1];
        }
      }
      (which == 0 ? record.sole_status : record.source_status) =
          pause_edge_status(edges);
    }
    record.complete = record.plane_identity &&
                      record.sole_status == PauseStatus::contained &&
                      record.source_status == PauseStatus::contained;
  }
  if (!boarding_route_intermediate_support01_definition_charge(d, c, limits,
                                                               refusal))
    return d.state;

  if (!d.arithmetic_supported || d.stop_condition != Why::none ||
      !std::all_of(c.sites.begin(), c.sites.end(), [](const auto& s) {
        return std::all_of(s.sole_evaluated.begin(), s.sole_evaluated.end(),
                           [](bool b) { return b; }) &&
               std::all_of(s.source_evaluated.begin(), s.source_evaluated.end(),
                           [](bool b) { return b; });
      })) {
    stop(Why::unsupported_arithmetic);
    return d.state;
  }
  if (!boarding_route_intermediate_support01_definition_charge(d, c, limits,
                                                               refusal))
    return d.state;

  c.port_contact_geometry = c.sites[0].complete;
  c.star_support = c.sites[1].complete;

  c.nominal_support_complete =
      c.nominal_equilibrium && (c.sites[0].complete && c.sites[1].complete) &&
      c.star_everywhere_positive &&
      c.port_force_state !=
          BoardingRouteIntermediateSupport01PortForceState::not_run;

  c.complete = c.phase.complete && c.projection_complete &&
               c.plane_identities && c.nominal_support_complete &&
               refusal.condition == Why::none;

  if (c.complete)
    d.state = State::accepted;

  else {
    const bool refuted = c.sites[0].sole_status == PauseStatus::refuted ||
                         c.sites[0].source_status == PauseStatus::refuted ||
                         c.sites[1].sole_status == PauseStatus::refuted ||
                         c.sites[1].source_status == PauseStatus::refuted;
    d.state = refuted ? State::witness_refused : State::unresolved;
  }
  c.state = d.state;
  return d.state;
}
} // namespace apsis_drift
