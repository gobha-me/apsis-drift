#include "apsis_drift/origin_boarding_hatch_seal_material.hpp"
#include "origin_boarding_hatch_seal_material_internal.hpp"
#include "origin_boarding_hatch_seal_material_prepared.hpp"
#include <algorithm>
#include <bit>
#include <cfenv>
#include <cmath>
#include <limits>
#include <utility>
namespace apsis_drift {
namespace {
using Vec = RigidVector3;
using Bounds = BoardingPlantedLegPointBounds;
using View = detail::HatchSealMaterialPreparedView;
using Limits = detail::BoardingHatchSealMaterialLimits;
using Proof = detail::BoardingHatchSealMaterialConstructorMath;
using Why = detail::BoardingHatchSealMaterialCondition;
using Capsule = detail::BoardingHatchSealCapsule;
using Capsules = std::array<Capsule, 8>;
constexpr std::size_t source_index{1441};
constexpr std::string_view master_pin{
    "87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677"},
    inventory_pin{
        "a78800c6f5223323855aae1c37e1b40f7f83af76c709696b4eb0f08f3a9f55d8"},
    lifeboat_pin{
        "1ff8c7de423eeddf100f5a36e1f494225263c342e745361075dbef88c7215db1"},
    finish_pin{
        "8c9fe3208c3360c5d96d2fd38847674c5875289e171ea9ca232031b33d78f38f"},
    raw_pin{"1c46cd423627cbdf9f94bb2cbc74f23befe9f8e5ac903d9be195b79d8c346e40"},
    grid_pin{
        "21c6d9c5dd54af597e8e04a65d9fda80c56c3fa97c45f9ad315c86de9bfbfce1"};
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
auto fail(Proof& p, Why why) -> bool {
  p.condition = why;
  p.complete = false;
  return false;
}
auto charge(std::uint64_t& count, std::uint64_t cap, Proof& p, Why why)
    -> bool {
  if (count >= cap) return fail(p, why);
  ++count;
  return true;
}
auto base_charge(Proof& p, const Limits& caps) -> bool {
  return charge(p.work.base_guards, caps.base_guards, p, Why::base_capacity);
}
auto valid_limits(const Limits& c) -> bool {
  const Limits t;
  return c.source_bytes <= t.source_bytes && c.segments <= t.segments &&
         c.base_guards <= t.base_guards &&
         c.coordinate_guards <= t.coordinate_guards &&
         c.bound_guards <= t.bound_guards && c.index_guards <= t.index_guards &&
         c.strip_guards <= t.strip_guards && c.inclusions <= t.inclusions;
}
auto hash(std::string_view s) -> bool {
  return s.size() == 64 && std::ranges::all_of(s, [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}
auto same_source(const detail::MaterialSourceRecord& a,
                 const detail::MaterialSourceRecord& b) -> bool {
  return a.name == b.name && a.source_type == b.source_type &&
         a.parent == b.parent && a.raw_fingerprint == b.raw_fingerprint &&
         a.quantized_fingerprint == b.quantized_fingerprint &&
         a.raw_bounds.lower == b.raw_bounds.lower &&
         a.raw_bounds.upper == b.raw_bounds.upper &&
         a.quantized_bounds.lower == b.quantized_bounds.lower &&
         a.quantized_bounds.upper == b.quantized_bounds.upper &&
         a.original_object == b.original_object &&
         a.original_group == b.original_group &&
         a.motion_group == b.motion_group && a.mesh == b.mesh &&
         a.original_start == b.original_start &&
         a.original_count == b.original_count && a.relation == b.relation &&
         a.removed == b.removed && a.absent_crop == b.absent_crop &&
         a.replacement == b.replacement;
}
[[gnu::noinline]] auto metadata(const View& v, const Limits& caps, Proof& p)
    -> bool {
  if (!base_charge(p, caps)) return false;
  if (!v.source || !v.mesh || !v.constructor || v.master_sha256 != master_pin ||
      v.inventory_sha256 != inventory_pin ||
      v.lifeboat_sha256 != lifeboat_pin || v.finish_sha256 != finish_pin ||
      !hash(v.metadata_sha256) || !hash(v.binary_sha256))
    return fail(p, Why::source_identity);
  const auto original = detail::origin_boarding_initial_material_prepared();
  const auto provider = detail::origin_boarding_hatch_seal_material_prepared();
  if (!base_charge(p, caps)) return false;
  if (!provider.constructor || v.storage_bytes != provider.storage_bytes ||
      v.metadata_sha256 != provider.metadata_sha256 ||
      v.binary_sha256 != provider.binary_sha256 ||
      original.sources.size() <= source_index ||
      !same_source(*v.source, original.sources[source_index]) ||
      v.source->name != "WF02 | CABIN hatch perimeter seal" ||
      v.source->source_type != "CURVE" || v.source->parent != "" ||
      v.source->motion_group != -1 || v.source->mesh != -1 ||
      v.source->relation != BoardingInitialMaterialRelation::unknown ||
      v.source->removed || v.source->replacement || v.source->absent_crop ||
      v.source->original_object != 655 || v.source->original_group != 0 ||
      v.source->original_start != 189501 || v.source->original_count != 140 ||
      v.source->raw_fingerprint != raw_pin ||
      v.source->quantized_fingerprint != grid_pin ||
      v.mesh->source != source_index || v.mesh->raw_vertices.size() != 90 ||
      v.mesh->quantized_vertices.size() != 90 ||
      v.mesh->triangles.size() != 160)
    return fail(p, Why::source_identity);
  const auto& c = *v.constructor;
  const auto& authentic = *provider.constructor;
  constexpr OperatingTransform correction{
      {Vec{1, 0, 0}, Vec{0, 0, -1}, Vec{0, 1, 0}, Vec{}}};
  if (!base_charge(p, caps)) return false;
  if (c.corrected_world != correction ||
      c.corrected_world != authentic.corrected_world ||
      !std::isfinite(c.bevel_depth) || c.bevel_depth <= 0 ||
      c.bevel_depth >= .1 || c.bevel_depth != authentic.bevel_depth ||
      c.local_points[0] != c.local_points[8])
    return fail(p, Why::constructor_settings);
  for (const auto& strings :
       {std::pair{c.settings_json, authentic.settings_json},
        std::pair{c.spline_json, authentic.spline_json},
        std::pair{c.modifiers_json, authentic.modifiers_json},
        std::pair{c.properties_json, authentic.properties_json}}) {
    if (!base_charge(p, caps)) return false;
    if (strings.first.size() > 4096 || strings.first != strings.second)
      return fail(p, Why::constructor_settings);
  }
  for (std::size_t n = 0; n < 9; ++n) {
    p.vertex = n;
    for (std::size_t a = 0; a < 3; ++a) {
      p.axis = a;
      if (!base_charge(p, caps)) return false;
      const auto value = component(c.local_points[n], a);
      if (!std::isfinite(value) || std::abs(value) > 8 ||
          value != component(authentic.local_points[n], a))
        return fail(p, Why::constructor_settings);
    }
  }
  p.vertex.reset();
  p.axis.reset();
  p.work.bindings_complete = true;
  return true;
}
[[gnu::noinline]] auto coordinates(const View& v, const Limits& caps, Proof& p)
    -> bool {
  const auto& mesh = *v.mesh;
  for (std::size_t n = 0; n < 90; ++n) {
    p.vertex = n;
    for (std::size_t a = 0; a < 3; ++a) {
      p.axis = a;
      if (!charge(p.work.coordinate_guards, caps.coordinate_guards, p,
                  Why::coordinate_capacity))
        return false;
      const auto raw = component(mesh.raw_vertices[n], a);
      if (!std::isfinite(raw) || std::abs(raw) > 8)
        return fail(p, Why::source_identity);
      if (!charge(p.work.coordinate_guards, caps.coordinate_guards, p,
                  Why::coordinate_capacity))
        return false;
      const auto q = mesh.quantized_vertices[n].value[a];
      if (q < -8000000 || q > 8000000 ||
          std::nearbyint(raw * 1e6) != static_cast<double>(q))
        return fail(p, Why::source_identity);
      // The fixed1um cube includes .5um quantization and tiny grid decoding
      // error at8m. Use an outward bound even at an exactly half-grid input.
      const auto decoded = static_cast<double>(q) * 1e-6;
      const auto difference = decoded - raw;
      if (!std::isfinite(difference) ||
          std::nextafter(difference, -INFINITY) < -1e-6 ||
          std::nextafter(difference, INFINITY) > 1e-6)
        return fail(p, Why::source_identity);
      for (unsigned representation = 0; representation < 2; ++representation) {
        p.quantized = representation != 0;
        if (!charge(p.work.bound_guards, caps.bound_guards, p,
                    Why::bound_capacity))
          return false;
        const auto& bounds = representation == 0 ? v.source->raw_bounds
                                                 : v.source->quantized_bounds;
        const auto lo = component(bounds.lower, a),
                   hi = component(bounds.upper, a);
        const auto value = representation == 0 ? raw : decoded;
        if (!std::isfinite(lo) || !std::isfinite(hi) || lo > hi || value < lo ||
            value > hi)
          return fail(p, Why::source_bounds);
      }
    }
  }
  p.vertex.reset();
  p.axis.reset();
  p.quantized = false;
  for (std::size_t t = 0; t < 160; ++t) {
    p.triangle = t;
    for (const auto id : mesh.triangles[t].vertices) {
      if (!charge(p.work.index_guards, caps.index_guards, p,
                  Why::index_capacity))
        return false;
      if (id >= 90) return fail(p, Why::source_identity);
    }
  }
  p.triangle.reset();
  return true;
}
[[gnu::noinline]] auto radii(const View& v, const Limits& caps, Proof& p)
    -> bool {
  if (!base_charge(p, caps)) return false;
  using Shape = detail::BoardingSourceEndpointSurfaceCheckpointShape;
  const detail::BoardingSourceEndpointSurfaceCheckpointSolidBounds unit{
      Bounds{Vec{}, Vec{}}, Bounds{Vec{}, Vec{}}, {}, 1, Shape::capsule};
  // Support of the genuine zero-centered unit ball in(1,1,1) is sqrt(3).
  // The unchanged support helper certifies its root with three+three terms.
  const auto root =
      detail::boarding_source_endpoint_surface_checkpoint_support_math(
          unit, 0, {1, 1, 1});
  if (!root || !root->supported || !std::isfinite(root->upper) ||
      root->upper <= 0 || root->upper > 2)
    return fail(p, Why::unsupported_arithmetic);
  p.work.raw_radius =
      std::nextafter(v.constructor->bevel_depth + 0x1p-20, INFINITY);
  const auto padding = std::nextafter(root->upper * 1e-6, INFINITY);
  p.work.game_radius = std::nextafter(p.work.raw_radius + padding, INFINITY);
  if (!std::isfinite(p.work.raw_radius) || !std::isfinite(p.work.game_radius) ||
      p.work.raw_radius <= 0 || p.work.game_radius <= p.work.raw_radius)
    return fail(p, Why::unsupported_arithmetic);
  return true;
}
auto world(Vec p) -> Vec {
  return {p.x, p.z, -p.y};
}
[[gnu::noinline]] auto segments(const View& v, const Limits& caps,
                                Capsules& out, Proof& p) -> bool {
  for (std::size_t n = 0; n < 8; ++n) {
    p.strip = n;
    if (p.work.segments >= caps.segments) return fail(p, Why::segment_capacity);
    if (!base_charge(p, caps)) return false;
    ++p.work.segments;
    const auto first = world(v.constructor->local_points[n]),
               last = world(v.constructor->local_points[n + 1]);
    if (first == last) return fail(p, Why::constructor_settings);
    out[n] = {{first, first}, {last, last}, p.work.game_radius};
  }
  p.strip.reset();
  return true;
}
[[gnu::noinline]] auto inclusion(const View& v, const Limits& caps,
                                 const Capsules& segments, Proof& p) -> bool {
  const auto& mesh = *v.mesh;
  for (std::size_t t = 0; t < 160; ++t) {
    p.triangle = t;
    if (!charge(p.work.strip_guards, caps.strip_guards, p, Why::strip_capacity))
      return false;
    const auto& ids = mesh.triangles[t].vertices;
    const auto lo = *std::min_element(ids.begin(), ids.end()) / 10,
               hi = *std::max_element(ids.begin(), ids.end()) / 10;
    p.strip = lo;
    if (hi != lo + 1 || hi >= 9 || lo >= 8) return fail(p, Why::ring_topology);
    const auto& segment = segments[lo];
    for (const auto id : ids) {
      p.vertex = id;
      for (unsigned representation = 0; representation < 2; ++representation) {
        p.quantized = representation != 0;
        if (p.work.raw_inclusions + p.work.quantized_inclusions >=
            caps.inclusions)
          return fail(p, Why::inclusion_capacity);
        ++(representation == 0 ? p.work.raw_inclusions
                               : p.work.quantized_inclusions);
        const auto& q = mesh.quantized_vertices[id].value;
        const auto point = representation == 0
                               ? mesh.raw_vertices[id]
                               : Vec{static_cast<double>(q[0]) * 1e-6,
                                     static_cast<double>(q[1]) * 1e-6,
                                     static_cast<double>(q[2]) * 1e-6};
        const auto radius =
            representation == 0 ? p.work.raw_radius : p.work.game_radius;
        const auto proof = detail::initial_material_capsule_vertex_math(
            {point, point}, segment.first, segment.second, radius);
        if (!proof || !proof->arithmetic_supported)
          return fail(p, Why::unsupported_arithmetic);
        if (!proof->certified)
          return fail(p, representation == 0 ? Why::raw_containment
                                             : Why::quantized_containment);
      }
    }
  }
  p.source.reset();
  p.triangle.reset();
  p.strip.reset();
  p.vertex.reset();
  p.axis.reset();
  p.quantized = false;
  p.work.constructors_complete = true;
  p.complete = true;
  return true;
}
[[gnu::noinline]] auto construct(const View& v, const Limits& caps,
                                 Capsules& out, Proof& p) -> bool {
  p.source = source_index;
  p.work.arithmetic_supported = environment();
  if (!p.work.arithmetic_supported) return fail(p, Why::unsupported_arithmetic);
  return metadata(v, caps, p) && coordinates(v, caps, p) && radii(v, caps, p) &&
         segments(v, caps, out, p) && inclusion(v, caps, out, p);
}
[[gnu::noinline]] auto constructor_error(const Proof& p) -> std::string {
  return "Hatch seal refused condition=" +
         std::to_string(static_cast<unsigned>(p.condition)) +
         " triangle=" + (p.triangle ? std::to_string(*p.triangle) : "none") +
         " strip=" + (p.strip ? std::to_string(*p.strip) : "none") +
         " vertex=" + (p.vertex ? std::to_string(*p.vertex) : "none");
}
} // namespace
struct OriginBoardingHatchSealMaterial::Data {
  NativeCraftBinding binding;
  OriginBoardingInitialMaterial base;
  const OriginBoardingInitialMaterial::Data* base_identity{};
  View prepared;
  Capsules capsules{};
  BoardingHatchSealMaterialSummary summary;
  Data(const NativeCraftBinding& b,
       const OriginBoardingInitialMaterial& material)
      : binding(b), base(material),
        base_identity(detail::BoardingInitialMaterialAccess::data(material)) {}
};
namespace {
auto required_source_bytes(const View& v) -> std::size_t {
  constexpr auto fixed = sizeof(OriginBoardingHatchSealMaterial::Data) + 64;
  return v.storage_bytes > std::numeric_limits<std::size_t>::max() - fixed
             ? std::numeric_limits<std::size_t>::max()
             : v.storage_bytes + fixed;
}
// Include incoming limits, caller/reset evidence, shared control and pending
// return staging in the registered bound, not only the internal proof frame.
static_assert(
    sizeof(OriginBoardingHatchSealMaterial::Data) + 3 * sizeof(Proof) +
        2 * sizeof(Limits) + sizeof(View) + 64 +
        sizeof(std::expected<OriginBoardingHatchSealMaterial, std::string>) +
        4096 <=
    8192);
static_assert(sizeof(Capsules) + sizeof(Proof) +
                  sizeof(std::expected<Proof, std::string>) +
                  2 * sizeof(Limits) + sizeof(View) + 4096 <=
              8192);
} // namespace
OriginBoardingHatchSealMaterial::OriginBoardingHatchSealMaterial(
    std::shared_ptr<const Data> data)
    : data_(std::move(data)) {
}
auto OriginBoardingHatchSealMaterial::summary() const
    -> const BoardingHatchSealMaterialSummary* {
  return data_ ? &data_->summary : nullptr;
}
namespace detail {
auto BoardingHatchSealMaterialAccess::data(
    const OriginBoardingHatchSealMaterial& source)
    -> const OriginBoardingHatchSealMaterial::Data* {
  return source.data_.get();
}
auto BoardingHatchSealMaterialAccess::valid(
    const OriginBoardingHatchSealMaterial& seal,
    const OriginBoardingInitialMaterial& base) -> bool {
  const auto* d = data(seal);
  return d && d->summary.version == kBoardingHatchSealMaterialVersion &&
         d->summary.constructors_complete && d->summary.bindings_complete &&
         d->base_identity &&
         BoardingInitialMaterialAccess::data(base) == d->base_identity &&
         BoardingInitialMaterialAccess::data(d->base) == d->base_identity &&
         initial_material_extension_binding_matches(d->binding, base);
}
auto BoardingHatchSealMaterialAccess::make(
    const NativeCraftBinding& binding,
    const OriginBoardingInitialMaterial& base, Limits caps, Proof* evidence)
    -> std::expected<OriginBoardingHatchSealMaterial, std::string> {
  Proof local;
  auto& p = evidence ? *evidence : local;
  p = {};
  if (!valid_limits(caps)) {
    fail(p, Why::invalid_limits);
    return std::unexpected("Hatch seal lowered registered capacities required");
  }
  const auto view = origin_boarding_hatch_seal_material_prepared();
  p.work.source_bytes = required_source_bytes(view);
  if (p.work.source_bytes > caps.source_bytes) {
    fail(p, Why::source_capacity);
    return std::unexpected("Hatch seal source capacity before allocation");
  }
  if (!initial_material_extension_binding_matches(binding, base)) {
    fail(p, Why::invalid_binding);
    return std::unexpected(
        "Hatch seal genuine same-base native binding required");
  }
  auto owned =
      std::make_shared<OriginBoardingHatchSealMaterial::Data>(binding, base);
  owned->prepared = view;
  if (!construct(view, caps, owned->capsules, p))
    return std::unexpected(constructor_error(p));
  owned->summary = p.work;
  return OriginBoardingHatchSealMaterial{std::move(owned)};
}
auto BoardingHatchSealMaterialAccess::encloser_count(
    const OriginBoardingHatchSealMaterial& seal,
    const OriginBoardingInitialMaterial& base, std::size_t source)
    -> std::size_t {
  return source == source_index && valid(seal, base) ? 8 : 0;
}
auto BoardingHatchSealMaterialAccess::capsule(
    const OriginBoardingHatchSealMaterial& seal,
    const OriginBoardingInitialMaterial& base, std::size_t source,
    std::size_t ordinal) -> const BoardingHatchSealCapsule* {
  return ordinal < encloser_count(seal, base, source)
             ? &data(seal)->capsules[ordinal]
             : nullptr;
}
auto hatch_seal_material_constructor_math(const View& view, Limits caps)
    -> std::expected<Proof, std::string> {
  if (!valid_limits(caps))
    return std::unexpected("Hatch seal lowered registered capacities required");
  Proof p;
  p.work.source_bytes = required_source_bytes(view);
  if (p.work.source_bytes > caps.source_bytes) {
    fail(p, Why::source_capacity);
    return p;
  }
  Capsules capsules;
  static_cast<void>(construct(view, caps, capsules, p));
  return p;
}
auto hatch_seal_material_capsule_inclusion_math(Bounds point, Bounds first,
                                                Bounds second, double radius)
    -> std::expected<MaterialConstructorMathEvidence, std::string> {
  return initial_material_capsule_vertex_math(point, first, second, radius);
}
} // namespace detail
auto make_origin_boarding_hatch_seal_material(
    const NativeCraftBinding& binding,
    const OriginBoardingInitialMaterial& base)
    -> std::expected<OriginBoardingHatchSealMaterial, std::string> {
  return detail::BoardingHatchSealMaterialAccess::make(binding, base, {},
                                                       nullptr);
}
} // namespace apsis_drift
