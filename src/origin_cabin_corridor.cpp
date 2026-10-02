#include "apsis_drift/origin_cabin_corridor.hpp"
#include "origin_cabin_contact_internal.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

namespace apsis_drift {
namespace {
using Access = detail::CabinContactAccess;
constexpr double sole_area{.12 * .28};
constexpr std::int64_t sole_half_length_um{140000};
struct SourcePartition {
  std::uint32_t tile{}, first_triangle{};
  std::int64_t front_width{}, rear_width{}, front_z{}, rear_z{};
};
// Paired triangles partition each actual quadrilateral along the same source
// diagonal. Tile05 has two disjoint-Z partitions sharing its real sliver edge.
constexpr std::array<SourcePartition, 9> partitions{
    SourcePartition{0, 85129, 711000, 711000, 3664500, 4225500},
    SourcePartition{1, 85537, 711000, 711000, 3069500, 3630500},
    SourcePartition{2, 85945, 711000, 711000, 2474500, 3035500},
    SourcePartition{3, 86353, 711000, 711000, 1879500, 2440500},
    SourcePartition{4, 86761, 711000, 711000, 1284500, 1845500},
    SourcePartition{5, 87211, 710992, 710992, 700000, 1250500},
    SourcePartition{5, 87219, 710365, 710992, 689500, 700000},
    SourcePartition{6, 87645, 674846, 708328, 94500, 655500},
    SourcePartition{7, 88053, 639335, 672817, -500500, 60500}};
constexpr auto tile_intervals = [] {
  std::array<std::pair<std::int64_t, std::int64_t>, 8> result;
  for (auto& interval : result)
    interval = {std::numeric_limits<std::int64_t>::max(),
                std::numeric_limits<std::int64_t>::min()};
  for (const auto& p : partitions) {
    auto& interval = result[p.tile];
    interval.first = std::min(interval.first, p.front_z);
    interval.second = std::max(interval.second, p.rear_z);
  }
  return result;
}();
auto point(std::int64_t x, std::int64_t z) -> RigidVector3 {
  return {static_cast<double>(x) * 1e-6, kCabinCorridorFloorMetres,
          static_cast<double>(z) * 1e-6};
}
auto source_pair(const SourcePartition& p)
    -> std::array<std::array<RigidVector3, 3>, 2> {
  const auto a = point(p.front_width, p.front_z),
             b = point(-p.front_width, p.front_z),
             c = point(-p.rear_width, p.rear_z),
             d = point(p.rear_width, p.rear_z);
  return {{{a, b, c}, {a, c, d}}};
}
auto valid_center(double z) -> bool {
  return std::isfinite(z) && z >= kCabinCorridorMinimumCenterZMetres &&
         z <= kCabinCorridorMaximumCenterZMetres;
}
auto subtract(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto turn(RigidVector3 a, RigidVector3 b, RigidVector3 c) -> double {
  // No geometric epsilon: this is a membership test, not permission for an
  // adjacent representable intrusion. Source taper remains in these edges.
  return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
}
auto in_triangle(RigidVector3 p, const LowerCockpitFace& face) -> bool {
  const auto& t = face.points_current_metres;
  const auto a = turn(t[0], t[1], p), b = turn(t[1], t[2], p),
             c = turn(t[2], t[0], p);
  return (a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0);
}
auto band_clipped(std::vector<RigidVector3> polygon, double low_x,
                  double high_x, double low_z, double high_z)
    -> std::vector<RigidVector3> {
  polygon = Access::clip_floor_polygon(std::move(polygon), 0, low_x, true);
  polygon = Access::clip_floor_polygon(std::move(polygon), 0, high_x, false);
  polygon = Access::clip_floor_polygon(std::move(polygon), 2, low_z, true);
  return Access::clip_floor_polygon(std::move(polygon), 2, high_z, false);
}
struct IntervalLimit {
  double area_fraction{}, margin{};
};
auto interval_limit(double center, CabinCorridorLimitSide side)
    -> IntervalLimit {
  const auto low = center - .14, high = center + .14;
  double covered{}, support_low = std::numeric_limits<double>::max(),
                    support_high = -support_low;
  for (const auto& [begin, end] : tile_intervals) {
    // Actual admitted source-double endpoints, not ideal decimal gaps.
    const auto begin_metres = static_cast<double>(begin) * 1e-6,
               end_metres = static_cast<double>(end) * 1e-6;
    const auto a = std::max(low, begin_metres), b = std::min(high, end_metres);
    if (b > a) {
      covered += b - a;
      support_low = std::min(support_low, a);
      support_high = std::max(support_high, b);
    } else if (a == b &&
               ((side == CabinCorridorLimitSide::above &&
                 high == begin_metres) ||
                (side == CabinCorridorLimitSide::below && low == end_metres))) {
      // One-sided limit of a genuinely positive entering component. Its area
      // tends to zero while its extremity still changes the convex load hull.
      support_low = std::min(support_low, a);
      support_high = std::max(support_high, b);
    }
  }
  IntervalLimit result;
  result.area_fraction =
      std::nextafter(covered / .28 - kCabinCorridorAreaFractionReportingBound,
                     -std::numeric_limits<double>::infinity());
  if (covered > 0)
    result.margin = std::nextafter(
        std::min({.20, center - support_low, support_high - center}) -
            kCabinCorridorLengthReportingBoundMetres,
        -std::numeric_limits<double>::infinity());
  return result;
}
} // namespace

struct OriginCabinCorridorGeometry::Data {
  explicit Data(OriginLowerCockpitContact selected)
      : lower(std::move(selected)) {}
  OriginLowerCockpitContact lower;
  std::array<LowerCockpitFace, 18> tops;
};
OriginCabinCorridorGeometry::OriginCabinCorridorGeometry(
    std::shared_ptr<const Data> data)
    : data_(std::move(data)) {
}
auto OriginCabinCorridorGeometry::selected_top_faces() const
    -> std::span<const LowerCockpitFace> {
  return data_ ? std::span<const LowerCockpitFace>{data_->tops}
               : std::span<const LowerCockpitFace>{};
}
auto OriginCabinCorridorGeometry::lower_contact() const
    -> const OriginLowerCockpitContact* {
  return data_ ? &data_->lower : nullptr;
}
auto make_origin_cabin_corridor_geometry(const OriginLowerCockpitContact& lower)
    -> std::expected<OriginCabinCorridorGeometry, std::string> {
  const auto* original = lower.original_geometry();
  if (!original || !original->support_catalog())
    return std::unexpected("Corridor immutable admitted source required");
  auto data = std::make_shared<OriginCabinCorridorGeometry::Data>(lower);
  std::size_t cursor{};
  for (const auto& partition : partitions) {
    const auto pair = source_pair(partition);
    const auto expected_name = "CABIN | lift-out walking tile 0" +
                               std::to_string(partition.tile) + "-1";
    for (std::uint32_t i = 0; i < 2; ++i) {
      auto face = lookup_lower_cockpit_face(
          data->lower, {LowerCockpitContactBuffer::original, 0,
                        partition.first_triangle + i});
      if (!face || face->object != 178 + 3 * partition.tile ||
          face->source_object != expected_name ||
          face->points_current_metres != pair[i])
        return std::unexpected("Corridor exact source top partition required");
      data->tops[cursor++] = *face;
    }
    // Actual trapezoids, not their AABBs, cover both unchanged sole bands:
    // linear left/right edges stay strictly outside X[-.20,.20]. The paired
    // diagonal partitions the entire trapezoid without shared-edge area.
    if (partition.front_width <= 200000 || partition.rear_width <= 200000 ||
        partition.front_z >= partition.rear_z)
      return std::unexpected("Corridor complete source sole-band coverage");
  }
  // The two tile05 trapezoids have a shared full edge at Z=.7000 and disjoint
  // interiors. Their outer side slope decreases to zero, so their actual
  // union is convex. The other seven tiles each are one convex trapezoid.
  // Consequently a clipped plane contact contained at every vertex is wholly
  // contained in the actual same-tile source partition, including segments.
  if (partitions[5].front_z != partitions[6].rear_z ||
      partitions[5].front_width != partitions[6].rear_width)
    return std::unexpected("Corridor coplanar sliver source continuity");
  for (std::size_t i = 0; i + 1 < tile_intervals.size(); ++i)
    if (tile_intervals[i].first - tile_intervals[i + 1].second != 34000 ||
        tile_intervals[i].second - tile_intervals[i].first < 280000)
      return std::unexpected("Corridor actual interval partition/gaps");
  return OriginCabinCorridorGeometry{std::move(data)};
}
auto assess_origin_cabin_corridor_proxy_support(
    const OriginCabinCorridorGeometry& geometry, RigidVector3 foot)
    -> std::expected<CabinSeamSupport, std::string> {
  if (!geometry.data_) return std::unexpected("Corridor moved-from geometry");
  if (!Access::finite(foot))
    return std::unexpected("Corridor finite bounded sole pose required");
  CabinSeamSupport result;
  if (foot.y != kCabinCorridorFloorMetres) return result;
  std::vector<RigidVector3> support_points;
  for (std::size_t sole = 0; sole < 2; ++sole) {
    const auto x = foot.x + (sole == 0 ? -.14 : .14);
    auto& evidence = result.soles[sole];
    for (const auto& face : geometry.data_->tops) {
      auto polygon = band_clipped({face.points_current_metres.begin(),
                                   face.points_current_metres.end()},
                                  x - .06, x + .06, foot.z - .14, foot.z + .14);
      const auto area = Access::floor_polygon_area(polygon);
      evidence.area_square_metres += area;
      // Every positive source component contributes. No tiny-area cutoff can
      // silently delete a support-hull extremity beside a critical event.
      if (area > 0) {
        support_points.insert(support_points.end(), polygon.begin(),
                              polygon.end());
        evidence.pieces.push_back(
            {{face.key.group, face.key.triangle}, area, std::move(polygon)});
      }
    }
    evidence.area_fraction = evidence.area_square_metres / sole_area;
  }
  result.support_hull = Access::floor_support_hull(std::move(support_points));
  result.area_sufficient =
      std::ranges::all_of(result.soles, [](const CabinSoleSupport& s) {
        return s.area_fraction >= .75;
      });
  if (result.support_hull.size() >= 3) {
    double margin = 128;
    const auto& h = result.support_hull;
    for (std::size_t i = 0; i < h.size(); ++i) {
      const auto a = h[i], b = h[(i + 1) % h.size()];
      const auto edge = subtract(b, a);
      const auto length = std::hypot(edge.x, edge.z);
      if (length == 0) continue;
      margin = std::min(margin, static_cast<double>(turn(a, b, foot)) / length);
    }
    result.projected_load_margin_metres = margin;
    result.load_supported = margin >= .01;
  }
  return result;
}
auto assess_origin_cabin_corridor_support(
    const OriginCabinCorridorGeometry& geometry, double z)
    -> std::expected<CabinSeamSupport, std::string> {
  if (!valid_center(z))
    return std::unexpected("Corridor center outside certified range");
  return assess_origin_cabin_corridor_proxy_support(
      geometry, {0, kCabinCorridorFloorMetres, z});
}
auto certify_origin_cabin_corridor_translation(
    const OriginCabinCorridorGeometry& geometry, double from, double to)
    -> std::expected<CabinCorridorCertificate, std::string> {
  if (!geometry.data_) return std::unexpected("Corridor moved-from geometry");
  if (!valid_center(from) || !valid_center(to))
    return std::unexpected("Corridor translation outside certified range");
  CabinCorridorCertificate result;
  result.from_center_z_metres = from;
  result.to_center_z_metres = to;
  const auto low = std::min(from, to), high = std::max(from, to);
  result.critical_centers_z_metres = {low, high};
  auto& sources = result.source_events_micrometres;
  for (const auto endpoint : {-350000LL, 3840000LL}) {
    const auto event = static_cast<double>(endpoint) * 1e-6;
    if (event >= low && event <= high) sources.push_back(endpoint);
  }
  for (const auto& partition : partitions)
    for (const auto z : {partition.front_z, partition.rear_z})
      for (const auto offset : {-sole_half_length_um, sole_half_length_um}) {
        const auto source = z + offset;
        const auto event = static_cast<double>(source) * 1e-6;
        if (event >= low && event <= high) sources.push_back(source);
        // Match the actual reservation's literal half length: converting
        // integer 140000 by multiplication rounds to a different double.
        const auto arithmetic =
            static_cast<double>(z) * 1e-6 + (offset < 0 ? -.14 : .14);
        if (arithmetic >= low && arithmetic <= high)
          result.critical_centers_z_metres.push_back(arithmetic);
      }
  std::ranges::sort(sources);
  sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
  for (const auto source : sources)
    result.critical_centers_z_metres.push_back(static_cast<double>(source) *
                                               1e-6);
  auto& events = result.critical_centers_z_metres;
  std::ranges::sort(events);
  events.erase(std::unique(events.begin(), events.end()), events.end());

  result.minimum_sole_area_fraction = 1;
  result.minimum_load_margin_metres = 128;
  result.finite_support = true;
  auto analytic = events;
  // Adjacent binary values bracket source-edge event rounding. Together with
  // the complete spatial partition, analytic limits and outward reporting
  // bounds, retain minima of disappearing components beside split events.
  for (const auto event : events)
    for (const auto toward : {-std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::infinity()}) {
      const auto adjacent = std::nextafter(event, toward);
      if (adjacent > low && adjacent < high) analytic.push_back(adjacent);
    }
  std::ranges::sort(analytic);
  analytic.erase(std::unique(analytic.begin(), analytic.end()), analytic.end());
  // Within the proven source sole bands every support interval is rectangular.
  // Covered length is affine between actual sole-edge/top-edge events. Hull
  // extrema are affine there as well, but entering zero-area components cause
  // discontinuous limits. Check both one-sided limits explicitly. This is a
  // complete spatial partition, not a grid of temporal support observations.
  for (const auto center : analytic)
    for (const auto side :
         {CabinCorridorLimitSide::below, CabinCorridorLimitSide::at,
          CabinCorridorLimitSide::above}) {
      if ((center == low && side == CabinCorridorLimitSide::below) ||
          (center == high && side == CabinCorridorLimitSide::above))
        continue;
      const auto limit = interval_limit(center, side);
      result.support_limits.push_back(
          {center, side, limit.area_fraction, limit.margin});
      result.minimum_sole_area_fraction =
          std::min(result.minimum_sole_area_fraction, limit.area_fraction);
      result.minimum_load_margin_metres =
          std::min(result.minimum_load_margin_metres, limit.margin);
      result.finite_support = result.finite_support &&
                              limit.area_fraction >= .75 && limit.margin >= .01;
    }
  const auto clearance = assess_lower_cockpit_reservations(
      geometry.data_->lower, {0, kCabinCorridorFloorMetres, from},
      {0, kCabinCorridorFloorMetres, to});
  if (!clearance) return std::unexpected(clearance.error());
  result.swept_clearance = *clearance;
  result.boundary_support_compatible = true;
  const auto first = Access::reservations({0, kCabinCorridorFloorMetres, low}),
             last = Access::reservations({0, kCabinCorridorFloorMetres, high});
  for (const auto& contact : clearance->contacts) {
    if (contact.part == CabinProxyPart::body ||
        contact.intersection != CabinIntersection::boundary ||
        contact.key.buffer != LowerCockpitContactBuffer::original) {
      result.boundary_support_compatible = false;
      continue;
    }
    auto face = lookup_lower_cockpit_face(geometry.data_->lower, contact.key);
    if (!face) return std::unexpected(face.error());
    // Full SAT retains every bevel/neighbor. A permitted lower-face touch has
    // no source vertex above the sole plane and a real plane intersection.
    std::vector<RigidVector3> plane;
    for (const auto p : face->points_current_metres) {
      if (p.y > kCabinCorridorFloorMetres)
        result.boundary_support_compatible = false;
      if (p.y == kCabinCorridorFloorMetres) plane.push_back(p);
    }
    const auto part = static_cast<std::size_t>(contact.part);
    const auto swept = Access::unite(first[part], last[part]);
    plane = band_clipped(std::move(plane), swept.low.x, swept.high.x,
                         swept.low.z, swept.high.z);
    if (plane.empty()) result.boundary_support_compatible = false;
    for (const auto p : plane)
      if (!std::ranges::any_of(
              geometry.data_->tops, [&](const LowerCockpitFace& top) {
                return top.object == contact.object && in_triangle(p, top);
              }))
        result.boundary_support_compatible = false;
    // Convexity and exact face partition established above extend vertex
    // membership to the whole clipped point/segment/polygon. No tapered tile
    // bounding box or source-object collision exemption substitutes for this.
  }
  result.nonpenetrating_crossing =
      result.finite_support && clearance->coverage_complete &&
      clearance->interior_clear && result.boundary_support_compatible;
  return result;
}
} // namespace apsis_drift
