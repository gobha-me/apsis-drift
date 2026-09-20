#include "contact_patch.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <source_location>
#include <stdexcept>
#include <string>
#include <utility>

using namespace apsis_drift;
using namespace apsis_drift::godot_spike;

namespace {
using V = PlanetFixedPositionMetres;
auto check(bool condition, const char* message) -> void {
  if (!condition) throw std::runtime_error(message);
}
template <class T, class E>
auto require(std::expected<T, E> result,
             std::source_location location = std::source_location::current())
    -> T {
  if (!result)
    throw std::runtime_error(
        "fixture API refused input at line " + std::to_string(location.line()) +
        " error " + std::to_string(static_cast<unsigned>(result.error())));
  return std::move(*result);
}
auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto mul(V a, double s) -> V {
  return {a.x * s, a.y * s, a.z * s};
}
auto dot(V a, V b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto norm(V a) -> double {
  return std::hypot(a.x, a.y, a.z);
}
auto unit(V a) -> V {
  const auto r = norm(a);
  return {a.x / r, a.y / r, a.z / r};
}
auto vector(PlanetFixedDirection d) -> V {
  return {d.x, d.y, d.z};
}
auto near(double a, double b, double tolerance, const char* message) -> void {
  check(std::isfinite(a) && std::abs(a - b) <= tolerance, message);
}
auto rotate(RigidOrientation q, V v) -> V {
  // Independent quaternion matrix, not the provider's nested cross products.
  const auto ww = q.w * q.w, xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
  const auto s = ww + xx + yy + zz;
  return {((ww + xx - yy - zz) * v.x + 2 * (q.x * q.y - q.w * q.z) * v.y +
           2 * (q.x * q.z + q.w * q.y) * v.z) /
              s,
          (2 * (q.x * q.y + q.w * q.z) * v.x + (ww - xx + yy - zz) * v.y +
           2 * (q.y * q.z - q.w * q.x) * v.z) /
              s,
          (2 * (q.x * q.z - q.w * q.y) * v.x +
           2 * (q.y * q.z + q.w * q.x) * v.y + (ww - xx - yy + zz) * v.z) /
              s};
}
auto compose(RigidOrientation a, RigidOrientation b) -> RigidOrientation {
  return require(normalize_rigid_orientation(
      {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
       a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
       a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
       a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w}));
}
auto align_up(V normal) -> RigidOrientation {
  // Shortest-arc construction need not initially lie in the provider's
  // deliberate [0.5,2] squared-norm normalization admission envelope.
  const auto w = 1 + normal.y;
  const auto size = std::hypot(w, normal.z, normal.x);
  if (size < 1e-12) return {0, 1, 0, 0};
  return require(normalize_rigid_orientation(
      {w / size, normal.z / size, 0, -normal.x / size}));
}
struct Fixture {
  LocalSystemDescriptor system{generate_local_system(Seed{42})};
  RigidBodyWorldContext context{system};
  TerrainTileCache cache{require(TerrainTileCache::create(8))};
  auto planet() const -> const PlanetDescriptor& {
    return system.planets.front().descriptor;
  }
  auto triangle(unsigned face = 2) -> ContactTriangle {
    return require(build_contact_triangle(
        planet(), kExperimentalContactSurface,
        {planet().id, static_cast<CubeFace>(face), 4123, 3671, 10, 19, 0},
        cache));
  }
  auto state(V center, RigidOrientation q, unsigned support = 0) const
      -> RigidBodyState {
    const auto& craft = starter_shuttle_frame().properties;
    const auto& pad = craft.supports[support];
    V offset{
        (static_cast<double>(pad.contact_mm[0]) - craft.center_of_mass_mm[0]) /
            1000,
        (static_cast<double>(pad.contact_mm[1]) - craft.center_of_mass_mm[1]) /
            1000,
        (static_cast<double>(pad.contact_mm[2]) - craft.center_of_mass_mm[2]) /
            1000};
    const auto com = sub(center, rotate(q, offset));
    RigidBodyState s;
    s.frame = {RigidFrameKind::planet_fixed, system.id, planet().id,
               std::nullopt};
    s.tick = 17;
    s.orientation = q;
    s.position_metres = {com.x, com.y, com.z};
    return require(canonicalize_rigid_body_state(context, s));
  }
  auto certificate(const RigidBodyState& state, unsigned support = 0)
      -> std::expected<ContactPatch, ContactPatchError> {
    return certify_contact_patch(context, state, support,
                                 kExperimentalContactSurface, cache);
  }
};
auto center(const ContactTriangle& t) -> V {
  // Calculate locally to keep the oracle independent of large-sum rounding.
  return add(t.vertices[0], add(mul(sub(t.vertices[1], t.vertices[0]), .3),
                                mul(sub(t.vertices[2], t.vertices[0]), .3)));
}
auto hash(const ContactPatch& patch) -> std::uint64_t {
  std::uint64_t value = 1469598103934665603ULL;
  auto item = [&](std::uint64_t x) {
    for (unsigned i = 0; i < 8; ++i) {
      value ^= (x >> (8 * i)) & 255;
      value *= 1099511628211ULL;
    }
  };
  auto scalar = [&](double x) { item(std::bit_cast<std::uint64_t>(x)); };
  auto point = [&](V x) {
    scalar(x.x);
    scalar(x.y);
    scalar(x.z);
  };
  item(patch.version);
  item(patch.craft.id.value);
  item(patch.craft.version);
  item(static_cast<unsigned>(patch.frame.kind));
  item(patch.frame.system.value);
  item(patch.frame.planet->value);
  item(patch.tick);
  item(patch.support_index);
  const auto& r = patch.triangle.recipe;
  for (auto x : {r.version, r.terrain_generator, r.source_lod, r.relief,
                 r.mesh_lod, r.intervals, r.diagonal})
    item(x);
  const auto& id = patch.triangle.id;
  item(id.planet.value);
  item(static_cast<unsigned>(id.face));
  for (auto x : {id.tile_x, id.tile_y, id.cell_x, id.cell_y, id.half})
    item(x);
  for (auto p : patch.triangle.vertices)
    point(p);
  point(vector(patch.triangle.outward_normal));
  point(patch.pad_center);
  point(vector(patch.half_width));
  point(vector(patch.half_length));
  scalar(patch.minimum_gap_metres);
  scalar(patch.maximum_gap_metres);
  for (auto x : patch.minimum_edge_distances_metres)
    scalar(x);
  return value;
}
auto oracle(const ContactPatch& patch) -> void {
  const auto& t = patch.triangle;
  const auto n = vector(t.outward_normal);
  const auto a = sub(t.vertices[1], t.vertices[0]),
             b = sub(t.vertices[2], t.vertices[0]);
  const auto aa = dot(a, a), ab = dot(a, b), bb = dot(b, b),
             determinant = aa * bb - ab * ab;
  double low = std::numeric_limits<double>::infinity(), high = -low;
  for (int x = -8; x <= 8; ++x)
    for (int y = -8; y <= 8; ++y) {
      const auto p =
          add(patch.pad_center,
              add(mul(vector(patch.half_width), static_cast<double>(x) / 8),
                  mul(vector(patch.half_length), static_cast<double>(y) / 8)));
      const auto relative = sub(p, t.vertices[0]);
      const auto gap = dot(n, relative);
      low = std::min(low, gap);
      high = std::max(high, gap);
      const auto projected = sub(relative, mul(n, gap));
      const auto pa = dot(projected, a), pb = dot(projected, b);
      const auto u = (bb * pa - ab * pb) / determinant,
                 v = (aa * pb - ab * pa) / determinant;
      check(u > 0 && v > 0 && u + v < 1,
            "certificate includes projected point outside triangle");
      check(gap >= patch.minimum_gap_metres && gap <= patch.maximum_gap_metres,
            "whole-rectangle plane-gap enclosure missed an independent point");
    }
  near(patch.minimum_gap_metres, low - kContactPatchAllowanceMetres, 2e-8,
       "minimum is not analytic rectangle extremum with outward allowance");
  near(patch.maximum_gap_metres, high + kContactPatchAllowanceMetres, 2e-8,
       "maximum is not analytic rectangle extremum with outward allowance");
  for (auto margin : patch.minimum_edge_distances_metres)
    check(margin > kContactPatchAllowanceMetres, "non-strict inset accepted");
}
auto malformed(Fixture& f) -> void {
  const auto triangle = f.triangle();
  const auto q = align_up(vector(triangle.outward_normal));
  const auto state = f.state(center(triangle), q);
  auto reject = [&](RigidBodyState s, unsigned support,
                    ContactSurfaceRecipe recipe, ContactPatchError error) {
    auto cache = require(TerrainTileCache::create(1));
    const auto before = s;
    const auto result =
        certify_contact_patch(f.context, s, support, recipe, cache);
    check(!result && result.error() == error,
          "malformed patch request not rejected as expected");
    // Explicit IEEE checks include NaN and signed zero, where operator== is
    // insufficient.
    const std::array original{
        before.position_metres.x, before.position_metres.y,
        before.position_metres.z, before.orientation.w,
        before.orientation.x,     before.orientation.y,
        before.orientation.z};
    const std::array after{s.position_metres.x, s.position_metres.y,
                           s.position_metres.z, s.orientation.w,
                           s.orientation.x,     s.orientation.y,
                           s.orientation.z};
    for (unsigned i = 0; i < original.size(); ++i)
      check(std::bit_cast<std::uint64_t>(original[i]) ==
                std::bit_cast<std::uint64_t>(after[i]),
            "refused query mutated authoritative input bits");
    check(cache.size() == 0, "malformed request accessed triangle cache");
    const auto owned =
        certify_owned_contact_patch(f.context, s, support, recipe, cache);
    check(!owned && owned.error() == error && cache.size() == 0,
          "owned path weakened malformed-state or pre-cache validation");
  };
  auto s = state;
  s.position_metres.x = std::numeric_limits<double>::quiet_NaN();
  reject(s, 0, kExperimentalContactSurface, ContactPatchError::invalid_state);
  s = state;
  s.position_metres.z = std::numeric_limits<double>::infinity();
  reject(s, 0, kExperimentalContactSurface, ContactPatchError::invalid_state);
  s = state;
  s.angular_velocity_radians_per_second.y =
      std::numeric_limits<double>::quiet_NaN();
  reject(s, 0, kExperimentalContactSurface, ContactPatchError::invalid_state);
  s = state;
  s.orientation.w = -s.orientation.w;
  s.orientation.x = -s.orientation.x;
  s.orientation.y = -s.orientation.y;
  s.orientation.z = -s.orientation.z;
  reject(s, 0, kExperimentalContactSurface, ContactPatchError::invalid_state);
  s = state;
  s.orientation = {0, 0, 0, 0};
  reject(s, 0, kExperimentalContactSurface, ContactPatchError::invalid_state);
  s = state;
  s.linear_velocity_metres_per_second.x = -0.0;
  reject(s, 0, kExperimentalContactSurface, ContactPatchError::invalid_state);
  s = state;
  s.frame.system.value ^= 1;
  reject(s, 0, kExperimentalContactSurface, ContactPatchError::invalid_state);
  s = state;
  s.frame.planet = PlanetId{0};
  reject(s, 0, kExperimentalContactSurface, ContactPatchError::invalid_state);
  s = state;
  s.frame.kind = RigidFrameKind::system_inertial;
  s.frame.planet.reset();
  reject(s, 0, kExperimentalContactSurface,
         ContactPatchError::unsupported_frame);
  s = state;
  s.craft.version = 2;
  reject(s, 0, kExperimentalContactSurface, ContactPatchError::invalid_state);
  s = state;
  s.tick = std::numeric_limits<SimulationTick>::max();
  reject(s, 0, kExperimentalContactSurface, ContactPatchError::invalid_state);
  reject(state, 3, kExperimentalContactSurface,
         ContactPatchError::invalid_support);
  reject(state, std::numeric_limits<unsigned>::max(),
         kExperimentalContactSurface, ContactPatchError::invalid_support);
  auto recipe = kExperimentalContactSurface;
  recipe.intervals = 0;
  reject(state, 0, recipe, ContactPatchError::unsupported_recipe);
  s = state;
  s.position_metres = {1e14, 0, 0};
  reject(s, 0, kExperimentalContactSurface,
         ContactPatchError::outside_query_bounds);
  const auto radial = unit(center(triangle));
  s = f.state(mul(radial, f.planet().radius.value * 1000.0 + 100001), q);
  reject(s, 0, kExperimentalContactSurface,
         ContactPatchError::outside_query_bounds);
  s = f.state(mul(radial, f.planet().radius.value * 1000.0 - 100001), q);
  reject(s, 0, kExperimentalContactSurface,
         ContactPatchError::outside_query_bounds);
  for (double sign : {-1.0, 1.0}) {
    // Axis-only radius is exact: qualify inclusive altitude boundary and its
    // adjacent actual position value, not a decimal offset that rounds away.
    auto limit = f.state({0,
                          f.planet().radius.value * 1000.0 +
                              sign * kContactPatchMaximumDatumAltitudeMetres,
                          0},
                         {});
    auto limit_cache = require(TerrainTileCache::create(1));
    const auto in_range = certify_contact_patch(
        f.context, limit, 0, kExperimentalContactSurface, limit_cache);
    check((in_range ||
           in_range.error() != ContactPatchError::outside_query_bounds) &&
              limit_cache.size() > 0,
          "inclusive datum-altitude bound rejected before geometry sampling");
    limit.position_metres.y =
        std::nextafter(limit.position_metres.y,
                       sign * std::numeric_limits<double>::infinity());
    reject(limit, 0, kExperimentalContactSurface,
           ContactPatchError::outside_query_bounds);
  }
  const auto origin = generate_origin_system(Seed{42});
  const RigidBodyWorldContext origin_context{origin};
  auto origin_state = state;
  origin_state.frame = {RigidFrameKind::planet_fixed, origin.id,
                        origin.planets.front().descriptor.id, std::nullopt};
  origin_state.orientation = {};
  origin_state.position_metres = {
      0, origin.planets.front().descriptor.radius.value * 1000.0, 0};
  auto origin_cache = require(TerrainTileCache::create(1));
  check(validate_rigid_body_state(origin_context, origin_state).has_value(),
        "origin refusal fixture must have authentic valid ownership");
  const auto origin_result =
      certify_contact_patch(origin_context, origin_state, 0,
                            kExperimentalContactSurface, origin_cache);
  check(!origin_result &&
            origin_result.error() == ContactPatchError::surface_query_failed &&
            origin_cache.size() == 0,
        "unsupported authored origin variant silently used standalone terrain "
        "identity");
}
auto geometry(Fixture& f) -> void {
  for (unsigned face = 0; face < 6; ++face) {
    const auto triangle = f.triangle(face);
    const auto normal = vector(triangle.outward_normal);
    const auto q = align_up(normal);
    for (unsigned support = 0; support < 3; ++support) {
      const auto s =
          f.state(add(center(triangle), mul(normal, -.1)), q, support);
      const auto before = require(rigid_body_state_checksum(f.context, s));
      const auto result = require(f.certificate(s, support));
      const auto owned = require(certify_owned_contact_patch(
          f.context, s, support, kExperimentalContactSurface, f.cache));
      check(owned.patch == result && owned.owner.system == f.system.id &&
                owned.owner.planet == f.planet().id &&
                owned.owner.descriptor_variant ==
                    ContactDescriptorVariant::procedural,
            "context qualification changed existing procedural patch bits");
      check(result.triangle.id == triangle.id && result.frame == s.frame &&
                result.tick == s.tick && result.support_index == support &&
                result.craft == s.craft,
            "patch identity or support ownership changed");
      near(result.minimum_gap_metres, -.1 - kContactPatchAllowanceMetres, 2e-8,
           "level gap minimum wrong");
      near(result.maximum_gap_metres, -.1 + kContactPatchAllowanceMetres, 2e-8,
           "level gap maximum wrong");
      oracle(result);
      check(before == require(rigid_body_state_checksum(f.context, s)),
            "query changed state checksum");
      auto tiny = require(TerrainTileCache::create(1));
      const auto cold = require(certify_contact_patch(
          f.context, s, support, kExperimentalContactSurface, tiny));
      (void)require(build_contact_triangle(
          f.planet(), kExperimentalContactSurface,
          {f.planet().id, CubeFace::positive_z, 1, 1, 1, 1, 0}, tiny));
      const auto after = require(certify_contact_patch(
          f.context, s, support, kExperimentalContactSurface, tiny));
      check(result == cold && cold == after && tiny.size() <= 1,
            "cache eviction/capacity changed certificate");
      if (support == 0 && face == 2) {
        // New experimental fixture observed identically on GCC and Clang;
        // independent geometric oracles above qualify the underlying result.
        std::cout << "patch level golden " << hash(result) << '\n';
        check(hash(result) == 3330668434011759318ULL,
              "version-one level patch bit golden changed");
      }
    }
    const auto tilted = compose(q, {.96, .28, 0, 0});
    const auto s = f.state(center(triangle), tilted);
    const auto result = require(f.certificate(s));
    near(result.minimum_gap_metres, -.55 * .5376 - kContactPatchAllowanceMetres,
         2e-8, "tilted analytic minimum wrong");
    near(result.maximum_gap_metres, .55 * .5376 + kContactPatchAllowanceMetres,
         2e-8, "tilted analytic maximum wrong");
    oracle(result);
    if (face == 2) {
      std::cout << "patch tilted golden " << hash(result) << '\n';
      check(hash(result) == 11232463919126072566ULL,
            "version-one tilted patch bit golden changed");
    }
    if (face == 2) {
      auto almost_unit = tilted;
      for (auto component : {&RigidOrientation::w, &RigidOrientation::x,
                             &RigidOrientation::y, &RigidOrientation::z})
        almost_unit.*component *= 1 + 2e-13;
      const auto norm2 =
          ((almost_unit.w * almost_unit.w + almost_unit.x * almost_unit.x) +
           almost_unit.y * almost_unit.y) +
          almost_unit.z * almost_unit.z;
      check(std::abs(norm2 - 1) > 1e-13 && std::abs(norm2 - 1) < 1e-12,
            "near-unit quaternion fixture missed canonical acceptance band");
      const auto scaled_state = f.state(center(triangle), almost_unit);
      const auto original =
          require(rigid_body_state_checksum(f.context, scaled_state));
      const auto scaled = require(f.certificate(scaled_state));
      oracle(scaled);
      near(scaled.minimum_gap_metres, result.minimum_gap_metres, 2e-8,
           "near-unit quaternion incorrectly scaled rotated footprint");
      check(original ==
                require(rigid_body_state_checksum(f.context, scaled_state)),
            "query renormalized canonical near-unit quaternion");
    }
    const auto inverted = f.state(center(triangle), compose(q, {0, 1, 0, 0}));
    check(f.certificate(inverted).has_value(),
          "inversion improperly treated as geometry/support failure");
    const auto collapsed = f.state(
        center(triangle), compose(q, {std::sqrt(.5), std::sqrt(.5), 0, 0}));
    const auto refused = f.certificate(collapsed);
    check(!refused &&
              refused.error() == ContactPatchError::degenerate_projection,
          "collapsed normal projection accepted");
  }
}
auto boundaries(Fixture& f) -> void {
  const auto triangle = f.triangle();
  const auto n = vector(triangle.outward_normal);
  const auto q = align_up(n);
  auto inward = unit(cross(n, sub(triangle.vertices[1], triangle.vertices[0])));
  if (dot(inward, sub(triangle.vertices[2], triangle.vertices[0])) < 0)
    inward = mul(inward, -1);
  const auto width = rotate(q, {.375, 0, 0}), depth = rotate(q, {0, 0, .55});
  const auto extent =
      std::abs(dot(inward, width)) + std::abs(dot(inward, depth));
  const auto edgecenter =
      add(triangle.vertices[0],
          mul(sub(triangle.vertices[1], triangle.vertices[0]), .5));
  auto pose = [&](double margin) {
    return f.state(add(edgecenter, mul(inward, extent + margin)), q);
  };
  // A valid center hit and four individually valid surface samples cannot
  // attest that the whole pad belongs to that one planar patch.
  const auto crossing = pose(-.05);
  const auto crossing_center = add(edgecenter, mul(inward, extent - .05));
  const auto center_hit = require(query_contact_surface(
      f.planet(), kExperimentalContactSurface,
      {crossing_center.x, crossing_center.y, crossing_center.z}, f.cache));
  check(center_hit.triangle.id == triangle.id,
        "crossing fixture center unexpectedly changed triangle");
  bool other_triangle = false;
  for (double x : {-1.0, 1.0})
    for (double y : {-1.0, 1.0}) {
      const auto corner =
          add(crossing_center, add(mul(width, x), mul(depth, y)));
      const auto hit = require(
          query_contact_surface(f.planet(), kExperimentalContactSurface,
                                {corner.x, corner.y, corner.z}, f.cache));
      other_triangle |= hit.triangle.id != triangle.id;
    }
  check(other_triangle,
        "corner-sampling trap did not cross a triangle boundary");
  const auto refused = f.certificate(crossing);
  check(!refused &&
            refused.error() == ContactPatchError::boundary_not_certified,
        "center/corner samples mistaken for whole-pad coverage");
  for (double margin : {0.0, kContactPatchAllowanceMetres - 1e-7}) {
    const auto result = f.certificate(pose(margin));
    check(!result &&
              result.error() == ContactPatchError::boundary_not_certified,
          "edge/inset uncertainty accepted");
  }
  check(f.certificate(pose(kContactPatchAllowanceMetres + 1e-7)).has_value(),
        "strictly interior edge fixture rejected");

  // Find the actual binary64 world-coordinate boundary; tiny decimal shifts
  // of a metre-scale margin may round away at planetary coordinates.
  auto s = pose(kContactPatchAllowanceMetres);
  const std::array members{&RigidVector3::x, &RigidVector3::y,
                           &RigidVector3::z};
  const std::array components{inward.x, inward.y, inward.z};
  unsigned axis = 0;
  double largest = 0;
  for (unsigned i = 0; i < 3; ++i) {
    const auto value = s.position_metres.*members[i];
    const auto step =
        std::abs(
            std::nextafter(value, std::numeric_limits<double>::infinity()) -
            value) *
        std::abs(components[i]);
    if (step > largest) {
      largest = step;
      axis = i;
    }
  }
  const auto toward = components[axis] > 0
                          ? std::numeric_limits<double>::infinity()
                          : -std::numeric_limits<double>::infinity();
  auto& value = s.position_metres.*members[axis];
  for (unsigned i = 0; i < 256; ++i)
    value = std::nextafter(value, -toward);
  bool previous_covered = false, found_transition = false;
  for (unsigned i = 0; i < 512; ++i) {
    const auto result = f.certificate(s);
    if (result && !previous_covered && i > 0) {
      found_transition = true;
      break;
    }
    check(result || result.error() == ContactPatchError::boundary_not_certified,
          "unexpected edge-scan refusal");
    previous_covered = result.has_value();
    value = std::nextafter(value, toward);
  }
  check(found_transition,
        "could not qualify adjacent-world-ULP strict inset boundary");

  // Explicit diagonal, tile and cube-face boundary crossings. Successful
  // radial center selection still cannot certify the normal-projected pad.
  const std::array<ContactTriangleId, 3> ids{
      ContactTriangleId{f.planet().id, CubeFace::positive_y, 4123, 3671, 10, 19,
                        0},
      ContactTriangleId{f.planet().id, CubeFace::positive_y, 4123, 3671, 0, 19,
                        0},
      ContactTriangleId{f.planet().id, CubeFace::positive_x, 0, 3671, 0, 19,
                        0}};
  for (unsigned i = 0; i < ids.size(); ++i) {
    const auto t = require(build_contact_triangle(
        f.planet(), kExperimentalContactSurface, ids[i], f.cache));
    const unsigned edge = i == 0 ? 1 : 2;
    const auto base = t.vertices[edge], end = t.vertices[(edge + 1) % 3];
    const auto normal = vector(t.outward_normal);
    auto h = unit(cross(normal, sub(end, base)));
    if (dot(h, sub(t.vertices[(edge + 2) % 3], base)) < 0) h = mul(h, -1);
    const auto orientation = align_up(normal);
    const auto a = rotate(orientation, {.375, 0, 0});
    const auto b = rotate(orientation, {0, 0, .55});
    const auto half_span = std::abs(dot(h, a)) + std::abs(dot(h, b));
    const auto c =
        add(add(base, mul(sub(end, base), .5)), mul(h, half_span - .05));
    const auto selected = require(locate_contact_triangle(
        f.planet(), kExperimentalContactSurface, {c.x, c.y, c.z}));
    check(selected == t.id,
          "explicit seam fixture selected wrong center triangle");
    const auto result = f.certificate(f.state(c, orientation));
    check(!result &&
              result.error() == ContactPatchError::boundary_not_certified,
          "diagonal/tile/cube boundary crossing was certified");
  }
}
auto owned_patch_hash(const ExperimentalOwnedContactPatch& patch)
    -> std::uint64_t {
  auto result = hash(patch.patch);
  const auto& owner = patch.owner;
  const std::array<std::uint64_t, 9> fields{
      owner.format,
      owner.system.value,
      static_cast<unsigned>(owner.catalog_kind),
      owner.planet.value,
      owner.seed_derivation,
      owner.system_generator,
      owner.planet_generator,
      static_cast<unsigned>(owner.descriptor_variant),
      owner.origin_home_generator};
  for (auto field : fields)
    for (unsigned i = 0; i < 8; ++i) {
      result ^= (field >> (8 * i)) & 255;
      result *= 1099511628211ULL;
    }
  return result;
}
auto owned_patches() -> void {
  Fixture home{generate_origin_system(Seed{42})};
  auto selected = require(locate_owned_contact_triangle(
      home.system, home.planet().id, kExperimentalContactSurface, {1, 0, 0}));
  for (unsigned face = 0; face < 6; ++face) {
    selected.triangle = {
        home.planet().id, static_cast<CubeFace>(face), 4123, 3671, 10, 19, 0};
    const auto triangle = require(
        build_owned_contact_triangle(home.system, selected, home.cache));
    const auto n = vector(triangle.triangle.outward_normal);
    const auto q = align_up(n);
    for (unsigned support = 0; support < 3; ++support) {
      const auto state =
          home.state(add(center(triangle.triangle), mul(n, -.1)), q, support);
      const auto before =
          require(rigid_body_state_checksum(home.context, state));
      const auto result = require(
          certify_owned_contact_patch(home.context, state, support,
                                      kExperimentalContactSurface, home.cache));
      check(result.owner == selected.owner &&
                result.patch.frame == state.frame &&
                result.patch.triangle == triangle.triangle &&
                result.patch.tick == state.tick &&
                result.patch.support_index == support,
            "origin patch lost canonical owner/triangle/state provenance");
      oracle(result.patch);
      near(result.patch.minimum_gap_metres, -.1 - kContactPatchAllowanceMetres,
           2e-8,
           "owned origin level gap differs from independent plane geometry");
      check(require(rigid_body_state_checksum(home.context, state)) == before,
            "owned patch query mutated authoritative rigid state");
      auto separate = require(TerrainTileCache::create(1));
      const auto cold = require(certify_owned_contact_patch(
          home.context, state, support, kExperimentalContactSurface, separate));
      auto distant = selected;
      distant.triangle.tile_x = 1;
      distant.triangle.tile_y = 1;
      (void)require(
          build_owned_contact_triangle(home.system, distant, separate));
      check(require(certify_owned_contact_patch(home.context, state, support,
                                                kExperimentalContactSurface,
                                                separate)) == cold &&
                cold == result,
            "context-scoped cache eviction changed origin patch");
      const auto standalone = home.certificate(state, support);
      check(!standalone &&
                standalone.error() == ContactPatchError::surface_query_failed,
            "existing standalone origin patch refusal changed");
      if (face == 2 && support == 0) {
        std::cout << "owned patch level golden " << owned_patch_hash(result)
                  << '\n';
        check(owned_patch_hash(result) == 7643701639146807608ULL,
              "experimental owned level patch golden changed");
      }
    }
    const auto tilted =
        home.state(center(triangle.triangle), compose(q, {.96, .28, 0, 0}));
    const auto result = require(certify_owned_contact_patch(
        home.context, tilted, 0, kExperimentalContactSurface, home.cache));
    oracle(result.patch);
    near(result.patch.minimum_gap_metres,
         -.55 * .5376 - kContactPatchAllowanceMetres, 2e-8,
         "owned origin tilted gap differs from analytic rectangle");
    if (face == 2) {
      std::cout << "owned patch tilted golden " << owned_patch_hash(result)
                << '\n';
      check(owned_patch_hash(result) == 4396711435420471693ULL,
            "experimental owned tilted patch golden changed");
    }
    const auto wrong_system = generate_local_system(Seed{7});
    const RigidBodyWorldContext wrong{wrong_system};
    auto untouched = require(TerrainTileCache::create(1));
    const auto refused = certify_owned_contact_patch(
        wrong, tilted, 0, kExperimentalContactSurface, untouched);
    check(!refused && refused.error() == ContactPatchError::invalid_state &&
              untouched.size() == 0,
          "owned patch admitted mismatched frame/system provenance");
  }
}
} // namespace

auto main() -> int {
  try {
    Fixture fixture;
    malformed(fixture);
    geometry(fixture);
    boundaries(fixture);
    owned_patches();
    std::cout << "contact patch: PASS (normal-projection geometry only)\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "contact patch: " << error.what() << '\n';
    return 1;
  }
}
