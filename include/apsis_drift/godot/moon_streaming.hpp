#pragma once

#include "apsis_drift/moon_terrain.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <stdexcept>
#include <vector>

namespace apsis_drift::godot_spike {
inline constexpr unsigned kMoonStreamIntervals{32};
inline constexpr unsigned kMoonStreamMaxTiles{384};
inline constexpr unsigned kMoonStreamMaxLod{13};
struct MoonStreamKey {
  unsigned face{}, lod{}, x{}, y{};
  auto operator<=>(const MoonStreamKey&) const = default;
};
struct MoonMeshTile {
  std::uint32_t terrain_version{kMoonTerrainRecipeVersion};
  BodyId body;
  MoonStreamKey key;
  MoonFixedPositionMetres anchor;
  // Fixed-frame directions/displacements. Mesh points are relative to anchor;
  // no camera/world rewrite or substitute physical descriptor is retained.
  std::vector<RigidVector3> vertices, normals;
  std::vector<Rgb8> colors;
  std::vector<double> elevations;
  std::vector<std::int32_t> indices;
  auto bytes() const -> std::size_t {
    return vertices.size() * sizeof(RigidVector3) +
           normals.size() * sizeof(RigidVector3) +
           colors.size() * sizeof(Rgb8) + elevations.size() * sizeof(double) +
           indices.size() * sizeof(std::int32_t);
  }
};
namespace moon_stream_detail {
using V = RigidVector3;
inline auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline auto mul(V a, double s) -> V {
  return {a.x * s, a.y * s, a.z * s};
}
inline auto dot(V a, V b) -> double {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline auto length(V a) -> double {
  return std::hypot(a.x, a.y, a.z);
}
inline auto unit(V a) -> V {
  const auto n = length(a);
  if (!std::isfinite(n) || n <= 0)
    throw std::invalid_argument("Invalid moon mesh direction");
  return mul(a, 1 / n);
}
inline auto valid_key(MoonStreamKey key) -> bool {
  return key.face < 6 && key.lod <= kMoonStreamMaxLod &&
         key.x < (1U << key.lod) && key.y < (1U << key.lod);
}
template <class T, class E>
inline auto require(std::expected<T, E> value) -> T {
  if (!value) throw std::invalid_argument("Moon mesh source refused");
  return std::move(*value);
}
inline auto direction(MoonStreamKey key, double u, double v)
    -> SystemDirection {
  if (!valid_key(key) || !std::isfinite(u) || !std::isfinite(v) || u < 0 ||
      u > 1 || v < 0 || v > 1)
    throw std::invalid_argument("Invalid moon mesh address");
  return require(direction_from_cube_surface_address(
      {static_cast<CubeFace>(key.face), static_cast<std::uint8_t>(key.lod),
       key.x, key.y, u, v}));
}
inline auto point(const MoonTerrainSampler& sampler, MoonStreamKey key,
                  double u, double v) -> V {
  const auto d = direction(key, u, v);
  const auto p = require(sampler.surface_position({d.x, d.y, d.z}));
  return {p.x, p.y, p.z};
}
} // namespace moon_stream_detail

// Complete six-face partition; subdivisions replace one leaf by all4 children.
// Same bounded best-first score/buffer as the existing stream planner. On small
// moons that buffer can consume the budget before contact LOD: prioritize the
// observer's branch to13 within4km actual clearance, then spend the remainder
// best-first. Budgets below45 may retain a coarser but complete cover.
inline auto plan_moon_tiles(const PhysicalMoonDescriptor& owner,
                            MoonFixedPositionMetres observer,
                            unsigned budget = kMoonStreamMaxTiles)
    -> std::vector<MoonStreamKey> {
  using namespace moon_stream_detail;
  const V actual{observer.x, observer.y, observer.z};
  const double radial = length(actual);
  if (budget < 6 || budget > kMoonStreamMaxTiles || !std::isfinite(radial) ||
      radial < 1 || radial > 1e10)
    throw std::invalid_argument("Invalid moon stream observer or budget");
  const auto sampler = require(MoonTerrainSampler::create(owner));
  const auto sample =
      require(sampler.sample({observer.x, observer.y, observer.z}));
  const double planner_radius = radial - sample.elevation_metres;
  if (!std::isfinite(planner_radius) || planner_radius < 1 ||
      planner_radius > 1e10)
    throw std::invalid_argument("Unsafe moon stream clearance radius");
  const V planner_observer = mul(actual, planner_radius / radial);
  const auto target = require(cube_surface_address_from_direction(
      {actual.x, actual.y, actual.z}, kMoonStreamMaxLod));
  const bool near =
      radial - owner.radius_metres - sample.elevation_metres <= 4000;
  struct Candidate {
    double score;
    MoonStreamKey key;
  };
  const auto lower = [](const Candidate& a, const Candidate& b) {
    return a.score == b.score ? a.key > b.key : a.score < b.score;
  };
  std::priority_queue<Candidate, std::vector<Candidate>, decltype(lower)> queue(
      lower);
  std::map<MoonStreamKey, bool> leaves;
  const auto insert = [&](MoonStreamKey key) {
    const auto d = direction(key, .5, .5);
    const V center{d.x * owner.radius_metres, d.y * owner.radius_metres,
                   d.z * owner.radius_metres};
    const double width =
        2 * owner.radius_metres / static_cast<double>(1U << key.lod);
    const double distance = std::max(
        80., length(sub(planner_observer, center)) - width * .85 - 4000.);
    double score = width / distance;
    const unsigned shift = kMoonStreamMaxLod - key.lod;
    if (near && key.face == static_cast<unsigned>(target.face) &&
        key.x == (target.x >> shift) && key.y == (target.y >> shift))
      score = std::numeric_limits<double>::infinity();
    leaves.emplace(key, true);
    if (key.lod < kMoonStreamMaxLod) queue.push({score, key});
  };
  for (unsigned face = 0; face < 6; ++face)
    insert({face, 0, 0, 0});
  while (!queue.empty() && leaves.size() + 3 <= budget) {
    const auto best = queue.top();
    queue.pop();
    if (best.score < 1.6) break;
    leaves.erase(best.key);
    for (unsigned y = 0; y < 2; ++y)
      for (unsigned x = 0; x < 2; ++x)
        insert({best.key.face, best.key.lod + 1, best.key.x * 2 + x,
                best.key.y * 2 + y});
  }
  std::vector<MoonStreamKey> result;
  result.reserve(leaves.size());
  for (const auto& [key, present] : leaves) {
    (void)present;
    result.push_back(key);
  }
  return result;
}

// One fixed-size pure mesh. Top vertices are exactly the same33x33 sample
// addresses/diagonal as MoonContact atLOD13. Other LODs are visual only.
// Double-sided inward skirts conceal unequal tessellation cracks without
// replacing the actual top surface used for contact/walking/impact.
inline auto make_moon_stream_tile(const MoonTerrainSampler& sampler,
                                  MoonStreamKey key)
    -> std::shared_ptr<const MoonMeshTile> {
  using namespace moon_stream_detail;
  if (!valid_key(key) || !validate_physical_moon_descriptor(sampler.owner()))
    throw std::invalid_argument("Invalid moon mesh key or owner");
  auto tile = std::make_shared<MoonMeshTile>();
  tile->body = sampler.owner().body;
  tile->key = key;
  const auto center = point(sampler, key, .5, .5);
  tile->anchor = {center.x, center.y, center.z};
  constexpr unsigned n = kMoonStreamIntervals + 1;
  constexpr unsigned count = n * n + 4 * n;
  tile->vertices.reserve(count);
  tile->normals.reserve(count);
  tile->colors.reserve(count);
  tile->elevations.reserve(count);
  tile->indices.reserve(6 * kMoonStreamIntervals * kMoonStreamIntervals +
                        48 * kMoonStreamIntervals);
  std::vector<V> fixed;
  fixed.reserve(n * n);
  for (unsigned row = 0; row < n; ++row)
    for (unsigned col = 0; col < n; ++col) {
      const double u = static_cast<double>(col) / kMoonStreamIntervals,
                   v = static_cast<double>(row) / kMoonStreamIntervals;
      const auto d = direction(key, u, v);
      const auto sample = require(sampler.sample({d.x, d.y, d.z}));
      const auto p = require(sampler.surface_position({d.x, d.y, d.z}));
      const V position{p.x, p.y, p.z};
      fixed.push_back(position);
      tile->vertices.push_back(sub(position, center));
      tile->normals.push_back({});
      tile->colors.push_back(sample.color);
      tile->elevations.push_back(sample.elevation_metres);
    }
  const auto triangle = [&](unsigned a, unsigned b, unsigned c,
                            bool accumulate) {
    auto normal = cross(sub(tile->vertices[b], tile->vertices[a]),
                        sub(tile->vertices[c], tile->vertices[a]));
    if (dot(normal, add(tile->vertices[a], center)) < 0) {
      std::swap(b, c);
      normal = mul(normal, -1);
    }
    // Godot front faces clockwise; stored normals explicitly outward.
    tile->indices.insert(
        tile->indices.end(),
        {static_cast<int>(a), static_cast<int>(c), static_cast<int>(b)});
    if (!accumulate)
      tile->indices.insert(
          tile->indices.end(),
          {static_cast<int>(a), static_cast<int>(b), static_cast<int>(c)});
    if (accumulate)
      for (unsigned i : {a, b, c})
        tile->normals[i] = add(tile->normals[i], normal);
  };
  for (unsigned row = 0; row < kMoonStreamIntervals; ++row)
    for (unsigned col = 0; col < kMoonStreamIntervals; ++col) {
      const auto a = row * n + col;
      triangle(a, a + 1, a + n, true);
      triangle(a + 1, a + n + 1, a + n, true);
    }
  for (auto& normal : tile->normals)
    normal = unit(normal);
  if (key.lod >= 5) {
    const auto surface = [&](V v) {
      const auto p = require(sampler.surface_position({v.x, v.y, v.z}));
      return V{p.x, p.y, p.z};
    };
    const double radius = sampler.owner().radius_metres;
    for (std::size_t i = 0; i < fixed.size(); ++i) {
      const auto radial = unit(fixed[i]), reference = mul(radial, radius);
      const auto east = unit(
          cross(std::abs(radial.z) < .9 ? V{0, 0, 1} : V{0, 1, 0}, radial));
      const auto north = cross(radial, east);
      constexpr double stencil_metres = 32;
      const auto dx = sub(surface(add(reference, mul(east, stencil_metres))),
                          surface(sub(reference, mul(east, stencil_metres))));
      const auto dy = sub(surface(add(reference, mul(north, stencil_metres))),
                          surface(sub(reference, mul(north, stencil_metres))));
      tile->normals[i] = unit(cross(dx, dy));
    }
  }
  const double width =
      2 * sampler.owner().radius_metres / static_cast<double>(1U << key.lod);
  // Visual only: either side of a coarse/fine edge can be the higher side.
  // Two complete relief envelopes plus a conservativeLOD0/32 spherical chord
  // sag bound keep even the fine skirt below the coarsest neighbouring edge.
  // Radius/1024 exceedsR*(2/32)^2/8;1m covers arithmetic slack. These vertices
  // never enter the actual top-facet contact/walking/impact adapters.
  const double seam_floor = 2 * kMoonTerrainMaximumReliefMetres +
                            sampler.owner().radius_metres / 1024. + 1.;
  const double depth =
      std::max(std::clamp(width / 8., 200., 20000.), seam_floor);
  for (unsigned edge = 0; edge < 4; ++edge) {
    unsigned previous_top = 0, previous_bottom = 0;
    for (unsigned i = 0; i < n; ++i) {
      const unsigned top = edge == 0   ? i
                           : edge == 1 ? i * n + n - 1
                           : edge == 2 ? (n - 1) * n + n - 1 - i
                                       : (n - 1 - i) * n;
      const auto bottom = static_cast<unsigned>(tile->vertices.size());
      tile->vertices.push_back(
          sub(sub(fixed[top], mul(unit(fixed[top]), depth)), center));
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
} // namespace apsis_drift::godot_spike
