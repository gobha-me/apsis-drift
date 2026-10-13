#include "../../src/godot/contact_geometry_internal.hpp"
#include "apsis_drift/godot/streaming.hpp"

#include <iostream>
#include <set>
#include <thread>

using namespace apsis_drift;
using namespace apsis_drift::godot_spike;

static auto check(bool condition, const char* message) -> void {
  if (!condition) throw std::runtime_error(message);
}
template <class F> static auto rejects(F function) -> void {
  try {
    function();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error("invalid streaming input accepted");
}
static auto complete_cover(const std::vector<StreamKey>& keys) -> void {
  std::array<double, 6> areas{};
  std::set<StreamKey> unique(keys.begin(), keys.end());
  check(unique.size() == keys.size() && keys.size() <= kStreamMaxTiles,
        "tile budget or duplicate key");
  for (auto key : keys) {
    areas[key.face] += std::ldexp(1.0, -2 * static_cast<int>(key.lod));
    while (key.lod) {
      --key.lod;
      key.x /= 2;
      key.y /= 2;
      check(!unique.contains(key), "overlapping ancestor and child");
    }
  }
  for (auto area : areas)
    check(area == 1.0, "planet cover has holes");
}
static auto await_batch(PlanetStream& stream) -> StreamBatch {
  const auto limit =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (std::chrono::steady_clock::now() < limit) {
    if (auto batch = stream.poll()) return std::move(*batch);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  throw std::runtime_error("stream worker timed out");
}
static auto selected_stream_contract() -> void {
  const auto owner = require(generate_physical_origin_system(Seed{42}, 2));
  const auto& planet =
      owner.catalog.planets[kOriginHomePlanetOrdinal].descriptor;
  const auto body = body_id(planet.id);
  for (const auto policy : {0U, 3U, std::numeric_limits<std::uint32_t>::max()})
    rejects([&] { PlanetStream bad(owner, body, policy); });
  auto forged = owner;
  ++forged.stellar_gm_km3_per_second2;
  rejects([&] { PlanetStream bad(forged, body, 2); });
  rejects([&] { PlanetStream bad(owner, body_id(owner.catalog.star.id), 2); });
  auto wrong = body;
  wrong.version = 99;
  rejects([&] { PlanetStream bad(owner, wrong, 2); });
  PlanetStream stream(owner, body, 2);
  check(stream.selected_owner() == owner && stream.selected_body() == body &&
            stream.selected_terrain_policy() == 2,
        "owned streaming lost its authenticated selection");
  const StreamPoint observer{planet.radius.value * 1000.0 + 1e8, 0, 0};
  check(stream.request(observer), "owned distant cover did not schedule");
  const auto batch = await_batch(stream);
  complete_cover([&] {
    std::vector<StreamKey> keys;
    for (const auto& [key, tile] : batch.tiles) {
      (void)tile;
      keys.push_back(key);
    }
    return keys;
  }());
  check(batch.tiles.size() == 6,
        "distant selected cover did not retain six faces");
  auto cache = require(TerrainTileCache::create(2));
  for (const auto& [key, tile] : batch.tiles) {
    const auto regenerated = build_stream_tile(owner, body, key, 2, cache);
    check(
        tile->vertices == regenerated->vertices &&
            tile->elevations == regenerated->elevations &&
            tile->colors == regenerated->colors &&
            tile->indices == regenerated->indices,
        "owned async job changed selected geometry or retained dangling state");
  }
  check(cache.size() == 0,
        "coarse detail stream constructed thousands of macro tiles");
  check(!stream.request(observer),
        "unchanged selected cover needlessly regenerated");
  std::cout << "Selected six-face local-detail cover: worker ms="
            << batch.milliseconds << "; bytes=" << batch.bytes() << '\n';
}
struct ElevatedTerrainFixture {
  PhysicalLocalSystem owner;
  BodyId body;
  StreamPoint observer;
  double elevation{};
  unsigned legacy_lod{};
};
static auto containing_lod(const PlanetDescriptor& planet,
                           const std::vector<StreamKey>& cover,
                           StreamPoint observer) -> unsigned {
  for (const auto& key : cover) {
    const auto address = require(terrain_address_from_planet_direction(
        planet, {observer.x, observer.y, observer.z},
        static_cast<std::uint8_t>(key.lod)));
    if (address.tile == tile_key(planet, key)) return key.lod;
  }
  throw std::runtime_error("complete cover omitted its observer direction");
}
static auto elevated_fixture() -> ElevatedTerrainFixture {
  // Select a real admitted procedural owner, not an overridden alpine sphere.
  // The fixed finite search also verifies that the regression trigger exists.
  for (std::uint64_t seed = 0; seed < 128; ++seed) {
    auto owner = require(generate_physical_local_system(Seed{seed}, 2));
    for (const auto& entry : owner.catalog.planets) {
      const auto& planet = entry.descriptor;
      if (planet.radius.value < 4000 ||
          planet.terrain_character == TerrainCharacter::plains ||
          planet.terrain_character == TerrainCharacter::oceanic)
        continue;
      auto cache = require(TerrainTileCache::create(1));
      const auto body = body_id(planet.id);
      const auto sampler =
          require(TerrainLocalDetailSampler::create(owner, body, {2}, cache));
      for (unsigned face = 0; face < 6; ++face) {
        for (const double u : {.125, .375, .625, .875}) {
          for (const double v : {.125, .375, .625, .875}) {
            const auto reference = tile_point(planet, {face, 0, 0, 0}, u, v);
            const auto sampled = require(sampler.sample_height(
                {reference.x, reference.y, reference.z}, 13));
            if (sampled.elevation_metres <= 8000) continue;
            const auto observer =
                mul(unit(reference), planet.radius.value * 1000.0 +
                                         sampled.elevation_metres + 2.0);
            const auto old_lod =
                containing_lod(planet, plan_tiles(planet, observer), observer);
            if (old_lod == kStreamMaxLod) continue;
            return {std::move(owner), body, observer, sampled.elevation_metres,
                    old_lod};
          }
        }
      }
    }
  }
  throw std::runtime_error("no elevated procedural regression fixture found");
}
static auto selected_clearance_planning() -> void {
  const auto fixture = elevated_fixture();
  const auto& owner = fixture.owner;
  const auto body = fixture.body;
  const auto& planet =
      require(find_local_system_planet(owner, PlanetId{body.value}))
          ->descriptor;
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  for (const auto bad :
       std::array{StreamPoint{nan, 0, 0}, StreamPoint{0, INFINITY, 0},
                  StreamPoint{}, StreamPoint{1e11, 0, 0}})
    rejects([&] { (void)plan_selected_tiles(owner, body, 2, bad); });
  for (const auto budget : {0U, 5U, 385U})
    rejects([&] {
      (void)plan_selected_tiles(owner, body, 2, fixture.observer, budget);
    });
  // A deep observer would produce a negative virtual datum radius. Refuse it
  // rather than clamping/mirroring the angular point through the body center.
  rejects([&] {
    (void)plan_selected_tiles(owner, body, 2, unit(fixture.observer));
  });
  const auto cover = plan_selected_tiles(owner, body, 2, fixture.observer);
  complete_cover(cover);
  check(cover == plan_selected_tiles(owner, body, 2, fixture.observer),
        "selected clearance planner changed at an identical observer");
  check(containing_lod(planet, cover, fixture.observer) == kStreamMaxLod &&
            fixture.legacy_lod < kStreamMaxLod,
        "high-ground observer still uses a coarse top instead of certified "
        "finest facets");
  check(plan_selected_tiles(owner, body, 1, fixture.observer) ==
            plan_tiles(planet, fixture.observer),
        "legacy selected policy changed its datum planner");
  const auto orbit =
      mul(unit(fixture.observer), planet.radius.value * 1000.0 + 1e6);
  const auto orbital_cover = plan_selected_tiles(owner, body, 2, orbit);
  complete_cover(orbital_cover);
  check(orbital_cover == plan_selected_tiles(owner, body, 2, orbit),
        "selected orbital cover changed on a repeated query");
  PlanetStream stream(owner, body, 2);
  check(stream.request(fixture.observer),
        "selected elevated cover did not schedule");
  const auto batch = await_batch(stream);
  check(batch.tiles.size() == cover.size(),
        "selected worker ignored clearance plan");
  const auto id = require(
      apsis_drift::godot_spike::detail::locate_selected_contact_triangle(
          planet,
          {fixture.observer.x, fixture.observer.y, fixture.observer.z}));
  const StreamKey key{static_cast<unsigned>(id.face), 13, id.tile_x, id.tile_y};
  check(batch.tiles.contains(key),
        "elevated worker cover omitted finest containing tile");
  const auto& tile = batch.tiles.at(key);
  auto cache = require(TerrainTileCache::create(1));
  const auto triangle =
      require(apsis_drift::godot_spike::detail::build_selected_contact_triangle(
          owner, planet, kLocalDetailSavedContactSurface, id, cache));
  constexpr unsigned n = kStreamIntervals + 1;
  constexpr std::array<std::array<unsigned, 3>, 2> offsets{
      {{0, 1, n}, {1, n + 1, n}}};
  const auto start = id.cell_y * n + id.cell_x;
  for (unsigned corner = 0; corner < 3; ++corner) {
    const auto vertex =
        add(tile->vertices[start + offsets[id.half][corner]], tile->anchor);
    check(
        length(sub(vertex, triangle.vertices[corner])) < 4e-9,
        "actual high-ground worker top differs from selected contact vertices");
  }
  std::cout << "Selected elevated fixture: system=" << owner.catalog.id.value
            << "; body=" << body.value << "; elevation=" << fixture.elevation
            << "m; clearance=2m; old lod=" << fixture.legacy_lod
            << "; selected lod=13; cover=" << batch.tiles.size()
            << "; worker ms=" << batch.milliseconds << '\n';
}
auto main() -> int {
  try {
    selected_clearance_planning();
    selected_stream_contract();
    const auto planet = generate_planet_descriptor(Seed{42});
    const auto origin =
        require(planet_fixed_from_geodetic(planet, {.25, .4, 2500}));
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    for (auto bad : std::vector<StreamPoint>{
             {nan, 0, 0}, {0, INFINITY, 0}, {0, 0, 0}, {1e11, 0, 0}})
      rejects([&] { (void)plan_tiles(planet, bad); });
    for (unsigned budget : {0U, 5U, 385U})
      rejects([&] { (void)plan_tiles(planet, origin, budget); });
    for (auto bad : std::vector<StreamKey>{
             {6, 0, 0, 0}, {0, 14, 0, 0}, {0, 0, 1, 0}, {0, 1, 0, 2}})
      rejects([&] { (void)tile_key(planet, bad); });
    unsigned maximum = 0;
    for (int step = 0; step <= 48; ++step) {
      const double longitude = -std::numbers::pi + std::numbers::pi * step / 24;
      for (double latitude :
           {-.8, 0.0, .8, -std::numbers::pi / 2, std::numbers::pi / 2}) {
        const auto position = require(
            planet_fixed_from_geodetic(planet, {latitude, longitude, 2500}));
        const auto keys = plan_tiles(planet, position);
        complete_cover(keys);
        maximum = std::max(maximum, static_cast<unsigned>(keys.size()));
      }
    }
    for (double altitude : {1000.0, 100000.0, 1e6, 1e7, 1e8})
      complete_cover(plan_tiles(planet, require(planet_fixed_from_geodetic(
                                            planet, {.25, .4, altitude}))));
    auto cache = require(TerrainTileCache::create(16));
    auto a = build_stream_tile(planet, {0, 0, 0, 0}, 8, 1, cache);
    auto b = build_stream_tile(planet, {2, 0, 0, 0}, 8, 1, cache);
    constexpr unsigned n = kStreamIntervals + 1;
    auto near_left = build_stream_tile(planet, {0, 9, 255, 255}, 8, 1, cache);
    auto near_right = build_stream_tile(planet, {0, 9, 256, 255}, 8, 1, cache);
    for (unsigned row = 0; row < n; ++row)
      check(length(sub(near_left->normals[row * n + n - 1],
                       near_right->normals[row * n])) < 1e-5,
            "shared terrain edge has a lighting seam");
    // The old method changed at LOD 7: identical surface positions on the
    // LOD-6/7 boundary could have completely different lighting normals.
    for (unsigned coarse_lod : {5U, 6U, 7U}) {
      const unsigned half = 1U << (coarse_lod - 1);
      auto coarse_edge = build_stream_tile(
          planet, {0, coarse_lod, half - 1, half - 1}, 8, 1, cache);
      auto fine_edge = build_stream_tile(
          planet, {0, coarse_lod + 1, half * 2, (half - 1) * 2}, 8, 1, cache);
      for (unsigned row = 0; row <= kStreamIntervals / 2; ++row) {
        const auto left = row * n + n - 1;
        const auto right = row * 2 * n;
        check(length(sub(add(coarse_edge->vertices[left], coarse_edge->anchor),
                         add(fine_edge->vertices[right], fine_edge->anchor))) <
                  1e-5,
              "cross-LOD canonical position mismatch");
        check(length(sub(coarse_edge->normals[left],
                         fine_edge->normals[right])) < 1e-5,
              "cross-LOD normal recipe changed at shared position");
      }
    }
    for (unsigned row = 0; row < n; ++row) {
      const auto ai = row * n + n - 1, bi = row * n;
      check(length(sub(add(a->vertices[ai], a->anchor),
                       add(b->vertices[bi], b->anchor))) < 1e-6,
            "cube-face edge mismatch");
    }
    for (auto tile : {a, b}) {
      check(tile->vertices.size() == n * n + 4 * n, "mesh buffer dimensions");
      for (auto i : tile->indices)
        check(i >= 0 && static_cast<std::size_t>(i) < tile->vertices.size(),
              "index out of bounds");
      for (unsigned i = 0; i < tile->vertices.size(); ++i) {
        check(std::isfinite(length(tile->vertices[i])), "nonfinite mesh");
        check(std::abs(length(tile->normals[i]) - 1) < 1e-9, "nonunit normal");
        check(dot(tile->normals[i], add(tile->vertices[i], tile->anchor)) > 0,
              "inward terrain normal");
      }
    }
    // Coarse samples must be from the same canonical v1 surface as fine flight.
    for (unsigned i : {0U, 16U * n + 16U, n * n - 1}) {
      const auto fixed = add(a->vertices[i], a->anchor);
      const auto sample =
          with_relief(require(sample_planet_surface(planet, fixed, 8, cache)),
                      planet, fixed, 1);
      check(std::abs(sample.elevation_metres - a->elevations[i]) <= 1.0,
            "coarse and flight terrain disagree");
    }
    const auto address =
        require(terrain_address_from_planet_fixed(planet, origin, 13));
    const StreamKey fine{static_cast<unsigned>(address.tile.face), 13,
                         address.tile.x, address.tile.y};
    auto first = build_stream_tile(planet, fine, 8, 1, cache);
    for (unsigned face = 0; face < 6; ++face)
      (void)build_stream_tile(planet, {face, 1, 0, 0}, 8, 1, cache);
    auto returned = build_stream_tile(planet, fine, 8, 1, cache);
    check(first->vertices == returned->vertices &&
              first->normals == returned->normals &&
              first->elevations == returned->elevations,
          "regenerated terrain changed on return");
    const auto frame =
        require(make_local_tangent_frame(planet, {.25, .4, 2500}));
    const auto close =
        require(planet_fixed_from_local(frame, {.001, .002, .003}));
    const auto local = require(local_from_planet_fixed(frame, close));
    check(std::abs(local.east - .001) < 1e-8, "camera-relative precision lost");
    PlanetStream stream(planet, 8, 1);
    check(stream.request(origin), "first request not scheduled");
    check(!stream.request(origin), "unbounded concurrent request");
    auto initial = await_batch(stream);
    check(initial.tiles.size() <= kStreamMaxTiles &&
              initial.bytes() < 48 * 1024 * 1024,
          "resident mesh budget");
    check(!stream.request(origin), "identical cover needlessly regenerated");
    const auto moved =
        require(planet_fixed_from_geodetic(planet, {.25, .402, 2500}));
    check(stream.request(moved),
          "moving observer did not schedule replacement");
    auto next = await_batch(stream);
    check(next.reused > 0 && next.generated > 0,
          "cache failed to reuse/replenish tiles");
    check(stream.request(origin), "return request not scheduled");
    auto back = await_batch(stream);
    check(back.tiles.size() == initial.tiles.size(), "return coverage changed");
    for (const auto& [key, tile] : initial.tiles)
      check(back.tiles.at(key)->vertices == tile->vertices,
            "return mesh differs");
    std::cout << "Planet stream: 245 global/polar covers, orbital levels, face "
                 "seams, finite/index bounds, "
              << "revisit identity, floating origin and async replacement "
                 "passed; max tiles="
              << maximum << "; initial bytes=" << initial.bytes()
              << "; worker ms=" << initial.milliseconds
              << "; moved generated/reused=" << next.generated << "/"
              << next.reused << '\n';
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
