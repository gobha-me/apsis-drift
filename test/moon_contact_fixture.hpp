#pragma once

#include "apsis_drift/moon_landed_craft.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "godot/contact_geometry_internal.hpp"

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
  // The rear-starboard footprint crosses its selected facet's first edge at
  // the front-pad centroid placement. Translate the whole craft within the
  // ORIGINAL front-pad plane, just far enough to leave 5cm of footprint edge
  // clearance. This is one geometric placement, not a terrain/location search;
  // the seed, facets, orientation, pad sizes and production tolerances stay
  // put.
  const auto rear =
      require_moon_contact(godot_spike::detail::nominal_contact_rectangle(
          moon.radius_metres, fixed, craft.properties, 2));
  const auto rear_id = require_moon_contact(locate_moon_contact_triangle(
      sampler, {rear.center.x, rear.center.y, rear.center.z}));
  const auto rear_triangle =
      require_moon_contact(build_moon_contact_triangle(sampler, rear_id));
  const auto& edge_a = rear_triangle.vertices[0];
  const auto& edge_b = rear_triangle.vertices[1];
  const auto& opposite = rear_triangle.vertices[2];
  const RigidVector3 edge{edge_b.x - edge_a.x, edge_b.y - edge_a.y,
                          edge_b.z - edge_a.z};
  const auto rn = rear_triangle.outward_normal;
  RigidVector3 inward{rn.y * edge.z - rn.z * edge.y,
                      rn.z * edge.x - rn.x * edge.z,
                      rn.x * edge.y - rn.y * edge.x};
  const auto dot = [](const auto& lhs, const auto& rhs) {
    return (lhs.x * rhs.x + lhs.y * rhs.y) + lhs.z * rhs.z;
  };
  const RigidVector3 toward{opposite.x - edge_a.x, opposite.y - edge_a.y,
                            opposite.z - edge_a.z};
  const auto inward_length = std::hypot(inward.x, inward.y, inward.z);
  const auto inward_scale =
      (dot(inward, toward) < 0 ? -1.0 : 1.0) / inward_length;
  inward = {inward.x * inward_scale, inward.y * inward_scale,
            inward.z * inward_scale};
  const RigidVector3 rear_relative{rear.center.x - edge_a.x,
                                   rear.center.y - edge_a.y,
                                   rear.center.z - edge_a.z};
  const auto minimum = dot(inward, rear_relative) -
                       std::abs(dot(inward, rear.width)) -
                       std::abs(dot(inward, rear.depth));
  const auto normal_component = dot(inward, n);
  const RigidVector3 tangent{inward.x - normal_component * n.x,
                             inward.y - normal_component * n.y,
                             inward.z - normal_component * n.z};
  const auto shift = (.05 - minimum) / dot(inward, tangent);
  if (!std::isfinite(shift) || shift <= 0 || shift >= 1)
    throw std::runtime_error("Fixed moon footprint translation is invalid");
  fixed.position_metres = {fixed.position_metres.x + shift * tangent.x,
                           fixed.position_metres.y + shift * tangent.y,
                           fixed.position_metres.z + shift * tangent.z};
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
