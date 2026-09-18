#include "streaming.hpp"

#include <iostream>
#include <set>
#include <thread>

using namespace apsis_drift;
using namespace apsis_drift::godot_spike;

static auto check(bool condition, const char *message) -> void {
  if (!condition)
    throw std::runtime_error(message);
}
template <class F> static auto rejects(F function) -> void {
  try {
    function();
  } catch (const std::invalid_argument &) {
    return;
  }
  throw std::runtime_error("invalid streaming input accepted");
}
static auto complete_cover(const std::vector<StreamKey> &keys) -> void {
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
static auto await_batch(PlanetStream &stream) -> StreamBatch {
  const auto limit =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (std::chrono::steady_clock::now() < limit) {
    if (auto batch = stream.poll())
      return std::move(*batch);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  throw std::runtime_error("stream worker timed out");
}
auto main() -> int {
  try {
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
                         add(fine_edge->vertices[right], fine_edge->anchor))) < 1e-5,
              "cross-LOD canonical position mismatch");
        check(length(sub(coarse_edge->normals[left], fine_edge->normals[right])) < 1e-5,
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
    for (const auto &[key, tile] : initial.tiles)
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
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
