#include "apsis_drift/origin_boarding_checkpoint_material_extension.hpp"
#include "origin_boarding_checkpoint_material_extension_internal.hpp"
#include "origin_boarding_checkpoint_material_extension_prepared.hpp"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <limits>
#include <utility>
namespace apsis_drift {
namespace {
using Vec = RigidVector3;
using Scalar = BoardingFootSiteScalarBounds;
using Plane = detail::MaterialPlane;
using View = detail::CheckpointMaterialExtensionPreparedView;
using Frame = detail::CheckpointMaterialExtensionFrameRecord;
using Limits = detail::BoardingCheckpointMaterialExtensionLimits;
using Proof = detail::BoardingCheckpointMaterialExtensionConstructorMath;
using Prism = detail::BoardingCheckpointMaterialExtensionPrismMath;
using Why = detail::BoardingCheckpointMaterialExtensionCondition;
using Relation = detail::BoardingCheckpointMaterialExtensionRelation;
using BoundaryLimits = detail::BoardingCheckpointMaterialBoundaryLimits;
using BoundaryProof = detail::BoardingCheckpointMaterialBoundaryEvidence;
using BoundaryWhy = detail::BoardingCheckpointMaterialBoundaryCondition;
constexpr std::array<std::size_t, 2> source_ids{1436, 1574};
constexpr std::array<std::size_t, 2> vertex_counts{256, 3152},
    face_counts{512, 5187};
constexpr std::string_view
    master_pin =
        "87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677",
    inventory_pin =
        "a78800c6f5223323855aae1c37e1b40f7f83af76c709696b4eb0f08f3a9f55d8",
    history_pin =
        "1ff8c7de423eeddf100f5a36e1f494225263c342e745361075dbef88c7215db1";
constexpr std::array<std::string_view, 2> raw_pins{
    "0047c631d9b53d9f7d6d0955f99a546b9d2562f128748eecc12be818280ba9aa",
    "983a916affe2927c3fc9f7b96e6d2102cb26bdcac80d7c27568fc57714801c5f"};
constexpr std::array<std::string_view, 2> q_pins{
    "789f5d12d72f9b9185a40e33bdcdb888df0a15f89083de3b1ddcb86738a75507",
    "9731912e8929e9476f7a0aef3ae7badbd6c193489ac5493606fb542f6756d7ef"};
using Prisms = std::array<Prism, 8>;
auto component(Vec p, std::size_t a) -> double {
  return a == 0 ? p.x : a == 1 ? p.y : p.z;
}
auto environment() -> bool {
  if (!std::numeric_limits<double>::is_iec559 ||
      std::numeric_limits<double>::digits != 53 ||
      std::fegetround() != FE_TONEAREST)
    return false;
  volatile double normal = std::numeric_limits<double>::min(), half = .5,
                  subnormal = std::numeric_limits<double>::denorm_min(),
                  one = 1, tie = 0x1p-53, above = 0x1.8p-53;
  const double tiny = normal * half, input = subnormal * one, sum = one + tie,
               next = one + above;
  return std::bit_cast<std::uint64_t>(tiny) == 0x0008000000000000ULL &&
         std::bit_cast<std::uint64_t>(input) == 1 &&
         std::bit_cast<std::uint64_t>(sum) == 0x3ff0000000000000ULL &&
         std::bit_cast<std::uint64_t>(next) == 0x3ff0000000000001ULL;
}
auto scalar(double v) -> Scalar {
  return {v, v, std::isfinite(v)};
}
auto range(double lo, double hi) -> Scalar {
  return {lo, hi, std::isfinite(lo) && std::isfinite(hi) && lo <= hi};
}
auto add(Scalar a, Scalar b) -> Scalar {
  if (!a.supported || !b.supported) return {};
  if (a.lower == 0 && a.upper == 0) return b;
  if (b.lower == 0 && b.upper == 0) return a;
  return range(std::nextafter(a.lower + b.lower, -INFINITY),
               std::nextafter(a.upper + b.upper, INFINITY));
}
auto mul(Scalar a, Scalar b) -> Scalar {
  if (!a.supported || !b.supported) return {};
  if ((a.lower == 0 && a.upper == 0) || (b.lower == 0 && b.upper == 0))
    return scalar(0);
  if (a.lower == 1 && a.upper == 1) return b;
  if (b.lower == 1 && b.upper == 1) return a;
  const std::array v{a.lower * b.lower, a.lower * b.upper, a.upper * b.lower,
                     a.upper * b.upper};
  return range(std::nextafter(*std::min_element(v.begin(), v.end()), -INFINITY),
               std::nextafter(*std::max_element(v.begin(), v.end()), INFINITY));
}
auto dot(Vec p, Vec n) -> Scalar {
  return add(add(mul(scalar(p.x), scalar(n.x)), mul(scalar(p.y), scalar(n.y))),
             mul(scalar(p.z), scalar(n.z)));
}
auto minus(Vec a, Vec b) -> Vec {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto cross(Vec a, Vec b) -> Vec {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto qpoint(const detail::MaterialQuantizedPoint& p) -> Vec {
  return {static_cast<double>(p.value[0]) * 1e-6,
          static_cast<double>(p.value[1]) * 1e-6,
          static_cast<double>(p.value[2]) * 1e-6};
}
auto fail(Proof& p, Why why) -> bool {
  p.condition = why;
  p.complete = false;
  return false;
}
auto charge(std::uint64_t& work, std::uint64_t cap, Proof& p, Why why) -> bool {
  if (work >= cap) return fail(p, why);
  ++work;
  return true;
}
auto base_charge(Proof& p, const Limits& c) -> bool {
  return charge(p.work.base_guards, c.base_guards, p, Why::base_capacity);
}
auto valid_limits(const Limits& c) -> bool {
  const Limits top;
  return c.source_bytes <= top.source_bytes &&
         c.base_guards <= top.base_guards &&
         c.vertex_guards <= top.vertex_guards &&
         c.index_guards <= top.index_guards &&
         c.plane_guards <= top.plane_guards &&
         c.adjacent_pair_attempts <= top.adjacent_pair_attempts;
}
auto hash(std::string_view s) -> bool {
  return s.size() == 64 && std::ranges::all_of(s, [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}
auto same_bounds(BoardingPlantedLegPointBounds a,
                 BoardingPlantedLegPointBounds b) -> bool {
  return a.lower == b.lower && a.upper == b.upper;
}
auto same_source(const detail::MaterialSourceRecord& a,
                 const detail::MaterialSourceRecord& b) -> bool {
  return a.name == b.name && a.source_type == b.source_type &&
         a.parent == b.parent && a.raw_fingerprint == b.raw_fingerprint &&
         a.quantized_fingerprint == b.quantized_fingerprint &&
         same_bounds(a.raw_bounds, b.raw_bounds) &&
         same_bounds(a.quantized_bounds, b.quantized_bounds) &&
         a.original_object == b.original_object &&
         a.original_group == b.original_group &&
         a.motion_group == b.motion_group &&
         a.original_start == b.original_start &&
         a.original_count == b.original_count && a.mesh == b.mesh &&
         a.relation == b.relation && a.removed == b.removed &&
         a.absent_crop == b.absent_crop && a.replacement == b.replacement;
}
auto frame_identity(const Frame& f, Proof& proof, const Limits& caps) -> bool {
  const auto pinned =
      detail::origin_boarding_checkpoint_material_extension_prepared();
  if (!base_charge(proof, caps)) return false;
  if (!pinned.frame || f.source != 1436 || f.mesh != 0 ||
      f.bevel_width != static_cast<double>(static_cast<float>(.006)) ||
      f.bevel_segments != 3 || f.bevel_profile != .5 ||
      !std::isfinite(f.bevel_angle_limit) || f.bevel_angle_limit <= 0 ||
      !f.bevel_show_viewport || !f.bevel_show_render ||
      !f.normals_show_viewport || !f.normals_show_render ||
      f.modifiers_json.size() > 4096 || f.properties_json.size() > 4096)
    return fail(proof, Why::source_identity);
  const auto& expected = *pinned.frame;
  if (!base_charge(proof, caps)) return false;
  if (f.bevel_angle_limit != expected.bevel_angle_limit ||
      f.modifiers_json != expected.modifiers_json ||
      f.properties_json != expected.properties_json)
    return fail(proof, Why::source_identity);
  const OperatingTransform correction{
      {Vec{1, 0, 0}, Vec{0, 0, -1}, Vec{0, 1, 0}, Vec{}}};
  if (!base_charge(proof, caps)) return false;
  if (f.corrected_world != correction) return fail(proof, Why::base_geometry);
  constexpr std::array<std::array<double, 2>, 8> outer{{{-1.30, -.56},
                                                        {-1.30, 1.65},
                                                        {-1.13, 2.20},
                                                        {-.78, 2.58},
                                                        {.78, 2.58},
                                                        {1.13, 2.20},
                                                        {1.30, 1.65},
                                                        {1.30, -.56}}};
  constexpr std::array<std::array<double, 2>, 8> inner{{{-.62, -.23},
                                                        {-.62, 1.66},
                                                        {-.62, 1.95},
                                                        {-.40, 1.95},
                                                        {.40, 1.95},
                                                        {.62, 1.95},
                                                        {.62, 1.66},
                                                        {.62, -.23}}};
  for (std::size_t i = 0; i < 32; ++i) {
    const auto& profile = (i % 16) < 8 ? outer : inner;
    const Vec expected_point{
        static_cast<double>(static_cast<float>(profile[i % 8][0])),
        static_cast<double>(
            static_cast<float>(i < 16 ? 1.035 - .028 : 1.035 + .028)),
        static_cast<double>(static_cast<float>(profile[i % 8][1]))};
    for (std::size_t a = 0; a < 3; ++a) {
      if (!base_charge(proof, caps)) return false;
      if (component(f.local_vertices[i], a) != component(expected_point, a))
        return fail(proof, Why::base_geometry);
    }
  }
  for (std::size_t i = 0; i < 8; ++i) {
    const auto j = (i + 1) % 8;
    const std::array<std::array<std::size_t, 4>, 4> faces{
        {{i, j, 8 + j, 8 + i},
         {16 + i, 24 + i, 24 + j, 16 + j},
         {i, 16 + i, 16 + j, j},
         {8 + i, 8 + j, 24 + j, 24 + i}}};
    for (std::size_t k = 0; k < 4; ++k) {
      const auto& actual = f.polygons[4 * i + k];
      const auto& target = faces[k];
      std::optional<std::size_t> start;
      for (std::size_t n = 0; n < 4; ++n) {
        if (!base_charge(proof, caps)) return false;
        if (actual[0] == target[n]) start = n;
      }
      if (!start) return fail(proof, Why::base_geometry);
      bool forward = true, backward = true;
      for (std::size_t n = 1; n < 4; ++n) {
        if (!base_charge(proof, caps)) return false;
        forward &= actual[n] == target[(*start + n) % 4];
        backward &= actual[n] == target[(*start + 4 - n) % 4];
      }
      if (!forward && !backward) return fail(proof, Why::base_geometry);
    }
  }
  return true;
}
// The exact captured frame is a signed permutation. This transformation is
// exact; a changed arbitrary affine frame is refused, never rounded into it.
auto world(Vec p) -> Vec {
  return {p.x, p.z, -p.y};
}
[[gnu::noinline]] auto build_prism(const Frame& f, std::size_t sector,
                                   Proof& proof, const Limits& caps, Prism& out)
    -> bool {
  out = {};
  if (sector >= 8) return fail(proof, Why::base_geometry);
  const auto j = (sector + 1) % 8;
  const std::array ids{sector,      j,      8 + j,  8 + sector,
                       16 + sector, 16 + j, 24 + j, 24 + sector};
  std::array<Vec, 8> vertices{};
  Vec center{};
  for (std::size_t i = 0; i < 8; ++i) {
    vertices[i] = world(f.local_vertices[ids[i]]);
    center.x += vertices[i].x / 8;
    center.y += vertices[i].y / 8;
    center.z += vertices[i].z / 8;
  }
  // Four strictly clockwise planar turns prove the original band quad convex.
  for (std::size_t i = 0; i < 4; ++i) {
    if (!base_charge(proof, caps)) return false;
    const auto a = minus(vertices[(i + 1) % 4], vertices[i]),
               b = minus(vertices[(i + 2) % 4], vertices[(i + 1) % 4]);
    const auto turn =
        add(mul(scalar(a.x), scalar(b.y)), mul(scalar(-a.y), scalar(b.x)));
    if (!turn.supported || turn.upper >= 0) {
      fail(proof, Why::base_geometry);
      return false;
    }
  }
  constexpr std::array<std::array<std::size_t, 4>, 6> faces{{{0, 1, 2, 3},
                                                             {4, 7, 6, 5},
                                                             {0, 4, 5, 1},
                                                             {1, 5, 6, 2},
                                                             {2, 6, 7, 3},
                                                             {3, 7, 4, 0}}};
  for (std::size_t k = 0; k < 6; ++k) {
    if (!base_charge(proof, caps)) return false;
    const auto& face = faces[k];
    const auto a = vertices[face[0]];
    auto n = cross(minus(vertices[face[1]], a), minus(vertices[face[2]], a));
    const auto orientation = dot(minus(a, center), n);
    if (!orientation.supported ||
        (orientation.lower <= 0 && orientation.upper >= 0)) {
      fail(proof, Why::base_geometry);
      return false;
    }
    if (orientation.upper < 0) n = {-n.x, -n.y, -n.z};
    const auto magnitude =
        std::max({std::abs(n.x), std::abs(n.y), std::abs(n.z)});
    if (!std::isfinite(magnitude) || magnitude <= 0) {
      fail(proof, Why::base_geometry);
      return false;
    }
    n = {n.x / magnitude, n.y / magnitude, n.z / magnitude};
    double maximum = -INFINITY;
    for (const auto v : vertices) {
      if (!base_charge(proof, caps)) return false;
      const auto h = dot(v, n);
      if (!h.supported) {
        fail(proof, Why::unsupported_arithmetic);
        return false;
      }
      maximum = std::max(maximum, h.upper);
    }
    out.raw_planes[k] = {n, scalar(maximum)};
    // The frozen 1um cube covers <=.5um grid rounding plus decoding error:
    // |q|<=8e6, stored1e-6 error<2^-73, and multiplication rounding<2^-50.
    // Their sum<2e-9um, far below the remaining .5um; this NEVER widens raw.
    const auto l1 = add(add(scalar(std::abs(n.x)), scalar(std::abs(n.y))),
                        scalar(std::abs(n.z)));
    out.game_planes[k] = {n, add(scalar(maximum), mul(scalar(1e-6), l1))};
    if (!out.game_planes[k].maximum.supported) {
      fail(proof, Why::unsupported_arithmetic);
      return false;
    }
  }
  out.arithmetic_supported = true;
  return true;
}
auto metadata(const View& v, Proof& p, const Limits& c) -> bool {
  if (!base_charge(p, c)) return false;
  if (v.sources.size() != 2 || v.meshes.size() != 2 || !v.frame ||
      v.master_sha256 != master_pin || v.inventory_sha256 != inventory_pin ||
      v.history_helper_sha256 != history_pin || !hash(v.metadata_sha256) ||
      !hash(v.binary_sha256) || v.nose_properties_json.size() > 4096)
    return fail(p, Why::source_identity);
  const auto original = detail::origin_boarding_initial_material_prepared();
  const auto provider =
      detail::origin_boarding_checkpoint_material_extension_prepared();
  if (!base_charge(p, c)) return false;
  if (v.storage_bytes != provider.storage_bytes ||
      v.metadata_sha256 != provider.metadata_sha256 ||
      v.binary_sha256 != provider.binary_sha256 ||
      v.nose_properties_json != provider.nose_properties_json)
    return fail(p, Why::source_identity);
  for (std::size_t i = 0; i < 2; ++i) {
    if (!base_charge(p, c)) return false;
    if (source_ids[i] >= original.sources.size() ||
        v.sources[i].source_index != source_ids[i] ||
        !same_source(v.sources[i].identity, original.sources[source_ids[i]]) ||
        v.sources[i].identity.raw_fingerprint != raw_pins[i] ||
        v.sources[i].identity.quantized_fingerprint != q_pins[i] ||
        v.sources[i].identity.removed || v.sources[i].identity.replacement ||
        v.sources[i].identity.motion_group != -1 ||
        v.sources[i].identity.parent != "" ||
        v.meshes[i].source != source_ids[i] ||
        v.meshes[i].raw_vertices.size() != vertex_counts[i] ||
        v.meshes[i].quantized_vertices.size() != vertex_counts[i] ||
        v.meshes[i].triangles.size() != face_counts[i])
      return fail(p, Why::source_identity);
  }
  return frame_identity(*v.frame, p, c);
}
auto coordinates(const View& v, Proof& p, const Limits& c) -> bool {
  for (std::size_t i = 0; i < 2; ++i) {
    p.source = source_ids[i];
    const auto& mesh = v.meshes[i];
    for (std::size_t n = 0; n < mesh.raw_vertices.size(); ++n) {
      p.vertex = n;
      for (std::size_t a = 0; a < 3; ++a) {
        if (!charge(p.work.vertex_guards, c.vertex_guards, p,
                    Why::vertex_capacity))
          return false;
        const auto raw = component(mesh.raw_vertices[n], a);
        if (!std::isfinite(raw) || std::abs(raw) > 8)
          return fail(p, Why::source_identity);
        if (!charge(p.work.vertex_guards, c.vertex_guards, p,
                    Why::vertex_capacity))
          return false;
        const auto q = mesh.quantized_vertices[n].value[a];
        if (q < -8000000 || q > 8000000 ||
            std::nearbyint(raw * 1e6) != static_cast<double>(q))
          return fail(p, Why::source_identity);
        const auto decoded = static_cast<double>(q) * 1e-6;
        const auto delta = add(scalar(decoded), scalar(-raw));
        if (!delta.supported || delta.lower < -1e-6 || delta.upper > 1e-6)
          return fail(p, Why::source_identity);
      }
    }
    for (std::size_t t = 0; t < mesh.triangles.size(); ++t) {
      p.triangle = t;
      for (const auto id : mesh.triangles[t].vertices) {
        if (!charge(p.work.index_guards, c.index_guards, p,
                    Why::index_capacity))
          return false;
        if (id >= mesh.raw_vertices.size())
          return fail(p, Why::source_identity);
      }
    }
  }
  p.vertex.reset();
  p.triangle.reset();
  return true;
}
[[gnu::noinline]] auto construct(const View& v, const Limits& caps,
                                 Prisms& store, Proof& p) -> bool {
  p = {};
  p.work.arithmetic_supported = environment();
  if (!p.work.arithmetic_supported) return fail(p, Why::unsupported_arithmetic);
  if (!metadata(v, p, caps) || !coordinates(v, p, caps)) return false;
  p.source = 1436;
  for (std::size_t sector = 0; sector < 8; ++sector) {
    p.sector = sector;
    if (!build_prism(*v.frame, sector, p, caps, store[sector])) return false;
  }
  p.work.bindings_complete = true;
  p.source = 1436;
  const auto& mesh = v.meshes[0];
  for (std::size_t t = 0; t < mesh.triangles.size(); ++t) {
    p.triangle = t;
    for (unsigned representation = 0; representation < 2; ++representation) {
      p.quantized = representation != 0;
      bool contained = false;
      for (std::size_t sector = 0; sector < 8 && !contained; ++sector) {
        p.sector = sector;
        contained = true;
        const auto& planes = representation == 0 ? store[sector].raw_planes
                                                 : store[sector].game_planes;
        for (std::size_t n = 0; n < 3 && contained; ++n) {
          p.vertex = mesh.triangles[t].vertices[n];
          const auto point = representation == 0
                                 ? mesh.raw_vertices[*p.vertex]
                                 : qpoint(mesh.quantized_vertices[*p.vertex]);
          for (std::size_t k = 0; k < 6; ++k) {
            p.plane = k;
            if (p.work.raw_plane_guards + p.work.quantized_plane_guards >=
                caps.plane_guards)
              return fail(p, Why::plane_capacity);
            ++(representation == 0 ? p.work.raw_plane_guards
                                   : p.work.quantized_plane_guards);
            const auto h = dot(point, planes[k].normal);
            if (!h.supported) return fail(p, Why::unsupported_arithmetic);
            if (h.upper > planes[k].maximum.lower) {
              contained = false;
              break;
            }
          }
        }
      }
      if (!contained)
        return fail(p, representation == 0 ? Why::raw_containment
                                           : Why::quantized_containment);
    }
  }
  p.source.reset();
  p.triangle.reset();
  p.sector.reset();
  p.vertex.reset();
  p.plane.reset();
  p.quantized = false;
  p.work.sources = 2;
  p.work.vertices = 3408;
  p.work.triangles = 5699;
  p.work.prisms = 8;
  p.work.constructors_complete = true;
  p.complete = true;
  return true;
}
// For A=R_A intersect {n.x<=a}, B=R_B intersect {-n.x<=b},
// a.lower>=-b.lower proves the seam halfspaces cover all points. All three
// triangle vertices inside the ten nonseam planes prove the whole triangle
// lies in R_A intersect R_B by convexity, hence in A union B.
[[gnu::noinline]] auto authenticate_adjacent(std::span<const Plane> a,
                                             std::span<const Plane> b,
                                             const Limits& caps, Proof& p)
    -> bool {
  if (a.size() != 6 || b.size() != 6) return fail(p, Why::base_geometry);
  for (const auto planes : {a, b}) {
    for (const auto& plane : planes) {
      if (!base_charge(p, caps)) return false;
      if (!std::isfinite(plane.normal.x) || !std::isfinite(plane.normal.y) ||
          !std::isfinite(plane.normal.z) || !plane.maximum.supported ||
          !std::isfinite(plane.maximum.lower) ||
          !std::isfinite(plane.maximum.upper) ||
          plane.maximum.lower > plane.maximum.upper)
        return fail(p, Why::base_geometry);
    }
  }
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (!base_charge(p, caps)) return false;
    if (component(a[3].normal, axis) != -component(b[5].normal, axis))
      return fail(p, Why::base_geometry);
  }
  if (!base_charge(p, caps)) return false;
  if (a[3].normal == Vec{}) return fail(p, Why::base_geometry);
  if (!base_charge(p, caps)) return false;
  // Unary sign is exact; do not round a sum near a zero-width seam.
  if (a[3].maximum.lower < -b[5].maximum.lower)
    return fail(p, Why::base_geometry);
  return true;
}
template <typename Points>
[[gnu::noinline]] auto adjacent_contains(std::span<const Plane> a,
                                         std::span<const Plane> b,
                                         const Points& point, std::size_t first,
                                         std::size_t second, const Limits& caps,
                                         Proof& p) -> bool {
  if (!charge(p.adjacent_pair_attempts, caps.adjacent_pair_attempts, p,
              Why::pair_capacity))
    return false;
  for (std::size_t n = 0; n < 3; ++n) {
    const auto vertex = point(n);
    for (unsigned side = 0; side < 2; ++side) {
      p.sector = side == 0 ? first : second;
      const auto planes = side == 0 ? a : b;
      for (std::size_t k = 0; k < 6; ++k) {
        if (k == (side == 0 ? 3 : 5)) continue;
        p.plane = k;
        if (p.work.raw_plane_guards + p.work.quantized_plane_guards >=
            caps.plane_guards)
          return fail(p, Why::plane_capacity);
        ++(p.quantized ? p.work.quantized_plane_guards
                       : p.work.raw_plane_guards);
        const auto h = dot(vertex, planes[k].normal);
        if (!h.supported) return fail(p, Why::unsupported_arithmetic);
        if (h.upper > planes[k].maximum.lower) return false;
      }
    }
  }
  return true;
}
[[gnu::noinline]] auto union02_triangles(const View& v, const Limits& caps,
                                         const Prisms& store, Proof& p)
    -> bool {
  // This phase starts AFTER the prism builder returns; no plane packets copy.
  std::array<std::array<bool, 8>, 2> seams{};
  for (unsigned representation = 0; representation < 2; ++representation) {
    p.quantized = representation != 0;
    for (std::size_t sector = 0; sector < 8; ++sector) {
      p.sector = sector;
      const auto next = (sector + 1) % 8;
      seams[representation][sector] =
          authenticate_adjacent(representation == 0 ? store[sector].raw_planes
                                                    : store[sector].game_planes,
                                representation == 0 ? store[next].raw_planes
                                                    : store[next].game_planes,
                                caps, p);
      if (!seams[representation][sector]) return false;
    }
  }
  p.work.bindings_complete = true;
  p.source = 1436;
  const auto& mesh = v.meshes[0];
  for (std::size_t t = 0; t < mesh.triangles.size(); ++t) {
    p.triangle = t;
    for (unsigned representation = 0; representation < 2; ++representation) {
      p.quantized = representation != 0;
      bool contained = false;
      for (std::size_t sector = 0; sector < 8 && !contained; ++sector) {
        p.sector = sector;
        contained = true;
        const auto& planes = representation == 0 ? store[sector].raw_planes
                                                 : store[sector].game_planes;
        for (std::size_t n = 0; n < 3 && contained; ++n) {
          p.vertex = mesh.triangles[t].vertices[n];
          const auto point = representation == 0
                                 ? mesh.raw_vertices[*p.vertex]
                                 : qpoint(mesh.quantized_vertices[*p.vertex]);
          for (std::size_t k = 0; k < 6; ++k) {
            p.plane = k;
            if (p.work.raw_plane_guards + p.work.quantized_plane_guards >=
                caps.plane_guards)
              return fail(p, Why::plane_capacity);
            ++(representation == 0 ? p.work.raw_plane_guards
                                   : p.work.quantized_plane_guards);
            const auto h = dot(point, planes[k].normal);
            if (!h.supported) return fail(p, Why::unsupported_arithmetic);
            if (h.upper > planes[k].maximum.lower) {
              contained = false;
              break;
            }
          }
        }
      }
      if (!contained) {
        for (std::size_t sector = 0; sector < 8 && !contained; ++sector) {
          if (!seams[representation][sector])
            return fail(p, Why::base_geometry);
          const auto next = (sector + 1) % 8;
          const auto& a = representation == 0 ? store[sector].raw_planes
                                              : store[sector].game_planes;
          const auto& b = representation == 0 ? store[next].raw_planes
                                              : store[next].game_planes;
          const auto point = [&](std::size_t n) {
            p.vertex = mesh.triangles[t].vertices[n];
            return representation == 0
                       ? mesh.raw_vertices[*p.vertex]
                       : qpoint(mesh.quantized_vertices[*p.vertex]);
          };
          contained = adjacent_contains(a, b, point, sector, next, caps, p);
          if (p.condition != Why::none) return false;
        }
      }
      if (!contained)
        return fail(p, representation == 0 ? Why::raw_containment
                                           : Why::quantized_containment);
    }
  }
  p.source.reset();
  p.triangle.reset();
  p.sector.reset();
  p.vertex.reset();
  p.plane.reset();
  p.quantized = false;
  p.work.sources = 2;
  p.work.vertices = 3408;
  p.work.triangles = 5699;
  p.work.prisms = 8;
  p.work.constructors_complete = true;
  p.complete = true;
  return true;
}
[[gnu::noinline]] auto construct_union02(const View& v, const Limits& caps,
                                         Prisms& store, Proof& p) -> bool {
  p = {};
  p.work.extension_version = kBoardingCheckpointMaterialUnion02Version;
  p.work.arithmetic_supported = environment();
  if (!p.work.arithmetic_supported) return fail(p, Why::unsupported_arithmetic);
  if (!metadata(v, p, caps) || !coordinates(v, p, caps)) return false;
  p.source = 1436;
  for (std::size_t sector = 0; sector < 8; ++sector) {
    p.sector = sector;
    if (!build_prism(*v.frame, sector, p, caps, store[sector])) return false;
  }
  return union02_triangles(v, caps, store, p);
}
[[gnu::noinline]] auto constructor_error(const Proof& proof) -> std::string {
  return "Extension constructor refused condition=" +
         std::to_string(static_cast<unsigned>(proof.condition)) + " triangle=" +
         (proof.triangle ? std::to_string(*proof.triangle) : "none") +
         " sector=" + (proof.sector ? std::to_string(*proof.sector) : "none") +
         " vertex=" + (proof.vertex ? std::to_string(*proof.vertex) : "none") +
         " plane=" + (proof.plane ? std::to_string(*proof.plane) : "none");
}
auto boundary_fail(BoundaryProof& p, BoundaryWhy why) -> bool {
  p.condition = why;
  p.complete = false;
  return false;
}
auto boundary_charge(std::uint64_t& count, std::uint64_t cap, BoundaryProof& p,
                     BoundaryWhy why) -> bool {
  if (count >= cap) return boundary_fail(p, why);
  ++count;
  return true;
}
auto boundary_limits_valid(const BoundaryLimits& c) -> bool {
  const BoundaryLimits top;
  return c.source_bytes <= top.source_bytes &&
         c.base_guards <= top.base_guards &&
         c.vertex_guards <= top.vertex_guards &&
         c.index_guards <= top.index_guards &&
         c.bound_coordinate_guards <= top.bound_coordinate_guards &&
         c.nondegenerate_triangles <= top.nondegenerate_triangles &&
         c.edge_occurrences <= top.edge_occurrences &&
         c.edge_comparisons <= top.edge_comparisons;
}
[[gnu::noinline]] auto boundary_input(const View& view,
                                      const BoundaryLimits& caps,
                                      BoundaryProof& out) -> bool {
  Proof original;
  Limits limits;
  limits.source_bytes = caps.source_bytes;
  limits.base_guards = caps.base_guards;
  limits.vertex_guards = caps.vertex_guards;
  limits.index_guards = caps.index_guards;
  const auto bytes = out.work.source_bytes;
  const bool complete =
      metadata(view, original, limits) && coordinates(view, original, limits);
  out.work = original.work;
  out.work.extension_version = kBoardingCheckpointMaterialBoundary03Version;
  out.work.source_bytes = bytes;
  out.work.arithmetic_supported = true;
  if (!complete) {
    out.input_condition = original.condition;
    out.source = original.source;
    out.triangle = original.triangle;
    out.vertex = original.vertex;
    return boundary_fail(out, original.condition == Why::unsupported_arithmetic
                                  ? BoundaryWhy::unsupported_arithmetic
                                  : BoundaryWhy::input_validation);
  }
  out.work.bindings_complete = true;
  return true;
}
[[gnu::noinline]] auto boundary_full_bounds(const View& view,
                                            const BoundaryLimits& caps,
                                            BoundaryProof& out) -> bool {
  for (std::size_t mesh = 0; mesh < 2; ++mesh) {
    out.source = source_ids[mesh];
    const auto& m = view.meshes[mesh];
    const auto& identity = view.sources[mesh].identity;
    for (std::size_t vertex = 0; vertex < m.raw_vertices.size(); ++vertex) {
      out.vertex = vertex;
      for (unsigned representation = 0; representation < 2; ++representation) {
        const auto& bounds = representation == 0 ? identity.raw_bounds
                                                 : identity.quantized_bounds;
        for (std::size_t axis = 0; axis < 3; ++axis) {
          out.axis = axis;
          if (!boundary_charge(out.bound_coordinate_guards,
                               caps.bound_coordinate_guards, out,
                               BoundaryWhy::bound_capacity))
            return false;
          const auto lo = component(bounds.lower, axis),
                     hi = component(bounds.upper, axis);
          const auto value =
              representation == 0
                  ? component(m.raw_vertices[vertex], axis)
                  : static_cast<double>(
                        m.quantized_vertices[vertex].value[axis]) *
                        1e-6;
          if (!std::isfinite(lo) || !std::isfinite(hi) ||
              !std::isfinite(value) || lo > hi || value < lo || value > hi)
            return boundary_fail(out, BoundaryWhy::source_bounds);
        }
      }
    }
  }
  out.vertex.reset();
  out.axis.reset();
  return true;
}
[[gnu::noinline]] auto boundary_topology(const detail::MaterialMeshRecord& mesh,
                                         const BoundaryLimits& caps,
                                         BoundaryProof& out) -> bool {
  out.source = mesh.source;
  for (std::size_t t = 0; t < mesh.triangles.size(); ++t) {
    out.triangle = t;
    if (!boundary_charge(out.nondegenerate_triangles,
                         caps.nondegenerate_triangles, out,
                         BoundaryWhy::triangle_capacity))
      return false;
    const auto& ids = mesh.triangles[t].vertices;
    const auto& a = mesh.quantized_vertices[ids[0]].value;
    const auto& b = mesh.quantized_vertices[ids[1]].value;
    const auto& c = mesh.quantized_vertices[ids[2]].value;
    const std::array<std::int64_t, 3> u{b[0] - a[0], b[1] - a[1], b[2] - a[2]},
        v{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    // Bounds authenticated before this helper: |u|,|v|<=16e6, each
    // cross component<=512e12, so every signed int64 operation is exact.
    if (u[1] * v[2] - u[2] * v[1] == 0 && u[2] * v[0] - u[0] * v[2] == 0 &&
        u[0] * v[1] - u[1] * v[0] == 0)
      return boundary_fail(out, BoundaryWhy::degenerate_triangle);
  }
  for (std::size_t t = 0; t < mesh.triangles.size(); ++t) {
    out.triangle = t;
    for (std::size_t edge = 0; edge < 3; ++edge) {
      out.edge = edge;
      if (!boundary_charge(out.edge_occurrences, caps.edge_occurrences, out,
                           BoundaryWhy::edge_capacity))
        return false;
      const auto a = mesh.triangles[t].vertices[edge],
                 b = mesh.triangles[t].vertices[(edge + 1) % 3];
      std::size_t forward{}, reverse{};
      for (std::size_t other = 0; other < mesh.triangles.size(); ++other) {
        out.other_triangle = other;
        for (std::size_t other_edge = 0; other_edge < 3; ++other_edge) {
          if (!boundary_charge(out.edge_comparisons, caps.edge_comparisons, out,
                               BoundaryWhy::edge_comparison_capacity))
            return false;
          const auto x = mesh.triangles[other].vertices[other_edge],
                     y = mesh.triangles[other].vertices[(other_edge + 1) % 3];
          forward += static_cast<std::size_t>(a == x && b == y);
          reverse += static_cast<std::size_t>(a == y && b == x);
        }
      }
      // Forward includes this occurrence itself. Exactly one opposite
      // occurrence is required; no manifold/vertex-link/embedding claim.
      if (forward != 1 || reverse != 1)
        return boundary_fail(out, BoundaryWhy::closed_chain);
    }
  }
  out.source.reset();
  out.triangle.reset();
  out.edge.reset();
  out.other_triangle.reset();
  out.vertex.reset();
  out.axis.reset();
  out.complete = true;
  return true;
}
[[gnu::noinline]] auto boundary_construct(const View& view,
                                          const BoundaryLimits& caps,
                                          BoundaryProof& out) -> bool {
  out.work.extension_version = kBoardingCheckpointMaterialBoundary03Version;
  out.work.arithmetic_supported = environment();
  if (!out.work.arithmetic_supported)
    return boundary_fail(out, BoundaryWhy::unsupported_arithmetic);
  if (!boundary_input(view, caps, out) ||
      !boundary_full_bounds(view, caps, out) ||
      !boundary_topology(view.meshes[0], caps, out))
    return false;
  out.work.sources = 2;
  out.work.vertices = 3408;
  out.work.triangles = 5699;
  out.work.prisms = 0;
  out.work.constructors_complete = true;
  return true;
}
[[gnu::noinline]] auto boundary_error(const BoundaryProof& out) -> std::string {
  return "Boundary03 refused condition=" +
         std::to_string(static_cast<unsigned>(out.condition)) + " triangle=" +
         (out.triangle ? std::to_string(*out.triangle) : "none") +
         " edge=" + (out.edge ? std::to_string(*out.edge) : "none");
}
} // namespace
struct OriginBoardingCheckpointMaterialExtension::Data {
  NativeCraftBinding binding;
  OriginBoardingInitialMaterial base;
  const OriginBoardingInitialMaterial::Data* base_identity{};
  View prepared;
  Prisms prisms{};
  BoardingCheckpointMaterialExtensionSummary summary;
  Data(const NativeCraftBinding& b, const OriginBoardingInitialMaterial& source)
      : binding(b), base(source),
        base_identity(detail::BoardingInitialMaterialAccess::data(source)) {}
};
namespace {
auto required_source_bytes(const View& view) -> std::size_t {
  constexpr auto fixed =
      sizeof(OriginBoardingCheckpointMaterialExtension::Data) + 64;
  return view.storage_bytes > std::numeric_limits<std::size_t>::max() - fixed
             ? std::numeric_limits<std::size_t>::max()
             : view.storage_bytes + fixed;
}
} // namespace
static_assert(sizeof(OriginBoardingCheckpointMaterialExtension::Data) +
                  sizeof(Proof) + sizeof(Prism) + sizeof(Limits) + 16 + 1024 +
                  1024 <=
              8192);
static_assert(sizeof(Prisms) + sizeof(Proof) + sizeof(Prism) + sizeof(Limits) +
                  16 + 1024 + 1024 <=
              8192);
static_assert(sizeof(OriginBoardingCheckpointMaterialExtension::Data) +
                  sizeof(BoundaryProof) + sizeof(Proof) +
                  sizeof(BoundaryLimits) + sizeof(Limits) + 2048 <=
              8192);
OriginBoardingCheckpointMaterialExtension::
    OriginBoardingCheckpointMaterialExtension(std::shared_ptr<const Data> data)
    : data_(std::move(data)) {
}
auto OriginBoardingCheckpointMaterialExtension::summary() const
    -> const BoardingCheckpointMaterialExtensionSummary* {
  return data_ ? &data_->summary : nullptr;
}
namespace detail {
auto BoardingCheckpointMaterialExtensionAccess::data(
    const OriginBoardingCheckpointMaterialExtension& source)
    -> const OriginBoardingCheckpointMaterialExtension::Data* {
  return source.data_.get();
}
auto BoardingCheckpointMaterialExtensionAccess::valid(
    const OriginBoardingCheckpointMaterialExtension& source,
    const OriginBoardingInitialMaterial& base) -> bool {
  const auto* d = data(source);
  return d && d->summary.constructors_complete &&
         (d->summary.extension_version ==
              kBoardingCheckpointMaterialExtensionVersion ||
          d->summary.extension_version ==
              kBoardingCheckpointMaterialUnion02Version ||
          d->summary.extension_version ==
              kBoardingCheckpointMaterialBoundary03Version) &&
         d->base_identity &&
         BoardingInitialMaterialAccess::data(base) == d->base_identity &&
         BoardingInitialMaterialAccess::data(d->base) == d->base_identity &&
         initial_material_extension_binding_matches(d->binding, base);
}
auto BoardingCheckpointMaterialExtensionAccess::make(
    const NativeCraftBinding& binding,
    const OriginBoardingInitialMaterial& base, Limits limits, Proof* evidence)
    -> std::expected<OriginBoardingCheckpointMaterialExtension, std::string> {
  if (!valid_limits(limits))
    return std::unexpected("Extension lowered registered capacities required");
  Proof local;
  auto& proof = evidence ? *evidence : local;
  proof = {};
  const auto view = origin_boarding_checkpoint_material_extension_prepared();
  proof.work.source_bytes = required_source_bytes(view);
  if (view.storage_bytes > limits.source_bytes ||
      sizeof(OriginBoardingCheckpointMaterialExtension::Data) + 64 >
          limits.source_bytes - view.storage_bytes) {
    fail(proof, Why::source_capacity);
    return std::unexpected("Extension source capacity before allocation");
  }
  if (!initial_material_extension_binding_matches(binding, base)) {
    fail(proof, Why::invalid_binding);
    return std::unexpected(
        "Extension genuine same-base original native binding required");
  }
  auto owned =
      std::make_shared<OriginBoardingCheckpointMaterialExtension::Data>(binding,
                                                                        base);
  owned->prepared = view;
  const bool complete = construct(view, limits, owned->prisms, proof);
  proof.work.source_bytes = required_source_bytes(view);
  if (!complete) return std::unexpected(constructor_error(proof));
  proof.work.source_bytes = required_source_bytes(view);
  owned->summary = proof.work;
  return OriginBoardingCheckpointMaterialExtension{std::move(owned)};
}
auto BoardingCheckpointMaterialExtensionAccess::make_union02(
    const NativeCraftBinding& binding,
    const OriginBoardingInitialMaterial& base, Limits limits, Proof* evidence)
    -> std::expected<OriginBoardingCheckpointMaterialExtension, std::string> {
  if (!valid_limits(limits))
    return std::unexpected("Extension lowered registered capacities required");
  Proof local;
  auto& proof = evidence ? *evidence : local;
  proof = {};
  proof.work.extension_version = kBoardingCheckpointMaterialUnion02Version;
  const auto view = origin_boarding_checkpoint_material_extension_prepared();
  proof.work.source_bytes = required_source_bytes(view);
  if (view.storage_bytes > limits.source_bytes ||
      sizeof(OriginBoardingCheckpointMaterialExtension::Data) + 64 >
          limits.source_bytes - view.storage_bytes) {
    fail(proof, Why::source_capacity);
    return std::unexpected("Extension source capacity before allocation");
  }
  if (!initial_material_extension_binding_matches(binding, base)) {
    fail(proof, Why::invalid_binding);
    return std::unexpected(
        "Extension genuine same-base original native binding required");
  }
  auto owned =
      std::make_shared<OriginBoardingCheckpointMaterialExtension::Data>(binding,
                                                                        base);
  owned->prepared = view;
  const bool complete = construct_union02(view, limits, owned->prisms, proof);
  proof.work.source_bytes = required_source_bytes(view);
  if (!complete) return std::unexpected(constructor_error(proof));
  proof.work.source_bytes = required_source_bytes(view);
  owned->summary = proof.work;
  return OriginBoardingCheckpointMaterialExtension{std::move(owned)};
}
auto BoardingCheckpointMaterialExtensionAccess::make_boundary03(
    const NativeCraftBinding& binding,
    const OriginBoardingInitialMaterial& base, BoundaryLimits caps,
    BoundaryProof* evidence)
    -> std::expected<OriginBoardingCheckpointMaterialExtension, std::string> {
  BoundaryProof local;
  auto& out = evidence ? *evidence : local;
  out = {};
  out.work.extension_version = kBoardingCheckpointMaterialBoundary03Version;
  if (!boundary_limits_valid(caps)) {
    boundary_fail(out, BoundaryWhy::invalid_limits);
    return std::unexpected("Boundary03 lowered registered capacities required");
  }
  const auto view = origin_boarding_checkpoint_material_extension_prepared();
  out.work.source_bytes = required_source_bytes(view);
  if (out.work.source_bytes > caps.source_bytes) {
    boundary_fail(out, BoundaryWhy::source_capacity);
    return std::unexpected("Boundary03 source capacity before allocation");
  }
  if (!initial_material_extension_binding_matches(binding, base)) {
    boundary_fail(out, BoundaryWhy::invalid_binding);
    return std::unexpected(
        "Boundary03 genuine same-base native binding required");
  }
  auto owned =
      std::make_shared<OriginBoardingCheckpointMaterialExtension::Data>(binding,
                                                                        base);
  owned->prepared = view;
  if (!boundary_construct(view, caps, out))
    return std::unexpected(boundary_error(out));
  owned->summary = out.work;
  return OriginBoardingCheckpointMaterialExtension{std::move(owned)};
}
auto BoardingCheckpointMaterialExtensionAccess::relation(
    const OriginBoardingCheckpointMaterialExtension& e,
    const OriginBoardingInitialMaterial& base, std::size_t source) -> Relation {
  if (!valid(e, base)) return Relation::none;
  return source == 1436   ? (data(e)->summary.extension_version ==
                                   kBoardingCheckpointMaterialBoundary03Version
                                 ? Relation::closed_frame_boundary
                                 : Relation::frame_annulus)
         : source == 1574 ? Relation::retained_cut_skin
                          : Relation::none;
}
auto BoardingCheckpointMaterialExtensionAccess::planes(
    const OriginBoardingCheckpointMaterialExtension& e,
    const OriginBoardingInitialMaterial& base, std::size_t source,
    std::size_t sector) -> std::span<const Plane> {
  if (relation(e, base, source) != Relation::frame_annulus || sector >= 8)
    return {};
  return data(e)->prisms[sector].game_planes;
}
auto BoardingCheckpointMaterialExtensionAccess::mesh(
    const OriginBoardingCheckpointMaterialExtension& e,
    const OriginBoardingInitialMaterial& base, std::size_t source)
    -> const MaterialMeshRecord* {
  if (!valid(e, base)) return nullptr;
  return source == 1436   ? &data(e)->prepared.meshes[0]
         : source == 1574 ? &data(e)->prepared.meshes[1]
                          : nullptr;
}
auto BoardingCheckpointMaterialExtensionAccess::triangle(
    const OriginBoardingCheckpointMaterialExtension& e,
    const OriginBoardingInitialMaterial& base, std::size_t source,
    std::size_t ordinal, bool& collapsed)
    -> std::expected<std::array<Vec, 3>, std::string> {
  collapsed = false;
  const auto* m = mesh(e, base, source);
  if (!m || ordinal >= m->triangles.size())
    return std::unexpected("Extension genuine source/full ordinal required");
  const auto& ids = m->triangles[ordinal].vertices;
  const auto &a = m->quantized_vertices[ids[0]],
             &b = m->quantized_vertices[ids[1]],
             &c = m->quantized_vertices[ids[2]];
  collapsed = a.value == b.value || b.value == c.value || c.value == a.value;
  return std::array{qpoint(a), qpoint(b), qpoint(c)};
}
auto checkpoint_material_extension_constructor_math(const View& v, Limits caps)
    -> std::expected<Proof, std::string> {
  if (!valid_limits(caps))
    return std::unexpected(
        "Extension arithmetic lowered registered capacities required");
  Proof out;
  const auto required = required_source_bytes(v);
  out.work.source_bytes = required;
  if (v.storage_bytes > caps.source_bytes ||
      sizeof(OriginBoardingCheckpointMaterialExtension::Data) + 64 >
          caps.source_bytes - v.storage_bytes) {
    fail(out, Why::source_capacity);
    return out;
  }
  Prisms prisms;
  static_cast<void>(construct(v, caps, prisms, out));
  out.work.source_bytes = required;
  return out;
}
auto checkpoint_material_extension_union02_constructor_math(const View& v,
                                                            Limits caps)
    -> std::expected<Proof, std::string> {
  if (!valid_limits(caps))
    return std::unexpected(
        "Extension arithmetic lowered registered capacities required");
  Proof out;
  out.work.extension_version = kBoardingCheckpointMaterialUnion02Version;
  const auto required = required_source_bytes(v);
  out.work.source_bytes = required;
  if (v.storage_bytes > caps.source_bytes ||
      sizeof(OriginBoardingCheckpointMaterialExtension::Data) + 64 >
          caps.source_bytes - v.storage_bytes) {
    fail(out, Why::source_capacity);
    return out;
  }
  Prisms prisms;
  static_cast<void>(construct_union02(v, caps, prisms, out));
  out.work.source_bytes = required;
  return out;
}
auto checkpoint_material_extension_adjacent_union_math(
    std::span<const Plane> a, std::span<const Plane> b,
    const std::array<Vec, 3>& triangle, bool quantized, Limits caps)
    -> std::expected<Proof, std::string> {
  if (!valid_limits(caps))
    return std::unexpected(
        "Extension arithmetic lowered registered capacities required");
  Proof out;
  out.work.extension_version = kBoardingCheckpointMaterialUnion02Version;
  out.quantized = quantized;
  out.work.arithmetic_supported = environment();
  if (!out.work.arithmetic_supported) {
    fail(out, Why::unsupported_arithmetic);
    return out;
  }
  if (!authenticate_adjacent(a, b, caps, out)) return out;
  const auto point = [&](std::size_t n) {
    out.vertex = n;
    return triangle[n];
  };
  out.complete = adjacent_contains(a, b, point, 0, 1, caps, out);
  if (!out.complete && out.condition == Why::none)
    fail(out, quantized ? Why::quantized_containment : Why::raw_containment);
  return out;
}
auto checkpoint_material_extension_boundary03_constructor_math(
    const View& view, BoundaryLimits caps)
    -> std::expected<BoundaryProof, std::string> {
  if (!boundary_limits_valid(caps))
    return std::unexpected("Boundary03 lowered registered capacities required");
  BoundaryProof out;
  out.work.extension_version = kBoardingCheckpointMaterialBoundary03Version;
  out.work.source_bytes = required_source_bytes(view);
  if (out.work.source_bytes > caps.source_bytes) {
    boundary_fail(out, BoundaryWhy::source_capacity);
    return out;
  }
  static_cast<void>(boundary_construct(view, caps, out));
  return out;
}
auto checkpoint_material_extension_closed_boundary_math(
    const MaterialMeshRecord& mesh, BoundaryLimits caps)
    -> std::expected<BoundaryProof, std::string> {
  if (!boundary_limits_valid(caps))
    return std::unexpected("Boundary03 lowered registered capacities required");
  BoundaryProof out;
  out.work.extension_version = kBoardingCheckpointMaterialBoundary03Version;
  out.work.arithmetic_supported = environment();
  if (!out.work.arithmetic_supported) {
    boundary_fail(out, BoundaryWhy::unsupported_arithmetic);
    return out;
  }
  if (mesh.quantized_vertices.empty() ||
      mesh.raw_vertices.size() != mesh.quantized_vertices.size() ||
      mesh.triangles.empty() || mesh.triangles.size() > 512 ||
      mesh.quantized_vertices.size() > 3408) {
    boundary_fail(out, BoundaryWhy::input_validation);
    return out;
  }
  for (std::size_t vertex = 0; vertex < mesh.raw_vertices.size(); ++vertex) {
    out.vertex = vertex;
    for (std::size_t axis = 0; axis < 3; ++axis) {
      out.axis = axis;
      const auto raw = component(mesh.raw_vertices[vertex], axis);
      const auto q = mesh.quantized_vertices[vertex].value[axis];
      if (!boundary_charge(out.work.vertex_guards, caps.vertex_guards, out,
                           BoundaryWhy::vertex_capacity))
        return out;
      if (!std::isfinite(raw) || std::abs(raw) > 8) {
        boundary_fail(out, BoundaryWhy::input_validation);
        return out;
      }
      if (!boundary_charge(out.work.vertex_guards, caps.vertex_guards, out,
                           BoundaryWhy::vertex_capacity))
        return out;
      if (q < -8000000 || q > 8000000 ||
          std::nearbyint(raw * 1e6) != static_cast<double>(q)) {
        boundary_fail(out, BoundaryWhy::input_validation);
        return out;
      }
    }
  }
  for (std::size_t t = 0; t < mesh.triangles.size(); ++t) {
    out.triangle = t;
    for (const auto index : mesh.triangles[t].vertices) {
      if (!boundary_charge(out.work.index_guards, caps.index_guards, out,
                           BoundaryWhy::index_capacity))
        return out;
      if (index >= mesh.quantized_vertices.size()) {
        boundary_fail(out, BoundaryWhy::input_validation);
        return out;
      }
    }
  }
  out.vertex.reset();
  out.axis.reset();
  static_cast<void>(boundary_topology(mesh, caps, out));
  return out;
}
auto checkpoint_material_extension_prism_math(const Frame& frame,
                                              std::size_t sector)
    -> std::expected<Prism, std::string> {
  if (sector >= 8)
    return std::unexpected("Extension arithmetic sector below eight required");
  Proof proof;
  const Limits limits;
  if (!environment()) return Prism{};
  if (!frame_identity(frame, proof, limits))
    return std::unexpected(
        "Extension arithmetic original frame identity refused");
  Prism out;
  if (!build_prism(frame, sector, proof, limits, out))
    return std::unexpected("Extension arithmetic convex prism refused");
  return out;
}
} // namespace detail
auto make_origin_boarding_checkpoint_material_extension(
    const NativeCraftBinding& binding,
    const OriginBoardingInitialMaterial& base)
    -> std::expected<OriginBoardingCheckpointMaterialExtension, std::string> {
  return detail::BoardingCheckpointMaterialExtensionAccess::make(binding, base,
                                                                 {}, nullptr);
}
auto make_origin_boarding_checkpoint_material_extension_union02(
    const NativeCraftBinding& binding,
    const OriginBoardingInitialMaterial& base)
    -> std::expected<OriginBoardingCheckpointMaterialExtension, std::string> {
  return detail::BoardingCheckpointMaterialExtensionAccess::make_union02(
      binding, base, {}, nullptr);
}
auto make_origin_boarding_checkpoint_material_extension_boundary03(
    const NativeCraftBinding& binding,
    const OriginBoardingInitialMaterial& base)
    -> std::expected<OriginBoardingCheckpointMaterialExtension, std::string> {
  return detail::BoardingCheckpointMaterialExtensionAccess::make_boundary03(
      binding, base, {}, nullptr);
}
} // namespace apsis_drift
