#include "apsis_drift/origin_boarding_initial_material.hpp"
#include "origin_boarding_initial_material_internal.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <limits>
#include <utility>

namespace apsis_drift {
namespace {
using Vec = RigidVector3;
using Bounds = BoardingPlantedLegPointBounds;
using Scalar = BoardingFootSiteScalarBounds;
using Solid = detail::BoardingSourceEndpointSurfaceCheckpointSolidBounds;
using Shape = detail::BoardingSourceEndpointSurfaceCheckpointShape;
using Limits = detail::BoardingInitialMaterialLimits;
using Condition = BoardingInitialMaterialCondition;
using Relation = BoardingInitialMaterialRelation;
using Evidence = detail::MaterialEnclosureMathEvidence;
constexpr double source_allowance = 1e-6;
constexpr std::size_t source_rows = 1759;
constexpr std::string_view completion_pin =
    "09cd65c9d0080ab792b03ab458b597db71e5cb80c38184818a6ed69a3389f63c";
constexpr std::string_view binary_pin =
    "2d6f51d7a7476d8fc0d140344f82eb8958c59e0ea3866053d99ef5cfb36840c4";
auto component(Vec p, std::size_t a) -> double {
  return a == 0 ? p.x : a == 1 ? p.y : p.z;
}
auto set(Vec& p, std::size_t a, double x) -> void {
  if (a == 0)
    p.x = x;
  else if (a == 1)
    p.y = x;
  else
    p.z = x;
}
auto down(double x) -> double {
  return std::nextafter(x, -std::numeric_limits<double>::infinity());
}
auto up(double x) -> double {
  return std::nextafter(x, std::numeric_limits<double>::infinity());
}
auto environment() -> bool {
  if (!std::numeric_limits<double>::is_iec559 ||
      std::numeric_limits<double>::radix != 2 ||
      std::numeric_limits<double>::digits != 53 ||
      std::fegetround() != FE_TONEAREST)
    return false;
  volatile double normal = std::numeric_limits<double>::min(), half = .5,
                  subnormal = std::numeric_limits<double>::denorm_min(),
                  one = 1., tie = 0x1p-53, above_tie = 0x1.8p-53;
  const double tiny = normal * half, preserved = subnormal * one,
               tie_sum = one + tie, above_sum = one + above_tie;
  return std::bit_cast<std::uint64_t>(tiny) == 0x0008000000000000ULL &&
         std::bit_cast<std::uint64_t>(preserved) == 1 &&
         std::bit_cast<std::uint64_t>(tie_sum) == 0x3ff0000000000000ULL &&
         std::bit_cast<std::uint64_t>(above_sum) == 0x3ff0000000000001ULL;
}
auto scalar(double x) -> Scalar {
  return {x, x, std::isfinite(x)};
}
auto range(double a, double b) -> Scalar {
  return {a, b, std::isfinite(a) && std::isfinite(b) && a <= b};
}
auto add(Scalar a, Scalar b) -> Scalar {
  if (!a.supported || !b.supported) return {};
  if (a.lower == 0 && a.upper == 0) return b;
  if (b.lower == 0 && b.upper == 0) return a;
  return range(down(a.lower + b.lower), up(a.upper + b.upper));
}
auto negate(Scalar a) -> Scalar {
  return {-a.upper, -a.lower, a.supported};
}
auto sub(Scalar a, Scalar b) -> Scalar {
  return add(a, negate(b));
}
auto mul(Scalar a, Scalar b) -> Scalar {
  if (!a.supported || !b.supported) return {};
  if ((a.lower == 0 && a.upper == 0) || (b.lower == 0 && b.upper == 0))
    return scalar(0);
  const std::array values{a.lower * b.lower, a.lower * b.upper,
                          a.upper * b.lower, a.upper * b.upper};
  return range(down(*std::min_element(values.begin(), values.end())),
               up(*std::max_element(values.begin(), values.end())));
}
auto square(Scalar a) -> Scalar {
  if (!a.supported) return {};
  const double low = a.lower <= 0 && a.upper >= 0
                         ? 0
                         : std::min(a.lower * a.lower, a.upper * a.upper);
  return range(low == 0 ? 0 : down(low),
               up(std::max(a.lower * a.lower, a.upper * a.upper)));
}
auto divide(Scalar a, Scalar b) -> Scalar {
  if (!a.supported || !b.supported || b.lower <= 0) return {};
  return mul(a, range(down(1 / b.upper), up(1 / b.lower)));
}
auto valid(Bounds b) -> bool {
  for (std::size_t a = 0; a < 3; ++a)
    if (!std::isfinite(component(b.lower, a)) ||
        !std::isfinite(component(b.upper, a)) ||
        component(b.lower, a) > component(b.upper, a) ||
        std::max(std::abs(component(b.lower, a)),
                 std::abs(component(b.upper, a))) > 1000)
      return false;
  return true;
}
auto point(Vec p) -> Bounds {
  return {p, p};
}
auto axis(Bounds b, std::size_t a) -> Scalar {
  return range(component(b.lower, a), component(b.upper, a));
}
auto dot(Bounds b, Vec n) -> Scalar {
  Scalar out = scalar(0);
  for (std::size_t a = 0; a < 3; ++a)
    out = add(out, mul(axis(b, a), scalar(component(n, a))));
  return out;
}
auto midpoint(Bounds b) -> Vec {
  return {(b.lower.x + b.upper.x) * .5, (b.lower.y + b.upper.y) * .5,
          (b.lower.z + b.upper.z) * .5};
}
auto minus(Vec a, Vec b) -> Vec {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto cross(Vec a, Vec b) -> Vec {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto union_bounds(Bounds a, Bounds b) -> Bounds {
  Bounds out;
  for (std::size_t i = 0; i < 3; ++i) {
    set(out.lower, i, std::min(component(a.lower, i), component(b.lower, i)));
    set(out.upper, i, std::max(component(a.upper, i), component(b.upper, i)));
  }
  return out;
}
auto transform(Bounds b, const OperatingTransform& m) -> Bounds {
  Bounds out;
  for (std::size_t a = 0; a < 3; ++a) {
    Scalar v = scalar(component(m.columns[3], a));
    for (std::size_t c = 0; c < 3; ++c)
      v = add(v, mul(axis(b, c), scalar(component(m.columns[c], a))));
    if (!v.supported) return {{NAN, NAN, NAN}, {NAN, NAN, NAN}};
    set(out.lower, a, v.lower);
    set(out.upper, a, v.upper);
  }
  return out;
}
auto permutation(const OperatingTransform& m) -> bool {
  for (std::size_t a = 0; a < 3; ++a) {
    std::size_t count = 0;
    for (std::size_t c = 0; c < 3; ++c) {
      double x = component(m.columns[c], a);
      if (x != 0 && x != 1 && x != -1) return false;
      count += x != 0;
    }
    if (count != 1) return false;
  }
  for (std::size_t c = 0; c < 3; ++c) {
    std::size_t count = 0;
    for (std::size_t a = 0; a < 3; ++a)
      count += component(m.columns[c], a) != 0;
    if (count != 1) return false;
  }
  return valid(point(m.columns[3]));
}
auto qpoint(const detail::MaterialQuantizedPoint& q) -> Vec {
  return {static_cast<double>(q.value[0]) * 1e-6,
          static_cast<double>(q.value[1]) * 1e-6,
          static_cast<double>(q.value[2]) * 1e-6};
}
// q is exact below 1e9. Nearest micrometre rounding costs <= .5um per
// coordinate; stored 1e-6 multiplication at |world|<=1000 costs <2^-40m.
// sqrt(3)<7/4, hence total Euclidean raw/game discrepancy is <1um.
// This is SOURCE inclusion, never a body/contact tolerance. Actual raw AND
// q vertices must still pass the frozen radius/plane proof below.
auto source_vertex_valid(Vec raw, const detail::MaterialQuantizedPoint& q)
    -> bool {
  if (!valid(point(raw))) return false;
  for (std::size_t a = 0; a < 3; ++a) {
    const auto x = q.value[a];
    if (x < -1000000000 || x > 1000000000 ||
        std::nearbyint(component(raw, a) * 1e6) != static_cast<double>(x))
      return false;
  }
  return valid(point(qpoint(q)));
}
auto contained_capsule(Bounds p, Bounds first, Bounds last, double radius)
    -> bool {
  Scalar numerator = scalar(0), length = scalar(0);
  std::array<Scalar, 3> d{}, r{};
  for (std::size_t a = 0; a < 3; ++a) {
    d[a] = sub(axis(last, a), axis(first, a));
    r[a] = sub(axis(p, a), axis(first, a));
    numerator = add(numerator, mul(d[a], r[a]));
    length = add(length, square(d[a]));
  }
  auto t = divide(numerator, length);
  if (!t.supported) return false;
  t.lower = std::clamp(t.lower, 0., 1.);
  t.upper = std::clamp(t.upper, 0., 1.);
  Scalar distance = scalar(0);
  for (std::size_t a = 0; a < 3; ++a)
    distance = add(distance, square(sub(r[a], mul(t, d[a]))));
  const auto rr = square(scalar(radius));
  return distance.supported && rr.supported && distance.upper <= rr.lower;
}
// The old kernel encloses the support MAXIMUM h(n), not the projection
// range. Use both signed supports to enclose every point of the solid:
// [min dot(n,p), max dot(n,p)] is [-h(-n),h(n)]. In particular h(n).lower
// must never be used as the minimum of a body or source projection.
auto projection(const Solid& solid, double common, Vec n)
    -> std::expected<Scalar, std::string> {
  const auto positive =
      detail::boarding_source_endpoint_surface_checkpoint_support_math(
          solid, common, n);
  if (!positive || !positive->supported)
    return std::unexpected("Material positive support unsupported");
  const Vec negative_direction{-n.x, -n.y, -n.z};
  const auto negative =
      detail::boarding_source_endpoint_surface_checkpoint_support_math(
          solid, common, negative_direction);
  if (!negative || !negative->supported)
    return std::unexpected("Material negative support unsupported");
  auto result = range(-negative->upper, positive->upper);
  if (!result.supported)
    return std::unexpected("Material signed projection unsupported");
  return result;
}
auto world_bounds(const Solid& body, double common)
    -> std::expected<Bounds, std::string> {
  Bounds out;
  for (std::size_t a = 0; a < 3; ++a) {
    Vec n{};
    set(n, a, 1);
    auto s = projection(body, common, n);
    if (!s) return std::unexpected(s.error());
    set(out.lower, a, s->lower);
    set(out.upper, a, s->upper);
  }
  return out;
}
auto support_source(Bounds first, Bounds second, double radius, Vec n)
    -> Scalar {
  const Solid source{first, second, {}, radius, Shape::capsule};
  const auto support = projection(source, 0, n);
  return support ? *support : Scalar{};
}
auto bounded_direction(Vec v) -> Vec {
  const double m = std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)});
  if (!std::isfinite(m) || m == 0) return {};
  if (m > 1) return {v.x / m, v.y / m, v.z / m};
  return v;
}
auto valid_limits(const Limits& l) -> bool {
  const Limits max;
  return l.source_bytes <= max.source_bytes &&
         l.ring_inclusions <= max.ring_inclusions &&
         l.support_plane_inclusions <= max.support_plane_inclusions &&
         l.envelope_pairs <= max.envelope_pairs &&
         l.enclosure_pairs <= max.enclosure_pairs &&
         l.triangle_visits <= max.triangle_visits &&
         l.boundary_pairs <= max.boundary_pairs && l.axes <= max.axes &&
         l.pair_axes <= max.pair_axes;
}
} // namespace
struct OriginBoardingInitialMaterial::Data {
  struct Segment {
    std::uint32_t source{};
    Bounds first, second;
    double radius{};
  };
  struct Support {
    std::uint32_t source{};
    std::array<detail::MaterialPlane, 6> planes;
  };
  NativeCraftBinding binding;
  detail::MaterialPreparedView prepared;
  BoardingInitialMaterialSourceSummary summary;
  std::array<Bounds, source_rows> envelopes;
  std::array<Segment, 14> segments;
  std::array<Support, 2> supports;
};
namespace {
using Data = OriginBoardingInitialMaterial::Data;
using Segment = Data::Segment;
using Support = Data::Support;
static_assert(sizeof(BoardingInitialMaterialPayload) <= 16384);
static_assert(sizeof(Data) + 13238724 + 1000000 <=
              kBoardingInitialMaterialMaximumSourceBytes);
// Numeric live ledger: solids2160 + full bounds720 + full result<4096 +
// constructor phase points/planes/intervals<3072 (not co-live with query),
// query control/error/expected return pool2048 + old nested pair kernel4096.
// Source arrays/envelopes/constructor certificates belong to retained Data.
static_assert(sizeof(Solid) == 136);
static_assert(sizeof(BoardingSourceEndpointLoadDiagnostic) == 34136);
static_assert(sizeof(BoardingInitialMaterialPayload) == 2264);
static_assert(sizeof(Data) == 86816);
// Both compilers' O3 stack reports audited before any producer observation:
// GCC query39424 - separately owned child34136 + capsule1248 + projection224
// +old kernel4096 +1024 control/error/outgoing pool =11880. Clang's path is
// smaller. The phased initial_result may briefly move old child storage but
// ends before new geometry math. Constructor admission's retained Data is
// source ownership, not hidden numeric scratch; its nested path is <6KiB.
constexpr std::size_t material_live_bound = 12000;
static_assert(material_live_bound <= 16384);
auto curve_proof(const detail::MaterialCurveRecord& c,
                 const detail::MaterialMeshRecord& mesh,
                 std::span<Segment> segments, std::uint64_t& checked,
                 std::uint64_t maximum) -> std::expected<void, std::string> {
  std::size_t segment_index = 0;
  if (c.source >= source_rows || c.point_count < 2 || c.point_count > 6 ||
      !permutation(c.corrected_world) || !std::isfinite(c.radius_metres) ||
      c.radius_metres <= 0)
    return std::unexpected("Material POLY constructor unsupported");
  if (mesh.source != c.source ||
      mesh.raw_vertices.size() != c.point_count * 10 ||
      mesh.quantized_vertices.size() != mesh.raw_vertices.size() ||
      mesh.triangles.size() != (c.point_count - 1) * 20)
    return std::unexpected("Material POLY ring topology");
  std::array<Bounds, 6> points{};
  for (std::size_t i = 0; i < c.point_count; ++i)
    points[i] = transform(point(c.local_points[i]), c.corrected_world);
  const auto begin = std::size_t{0};
  for (std::size_t i = 0; i + 1 < c.point_count; ++i) {
    if (segment_index >= segments.size())
      return std::unexpected("Material segment capacity");
    const auto rr = add(scalar(c.radius_metres), scalar(source_allowance));
    if (!rr.supported)
      return std::unexpected("Material source radius unsupported");
    segments[segment_index++] = {c.source, points[i], points[i + 1], rr.upper};
  }
  for (const auto& face : mesh.triangles) {
    const auto lo = *std::min_element(face.vertices.begin(),
                                      face.vertices.end()) /
                    10,
               hi = *std::max_element(face.vertices.begin(),
                                      face.vertices.end()) /
                    10;
    if (hi != lo + 1 || hi >= c.point_count)
      return std::unexpected("Material face is not adjacent POLY ring strip");
    const auto& segment = segments[begin + lo];
    for (const auto index : face.vertices) {
      if (index >= mesh.raw_vertices.size() ||
          index >= mesh.quantized_vertices.size())
        return std::unexpected("Material POLY face buffer");
      for (unsigned representation = 0; representation < 2; ++representation) {
        if (checked >= maximum)
          return std::unexpected("Material ring inclusion capacity");
        ++checked;
        const auto p = representation == 0
                           ? mesh.raw_vertices[index]
                           : qpoint(mesh.quantized_vertices[index]);
        if (!contained_capsule(point(p), segment.first, segment.second,
                               segment.radius))
          return std::unexpected(
              "Material actual ring vertex outside frozen capsule");
      }
    }
  }
  return {};
}
auto support_proof(const detail::MaterialSupportRecord& c,
                   const detail::MaterialMeshRecord& mesh, Support& support,
                   std::uint64_t& checked, std::uint64_t maximum)
    -> std::expected<void, std::string> {
  if (c.source >= source_rows || !permutation(c.corrected_world) ||
      !std::isfinite(c.bevel_width) || c.bevel_width <= 0)
    return std::unexpected("Material primitive constructor unsupported");
  if (mesh.source != c.source || mesh.raw_vertices.size() != 96 ||
      mesh.quantized_vertices.size() != mesh.raw_vertices.size() ||
      mesh.triangles.size() != 188)
    return std::unexpected("Material primitive packet counts");
  support.source = c.source;
  std::array<Bounds, 8> points{};
  for (std::size_t i = 0; i < 8; ++i)
    points[i] = transform(point(c.local_vertices[i]), c.corrected_world);
  Vec center{};
  for (const auto p : points) {
    const auto mid = midpoint(p);
    center.x += mid.x / 8;
    center.y += mid.y / 8;
    center.z += mid.z / 8;
  }
  for (std::size_t f = 0; f < 6; ++f) {
    const auto& ids = c.polygons[f];
    for (auto index : ids)
      if (index >= 8) return std::unexpected("Material primitive index");
    const auto a = midpoint(points[ids[0]]);
    auto n = cross(minus(midpoint(points[ids[1]]), a),
                   minus(midpoint(points[ids[2]]), a));
    const auto orientation = dot(point(minus(a, center)), n);
    if (!orientation.supported || orientation.lower <= 0)
      return std::unexpected("Material primitive outward face unresolved");
    n = bounded_direction(n);
    if (n == Vec{})
      return std::unexpected("Material primitive degenerate face");
    double plane_support_bound = -std::numeric_limits<double>::infinity();
    for (const auto p : points) {
      auto s = dot(p, n);
      if (!s.supported)
        return std::unexpected("Material primitive support unsupported");
      plane_support_bound = std::max(plane_support_bound, s.upper);
    }
    const auto allowance =
        mul(scalar(source_allowance),
            scalar(std::abs(n.x) + std::abs(n.y) + std::abs(n.z)));
    support.planes[f] = {n, add(scalar(plane_support_bound), allowance)};
    for (std::size_t v = 0; v < mesh.raw_vertices.size(); ++v)
      for (unsigned representation = 0; representation < 2; ++representation) {
        if (checked >= maximum)
          return std::unexpected("Material support inclusion capacity");
        ++checked;
        auto p = representation == 0 ? mesh.raw_vertices[v]
                                     : qpoint(mesh.quantized_vertices[v]);
        auto s = dot(point(p), n);
        if (!s.supported || s.upper > support.planes[f].maximum.lower)
          return std::unexpected(
              "Material evaluated primitive outside frozen prism");
      }
  }
  return {};
}
auto constructor_proofs(Data& d, const Limits& limits)
    -> std::expected<void, std::string> {
  std::size_t segment_index = 0, support_index = 0;
  for (const auto& c : d.prepared.curves) {
    if (c.source >= d.prepared.sources.size() ||
        c.mesh >= d.prepared.meshes.size() || c.point_count < 2 ||
        c.point_count > 6 ||
        segment_index + c.point_count - 1 > d.segments.size())
      return std::unexpected("Material curve packet capacity");
    auto proof = curve_proof(
        c, d.prepared.meshes[c.mesh],
        std::span{d.segments}.subspan(segment_index, c.point_count - 1),
        d.summary.ring_inclusions, limits.ring_inclusions);
    if (!proof)
      return std::unexpected(std::string(d.prepared.sources[c.source].name) +
                             ": " + proof.error());
    segment_index += c.point_count - 1;
  }
  for (const auto& c : d.prepared.supports) {
    if (c.source >= d.prepared.sources.size() ||
        c.mesh >= d.prepared.meshes.size() ||
        support_index >= d.supports.size())
      return std::unexpected("Material support packet capacity");
    auto proof = support_proof(
        c, d.prepared.meshes[c.mesh], d.supports[support_index++],
        d.summary.support_plane_inclusions, limits.support_plane_inclusions);
    if (!proof)
      return std::unexpected(std::string(d.prepared.sources[c.source].name) +
                             ": " + proof.error());
  }
  if (segment_index != 14 || support_index != 2 ||
      d.summary.ring_inclusions != 1680 ||
      d.summary.support_plane_inclusions != 2304)
    return std::unexpected("Material constructor completeness");
  d.summary.constructors_complete = true;
  return {};
}
auto matching_source(const detail::MaterialSourceRecord& source,
                     std::size_t index) -> bool {
  const auto p = detail::origin_boarding_initial_material_prepared();
  if (index >= p.sources.size()) return false;
  const auto& expected = p.sources[index];
  return source.name == expected.name &&
         source.source_type == expected.source_type &&
         source.parent == expected.parent &&
         source.raw_fingerprint == expected.raw_fingerprint &&
         source.quantized_fingerprint == expected.quantized_fingerprint &&
         source.raw_bounds.lower == expected.raw_bounds.lower &&
         source.raw_bounds.upper == expected.raw_bounds.upper &&
         source.quantized_bounds.lower == expected.quantized_bounds.lower &&
         source.quantized_bounds.upper == expected.quantized_bounds.upper &&
         source.original_object == expected.original_object &&
         source.original_group == expected.original_group &&
         source.motion_group == expected.motion_group &&
         source.mesh == expected.mesh &&
         source.original_start == expected.original_start &&
         source.original_count == expected.original_count &&
         source.relation == expected.relation &&
         source.removed == expected.removed &&
         source.absent_crop == expected.absent_crop &&
         source.replacement == expected.replacement;
}
auto authenticate(Data& d, const Limits& limits)
    -> std::expected<void, std::string> {
  const auto& p = d.prepared;
  const auto* contact = d.binding.contact();
  const auto* pose = d.binding.pose();
  if (!environment())
    return std::unexpected("Material arithmetic environment unsupported");
  if (!contact || !pose || !d.binding.selection() ||
      d.binding.selection()->hardware != OperatingProgress{1, 1, 1, 0} ||
      !contact->original_geometry() || !contact->stowed_partition())
    return std::unexpected("Material genuine selected binding required");
  if (p.completion_sha256 != completion_pin || p.binary_sha256 != binary_pin ||
      p.sources.size() != 1759 || p.meshes.size() != 19 ||
      p.curves.size() != 3 || p.supports.size() != 2 || p.moving.size() != 165)
    return std::unexpected("Material pinned source roster required");
  if (p.storage_bytes > limits.source_bytes ||
      sizeof(Data) + 64 > limits.source_bytes - p.storage_bytes)
    return std::unexpected("Material source storage capacity");
  d.summary.source_bytes = p.storage_bytes + sizeof(Data) + 64;
  auto removed = contact->stowed_partition()->removed();
  auto replacements = contact->replacement_objects();
  if (removed.size() != 8 || replacements.size() != 13)
    return std::unexpected("Material owned replacement partition");
  std::array<bool, 165> moving_seen{};
  std::array<bool, 13> group_seen{};
  std::array<OperatingTransform, 13> authoring_groups{};
  std::size_t original = 0, fixed = 0, moving = 0, removals = 0, replace = 0,
              absent = 0;
  for (std::size_t i = 0; i < p.sources.size(); ++i) {
    const auto& r = p.sources[i];
    if (r.name.empty() || r.raw_fingerprint.size() != 64 ||
        r.quantized_fingerprint.size() != 64 || !valid(r.raw_bounds) ||
        !valid(r.quantized_bounds) || r.motion_group < -1 ||
        r.motion_group >= 13)
      return std::unexpected("Material source identity or finite bounds");
    if (i < 1746) {
      ++original;
      if (r.replacement) return std::unexpected("Material original namespace");
      absent += r.absent_crop;
      if (r.motion_group < 0)
        ++fixed;
      else
        ++moving;
    } else {
      ++replace;
      if (!r.replacement || r.motion_group != 10 || r.removed)
        return std::unexpected("Material replacement namespace");
      const auto it = std::find_if(
          replacements.begin(), replacements.end(),
          [&](const auto& row) { return row.source_object == r.name; });
      if (it == replacements.end() || r.mesh < 0 ||
          static_cast<std::size_t>(r.mesh) >= p.meshes.size() ||
          it->triangle_count !=
              p.meshes[static_cast<std::size_t>(r.mesh)].triangles.size())
        return std::unexpected("Material replacement source identity");
    }
    if (r.removed) {
      ++removals;
      const auto it =
          std::find_if(removed.begin(), removed.end(), [&](const auto& row) {
            return row.source_object == r.name;
          });
      if (it == removed.end() ||
          r.original_object != static_cast<std::int32_t>(it->original_object) ||
          r.original_group != static_cast<std::int32_t>(it->group) ||
          r.original_start != it->triangles.start ||
          r.original_count != it->triangles.count)
        return std::unexpected("Material exact removal range");
    }
    auto envelope = union_bounds(r.raw_bounds, r.quantized_bounds);
    if (r.motion_group >= 0) {
      envelope = transform(
          envelope,
          pose->craft_world_deltas[static_cast<std::size_t>(r.motion_group)]);
      if (i < 1746) {
        auto it =
            std::find_if(p.moving.begin(), p.moving.end(),
                         [&](const auto& row) { return row.source == i; });
        if (it == p.moving.end() || !valid(it->raw_bounds) ||
            !valid(it->quantized_bounds))
          return std::unexpected(
              "Material missing moving construction cross-check");
        const auto j = static_cast<std::size_t>(it - p.moving.begin());
        if (moving_seen[j])
          return std::unexpected("Material duplicate moving summary");
        moving_seen[j] = true;
        for (const auto column : it->authoring_delta.columns)
          if (!valid(point(column)))
            return std::unexpected(
                "Material finite authoring group delta required");
        const auto group = static_cast<std::size_t>(r.motion_group);
        if (group_seen[group] && authoring_groups[group] != it->authoring_delta)
          return std::unexpected("Material captured same-group delta mismatch");
        group_seen[group] = true;
        authoring_groups[group] = it->authoring_delta;
        const auto captured =
            union_bounds(it->raw_bounds, it->quantized_bounds);
        for (std::size_t a = 0; a < 3; ++a)
          if (component(envelope.upper, a) < component(captured.lower, a) ||
              component(captured.upper, a) < component(envelope.lower, a))
            return std::unexpected(
                "Material current source/owned envelope cross-check disjoint");
        // Conservative selected-pose source enclosure. The owned C++ transform
        // remains authoritative; this union and overlap test prove no
        // NumPy/libm byte equivalence and introduce no tolerance or actor
        // permission.
        envelope = union_bounds(envelope, captured);
      }
    }
    if (!valid(envelope))
      return std::unexpected("Material current world envelope unsupported");
    d.envelopes[i] = envelope;
  }
  if (original != 1746 || fixed != 1581 || moving != 165 || removals != 8 ||
      replace != 13 || absent != 831 ||
      !std::ranges::all_of(moving_seen, [](bool x) { return x; }))
    return std::unexpected("Material complete effective source binding");
  for (const auto& mesh : p.meshes) {
    if (mesh.source >= 1759 ||
        mesh.raw_vertices.size() != mesh.quantized_vertices.size() ||
        mesh.raw_vertices.empty())
      return std::unexpected("Material geometry packet dimensions");
    for (std::size_t i = 0; i < mesh.raw_vertices.size(); ++i)
      if (!source_vertex_valid(mesh.raw_vertices[i],
                               mesh.quantized_vertices[i]))
        return std::unexpected("Material raw/game vertex mapping");
    for (const auto& face : mesh.triangles)
      for (auto i : face.vertices)
        if (i >= mesh.raw_vertices.size())
          return std::unexpected("Material source triangle index");
  }
  d.summary.original_objects = original;
  d.summary.fixed_objects = fixed;
  d.summary.moving_objects = moving;
  d.summary.removed_objects = removals;
  d.summary.replacement_objects = replace;
  d.summary.absent_crop_objects = absent;
  d.summary.effective_objects = original - removals + replace;
  d.summary.bindings_complete = true;
  return constructor_proofs(d, limits);
}
} // namespace
OriginBoardingInitialMaterial::OriginBoardingInitialMaterial(
    std::shared_ptr<const Data> data)
    : data_(std::move(data)) {
}
auto OriginBoardingInitialMaterial::summary() const
    -> const BoardingInitialMaterialSourceSummary* {
  return data_ ? &data_->summary : nullptr;
}
auto OriginBoardingInitialMaterial::source_name(std::size_t index) const
    -> std::string_view {
  return data_ && index < data_->prepared.sources.size()
             ? data_->prepared.sources[index].name
             : std::string_view{};
}
namespace detail {
auto BoardingInitialMaterialAccess::data(
    const OriginBoardingInitialMaterial& source) -> const Data* {
  return source.data_.get();
}
auto initial_material_source_envelope(
    const OriginBoardingInitialMaterial& source, std::size_t index)
    -> std::optional<Bounds> {
  const auto* data = BoardingInitialMaterialAccess::data(source);
  if (!data || index >= data->prepared.sources.size() ||
      data->prepared.sources[index].removed)
    return std::nullopt;
  return data->envelopes[index];
}
auto BoardingInitialMaterialAccess::make(const NativeCraftBinding& binding,
                                         Limits limits)
    -> std::expected<OriginBoardingInitialMaterial, std::string> {
  if (!valid_limits(limits))
    return std::unexpected("Material lowered limits required");
  const auto prepared = origin_boarding_initial_material_prepared();
  if (prepared.storage_bytes > limits.source_bytes ||
      sizeof(Data) + 64 > limits.source_bytes - prepared.storage_bytes)
    return std::unexpected("Material source storage capacity");
  auto data = std::make_shared<Data>();
  data->binding = binding;
  data->prepared = prepared;
  auto proof = authenticate(*data, limits);
  if (!proof) return std::unexpected(proof.error());
  return OriginBoardingInitialMaterial{std::move(data)};
}
auto initial_material_curve_constructor_math(const MaterialSourceRecord& source,
                                             const MaterialCurveRecord& curve,
                                             const MaterialMeshRecord& mesh,
                                             std::uint64_t maximum)
    -> std::expected<MaterialConstructorMathEvidence, std::string> {
  if (maximum > 1680)
    return std::unexpected("Material lowered ring capacity required");
  const auto p = origin_boarding_initial_material_prepared();
  const auto it =
      std::find_if(p.curves.begin(), p.curves.end(),
                   [&](const auto& c) { return c.source == curve.source; });
  if (it == p.curves.end() || !matching_source(source, curve.source) ||
      curve.mesh != it->mesh || curve.point_count != it->point_count ||
      curve.local_points != it->local_points ||
      curve.corrected_world != it->corrected_world ||
      curve.radius_metres != it->radius_metres || mesh.source != curve.source)
    return std::unexpected("Material pinned POLY constructor identity");
  MaterialConstructorMathEvidence out;
  out.arithmetic_supported = environment();
  if (!out.arithmetic_supported) return out;
  std::array<Segment, 5> segments{};
  auto proof =
      curve_proof(curve, mesh, segments, out.inclusion_predicates, maximum);
  if (!proof) return std::unexpected(proof.error());
  out.certified = true;
  return out;
}
auto initial_material_support_constructor_math(
    const MaterialSourceRecord& source, const MaterialSupportRecord& support,
    const MaterialMeshRecord& mesh, std::uint64_t maximum)
    -> std::expected<MaterialConstructorMathEvidence, std::string> {
  if (maximum > 2304)
    return std::unexpected("Material lowered support capacity required");
  const auto p = origin_boarding_initial_material_prepared();
  const auto it =
      std::find_if(p.supports.begin(), p.supports.end(),
                   [&](const auto& c) { return c.source == support.source; });
  if (it == p.supports.end() || !matching_source(source, support.source) ||
      support.mesh != it->mesh ||
      support.local_vertices != it->local_vertices ||
      support.polygons != it->polygons ||
      support.corrected_world != it->corrected_world ||
      support.bevel_width != it->bevel_width || mesh.source != support.source)
    return std::unexpected("Material pinned primitive constructor identity");
  MaterialConstructorMathEvidence out;
  out.arithmetic_supported = environment();
  if (!out.arithmetic_supported) return out;
  Support prepared{};
  auto proof =
      support_proof(support, mesh, prepared, out.inclusion_predicates, maximum);
  if (!proof) return std::unexpected(proof.error());
  out.certified = true;
  return out;
}
auto initial_material_envelope_math(const Solid& body, double common,
                                    Bounds bounds)
    -> std::expected<Evidence, std::string> {
  if (!valid(bounds))
    return std::unexpected("Material finite ordered envelope required");
  Evidence out;
  auto b = world_bounds(body, common);
  if (!b) return std::unexpected(b.error());
  out.arithmetic_supported = true;
  for (std::size_t a = 0; a < 3; ++a)
    if (component(b->upper, a) < component(bounds.lower, a) ||
        component(bounds.upper, a) < component(b->lower, a)) {
      out.certified = true;
      break;
    }
  return out;
}
auto initial_material_capsule_math(const Solid& body, double common,
                                   Bounds first, Bounds last, double radius,
                                   std::size_t max_axes)
    -> std::expected<Evidence, std::string> {
  if (!valid(first) || !valid(last) || !std::isfinite(radius) || radius <= 0 ||
      radius > 1 || max_axes > 16)
    return std::unexpected("Material finite capsule/axis capacity required");
  Evidence out;
  out.arithmetic_supported = environment();
  if (!out.arithmetic_supported) return out;
  const auto a = midpoint(first), b = midpoint(last),
             center = midpoint(union_bounds(body.first, body.second));
  Vec world_center = center;
  world_center.y += common;
  const auto d = minus(b, a);
  std::array<Vec, 16> directions{
      {{1, 0, 0},
       {0, 1, 0},
       {0, 0, 1},
       d,
       minus(world_center, a),
       minus(world_center, b),
       minus(world_center, midpoint(union_bounds(first, last))),
       cross(d, {1, 0, 0}),
       cross(d, {0, 1, 0}),
       cross(d, {0, 0, 1})}};
  const auto bd = minus(midpoint(body.second), midpoint(body.first));
  directions[10] = cross(d, bd);
  directions[11] = cross(bd, {1, 0, 0});
  directions[12] = cross(bd, {0, 1, 0});
  directions[13] = cross(bd, {0, 0, 1});
  directions[14] = cross(d, cross(d, minus(world_center, a)));
  directions[15] = cross(bd, cross(bd, minus(world_center, a)));
  for (std::size_t i = 0; i < max_axes; ++i) {
    ++out.axes_examined;
    auto n = bounded_direction(directions[i]);
    if (n == Vec{}) continue;
    auto s = projection(body, common, n);
    auto t = support_source(first, last, radius, n);
    if (!s || !s->supported || !t.supported) {
      out.arithmetic_supported = false;
      return out;
    }
    if (s->upper < t.lower || t.upper < s->lower) {
      out.certified = true;
      return out;
    }
  }
  return out;
}
auto initial_material_primitive_math(const Solid& body, double common,
                                     std::span<const MaterialPlane> planes,
                                     std::size_t max_axes)
    -> std::expected<Evidence, std::string> {
  if (planes.empty() || planes.size() > 6 || max_axes > 16)
    return std::unexpected("Material bounded finite primitive planes required");
  Evidence out;
  out.arithmetic_supported = environment();
  if (!out.arithmetic_supported) return out;
  for (std::size_t i = 0; i < std::min(planes.size(), max_axes); ++i) {
    const auto& p = planes[i];
    if (!p.maximum.supported || !std::isfinite(p.maximum.lower) ||
        !std::isfinite(p.maximum.upper) || p.maximum.lower > p.maximum.upper ||
        !valid(point(p.normal)) || p.normal == Vec{})
      return std::unexpected("Material finite supported plane required");
    ++out.axes_examined;
    const Vec negative_direction{-p.normal.x, -p.normal.y, -p.normal.z};
    auto b = boarding_source_endpoint_surface_checkpoint_support_math(
        body, common, negative_direction);
    if (!b || !b->supported) {
      out.arithmetic_supported = false;
      return out;
    }
    if (-b->upper > p.maximum.upper) {
      out.certified = true;
      return out;
    }
  }
  return out;
}
} // namespace detail
namespace {
using Diagnostic = BoardingInitialMaterialDiagnostic;
using Payload = BoardingInitialMaterialPayload;
[[gnu::noinline]] auto initial_result(
    const OriginBoardingBootSupport& boot,
    const OriginBoardingInitialMaterial& source)
    -> std::expected<Diagnostic, std::string> {
  auto child = assess_origin_boarding_source_endpoint_load(boot);
  if (!child) return std::unexpected(child.error());
  return Diagnostic{std::move(*child), source, {}};
}
auto refuse(Payload& out, Condition c, std::optional<std::size_t> part = {},
            std::optional<std::size_t> source = {}, std::string_view name = {},
            std::optional<std::uint32_t> triangle = {}) -> void {
  out.first_refusal =
      BoardingInitialMaterialRefusal{c, part, source, triangle, name};
}
auto prepare_body(Diagnostic& out, std::array<Solid, 15>& solids) -> bool {
  const auto& e = out.initial.self.endpoint;
  if (!out.initial.load.complete || !out.initial.self.complete || !e.complete ||
      !e.body || e.common_translation_y_metres != .847)
    return false;
  for (std::size_t i = 0; i < 15; ++i) {
    const auto& p = e.parts[i];
    auto& r = out.material.parts[i];
    r.id = p.id;
    if (static_cast<std::size_t>(p.id) != i) return false;
    const auto first = static_cast<std::size_t>(p.mass.first),
               last = static_cast<std::size_t>(p.mass.second);
    if (first >= 18 || last >= 18) return false;
    if (const auto* box =
            std::get_if<BoardingPlantedBodyBoxBinding>(&p.reservation)) {
      const auto id = static_cast<std::size_t>(box->center);
      if (id >= 18 || box->frame.columns != BoardingBodyFrame{}.columns ||
          first != id || last != id)
        return false;
      solids[i] = {e.body->points[id], e.body->points[id],
                   box->half_size_metres, 0, Shape::box};
      r.original_witness_identity =
          e.body->mass_points[i].lower == e.body->points[id].lower &&
          e.body->mass_points[i].upper == e.body->points[id].upper;
    } else {
      const auto& c =
          std::get<BoardingPlantedBodyCapsuleBinding>(p.reservation);
      const auto a = static_cast<std::size_t>(c.start),
                 b = static_cast<std::size_t>(c.end);
      if (a >= 18 || b >= 18 ||
          !((first == a && last == b) || (first == b && last == a)))
        return false;
      solids[i] = {e.body->points[a],
                   e.body->points[b],
                   {},
                   c.radius_metres,
                   Shape::capsule};
      r.original_witness_identity = true;
    }
    if (!r.original_witness_identity) return false;
    r.witness = e.body->mass_points[i];
    const auto low = add(scalar(r.witness.lower.y),
                         scalar(e.common_translation_y_metres)),
               high = add(scalar(r.witness.upper.y),
                          scalar(e.common_translation_y_metres));
    r.witness.lower.y = low.lower;
    r.witness.upper.y = high.upper;
    auto bounds = world_bounds(solids[i], e.common_translation_y_metres);
    if (!bounds) return false;
    r.full_reservation = *bounds;
    if (i == 5 || i == 11) {
      const auto side = i == 5 ? 0U : 1U;
      const auto& leg = e.legs[side];
      if (!leg.plane_identity ||
          leg.boot_center_y_terms != std::array{leg.plane_metres, .05, -.847} ||
          solids[i].half_size_metres.y != .05)
        return false;
      r.full_reservation.lower.y = leg.plane_metres;
    }
  }
  return true;
}
auto sole_source_clear(const Diagnostic& out, const Data& data,
                       std::size_t part, std::size_t source) -> bool {
  if (part != 5 && part != 11) return false;
  const auto& e = out.initial.self.endpoint;
  const auto side = part == 5 ? 0U : 1U;
  const auto& site = e.sites.sites[side];
  const auto& r = data.prepared.sources[source];
  if (!site.source_partition || !site.eligible ||
      !e.legs[side].plane_identity || r.mesh < 0)
    return false;
  const auto& pressure = out.initial.load.pressures[side];
  if (!pressure.complete || pressure.source_partition != site.source_partition)
    return false;
  // The existing owned finite admitted face selects this exact original object.
  const auto key = pressure.source_keys[0];
  if (key.buffer != LowerCockpitContactBuffer::original ||
      key.group != static_cast<std::uint32_t>(r.original_group) ||
      key.triangle < r.original_start ||
      key.triangle - r.original_start >= r.original_count)
    return false;
  const auto& mesh = data.prepared.meshes[static_cast<std::size_t>(r.mesh)];
  const auto plane = e.legs[side].plane_metres;
  return std::ranges::all_of(mesh.quantized_vertices, [&](const auto& q) {
    return qpoint(q).y <= plane;
  });
}
} // namespace
namespace detail {
auto boarding_initial_material_bounded(
    const OriginBoardingBootSupport& boot,
    const OriginBoardingInitialMaterial& source, Limits limits)
    -> std::expected<Diagnostic, std::string> {
  if (!valid_limits(limits))
    return std::unexpected("Material lowered bounded limits required");
  const auto* data = BoardingInitialMaterialAccess::data(source);
  if (!data) return std::unexpected("Material moved-from source handle");
  auto result = initial_result(boot, source);
  if (!result) return result;
  auto& out = result->material;
  out.source = data->summary;
  out.arithmetic_supported = environment();
  if (!out.arithmetic_supported) {
    refuse(out, Condition::unsupported_arithmetic);
    return result;
  }
  const auto* contact = result->initial.self.endpoint.sites.source.contact();
  if (!contact ||
      contact->stowed_partition() !=
          data->binding.contact()->stowed_partition() ||
      contact->original_geometry() !=
          data->binding.contact()->original_geometry()) {
    refuse(out, Condition::invalid_binding);
    return result;
  }
  if (data->summary.source_bytes > limits.source_bytes) {
    refuse(out, Condition::source_capacity);
    return result;
  }
  if (data->summary.ring_inclusions > limits.ring_inclusions ||
      data->summary.support_plane_inclusions >
          limits.support_plane_inclusions) {
    refuse(out, Condition::constructor_capacity);
    return result;
  }
  std::array<Solid, 15> solids{};
  if (!prepare_body(*result, solids)) {
    refuse(out, Condition::child_prerequisite);
    return result;
  }
  out.bindings_complete = true;
  out.source_complete =
      data->summary.bindings_complete && data->summary.constructors_complete;
  // Visit each compiled full-source triangle once, then its fifteen
  // reservations. Non-sheet relations are finite enclosers; an unknown overlap
  // always refuses.
  for (std::size_t source_index = 0;
       source_index < data->prepared.sources.size(); ++source_index) {
    const auto& r = data->prepared.sources[source_index];
    if (r.removed) continue;
    std::array<bool, 15> pending{};
    for (std::size_t p = 0; p < 15; ++p) {
      if (out.envelope_pairs >= limits.envelope_pairs) {
        refuse(out, Condition::envelope_capacity, p, source_index, r.name);
        return result;
      }
      ++out.envelope_pairs;
      auto& record = out.parts[p];
      ++record.envelopes_examined;
      const auto& body_bounds = record.full_reservation;
      const auto& source_bounds = data->envelopes[source_index];
      bool separated = false;
      for (std::size_t a = 0; a < 3; ++a)
        separated |=
            component(body_bounds.upper, a) <
                component(source_bounds.lower, a) ||
            component(source_bounds.upper, a) < component(body_bounds.lower, a);
      if (separated) {
        ++record.envelope_exclusions;
        continue;
      }
      pending[p] = true;
    }
    if (!std::ranges::any_of(pending, [](bool p) { return p; })) continue;
    if (r.relation == Relation::unknown || r.relation == Relation::stowed) {
      const auto p = static_cast<std::size_t>(
          std::find(pending.begin(), pending.end(), true) - pending.begin());
      refuse(out, Condition::missing_relation, p, source_index, r.name);
      return result;
    }
    if (r.relation == Relation::service_enclosure ||
        r.relation == Relation::support_enclosure) {
      for (std::size_t p = 0; p < 15; ++p) {
        if (!pending[p]) continue;
        if (r.relation == Relation::support_enclosure &&
            sole_source_clear(*result, *data, p, source_index)) {
          ++out.parts[p].relation_exclusions;
          continue;
        }
        bool clear = true;
        std::size_t relations = 0;
        const auto assess = [&](const auto& eval) -> bool {
          if (out.enclosure_pairs >= limits.enclosure_pairs) {
            refuse(out, Condition::pair_capacity, p, source_index, r.name);
            return false;
          }
          ++out.enclosure_pairs;
          const auto remaining = limits.axes - out.axes_examined;
          const auto allowance = static_cast<std::size_t>(
              std::min<std::uint64_t>(remaining, limits.pair_axes));
          auto evidence = eval(allowance);
          if (!evidence || !evidence->arithmetic_supported ||
              evidence->axes_examined > allowance) {
            out.arithmetic_supported = false;
            refuse(out, Condition::unsupported_arithmetic, p, source_index,
                   r.name);
            return false;
          }
          out.axes_examined += evidence->axes_examined;
          if (!evidence->certified) {
            refuse(out,
                   allowance < 16 ? Condition::axis_capacity
                                  : Condition::enclosure_unresolved,
                   p, source_index, r.name);
            return false;
          }
          return true;
        };
        if (r.relation == Relation::service_enclosure) {
          for (const auto& s : data->segments)
            if (s.source == source_index) {
              ++relations;
              if (!assess([&](std::size_t cap) {
                    return initial_material_capsule_math(
                        solids[p], .847, s.first, s.second, s.radius, cap);
                  })) {
                clear = false;
                break;
              }
            }
        } else {
          for (const auto& s : data->supports)
            if (s.source == source_index) {
              ++relations;
              clear = assess([&](std::size_t cap) {
                return initial_material_primitive_math(solids[p], .847,
                                                       s.planes, cap);
              });
              break;
            }
        }
        if (!clear) return result;
        if (relations == 0) {
          refuse(out, Condition::constructor_relation, p, source_index, r.name);
          return result;
        }
        ++out.parts[p].relation_exclusions;
      }
      continue;
    }
    if (r.relation != Relation::shell_sheet || r.mesh < 0 ||
        static_cast<std::size_t>(r.mesh) >= data->prepared.meshes.size()) {
      refuse(out, Condition::missing_relation, {}, source_index, r.name);
      return result;
    }
    const auto& mesh = data->prepared.meshes[static_cast<std::size_t>(r.mesh)];
    for (std::size_t ordinal = 0; ordinal < mesh.triangles.size(); ++ordinal) {
      if (out.triangle_visits >= limits.triangle_visits) {
        refuse(out, Condition::triangle_capacity, {}, source_index, r.name,
               static_cast<std::uint32_t>(ordinal));
        return result;
      }
      ++out.triangle_visits;
      const auto& ids = mesh.triangles[ordinal].vertices;
      const auto& qa = mesh.quantized_vertices[ids[0]].value;
      const auto& qb = mesh.quantized_vertices[ids[1]].value;
      const auto& qc = mesh.quantized_vertices[ids[2]].value;
      if (qa == qb || qb == qc || qc == qa) {
        ++out.collapsed_triangles;
        continue;
      }
      const std::array triangle{qpoint(mesh.quantized_vertices[ids[0]]),
                                qpoint(mesh.quantized_vertices[ids[1]]),
                                qpoint(mesh.quantized_vertices[ids[2]])};
      for (std::size_t p = 0; p < 15; ++p) {
        if (!pending[p]) continue;
        if (out.boundary_pairs >= limits.boundary_pairs) {
          refuse(out, Condition::pair_capacity, p, source_index, r.name,
                 static_cast<std::uint32_t>(ordinal));
          return result;
        }
        ++out.boundary_pairs;
        const auto allowance = static_cast<std::size_t>(std::min<std::uint64_t>(
            limits.axes - out.axes_examined, limits.pair_axes));
        auto proof = boarding_source_endpoint_surface_checkpoint_pair_math(
            solids[p], .847, triangle, allowance);
        if (!proof || !proof->arithmetic_supported ||
            proof->axes_examined > allowance) {
          out.arithmetic_supported = false;
          refuse(out, Condition::unsupported_arithmetic, p, source_index,
                 r.name, static_cast<std::uint32_t>(ordinal));
          return result;
        }
        out.axes_examined += proof->axes_examined;
        if (!proof->certified) {
          refuse(out,
                 proof->truncated ? Condition::axis_capacity
                                  : Condition::sheet_unresolved,
                 p, source_index, r.name, static_cast<std::uint32_t>(ordinal));
          return result;
        }
      }
    }
    for (std::size_t p = 0; p < 15; ++p)
      if (pending[p]) ++out.parts[p].relation_exclusions;
  }
  for (auto& p : out.parts) {
    p.complete = p.original_witness_identity && p.envelopes_examined == 1751 &&
                 p.envelope_exclusions + p.relation_exclusions == 1751;
    if (!p.complete) {
      refuse(out, Condition::source_capacity);
      return result;
    }
  }
  out.initial_material_exclusion =
      out.bindings_complete && out.source_complete && out.arithmetic_supported;
  return result;
}
} // namespace detail
auto make_origin_boarding_initial_material(const NativeCraftBinding& binding)
    -> std::expected<OriginBoardingInitialMaterial, std::string> {
  return detail::BoardingInitialMaterialAccess::make(binding, {});
}
auto assess_origin_boarding_initial_material(
    const OriginBoardingBootSupport& boot,
    const OriginBoardingInitialMaterial& source)
    -> std::expected<BoardingInitialMaterialDiagnostic, std::string> {
  return detail::boarding_initial_material_bounded(boot, source, {});
}
} // namespace apsis_drift
