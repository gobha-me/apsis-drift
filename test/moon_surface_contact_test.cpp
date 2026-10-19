#include "apsis_drift/moon_surface_contact.hpp"
#include "moon_contact_fixture.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace apsis_drift;
using test_fixture::require_moon_contact;
int checks{}, failures{};
auto check(bool value, const char* message) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
auto near(double a, long double b, long double tolerance) -> bool {
  return std::isfinite(a) &&
         std::abs(static_cast<long double>(a) - b) <= tolerance;
}
auto triangles(const MoonTerrainSampler& sampler) -> void {
  for (unsigned face = 0; face < 6; ++face)
    for (unsigned half = 0; half < 2; ++half) {
      const MoonContactTriangleId id{sampler.owner().body,
                                     static_cast<CubeFace>(face),
                                     4096,
                                     4096,
                                     16,
                                     16,
                                     half};
      const auto triangle =
          require_moon_contact(build_moon_contact_triangle(sampler, id));
      check(triangle.id == id && triangle.version == 1,
            "Actual facets retain typed moon and fixed topology identity");
      const auto& a = triangle.vertices[0];
      const auto& b = triangle.vertices[1];
      const auto& c = triangle.vertices[2];
      const long double ux = static_cast<long double>(b.x) - a.x,
                        uy = static_cast<long double>(b.y) - a.y,
                        uz = static_cast<long double>(b.z) - a.z;
      const long double vx = static_cast<long double>(c.x) - a.x,
                        vy = static_cast<long double>(c.y) - a.y,
                        vz = static_cast<long double>(c.z) - a.z;
      const std::array<long double, 3> cross{
          uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
      const auto area = std::sqrt(cross[0] * cross[0] + cross[1] * cross[1] +
                                  cross[2] * cross[2]);
      const auto sign =
          cross[0] * a.x + cross[1] * a.y + cross[2] * a.z < 0 ? -1 : 1;
      check(
          near(triangle.outward_normal.x, sign * cross[0] / area, 1e-12L) &&
              near(triangle.outward_normal.y, sign * cross[1] / area, 1e-12L) &&
              near(triangle.outward_normal.z, sign * cross[2] / area, 1e-12L),
          "Outward facet normal agrees with independent long-double cross "
          "product");
      const MoonFixedDirection center{a.x + ((b.x - a.x) + (c.x - a.x)) / 3,
                                      a.y + ((b.y - a.y) + (c.y - a.y)) / 3,
                                      a.z + ((b.z - a.z) + (c.z - a.z)) / 3};
      const auto hit =
          require_moon_contact(query_moon_contact_surface(sampler, center));
      check(hit.triangle.id == id && hit.radial_distance_metres > 0,
            "Radial hit uses actual selected top facet");
      const auto& h = hit.position;
      const long double residual =
          (static_cast<long double>(h.x) - a.x) * triangle.outward_normal.x +
          (static_cast<long double>(h.y) - a.y) * triangle.outward_normal.y +
          (static_cast<long double>(h.z) - a.z) * triangle.outward_normal.z;
      check(std::abs(residual) < 1e-8L, "Actual radial hit lies on the facet "
                                        "plane, not continuous noise height");
      check(near(hit.barycentric[0] + hit.barycentric[1] + hit.barycentric[2],
                 1, 1e-14L),
            "Top-facet barycentric weights conserve unity");
      const std::array<MoonFixedPositionMetres, 3> v{a, b, c};
      for (unsigned axis = 0; axis < 3; ++axis) {
        long double reconstructed{};
        for (unsigned i = 0; i < 3; ++i)
          reconstructed += hit.barycentric[i] * (axis == 0   ? v[i].x
                                                 : axis == 1 ? v[i].y
                                                             : v[i].z);
        check(near(axis == 0   ? h.x
                   : axis == 1 ? h.y
                               : h.z,
                   reconstructed, 1e-8L),
              "Barycentric hit reconstructs all actual submitted coordinates");
      }
    }
  for (const auto& direction : std::array<MoonFixedDirection, 5>{
           {{0, 0, 0},
            {NAN, 1, 0},
            {1, std::numeric_limits<double>::infinity(), 0},
            {1e10, 0, 0},
            {1e-13, 0, 0}}})
    check(!query_moon_contact_surface(sampler, direction),
          "Malformed/out-of-domain radial direction refuses");
  auto id =
      require_moon_contact(locate_moon_contact_triangle(sampler, {1, 0, 0}));
  check(id.face == CubeFace::positive_x && id.tile_x == 4096 &&
            id.tile_y == 4096 && id.cell_x == 0 && id.cell_y == 0 &&
            id.half == 0,
        "Exact cube/grid ownership preserves positive-side seam convention");
  for (unsigned field = 0; field < 7; ++field) {
    auto bad = id;
    if (field == 0)
      bad.body.kind = BodyKind::planet;
    else if (field == 1)
      ++bad.body.value;
    else if (field == 2)
      bad.face = static_cast<CubeFace>(6);
    else if (field == 3)
      bad.tile_x = 8192;
    else if (field == 4)
      bad.cell_y = 32;
    else if (field == 5)
      bad.half = 2;
    else
      ++bad.body.version;
    check(!build_moon_contact_triangle(sampler, bad),
          "Foreign body and malformed top triangle key refuse");
  }
}
auto snapshots(const test_fixture::MoonContactFixture& f) -> void {
  const auto before = require_moon_contact(
      encode_rigid_body_state_json(RigidBodyWorldContext{f.moon}, f.source));
  const auto snapshot =
      require_moon_contact(MoonContactSnapshot::create(f.moon, f.source, true));
  const auto result = snapshot.query();
  const auto hull = snapshot.hull_clearance();
  std::cout << "Fixed moon contact: supports=" << unsigned(result.support_count)
            << " assessment=" << bool(result.assessment) << " class="
            << (result.assessment
                    ? static_cast<int>(result.assessment->classification)
                    : -1)
            << " hull=" << bool(hull)
            << " certified=" << (hull && hull->certified) << '\n';
  check(result.provenance.owner == f.moon &&
            result.provenance.original_state == f.source &&
            result.provenance.fixed_query_state.frame.kind ==
                RigidFrameKind::body_fixed,
        "Snapshot retains actual owner/original and same-body fixed query");
  for (unsigned i = 0; i < result.support_count; ++i) {
    const auto& pad = result.supports[i];
    std::cout << " pad" << i << " geometry=" << (pad.geometry && *pad.geometry)
              << " support=" << (pad.support && *pad.support) << '\n';
    check(pad.support_index == i && pad.geometry.has_value() &&
              pad.support.has_value(),
          "Every actual support retains indexed geometry and material outcome");
    if (pad.support && *pad.support)
      check((**pad.support).surface == TouchdownSurface::solid &&
                (**pad.support).footprint_supported,
            "Authenticated moon facets are dry even below reference radius");
    if (pad.geometry && *pad.geometry) {
      const auto& geometry = **pad.geometry;
      check(geometry.frame.body == f.moon.body &&
                geometry.triangle.id.body == f.moon.body &&
                geometry.minimum_gap_metres <= geometry.maximum_gap_metres,
            "Successful whole-pad geometry retains genuine body/frame/bounded "
            "gaps");
      for (const auto edge : geometry.minimum_edge_distances_metres)
        check(edge > godot_spike::kContactPatchAllowanceMetres,
              "Successful pad certifies every strict triangle edge inset");
    }
  }
  if (hull)
    check(
        hull->triangle_count <= 128 &&
            hull->source_checksum ==
                result.provenance.original_state_checksum &&
            hull->certified == (hull->lower_clearance_metres > 0),
        "Whole hull uses bounded actual top facets and strict lower clearance");
  const auto no_gear =
      require_moon_contact(MoonContactSnapshot::create(f.moon, f.source, false))
          .query();
  check(!no_gear.assessment || no_gear.assessment->classification !=
                                   TouchdownClass::pad_contact_ready,
        "Prospective geometry never deploys missing gear");
  auto forged = f.moon;
  ++forged.radius_metres;
  check(!MoonContactSnapshot::create(forged, f.source, true) &&
            !MoonContactSnapshot::create(f.moon, f.source, true, 2),
        "Wrong physical owner/terrain recipe refuses before source sampling");
  auto wrong = f.source;
  wrong.frame.body->kind = BodyKind::minor;
  check(!MoonContactSnapshot::create(f.moon, wrong, true),
        "Wrong kind cannot borrow numeric moon identity");
  wrong = f.source;
  wrong.position_metres.x = NAN;
  check(!MoonContactSnapshot::create(f.moon, wrong, true),
        "Nonfinite source refuses snapshot");
  auto distant = f.fixed;
  const auto scale = 1 + 200000.0 / f.moon.radius_metres;
  distant.position_metres = {distant.position_metres.x * scale,
                             distant.position_metres.y * scale,
                             distant.position_metres.z * scale};
  distant = require_moon_contact(
      reframe_rigid_body(RigidBodyWorldContext{f.moon}, distant,
                         {{RigidFrameKind::body_relative_inertial,
                           f.source.frame.system,
                           {},
                           {},
                           f.moon.body},
                          distant.tick},
                         f.moon));
  const auto outside =
      require_moon_contact(MoonContactSnapshot::create(f.moon, distant, true));
  check(!outside.query().assessment && !outside.hull_clearance(),
        "Bounded near-surface domain refuses rather than inventing support");
  auto seam = f.fixed;
  const auto coordinate =
      static_cast<double>(f.moon.radius_metres) / std::sqrt(2.0);
  seam.position_metres = {coordinate, coordinate, 0};
  seam = require_moon_contact(
      reframe_rigid_body(RigidBodyWorldContext{f.moon}, seam,
                         {{RigidFrameKind::body_relative_inertial,
                           f.source.frame.system,
                           {},
                           {},
                           f.moon.body},
                          seam.tick},
                         f.moon));
  const auto seam_snapshot =
      require_moon_contact(MoonContactSnapshot::create(f.moon, seam, true));
  const auto seam_hull = seam_snapshot.hull_clearance();
  check(!seam_hull && seam_hull.error() == TerrainHullError::face_boundary,
        "Whole-hull cube-face seam remains unknown rather than invisible "
        "clearance");
  const auto retained = [] {
    const auto local = test_fixture::make_moon_contact_fixture();
    return require_moon_contact(
        MoonContactSnapshot::create(local.moon, local.source, true));
  }();
  check(retained.provenance().owner == f.moon &&
            retained.query().support_count == result.support_count,
        "Snapshot owns complete moon and state beyond caller lifetime");
  check(require_moon_contact(encode_rigid_body_state_json(
            RigidBodyWorldContext{f.moon}, f.source)) == before,
        "Queries/refusals never modify source state or bytes");
}
} // namespace
int main() {
  try {
    const auto f = test_fixture::make_moon_contact_fixture();
    const auto sampler =
        require_moon_contact(MoonTerrainSampler::create(f.moon));
    triangles(sampler);
    snapshots(f);
  } catch (const std::exception& error) {
    check(false, error.what());
  }
  std::cout << checks << " moon contact checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
