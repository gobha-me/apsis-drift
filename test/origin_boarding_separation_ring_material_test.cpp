#include "apsis_drift/origin_boarding_separation_ring_material.hpp"
#include "origin_boarding_separation_ring_material_internal.hpp"
#include "origin_boarding_separation_ring_material_prepared.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
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
using View = detail::SeparationRingMaterialPreparedView;
std::size_t checks{};
int failures{};
void check(bool p, std::string_view why) {
  ++checks;
  if (!p) {
    ++failures;
    std::cerr << "FAIL: " << why << '\n';
  }
}
template <class T> auto require(std::expected<T, std::string> r) -> T {
  if (!r)
    throw std::runtime_error("Required ring arithmetic API refused: " +
                             r.error());
  return std::move(*r);
}
template <class T> void no_authority(const T&) {
  check(!T::source_qualified && !T::material_qualified && !T::world_qualified &&
            !T::body_qualified && !T::actor_qualified,
        "Arithmetic ring fixture cannot issue source/material/WORLD/body/actor "
        "authority");
}
struct Clone {
  View view;
  detail::MaterialSourceRecord source;
  detail::MaterialMeshRecord mesh;
  detail::SeparationRingSweepConstructorRecord constructor;
  std::vector<RigidVector3> raw;
  std::vector<detail::MaterialQuantizedPoint> grid;
  std::vector<detail::MaterialTriangle> triangles;
  explicit Clone(View v)
      : view(v), source(*v.source), mesh(*v.mesh), constructor(*v.constructor),
        raw(mesh.raw_vertices.begin(), mesh.raw_vertices.end()),
        grid(mesh.quantized_vertices.begin(), mesh.quantized_vertices.end()),
        triangles(mesh.triangles.begin(), mesh.triangles.end()) {
    mesh.raw_vertices = raw;
    mesh.quantized_vertices = grid;
    mesh.triangles = triangles;
    view.source = &source;
    view.mesh = &mesh;
    view.constructor = &constructor;
  }
  Clone(const Clone&) = delete;
  auto operator=(const Clone&) -> Clone& = delete;
};
auto component(RigidVector3 p, std::size_t a) -> double {
  return a == 0 ? p.x : a == 1 ? p.y : p.z;
}
auto point(RigidVector3 p) -> BoardingPlantedLegPointBounds {
  return {p, p};
}
auto world(const OperatingTransform& m, RigidVector3 p) -> RigidVector3 {
  return {m.columns[0].x * p.x + m.columns[1].x * p.y + m.columns[2].x * p.z +
              m.columns[3].x,
          m.columns[0].y * p.x + m.columns[1].y * p.y + m.columns[2].y * p.z +
              m.columns[3].y,
          m.columns[0].z * p.x + m.columns[1].z * p.y + m.columns[2].z * p.z +
              m.columns[3].z};
}
auto segment_distance_squared(RigidVector3 p, RigidVector3 a, RigidVector3 b)
    -> long double {
  const std::array<long double, 3> d{static_cast<long double>(b.x) - a.x,
                                     static_cast<long double>(b.y) - a.y,
                                     static_cast<long double>(b.z) - a.z};
  const std::array<long double, 3> v{static_cast<long double>(p.x) - a.x,
                                     static_cast<long double>(p.y) - a.y,
                                     static_cast<long double>(p.z) - a.z};
  long double dot{}, length{};
  for (std::size_t i = 0; i < 3; ++i) {
    dot += v[i] * d[i];
    length += d[i] * d[i];
  }
  const auto t = std::clamp(dot / length, 0.L, 1.L);
  long double square{};
  for (std::size_t i = 0; i < 3; ++i) {
    const auto q = v[i] - t * d[i];
    square += q * q;
  }
  return square;
}
using Access = detail::BoardingSeparationRingMaterialAccess;
using Limits = detail::BoardingSeparationRingMaterialLimits;
using Evidence = detail::BoardingSeparationRingMaterialConstructorMath;
using Condition = detail::BoardingSeparationRingMaterialCondition;
void log(std::string_view label, const Evidence& e) {
  const auto& w = e.work;
  std::cout << std::setprecision(17) << label << " complete=" << e.complete
            << " condition=" << static_cast<unsigned>(e.condition)
            << " arithmetic=" << w.arithmetic_supported
            << " bytes=" << w.source_bytes << " radius=" << w.raw_radius << ','
            << w.game_radius << " work=" << w.base_guards << ','
            << w.coordinate_guards << ',' << w.bound_guards << ','
            << w.index_guards << ',' << w.strip_guards << ','
            << w.raw_inclusions << ',' << w.quantized_inclusions << ','
            << w.segments;
  for (const auto& [name, n] : std::array{
           std::pair{"source", e.source}, std::pair{"triangle", e.triangle},
           std::pair{"strip", e.strip}, std::pair{"vertex", e.vertex},
           std::pair{"axis", e.axis}})
    if (n) std::cout << ' ' << name << '=' << *n;
  std::cout << " quantized=" << e.quantized << '\n' << std::flush;
}
void accounting(const Evidence& e, Limits l = {}) {
  no_authority(e);
  const auto& w = e.work;
  check(w.base_guards <= l.base_guards &&
            w.coordinate_guards <= l.coordinate_guards &&
            w.bound_guards <= l.bound_guards &&
            w.index_guards <= l.index_guards &&
            w.strip_guards <= l.strip_guards &&
            w.raw_inclusions + w.quantized_inclusions <= l.inclusions &&
            w.segments <= l.segments,
        "Every charged constructor operation remains within its independent "
        "actual cap");
  check(!e.complete ||
            (e.condition == Condition::none && w.arithmetic_supported &&
             w.bindings_complete && w.constructors_complete),
        "Complete arithmetic constructor requires all "
        "source/settings/inclusion obligations");
  check(e.complete || !w.constructors_complete,
        "Retained constructor refusal denies completion");
}
void inclusion_controls() {
  const auto a = point({0, 0, 0}), b = point({1, 0, 0});
  const auto inspect = [&](RigidVector3 p, double radius, bool expected) {
    const auto e =
        require(detail::separation_ring_material_capsule_inclusion_math(
            point(p), a, b, radius));
    check(!decltype(e)::source_qualified && !decltype(e)::material_qualified &&
              !decltype(e)::actor_qualified,
          "Raw inclusion does not enroll a source, filled material or actor");
    const auto d = segment_distance_squared(p, {0, 0, 0}, {1, 0, 0});
    check((d < static_cast<long double>(radius) * radius) == expected,
          "Independent long-double clamped segment distance corroborates "
          "strict fixture placement");
    check(e.arithmetic_supported && e.certified == expected,
          "Outward inclusion respects interior and exterior of the closed "
          "capsule");
  };
  inspect({.5, .0625, 0}, .125, true);
  inspect({-.0625, 0, 0}, .125, true);
  inspect({1.0625, 0, 0}, .125, true);
  inspect({.5, .25, 0}, .125, false);
  inspect({-.25, 0, 0}, .125, false);
  inspect({1.25, 0, 0}, .125, false);
  const auto touch =
      require(detail::separation_ring_material_capsule_inclusion_math(
          point({.5, .125, 0}), a, b, .125));
  check(touch.arithmetic_supported,
        "Exact boundary remains supported arithmetic without source authority");
  const auto raw_fail =
      require(detail::separation_ring_material_capsule_inclusion_math(
          point({.5, .13, 0}), a, b, .125));
  const auto grid_only =
      require(detail::separation_ring_material_capsule_inclusion_math(
          point({.5, .13, 0}), a, b, .14));
  check(!raw_fail.certified && grid_only.certified,
        "A larger separate grid radius cannot rescue a failed raw inclusion "
        "obligation");
  const auto wide =
      require(detail::separation_ring_material_capsule_inclusion_math(
          {{.5, 0, 0}, {.5, .25, 0}}, a, b, .125));
  check(!wide.certified, "A point interval crossing the capsule boundary "
                         "cannot be accepted by its midpoint");
  check(
      !detail::separation_ring_material_capsule_inclusion_math(point({0, 0, 0}),
                                                               a, b, 0) &&
          !detail::separation_ring_material_capsule_inclusion_math(
              point({0, 0, 0}), a, b,
              std::numeric_limits<double>::infinity()) &&
          !detail::separation_ring_material_capsule_inclusion_math(
              point({0, std::numeric_limits<double>::quiet_NaN(), 0}), a, b,
              .125) &&
          !detail::separation_ring_material_capsule_inclusion_math(
              {{1, 0, 0}, {0, 0, 0}}, a, b, .125),
      "Nonfinite, unordered and zero-radius raw inputs refuse before geometry");
}
void malformed(View v) {
  for (std::size_t i = 0; i < 31; ++i) {
    Clone c(v);
    switch (i) {
      case 0: c.view.source = nullptr; break;
      case 1: c.view.mesh = nullptr; break;
      case 2: c.view.constructor = nullptr; break;
      case 3: c.view.metadata_sha256 = "changed"; break;
      case 4: c.view.binary_sha256 = "changed"; break;
      case 5: c.source.raw_fingerprint = "changed"; break;
      case 6: c.source.quantized_fingerprint = "changed"; break;
      case 7: c.source.motion_group = 0; break;
      case 8: c.source.parent = "foreign owner"; break;
      case 9: c.source.original_count = 141; break;
      case 10: c.constructor.settings_json = "{}"; break;
      case 11: c.constructor.spline_json = "cyclic/profile/taper"; break;
      case 12: c.constructor.modifiers_json = "[{\"type\":\"BEVEL\"}]"; break;
      case 13: c.constructor.properties_json = "changed"; break;
      case 14: c.constructor.corrected_world.columns[3].x = .01; break;
      case 15: c.constructor.corrected_world.columns[0].x = -1; break;
      case 16:
        c.constructor.bevel_depth =
            std::nextafter(c.constructor.bevel_depth, 0.);
        break;
      case 17:
        c.constructor.local_points[1] = c.constructor.local_points[0];
        break;
      case 18: c.constructor.local_points[8].x += .001; break;
      case 19:
        c.constructor.local_points[2].z =
            std::numeric_limits<double>::quiet_NaN();
        break;
      case 20:
        c.mesh.raw_vertices = std::span(c.raw).first(c.raw.size() - 1);
        break;
      case 21:
        c.mesh.quantized_vertices = std::span(c.grid).first(c.grid.size() - 1);
        break;
      case 22:
        c.mesh.triangles = std::span(c.triangles).first(c.triangles.size() - 1);
        break;
      case 23: c.triangles[0].vertices[0] = 90; break;
      case 24: c.triangles[0].vertices[0] = 80; break;
      case 25: c.raw[0].x = std::numeric_limits<double>::infinity(); break;
      case 26:
        c.grid[0].value[0] = std::numeric_limits<std::int64_t>::max();
        break;
      case 27:
        c.source.raw_bounds.lower.x = std::numeric_limits<double>::quiet_NaN();
        break;
      case 28: c.view.master_sha256 = "changed"; break;
      case 29: c.source.original_object = 741; break;
      case 30:
        c.source.relation = BoardingInitialMaterialRelation::service_enclosure;
        break;
      default: break;
    }
    const auto e = detail::separation_ring_material_constructor_math(c.view);
    check(!e || !e->complete, "Malformed metadata/settings/ring/strip/buffer "
                              "packet cannot complete arithmetic constructor");
    if (e) accounting(*e);
  }
  for (std::size_t a = 0; a < 3; ++a) {
    Clone c(v);
    c.grid[0].value[a] += 100;
    const auto e = detail::separation_ring_material_constructor_math(c.view);
    check(!e || !e->complete,
          "Changed raw/grid mapping is rejected independently of game padding");
  }
}
void unsafe(View view, const NativeCraftBinding& binding,
            const OriginBoardingInitialMaterial& base) {
  const auto inspect = [&] {
    const auto math = detail::separation_ring_material_constructor_math(view);
    check(!math || (!math->complete && !math->work.arithmetic_supported),
          "Unsafe environment cannot grant constructor completeness");
    const auto p = detail::separation_ring_material_capsule_inclusion_math(
        point({.5, 0, 0}), point({0, 0, 0}), point({1, 0, 0}), .125);
    check(!p || (!p->arithmetic_supported && !p->certified),
          "Unsafe environment refuses raw inclusion arithmetic");
    Evidence e;
    check(!Access::make(binding, base, {}, &e) && !e.complete &&
              !e.work.constructors_complete,
          "Genuine issuer cannot override unsafe arithmetic");
  };
  const auto saved = std::fegetround();
  for (int mode : std::array{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO})
    if (std::fesetround(mode) == 0) inspect();
  check(std::fesetround(saved) == 0,
        "Restore rounding before first genuine ring admission");
#if defined(__SSE2__) && defined(__x86_64__)
  const auto control = _mm_getcsr();
  for (auto bit : std::array{0x8000u, 0x40u, 0x2000u}) {
    _mm_setcsr(control | bit);
    inspect();
    _mm_setcsr(control);
  }
#endif
}
struct Cap {
  std::uint64_t Limits::* limit;
  Condition cause;
};
constexpr std::array caps{
    Cap{&Limits::base_guards, Condition::base_capacity},
    Cap{&Limits::coordinate_guards, Condition::coordinate_capacity},
    Cap{&Limits::bound_guards, Condition::bound_capacity},
    Cap{&Limits::index_guards, Condition::index_capacity},
    Cap{&Limits::strip_guards, Condition::strip_capacity},
    Cap{&Limits::inclusions, Condition::inclusion_capacity}};
auto used_work(const Evidence& e) -> std::array<std::uint64_t, 6> {
  return {
      e.work.base_guards,  e.work.coordinate_guards,
      e.work.bound_guards, e.work.index_guards,
      e.work.strip_guards, e.work.raw_inclusions + e.work.quantized_inclusions};
}
auto same_refusal(const Evidence& a, const Evidence& b) -> bool {
  return a.complete == b.complete && a.condition == b.condition &&
         a.source == b.source && a.triangle == b.triangle &&
         a.strip == b.strip && a.vertex == b.vertex && a.axis == b.axis &&
         a.quantized == b.quantized && used_work(a) == used_work(b);
}
void budget_controls(const NativeCraftBinding& binding,
                     const OriginBoardingInitialMaterial& base,
                     const Evidence& baseline) {
  const auto used = used_work(baseline);
  for (std::size_t i = 0; i < caps.size(); ++i) {
    if (used[i] == 0) continue;
    for (int mode = 0; mode < 3; ++mode) {
      Limits l{};
      l.*caps[i].limit = mode == 0 ? 0 : mode == 1 ? used[i] : used[i] - 1;
      Evidence e;
      const auto r = Access::make(binding, base, l, &e);
      accounting(e, l);
      if (mode == 1)
        check(r.has_value() == baseline.complete && same_refusal(e, baseline),
              "Exact actual constructor work replays admission or original "
              "retained refusal");
      else
        check(!r && !e.complete && e.condition == caps[i].cause,
              "Reached zero/one-less constructor work stops before its next "
              "counted operation");
    }
  }
  for (std::size_t which = 0; which < 2; ++which) {
    const auto n =
        which == 0 ? baseline.work.source_bytes : baseline.work.segments;
    if (n == 0) continue;
    for (int mode = 0; mode < 3; ++mode) {
      Limits l{};
      const auto value = mode == 0 ? 0 : mode == 1 ? n : n - 1;
      if (which == 0)
        l.source_bytes = value;
      else
        l.segments = value;
      Evidence e;
      const auto r = Access::make(binding, base, l, &e);
      accounting(e, l);
      if (mode == 1)
        check(r.has_value() == baseline.complete && same_refusal(e, baseline),
              "Exact reached source/segment cap preserves original result");
      else
        check(!r && e.condition == (which == 0 ? Condition::source_capacity
                                               : Condition::segment_capacity),
              "Reached source/segment cap stops before allocation or next "
              "genuine encloser");
    }
  }
  for (std::size_t i = 0; i < caps.size() + 2; ++i) {
    Limits l{};
    if (i < caps.size())
      ++(l.*caps[i].limit);
    else if (i == caps.size())
      ++l.source_bytes;
    else
      ++l.segments;
    Evidence stale = baseline;
    stale.complete = true;
    stale.work.constructors_complete = true;
    check(!Access::make(binding, base, l, &stale) && !stale.complete &&
              !stale.work.constructors_complete &&
              stale.condition == Condition::invalid_limits,
          "Raised registered ceiling resets stale output and refuses before "
          "admission");
  }
}
void admitted_controls(View v, const NativeCraftBinding& binding,
                       const OriginBoardingInitialMaterial& base,
                       const OriginBoardingSeparationRingMaterial& ring,
                       const Evidence& e) {
  check(Access::valid(ring, base),
        "Issued ring retains same-base material identity");
  const auto* summary = ring.summary();
  check(summary && summary->version == 1 && summary->segments == 8 &&
            summary->constructors_complete && summary->bindings_complete &&
            summary->arithmetic_supported,
        "Genuinely issued ring retains exactly eight completed segment "
        "obligations");
  check(e.work.raw_radius > v.constructor->bevel_depth + std::ldexp(1., -20) &&
            e.work.game_radius > e.work.raw_radius + std::sqrt(3.) * 1.e-6,
        "Raw storage and separate game-grid allowances follow fixed outward "
        "radius expressions");
  for (std::size_t ordinal = 0; ordinal < v.mesh->triangles.size(); ++ordinal) {
    const auto& tri = v.mesh->triangles[ordinal];
    std::size_t low = 9, high = 0;
    for (auto index : tri.vertices) {
      low = std::min(low, static_cast<std::size_t>(index / 10));
      high = std::max(high, static_cast<std::size_t>(index / 10));
    }
    check(
        high == low + 1 && high < 9,
        "Every full triangle belongs to two genuine adjacent ten-vertex rings");
    if (high != low + 1 || high >= 9) continue;
    const auto a =
        world(v.constructor->corrected_world, v.constructor->local_points[low]);
    const auto b = world(v.constructor->corrected_world,
                         v.constructor->local_points[high]);
    for (auto index : tri.vertices) {
      const auto raw = v.mesh->raw_vertices[index];
      const auto q = v.mesh->quantized_vertices[index].value;
      const RigidVector3 decoded{static_cast<double>(q[0]) * 1.e-6,
                                 static_cast<double>(q[1]) * 1.e-6,
                                 static_cast<double>(q[2]) * 1.e-6};
      check(segment_distance_squared(raw, a, b) <=
                static_cast<long double>(e.work.raw_radius) * e.work.raw_radius,
            "Independent original raw vertex inclusion corroborates its "
            "genuine strip capsule");
      check(segment_distance_squared(decoded, a, b) <=
                static_cast<long double>(e.work.game_radius) *
                    e.work.game_radius,
            "Independent decoded-grid inclusion corroborates separate "
            "fixed-radius game capsule");
    }
  }
  for (std::size_t source = 0; source < 1759; ++source)
    check(
        Access::encloser_count(ring, base, source) ==
            (source == 1589 ? 8u : 0u),
        "Ring capability adds only source1589 and never fills another source");
  for (std::size_t i = 0; i < 8; ++i) {
    const auto* c = Access::capsule(ring, base, 1589, i);
    check(c && c->radius == e.work.game_radius,
          "Every genuine encloser retains the fixed game radius");
    if (!c) continue;
    const auto a =
        world(v.constructor->corrected_world, v.constructor->local_points[i]);
    const auto b = world(v.constructor->corrected_world,
                         v.constructor->local_points[i + 1]);
    for (std::size_t axis = 0; axis < 3; ++axis) {
      check(component(c->first.lower, axis) <= component(a, axis) &&
                component(c->first.upper, axis) >= component(a, axis) &&
                component(c->second.lower, axis) <= component(b, axis) &&
                component(c->second.upper, axis) >= component(b, axis),
            "Each segment endpoint independently encloses the once-transformed "
            "authentic local point");
    }
  }
  check(!Access::capsule(ring, base, 1589, 8) &&
            !Access::capsule(ring, base, 1436, 0),
        "Past-end and unrelated source cannot borrow a ring encloser");
  const auto different = make_origin_boarding_initial_material(binding);
  check(different.has_value(),
        "Unchanged original material remains independently available");
  if (different)
    check(!Access::valid(ring, *different) &&
              !Access::capsule(ring, *different, 1589, 0),
          "Equal independently issued source cannot spoof original base "
          "identity");
  auto empty = ring;
  auto retained = std::move(empty);
  // NOLINTBEGIN(bugprone-use-after-move) -- Empty moved-from immutable ring
  // handles are a tested public contract.
  check(!empty.summary() && !Access::valid(empty, base) &&
            !Access::capsule(empty, base, 1589, 0),
        "Moved ring loses borrowed material authority");
  // NOLINTEND(bugprone-use-after-move) -- End moved-from ring contract.
  check(Access::valid(retained, base),
        "Retained immutable ring survives movement");
  auto base_empty = base;
  auto base_retained = std::move(base_empty);
  // NOLINTBEGIN(bugprone-use-after-move) -- Empty moved-from original base must
  // invalidate ring binding.
  check(!Access::valid(ring, base_empty) &&
            !Access::make(binding, base_empty, {}),
        "Moved base cannot authenticate ring capability");
  // NOLINTEND(bugprone-use-after-move) -- End moved-from original base
  // contract.
  check(Access::valid(ring, base_retained),
        "Copied source retains immutable original identity");
}
} // namespace
int main() {
  try {
    inclusion_controls();
    const auto view =
        detail::origin_boarding_separation_ring_material_prepared();
    check(view.source && view.mesh && view.constructor,
          "Build-time package supplies bounded immutable records");
    if (!view.source || !view.mesh || !view.constructor)
      throw std::runtime_error("Prepared ring records missing");
    const auto binding_result = make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{});
    if (!binding_result) throw std::runtime_error(binding_result.error());
    const auto& binding = *binding_result;
    const auto base_result = make_origin_boarding_initial_material(binding);
    if (!base_result) throw std::runtime_error(base_result.error());
    const auto& base = *base_result;
    Evidence stale;
    stale.complete = true;
    stale.work.constructors_complete = true;
    stale.work.bindings_complete = true;
    check(!Access::make(NativeCraftBinding{}, base, {}, &stale) &&
              !stale.complete && !stale.work.constructors_complete &&
              stale.condition == Condition::invalid_binding,
          "Wrong hardware/binding clears stale constructor authority before "
          "work");
    const auto public_admission =
        make_origin_boarding_separation_ring_material(binding, base);
    std::cout << "FIRST_PUBLIC_SEPARATION_RING_ISSUER accepted="
              << public_admission.has_value();
    if (!public_admission) std::cout << " error=" << public_admission.error();
    std::cout << '\n' << std::flush;
    Evidence evidence;
    const auto admission = Access::make(binding, base, {}, &evidence);
    std::cout << "FIRST_PUBLIC_SEPARATION_RING_MATERIAL accepted="
              << admission.has_value();
    if (!admission) std::cout << " error=" << admission.error();
    std::cout << '\n' << std::flush;
    log("FIRST_SEPARATION_RING_CONSTRUCTOR", evidence);
    // This distinct genuine constructor outcome is unknown before FIRST.
    accounting(evidence);
    check(public_admission.has_value() == admission.has_value(),
          "Public factory and independently replayed authentic detailed issuer "
          "agree");
    check(admission.has_value() == evidence.complete,
          "Issuer cannot outlive failed constructor proof");
    malformed(view);
    unsafe(view, binding, base);
    budget_controls(binding, base, evidence);
    if (admission)
      admitted_controls(view, binding, base, *admission, evidence);
    else
      std::cout << "SEPARATION_RING_UNAVAILABLE material_observed=0\n"
                << std::flush;
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "Exception: " << e.what() << '\n';
  }
  std::cout << "Separation ring checks=" << checks << " failures=" << failures
            << '\n';
  return failures == 0 ? 0 : 1;
}
