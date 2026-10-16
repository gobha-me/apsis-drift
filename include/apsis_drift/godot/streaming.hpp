#pragma once

#include "apsis_drift/godot/saved_contact_geometry.hpp"
#include "apsis_drift/godot/snapshot.hpp"
#include "apsis_drift/terrain_local_detail.hpp"
#include "apsis_drift/terrain_sea_surface.hpp"

#include <algorithm>
#include <array>
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
  result.reserve(leaves.size());
  for (const auto& [key, _] : leaves)
    result.push_back(key);
  return result;
}

// Policy2 planner distance is clearance above the selected radial height,
// rather than altitude above the spherical datum. Only the planner observer
// changes; physical camera/terrain points and the old planner stay untouched.
inline auto plan_selected_tiles(const PhysicalLocalSystem& owner, BodyId body,
                                std::uint32_t terrain_policy,
                                StreamPoint observer,
                                unsigned budget = kStreamMaxTiles)
    -> std::vector<StreamKey> {
  const double observer_radius = length(observer);
  // Check raw dimensions/numerics before evaluating any source sample.
  if (budget < 6 || budget > kStreamMaxTiles ||
      !std::isfinite(observer_radius) || observer_radius < 1 ||
      observer_radius > 1.0e10)
    throw std::invalid_argument(
        "invalid selected stream observer or tile budget");
  if (!saved_contact_recipe(terrain_policy))
    throw std::invalid_argument("unsupported selected stream terrain policy");
  auto cache = require(TerrainTileCache::create(1));
  const auto sampler = TerrainLocalDetailSampler::create(
      owner, body, {require(terrain_surface_raw_recipe(terrain_policy))},
      cache);
  if (!sampler)
    throw std::invalid_argument("invalid selected stream owner/body");
  if (terrain_policy == 1)
    return plan_tiles(sampler->planet(), observer, budget);
  const auto sample = sampler->sample_height(
      {observer.x, observer.y, observer.z}, kStreamMaxLod);
  if (!sample)
    throw std::invalid_argument("selected stream surface is unavailable");
  const double planner_radius =
      observer_radius - require(select_terrain_sea_surface(
                                    terrain_policy, sample->elevation_metres))
                            .elevation_metres;
  if (!std::isfinite(planner_radius) || planner_radius < 1 ||
      planner_radius > 1.0e10)
    throw std::invalid_argument("unsafe selected stream clearance radius");
  const auto planner_observer = mul(observer, planner_radius / observer_radius);
  return plan_tiles(sampler->planet(), planner_observer, budget);
}

struct StreamTile {
  StreamKey key;
  StreamPoint anchor;
  std::vector<StreamPoint> vertices, normals;
  std::vector<Rgb8> colors;
  std::vector<double> elevations;
  // Empty for historical policies; selected sea-top vertices carry raw wetness.
  std::vector<double> wetness;
  std::vector<std::int32_t> indices;
  auto bytes() const -> std::size_t {
    return vertices.size() * sizeof(StreamPoint) +
           normals.size() * sizeof(StreamPoint) + colors.size() * sizeof(Rgb8) +
           (elevations.size() + wetness.size()) * sizeof(double) +
           indices.size() * sizeof(std::int32_t);
  }
};

inline auto build_stream_tile_impl(const PlanetDescriptor& planet,
                                   StreamKey key, unsigned source_lod,
                                   unsigned relief_version,
                                   TerrainTileCache& cache,
                                   const TerrainLocalDetailSampler* selected,
                                   std::uint32_t surface_policy = 0)
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
  if (!selected && key.lod <= source_lod)
    coarse = require(cache.get(planet, address_key));
  for (unsigned row = 0; row < n; ++row)
    for (unsigned col = 0; col < n; ++col) {
      const double u = static_cast<double>(col) / kStreamIntervals;
      const double v = static_cast<double>(row) / kStreamIntervals;
      const auto reference = tile_point(planet, key, u, v);
      TerrainSurfaceSample sample;
      if (selected) {
        const auto physical = require(
            selected->sample_height({reference.x, reference.y, reference.z},
                                    static_cast<std::uint8_t>(key.lod)));
        sample = {
            {address_key, u, v}, physical.elevation_metres, physical.color};
      } else if (coarse) {
        const auto& exact =
            require(coarse->sample_at(static_cast<std::size_t>(col) * 2,
                                      static_cast<std::size_t>(row) * 2))
                .get();
        sample = {{address_key, u, v},
                  static_cast<double>(exact.elevation_metres),
                  exact.color};
      } else
        sample = require(sample_planet_surface(
            planet, reference, static_cast<std::uint8_t>(source_lod), cache));
      if (!selected)
        sample = with_relief(sample, planet, reference, relief_version);
      if (surface_policy == kSeaDatumTerrainPolicy) {
        const auto top = require(select_terrain_sea_surface(
            surface_policy, sample.elevation_metres));
        sample.elevation_metres = top.elevation_metres;
        tile->wetness.push_back(top.wet_datum ? 1.0 : 0.0);
        if (top.wet_datum) sample.color = planet.palette.shallow_water;
      }
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
      double elevation{};
      if (selected) {
        elevation = require(selected->sample_height(
                                {probe.x, probe.y, probe.z},
                                static_cast<std::uint8_t>(source_lod)))
                        .elevation_metres;
      } else {
        const auto sample = with_relief(
            require(sample_planet_surface(
                planet, probe, static_cast<std::uint8_t>(source_lod), cache)),
            planet, probe, relief_version);
        elevation = sample.elevation_metres;
      }
      if (surface_policy == kSeaDatumTerrainPolicy)
        elevation =
            require(select_terrain_sea_surface(surface_policy, elevation))
                .elevation_metres;
      return mul(unit(probe), radius + elevation);
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
      if (!tile->wetness.empty()) tile->wetness.push_back(tile->wetness[top]);
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

inline auto build_stream_tile(const PlanetDescriptor& planet, StreamKey key,
                              unsigned source_lod, unsigned relief_version,
                              TerrainTileCache& cache)
    -> std::shared_ptr<const StreamTile> {
  return build_stream_tile_impl(planet, key, source_lod, relief_version, cache,
                                nullptr);
}

// Complete owner selection; the old descriptor-only study path cannot select
// local-detail terrain. The sampler/cache live together for this tile build.
inline auto build_stream_tile(const PhysicalLocalSystem& owner, BodyId body,
                              StreamKey key, std::uint32_t terrain_policy,
                              TerrainTileCache& cache)
    -> std::shared_ptr<const StreamTile> {
  if (!saved_contact_recipe(terrain_policy))
    throw std::invalid_argument("unsupported selected stream terrain policy");
  const auto sampler = TerrainLocalDetailSampler::create(
      owner, body, {require(terrain_surface_raw_recipe(terrain_policy))},
      cache);
  if (!sampler)
    throw std::invalid_argument("invalid selected stream owner/body");
  return build_stream_tile_impl(sampler->planet(), key, 8, 0, cache,
                                terrain_policy >= 2 ? &*sampler : nullptr,
                                terrain_policy);
}

using TileMap = std::map<StreamKey, std::shared_ptr<const StreamTile>>;

struct StreamMorphTile {
  StreamKey coarse_key;
  // Fine weight increases from zero when refining, decreases when coarsening.
  bool refining{};
  // Actual coarse triangle endpoints, relative to the finer tile's anchor.
  std::vector<StreamPoint> coarse_vertices, coarse_normals;
  std::vector<std::array<double, 3>> coarse_colors; // Normalized RGB.
  std::vector<double> coarse_elevations, coarse_wetness;
  auto bytes() const -> std::size_t {
    return coarse_vertices.size() * sizeof(StreamPoint) +
           coarse_normals.size() * sizeof(StreamPoint) +
           coarse_colors.size() * sizeof(std::array<double, 3>) +
           (coarse_elevations.size() + coarse_wetness.size()) * sizeof(double);
  }
};
struct StreamTransition {
  // Common refinement: only keys already present in one of the two covers.
  TileMap draw_tiles;
  std::map<StreamKey, StreamMorphTile> morphs;
  // Endpoint payload only; draw_tiles share immutable existing base geometry.
  auto bytes() const -> std::size_t {
    std::size_t sum{};
    for (const auto& [_, morph] : morphs)
      sum += morph.bytes();
    return sum;
  }
};

namespace stream_morph_detail {
inline auto valid_key(StreamKey key) -> bool {
  return key.face < 6 && key.lod <= kStreamMaxLod && key.x < (1U << key.lod) &&
         key.y < (1U << key.lod);
}
inline auto finite(StreamPoint p) -> bool {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
inline auto validate_cover(const TileMap& tiles) -> void {
  if (tiles.size() < 6 || tiles.size() > kStreamMaxTiles)
    throw std::invalid_argument("invalid morph cover size");
  constexpr unsigned n = kStreamIntervals + 1;
  constexpr unsigned vertices = n * n + 4 * n;
  constexpr unsigned top_indices = 6 * kStreamIntervals * kStreamIntervals;
  constexpr unsigned indices = top_indices + 4 * kStreamIntervals * 12;
  std::array<std::uint64_t, 6> area{};
  for (const auto& [key, tile] : tiles) {
    if (!valid_key(key) || !tile || tile->key != key || !finite(tile->anchor) ||
        tile->vertices.size() != vertices || tile->normals.size() != vertices ||
        tile->colors.size() != vertices ||
        tile->elevations.size() != vertices ||
        tile->indices.size() != indices ||
        (!tile->wetness.empty() && tile->wetness.size() != vertices))
      throw std::invalid_argument("invalid morph tile shape or key");
    auto ancestor = key;
    while (ancestor.lod) {
      --ancestor.lod;
      ancestor.x /= 2;
      ancestor.y /= 2;
      if (tiles.contains(ancestor))
        throw std::invalid_argument("overlapping morph cover");
    }
    area[key.face] += std::uint64_t{1} << (2 * (kStreamMaxLod - key.lod));
    for (unsigned i = 0; i < vertices; ++i) {
      const auto magnitude = length(tile->normals[i]);
      if (!finite(tile->vertices[i]) || !finite(tile->normals[i]) ||
          !std::isfinite(magnitude) || std::abs(magnitude - 1) > 1e-6 ||
          !std::isfinite(tile->elevations[i]) ||
          (!tile->wetness.empty() &&
           (tile->wetness[i] != 0.0 && tile->wetness[i] != 1.0)))
        throw std::invalid_argument("invalid morph vertex attributes");
    }
    for (const auto index : tile->indices)
      if (index < 0 || static_cast<unsigned>(index) >= vertices)
        throw std::invalid_argument("invalid morph index buffer");
    // Correspondence relies on this actual dyadic top topology, not arbitrary
    // triangles or a second reconstruction of the terrain recipe.
    for (unsigned row = 0; row < kStreamIntervals; ++row)
      for (unsigned col = 0; col < kStreamIntervals; ++col) {
        const auto a = row * n + col;
        const auto offset = (row * kStreamIntervals + col) * 6;
        const auto triangle_matches = [&](unsigned at, unsigned first,
                                          unsigned second, unsigned third) {
          return tile->indices[at] == static_cast<std::int32_t>(first) &&
                 ((tile->indices[at + 1] == static_cast<std::int32_t>(second) &&
                   tile->indices[at + 2] == static_cast<std::int32_t>(third)) ||
                  (tile->indices[at + 1] == static_cast<std::int32_t>(third) &&
                   tile->indices[at + 2] == static_cast<std::int32_t>(second)));
        };
        if (!triangle_matches(offset, a, a + 1, a + n) ||
            !triangle_matches(offset + 3, a + 1, a + n + 1, a + n))
          throw std::invalid_argument("unsupported morph top topology");
      }
  }
  for (const auto face_area : area)
    if (face_area != (std::uint64_t{1} << (2 * kStreamMaxLod)))
      throw std::invalid_argument("incomplete morph cover");
}
inline auto ancestor_in(const TileMap& cover, StreamKey key)
    -> TileMap::const_iterator {
  for (;;) {
    if (const auto found = cover.find(key); found != cover.end()) return found;
    if (!key.lod) return cover.end();
    --key.lod;
    key.x /= 2;
    key.y /= 2;
  }
}
inline auto endpoints(const StreamTile& fine, const StreamTile& coarse,
                      bool refining) -> StreamMorphTile {
  if (!valid_key(fine.key) || !valid_key(coarse.key) ||
      fine.key.face != coarse.key.face || fine.key.lod <= coarse.key.lod)
    throw std::invalid_argument("invalid morph ancestry");
  const unsigned difference = fine.key.lod - coarse.key.lod;
  const unsigned scale = 1U << difference;
  if (fine.key.x / scale != coarse.key.x || fine.key.y / scale != coarse.key.y)
    throw std::invalid_argument("unrelated morph tiles");
  if (fine.wetness.empty() != coarse.wetness.empty())
    throw std::invalid_argument("incompatible morph surface attributes");
  StreamMorphTile result{coarse.key, refining, {}, {}, {}, {}, {}};
  const auto count = fine.vertices.size();
  result.coarse_vertices.reserve(count);
  result.coarse_normals.reserve(count);
  result.coarse_colors.reserve(count);
  result.coarse_elevations.reserve(count);
  constexpr unsigned n = kStreamIntervals + 1;
  const unsigned offset_x = (fine.key.x % scale) * kStreamIntervals;
  const unsigned offset_y = (fine.key.y % scale) * kStreamIntervals;
  const auto anchor_delta = sub(coarse.anchor, fine.anchor);
  for (unsigned row = 0; row < n; ++row)
    for (unsigned col = 0; col < n; ++col) {
      const unsigned numerator_x = offset_x + col;
      const unsigned numerator_y = offset_y + row;
      const unsigned cell_x =
          std::min(numerator_x / scale, kStreamIntervals - 1);
      const unsigned cell_y =
          std::min(numerator_y / scale, kStreamIntervals - 1);
      const double x =
          static_cast<double>(numerator_x - cell_x * scale) / scale;
      const double y =
          static_cast<double>(numerator_y - cell_y * scale) / scale;
      const unsigned a = cell_y * n + cell_x;
      const std::array<unsigned, 3> corners =
          x + y <= 1 ? std::array{a, a + 1, a + n}
                     : std::array{a + 1, a + n + 1, a + n};
      const std::array<double, 3> weights =
          x + y <= 1 ? std::array{1 - x - y, x, y}
                     : std::array{1 - y, x + y - 1, 1 - x};
      StreamPoint point{}, normal{};
      std::array<double, 3> color{};
      double elevation{}, wetness{};
      for (unsigned i = 0; i < 3; ++i) {
        const auto at = corners[i];
        point = add(point, mul(coarse.vertices[at], weights[i]));
        normal = add(normal, mul(coarse.normals[at], weights[i]));
        color[0] += coarse.colors[at].red / 255.0 * weights[i];
        color[1] += coarse.colors[at].green / 255.0 * weights[i];
        color[2] += coarse.colors[at].blue / 255.0 * weights[i];
        elevation += coarse.elevations[at] * weights[i];
        if (!coarse.wetness.empty()) wetness += coarse.wetness[at] * weights[i];
      }
      point = add(point, anchor_delta);
      if (!finite(point) || !finite(normal) || length(normal) <= 1e-12 ||
          !std::isfinite(elevation))
        throw std::invalid_argument("invalid morph endpoint");
      result.coarse_vertices.push_back(point);
      result.coarse_normals.push_back(normal);
      result.coarse_colors.push_back(color);
      result.coarse_elevations.push_back(elevation);
      if (!coarse.wetness.empty()) result.coarse_wetness.push_back(wetness);
    }
  // Preserve each fine skirt's existing inward offset below its associated top.
  // This morphs the top surface, not the old coarse skirt triangulation/depth.
  for (unsigned edge = 0; edge < 4; ++edge)
    for (unsigned i = 0; i < n; ++i) {
      const unsigned top = edge == 0   ? i
                           : edge == 1 ? i * n + n - 1
                           : edge == 2 ? (n - 1) * n + n - 1 - i
                                       : (n - 1 - i) * n;
      const unsigned bottom = n * n + edge * n + i;
      const auto point = add(result.coarse_vertices[top],
                             sub(fine.vertices[bottom], fine.vertices[top]));
      const auto elevation = result.coarse_elevations[top] +
                             fine.elevations[bottom] - fine.elevations[top];
      const auto inward = dot(sub(point, result.coarse_vertices[top]),
                              add(result.coarse_vertices[top], fine.anchor));
      if (!finite(point) || !std::isfinite(elevation) ||
          !std::isfinite(inward) || inward >= 0)
        throw std::invalid_argument("invalid morph skirt endpoint");
      result.coarse_vertices.push_back(point);
      result.coarse_normals.push_back(result.coarse_normals[top]);
      result.coarse_colors.push_back(result.coarse_colors[top]);
      result.coarse_elevations.push_back(elevation);
      if (!coarse.wetness.empty())
        result.coarse_wetness.push_back(result.coarse_wetness[top]);
    }
  return result;
}
} // namespace stream_morph_detail

// No terrain query: correspondence is derived only from the two immutable
// accepted/generated covers. Initial loading remains atomic. Complete nested
// quadtree partitions have a common refinement contained in their union.
inline auto make_stream_transition(const TileMap& old, const TileMap& next)
    -> StreamTransition {
  stream_morph_detail::validate_cover(next);
  if (old.empty()) return {next, {}};
  stream_morph_detail::validate_cover(old);
  StreamTransition result;
  for (const auto& [key, tile] : old) {
    if (const auto found = next.find(key); found != next.end()) {
      const auto& other = *found->second;
      if (tile != found->second &&
          (tile->anchor != other.anchor || tile->vertices != other.vertices ||
           tile->normals != other.normals || tile->colors != other.colors ||
           tile->elevations != other.elevations ||
           tile->wetness != other.wetness || tile->indices != other.indices))
        throw std::invalid_argument("shared morph tile changed geometry");
      result.draw_tiles.emplace(key, tile);
    } else if (const auto coarse = stream_morph_detail::ancestor_in(next, key);
               coarse != next.end()) {
      result.draw_tiles.emplace(key, tile);
      result.morphs.emplace(
          key, stream_morph_detail::endpoints(*tile, *coarse->second, false));
    }
  }
  for (const auto& [key, tile] : next) {
    if (old.contains(key)) continue;
    if (const auto coarse = stream_morph_detail::ancestor_in(old, key);
        coarse != old.end()) {
      result.draw_tiles.emplace(key, tile);
      result.morphs.emplace(
          key, stream_morph_detail::endpoints(*tile, *coarse->second, true));
    }
  }
  if (result.draw_tiles.empty() ||
      result.draw_tiles.size() > old.size() + next.size() ||
      result.draw_tiles.size() > kStreamMaxTiles * std::size_t{2})
    throw std::invalid_argument("invalid morph draw cover");
  return result;
}

struct StreamBatch {
  TileMap tiles;
  StreamTransition transition;
  unsigned generated{}, reused{};
  double milliseconds{};
  // New resident base geometry only, excluding shared transition payloads.
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
  std::optional<PhysicalLocalSystem> physical_owner;
  std::optional<BodyId> physical_body;
  std::uint32_t terrain_policy{};
  TileMap resident;
  std::vector<StreamKey> requested;
  std::future<StreamBatch> future;
  std::stop_source stop;

  static auto selected_planet(const PhysicalLocalSystem& owner, BodyId body,
                              std::uint32_t policy) -> PlanetDescriptor {
    if (!saved_contact_recipe(policy))
      throw std::invalid_argument("unsupported selected stream terrain policy");
    auto cache = require(TerrainTileCache::create(1));
    const auto sampler = TerrainLocalDetailSampler::create(
        owner, body, {require(terrain_surface_raw_recipe(policy))}, cache);
    if (!sampler)
      throw std::invalid_argument("invalid selected stream owner/body");
    return sampler->planet();
  }

 public:
  PlanetStream(const PlanetDescriptor& p, unsigned lod, unsigned relief)
      : planet(p), source_lod(lod), relief_version(relief) {
    if (lod > 10 || relief > kExperimentalReliefVersion)
      throw std::invalid_argument("invalid stream recipe");
  }
  PlanetStream(const PhysicalLocalSystem& owner, BodyId body,
               std::uint32_t selected_policy)
      : PlanetStream(selected_planet(owner, body, selected_policy), 8, 0) {
    physical_owner.emplace(owner);
    physical_body = body;
    terrain_policy = selected_policy;
  }
  [[nodiscard]] auto selected_owner() const noexcept
      -> const std::optional<PhysicalLocalSystem>& {
    return physical_owner;
  }
  [[nodiscard]] auto selected_body() const noexcept -> std::optional<BodyId> {
    return physical_body;
  }
  [[nodiscard]] auto selected_terrain_policy() const noexcept -> std::uint32_t {
    return terrain_policy;
  }
  ~PlanetStream() {
    stop.request_stop();
    if (future.valid()) future.wait();
  }
  PlanetStream(const PlanetStream&) = delete;
  auto operator=(const PlanetStream&) -> PlanetStream& = delete;
  auto busy() const -> bool { return future.valid(); }
  auto request(StreamPoint observer) -> bool {
    // Validate even while busy. Historical study/policy1 planning remains
    // exact.
    const auto plan = terrain_policy >= 2
                          ? plan_selected_tiles(*physical_owner, *physical_body,
                                                terrain_policy, observer)
                          : plan_tiles(planet, observer);
    if (busy() || plan == requested) return false;
    requested = plan;
    const auto token = stop.get_token();
    future = std::async(
        std::launch::async,
        [p = planet, lod = source_lod, relief = relief_version, plan,
         old = resident, token, owner = physical_owner, body = physical_body,
         policy = terrain_policy]() {
          const auto begin = std::chrono::steady_clock::now();
          StreamBatch batch;
          auto cache = require(TerrainTileCache::create(64));
          // Created inside this job: its raw cache reference never crosses a
          // move or outlives the job-local cache. Completed tiles own only mesh
          // values.
          std::optional<TerrainLocalDetailSampler> sampler;
          if (owner && policy >= 2)
            sampler.emplace(require(TerrainLocalDetailSampler::create(
                *owner, *body, {require(terrain_surface_raw_recipe(policy))},
                cache)));
          for (const auto& key : plan) {
            if (token.stop_requested()) break;
            if (auto it = old.find(key); it != old.end()) {
              batch.tiles.emplace(key, it->second);
              ++batch.reused;
            } else {
              batch.tiles.emplace(
                  key, build_stream_tile_impl(p, key, lod, relief, cache,
                                              sampler ? &*sampler : nullptr,
                                              policy));
              ++batch.generated;
            }
          }
          if (!token.stop_requested())
            batch.transition = make_stream_transition(old, batch.tiles);
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
