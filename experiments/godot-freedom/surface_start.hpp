#pragma once

#include "snapshot.hpp"
#include "thrust_flight.hpp"

namespace apsis_drift::godot_spike {

// Authored native practice fixture, not terrain generation or collision logic.
// The square survey has 2 km half-extent (corners are about 2.828 km away).
// Its finite sample set does NOT prove clearance between probes or during
// flight.
inline constexpr unsigned kSurfaceStartVersion{1};
inline constexpr unsigned kSurfaceStartIntervals{16};
inline constexpr unsigned kSurfaceStartSamples{(kSurfaceStartIntervals + 1) *
                                               (kSurfaceStartIntervals + 1)};
inline constexpr double kSurfaceStartHalfExtentMetres{2000.0};
inline constexpr double kSurfaceStartSpacingMetres{250.0};
inline constexpr double kSurfaceStartMarginMetres{300.0};
inline constexpr double kSurfaceStartHeadingRadians{0.3};

struct SurfacePracticeStart {
  GeodeticPosition reference; // Original snapshot anchor, altitude zero.
  GeodeticPosition pose;
  double center_elevation_metres{};
  double maximum_sampled_elevation_metres{};
  flight_lab::State flight;

  friend auto operator==(const SurfacePracticeStart&,
                         const SurfacePracticeStart&) -> bool = default;
};

// All inputs validate before terrain access. Returns a complete candidate; the
// caller commits it only on success. Tick zero is mandatory: this is never an
// automatic repair/teleport for an ongoing flight. No random streams are drawn.
inline auto survey_surface_start(const PlanetDescriptor& planet,
                                 const Request& reference,
                                 TerrainTileCache& cache,
                                 SimulationTick tick = 0)
    -> SurfacePracticeStart {
  validate(reference);
  if (tick != 0 || planet.seed != reference.planet_seed)
    throw std::invalid_argument(
        "surface practice requires tick zero and original seed");
  auto sampler =
      require(TerrainSurfaceSampler::create(planet, reference.lod, cache));
  const GeodeticPosition anchor{reference.latitude, reference.longitude, 0.0};
  const auto frame = require(make_local_tangent_frame(planet, anchor));
  SurfacePracticeStart result;
  result.reference = anchor;
  result.maximum_sampled_elevation_metres =
      -std::numeric_limits<double>::infinity();
  for (unsigned row = 0; row <= kSurfaceStartIntervals; ++row) {
    for (unsigned column = 0; column <= kSurfaceStartIntervals; ++column) {
      const auto east =
          static_cast<double>(column) * kSurfaceStartSpacingMetres -
          kSurfaceStartHalfExtentMetres;
      const auto north = static_cast<double>(row) * kSurfaceStartSpacingMetres -
                         kSurfaceStartHalfExtentMetres;
      const auto fixed =
          require(planet_fixed_from_local(frame, {east, north, 0.0}));
      const auto sample = with_relief(require(sampler.sample(fixed)), planet,
                                      fixed, reference.relief_version);
      if (!std::isfinite(sample.elevation_metres) ||
          std::abs(sample.elevation_metres) > 1.0e7)
        throw std::invalid_argument(
            "surface practice sample outside finite bounds");
      result.maximum_sampled_elevation_metres = std::max(
          result.maximum_sampled_elevation_metres, sample.elevation_metres);
      if (row == kSurfaceStartIntervals / 2 &&
          column == kSurfaceStartIntervals / 2)
        result.center_elevation_metres = sample.elevation_metres;
    }
  }
  result.pose = anchor;
  result.pose.altitude_metres =
      result.maximum_sampled_elevation_metres + kSurfaceStartMarginMetres;
  result.flight =
      flight_lab::initial(planet, result.pose, kSurfaceStartHeadingRadians);
  result.flight.clearance =
      result.pose.altitude_metres - result.center_elevation_metres;
  result.flight.density =
      flight_lab::density(planet, result.pose.altitude_metres);
  flight_lab::validate(result.flight);
  return result;
}

} // namespace apsis_drift::godot_spike
