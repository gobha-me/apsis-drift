#include "apsis_drift/craft_frame.hpp"
#include "apsis_drift/godot/moon_streaming.hpp"
#include "apsis_drift/moon_surface_contact.hpp"

#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

namespace {
using namespace apsis_drift;
using namespace apsis_drift::godot_spike;
using V = RigidVector3;
int checks{}, failures{};
auto check(bool ok, std::string_view text) -> void {
  ++checks;
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << text << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Actual moon stream fixture refused");
  return std::move(*value);
}
template <class F> auto rejects(F fn, std::string_view text) -> void {
  bool refused{};
  try {
    fn();
  } catch (const std::invalid_argument&) {
    refused = true;
  }
  check(refused, text);
}
auto norm(V v) -> double {
  return std::hypot(v.x, v.y, v.z);
}
auto finite(V v) -> bool {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
auto absolute(const MoonMeshTile& tile, unsigned i) -> V {
  return {tile.anchor.x + tile.vertices[i].x,
          tile.anchor.y + tile.vertices[i].y,
          tile.anchor.z + tile.vertices[i].z};
}
auto cover(const std::vector<MoonStreamKey>& keys, unsigned budget) -> void {
  check(keys.size() >= 6 && keys.size() <= budget,
        "Complete cover respects selected finite tile budget");
  const std::set<MoonStreamKey> unique(keys.begin(), keys.end());
  check(unique.size() == keys.size(), "Complete cover has no duplicate keys");
  std::array<std::uint64_t, 6> area{};
  for (auto key : keys) {
    check(key.face < 6 && key.lod <= 13 && key.x < (1U << key.lod) &&
              key.y < (1U << key.lod),
          "Every planned key has valid finite dimensions");
    area[key.face] += std::uint64_t{1} << (2 * (13 - key.lod));
    while (key.lod) {
      --key.lod;
      key.x /= 2;
      key.y /= 2;
      check(!unique.contains(key), "Partition has no overlapping ancestor");
    }
  }
  for (auto a : area)
    check(a == (std::uint64_t{1} << 26),
          "Each entire cube face is covered exactly once");
}
auto mesh(const MoonTerrainSampler& sampler, const MoonMeshTile& tile) -> void {
  constexpr unsigned n = 33, vertices = n * n + 4 * n,
                     indices = 6 * 32 * 32 + 48 * 32;
  check(tile.terrain_version == 1 && tile.body == sampler.owner().body,
        "Mesh records actual typed source owner and recipe");
  check(tile.vertices.size() == vertices && tile.normals.size() == vertices &&
            tile.colors.size() == vertices &&
            tile.elevations.size() == vertices &&
            tile.indices.size() == indices,
        "Mesh dimensions include fixed complete top and four double-sided "
        "skirts");
  check(tile.bytes() ==
            vertices * (2 * sizeof(V) + sizeof(Rgb8) + sizeof(double)) +
                indices * sizeof(std::int32_t),
        "Owned bytes account every output attribute");
  const auto& key = tile.key;
  for (unsigned row = 0; row < n; ++row)
    for (unsigned col = 0; col < n; ++col) {
      const auto d = need(direction_from_cube_surface_address(
          {static_cast<CubeFace>(key.face), static_cast<std::uint8_t>(key.lod),
           key.x, key.y, col / 32., row / 32.}));
      const auto sample = need(sampler.sample({d.x, d.y, d.z}));
      const auto source = need(sampler.surface_position({d.x, d.y, d.z}));
      const unsigned i = row * n + col;
      check(tile.vertices[i] == V{source.x - tile.anchor.x,
                                  source.y - tile.anchor.y,
                                  source.z - tile.anchor.z},
            "Top mesh uses exact sampler point minus owned anchor, without "
            "flattening");
      check(tile.elevations[i] == sample.elevation_metres &&
                tile.colors[i] == sample.color,
            "Signed relief and actual tint bytes are preserved without "
            "water/sea palette");
    }
  for (unsigned i = 0; i < vertices; ++i) {
    check(finite(tile.vertices[i]) && finite(tile.normals[i]) &&
              std::isfinite(tile.elevations[i]),
          "All generated scalar arrays finite");
    check(std::abs(norm(tile.normals[i]) - 1) < 1e-10,
          "All top/skirt normals unit");
  }
  for (auto index : tile.indices)
    check(index >= 0 && static_cast<unsigned>(index) < vertices,
          "Every top/skirt index stays in owned buffer");
  for (unsigned edge = 0; edge < 4; ++edge)
    for (unsigned i = 0; i < n; ++i) {
      const unsigned top = edge == 0   ? i
                           : edge == 1 ? i * n + n - 1
                           : edge == 2 ? (n - 1) * n + n - 1 - i
                                       : (n - 1 - i) * n;
      const auto bottom = n * n + edge * n + i;
      const auto minimum_depth = 2 * kMoonTerrainMaximumReliefMetres +
                                 sampler.owner().radius_metres / 1024. + 1.;
      check(tile.elevations[top] - tile.elevations[bottom] >=
                minimum_depth - 1e-9,
            "Even fine skirts retain source-derived two-envelope/chord crack "
            "coverage");
      check(tile.elevations[bottom] < tile.elevations[top] &&
                tile.colors[bottom] == tile.colors[top] &&
                tile.normals[bottom] == tile.normals[top],
            "Skirt is inward only and retains source top normal/tint");
      check(norm(absolute(tile, bottom)) < norm(absolute(tile, top)),
            "Skirt lowers actual radial position rather than creating a second "
            "top");
    }
  // Godot clockwise front-face order, with explicit outward normals.
  for (unsigned i = 0; i < 6 * 32 * 32; i += 3) {
    const auto a = absolute(tile, static_cast<unsigned>(tile.indices[i]));
    const auto b = absolute(tile, static_cast<unsigned>(tile.indices[i + 1]));
    const auto c = absolute(tile, static_cast<unsigned>(tile.indices[i + 2]));
    const long double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    const long double vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    check((uy * vz - uz * vy) * a.x + (uz * vx - ux * vz) * a.y +
                  (ux * vy - uy * vx) * a.z <
              0,
          "Actual top index winding is clockwise when seen from outside");
  }
}
auto contacts(const MoonTerrainSampler& sampler, const MoonMeshTile& tile)
    -> void {
  check(tile.key.lod == 13, "Contact equality uses genuine actual fixed LOD13");
  for (unsigned y = 0; y < 32; ++y)
    for (unsigned x = 0; x < 32; ++x)
      for (unsigned half = 0; half < 2; ++half) {
        const auto triangle = need(build_moon_contact_triangle(
            sampler,
            {sampler.owner().body, static_cast<CubeFace>(tile.key.face),
             tile.key.x, tile.key.y, x, y, half}));
        const unsigned a = y * 33 + x;
        const std::array<unsigned, 3> corners =
            half == 0 ? std::array{a, a + 1, a + 33}
                      : std::array{a + 1, a + 34, a + 33};
        const unsigned offset = (y * 32 + x) * 6 + half * 3;
        std::set<unsigned> emitted;
        for (unsigned i = 0; i < 3; ++i) {
          const auto& p = triangle.vertices[i];
          check(tile.vertices[corners[i]] == V{p.x - tile.anchor.x,
                                               p.y - tile.anchor.y,
                                               p.z - tile.anchor.z},
                "Every full LOD13 contact triangle corner is bit-identical "
                "before anchor reconstruction");
          emitted.insert(static_cast<unsigned>(tile.indices[offset + i]));
        }
        check(emitted == std::set<unsigned>(corners.begin(), corners.end()),
              "Rendered and physical triangle have same finite diagonal1 "
              "vertices");
      }
}
// Independent finite probes at a fixed tile edge, face seam and triple
// corner exercise the interval guard. These are visibility/topology checks,
// not claims that a craft is actually supported at these authored locations.
auto contact_footprints(const PhysicalMoonDescriptor& owner,
                        const MoonTerrainSampler& sampler) -> void {
  const auto edge = need(direction_from_cube_surface_address(
      {CubeFace::positive_y, 13, 4096, 4096, 0, .5}));
  const std::array<MoonFixedDirection, 3> directions{
      {{edge.x, edge.y, edge.z}, {1, 1, .2}, {1, 1, 1}}};
  const auto craft =
      need(resolve_craft_frame({kWayfarerFrameId, kWayfarerFrameVersion}))
          .properties;
  for (const auto& direction : directions) {
    const auto observer = need(sampler.surface_position(direction));
    for (unsigned budget : {186U, 384U}) {
      const auto plan = plan_moon_tiles(owner, observer, budget);
      cover(plan, budget);
      const std::set<MoonStreamKey> leaves(plan.begin(), plan.end());
      const auto covered = [&](double dx, double dy, double dz) {
        const MoonFixedDirection probe{observer.x + dx, observer.y + dy,
                                       observer.z + dz};
        const auto actual = need(locate_moon_contact_triangle(sampler, probe));
        check(actual.body == owner.body &&
                  leaves.contains({static_cast<unsigned>(actual.face), 13,
                                   actual.tile_x, actual.tile_y}),
              "Every actual local contact probe has resident LOD13 across "
              "tile/face/corner boundaries");
      };
      for (double x : {-32., -16., 0., 16., 32.})
        for (double y : {-32., -16., 0., 16., 32.})
          for (double z : {-32., -16., 0., 16., 32.})
            covered(x, y, z);
      for (unsigned bits = 0; bits < 8; ++bits) {
        std::array<double, 3> offset;
        for (unsigned axis = 0; axis < 3; ++axis)
          offset[axis] = (static_cast<double>((bits & (1U << axis))
                                                  ? craft.hull_max_mm[axis]
                                                  : craft.hull_min_mm[axis]) -
                          craft.center_of_mass_mm[axis]) /
                         1000.;
        covered(offset[0], offset[1], offset[2]);
      }
      for (unsigned i = 0; i < craft.support_count; ++i) {
        const auto& pad = craft.supports[i];
        for (double sx : {-1., 1.})
          for (double sz : {-1., 1.})
            covered((static_cast<double>(pad.contact_mm[0]) -
                     craft.center_of_mass_mm[0] + sx * pad.half_width_mm) /
                        1000.,
                    (static_cast<double>(pad.contact_mm[1]) -
                     craft.center_of_mass_mm[1]) /
                        1000.,
                    (static_cast<double>(pad.contact_mm[2]) -
                     craft.center_of_mass_mm[2] + sz * pad.half_length_mm) /
                        1000.);
      }
      for (double x : {-.32, .32})
        for (double y : {0., 1.93})
          for (double z : {-.32, .32})
            covered(x, y, z);
    }
  }
  rejects([&] { (void)plan_moon_tiles(owner, {1, 0, 0}); },
          "Deep interior cannot manufacture an unbounded fine-contact cover");
}
auto run() -> void {
  const auto system = need(generate_physical_origin_system(Seed{42}, 2));
  const auto catalog = need(generate_secondary_body_catalog(system));
  const auto owner = need(
      generate_physical_moon_descriptor(catalog, catalog.bodies[0].node.id));
  const auto snapshot = owner;
  const auto sampler = need(MoonTerrainSampler::create(owner));
  contact_footprints(owner, sampler);
  for (unsigned budget : {6U, 12U, 45U, 384U}) {
    const MoonFixedPositionMetres observer{
        static_cast<double>(owner.radius_metres) + 10, 0, 0};
    const auto plan = plan_moon_tiles(owner, observer, budget);
    cover(plan, budget);
    check(plan == plan_moon_tiles(owner, observer, budget),
          "Planner is deterministic without random streams or mutable cache");
  }
  for (const auto& direction :
       std::array<MoonFixedDirection, 6>{{{1, 0, 0},
                                          {-1, 0, 0},
                                          {0, 1, 0},
                                          {0, -1, 0},
                                          {0, 0, 1},
                                          {0, 0, -1}}}) {
    const auto ground = need(sampler.surface_position(direction));
    const auto radial = std::hypot(ground.x, ground.y, ground.z);
    const MoonFixedPositionMetres observer{ground.x * (radial + 1) / radial,
                                           ground.y * (radial + 1) / radial,
                                           ground.z * (radial + 1) / radial};
    const auto plan = plan_moon_tiles(owner, observer);
    cover(plan, 384);
    const auto address = need(cube_surface_address_from_direction(
        {observer.x, observer.y, observer.z}, 13));
    check(std::ranges::find(
              plan, MoonStreamKey{static_cast<unsigned>(address.face), 13,
                                  address.x, address.y}) != plan.end(),
          "Default bounded near-ground cover reaches actual contact LOD13 on "
          "every face");
  }
  const auto far = plan_moon_tiles(owner, {owner.radius_metres + 1e8, 0, 0});
  cover(far, 384);
  check(far.size() == 6,
        "Distant whole moon retains only complete coarse faces");
  bool negative{};
  for (unsigned face = 0; face < 6; ++face) {
    const auto tile = make_moon_stream_tile(sampler, {face, 0, 0, 0});
    mesh(sampler, *tile);
    for (unsigned i = 0; i < 33 * 33; ++i)
      negative |= tile->elevations[i] < 0;
  }
  check(negative,
        "Deterministic full-face dry meshes retain actual negative relief");
  const auto fine = make_moon_stream_tile(sampler, {2, 13, 4096, 4096});
  mesh(sampler, *fine);
  contacts(sampler, *fine);
  const auto adjacent = make_moon_stream_tile(sampler, {2, 13, 4097, 4096});
  for (unsigned row = 0; row < 33; ++row) {
    const auto a = row * 33 + 32, b = row * 33;
    const auto d = need(direction_from_cube_surface_address(
        {CubeFace::positive_y, 13, 4096, 4096, 1, row / 32.}));
    const auto p = need(sampler.surface_position({d.x, d.y, d.z}));
    check(fine->vertices[a] == V{p.x - fine->anchor.x, p.y - fine->anchor.y,
                                 p.z - fine->anchor.z} &&
              adjacent->vertices[b] == V{p.x - adjacent->anchor.x,
                                         p.y - adjacent->anchor.y,
                                         p.z - adjacent->anchor.z} &&
              fine->normals[a] == adjacent->normals[b] &&
              fine->colors[a] == adjacent->colors[b],
          "Adjacent fine tiles share exact source boundary points/stencil "
          "normals/tints before local-anchor roundtrip");
  }
  const auto repeated = make_moon_stream_tile(sampler, fine->key);
  check(repeated->anchor == fine->anchor &&
            repeated->vertices == fine->vertices &&
            repeated->normals == fine->normals &&
            repeated->colors == fine->colors &&
            repeated->elevations == fine->elevations &&
            repeated->indices == fine->indices,
        "Repeated pure generation retains every owned byte value");
  for (unsigned budget : {0U, 5U, 385U, std::numeric_limits<unsigned>::max()})
    rejects(
        [&] {
          (void)plan_moon_tiles(
              owner, {static_cast<double>(owner.radius_metres), 0, 0}, budget);
        },
        "Invalid budgets refuse before generating cover");
  for (const auto& point : std::array<MoonFixedPositionMetres, 4>{
           {{0, 0, 0},
            {std::numeric_limits<double>::quiet_NaN(), 0, 0},
            {0, std::numeric_limits<double>::infinity(), 0},
            {1e11, 0, 0}}})
    rejects([&] { (void)plan_moon_tiles(owner, point); },
            "Invalid observer dimensions refuse");
  auto forged = owner;
  ++forged.radius_metres;
  rejects(
      [&] {
        (void)plan_moon_tiles(forged,
                              {static_cast<double>(owner.radius_metres), 0, 0});
      },
      "Forged source cannot select mesh plan");
  for (const auto& key : std::array<MoonStreamKey, 6>{{{6, 0, 0, 0},
                                                       {0, 14, 0, 0},
                                                       {0, 32, 0, 0},
                                                       {0, 0, 1, 0},
                                                       {0, 13, 8192, 0},
                                                       {0, 13, 0, 8192}}})
    rejects([&] { (void)make_moon_stream_tile(sampler, key); },
            "Invalid address refuses before mesh allocation/sample");
  auto foreign = owner;
  foreign.body.kind = BodyKind::planet;
  check(!MoonTerrainSampler::create(foreign),
        "No numeric moon-to-planet carrier can create source sampler");
  check(owner == snapshot && sampler.owner() == snapshot,
        "Planning/building never mutates source catalog, seed or sampler");
}
} // namespace
int main() {
  try {
    run();
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << e.what() << '\n';
  }
  std::cout << checks << " moon streaming checks, " << failures
            << " failures\n";
  return failures ? 1 : 0;
}
