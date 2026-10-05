#include "apsis_drift/origin_boarding_boot_support.hpp"
#include "origin_boarding_foot_sites_internal.hpp"

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
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#if defined(__SSE2__) && defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace {
using namespace apsis_drift;
std::size_t checks{};
int failures{};
// Sites02 registered before evaluation: products are stored binary64 source
// coordinates, never the superseded unexecuted ideal0/-.16 assumption.
constexpr double upper_plane{kCabinCorridorFloorMetres};
constexpr double transition_plane{-160000.0 * 1e-6};
auto check(bool value, std::string_view label) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
template <class T, class E> auto require(std::expected<T, E> value) -> T {
  if (!value)
    throw std::runtime_error("Required fixed foot-site fixture refused");
  return std::move(*value);
}
auto bits(double a, double b) -> bool {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
// Independent exact signed binary64 sums in units2^-1074. Separate unsigned
// accumulators preserve nextafter/subnormal height signs after cancellation.
class DyadicSum {
 public:
  auto add(double x) -> void {
    if (++terms_ > 32 || !std::isfinite(x))
      throw std::runtime_error("Independent dyadic operand bound");
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
          throw std::runtime_error("Independent dyadic capacity");
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
auto sign(std::initializer_list<double> terms) -> int {
  DyadicSum result;
  for (double x : terms)
    result.add(x);
  return result.sign();
}
struct Point {
  long double x{}, z{};
};
auto side(Point a, Point b, Point p) -> long double {
  return (b.z - a.z) * (p.x - a.x) - (b.x - a.x) * (p.z - a.z);
}
auto norm2(Point a, Point b) -> long double {
  const auto x = b.x - a.x, z = b.z - a.z;
  return x * x + z * z;
}
auto source_polygon(const BoardingBootSourcePartition& part)
    -> std::array<Point, 4> {
  std::array<Point, 4> out;
  for (std::size_t i = 0; i < out.size(); ++i)
    out[i] = {part.perimeter_metres[i].x, part.perimeter_metres[i].z};
  return out;
}
auto sole_polygon(Point c) -> std::array<Point, 4> {
  const auto x = static_cast<long double>(.06),
             z = static_cast<long double>(.14);
  return {{{c.x + x, c.z - z},
           {c.x - x, c.z - z},
           {c.x - x, c.z + z},
           {c.x + x, c.z + z}}};
}
// Independent SAT over complete polygons. A resolved strict separation is the
// only negative overlap conclusion; boundary uncertainty is retained.
auto separated(const std::array<Point, 4>& a, const std::array<Point, 4>& b,
               bool relaxed = false) -> bool {
  for (std::size_t i = 0; i < a.size(); ++i) {
    bool all = true;
    for (const auto& p : b) {
      const auto v = side(a[i], a[(i + 1) % 4], p);
      const auto err =
          64 * std::numeric_limits<long double>::epsilon() * (1 + std::abs(v));
      all = all && v < (relaxed ? err : -err);
    }
    if (all) return true;
  }
  return false;
}
auto finite_polygon(const BoardingBootSourcePartition& part) -> void {
  const auto p = source_polygon(part);
  long double twice_area{};
  for (std::size_t i = 0; i < 4; ++i) {
    const auto& a = p[i];
    const auto& b = p[(i + 1) % 4];
    check(std::isfinite(a.x) && std::isfinite(a.z) && norm2(a, b) > 0,
          "Complete finite distinct source perimeter edges");
    twice_area += a.x * b.z - b.x * a.z;
    for (std::size_t j = 0; j < 4; ++j)
      if (j != i && j != (i + 1) % 4)
        check(side(a, b, p[j]) > 0,
              "Actual source perimeter is strictly convex clockwise");
  }
  check(twice_area < 0, "Source clockwise winding independently reconstructed");
}
auto source_checks(const OriginBoardingBootSupport& provider) -> void {
  constexpr std::array<std::uint32_t, 10> first{
      85129, 85537, 85945, 86353, 86761, 87211, 87219, 87645, 88053, 62119};
  constexpr std::array<std::uint32_t, 10> objects{178, 181, 184, 187, 190,
                                                  193, 193, 196, 199, 124};
  const auto parts = provider.selected_partitions();
  check(parts.size() == 10 && provider.contact() != nullptr,
        "Exact owned ten-partition provider");
  if (parts.size() != 10 || !provider.contact())
    throw std::runtime_error("Missing source provider");
  for (std::size_t i = 0; i < parts.size(); ++i) {
    check(bits(parts[i].plane_metres, i == 9 ? transition_plane : upper_plane),
          "Unchanged upper/transition source planes");
    finite_polygon(parts[i]);
    for (std::size_t j = 0; j < 2; ++j) {
      const auto& face = parts[i].faces[j];
      const LowerCockpitTriangleKey key{LowerCockpitContactBuffer::original, 0,
                                        first[i] +
                                            static_cast<std::uint32_t>(j)};
      check(face.key == key && face.object == objects[i] &&
                !face.evaluated_source_triangle,
            "Source faces retain original keys/object identities");
      const auto original =
          require(lookup_lower_cockpit_face(*provider.contact(), key));
      check(original.points_current_metres == face.points_current_metres &&
                original.unit_normal_current == face.unit_normal_current &&
                original.source_object == face.source_object,
            "Selected top triangles retain immutable actual source identity");
    }
  }
}
struct Reference {
  Point center, pressure;
  long double center_y{}, sole_y{};
  std::array<Point, 4> sole;
};
auto reference(std::size_t site, const BoardingFootSiteOffsets& offsets)
    -> Reference {
  const auto x = static_cast<long double>(.16) +
                 (site == 0 ? -static_cast<long double>(.14)
                            : static_cast<long double>(.14)) +
                 offsets.center_metres.x;
  const auto z =
      static_cast<long double>(site == 0 ? -.5 : -.8) + offsets.center_metres.z;
  const auto plane =
      static_cast<long double>(site == 0 ? upper_plane : transition_plane);
  const Point center{x, z};
  return {center,
          {x + offsets.pressure_xz_metres[0],
           z + static_cast<long double>(site == 0 ? .040 : 0.) +
               offsets.pressure_xz_metres[1]},
          plane + static_cast<long double>(.05) + offsets.center_metres.y,
          plane + offsets.center_metres.y,
          sole_polygon(center)};
}
// These bounded local products/sums use <=16 LD operations and values <=10.
// Only oracle roundoff is budgeted; no production contact tolerance is granted.
auto error_bound(long double value) -> long double {
  return 512 * std::numeric_limits<long double>::epsilon() *
         (1 + std::abs(value));
}
auto contains(const BoardingFootSiteScalarBounds& b, long double value)
    -> void {
  check(b.supported && std::isfinite(b.lower) && std::isfinite(b.upper) &&
            b.lower <= b.upper,
        "Supported ordered finite source edge bounds");
  check(static_cast<long double>(b.lower) <= value + error_bound(value) &&
            static_cast<long double>(b.upper) >= value - error_bound(value),
        "Independent finite perimeter expression belongs to outward bounds");
}
auto bounds_contain(const std::array<RigidVector3, 2>& b, Point p,
                    long double y) -> void {
  for (const auto& triple :
       std::array{std::array<long double, 3>{b[0].x, p.x, b[1].x},
                  std::array<long double, 3>{b[0].y, y, b[1].y},
                  std::array<long double, 3>{b[0].z, p.z, b[1].z}})
    check(std::isfinite(triple[0]) && std::isfinite(triple[2]) &&
              triple[0] <= triple[2] &&
              triple[0] <= triple[1] + error_bound(triple[1]) &&
              triple[2] >= triple[1] - error_bound(triple[1]),
          "Independent complete exact-expression point belongs to outward "
          "enclosure");
}
auto inspect_edge(const BoardingFootSiteEdgeEvidence& e, Point a, Point b,
                  Point p) -> void {
  const auto s = side(a, b, p), length = norm2(a, b);
  const auto radius =
      static_cast<long double>(.020) + static_cast<long double>(.010);
  const auto gap = s * s - radius * radius * length;
  contains(e.signed_side, s);
  contains(e.edge_length_squared, length);
  contains(e.squared_margin_gap, gap);
  if (e.disk_contained) {
    check(e.signed_side.lower > 0 && e.edge_length_squared.lower > 0 &&
              e.squared_margin_gap.lower >= 0,
          "Disk certificate uses supported positive side and nonnegative "
          "squared margin");
    check(s > 0 && gap >= -error_bound(gap),
          "Independent exact-source geometry corroborates granted disk margin");
  }
  if (s < -error_bound(s) || gap < -error_bound(gap))
    check(!e.disk_contained, "Resolved independent side/distance deficit "
                             "cannot grant disk containment");
}
auto expected_plane(std::size_t site, double height, double source_plane)
    -> BoardingBootPlaneRelation {
  const auto s = sign({site == 0 ? upper_plane : transition_plane, .05, height,
                       -.05, -source_plane});
  return s < 0   ? BoardingBootPlaneRelation::below
         : s > 0 ? BoardingBootPlaneRelation::above
                 : BoardingBootPlaneRelation::equal;
}
auto inspect(const BoardingFootSitesDiagnostic& d,
             const std::array<BoardingFootSiteOffsets, 2>& offsets = {},
             std::size_t cap = 10) -> void {
  check(d.sites_version == 2 && d.source.selected_partitions().size() == 10 &&
            d.source.contact(),
        "Diagnostic owns complete immutable source provider and version");
  check(bits(d.required_radius_margin_terms[0], .020) &&
            bits(d.required_radius_margin_terms[1], .010),
        "Unchanged exact radius-plus-margin operands retained");
  check(!d.body_qualified && !d.com_qualified && !d.force_qualified &&
            !d.load_qualified && !d.self_qualified && !d.world_qualified &&
            !d.crop_qualified && !d.sweep_qualified && !d.movement_qualified &&
            !d.route_qualified && !d.actor_qualified && !d.seat_qualified &&
            !d.save_qualified && !d.first_flight_qualified,
        "Foot-site eligibility grants no body/load/runtime authority");
  const auto parts = d.source.selected_partitions();
  if (parts.size() != 10)
    throw std::runtime_error("Missing owned partition roster");
  for (std::size_t i = 0; i < 2; ++i) {
    const auto& e = d.sites[i];
    const auto& o = offsets[i];
    const auto r = reference(i, o);
    check(bits(e.center_x_terms[0], .16) &&
              bits(e.center_x_terms[1], i == 0 ? -.14 : .14) &&
              bits(e.center_x_terms[2], o.center_metres.x),
          "Exact two-term X retains separate private offset");
    check(bits(e.center_y_terms[0], i == 0 ? upper_plane : transition_plane) &&
              bits(e.center_y_terms[1], .05) &&
              bits(e.center_y_terms[2], o.center_metres.y),
          "Exact source-plane/half-height expression retained");
    check(bits(e.center_z_terms[0], i == 0 ? -.5 : -.8) &&
              bits(e.center_z_terms[1], o.center_metres.z),
          "Fixed site Z is not selected from rounded report geometry");
    check(bits(e.pressure_offset_terms[0][0], 0.) &&
              bits(e.pressure_offset_terms[1][0], i == 0 ? .040 : 0.) &&
              bits(e.pressure_offset_terms[0][1], o.pressure_xz_metres[0]) &&
              bits(e.pressure_offset_terms[1][1], o.pressure_xz_metres[1]),
          "Frozen pressure offsets retain full expression lineage");
    check(e.half_size_metres == RigidVector3{.06, .05, .14},
          "Immutable flat sole dimensions");
    check(e.scanned_partitions <= cap &&
              e.coverage_complete == (e.scanned_partitions == 10),
          "Retained source scan prefix never becomes complete coverage");
    check(e.eligible == (e.arithmetic_supported && e.coverage_complete &&
                         e.placement_nonpenetrating && e.sole_disk_contained &&
                         e.source_disk_contained),
          "Site eligibility requires every separate predicate");
    check(e.eligible == !e.first_refusal.has_value(),
          "Each incomplete/refused site retains owned first refusal");
    if (!e.arithmetic_supported) {
      check(e.scanned_partitions == 0 && !e.eligible && !e.coverage_complete &&
                !e.sole_disk_contained && !e.source_disk_contained &&
                !e.placement_nonpenetrating,
            "Unsupported arithmetic scans no source and retains no positive "
            "eligibility flags");
      continue;
    }
    bounds_contain(e.center_bounds_metres, r.center, r.center_y);
    bounds_contain(e.pressure_bounds_metres, r.pressure, r.sole_y);
    bool sole_all = true;
    for (std::size_t j = 0; j < 4; ++j) {
      bounds_contain(e.sole_corner_bounds_metres[j], r.sole[j], r.sole_y);
      inspect_edge(e.sole_edges[j], r.sole[j], r.sole[(j + 1) % 4], r.pressure);
      sole_all = sole_all && e.sole_edges[j].disk_contained;
    }
    check(e.sole_disk_contained == sole_all,
          "Complete four-edge sole margin conjunction");
    bool source_any = false, higher_overlap = false;
    for (std::size_t p = 0; p < 10; ++p) {
      const auto& ev = e.partitions[p];
      check(ev.evaluated == (p < e.scanned_partitions),
            "Exact ascending ten-partition scan prefix");
      if (!ev.evaluated) {
        check(!ev.disk_contained,
              "Unexamined top grants no source disk authority");
        continue;
      }
      const auto polygon = source_polygon(parts[p]);
      check(ev.plane_relation ==
                expected_plane(i, o.center_metres.y, parts[p].plane_metres),
            "Integer dyadic oracle corroborates exact sole/source plane "
            "relation");
      const auto robust_overlap = !separated(r.sole, polygon, true) &&
                                  !separated(polygon, r.sole, true);
      if (robust_overlap)
        check(
            ev.footprint_overlap_possible,
            "Complete independent SAT cannot lose resolved footprint overlap");
      if (!ev.footprint_overlap_possible)
        check(
            !robust_overlap,
            "Only complete-polygon separation removes possible source overlap");
      higher_overlap = higher_overlap ||
                       (ev.footprint_overlap_possible &&
                        ev.plane_relation == BoardingBootPlaneRelation::below);
      if (ev.plane_relation == BoardingBootPlaneRelation::equal) {
        bool all = true;
        for (std::size_t j = 0; j < 4; ++j) {
          inspect_edge(ev.edges[j], polygon[j], polygon[(j + 1) % 4],
                       r.pressure);
          all = all && ev.edges[j].disk_contained;
        }
        if (ev.disk_contained)
          check(all,
                "Source containment retains all four perimeter certificates");
      }
      if (ev.disk_contained)
        check(
            ev.plane_relation == BoardingBootPlaneRelation::equal,
            "A source disk cannot float above or penetrate its selected plane");
      source_any = source_any || ev.disk_contained;
    }
    check(e.source_disk_contained == source_any,
          "Source admission needs one real matching-plane partition, not "
          "combined gaps");
    if (higher_overlap)
      check(!e.placement_nonpenetrating && !e.eligible,
            "Possible full-sole overlap below a higher top strictly refuses "
            "placement");
    if (e.source_partition) {
      check(*e.source_partition < e.scanned_partitions &&
                e.partitions[*e.source_partition].disk_contained,
            "Selected source index binds examined matching perimeter evidence");
    }
    if (e.first_refusal)
      check(e.first_refusal->site == i && (!e.first_refusal->partition ||
                                           *e.first_refusal->partition < 10),
            "Site refusal retains bounded original site/partition identity");
  }
  check(d.coverage_complete == (d.sites[0].coverage_complete &&
                                d.sites[1].coverage_complete) &&
            d.arithmetic_supported == (d.sites[0].arithmetic_supported &&
                                       d.sites[1].arithmetic_supported) &&
            d.eligible == (d.sites[0].eligible && d.sites[1].eligible),
        "Summary predicates retain separate full-site conjunctions");
  check(d.eligible == !d.first_refusal.has_value(),
        "Overall refusal cannot masquerade as complete site eligibility");
}
auto observe(const OriginBoardingBootSupport& provider) -> void {
  const auto d = require(assess_origin_boarding_foot_sites(provider));
  std::cout << std::setprecision(17);
  for (std::size_t i = 0; i < 2; ++i) {
    const auto& s = d.sites[i];
    std::cout << "fixed site=" << i << " arithmetic=" << s.arithmetic_supported
              << " coverage=" << s.coverage_complete
              << " placement=" << s.placement_nonpenetrating
              << " sole=" << s.sole_disk_contained
              << " source=" << s.source_disk_contained
              << " eligible=" << s.eligible
              << " scanned=" << s.scanned_partitions << " source_partition=";
    if (s.source_partition)
      std::cout << *s.source_partition;
    else
      std::cout << "none";
    if (s.first_refusal)
      std::cout << " refusal="
                << static_cast<unsigned>(s.first_refusal->condition);
    std::cout << " pressureX=[" << s.pressure_bounds_metres[0].x << ','
              << s.pressure_bounds_metres[1].x << "] pressureZ=["
              << s.pressure_bounds_metres[0].z << ','
              << s.pressure_bounds_metres[1].z << "]\n";
  }
  inspect(d);
  source_checks(d.source);
  check(
      d.arithmetic_supported && d.coverage_complete && d.eligible &&
          !d.first_refusal,
      "Observed fixed public assessment retains complete two-site eligibility");
  for (std::size_t i = 0; i < d.sites.size(); ++i) {
    const auto& site = d.sites[i];
    const auto expected_partition = i == 0 ? std::size_t{8} : std::size_t{9};
    check(site.arithmetic_supported && site.coverage_complete &&
              site.placement_nonpenetrating && site.sole_disk_contained &&
              site.source_disk_contained && site.eligible &&
              site.scanned_partitions == 10 && !site.first_refusal &&
              site.source_partition == expected_partition,
          "Observed unchanged public site retains all predicates and exact "
          "source binding");
    const auto& source = site.partitions[expected_partition];
    check(source.evaluated && source.footprint_overlap_possible &&
              source.plane_relation == BoardingBootPlaneRelation::equal &&
              source.disk_contained,
          "Observed selected source retains complete matching-plane disk "
          "evidence");
    for (std::size_t edge = 0; edge < site.sole_edges.size(); ++edge) {
      for (const auto* certificate :
           std::array{&site.sole_edges[edge], &source.edges[edge]}) {
        check(certificate->disk_contained &&
                  certificate->signed_side.supported &&
                  certificate->edge_length_squared.supported &&
                  certificate->squared_margin_gap.supported &&
                  certificate->signed_side.lower > 0 &&
                  certificate->edge_length_squared.lower > 0 &&
                  certificate->squared_margin_gap.lower >= 0,
              "Observed sole and selected-source edges retain supported strict "
              "side and squared margin");
      }
    }
  }
}
auto query(const OriginBoardingBootSupport& p,
           const std::array<BoardingFootSiteOffsets, 2>& offsets = {},
           std::size_t cap = 10) -> BoardingFootSitesDiagnostic {
  auto d = require(detail::boarding_foot_sites_bounded(p, offsets, cap));
  inspect(d, offsets, cap);
  return d;
}
auto private_controls(const OriginBoardingBootSupport& p) -> void {
  std::array<BoardingFootSiteOffsets, 2> o{};
  o[0].pressure_xz_metres[1] = -.040;
  const auto centered = query(p, o);
  check(!centered.sites[0].source_disk_contained && !centered.sites[0].eligible,
        "Declared centered upper pressure has insufficient actual rear-source "
        "disk margin");
  // A source partition is wide enough to contain this point, but its own sole
  // is not; this is never a request for arbitrary source/caller geometry.
  o = {};
  o[0].pressure_xz_metres[0] = .20;
  const auto outside_sole = query(p, o);
  check(!outside_sole.sites[0].sole_disk_contained &&
            !outside_sole.sites[0].eligible,
        "Outside-only-sole pressure cannot gain source-only eligibility");
  o = {};
  o[0].pressure_xz_metres[1] = -.05;
  const auto outside_source = query(p, o);
  check(outside_source.sites[0].sole_disk_contained &&
            !outside_source.sites[0].source_disk_contained &&
            !outside_source.sites[0].eligible,
        "Inside sole pressure beyond actual rear source is refused");
  o = {};
  o[0].center_metres.z = -.09;
  const auto gap = query(p, o);
  check(!gap.sites[0].source_disk_contained && !gap.sites[0].eligible,
        "Actual upper/transition source gap is never filled by a convex hull "
        "or neighboring height");
  o = {};
  o[1].center_metres.z = .30;
  const auto higher = query(p, o);
  check(!higher.sites[1].placement_nonpenetrating && !higher.sites[1].eligible,
        "Transition sole footprint below possible higher upper top overlap "
        "refuses");
  for (std::size_t i = 0; i < 2; ++i) {
    for (double height :
         std::array{std::numeric_limits<double>::denorm_min(),
                    -std::numeric_limits<double>::denorm_min(), .01, -.01}) {
      o = {};
      o[i].center_metres.y = height;
      const auto d = query(p, o);
      check(!d.sites[i].source_disk_contained && !d.sites[i].eligible,
            "Every nonzero exact plane offset refuses matching-plane disk "
            "eligibility");
      if (height < 0)
        check(!d.sites[i].placement_nonpenetrating,
              "Subnormal or normal downward offset retains actual-top "
              "penetration");
    }
  }
  o = {};
  o[1].center_metres.y = .16;
  const auto wrong_height = query(p, o);
  check(!wrong_height.sites[1].source_disk_contained &&
            !wrong_height.sites[1].eligible,
        "Upper height over transition source cannot create an invisible floor");
}
auto boundary_controls(const OriginBoardingBootSupport& p) -> void {
  // Fixed, pre-observation neighborhoods of the exact R=.020+.010 sole
  // and actual upper rear perimeter margins. No candidate search/refitting.
  for (const auto& boundary :
       std::array{std::pair<std::size_t, double>{0, -.030},
                  std::pair<std::size_t, double>{0, .030},
                  std::pair<std::size_t, double>{1, -.0105}}) {
    for (double value :
         std::array{std::nextafter(boundary.second,
                                   -std::numeric_limits<double>::infinity()),
                    boundary.second,
                    std::nextafter(boundary.second,
                                   std::numeric_limits<double>::infinity())}) {
      std::array<BoardingFootSiteOffsets, 2> o{};
      o[0].pressure_xz_metres[boundary.first] = value;
      const auto d = query(p, o);
      bool deficit = false;
      const auto r = reference(0, o[0]);
      const auto polygon = boundary.first == 0
                               ? r.sole
                               : source_polygon(p.selected_partitions()[8]);
      const auto radius =
          static_cast<long double>(.020) + static_cast<long double>(.010);
      for (std::size_t e = 0; e < 4; ++e) {
        const auto s = side(polygon[e], polygon[(e + 1) % 4], r.pressure);
        const auto margin =
            s * s - radius * radius * norm2(polygon[e], polygon[(e + 1) % 4]);
        deficit =
            deficit || s < -error_bound(s) || margin < -error_bound(margin);
      }
      if (deficit)
        check(!d.sites[0].eligible, "Independent strict edge deficit forbids "
                                    "contact at adjacent margin inputs");
      const auto& edges = boundary.first == 0 ? d.sites[0].sole_edges
                                              : d.sites[0].partitions[8].edges;
      for (const auto& edge : edges)
        if (edge.squared_margin_gap.supported &&
            edge.squared_margin_gap.lower < 0)
          check(!edge.disk_contained,
                "Outward boundary uncertainty never grants disk containment");
    }
  }
  // Complete zero-area footprint touch with the actual upper rear edge; even
  // an equal plane cannot place a pressure disk across a source boundary.
  std::array<BoardingFootSiteOffsets, 2> o{};
  o[0].center_metres.z = -.1405;
  const auto touch = query(p, o);
  check(!touch.sites[0].source_disk_contained && !touch.sites[0].eligible,
        "Zero-area/adjacent source-edge touch never supplies finite disk "
        "support");
}
auto offset_component(BoardingFootSiteOffsets& o, std::size_t component)
    -> double& {
  switch (component) {
    case 0: return o.center_metres.x;
    case 1: return o.center_metres.y;
    case 2: return o.center_metres.z;
    case 3: return o.pressure_xz_metres[0];
    default: return o.pressure_xz_metres[1];
  }
}
auto capacity_invalid_controls(const OriginBoardingBootSupport& p) -> void {
  for (std::size_t cap = 0; cap < 10; ++cap) {
    const auto d = query(p, {}, cap);
    check(!d.coverage_complete && !d.eligible && d.first_refusal,
          "Lowered ten-partition work cap retains incomplete eligibility");
    for (const auto& s : d.sites)
      check(s.scanned_partitions == cap && !s.coverage_complete && !s.eligible,
            "Exact retained scan prefix counts include every examined source");
  }
  check(!detail::boarding_foot_sites_bounded(p, {}, 11),
        "Cannot enlarge fixed source scan capacity");
  for (std::size_t site = 0; site < 2; ++site) {
    for (std::size_t component = 0; component < 5; ++component) {
      for (double invalid :
           std::array{std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::infinity(),
                      -std::numeric_limits<double>::infinity(),
                      std::nextafter(8., 9.), std::nextafter(-8., -9.)}) {
        std::array<BoardingFootSiteOffsets, 2> o{};
        offset_component(o[site], component) = invalid;
        check(!detail::boarding_foot_sites_bounded(p, o),
              "Nonfinite/excessive center and pressure offsets are API errors");
      }
      for (double endpoint : std::array{-8., 8.}) {
        std::array<BoardingFootSiteOffsets, 2> o{};
        offset_component(o[site], component) = endpoint;
        const auto d = query(p, o);
        check(!d.sites[site].eligible, "Inclusive workspace endpoint retains "
                                       "bounded refusal, never a new source");
      }
    }
  }
  check(sign({1., std::numeric_limits<double>::denorm_min(), -1.}) == 1 &&
            sign({1., -std::numeric_limits<double>::denorm_min(), -1.}) == -1 &&
            sign({.16, -.14, -.16, .14}) == 0,
        "Independent dyadic oracle retains small signs and exact expression "
        "cancellation");
}
struct SavedEnvironment {
  std::fenv_t saved{};
  SavedEnvironment() {
    if (std::fegetenv(&saved) != 0)
      throw std::runtime_error("Cannot save environment");
  }
  SavedEnvironment(const SavedEnvironment&) = delete;
  auto operator=(const SavedEnvironment&) -> SavedEnvironment& = delete;
  ~SavedEnvironment() {
    check(std::fesetenv(&saved) == 0,
          "Restore arithmetic state after site adversary");
  }
};
auto denied_environment(const OriginBoardingBootSupport& p) -> void {
  const auto d = require(assess_origin_boarding_foot_sites(p));
  check(!d.arithmetic_supported && !d.coverage_complete && !d.eligible &&
            d.first_refusal &&
            d.first_refusal->condition ==
                BoardingFootSiteCondition::unsupported_arithmetic,
        "Unsupported environment cannot grant fixed source-site eligibility");
  for (const auto& s : d.sites)
    check(!s.arithmetic_supported && !s.coverage_complete && !s.eligible &&
              !s.placement_nonpenetrating && !s.sole_disk_contained &&
              !s.source_disk_contained && s.scanned_partitions == 0 &&
              !s.source_partition,
          "Unsupported arithmetic refuses before any source scan or geometric "
          "certificate");
}
auto environment_controls(const OriginBoardingBootSupport& p) -> void {
  for (int mode : std::array{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    SavedEnvironment saved;
    check(std::fesetround(mode) == 0, "Set directed rounding adversary");
    denied_environment(p);
  }
#if defined(__SSE2__) && defined(__x86_64__)
  struct SavedControl {
    unsigned saved{_mm_getcsr()};
    ~SavedControl() { _mm_setcsr(saved); }
  };
  for (unsigned mode : std::array{1U << 15, 1U << 6, (1U << 15) | (1U << 6)}) {
    SavedEnvironment saved;
    SavedControl control;
    _mm_setcsr(control.saved | mode);
    denied_environment(p);
  }
  for (unsigned mode : std::array{1U, 2U, 3U}) {
    SavedEnvironment saved;
    SavedControl control;
    _mm_setcsr((control.saved & ~(3U << 13)) | (mode << 13));
    denied_environment(p);
  }
#endif
  check(std::fegetround() == FE_TONEAREST,
        "Restore nearest arithmetic before later site observations");
}
struct Snapshot {
  std::vector<std::uint64_t> words;
  std::vector<std::string> names;
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
  auto scalar(const BoardingFootSiteScalarBounds& b) -> void {
    number(b.lower);
    number(b.upper);
    integer(b.supported);
  }
  auto edge(const BoardingFootSiteEdgeEvidence& e) -> void {
    scalar(e.signed_side);
    scalar(e.edge_length_squared);
    scalar(e.squared_margin_gap);
    integer(e.disk_contained);
  }
  auto refusal(const std::optional<BoardingFootSiteRefusal>& r) -> void {
    integer(r.has_value());
    if (r) {
      integer(r->site);
      integer(r->partition.has_value());
      if (r->partition) integer(*r->partition);
      integer(r->condition);
    }
  }
  friend auto operator==(const Snapshot&, const Snapshot&) -> bool = default;
};
auto snapshot(const BoardingFootSitesDiagnostic& d) -> Snapshot {
  Snapshot out;
  out.integer(d.sites_version);
  out.integer(d.arithmetic_supported);
  out.integer(d.coverage_complete);
  out.integer(d.eligible);
  out.refusal(d.first_refusal);
  for (double x : d.required_radius_margin_terms)
    out.number(x);
  for (const auto& p : d.source.selected_partitions()) {
    out.number(p.plane_metres);
    for (auto point : p.perimeter_metres)
      out.point(point);
    for (const auto& f : p.faces) {
      out.integer(f.key.buffer);
      out.integer(f.key.group);
      out.integer(f.key.triangle);
      out.integer(f.object);
      out.integer(f.evaluated_source_triangle.has_value());
      if (f.evaluated_source_triangle)
        out.integer(*f.evaluated_source_triangle);
      out.names.emplace_back(f.source_object);
      for (auto point : f.points_current_metres)
        out.point(point);
      out.point(f.unit_normal_current);
    }
  }
  for (const auto& s : d.sites) {
    for (double x : s.center_x_terms)
      out.number(x);
    for (double x : s.center_y_terms)
      out.number(x);
    for (double x : s.center_z_terms)
      out.number(x);
    for (const auto& row : s.pressure_offset_terms)
      for (double x : row)
        out.number(x);
    out.point(s.half_size_metres);
    for (auto p : s.center_bounds_metres)
      out.point(p);
    for (auto p : s.pressure_bounds_metres)
      out.point(p);
    for (const auto& row : s.sole_corner_bounds_metres)
      for (auto p : row)
        out.point(p);
    for (const auto& e : s.sole_edges)
      out.edge(e);
    for (const auto& p : s.partitions) {
      out.integer(p.evaluated);
      out.integer(p.footprint_overlap_possible);
      out.integer(p.disk_contained);
      out.integer(p.plane_relation);
      for (const auto& e : p.edges)
        out.edge(e);
    }
    out.integer(s.source_partition.has_value());
    if (s.source_partition) out.integer(*s.source_partition);
    out.integer(s.scanned_partitions);
    out.integer(s.arithmetic_supported);
    out.integer(s.coverage_complete);
    out.integer(s.placement_nonpenetrating);
    out.integer(s.sole_disk_contained);
    out.integer(s.source_disk_contained);
    out.integer(s.eligible);
    out.refusal(s.first_refusal);
  }
  return out;
}
auto ownership_controls(const OriginBoardingBootSupport& provider) -> void {
  const auto initial = require(assess_origin_boarding_foot_sites(provider));
  const auto original = snapshot(initial);
  auto survivor = [] {
    auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    auto provider = require(make_origin_boarding_boot_support(binding));
    auto diagnostic = require(assess_origin_boarding_foot_sites(provider));
    auto moved_binding = std::move(binding);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Empty-handle API contract.
    check(!make_origin_boarding_boot_support(binding),
          "Moved-from binding cannot supply a site provider");
    check(moved_binding.contact() != nullptr,
          "Moved binding owns immutable original selection");
    auto moved_provider = std::move(provider);
    // NOLINTBEGIN(bugprone-use-after-move) -- Empty-handle API controls.
    check(!assess_origin_boarding_foot_sites(provider) &&
              !detail::boarding_foot_sites_bounded(provider),
          "Moved-from provider refuses both fixed and private queries");
    // NOLINTEND(bugprone-use-after-move) -- End empty-handle API controls.
    check(moved_provider.selected_partitions().size() == 10,
          "Moved provider retains full source selection");
    return diagnostic;
  }();
  check(snapshot(survivor) == original, "Diagnostic source faces, names and "
                                        "all evidence survive caller lifetime");
  auto copy = survivor;
  auto moved = std::move(survivor);
  check(snapshot(copy) == original && snapshot(moved) == original,
        "Copied/moved diagnostic preserves full owned source and expression "
        "evidence");
  copy.sites[0].center_x_terms[0] = 3.;
  copy.sites[0].partitions[8].disk_contained = false;
  copy.sites[0].eligible = false;
  check(snapshot(moved) == original,
        "Mutating caller evidence copy cannot alter another diagnostic or "
        "immutable source");
  check(snapshot(require(assess_origin_boarding_foot_sites(moved.source))) ==
            original,
        "Later query uses immutable compiled source, never caller-edited "
        "evidence as authority");
  auto shared = moved.source;
  auto retained = std::move(moved.source);
  check(!assess_origin_boarding_foot_sites(moved.source),
        "Moved-from diagnostic source remains unavailable");
  check(snapshot(require(assess_origin_boarding_foot_sites(shared))) ==
                original &&
            snapshot(require(assess_origin_boarding_foot_sites(retained))) ==
                original,
        "Result-owned provider sharing and movement preserve source binding "
        "once");
}
} // namespace
int main() {
  try {
    if (std::numeric_limits<long double>::digits < 64)
      throw std::runtime_error(
          "Independent finite perimeter oracle requires64 mantissa bits");
    const auto binding = require(make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{}));
    const auto provider = require(make_origin_boarding_boot_support(binding));
    // FIRST evaluation is the unchanged public registered sites, before all
    // private controls. Its actual predicates are printed, not predicted.
    observe(provider);
    private_controls(provider);
    boundary_controls(provider);
    capacity_invalid_controls(provider);
    environment_controls(provider);
    ownership_controls(provider);
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout << checks << " fixed foot-site checks, " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
