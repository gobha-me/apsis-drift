#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "apsis_drift/planetary_flight.hpp"
#include "apsis_drift/terrain_tiles.hpp"
#include "relief.hpp"

namespace apsis_drift::godot_spike {

// An experimental presentation interchange, NOT a save format or new generator.
struct Request {
  Seed planet_seed{42};
  unsigned samples{129};
  double span_metres{64'000.0};
  double latitude{0.25};
  double longitude{0.4};
  std::uint8_t lod{8};
  unsigned relief_version{0};
};

inline auto validate(const Request& request) -> void {
  if (request.samples < 2 || request.samples > 513 ||
      !std::isfinite(request.span_metres) || request.span_metres < 64.0 ||
      request.span_metres > 262'144.0 || !std::isfinite(request.latitude) ||
      std::abs(request.latitude) > std::numbers::pi / 2.0 ||
      !std::isfinite(request.longitude) ||
      std::abs(request.longitude) > std::numbers::pi ||
      request.lod > 10 || request.relief_version > kExperimentalReliefVersion) {
    throw std::invalid_argument("invalid snapshot dimensions, coordinates or LOD");
  }
}

template <typename T, typename E>
inline auto require(std::expected<T, E> result) -> T {
  if (!result) throw std::runtime_error("C++ world API rejected snapshot request");
  return std::move(*result);
}

inline auto snapshot(const Request& request) -> nlohmann::json {
  validate(request);  // Bound allocations and reject NaN before touching terrain.
  const auto planet = generate_planet_descriptor(request.planet_seed);
  auto cache = require(TerrainTileCache::create());
  auto sampler = require(TerrainSurfaceSampler::create(planet, request.lod, cache));
  auto origin = GeodeticPosition{request.latitude, request.longitude, 0.0};
  const auto origin_fixed = require(planet_fixed_from_geodetic(planet, origin));
  const auto center = with_relief(require(sampler.sample(origin_fixed)), planet,
                                 origin_fixed, request.relief_version);
  origin.altitude_metres = center.elevation_metres;
  const auto frame = require(make_local_tangent_frame(planet, origin));
  auto vertices = nlohmann::json::array();
  auto colors = nlohmann::json::array();
  double low = std::numeric_limits<double>::infinity();
  double high = -low;
  for (unsigned row = 0; row < request.samples; ++row) {
    for (unsigned column = 0; column < request.samples; ++column) {
      const double east = request.span_metres *
          (static_cast<double>(column) / (request.samples - 1) - 0.5);
      const double north = request.span_metres *
          (static_cast<double>(row) / (request.samples - 1) - 0.5);
      const auto probe = require(planet_fixed_from_local(frame, {east, north, 0}));
      const auto sample = with_relief(require(sampler.sample(probe)), planet, probe, request.relief_version);
      auto geodetic = require(geodetic_from_planet_fixed(planet, probe));
      geodetic.altitude_metres = sample.elevation_metres;
      const auto local = require(local_from_planet_fixed(
          frame, require(planet_fixed_from_geodetic(planet, geodetic))));
      // ENU -> Godot: east +X, up +Y, north -Z. Metres, no relief exaggeration.
      vertices.push_back({local.east, local.up, -local.north});
      colors.push_back({sample.color.red, sample.color.green, sample.color.blue});
      low = std::min(low, sample.elevation_metres);
      high = std::max(high, sample.elevation_metres);
    }
  }

  // A finite, recorded C++ flight: Godot interpolates these poses, never simulates.
  auto flight_origin = origin;
  flight_origin.altitude_metres += 60.0;
  auto flight = require(initial_planetary_flight_state(
      planet, flight_origin, {center.elevation_metres}, 0.3, FlightMode::manual));
  auto replay = nlohmann::json::array();
  for (unsigned tick = 0; tick <= 1200; ++tick) {
    if (tick % 4 == 0) {
      const auto local = require(local_from_planet_fixed(frame, require(
          planet_fixed_from_geodetic(planet, flight.pose.position))));
      replay.push_back({{"tick", flight.tick},
                        {"position", {local.east, local.up, -local.north}},
                        {"heading", flight.pose.heading_radians},
                        {"checksum", std::to_string(planetary_flight_state_checksum(flight))}});
    }
    if (tick == 1200) break;
    const auto probe = require(planet_fixed_from_geodetic(planet, flight.pose.position));
    const auto surface = with_relief(require(sampler.sample(probe)), planet, probe, request.relief_version);
    const FlightCommand command{flight.tick, FlightCommandKind::press_forward};
    const auto commands = tick == 0 ? std::span{&command, std::size_t{1}}
                                    : std::span<const FlightCommand>{};
    if (!advance_planetary_flight(planet, {surface.elevation_metres}, flight,
                                 commands, kSimulationStep)) {
      throw std::runtime_error("C++ flight replay failed");
    }
  }

  nlohmann::json result = {{"schema_version", 1},
          {"terrain_generator_version", kTerrainTileGeneratorVersion},
          {"seed_derivation_version", kSeedDerivationVersion},
          {"planet", nlohmann::json::parse(planet_descriptor_json(planet))},
          {"frame", {{"axes", "east,up,-north"}, {"units", "metres"},
                     {"latitude_radians", origin.latitude_radians},
                     {"longitude_radians", origin.longitude_radians},
                     {"altitude_metres", origin.altitude_metres}}},
          {"samples", request.samples}, {"span_metres", request.span_metres},
          {"lod", request.lod}, {"tiles_touched", sampler.tiles_touched()},
          {"elevation_min_metres", low}, {"elevation_max_metres", high},
          {"vertices", std::move(vertices)}, {"colors", std::move(colors)},
          {"simulation_hz", kSimulationHz}, {"replay", std::move(replay)}};
  if (request.relief_version != 0)
    result["experimental_relief_version"] = request.relief_version;
  return result;
}
}  // namespace apsis_drift::godot_spike
