#pragma once

#include "apsis_drift/moon_landed_craft.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace apsis_drift::test_fixture {
template <class T, class E>
auto require_moon_contact(std::expected<T, E> value) -> T {
  if (!value)
    throw std::runtime_error("Fixed genuine moon contact fixture refused");
  return std::move(*value);
}
struct MoonContactFixture {
  PhysicalMoonDescriptor moon;
  RigidBodyState fixed, source;
  MoonContactTriangle triangle;
};
// One preselected seed/cell, no search or terrain/shape changes. Front pad
// placed5cm into the genuine facet; all other pads/hull must still certify.
inline auto make_moon_contact_fixture() -> MoonContactFixture {
  const auto system =
      require_moon_contact(generate_physical_origin_system(Seed{42}, 2));
  const auto catalog =
      require_moon_contact(generate_secondary_body_catalog(system));
  auto moon = require_moon_contact(
      generate_physical_moon_descriptor(catalog, catalog.bodies[0].node.id));
  const auto sampler = require_moon_contact(MoonTerrainSampler::create(moon));
  const auto triangle = require_moon_contact(build_moon_contact_triangle(
      sampler, {moon.body, CubeFace::positive_y, 4096, 4096, 16, 16, 0}));
  const auto& a = triangle.vertices[0];
  const auto& b = triangle.vertices[1];
  const auto& c = triangle.vertices[2];
  const RigidVector3 center{a.x + ((b.x - a.x) + (c.x - a.x)) / 3,
                            a.y + ((b.y - a.y) + (c.y - a.y)) / 3,
                            a.z + ((b.z - a.z) + (c.z - a.z)) / 3};
  const auto n = triangle.outward_normal;
  const auto w = 1 + n.y, magnitude = std::hypot(w, n.z, n.x);
  RigidBodyState fixed;
  fixed.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  fixed.tick = 1200;
  fixed.frame = {
      RigidFrameKind::body_fixed, system.catalog.id, {}, {}, moon.body};
  fixed.orientation = {w / magnitude, n.z / magnitude, 0, -n.x / magnitude};
  const auto craft = require_moon_contact(resolve_craft_frame(fixed.craft));
  const auto& pad = craft.properties.supports[0];
  const RigidVector3 offset{
      static_cast<double>(std::int64_t{pad.contact_mm[0]} -
                          craft.properties.center_of_mass_mm[0]) /
          1000,
      static_cast<double>(std::int64_t{pad.contact_mm[1]} -
                          craft.properties.center_of_mass_mm[1]) /
          1000,
      static_cast<double>(std::int64_t{pad.contact_mm[2]} -
                          craft.properties.center_of_mass_mm[2]) /
          1000};
  // Independent unit-equivalent quaternion vector rotation for fixture setup.
  const auto& q = fixed.orientation;
  const RigidVector3 v{q.x, q.y, q.z};
  const RigidVector3 cross1{v.y * offset.z - v.z * offset.y,
                            v.z * offset.x - v.x * offset.z,
                            v.x * offset.y - v.y * offset.x};
  const RigidVector3 cross2{v.y * cross1.z - v.z * cross1.y,
                            v.z * cross1.x - v.x * cross1.z,
                            v.x * cross1.y - v.y * cross1.x};
  const auto scale = 2 / (((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z);
  const RigidVector3 rotated{offset.x + scale * (q.w * cross1.x + cross2.x),
                             offset.y + scale * (q.w * cross1.y + cross2.y),
                             offset.z + scale * (q.w * cross1.z + cross2.z)};
  fixed.position_metres = {center.x - .05 * n.x - rotated.x,
                           center.y - .05 * n.y - rotated.y,
                           center.z - .05 * n.z - rotated.z};
  const RigidBodyWorldContext context{moon};
  fixed = require_moon_contact(canonicalize_rigid_body_state(context, fixed));
  const auto source = require_moon_contact(
      reframe_rigid_body(context, fixed,
                         {{RigidFrameKind::body_relative_inertial,
                           system.catalog.id,
                           {},
                           {},
                           moon.body},
                          fixed.tick},
                         moon));
  return {std::move(moon), fixed, source, triangle};
}
} // namespace apsis_drift::test_fixture
