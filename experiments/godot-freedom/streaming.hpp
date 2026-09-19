#pragma once

#include "snapshot.hpp"

#include <chrono>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <stop_token>
#include <tuple>
#include <vector>

namespace apsis_drift::godot_spike {

// Presentation topology only. World identity and height sampling remain C++.
inline constexpr unsigned kStreamIntervals = 32;
inline constexpr unsigned kStreamMaxTiles = 384;
inline constexpr unsigned kStreamMaxLod = 13;
using StreamPoint = PlanetFixedPositionMetres;

inline auto sub(StreamPoint a, StreamPoint b) -> StreamPoint {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline auto add(StreamPoint a, StreamPoint b) -> StreamPoint {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline auto mul(StreamPoint a, double s) -> StreamPoint {
  return {a.x * s, a.y * s, a.z * s};
}
inline auto dot(StreamPoint a, StreamPoint b) -> double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline auto cross(StreamPoint a, StreamPoint b) -> StreamPoint {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline auto length(StreamPoint a) -> double {
  return std::hypot(a.x, a.y, a.z);
}
inline auto unit(StreamPoint a) -> StreamPoint {
  const auto n = length(a);
  if (!std::isfinite(n) || n <= 0)
    throw std::invalid_argument("invalid stream direction");
  return mul(a, 1.0 / n);
}

struct StreamKey {
  unsigned face{}, lod{}, x{}, y{};
  auto operator<=>(const StreamKey&) const = default;
};
inline auto tile_key(const PlanetDescriptor& planet, StreamKey key)
    -> TerrainTileKey {
  if (key.face > 5 || key.lod > kStreamMaxLod || key.x >= (1U << key.lod) ||
      key.y >= (1U << key.lod))
    throw std::invalid_argument("invalid streaming tile key");
  return {planet.id, static_cast<CubeFace>(key.face),
          static_cast<std::uint8_t>(key.lod), key.x, key.y};
}
inline auto stream_id(StreamKey k) -> std::string {
  return std::to_string(k.face) + ":" + std::to_string(k.lod) + ":" +
         std::to_string(k.x) + ":" + std::to_string(k.y);
}
inline auto tile_point(const PlanetDescriptor& p, StreamKey k, double u,
                       double v, double height = 0) -> StreamPoint {
  return require(
      planet_fixed_from_terrain_address(p, {tile_key(p, k), u, v}, height));
}

// A complete six-face partition. Best-first subdivision enforces a hard budget;
// it never drops a visible region to fit that budget. No camera-facing culling
// in this first planner: rapid head turns always have a resident coarse
// surface.
inline auto plan_tiles(const PlanetDescriptor& planet, StreamPoint observer,
                       unsigned budget = kStreamMaxTiles)
    -> std::vector<StreamKey> {
  if (budget < 6 || budget > kStreamMaxTiles ||
      !std::isfinite(length(observer)) || length(observer) < 1 ||
      length(observer) > 1.0e10)
    throw std::invalid_argument("invalid stream observer or tile budget");
  struct Candidate {
    double score;
    StreamKey key;
  };
  auto lower = [](const Candidate& a, const Candidate& b) {
    return a.score == b.score ? a.key > b.key : a.score < b.score;
  };
  std::priority_queue<Candidate, std::vector<Candidate>, decltype(lower)> queue(
      lower);
  std::map<StreamKey, bool> leaves;
  const double radius = planet.radius.value * 1000.0;
  auto insert = [&](StreamKey k) {
    const auto center = tile_point(planet, k, .5, .5);
    const double width = 2.0 * radius / static_cast<double>(1U << k.lod);
    // A wide surrounding buffer naturally refines on both sides of the path.
    const double distance =
        std::max(80.0, length(sub(observer, center)) - width * .85 - 4000.0);
    leaves.emplace(k, true);
    if (k.lod < kStreamMaxLod) queue.push({width / distance, k});
  };
  for (unsigned face = 0; face < 6; ++face)
    insert({face, 0, 0, 0});
  while (!queue.empty() && leaves.size() + 3 <= budget) {
    auto best = queue.top();
    queue.pop();
    if (best.score < 1.6) break;
    leaves.erase(best.key);
    for (unsigned y = 0; y < 2; ++y)
      for (unsigned x = 0; x < 2; ++x)
        insert({best.key.face, best.key.lod + 1, best.key.x * 2 + x,
                best.key.y * 2 + y});
  }
  std::vector<StreamKey> result;
  for (const auto& [key, _] : leaves)
    result.push_back(key);
  return result;
}

struct StreamTile {
  StreamKey key;
  StreamPoint anchor;
  std::vector<StreamPoint> vertices, normals;
  std::vector<Rgb8> colors;
  std::vector<double> elevations;
  std::vector<std::int32_t> indices;
  auto bytes() const -> std::size_t {
    return vertices.size() * sizeof(StreamPoint) +
           normals.size() * sizeof(StreamPoint) + colors.size() * sizeof(Rgb8) +
           elevations.size() * sizeof(double) +
           indices.size() * sizeof(std::int32_t);
  }
};

inline auto build_stream_tile(const PlanetDescriptor& planet, StreamKey key,
                              unsigned source_lod, unsigned relief_version,
                              TerrainTileCache& cache)
    -> std::shared_ptr<const StreamTile> {
  if (source_lod > 10 || relief_version > kExperimentalReliefVersion)
    throw std::invalid_argument("invalid stream recipe");
  const auto address_key = tile_key(planet, key);
  auto tile = std::make_shared<StreamTile>();
  tile->key = key;
  tile->anchor = tile_point(planet, key, .5, .5);
  constexpr unsigned n = kStreamIntervals + 1;
  std::vector<StreamPoint> fixed;
  // On coarse meshes, every vertex coincides with a canonical generator sample.
  // Read that immutable tile directly instead of generating thousands of tiny
  // high-LOD tiles merely to draw a distant hemisphere. Fine meshes use exactly
  // the same source-LOD interpolation as live flight.
  std::shared_ptr<const TerrainTile> coarse;
  if (key.lod <= source_lod) coarse = require(cache.get(planet, address_key));
  for (unsigned row = 0; row < n; ++row)
    for (unsigned col = 0; col < n; ++col) {
      const double u = static_cast<double>(col) / kStreamIntervals;
      const double v = static_cast<double>(row) / kStreamIntervals;
      const auto reference = tile_point(planet, key, u, v);
      TerrainSurfaceSample sample;
      if (coarse) {
        const auto& exact = require(coarse->sample_at(col * 2, row * 2)).get();
        sample = {{address_key, u, v},
                  static_cast<double>(exact.elevation_metres),
                  exact.color};
      } else
        sample = require(sample_planet_surface(
            planet, reference, static_cast<std::uint8_t>(source_lod), cache));
      sample = with_relief(sample, planet, reference, relief_version);
      const auto position =
          tile_point(planet, key, u, v, sample.elevation_metres);
      fixed.push_back(position);
      tile->vertices.push_back(sub(position, tile->anchor));
      tile->normals.push_back({});
      tile->colors.push_back(sample.color);
      tile->elevations.push_back(sample.elevation_metres);
    }
  auto triangle = [&](unsigned a, unsigned b, unsigned c, bool accumulate) {
    auto normal = cross(sub(tile->vertices[b], tile->vertices[a]),
                        sub(tile->vertices[c], tile->vertices[a]));
    if (dot(normal, add(tile->vertices[a], tile->anchor)) < 0) {
      std::swap(b, c);
      normal = mul(normal, -1);
    }
    // Godot's front faces are clockwise; normals remain explicitly outward.
    tile->indices.insert(
        tile->indices.end(),
        {static_cast<int>(a), static_cast<int>(c), static_cast<int>(b)});
    // A high-resolution edge may lie above OR below its coarse neighbour.
    // Cover both viewing directions instead of culling half of that skirt.
    if (!accumulate)
      tile->indices.insert(
          tile->indices.end(),
          {static_cast<int>(a), static_cast<int>(b), static_cast<int>(c)});
    if (accumulate)
      for (auto i : {a, b, c})
        tile->normals[i] = add(tile->normals[i], normal);
  };
  for (unsigned row = 0; row < kStreamIntervals; ++row)
    for (unsigned col = 0; col < kStreamIntervals; ++col) {
      const auto a = row * n + col;
      triangle(a, a + 1, a + n, true);
      triangle(a + 1, a + n + 1, a + n, true);
    }
  for (auto& normal : tile->normals)
    normal = unit(normal);
  // Near tiles need normals from the same continuous surface, not independent
  // one-sided triangle fans at each tile edge. A fixed metre-scale stencil also
  // matches across adjacent LODs. Extend the common recipe beyond the visible
  // detail fade (terrain.gdshader), not just the nearest LOD-7 patch.
  // Very distant orbital meshes retain their cheap mesh-derived normals.
  if (key.lod >= 5) {
    const double radius = planet.radius.value * 1000.0;
    auto surface_point = [&](StreamPoint probe) {
      const auto sample = with_relief(
          require(sample_planet_surface(
              planet, probe, static_cast<std::uint8_t>(source_lod), cache)),
          planet, probe, relief_version);
      return mul(unit(probe), radius + sample.elevation_metres);
    };
    for (std::size_t i = 0; i < fixed.size(); ++i) {
      const auto radial = unit(fixed[i]);
      const auto reference = mul(radial, radius);
      const auto east =
          unit(cross(std::abs(radial.z) < 0.9 ? StreamPoint{0, 0, 1}
                                              : StreamPoint{0, 1, 0},
                     radial));
      const auto north = cross(radial, east);
      constexpr double stencil_metres = 32.0;
      const auto dx =
          sub(surface_point(add(reference, mul(east, stencil_metres))),
              surface_point(sub(reference, mul(east, stencil_metres))));
      const auto dy =
          sub(surface_point(add(reference, mul(north, stencil_metres))),
              surface_point(sub(reference, mul(north, stencil_metres))));
      tile->normals[i] = unit(cross(dx, dy));
    }
  }
  // Skirts conceal cracks between unequal tessellations; they do NOT alter the
  // sampled physical surface. Exact stitched/morphed LOD edges are a later
  // pass.
  const double width =
      2.0 * planet.radius.value * 1000.0 / static_cast<double>(1U << key.lod);
  const double depth = std::clamp(width / 8.0, 200.0, 20000.0);
  for (unsigned edge = 0; edge < 4; ++edge) {
    unsigned previous_top = 0, previous_bottom = 0;
    for (unsigned i = 0; i < n; ++i) {
      const unsigned top = edge == 0   ? i
                           : edge == 1 ? i * n + n - 1
                           : edge == 2 ? (n - 1) * n + n - 1 - i
                                       : (n - 1 - i) * n;
      const auto bottom = static_cast<unsigned>(tile->vertices.size());
      tile->vertices.push_back(
          sub(sub(fixed[top], mul(unit(fixed[top]), depth)), tile->anchor));
      tile->normals.push_back(tile->normals[top]);
      tile->colors.push_back(tile->colors[top]);
      tile->elevations.push_back(tile->elevations[top] - depth);
      if (i) {
        triangle(previous_top, top, previous_bottom, false);
        triangle(top, bottom, previous_bottom, false);
      }
      previous_top = top;
      previous_bottom = bottom;
    }
  }
  return tile;
}

using TileMap = std::map<StreamKey, std::shared_ptr<const StreamTile>>;
struct StreamBatch {
  TileMap tiles;
  unsigned generated{}, reused{};
  double milliseconds{};
  auto bytes() const -> std::size_t {
    std::size_t sum = 0;
    for (const auto& [_, tile] : tiles)
      sum += tile->bytes();
    return sum;
  }
};

// One owned worker/future, at most one outstanding batch. No Godot API is
// called on the worker. Shared immutable tiles survive until the frontend
// commits the replacement cover; old cache entries then fall out of scope. Stop
// is checked between tiles, bounding shutdown/reset latency to a single tile
// build.
class PlanetStream {
  PlanetDescriptor planet;
  unsigned source_lod, relief_version;
  TileMap resident;
  std::vector<StreamKey> requested;
  std::future<StreamBatch> future;
  std::stop_source stop;

 public:
  PlanetStream(const PlanetDescriptor& p, unsigned lod, unsigned relief)
      : planet(p), source_lod(lod), relief_version(relief) {
    if (lod > 10 || relief > kExperimentalReliefVersion)
      throw std::invalid_argument("invalid stream recipe");
  }
  ~PlanetStream() {
    stop.request_stop();
    if (future.valid()) future.wait();
  }
  PlanetStream(const PlanetStream&) = delete;
  auto operator=(const PlanetStream&) -> PlanetStream& = delete;
  auto busy() const -> bool { return future.valid(); }
  auto request(StreamPoint observer) -> bool {
    const auto plan = plan_tiles(planet, observer); // Validate even while busy.
    if (busy() || plan == requested) return false;
    requested = plan;
    const auto token = stop.get_token();
    future = std::async(std::launch::async, [p = planet, lod = source_lod,
                                             relief = relief_version, plan,
                                             old = resident, token]() {
      const auto begin = std::chrono::steady_clock::now();
      StreamBatch batch;
      auto cache = require(TerrainTileCache::create(64));
      for (const auto& key : plan) {
        if (token.stop_requested()) break;
        if (auto it = old.find(key); it != old.end()) {
          batch.tiles.emplace(key, it->second);
          ++batch.reused;
        } else {
          batch.tiles.emplace(key,
                              build_stream_tile(p, key, lod, relief, cache));
          ++batch.generated;
        }
      }
      batch.milliseconds = std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - begin)
                               .count();
      return batch;
    });
    return true;
  }
  auto poll() -> std::optional<StreamBatch> {
    if (!future.valid() ||
        future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
      return {};
    auto batch = future.get();
    resident = batch.tiles;
    return batch;
  }
};
} // namespace apsis_drift::godot_spike
