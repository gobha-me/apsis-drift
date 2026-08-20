#include "apsis_drift/origin_station.hpp"

#include <format>
#include <limits>

namespace apsis_drift {
namespace {

class SplitMix64 {
 public:
  explicit SplitMix64(Seed seed) noexcept : m_state{seed.value} {}

  [[nodiscard]] auto next() noexcept -> std::uint64_t {
    auto value = (m_state += 0x9E3779B97F4A7C15ULL);
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
  }

  [[nodiscard]] auto bounded(std::uint64_t exclusive_upper) noexcept
      -> std::uint64_t {
    if (exclusive_upper == 0)
      return 0;
    const auto threshold =
        (std::numeric_limits<std::uint64_t>::max() - exclusive_upper + 1U) %
        exclusive_upper;
    for (;;) {
      const auto value = next();
      if (value >= threshold)
        return value % exclusive_upper;
    }
  }

 private:
  std::uint64_t m_state{};
};

enum class OriginHomeStream : std::uint64_t {
  physical = 101,
  atmosphere = 102,
  hydrology = 103,
};

enum class OriginStationOrbitStream : std::uint64_t {
  altitude = 1,
  period = 2,
  phase = 3,
  orientation = 4,
};

template <typename Integer>
[[nodiscard]] auto inclusive(SplitMix64& random, Integer minimum,
                             Integer maximum) noexcept -> Integer {
  return static_cast<Integer>(
      static_cast<std::uint64_t>(minimum) +
      random.bounded(static_cast<std::uint64_t>(maximum) -
                     static_cast<std::uint64_t>(minimum) + 1U));
}

[[nodiscard]] auto home_stream(Seed planet_seed,
                               OriginHomeStream stream) noexcept -> Seed {
  return derive_seed(planet_seed, SeedDomain::planet,
                     static_cast<std::uint64_t>(stream));
}

[[nodiscard]] auto orbit_stream(Seed station_seed,
                                OriginStationOrbitStream stream) noexcept
    -> Seed {
  return derive_seed(station_seed, SeedDomain::orbit,
                     static_cast<std::uint64_t>(stream));
}

[[nodiscard]] auto home_planet_seed(Seed home_system_seed) noexcept -> Seed {
  return derive_seed(home_system_seed, SeedDomain::planet,
                     kOriginHomePlanetOrdinal);
}

[[nodiscard]] auto home_planet_radius(Seed planet_seed) noexcept
    -> PlanetRadiusKm {
  SplitMix64 physical{home_stream(planet_seed, OriginHomeStream::physical)};
  return PlanetRadiusKm{inclusive(physical, kOriginHomeMinimumRadiusKilometres,
                                  kOriginHomeMaximumRadiusKilometres)};
}

[[nodiscard]] auto valid_state(const OriginOnboardingState& state) noexcept
    -> bool {
  switch (state.location) {
    case OriginLocation::docked_at_origin:
      return state.first_objective == FirstObjectiveStatus::offered ||
             state.first_objective == FirstObjectiveStatus::active ||
             state.first_objective == FirstObjectiveStatus::completed;
    case OriginLocation::in_flight:
      return state.first_objective == FirstObjectiveStatus::active ||
             state.first_objective == FirstObjectiveStatus::completed;
  }
  return false;
}

}  // namespace

auto generate_origin_home_planet(Seed home_system_seed) -> PlanetDescriptor {
  const auto planet_seed = home_planet_seed(home_system_seed);
  const auto base = generate_planet_descriptor(planet_seed);
  SplitMix64 physical{home_stream(planet_seed, OriginHomeStream::physical)};
  SplitMix64 atmosphere{home_stream(planet_seed, OriginHomeStream::atmosphere)};
  SplitMix64 hydrology{home_stream(planet_seed, OriginHomeStream::hydrology)};
  const auto radius =
      PlanetRadiusKm{inclusive(physical, kOriginHomeMinimumRadiusKilometres,
                               kOriginHomeMaximumRadiusKilometres)};
  return PlanetDescriptor{
      .seed = planet_seed,
      .id = PlanetId{planet_seed.value},
      .display_name = base.display_name,
      .radius = radius,
      .surface_gravity = SurfaceGravityMilliG{inclusive(
          physical, kOriginHomeMinimumGravityMilliG,
          kOriginHomeMaximumGravityMilliG)},
      .atmosphere_class = AtmosphereClass::temperate,
      .atmosphere_pressure = AtmospherePressureMillibars{inclusive(
          atmosphere, kOriginHomeMinimumPressureMillibars,
          kOriginHomeMaximumPressureMillibars)},
      .terrain_character = TerrainCharacter::plains,
      .water_coverage = WaterCoverageBasisPoints{inclusive(
          hydrology, kOriginHomeMinimumWaterBasisPoints,
          kOriginHomeMaximumWaterBasisPoints)},
      .palette = base.palette,
  };
}

auto is_tutorial_safe_home_planet(const PlanetDescriptor& planet) noexcept
    -> bool {
  return planet.id.value == planet.seed.value &&
         planet.radius.value >= kOriginHomeMinimumRadiusKilometres &&
         planet.radius.value <= kOriginHomeMaximumRadiusKilometres &&
         planet.surface_gravity.value >= kOriginHomeMinimumGravityMilliG &&
         planet.surface_gravity.value <= kOriginHomeMaximumGravityMilliG &&
         planet.atmosphere_class == AtmosphereClass::temperate &&
         planet.atmosphere_pressure.value >=
             kOriginHomeMinimumPressureMillibars &&
         planet.atmosphere_pressure.value <=
             kOriginHomeMaximumPressureMillibars &&
         planet.terrain_character == TerrainCharacter::plains &&
         planet.water_coverage.value >= kOriginHomeMinimumWaterBasisPoints &&
         planet.water_coverage.value <= kOriginHomeMaximumWaterBasisPoints;
}

auto generate_origin_station(Seed universe_seed) noexcept
    -> OriginStationDescriptor {
  const auto home_system_seed =
      derive_seed(universe_seed, SeedDomain::system, kOriginSystemOrdinal);
  const auto station_seed = derive_seed(
      home_system_seed, SeedDomain::settlement, kOriginStationOrdinal);
  const auto planet_seed = home_planet_seed(home_system_seed);
  const auto radius = home_planet_radius(planet_seed);
  SplitMix64 altitude{
      orbit_stream(station_seed, OriginStationOrbitStream::altitude)};
  SplitMix64 period{
      orbit_stream(station_seed, OriginStationOrbitStream::period)};
  SplitMix64 phase{orbit_stream(station_seed, OriginStationOrbitStream::phase)};
  SplitMix64 orientation{
      orbit_stream(station_seed, OriginStationOrbitStream::orientation)};
  const auto inclination_width =
      static_cast<std::uint64_t>(kOriginStationMaximumInclinationMicrodegrees) *
          2U +
      1U;
  return OriginStationDescriptor{
      .universe_seed = universe_seed,
      .home_system_seed = home_system_seed,
      .station_seed = station_seed,
      .id = OriginStationId{station_seed.value},
      .orbit =
          OriginStationOrbit{
              .host_planet = PlanetId{planet_seed.value},
              .radius_kilometres =
                  static_cast<std::uint64_t>(radius.value) +
                  inclusive(altitude, kOriginStationMinimumAltitudeKilometres,
                            kOriginStationMaximumAltitudeKilometres),
              .period_ticks =
                  inclusive(period, kOriginStationMinimumPeriodTicks,
                            kOriginStationMaximumPeriodTicks),
              .epoch_phase_turns = static_cast<std::uint32_t>(phase.next()),
              .inclination_microdegrees = static_cast<std::int32_t>(
                  static_cast<std::int64_t>(
                      orientation.bounded(inclination_width)) -
                  kOriginStationMaximumInclinationMicrodegrees),
              .ascending_node_turns =
                  static_cast<std::uint32_t>(orientation.next()),
          },
  };
}

auto origin_station_id_string(OriginStationId id) -> std::string {
  return std::format("station-{:016x}", id.value);
}

auto initial_origin_onboarding_state(
    const OriginStationDescriptor& station) noexcept -> OriginOnboardingState {
  return OriginOnboardingState{
      .origin_station = station.id,
      .location = OriginLocation::docked_at_origin,
      .first_objective = FirstObjectiveStatus::offered,
  };
}

auto advance_origin_onboarding(
    OriginOnboardingState& state, OriginOnboardingCommand command) noexcept
    -> std::expected<void, OriginOnboardingError> {
  if (!valid_state(state)) {
    return std::unexpected{OriginOnboardingError::invalid_state};
  }

  auto next = state;
  switch (command) {
    case OriginOnboardingCommand::accept_first_objective:
      if (state.location != OriginLocation::docked_at_origin ||
          state.first_objective != FirstObjectiveStatus::offered) {
        return std::unexpected{OriginOnboardingError::invalid_transition};
      }
      next.first_objective = FirstObjectiveStatus::active;
      break;
    case OriginOnboardingCommand::launch:
      if (state.location != OriginLocation::docked_at_origin ||
          state.first_objective != FirstObjectiveStatus::active) {
        return std::unexpected{OriginOnboardingError::invalid_transition};
      }
      next.location = OriginLocation::in_flight;
      break;
    case OriginOnboardingCommand::complete_first_objective:
      if (state.location != OriginLocation::in_flight ||
          state.first_objective != FirstObjectiveStatus::active) {
        return std::unexpected{OriginOnboardingError::invalid_transition};
      }
      next.first_objective = FirstObjectiveStatus::completed;
      break;
    case OriginOnboardingCommand::return_to_origin:
      if (state.location != OriginLocation::in_flight ||
          state.first_objective != FirstObjectiveStatus::completed) {
        return std::unexpected{OriginOnboardingError::invalid_transition};
      }
      next.location = OriginLocation::docked_at_origin;
      break;
    default:
      return std::unexpected{OriginOnboardingError::invalid_transition};
  }

  state = next;
  return {};
}

}  // namespace apsis_drift
