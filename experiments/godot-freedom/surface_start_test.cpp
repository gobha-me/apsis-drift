#include "surface_start.hpp"

#include <iostream>

namespace {
using namespace apsis_drift;
using namespace apsis_drift::godot_spike;

int failures{};
auto check(bool condition, std::string_view message) -> void {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}

template <typename Call>
auto refuses(Call call, std::string_view message) -> void {
  try {
    call();
    check(false, message);
  } catch (const std::exception&) {
    // All failure paths are expected to be transactional for authoritative
    // state.
  }
}

auto invalid_input_contract() -> void {
  const auto planet = generate_planet_descriptor(Seed{42});
  auto cache = require(TerrainTileCache::create(1));
  Request request;
  for (const auto nonfinite : {std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity(),
                               -std::numeric_limits<double>::infinity()}) {
    auto invalid = request;
    invalid.latitude = nonfinite;
    refuses([&] { survey_surface_start(planet, invalid, cache); },
            "nonfinite latitude accepted");
    invalid = request;
    invalid.longitude = nonfinite;
    refuses([&] { survey_surface_start(planet, invalid, cache); },
            "nonfinite longitude accepted");
    invalid = request;
    invalid.span_metres = nonfinite;
    refuses([&] { survey_surface_start(planet, invalid, cache); },
            "nonfinite snapshot span accepted");
  }
  for (const auto latitude : {-2.0, 2.0}) {
    auto invalid = request;
    invalid.latitude = latitude;
    refuses([&] { survey_surface_start(planet, invalid, cache); },
            "invalid latitude accepted");
  }
  for (const auto longitude : {-4.0, 4.0}) {
    auto invalid = request;
    invalid.longitude = longitude;
    refuses([&] { survey_surface_start(planet, invalid, cache); },
            "invalid longitude accepted");
  }
  auto invalid = request;
  invalid.planet_seed = Seed{43};
  refuses([&] { survey_surface_start(planet, invalid, cache); },
          "wrong seed accepted");
  invalid = request;
  invalid.lod = 11;
  refuses([&] { survey_surface_start(planet, invalid, cache); },
          "unsupported source LOD accepted");
  invalid = request;
  invalid.relief_version = kExperimentalReliefVersion + 1;
  refuses([&] { survey_surface_start(planet, invalid, cache); },
          "unknown relief recipe accepted");
  for (const auto tick :
       {SimulationTick{1}, std::numeric_limits<SimulationTick>::max()}) {
    refuses([&] { survey_surface_start(planet, request, cache, tick); },
            "ongoing flight tick accepted");
  }
  for (const auto samples :
       {0U, 1U, 514U, std::numeric_limits<unsigned>::max()}) {
    invalid = request;
    invalid.samples = samples;
    refuses([&] { survey_surface_start(planet, invalid, cache); },
            "malformed snapshot dimensions accepted");
  }
  check(cache.size() == 0,
        "invalid input refuses before terrain generation or cache access");
}

auto survey_contract(unsigned relief) -> void {
  Request request;
  request.relief_version = relief;
  const auto planet = generate_planet_descriptor(request.planet_seed);
  auto cache = require(TerrainTileCache::create());
  const auto start = survey_surface_start(planet, request, cache);
  const auto again = survey_surface_start(planet, request, cache);
  check(start == again,
        "same reference start reproduces exact state and survey");
  check(
      start.reference ==
          GeodeticPosition{request.latitude, request.longitude, 0},
      "surface start keeps original seed/reference point, no location search");
  check(start.pose.latitude_radians == request.latitude &&
            start.pose.longitude_radians == request.longitude,
        "survey changes only starting altitude");
  check(start.flight.tick == 0 && start.flight.velocity == flight_lab::V{} &&
            start.flight.angular == flight_lab::V{} && start.flight.assist &&
            start.flight.main == 0 && start.flight.retro == 0 &&
            !start.flight.floor_guard,
        "surface practice begins stationary, level, assisted and unpowered");
  check(start.flight.clearance ==
            start.pose.altitude_metres - start.center_elevation_metres,
        "clearance measures terrain directly below, not neighborhood maximum");
  check(start.flight.density ==
            flight_lab::density(planet, start.pose.altitude_metres),
        "paused start already has correct atmospheric telemetry");
  check(start.pose.altitude_metres ==
            start.maximum_sampled_elevation_metres + 300.0,
        "start is 300m above highest surveyed elevation");
  check(kSurfaceStartSamples == 289 && kSurfaceStartHalfExtentMetres == 2000 &&
            kSurfaceStartSpacingMetres == 250 && kSurfaceStartIntervals == 16,
        "fixed work and neighborhood dimensions remain explicit");

  // Verify every probe independently, in reversed order and with a tiny cache.
  auto evicting = require(TerrainTileCache::create(1));
  const auto frame = require(make_local_tangent_frame(planet, start.reference));
  double maximum = -std::numeric_limits<double>::infinity();
  unsigned visited{};
  for (int row = 16; row >= 0; --row) {
    for (int column = 16; column >= 0; --column) {
      const auto fixed = require(planet_fixed_from_local(
          frame, {column * 250.0 - 2000.0, row * 250.0 - 2000.0, 0.0}));
      const auto sample = with_relief(
          require(sample_planet_surface(planet, fixed, request.lod, evicting)),
          planet, fixed, relief);
      maximum = std::max(maximum, sample.elevation_metres);
      check(start.pose.altitude_metres - sample.elevation_metres >=
                300.0 - 1e-9,
            "each surveyed neighbor has at least prescribed altitude margin");
      ++visited;
    }
  }
  check(visited == 289 && maximum == start.maximum_sampled_elevation_metres,
        "independent reversed probe order reproduces same highest terrain");
  check(survey_surface_start(planet, request, evicting) == start,
        "terrain residency/eviction does not alter placement");
  auto elsewhere = request;
  elsewhere.latitude = -.8;
  elsewhere.longitude = -2.0;
  static_cast<void>(survey_surface_start(planet, elsewhere, evicting));
  check(survey_surface_start(planet, request, evicting) == start,
        "return after different-region generation reproduces same start");
  std::cout << "Surface start seed 42 relief " << relief
            << ": center=" << start.center_elevation_metres
            << " max=" << maximum << " altitude=" << start.pose.altitude_metres
            << " clearance=" << start.flight.clearance << '\n';
}

auto geographic_boundaries_contract() -> void {
  Request request;
  request.relief_version = 1;
  const auto planet = generate_planet_descriptor(request.planet_seed);
  auto cache = require(TerrainTileCache::create(1));
  for (const auto latitude :
       {-std::numbers::pi / 2.0, std::numbers::pi / 2.0}) {
    for (const auto longitude : {-std::numbers::pi, std::numbers::pi}) {
      request.latitude = latitude;
      request.longitude = longitude;
      const auto start = survey_surface_start(planet, request, cache);
      check(std::isfinite(start.flight.clearance) &&
                start.flight.clearance >= 300,
            "pole/seam neighborhood remains finite and above its samples");
      check(survey_surface_start(planet, request, cache) == start,
            "pole/seam reference start is repeatable");
    }
  }
}
} // namespace

auto main() -> int {
  invalid_input_contract();
  survey_contract(0);
  survey_contract(1);
  geographic_boundaries_contract();
  std::cout << "Surface practice start: " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
