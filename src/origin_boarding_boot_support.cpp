#include "apsis_drift/origin_boarding_boot_support.hpp"
#include "origin_boarding_self_model02_internal.hpp"
#include <algorithm>
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
} // namespace apsis_drift
