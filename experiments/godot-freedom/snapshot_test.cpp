#include "snapshot.hpp"

#include <iostream>

using namespace apsis_drift;
using namespace apsis_drift::godot_spike;

auto check(bool condition, const char* message) -> void {
  if (!condition) throw std::runtime_error(message);
}

auto rejected(Request request) -> void {
  try {
    validate(request);
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error("invalid request accepted");
}

auto main() -> int {
  try {
    for (unsigned size : {0U, 1U, 514U, std::numeric_limits<unsigned>::max()}) {
      Request r;
      r.samples = size;
      rejected(r);
    }
    for (double invalid :
         {0.0, -1.0, 262145.0, std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::quiet_NaN()}) {
      Request r;
      r.span_metres = invalid;
      rejected(r);
    }
    Request invalid;
    invalid.latitude = std::numeric_limits<double>::quiet_NaN();
    rejected(invalid);
    invalid = {};
    invalid.latitude = 2.0;
    rejected(invalid);
    invalid = {};
    invalid.longitude = 4.0;
    rejected(invalid);
    invalid = {};
    invalid.longitude = std::numeric_limits<double>::infinity();
    rejected(invalid);
    invalid = {};
    invalid.lod = 17;
    rejected(invalid);
    invalid = {};
    invalid.lod = 11;
    rejected(invalid);
    invalid = {};
    invalid.relief_version = 2;
    rejected(invalid);
    Request boundary;
    boundary.samples = 513;
    boundary.span_metres = 262144;
    boundary.lod = 10;
    validate(boundary);

    Request request;
    request.samples = 3;
    request.span_metres = 1000;
    const auto data = snapshot(request);
    check(data == snapshot(request), "snapshot replay is not deterministic");
    check(data.at("vertices").size() == 9 && data.at("colors").size() == 9,
          "grid buffer bounds incorrect");
    const auto center = data.at("vertices").at(4);
    for (const auto& coordinate : center)
      check(std::abs(coordinate.get<double>()) < 1e-6, "local origin mismatch");
    for (const auto& vertex : data.at("vertices"))
      for (const auto& coordinate : vertex)
        check(std::isfinite(coordinate.get<double>()),
              "non-finite mesh vertex");
    check(data.at("vertices").at(0).at(0) < 0 &&
              data.at("vertices").at(0).at(2) > 0,
          "ENU conversion reflected");
    const auto planet = generate_planet_descriptor(request.planet_seed);
    auto cache = require(TerrainTileCache::create());
    const auto origin =
        GeodeticPosition{request.latitude, request.longitude, 0};
    const auto sample = require(sample_planet_surface(
        planet, require(planet_fixed_from_geodetic(planet, origin)),
        request.lod, cache));
    check(data.at("frame").at("altitude_metres") == sample.elevation_metres,
          "snapshot differs from authoritative terrain sample");
    check(data.at("planet").at("planet_seed").is_string(),
          "64-bit identity not string");
    check(data.at("replay").size() == 301 &&
              data.at("replay").back().at("tick") == 1200 &&
              data.at("replay").back().at("checksum").is_string(),
          "replay contract broken");
    request.samples = 2;
    check(snapshot(request).at("vertices").size() == 4,
          "minimum grid rejected");
    request.planet_seed.value = std::numeric_limits<std::uint64_t>::max();
    check(snapshot(request).at("planet").at("planet_seed") ==
              "18446744073709551615",
          "64-bit seed lost precision");
    for (double invalid_coordinate :
         {std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::quiet_NaN(), 1.0e10}) {
      bool rejected_coordinate = false;
      try {
        (void)relief_offset(Seed{42}, {invalid_coordinate, 0, 0}, 1);
      } catch (const std::invalid_argument&) {
        rejected_coordinate = true;
      }
      check(rejected_coordinate, "invalid relief coordinate accepted");
    }
    bool rejected_version = false;
    try {
      (void)relief_offset(Seed{42}, {0, 0, 0}, 2);
    } catch (const std::invalid_argument&) {
      rejected_version = true;
    }
    check(rejected_version, "unknown relief recipe accepted");
    const std::array point{4100000.0, -5300000.0, 600000.0};
    const auto offset = relief_offset(Seed{42}, point, 1);
    check(offset >= -750 && offset <= 1725 &&
              offset == relief_offset(Seed{42}, point, 1),
          "relief bound or repeatability broken");
    check(offset != relief_offset(Seed{43}, point, 1), "relief ignored seed");
    check(relief_offset(Seed{42}, point, 0) == 0, "baseline terrain changed");
    auto adjacent = point;
    adjacent[0] += 0.001;
    check(std::abs(relief_offset(Seed{42}, adjacent, 1) - offset) <= 0.02,
          "relief discontinuity at adjacent sample");
    request = {};
    request.samples = 3;
    request.span_metres = 1000;
    request.relief_version = 1;
    const auto detailed = snapshot(request);
    check(detailed == snapshot(request), "relief snapshot is not repeatable");
    check(detailed.at("experimental_relief_version") == 1,
          "relief identity missing");
    const auto detailed_sample = with_relief(
        sample, planet, require(planet_fixed_from_geodetic(planet, origin)), 1);
    check(detailed.at("frame").at("altitude_metres") ==
              detailed_sample.elevation_metres,
          "relief mesh and C++ sampler differ");
    for (const auto& coordinate : detailed.at("vertices").at(4))
      check(std::abs(coordinate.get<double>()) < 1e-6,
            "relief local origin mismatch");
    request.relief_version = 0;
    check(snapshot(request) == data,
          "opting out of relief changed baseline snapshot");
    check(data.at("schema_version") == 1 && !data.contains("world_context"),
          "standalone snapshot silently gained physical ownership");
    for (const auto universe :
         {Seed{0}, Seed{42}, Seed{std::numeric_limits<std::uint64_t>::max()}}) {
      Request physical_request;
      physical_request.samples = 3;
      physical_request.span_metres = 1000;
      physical_request.physical_origin_seed = universe;
      const auto world = resolve_snapshot_world(physical_request);
      const auto expected_system =
          require(generate_physical_origin_system(universe));
      const auto& home =
          expected_system.catalog.planets[kOriginHomePlanetOrdinal].descriptor;
      check(world.planet == home && world.physical == expected_system,
            "physical snapshot did not resolve exact authored origin home");
      const auto physical = snapshot(physical_request);
      check(physical == snapshot(physical_request) &&
                physical.at("schema_version") == 2,
            "physical snapshot not deterministic or separately versioned");
      check(physical.at("world_context") == snapshot_world_context(world) &&
                physical.at("planet") ==
                    nlohmann::json::parse(planet_descriptor_json(home)),
            "physical context or descriptor projection differs from C++ owner");
      check(physical.at("world_context").at("origin_universe_seed") ==
                    std::to_string(universe.value) &&
                physical.at("world_context").at("family") ==
                    "physical_circular" &&
                physical.at("world_context")
                    .at("rotation")
                    .at("period_ticks")
                    .is_string(),
            "physical world provenance lost family or 64-bit precision");
      auto physical_cache = require(TerrainTileCache::create());
      const auto location = require(planet_fixed_from_geodetic(
          home, {physical_request.latitude, physical_request.longitude, 0}));
      const auto surface = require(sample_planet_surface(
          home, location, physical_request.lod, physical_cache));
      check(physical.at("frame").at("altitude_metres") ==
                surface.elevation_metres,
            "physical snapshot does not use authored-home terrain");
      for (const auto& coordinate : physical.at("vertices").at(4))
        check(std::abs(coordinate.get<double>()) < 1e-6,
              "physical local origin changed ENU convention");
      check(physical.at("replay").size() == 301 &&
                physical.at("replay").front().at("tick") == 0 &&
                physical.at("replay").back().at("tick") == 1200,
            "physical snapshot broke shared replay shape");
      physical_request.latitude = -.7;
      physical_request.longitude = -2.4;
      const auto diagnostic = snapshot(physical_request);
      check(diagnostic.at("frame").at("latitude_radians") == -.7 &&
                diagnostic.at("frame").at("longitude_radians") == -2.4 &&
                diagnostic.at("world_context") == physical.at("world_context"),
            "diagnostic viewpoint changed physical world owner");
      if (universe == Seed{42}) {
        check(
            world.planet != generate_planet_descriptor(home.seed),
            "authored-home alias fixture unexpectedly identical to standalone");
        auto forged = resolve_snapshot_world(physical_request);
        forged.rotation->owner_version = 0;
        bool refused = false;
        try {
          (void)snapshot_world_context(forged);
        } catch (const std::invalid_argument&) {
          refused = true;
        }
        check(refused, "forged physical wrapper projected as canonical owner");
      }
    }
    std::cout << "Godot snapshot boundary, identity, terrain and replay "
                 "contracts passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
