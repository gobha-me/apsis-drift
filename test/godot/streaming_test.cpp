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
  for (const auto policy : {0U, 4U, std::numeric_limits<std::uint32_t>::max()})
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
static auto sea_surface_stream_contract() -> void {
  const auto owner = require(generate_physical_origin_system(Seed{42}, 2));
  const auto& planet =
      owner.catalog.planets[kOriginHomePlanetOrdinal].descriptor;
  const auto body = body_id(planet.id);
  auto cache = require(TerrainTileCache::create(2));
  TileMap cover;
  std::size_t wet{}, dry{};
  for (unsigned face = 0; face < 6; ++face) {
    const StreamKey key{face, 0, 0, 0};
    const auto raw = build_stream_tile(owner, body, key, 2, cache);
    const auto top = build_stream_tile(owner, body, key, 3, cache);
    check(
        raw->wetness.empty() && top->wetness.size() == top->vertices.size(),
        "sea attributes changed historical buffers or lost top/skirt entries");
    constexpr unsigned n = kStreamIntervals + 1;
    for (unsigned i = 0; i < n * n; ++i) {
      const bool submerged = raw->elevations[i] <= 0;
      check(top->elevations[i] == std::max(raw->elevations[i], 0.0) &&
                top->wetness[i] == (submerged ? 1.0 : 0.0),
            "sea top or raw datum classification differs from authoritative "
            "recipe2");
      if (submerged) {
        ++wet;
        check(top->colors[i] == planet.palette.shallow_water,
              "wet vertex retained a contradictory land color");
      } else {
        ++dry;
        check(top->vertices[i] == raw->vertices[i] &&
                  top->colors[i] == raw->colors[i],
              "sea selection changed a dry vertex");
      }
    }
    cover.emplace(key, top);
  }
  check(wet > 0 && dry > 0,
        "fixed Home sea fixture lost its land/water coverage");
  stream_morph_detail::validate_cover(cover);
  const auto& coarse = *cover.at({0, 0, 0, 0});
  const auto fine = build_stream_tile(owner, body, {0, 1, 0, 0}, 3, cache);
  const auto endpoint = stream_morph_detail::endpoints(*fine, coarse, true);
  check(endpoint.coarse_wetness.size() == fine->wetness.size() &&
            endpoint.coarse_wetness[1] ==
                (coarse.wetness[0] + coarse.wetness[1]) * .5,
        "sea morph lost barycentric wetness correspondence");
  for (const auto value : endpoint.coarse_wetness)
    check(std::isfinite(value) && value >= 0 && value <= 1,
          "sea morph emitted invalid material weights");
  auto broken = std::make_shared<StreamTile>(coarse);
  broken->wetness.pop_back();
  auto bad = cover;
  bad[{0, 0, 0, 0}] = broken;
  rejects([&] { stream_morph_detail::validate_cover(bad); });
  broken->wetness = coarse.wetness;
  broken->wetness[0] = .5;
  rejects([&] { stream_morph_detail::validate_cover(bad); });
  broken->wetness[0] = std::numeric_limits<double>::quiet_NaN();
  rejects([&] { stream_morph_detail::validate_cover(bad); });
  PlanetStream stream(owner, body, 3);
  const StreamPoint observer{planet.radius.value * 1000.0 + 1e8, 0, 0};
  check(stream.request(observer), "sea policy worker did not schedule");
  const auto batch = await_batch(stream);
  check(batch.tiles.size() == 6, "sea policy lost its distant six-face cover");
  for (const auto& [key, tile] : batch.tiles)
    check(tile->vertices == cover.at(key)->vertices &&
              tile->wetness == cover.at(key)->wetness,
          "sea worker diverged from synchronous C++ geometry");
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
static auto selected_morph_contract() -> void {
  // Actual parent/four-child pair retained by the Oct14 coarse-to-eye fixture.
  // No terrain search, height override or manufactured flight is involved.
  const auto owner =
      require(generate_physical_local_system(Seed{2910174744474113395ULL}, 2));
  const auto body = body_id(PlanetId{7198692597331823481ULL});
  const StreamKey parent{0, 10, 513, 511};
  auto cache = require(TerrainTileCache::create(64));
  const auto build = [&](StreamKey key) {
    return build_stream_tile(owner, body, key, 2, cache);
  };
  TileMap old;
  for (unsigned face = 0; face < 6; ++face) {
    const StreamKey key{face, 0, 0, 0};
    old.emplace(key, build(key));
  }
  // Complete cover containing the recorded parent; only its ancestor path is
  // refined. All unchanged regions share the same actual immutable meshes.
  for (unsigned depth = 0; depth < parent.lod; ++depth) {
    const StreamKey key{parent.face, depth, parent.x >> (parent.lod - depth),
                        parent.y >> (parent.lod - depth)};
    check(old.erase(key) == 1, "retained morph parent path is incomplete");
    for (unsigned y = 0; y < 2; ++y)
      for (unsigned x = 0; x < 2; ++x) {
        const StreamKey child{key.face, depth + 1, key.x * 2 + x,
                              key.y * 2 + y};
        old.emplace(child, build(child));
      }
  }
  auto next = old;
  next.erase(parent);
  for (unsigned y = 0; y < 2; ++y)
    for (unsigned x = 0; x < 2; ++x) {
      const StreamKey child{parent.face, parent.lod + 1, parent.x * 2 + x,
                            parent.y * 2 + y};
      next.emplace(child, build(child));
    }
  const auto initial = make_stream_transition({}, old);
  check(initial.draw_tiles == old && initial.morphs.empty() &&
            initial.bytes() == 0 &&
            make_stream_transition(old, old).morphs.empty(),
        "initial/unchanged cover invented a morph");
  const auto forward = make_stream_transition(old, next);
  const auto backward = make_stream_transition(next, old);
  const auto repeated = make_stream_transition(old, next);
  constexpr unsigned n = kStreamIntervals + 1;
  constexpr unsigned vertices = n * n + 4 * n;
  check(forward.draw_tiles == next && backward.draw_tiles == next &&
            forward.morphs.size() == 4 && backward.morphs.size() == 4 &&
            forward.bytes() ==
                4 * vertices *
                    (2 * sizeof(StreamPoint) + sizeof(std::array<double, 3>) +
                     sizeof(double)),
        "bidirectional handoff lost fine topology or endpoint byte bound");
  const auto& coarse = *old.at(parent);
  // Independent oracle walks the actual index buffer and solves general 2D
  // triangle barycentrics. It does not use the production cell/half selection.
  const auto oracle = [&](double u, double v) {
    struct Sample {
      StreamPoint position{}, normal{};
      std::array<double, 3> color{};
      double elevation{};
    };
    for (unsigned at = 0; at < 6 * kStreamIntervals * kStreamIntervals;
         at += 3) {
      std::array<unsigned, 3> indices{};
      std::array<double, 3> x{}, y{};
      for (unsigned i = 0; i < 3; ++i) {
        indices[i] = static_cast<unsigned>(coarse.indices[at + i]);
        x[i] = static_cast<double>(indices[i] % n) / kStreamIntervals;
        y[i] = static_cast<double>(indices[i] / n) / kStreamIntervals;
      }
      const auto determinant =
          (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2]);
      std::array<double, 3> w{
          ((y[1] - y[2]) * (u - x[2]) + (x[2] - x[1]) * (v - y[2])) /
              determinant,
          ((y[2] - y[0]) * (u - x[2]) + (x[0] - x[2]) * (v - y[2])) /
              determinant,
          0};
      w[2] = 1 - w[0] - w[1];
      if (w[0] < -1e-12 || w[1] < -1e-12 || w[2] < -1e-12) continue;
      Sample result;
      for (unsigned i = 0; i < 3; ++i) {
        const auto index = indices[i];
        result.position =
            add(result.position,
                mul(add(coarse.vertices[index], coarse.anchor), w[i]));
        result.normal = add(result.normal, mul(coarse.normals[index], w[i]));
        result.color[0] += coarse.colors[index].red / 255.0 * w[i];
        result.color[1] += coarse.colors[index].green / 255.0 * w[i];
        result.color[2] += coarse.colors[index].blue / 255.0 * w[i];
        result.elevation += coarse.elevations[index] * w[i];
      }
      return result;
    }
    throw std::runtime_error("actual coarse mesh omitted a tested UV point");
  };
  const auto check_endpoints = [&](const StreamTransition& transition) {
    for (const auto& [key, morph] : transition.morphs) {
      const auto& fine = *transition.draw_tiles.at(key);
      check(morph.coarse_key == parent &&
                morph.coarse_vertices.size() == vertices &&
                morph.coarse_normals.size() == vertices &&
                morph.coarse_colors.size() == vertices &&
                morph.coarse_elevations.size() == vertices,
            "morph endpoint dimensions or ancestry changed");
      const double factor =
          std::ldexp(1.0, static_cast<int>(key.lod - parent.lod));
      for (const auto probe :
           std::array<std::array<unsigned, 2>, 9>{{{0, 0},
                                                   {32, 0},
                                                   {0, 32},
                                                   {32, 32},
                                                   {1, 1},
                                                   {7, 22},
                                                   {21, 7},
                                                   {31, 17},
                                                   {16, 16}}}) {
        const unsigned index = probe[1] * n + probe[0];
        const double u =
            (key.x + static_cast<double>(probe[0]) / kStreamIntervals) /
                factor -
            parent.x;
        const double v =
            (key.y + static_cast<double>(probe[1]) / kStreamIntervals) /
                factor -
            parent.y;
        const auto expected = oracle(u, v);
        check(length(sub(add(morph.coarse_vertices[index], fine.anchor),
                         expected.position)) < 1e-6 &&
                  length(sub(morph.coarse_normals[index], expected.normal)) <
                      1e-12 &&
                  std::abs(morph.coarse_elevations[index] -
                           expected.elevation) < 1e-9,
              "morph endpoint is not the actual indexed coarse triangle");
        for (unsigned channel = 0; channel < 3; ++channel)
          check(std::abs(morph.coarse_colors[index][channel] -
                         expected.color[channel]) < 1e-12,
                "coarse endpoint substituted a palette sample");
      }
      for (unsigned edge = 0; edge < 4; ++edge)
        for (unsigned i = 0; i < n; ++i) {
          const unsigned top = edge == 0   ? i
                               : edge == 1 ? i * n + n - 1
                               : edge == 2 ? (n - 1) * n + n - 1 - i
                                           : (n - 1 - i) * n;
          const unsigned bottom = n * n + edge * n + i;
          const auto offset = sub(fine.vertices[bottom], fine.vertices[top]);
          check(
              length(sub(sub(morph.coarse_vertices[bottom],
                             morph.coarse_vertices[top]),
                         offset)) < 1e-8 &&
                  dot(offset, add(morph.coarse_vertices[top], fine.anchor)) < 0,
              "morph skirt changed its fine inward offset or crossed the top");
        }
    }
  };
  check_endpoints(forward);
  for (const auto& [key, morph] : forward.morphs) {
    const auto& reverse = backward.morphs.at(key);
    const auto& repeat = repeated.morphs.at(key);
    check(morph.refining && !reverse.refining &&
              morph.coarse_vertices == reverse.coarse_vertices &&
              morph.coarse_normals == reverse.coarse_normals &&
              morph.coarse_colors == reverse.coarse_colors &&
              morph.coarse_elevations == reverse.coarse_elevations &&
              morph.coarse_vertices == repeat.coarse_vertices,
          "reverse/repeated handoff changed endpoints or direction");
  }
  const StreamKey left{0, 11, 1026, 1022}, right{0, 11, 1027, 1022};
  for (unsigned row = 0; row < n; ++row) {
    const auto a = row * n + n - 1, b = row * n;
    const auto& l = forward.morphs.at(left);
    const auto& r = forward.morphs.at(right);
    check(length(sub(add(l.coarse_vertices[a], next.at(left)->anchor),
                     add(r.coarse_vertices[b], next.at(right)->anchor))) <
                  1e-6 &&
              length(sub(l.coarse_normals[a], r.coarse_normals[b])) < 1e-12 &&
              l.coarse_colors[a] == r.coarse_colors[b] &&
              l.coarse_elevations[a] == r.coarse_elevations[b],
          "adjacent child endpoint positions or attributes expose a seam");
  }
  auto multilevel = next;
  multilevel.erase(left);
  for (unsigned y = 0; y < 2; ++y)
    for (unsigned x = 0; x < 2; ++x) {
      const StreamKey child{0, 12, left.x * 2 + x, left.y * 2 + y};
      multilevel.emplace(child, build(child));
    }
  const auto multi = make_stream_transition(old, multilevel);
  check(multi.morphs.size() == 7 && multi.draw_tiles == multilevel,
        "multi-level common refinement lost a leaf");
  check_endpoints(multi);
  // One batch can refine the recorded face0 parent and coarsen face1. The
  // common refinement must keep both sets of actual fine leaves without
  // overlap.
  auto mixed_old = old;
  mixed_old.erase({1, 0, 0, 0});
  for (unsigned y = 0; y < 2; ++y)
    for (unsigned x = 0; x < 2; ++x) {
      const StreamKey key{1, 1, x, y};
      mixed_old.emplace(key, build(key));
    }
  const auto mixed = make_stream_transition(mixed_old, next);
  check(mixed.morphs.size() == 8 &&
            mixed.draw_tiles.size() == next.size() + 3 &&
            mixed.draw_tiles.size() <= mixed_old.size() + next.size(),
        "mixed common refinement dropped a direction or exceeded two covers");
  std::vector<StreamKey> draw_keys;
  for (const auto& [key, tile] : mixed.draw_tiles) {
    check(mixed_old.contains(key) || next.contains(key),
          "common refinement invented a third mesh cover");
    (void)tile;
    draw_keys.push_back(key);
    if (const auto morph = mixed.morphs.find(key); morph != mixed.morphs.end())
      check(morph->second.refining == (key.face == 0),
            "mixed replacement reversed a fine weight");
  }
  complete_cover(draw_keys);

  rejects([&] { (void)make_stream_transition({}, {}); });
  auto malformed = next;
  malformed.erase(left);
  rejects([&] { (void)make_stream_transition(old, malformed); });
  malformed = next;
  malformed.emplace(parent, old.at(parent));
  rejects([&] { (void)make_stream_transition(old, malformed); });
  malformed = next;
  malformed[left].reset();
  rejects([&] { (void)make_stream_transition(old, malformed); });
  malformed.clear();
  for (unsigned i = 0; i <= kStreamMaxTiles; ++i)
    malformed.emplace(StreamKey{0, 13, i, 0}, next.at(left));
  rejects([&] { (void)make_stream_transition(old, malformed); });
  for (const auto key :
       std::array{StreamKey{6, 11, 1026, 1022}, StreamKey{0, 14, 1026, 1022},
                  StreamKey{0, 11, 2048, 1022}}) {
    malformed = next;
    auto invalid = std::make_shared<StreamTile>(*malformed.at(left));
    invalid->key = key;
    malformed.erase(left);
    malformed[key] = std::move(invalid);
    rejects([&] { (void)make_stream_transition(old, malformed); });
  }
  // Mutate only a copied input mesh, never the immutable canonical fixture.
  const auto reject_tile = [&](auto corrupt) {
    auto bad = next;
    auto tile = std::make_shared<StreamTile>(*bad.at(left));
    corrupt(*tile);
    bad[left] = std::move(tile);
    rejects([&] { (void)make_stream_transition(old, bad); });
  };
  reject_tile([](StreamTile& tile) { tile.key.face = 6; });
  reject_tile([](StreamTile& tile) { tile.vertices.pop_back(); });
  reject_tile([](StreamTile& tile) { tile.normals.pop_back(); });
  reject_tile([](StreamTile& tile) { tile.colors.pop_back(); });
  reject_tile([](StreamTile& tile) { tile.elevations.pop_back(); });
  reject_tile([](StreamTile& tile) { tile.indices.pop_back(); });
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  reject_tile([&](StreamTile& tile) { tile.anchor.x = nan; });
  reject_tile([&](StreamTile& tile) { tile.vertices[0].x = nan; });
  reject_tile([&](StreamTile& tile) { tile.normals[0].x = nan; });
  reject_tile([&](StreamTile& tile) { tile.elevations[0] = nan; });
  reject_tile([](StreamTile& tile) { tile.normals[0] = {}; });
  reject_tile([](StreamTile& tile) { tile.indices[0] = -1; });
  reject_tile([](StreamTile& tile) { tile.indices[0] = vertices; });
  reject_tile([](StreamTile& tile) { tile.indices[1] = tile.indices[2]; });
  reject_tile([](StreamTile& tile) {
    tile.vertices[n * n] = add(
        tile.vertices[0], mul(unit(add(tile.vertices[0], tile.anchor)), 200));
  });
  auto changed = old;
  auto altered = std::make_shared<StreamTile>(*old.at(parent));
  altered->vertices[0].x += 1;
  changed[parent] = altered;
  rejects([&] { (void)make_stream_transition(old, changed); });
  auto overflow_old = old;
  auto overflow_next = next;
  auto huge = std::make_shared<StreamTile>(*old.at(parent));
  huge->anchor.x = 1e308;
  overflow_old[parent] = huge;
  huge = std::make_shared<StreamTile>(*next.at(left));
  huge->anchor.x = -1e308;
  overflow_next[left] = huge;
  rejects([&] { (void)make_stream_transition(overflow_old, overflow_next); });
  check(make_stream_transition(old, next).draw_tiles == next,
        "refused correspondence mutated the accepted immutable covers");
  std::cout << "Actual retained parent/children: bidirectional and multilevel "
               "coarse-triangle endpoints, shared edge, skirts, byte bounds "
               "and malformed data passed; endpoint bytes="
            << forward.bytes() << '\n';
}

auto main() -> int {
  try {
    selected_morph_contract();
    selected_clearance_planning();
    selected_stream_contract();
    sea_surface_stream_contract();
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
    check(initial.transition.draw_tiles == initial.tiles &&
              initial.transition.morphs.empty(),
          "first worker batch invented a transition");
    check(!stream.request(origin), "identical cover needlessly regenerated");
    const auto moved =
        require(planet_fixed_from_geodetic(planet, {.25, .402, 2500}));
    check(stream.request(moved),
          "moving observer did not schedule replacement");
    auto next = await_batch(stream);
    check(next.reused > 0 && next.generated > 0,
          "cache failed to reuse/replenish tiles");
    check(!next.transition.morphs.empty() &&
              next.transition.draw_tiles.size() <=
                  initial.tiles.size() + next.tiles.size() &&
              next.transition.bytes() > 0,
          "replacement worker omitted its bounded mesh correspondence");
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
