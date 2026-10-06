#include "apsis_drift/origin_boarding_checkpoint_material_extension.hpp"
#include "origin_boarding_checkpoint_material_extension_internal.hpp"
#include "origin_boarding_checkpoint_material_extension_prepared.hpp"
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
using Access = detail::BoardingCheckpointMaterialExtensionAccess;
using Limits = detail::BoardingCheckpointMaterialExtensionLimits;
using Condition = detail::BoardingCheckpointMaterialExtensionCondition;
using Evidence = detail::BoardingCheckpointMaterialExtensionConstructorMath;
using View = detail::CheckpointMaterialExtensionPreparedView;
using Frame = detail::CheckpointMaterialExtensionFrameRecord;
using Solid = detail::BoardingSourceEndpointSurfaceCheckpointSolidBounds;
using Shape = detail::BoardingSourceEndpointSurfaceCheckpointShape;
using Triangle = std::array<RigidVector3, 3>;
std::size_t checks{};
int failures{};
void check(bool value, std::string_view label) {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
template <class T> auto require(std::expected<T, std::string> result) -> T {
  if (!result)
    throw std::runtime_error("Required extension arithmetic API refused: " +
                             result.error());
  return std::move(*result);
}
auto component(RigidVector3 p, std::size_t i) -> double {
  return i == 0 ? p.x : i == 1 ? p.y : p.z;
}
auto point(RigidVector3 p) -> BoardingPlantedLegPointBounds {
  return {p, p};
}
auto add(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto scale(RigidVector3 p, double s) -> RigidVector3 {
  return {p.x * s, p.y * s, p.z * s};
}
auto dot(RigidVector3 a, RigidVector3 b) -> long double {
  return static_cast<long double>(a.x) * b.x +
         static_cast<long double>(a.y) * b.y +
         static_cast<long double>(a.z) * b.z;
}
auto world(const OperatingTransform& m, RigidVector3 p) -> RigidVector3 {
  return {m.columns[0].x * p.x + m.columns[1].x * p.y + m.columns[2].x * p.z +
              m.columns[3].x,
          m.columns[0].y * p.x + m.columns[1].y * p.y + m.columns[2].y * p.z +
              m.columns[3].y,
          m.columns[0].z * p.x + m.columns[1].z * p.y + m.columns[2].z * p.z +
              m.columns[3].z};
}
auto box(RigidVector3 p, double half = .001) -> Solid {
  return {point(p), point(p), {half, half, half}, 0, Shape::box};
}
template <class T> void no_authority(const T&) {
  check(!T::source_qualified && !T::material_qualified && !T::world_qualified &&
            !T::body_qualified && !T::actor_qualified,
        "Arithmetic evidence cannot enroll source, material, body, WORLD or "
        "actor");
}
void log_evidence(std::string_view label, const Evidence& e) {
  const auto& w = e.work;
  std::cout << label << " complete=" << e.complete
            << " condition=" << static_cast<int>(e.condition)
            << " arithmetic=" << w.arithmetic_supported
            << " bytes=" << w.source_bytes << " work=" << w.base_guards << ','
            << w.vertex_guards << ',' << w.index_guards << ','
            << w.raw_plane_guards << ',' << w.quantized_plane_guards;
  for (const auto& [name, value] : std::array{
           std::pair{"source", e.source}, std::pair{"triangle", e.triangle},
           std::pair{"sector", e.sector}, std::pair{"vertex", e.vertex},
           std::pair{"plane", e.plane}})
    if (value) std::cout << ' ' << name << '=' << *value;
  std::cout << " quantized=" << e.quantized << '\n';
}
void accounting(const Evidence& e, Limits l = {}) {
  no_authority(e);
  const auto& w = e.work;
  check(w.base_guards <= l.base_guards && w.vertex_guards <= l.vertex_guards &&
            w.index_guards <= l.index_guards &&
            w.raw_plane_guards + w.quantized_plane_guards <= l.plane_guards,
        "Actual attempted constructor operations respect each registered cap");
  check(!e.complete || (e.condition == Condition::none && w.bindings_complete &&
                        w.arithmetic_supported && w.constructors_complete),
        "Completion requires arithmetic, binding and constructor guards");
  check(e.complete || !w.constructors_complete,
        "A retained refusal never asserts constructor completeness");
  if (e.triangle)
    check(e.source.has_value(),
          "Refused full triangle retains its original source");
}
struct Clone {
  View view;
  std::array<detail::CheckpointMaterialExtensionSourceRecord, 2> sources;
  std::array<detail::MaterialMeshRecord, 2> meshes;
  Frame frame;
  std::array<std::vector<RigidVector3>, 2> raw;
  std::array<std::vector<detail::MaterialQuantizedPoint>, 2> quantized;
  std::array<std::vector<detail::MaterialTriangle>, 2> triangles;
  explicit Clone(View original) : view(original), frame(*original.frame) {
    if (original.sources.size() != 2 || original.meshes.size() != 2)
      throw std::runtime_error(
          "Pinned extension must contain exactly two source packets");
    std::copy(original.sources.begin(), original.sources.end(),
              sources.begin());
    std::copy(original.meshes.begin(), original.meshes.end(), meshes.begin());
    for (std::size_t i = 0; i < 2; ++i) {
      raw[i].assign(meshes[i].raw_vertices.begin(),
                    meshes[i].raw_vertices.end());
      quantized[i].assign(meshes[i].quantized_vertices.begin(),
                          meshes[i].quantized_vertices.end());
      triangles[i].assign(meshes[i].triangles.begin(),
                          meshes[i].triangles.end());
      meshes[i].raw_vertices = raw[i];
      meshes[i].quantized_vertices = quantized[i];
      meshes[i].triangles = triangles[i];
    }
    view.sources = sources;
    view.meshes = meshes;
    view.frame = &frame;
  }
  Clone(const Clone&) = delete;
  auto operator=(const Clone&) -> Clone& = delete;
};
void prism_controls(const Frame& frame) {
  RigidVector3 cavity{};
  for (auto p : frame.local_vertices)
    cavity = add(cavity, world(frame.corrected_world, p));
  cavity = scale(cavity, 1. / 32.);
  bool cavity_clear = true;
  for (std::size_t sector = 0; sector < 8; ++sector) {
    const auto p = require(
        detail::checkpoint_material_extension_prism_math(frame, sector));
    no_authority(p);
    check(p.arithmetic_supported,
          "Pinned base geometry yields bounded prism arithmetic independent of "
          "evaluated bevel admission");
    if (!p.arithmetic_supported) continue;
    const auto clear = require(
        detail::initial_material_primitive_math(box(cavity), 0, p.raw_planes));
    check(!decltype(clear)::source_qualified &&
              !decltype(clear)::material_qualified &&
              !decltype(clear)::actor_qualified,
          "Cavity exclusion remains arithmetic-only");
    cavity_clear &= clear.certified;
    RigidVector3 center{};
    std::size_t included{};
    for (auto local : frame.local_vertices) {
      const auto v = world(frame.corrected_world, local);
      bool inside = true;
      for (const auto& plane : p.raw_planes)
        inside &= dot(plane.normal, v) <= plane.maximum.upper;
      if (inside) {
        center = add(center, v);
        ++included;
      }
    }
    check(included >= 4,
          "Independent stored-plane evaluations retain base sector vertices");
    if (included) {
      center = scale(center, 1. / static_cast<double>(included));
      const auto band = require(detail::initial_material_primitive_math(
          box(center, .0001), 0, p.raw_planes));
      check(!band.certified,
            "A body centered in material band cannot be excluded as cavity");
      for (const auto& plane : p.raw_planes)
        check(dot(plane.normal, center) <= plane.maximum.upper,
              "Long-double band witness lies inside all actual stored sector "
              "planes");
    }
    for (std::size_t j = 0; j < 6; ++j) {
      const auto& r = p.raw_planes[j];
      const auto& q = p.game_planes[j];
      const auto next = (sector + 1) % 8;
      long double maximum = -std::numeric_limits<long double>::infinity();
      for (auto i : std::array<std::size_t, 8>{
               sector, next, 8 + next, 8 + sector, 16 + sector, 16 + next,
               24 + next, 24 + sector}) {
        const auto v = world(frame.corrected_world, frame.local_vertices[i]);
        maximum = std::max(maximum, dot(r.normal, v));
      }
      const long double support_round_error =
          32.L * std::numeric_limits<double>::epsilon() *
          std::max(1.L, std::abs(maximum));
      check(static_cast<long double>(r.maximum.upper) >= maximum &&
                static_cast<long double>(r.maximum.upper) <=
                    maximum + support_round_error,
            "Independent full base-prism vertex oracle encloses actual "
            "stored-normal support without raw widening");
      check(r.maximum.supported && q.maximum.supported,
            "All six raw and quantized sector support maxima are supported");
      check(std::bit_cast<std::uint64_t>(r.normal.x) ==
                    std::bit_cast<std::uint64_t>(q.normal.x) &&
                std::bit_cast<std::uint64_t>(r.normal.y) ==
                    std::bit_cast<std::uint64_t>(q.normal.y) &&
                std::bit_cast<std::uint64_t>(r.normal.z) ==
                    std::bit_cast<std::uint64_t>(q.normal.z),
            "Quantization changes support allowance rather than stored plane "
            "normals");
      const long double guard =
          1.e-6L * (std::abs(static_cast<long double>(r.normal.x)) +
                    std::abs(static_cast<long double>(r.normal.y)) +
                    std::abs(static_cast<long double>(r.normal.z)));
      check(static_cast<long double>(q.maximum.upper) >=
                static_cast<long double>(r.maximum.upper) + guard,
            "Quantized outward maximum covers the fixed world-axis "
            "one-micrometre cube");
      const long double round_error =
          32.L * std::numeric_limits<double>::epsilon() *
          std::max(1.L, std::abs(static_cast<long double>(q.maximum.upper)));
      check(static_cast<long double>(q.maximum.upper) - r.maximum.upper <=
                guard + round_error,
            "Outward arithmetic corroboration cannot hide a fitted or "
            "raw-bevel allowance");
    }
  }
  check(cavity_clear,
        "Annular aperture remains clear of every material band prism");
  check(!detail::checkpoint_material_extension_prism_math(frame, 8),
        "Past-end sector never creates a plane packet");
  auto malformed = frame;
  malformed.local_vertices[0].x = std::numeric_limits<double>::quiet_NaN();
  check(!detail::checkpoint_material_extension_prism_math(malformed, 0),
        "Nonfinite authored base rejects before enclosure arithmetic");
  const auto solid = box({0, 0, 0}, .1);
  const std::array<Triangle, 6> sheets{
      {{{{-1, -1, -1}, {-1, 1, -1}, {-1, 1, 1}}},
       {{{1, -1, -1}, {1, 1, 1}, {1, 1, -1}}},
       {{{-1, -1, -1}, {1, -1, 1}, {1, -1, -1}}},
       {{{-1, 1, -1}, {1, 1, -1}, {1, 1, 1}}},
       {{{-1, -1, -1}, {1, 1, -1}, {1, -1, -1}}},
       {{{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}}}}};
  for (const auto& t : sheets) {
    const auto separated =
        require(detail::boarding_checkpoint_world_finite_triangle_pair_math(
            solid, 0, t));
    check(separated.certified && !decltype(separated)::world_qualified &&
              !decltype(separated)::body_qualified,
          "A body inside a hollow sheet roster can be clear of each original "
          "face without volume authority");
  }
  std::array<detail::MaterialPlane, 6> hull{};
  for (std::size_t i = 0; i < 6; ++i) {
    if (i / 2 == 0) hull[i].normal.x = i % 2 ? -1 : 1;
    if (i / 2 == 1) hull[i].normal.y = i % 2 ? -1 : 1;
    if (i / 2 == 2) hull[i].normal.z = i % 2 ? -1 : 1;
    hull[i].maximum = {1, 1, true};
  }
  check(!require(detail::initial_material_primitive_math(solid, 0, hull))
             .certified,
        "Replacing clear open sheets with a filled material volume changes the "
        "obligation");
}
void malformed_controls(View view) {
  for (int which = 0; which < 19; ++which) {
    Clone c(view);
    switch (which) {
      case 0: c.view.master_sha256 = "altered"; break;
      case 1: c.view.binary_sha256 = "altered"; break;
      case 2: c.view.metadata_sha256 = "altered"; break;
      case 3: c.view.inventory_sha256 = "altered"; break;
      case 4: c.view.history_helper_sha256 = "altered"; break;
      case 5: c.sources[0].source_index = 1574; break;
      case 6: c.sources[0].identity.original_start += 1; break;
      case 7: c.sources[1].identity.original_count += 1; break;
      case 8: c.sources[0].identity.raw_fingerprint = "altered"; break;
      case 9: c.sources[1].identity.quantized_fingerprint = "altered"; break;
      case 10:
        c.frame.bevel_width = std::nextafter(c.frame.bevel_width, 1.);
        break;
      case 11: c.frame.bevel_segments += 1; break;
      case 12: c.frame.modifiers_json = "[]"; break;
      case 13: c.frame.polygons[0][0] = 32; break;
      case 14:
        c.frame.local_vertices[0].x =
            std::nextafter(c.frame.local_vertices[0].x, 10.);
        break;
      case 15: c.view.nose_properties_json = "altered"; break;
      case 16: c.sources[1].identity.parent = "invented ancestor"; break;
      case 17: c.sources[0].identity.mesh = 0; break;
      case 18:
        c.sources[1].identity.relation =
            BoardingInitialMaterialRelation::service_enclosure;
        break;
    }
    auto r = detail::checkpoint_material_extension_constructor_math(c.view);
    check(!r || !r->complete,
          "Changed hash, crop identity, source mapping, constructor or "
          "modifier cannot complete pinned admission");
    if (r) accounting(*r);
  }
  for (int which = 0; which < 5; ++which) {
    Clone c(view);
    if (which == 0) c.raw[0][0].x = std::numeric_limits<double>::quiet_NaN();
    if (which == 1) c.raw[1][0].z = std::numeric_limits<double>::infinity();
    if (which == 2) c.raw[0][0].y = std::nextafter(8., 9.);
    if (which == 3) c.quantized[1][0].value[0] = 8000001;
    if (which == 4)
      c.triangles[1][0].vertices[0] =
          static_cast<std::uint32_t>(c.raw[1].size());
    auto r = detail::checkpoint_material_extension_constructor_math(c.view);
    check(!r || !r->complete, "Malformed coordinates, decoded workspace and "
                              "full index range never qualify material");
    if (r) accounting(*r);
  }
}
auto quantized_point(RigidVector3 p) -> detail::MaterialQuantizedPoint {
  return {{static_cast<std::int64_t>(std::nearbyint(p.x * 1.e6)),
           static_cast<std::int64_t>(std::nearbyint(p.y * 1.e6)),
           static_cast<std::int64_t>(std::nearbyint(p.z * 1.e6))}};
}
void caps(View view, const Evidence& baseline);
void numerical_constructor_controls(View view) {
  Clone interior(view);
  // Replace only fixture mesh coordinates, retaining fixed full counts and
  // constructor metadata. This arithmetic seam cannot issue a source handle.
  const std::array<std::size_t, 8> sector0{0, 1, 9, 8, 16, 17, 25, 24};
  RigidVector3 center{};
  for (auto i : sector0)
    center = add(center, world(interior.frame.corrected_world,
                               interior.frame.local_vertices[i]));
  center = scale(center, 1. / 8.);
  std::fill(interior.raw[0].begin(), interior.raw[0].end(), center);
  std::fill(interior.quantized[0].begin(), interior.quantized[0].end(),
            quantized_point(center));
  const auto positive = require(
      detail::checkpoint_material_extension_constructor_math(interior.view));
  accounting(positive);
  check(positive.complete && positive.work.raw_plane_guards > 0 &&
            positive.work.quantized_plane_guards > 0,
        "Strict interior numerical triangles earn separate raw and quantized "
        "containment without any authority");
  if (positive.complete) caps(interior.view, positive);
  RigidVector3 cavity{};
  for (auto p : interior.frame.local_vertices)
    cavity = add(cavity, world(interior.frame.corrected_world, p));
  cavity = scale(cavity, 1. / 32.);
  std::fill(interior.raw[0].begin(), interior.raw[0].end(), cavity);
  std::fill(interior.quantized[0].begin(), interior.quantized[0].end(),
            quantized_point(cavity));
  const auto aperture = require(
      detail::checkpoint_material_extension_constructor_math(interior.view));
  accounting(aperture);
  check(!aperture.complete &&
            aperture.condition == Condition::raw_containment &&
            aperture.source == 1436 && aperture.triangle == 0 &&
            !aperture.quantized && aperture.work.quantized_plane_guards == 0,
        "A source face in the aperture fails raw containment before quantized "
        "allowances can repair it");
  const auto left0 =
      world(interior.frame.corrected_world, interior.frame.local_vertices[0]);
  const auto left1 =
      world(interior.frame.corrected_world, interior.frame.local_vertices[1]);
  auto outside = scale(add(left0, left1), .5);
  outside.x -= 1.e-7;
  std::fill(interior.raw[0].begin(), interior.raw[0].end(), outside);
  std::fill(interior.quantized[0].begin(), interior.quantized[0].end(),
            quantized_point(outside));
  const auto raw_outside = require(
      detail::checkpoint_material_extension_constructor_math(interior.view));
  accounting(raw_outside);
  check(!raw_outside.complete &&
            raw_outside.condition == Condition::raw_containment &&
            !raw_outside.quantized,
        "Sub-micrometre raw protrusion still refuses even when the quantized "
        "cube could cover the decoded point");
  bool quantized_inside = false;
  const auto qp = quantized_point(outside);
  const RigidVector3 decoded{static_cast<double>(qp.value[0]) * 1.e-6,
                             static_cast<double>(qp.value[1]) * 1.e-6,
                             static_cast<double>(qp.value[2]) * 1.e-6};
  for (std::size_t s = 0; s < 8; ++s) {
    const auto planes = require(
        detail::checkpoint_material_extension_prism_math(interior.frame, s));
    bool contained = true;
    for (const auto& plane : planes.game_planes)
      contained &= dot(decoded, plane.normal) <= plane.maximum.lower;
    quantized_inside |= contained;
  }
  check(quantized_inside,
        "Independent long-double decoded point corroborates that q allowance "
        "never substitutes for raw containment");
  // Every vertex of this triangle is strictly inside some band sector, but
  // opposite frame sides cannot share one convex sector. Union-only vertex
  // membership is insufficient for an entire material face.
  std::fill(interior.raw[0].begin(), interior.raw[0].end(), center);
  std::fill(interior.quantized[0].begin(), interior.quantized[0].end(),
            quantized_point(center));
  RigidVector3 opposite{};
  for (auto i : std::array<std::size_t, 8>{6, 7, 15, 14, 22, 23, 31, 30})
    opposite = add(opposite, world(interior.frame.corrected_world,
                                   interior.frame.local_vertices[i]));
  opposite = scale(opposite, 1. / 8.);
  const auto index = interior.triangles[0][0].vertices[1];
  interior.raw[0][index] = opposite;
  interior.quantized[0][index] = quantized_point(opposite);
  const auto seam = require(
      detail::checkpoint_material_extension_constructor_math(interior.view));
  accounting(seam);
  check(!seam.complete && seam.condition == Condition::raw_containment &&
            seam.triangle == 0,
        "A face spanning aperture sectors refuses despite separate per-vertex "
        "membership");
}
void caps(View view, const Evidence& baseline) {
  accounting(baseline);
  for (int which = 0; which < 5; ++which) {
    Limits l{};
    if (which == 0) ++l.source_bytes;
    if (which == 1) ++l.base_guards;
    if (which == 2) ++l.vertex_guards;
    if (which == 3) ++l.index_guards;
    if (which == 4) ++l.plane_guards;
    check(!detail::checkpoint_material_extension_constructor_math(view, l),
          "Private control cannot increase a registered resource ceiling");
  }
  const std::array<std::uint64_t, 5> used{
      {baseline.work.source_bytes, baseline.work.base_guards,
       baseline.work.vertex_guards, baseline.work.index_guards,
       baseline.work.raw_plane_guards + baseline.work.quantized_plane_guards}};
  const std::array<Condition, 5> conditions{
      {Condition::source_capacity, Condition::base_capacity,
       Condition::vertex_capacity, Condition::index_capacity,
       Condition::plane_capacity}};
  const auto set = [](Limits& l, int which, std::uint64_t n) {
    switch (which) {
      case 0: l.source_bytes = static_cast<std::size_t>(n); break;
      case 1: l.base_guards = n; break;
      case 2: l.vertex_guards = n; break;
      case 3: l.index_guards = n; break;
      case 4: l.plane_guards = n; break;
    }
  };
  const auto consumed = [](const Evidence& e, int which) -> std::uint64_t {
    switch (which) {
      case 0: return e.work.source_bytes;
      case 1: return e.work.base_guards;
      case 2: return e.work.vertex_guards;
      case 3: return e.work.index_guards;
      default: return e.work.raw_plane_guards + e.work.quantized_plane_guards;
    }
  };
  for (int which = 0; which < 5; ++which) {
    if (used[static_cast<std::size_t>(which)] == 0) continue;
    Limits l{};
    set(l, which, 0);
    const auto zero =
        detail::checkpoint_material_extension_constructor_math(view, l);
    check(zero.has_value(),
          "Reached lowered budget returns retained constructor evidence");
    if (zero) {
      accounting(*zero, l);
      check(!zero->complete &&
                zero->condition ==
                    conditions[static_cast<std::size_t>(which)] &&
                consumed(*zero, which) == (which == 0 ? used[0] : 0),
            "Zero reached cap stops before operation or allocation with exact "
            "capacity cause");
    }
    l = {};
    set(l, which, used[static_cast<std::size_t>(which)]);
    const auto exact =
        detail::checkpoint_material_extension_constructor_math(view, l);
    check(exact.has_value(),
          "Exact actual budget replays the observed constructor");
    if (exact) {
      accounting(*exact, l);
      check(exact->complete == baseline.complete &&
                exact->condition == baseline.condition &&
                exact->source == baseline.source &&
                exact->triangle == baseline.triangle &&
                exact->sector == baseline.sector &&
                exact->vertex == baseline.vertex &&
                exact->plane == baseline.plane &&
                exact->quantized == baseline.quantized &&
                consumed(*exact, which) ==
                    used[static_cast<std::size_t>(which)],
            "Exact actual budget preserves arithmetic result and "
            "source-attributed refusal");
    }
    set(l, which, used[static_cast<std::size_t>(which)] - 1);
    const auto less =
        detail::checkpoint_material_extension_constructor_math(view, l);
    check(less.has_value(), "One-less reached budget returns retained refusal");
    if (less) {
      accounting(*less, l);
      check(!less->complete &&
                less->condition ==
                    conditions[static_cast<std::size_t>(which)] &&
                consumed(*less, which) ==
                    (which == 0 ? used[0]
                                : used[static_cast<std::size_t>(which)] - 1),
            "One-less actual budget stops at next operation without masked "
            "geometry permission");
    }
  }
}
void unsafe(View view, const NativeCraftBinding& binding,
            const OriginBoardingInitialMaterial& base) {
  const auto inspect = [&] {
    const auto r = detail::checkpoint_material_extension_constructor_math(view);
    check(!r || (!r->work.arithmetic_supported && !r->complete),
          "Unsafe environment cannot complete source constructor arithmetic");
    if (r) no_authority(*r);
    check(!make_origin_boarding_checkpoint_material_extension(binding, base),
          "Unsafe environment cannot mint a genuine extension");
  };
  const int old = std::fegetround();
  for (int mode : std::array{FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
    if (std::fesetround(mode) == 0) inspect();
  }
  check(std::fesetround(old) == 0,
        "Restore original rounding before later observations");
#if defined(__SSE2__) && defined(__x86_64__)
  const auto saved = _mm_getcsr();
  for (auto bit : std::array{0x8000u, 0x40u, 0x2000u}) {
    _mm_setcsr(saved | bit);
    inspect();
    _mm_setcsr(saved);
  }
#endif
}
void issuer_caps(const NativeCraftBinding& binding,
                 const OriginBoardingInitialMaterial& base,
                 const Evidence& baseline) {
  const std::array<std::uint64_t, 5> used{
      baseline.work.source_bytes, baseline.work.base_guards,
      baseline.work.vertex_guards, baseline.work.index_guards,
      baseline.work.raw_plane_guards + baseline.work.quantized_plane_guards};
  const std::array<Condition, 5> causes{
      Condition::source_capacity, Condition::base_capacity,
      Condition::vertex_capacity, Condition::index_capacity,
      Condition::plane_capacity};
  for (std::size_t i = 0; i < 5; ++i) {
    if (used[i] == 0) continue;
    for (int mode = 0; mode < 3; ++mode) {
      Limits l{};
      const auto n = mode == 0 ? 0 : mode == 1 ? used[i] : used[i] - 1;
      switch (i) {
        case 0: l.source_bytes = static_cast<std::size_t>(n); break;
        case 1: l.base_guards = n; break;
        case 2: l.vertex_guards = n; break;
        case 3: l.index_guards = n; break;
        case 4: l.plane_guards = n; break;
        default: break;
      }
      Evidence e;
      const auto r = Access::make(binding, base, l, &e);
      accounting(e, l);
      if (mode == 1) {
        check(r.has_value() == baseline.complete &&
                  e.condition == baseline.condition &&
                  e.source == baseline.source &&
                  e.triangle == baseline.triangle &&
                  e.sector == baseline.sector && e.vertex == baseline.vertex &&
                  e.plane == baseline.plane &&
                  e.quantized == baseline.quantized,
              "Exact authentic allocation/work cap replays observed admission "
              "or retained constructor refusal");
      } else
        check(!r && !e.complete && e.condition == causes[i],
              "Actual issuer zero/one-less reached budget refuses before next "
              "operation without minting a handle");
    }
  }
  Evidence stale;
  stale.complete = true;
  stale.work.bindings_complete = true;
  stale.work.arithmetic_supported = true;
  stale.work.constructors_complete = true;
  const auto invalid = Access::make(NativeCraftBinding{}, base, {}, &stale);
  check(!invalid && !stale.complete && !stale.work.bindings_complete &&
            !stale.work.constructors_complete &&
            stale.condition == Condition::invalid_binding,
        "Invalid issuer clears stale source-completion flags and retains the "
        "binding refusal");
}
auto summary_bits(const BoardingInitialMaterialSourceSummary& s)
    -> std::array<std::uint64_t, 12> {
  return {
      s.original_objects,         s.effective_objects, s.fixed_objects,
      s.moving_objects,           s.removed_objects,   s.replacement_objects,
      s.absent_crop_objects,      s.source_bytes,      s.ring_inclusions,
      s.support_plane_inclusions, s.bindings_complete, s.constructors_complete};
}
void admitted_controls(const NativeCraftBinding& binding,
                       const OriginBoardingInitialMaterial& base,
                       const OriginBoardingCheckpointMaterialExtension& cap,
                       View view) {
  check(Access::valid(cap, base), "Only retained extension and its owned old "
                                  "material form a valid source binding");
  const auto different = make_origin_boarding_initial_material(binding);
  check(different.has_value(),
        "Unchanged independent old material admission remains available");
  if (different)
    check(!Access::valid(cap, *different) &&
              !Access::mesh(cap, *different, 1436) &&
              Access::planes(cap, *different, 1436, 0).empty(),
          "Separately issued equal source cannot spoof retained base "
          "capability identity");
  const auto* summary = cap.summary();
  check(summary && summary->bindings_complete &&
            summary->arithmetic_supported && summary->constructors_complete,
        "A genuinely issued extension retains completed source constructor "
        "evidence");
  if (summary)
    check(summary->sources == 2 && summary->vertices == 3408 &&
              summary->triangles == 5699 && summary->prisms == 8 &&
              summary->source_bytes <= 262144,
          "Issued extension retains all complete full meshes and eight "
          "aperture-preserving prisms");
  for (std::size_t source = 0; source < 1759; ++source) {
    const auto relation = Access::relation(cap, base, source);
    check(relation ==
              (source == 1436
                   ? detail::BoardingCheckpointMaterialExtensionRelation::
                         frame_annulus
               : source == 1574
                   ? detail::BoardingCheckpointMaterialExtensionRelation::
                         retained_cut_skin
                   : detail::BoardingCheckpointMaterialExtensionRelation::none),
          "Exactly two fixed original source indices acquire explicit new "
          "material relations");
  }
  for (std::size_t i = 0; i < 2; ++i) {
    const auto source = view.sources[i].source_index;
    const auto* mesh = Access::mesh(cap, base, source);
    check(mesh && mesh->triangles.size() == (source == 1436 ? 512u : 5187u) &&
              mesh->raw_vertices.size() == (source == 1436 ? 256u : 3152u),
          "Full authenticated mesh remains distinct from smaller native crop");
    if (!mesh) continue;
    for (std::size_t ordinal : std::array<std::size_t, 3>{
             0, mesh->triangles.size() / 2, mesh->triangles.size() - 1}) {
      bool collapsed = false;
      const auto tri = Access::triangle(cap, base, source, ordinal, collapsed);
      check(tri.has_value(),
            "Full evaluated ordinal exposes genuine immutable source triangle");
      if (tri)
        for (std::size_t v = 0; v < 3; ++v)
          for (std::size_t a = 0; a < 3; ++a) {
            const auto index = mesh->triangles[ordinal].vertices[v];
            const double decoded =
                static_cast<double>(mesh->quantized_vertices[index].value[a]) *
                1.e-6;
            check(std::bit_cast<std::uint64_t>(component((*tri)[v], a)) ==
                      std::bit_cast<std::uint64_t>(decoded),
                  "Full ordinal decodes its own original indices exactly once");
          }
    }
    bool collapsed = true;
    check(
        !Access::triangle(cap, base, source, mesh->triangles.size(), collapsed),
        "Past-end full ordinal cannot fall back to a native crop key");
  }
  for (std::size_t s = 0; s < 8; ++s)
    check(
        Access::planes(cap, base, 1436, s).size() == 6,
        "Genuine frame binds exactly six outward planes for every band sector");
  check(Access::planes(cap, base, 1436, 8).empty() &&
            Access::planes(cap, base, 1574, 0).empty(),
        "Sheet source and past-end annular sector expose no filled primitive");
  auto copy = cap;
  auto retained = std::move(copy);
  // NOLINTBEGIN(bugprone-use-after-move) -- The immutable extension explicitly
  // supports empty moved-from handles.
  check(!copy.summary() && !Access::valid(copy, base) &&
            !Access::mesh(copy, base, 1436) &&
            Access::planes(copy, base, 1436, 0).empty(),
        "Moved extension cannot retain borrowed source authority");
  // NOLINTEND(bugprone-use-after-move) -- End moved-from empty-handle contract.
  check(Access::valid(retained, base),
        "Retained extension survives original handle movement");
  auto base_copy = base;
  auto base_retained = std::move(base_copy);
  // NOLINTBEGIN(bugprone-use-after-move) -- Empty moved-from base handles must
  // invalidate extension bindings.
  check(!Access::valid(cap, base_copy) &&
            !make_origin_boarding_checkpoint_material_extension(binding,
                                                                base_copy),
        "Moved old material cannot authenticate new extension");
  // NOLINTEND(bugprone-use-after-move) -- End moved-from base contract.
  check(Access::valid(cap, base_retained),
        "Copies preserve genuine immutable base identity");
}
} // namespace
int main() {
  try {
    const auto view =
        detail::origin_boarding_checkpoint_material_extension_prepared();
    if (!view.frame)
      throw std::runtime_error(
          "Immutable extension provider has no frame constructor");
    prism_controls(*view.frame);
    const auto binding_result = make_native_starting_assembly_binding(
        NativeStartingAssemblySelection{});
    if (!binding_result) throw std::runtime_error(binding_result.error());
    const auto& binding = *binding_result;
    const auto base_result = make_origin_boarding_initial_material(binding);
    if (!base_result) throw std::runtime_error(base_result.error());
    const auto& base = *base_result;
    const auto before = summary_bits(*base.summary());
    check(!make_origin_boarding_checkpoint_material_extension(
              NativeCraftBinding{}, base),
          "Unbound craft cannot issue extension");
    auto wrong_selection = NativeStartingAssemblySelection{};
    wrong_selection.hardware.seat_boarding = 0;
    const auto wrong = make_native_starting_assembly_binding(wrong_selection);
    check(!wrong ||
              !make_origin_boarding_checkpoint_material_extension(*wrong, base),
          "Changed hardware cannot issue pinned extension");
    // Preserve the registered FIRST log ordering. Both original compiler logs
    // are retained; the observed raw-containment refusal is mandatory below.
    const auto admission =
        make_origin_boarding_checkpoint_material_extension(binding, base);
    std::cout << "FIRST_PUBLIC_CHECKPOINT_MATERIAL_EXTENSION accepted="
              << admission.has_value();
    if (!admission) std::cout << " error=" << admission.error();
    std::cout << '\n' << std::flush;
    Evidence evidence;
    const auto detailed = Access::make(binding, base, {}, &evidence);
    log_evidence("FIRST_CHECKPOINT_MATERIAL_EXTENSION_CONSTRUCTOR", evidence);
    check(!admission && !detailed && !evidence.complete &&
              evidence.condition == Condition::raw_containment &&
              evidence.source == 1436 && evidence.triangle == 2 &&
              evidence.sector == 7 && evidence.vertex == 3 &&
              evidence.plane == 3 && !evidence.quantized,
          "Frozen FrameAnnulus01 refuses the same original raw face and last "
          "attempted sector/vertex/plane without issuing source authority");
    check(evidence.work.base_guards == 791 &&
              evidence.work.vertex_guards == 20448 &&
              evidence.work.index_guards == 17097 &&
              evidence.work.raw_plane_guards == 80 &&
              evidence.work.quantized_plane_guards == 36 &&
              evidence.work.bindings_complete &&
              evidence.work.arithmetic_supported &&
              !evidence.work.constructors_complete,
          "Frozen constructor refusal retains exact completed guards and "
          "separate raw/quantized attempted work");
    check(admission.has_value() == detailed.has_value(),
          "Public and authentic detailed issuer preserve the same first "
          "observed admission");
    accounting(evidence);
    issuer_caps(binding, base, evidence);
    const auto numeric =
        detail::checkpoint_material_extension_constructor_math(view);
    check(numeric.has_value(),
          "Immutable arithmetic constructor returns honest success or "
          "source-attributed refusal evidence");
    if (numeric) {
      log_evidence("IMMUTABLE_EXTENSION_CONSTRUCTOR_MATH", *numeric);
      check(numeric->complete == evidence.complete &&
                numeric->condition == evidence.condition &&
                numeric->source == evidence.source &&
                numeric->triangle == evidence.triangle &&
                numeric->sector == evidence.sector &&
                numeric->vertex == evidence.vertex &&
                numeric->plane == evidence.plane &&
                numeric->quantized == evidence.quantized &&
                numeric->work.base_guards == evidence.work.base_guards &&
                numeric->work.vertex_guards == evidence.work.vertex_guards &&
                numeric->work.index_guards == evidence.work.index_guards &&
                numeric->work.raw_plane_guards ==
                    evidence.work.raw_plane_guards &&
                numeric->work.quantized_plane_guards ==
                    evidence.work.quantized_plane_guards,
            "Immutable arithmetic fixture preserves the frozen authentic "
            "constructor refusal and actual work without source authority");
      caps(view, *numeric);
    }
    malformed_controls(view);
    numerical_constructor_controls(view);
    unsafe(view, binding, base);
    if (admission) admitted_controls(binding, base, *admission, view);
    check(summary_bits(*base.summary()) == before,
          "Extension admission and controls preserve every old material "
          "summary field");
    check(base.source_name(1436) == "WF02 | CABIN emergency pressure frame" &&
              base.source_name(1574) == "WF02 | retained nose joint backing",
          "Original source names and identities remain owned by unchanged base "
          "capability");
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
