#include "apsis_drift/planetary_flight.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>

namespace apsis_drift {
namespace {

inline constexpr double kAirlessApproachCeilingMetres{20'000.0};
inline constexpr double kTenuousAtmosphereCeilingMetres{60'000.0};
inline constexpr double kTemperateAtmosphereCeilingMetres{100'000.0};
inline constexpr double kDenseAtmosphereCeilingMetres{160'000.0};
inline constexpr double kMinimumOrbitHysteresisMetres{10'000.0};
inline constexpr double kAtmosphericDescentTargetSeconds{100.0};
inline constexpr double kAtmosphericVerticalResponseSeconds{1.8};
inline constexpr SimulationSeconds kMaximumPlanetaryFlightStep{0.25};

[[nodiscard]] auto valid_mode(FlightMode mode) noexcept -> bool {
  return mode == FlightMode::manual || mode == FlightMode::autopilot;
}

[[nodiscard]] auto valid_regime(FlightRegime regime) noexcept -> bool {
  switch (regime) {
    case FlightRegime::orbital:
    case FlightRegime::atmospheric:
    case FlightRegime::terrain_flight: return true;
  }
  return false;
}

[[nodiscard]] auto valid_atmosphere(const PlanetDescriptor& planet) noexcept
    -> bool {
  const auto pressure = planet.atmosphere_pressure.value;
  switch (planet.atmosphere_class) {
    case AtmosphereClass::airless:
      return pressure == 0;
    case AtmosphereClass::tenuous:
      return pressure >= 1 && pressure <= 249;
    case AtmosphereClass::temperate:
      return pressure >= 250 && pressure <= 1'499;
    case AtmosphereClass::dense:
      return pressure >= 1'500 &&
             pressure <= AtmospherePressureMillibars::max;
  }
  return false;
}

[[nodiscard]] auto valid_terrain(TerrainCharacter terrain) noexcept -> bool {
  switch (terrain) {
    case TerrainCharacter::oceanic:
    case TerrainCharacter::plains:
    case TerrainCharacter::rugged:
    case TerrainCharacter::alpine:
    case TerrainCharacter::volcanic: return true;
  }
  return false;
}

[[nodiscard]] auto valid_palette(PaletteFamily palette) noexcept -> bool {
  switch (palette) {
    case PaletteFamily::verdant:
    case PaletteFamily::arid:
    case PaletteFamily::glacial:
    case PaletteFamily::volcanic:
    case PaletteFamily::alien: return true;
  }
  return false;
}

[[nodiscard]] auto valid_planet(const PlanetDescriptor& planet) noexcept
    -> bool {
  return planet.id.value == planet.seed.value &&
         planet.radius.value >= PlanetRadiusKm::min &&
         planet.radius.value <= PlanetRadiusKm::max &&
         planet.surface_gravity.value >= SurfaceGravityMilliG::min &&
         planet.surface_gravity.value <= SurfaceGravityMilliG::max &&
         planet.water_coverage.value <= WaterCoverageBasisPoints::max &&
         valid_atmosphere(planet) && valid_terrain(planet.terrain_character) &&
         valid_palette(planet.palette.family);
}

[[nodiscard]] auto valid_transition(
    const PlanetaryFlightState& state) noexcept -> bool {
  if (!state.last_transition) return true;
  const auto& transition = *state.last_transition;
  return valid_regime(transition.from) && valid_regime(transition.to) &&
         transition.from != transition.to && transition.to == state.regime &&
         transition.tick <= state.tick;
}

[[nodiscard]] auto finite_state(const PlanetaryFlightState& state) noexcept
    -> bool {
  return std::isfinite(state.pose.position.latitude_radians) &&
         std::isfinite(state.pose.position.longitude_radians) &&
         std::isfinite(state.pose.position.altitude_metres) &&
         std::isfinite(state.pose.heading_radians) &&
         std::isfinite(state.velocity.east_metres_per_second) &&
         std::isfinite(state.velocity.north_metres_per_second) &&
         std::isfinite(state.velocity.up_metres_per_second) &&
         std::isfinite(state.clearance_metres) &&
         state.clearance_metres >= kMinimumFlightClearanceMetres &&
         valid_mode(state.mode) && valid_regime(state.regime) &&
         valid_transition(state);
}

[[nodiscard]] auto valid_command(FlightCommandKind kind) noexcept -> bool {
  switch (kind) {
    case FlightCommandKind::press_forward:
    case FlightCommandKind::release_forward:
    case FlightCommandKind::press_backward:
    case FlightCommandKind::release_backward:
    case FlightCommandKind::press_turn_left:
    case FlightCommandKind::release_turn_left:
    case FlightCommandKind::press_turn_right:
    case FlightCommandKind::release_turn_right:
    case FlightCommandKind::press_strafe_left:
    case FlightCommandKind::release_strafe_left:
    case FlightCommandKind::press_strafe_right:
    case FlightCommandKind::release_strafe_right:
    case FlightCommandKind::press_rise:
    case FlightCommandKind::release_rise:
    case FlightCommandKind::press_fall:
    case FlightCommandKind::release_fall:
    case FlightCommandKind::toggle_autopilot: return true;
  }
  return false;
}

auto apply_command(PlanetaryFlightState& state,
                   FlightCommandKind kind) noexcept -> void {
  const auto manual = [&state](bool& control, bool value) {
    control = value;
    if (value) state.mode = FlightMode::manual;
  };
  switch (kind) {
    case FlightCommandKind::press_forward:
      manual(state.controls.forward, true);
      break;
    case FlightCommandKind::release_forward:
      state.controls.forward = false;
      break;
    case FlightCommandKind::press_backward:
      manual(state.controls.backward, true);
      break;
    case FlightCommandKind::release_backward:
      state.controls.backward = false;
      break;
    case FlightCommandKind::press_turn_left:
      manual(state.controls.turn_left, true);
      break;
    case FlightCommandKind::release_turn_left:
      state.controls.turn_left = false;
      break;
    case FlightCommandKind::press_turn_right:
      manual(state.controls.turn_right, true);
      break;
    case FlightCommandKind::release_turn_right:
      state.controls.turn_right = false;
      break;
    case FlightCommandKind::press_strafe_left:
      manual(state.controls.strafe_left, true);
      break;
    case FlightCommandKind::release_strafe_left:
      state.controls.strafe_left = false;
      break;
    case FlightCommandKind::press_strafe_right:
      manual(state.controls.strafe_right, true);
      break;
    case FlightCommandKind::release_strafe_right:
      state.controls.strafe_right = false;
      break;
    case FlightCommandKind::press_rise:
      manual(state.controls.rise, true);
      break;
    case FlightCommandKind::release_rise:
      state.controls.rise = false;
      break;
    case FlightCommandKind::press_fall:
      manual(state.controls.fall, true);
      break;
    case FlightCommandKind::release_fall:
      state.controls.fall = false;
      break;
    case FlightCommandKind::toggle_autopilot:
      state.mode = state.mode == FlightMode::autopilot
                       ? FlightMode::manual
                       : FlightMode::autopilot;
      state.controls = {};
      break;
  }
}

[[nodiscard]] auto atmosphere_ceiling_for(
    AtmosphereClass atmosphere) noexcept -> double {
  switch (atmosphere) {
    case AtmosphereClass::airless: return kAirlessApproachCeilingMetres;
    case AtmosphereClass::tenuous: return kTenuousAtmosphereCeilingMetres;
    case AtmosphereClass::temperate: return kTemperateAtmosphereCeilingMetres;
    case AtmosphereClass::dense: return kDenseAtmosphereCeilingMetres;
  }
  return 0.0;
}

[[nodiscard]] auto parameters_for(const PlanetDescriptor& planet,
                                  FlightRegime regime) noexcept
    -> FlightPerformance {
  switch (regime) {
    case FlightRegime::orbital:
      return {4'000.0, 2'000.0, 1'000.0, 1'000.0, 0.35};
    case FlightRegime::atmospheric: {
      const double vertical_speed = std::max(
          180.0,
          (atmosphere_ceiling_for(planet.atmosphere_class) -
           kTerrainFlightEnterClearanceMetres) /
              kAtmosphericDescentTargetSeconds);
      return {500.0, vertical_speed, 180.0,
              vertical_speed / kAtmosphericVerticalResponseSeconds, 0.75};
    }
    case FlightRegime::terrain_flight:
      return {120.0, 45.0, 100.0, 60.0, 1.15};
  }
  return {};
}

[[nodiscard]] auto bounded_velocity(
    const PlanetDescriptor& planet,
    const PlanetaryFlightState& state) noexcept -> bool {
  if (!valid_regime(state.regime)) return false;
  const auto parameters = parameters_for(planet, state.regime);
  constexpr double tolerance{1.0e-9};
  return std::hypot(state.velocity.east_metres_per_second,
                    state.velocity.north_metres_per_second) <=
             parameters.maximum_horizontal_speed + tolerance &&
         std::abs(state.velocity.up_metres_per_second) <=
             parameters.maximum_vertical_speed + tolerance;
}

auto clamp_velocity(const PlanetDescriptor& planet,
                    PlanetaryFlightState& state) noexcept -> void {
  const auto parameters = parameters_for(planet, state.regime);
  const double horizontal_speed =
      std::hypot(state.velocity.east_metres_per_second,
                 state.velocity.north_metres_per_second);
  if (horizontal_speed > parameters.maximum_horizontal_speed) {
    const double scale =
        parameters.maximum_horizontal_speed / horizontal_speed;
    state.velocity.east_metres_per_second *= scale;
    state.velocity.north_metres_per_second *= scale;
  }
  state.velocity.up_metres_per_second = std::clamp(
      state.velocity.up_metres_per_second,
      -parameters.maximum_vertical_speed,
      parameters.maximum_vertical_speed);
}

[[nodiscard]] auto move_toward(double current, double target,
                               double maximum_delta) noexcept -> double {
  return current +
         std::clamp(target - current, -maximum_delta, maximum_delta);
}

[[nodiscard]] auto canonical_heading(double heading) noexcept -> double {
  constexpr double tau = std::numbers::pi_v<double> * 2.0;
  heading = std::fmod(heading + std::numbers::pi_v<double>, tau);
  if (heading < 0.0) heading += tau;
  return heading - std::numbers::pi_v<double>;
}

[[nodiscard]] auto regime_for_initial(const FlightRegimeBands& bands,
                                      double altitude,
                                      double clearance) noexcept
    -> FlightRegime {
  if (clearance <= bands.terrain_enter_clearance_metres) {
    return FlightRegime::terrain_flight;
  }
  if (altitude <= bands.atmosphere_enter_altitude_metres) {
    return FlightRegime::atmospheric;
  }
  return FlightRegime::orbital;
}

[[nodiscard]] auto next_regime(const FlightRegimeBands& bands,
                               FlightRegime current, double altitude,
                               double clearance) noexcept -> FlightRegime {
  switch (current) {
    case FlightRegime::orbital:
      if (altitude <= bands.atmosphere_enter_altitude_metres) {
        return FlightRegime::atmospheric;
      }
      break;
    case FlightRegime::atmospheric:
      if (clearance <= bands.terrain_enter_clearance_metres) {
        return FlightRegime::terrain_flight;
      }
      if (altitude >= bands.orbit_enter_altitude_metres) {
        return FlightRegime::orbital;
      }
      break;
    case FlightRegime::terrain_flight:
      if (clearance >= bands.terrain_exit_clearance_metres) {
        return FlightRegime::atmospheric;
      }
      break;
  }
  return current;
}

auto hash_word(std::uint64_t& hash, std::uint64_t value) noexcept -> void {
  constexpr std::uint64_t prime{1099511628211ULL};
  for (int byte = 0; byte < 8; ++byte) {
    hash ^= (value >> (byte * 8)) & 0xFFU;
    hash *= prime;
  }
}

auto hash_bool(std::uint64_t& hash, bool value) noexcept -> void {
  hash_word(hash, value ? 1U : 0U);
}

}  // namespace

auto flight_regime_name(FlightRegime regime) noexcept -> std::string_view {
  switch (regime) {
    case FlightRegime::orbital: return "orbital";
    case FlightRegime::atmospheric: return "atmospheric";
    case FlightRegime::terrain_flight: return "terrain-flight";
  }
  return "unknown";
}

auto planetary_flight_error_name(PlanetaryFlightError error) noexcept
    -> std::string_view {
  switch (error) {
    case PlanetaryFlightError::invalid_planet: return "invalid_planet";
    case PlanetaryFlightError::invalid_state: return "invalid_state";
    case PlanetaryFlightError::invalid_environment:
      return "invalid_environment";
    case PlanetaryFlightError::invalid_step: return "invalid_step";
    case PlanetaryFlightError::invalid_command: return "invalid_command";
    case PlanetaryFlightError::wrong_command_tick:
      return "wrong_command_tick";
    case PlanetaryFlightError::tick_overflow: return "tick_overflow";
    case PlanetaryFlightError::coordinate_failure: return "coordinate_failure";
  }
  return "unknown";
}

auto flight_regime_bands(const PlanetDescriptor& planet) noexcept
    -> std::expected<FlightRegimeBands, PlanetaryFlightError> {
  if (!valid_planet(planet)) {
    return std::unexpected{PlanetaryFlightError::invalid_planet};
  }

  const double atmosphere_ceiling =
      atmosphere_ceiling_for(planet.atmosphere_class);
  const double orbit_hysteresis =
      std::max(kMinimumOrbitHysteresisMetres, atmosphere_ceiling * 0.1);
  return FlightRegimeBands{
      .terrain_enter_clearance_metres =
          kTerrainFlightEnterClearanceMetres,
      .terrain_exit_clearance_metres = kTerrainFlightExitClearanceMetres,
      .atmosphere_enter_altitude_metres = atmosphere_ceiling,
      .orbit_enter_altitude_metres = atmosphere_ceiling + orbit_hysteresis,
  };
}

auto flight_performance(const PlanetDescriptor& planet,
                        FlightRegime regime) noexcept
    -> std::expected<FlightPerformance, PlanetaryFlightError> {
  if (!valid_planet(planet)) {
    return std::unexpected{PlanetaryFlightError::invalid_planet};
  }
  if (!valid_regime(regime)) {
    return std::unexpected{PlanetaryFlightError::invalid_state};
  }
  return parameters_for(planet, regime);
}

auto flight_drive_state(const PlanetaryFlightState& state) noexcept
    -> std::expected<FlightDriveState, PlanetaryFlightError> {
  if (!finite_state(state)) {
    return std::unexpected{PlanetaryFlightError::invalid_state};
  }
  double forward = static_cast<double>(state.controls.forward) -
                   static_cast<double>(state.controls.backward);
  double strafe = static_cast<double>(state.controls.strafe_right) -
                  static_cast<double>(state.controls.strafe_left);
  double vertical = static_cast<double>(state.controls.rise) -
                    static_cast<double>(state.controls.fall);
  if (state.mode == FlightMode::autopilot) forward = 0.72;

  const double speed = std::hypot(
      state.velocity.east_metres_per_second,
      state.velocity.north_metres_per_second,
      state.velocity.up_metres_per_second);
  if (forward == 0.0 && strafe == 0.0 && vertical == 0.0) {
    return speed <= 0.5 ? FlightDriveState::idle
                        : FlightDriveState::coast;
  }

  const double heading_cos = std::cos(state.pose.heading_radians);
  const double heading_sin = std::sin(state.pose.heading_radians);
  const double intent_east = heading_cos * forward - heading_sin * strafe;
  const double intent_north = heading_sin * forward + heading_cos * strafe;
  const double alignment =
      intent_east * state.velocity.east_metres_per_second +
      intent_north * state.velocity.north_metres_per_second +
      vertical * state.velocity.up_metres_per_second;
  if (speed > 0.5 && alignment < -0.5) {
    return FlightDriveState::braking;
  }
  if (strafe != 0.0 || vertical != 0.0) {
    return FlightDriveState::maneuvering;
  }
  return forward < 0.0 ? FlightDriveState::reverse
                       : FlightDriveState::forward;
}

auto resolve_target_relative_motion(
    const PlanetDescriptor& planet, const PlanetaryFlightState& state,
    LocalPositionMetres target, double arrival_radius_metres) noexcept
    -> std::expected<TargetRelativeMotion, PlanetaryFlightError> {
  if (!valid_planet(planet) || state.planet != planet.id ||
      !finite_state(state) || !std::isfinite(target.east) ||
      !std::isfinite(target.north) || !std::isfinite(target.up) ||
      !std::isfinite(arrival_radius_metres) || arrival_radius_metres < 0.0) {
    return std::unexpected{PlanetaryFlightError::invalid_state};
  }
  const double distance = std::hypot(target.east, target.north, target.up);
  if (!std::isfinite(distance)) {
    return std::unexpected{PlanetaryFlightError::invalid_state};
  }
  TargetRelativeMotion motion;
  if (distance <= std::max(1.0e-9, arrival_radius_metres)) return motion;

  motion.closing_speed_metres_per_second =
      (target.east * state.velocity.east_metres_per_second +
       target.north * state.velocity.north_metres_per_second +
       target.up * state.velocity.up_metres_per_second) /
      distance;
  const auto performance = flight_performance(planet, state.regime);
  if (!performance ||
      !std::isfinite(motion.closing_speed_metres_per_second)) {
    return std::unexpected{PlanetaryFlightError::invalid_state};
  }
  const double braking_acceleration =
      std::min(performance->horizontal_acceleration,
               performance->vertical_acceleration);
  const double remaining =
      std::max(0.0, distance - arrival_radius_metres);
  constexpr double motion_threshold{1.0};
  if (motion.closing_speed_metres_per_second > motion_threshold) {
    const double closing = motion.closing_speed_metres_per_second;
    motion.arrival_estimate_seconds = remaining / closing;
    motion.stopping_distance_metres =
        closing * closing / (2.0 * braking_acceleration);
    motion.cue = remaining <= motion.stopping_distance_metres * 1.25
                     ? TargetMotionCue::brake
                     : TargetMotionCue::closing;
  } else if (motion.closing_speed_metres_per_second < -motion_threshold) {
    motion.cue = TargetMotionCue::opening;
  }
  return motion;
}

auto initial_planetary_flight_state(
    const PlanetDescriptor& planet, GeodeticPosition position,
    PlanetaryFlightEnvironment environment, double heading_radians,
    FlightMode mode) noexcept
    -> std::expected<PlanetaryFlightState, PlanetaryFlightError> {
  const auto bands = flight_regime_bands(planet);
  if (!bands) return std::unexpected{bands.error()};
  if (!std::isfinite(environment.surface_elevation_metres)) {
    return std::unexpected{PlanetaryFlightError::invalid_environment};
  }
  if (!std::isfinite(heading_radians) || !valid_mode(mode)) {
    return std::unexpected{PlanetaryFlightError::invalid_state};
  }
  if (!planet_fixed_from_geodetic(planet, position)) {
    return std::unexpected{PlanetaryFlightError::coordinate_failure};
  }
  const double clearance =
      position.altitude_metres - environment.surface_elevation_metres;
  if (!std::isfinite(clearance) ||
      clearance < kMinimumFlightClearanceMetres) {
    return std::unexpected{PlanetaryFlightError::invalid_environment};
  }

  PlanetaryFlightState state{
      .tick = 0,
      .planet = planet.id,
      .pose = {position, canonical_heading(heading_radians)},
      .velocity = {},
      .clearance_metres = clearance,
      .mode = mode,
      .controls = {},
      .regime = regime_for_initial(*bands, position.altitude_metres,
                                   clearance),
      .last_transition = std::nullopt,
  };
  if (!finite_state(state)) {
    return std::unexpected{PlanetaryFlightError::invalid_state};
  }
  return state;
}

auto validate_planetary_flight_state(
    const PlanetDescriptor& planet,
    const PlanetaryFlightState& state) noexcept
    -> std::expected<void, PlanetaryFlightError> {
  if (!valid_planet(planet) || state.planet != planet.id ||
      !finite_state(state) || !bounded_velocity(planet, state)) {
    return std::unexpected{PlanetaryFlightError::invalid_state};
  }
  if (!planet_fixed_from_geodetic(planet, state.pose.position)) {
    return std::unexpected{PlanetaryFlightError::coordinate_failure};
  }
  return {};
}

auto advance_planetary_flight(
    const PlanetDescriptor& planet, PlanetaryFlightEnvironment environment,
    PlanetaryFlightState& state, std::span<const FlightCommand> commands,
    SimulationSeconds step) noexcept
    -> std::expected<void, PlanetaryFlightError> {
  const auto bands = flight_regime_bands(planet);
  if (!bands) return std::unexpected{bands.error()};
  if (state.planet != planet.id || !finite_state(state) ||
      !bounded_velocity(planet, state)) {
    return std::unexpected{PlanetaryFlightError::invalid_state};
  }
  if (!std::isfinite(environment.surface_elevation_metres)) {
    return std::unexpected{PlanetaryFlightError::invalid_environment};
  }
  if (!std::isfinite(step.count()) || step <= SimulationSeconds::zero() ||
      step > kMaximumPlanetaryFlightStep) {
    return std::unexpected{PlanetaryFlightError::invalid_step};
  }
  if (state.tick == std::numeric_limits<SimulationTick>::max()) {
    return std::unexpected{PlanetaryFlightError::tick_overflow};
  }
  for (const auto& command : commands) {
    if (!valid_command(command.kind)) {
      return std::unexpected{PlanetaryFlightError::invalid_command};
    }
    if (command.tick != state.tick) {
      return std::unexpected{PlanetaryFlightError::wrong_command_tick};
    }
  }

  PlanetaryFlightState next = state;
  next.clearance_metres = next.pose.position.altitude_metres -
                          environment.surface_elevation_metres;
  if (next.clearance_metres < kMinimumFlightClearanceMetres) {
    next.pose.position.altitude_metres =
        environment.surface_elevation_metres + kMinimumFlightClearanceMetres;
    next.clearance_metres = kMinimumFlightClearanceMetres;
    next.velocity.up_metres_per_second =
        std::max(0.0, next.velocity.up_metres_per_second);
  }
  for (const auto& command : commands) apply_command(next, command.kind);

  double forward = static_cast<double>(next.controls.forward) -
                   static_cast<double>(next.controls.backward);
  double turn = static_cast<double>(next.controls.turn_right) -
                static_cast<double>(next.controls.turn_left);
  double strafe = static_cast<double>(next.controls.strafe_right) -
                  static_cast<double>(next.controls.strafe_left);
  double vertical = static_cast<double>(next.controls.rise) -
                    static_cast<double>(next.controls.fall);
  if (next.mode == FlightMode::autopilot) {
    forward = 0.72;
    turn = 0.055;
    strafe = 0.0;
    vertical = 0.0;
  }

  const auto parameters = parameters_for(planet, next.regime);
  const double dt = step.count();
  next.pose.heading_radians = canonical_heading(
      next.pose.heading_radians +
      turn * parameters.turn_rate_radians_per_second * dt);

  const double heading_cos = std::cos(next.pose.heading_radians);
  const double heading_sin = std::sin(next.pose.heading_radians);
  double target_east =
      (heading_cos * forward - heading_sin * strafe) *
      parameters.maximum_horizontal_speed;
  double target_north =
      (heading_sin * forward + heading_cos * strafe) *
      parameters.maximum_horizontal_speed;
  const double target_horizontal_speed = std::hypot(target_east, target_north);
  if (target_horizontal_speed > parameters.maximum_horizontal_speed) {
    const double scale =
        parameters.maximum_horizontal_speed / target_horizontal_speed;
    target_east *= scale;
    target_north *= scale;
  }
  double target_up = vertical * parameters.maximum_vertical_speed;
  if (next.regime == FlightRegime::orbital) {
    if (forward == 0.0 && strafe == 0.0) {
      target_east = next.velocity.east_metres_per_second;
      target_north = next.velocity.north_metres_per_second;
    }
    if (vertical == 0.0) {
      target_up = next.velocity.up_metres_per_second;
    }
  }
  next.velocity.east_metres_per_second = move_toward(
      next.velocity.east_metres_per_second, target_east,
      parameters.horizontal_acceleration * dt);
  next.velocity.north_metres_per_second = move_toward(
      next.velocity.north_metres_per_second, target_north,
      parameters.horizontal_acceleration * dt);
  next.velocity.up_metres_per_second = move_toward(
      next.velocity.up_metres_per_second, target_up,
      parameters.vertical_acceleration * dt);
  clamp_velocity(planet, next);

  const auto frame = make_local_tangent_frame(planet, next.pose.position);
  if (!frame) {
    return std::unexpected{PlanetaryFlightError::coordinate_failure};
  }
  PlanetFixedPositionMetres fixed = frame->origin;
  fixed.x += (frame->east.x * next.velocity.east_metres_per_second +
              frame->north.x * next.velocity.north_metres_per_second +
              frame->up.x * next.velocity.up_metres_per_second) *
             dt;
  fixed.y += (frame->east.y * next.velocity.east_metres_per_second +
              frame->north.y * next.velocity.north_metres_per_second +
              frame->up.y * next.velocity.up_metres_per_second) *
             dt;
  fixed.z += (frame->east.z * next.velocity.east_metres_per_second +
              frame->north.z * next.velocity.north_metres_per_second +
              frame->up.z * next.velocity.up_metres_per_second) *
             dt;
  const auto position = geodetic_from_planet_fixed(planet, fixed);
  if (!position) {
    return std::unexpected{PlanetaryFlightError::coordinate_failure};
  }
  next.pose.position = *position;

  const double minimum_altitude =
      environment.surface_elevation_metres +
      kMinimumFlightClearanceMetres;
  if (next.pose.position.altitude_metres < minimum_altitude) {
    next.pose.position.altitude_metres = minimum_altitude;
    next.velocity.up_metres_per_second =
        std::max(0.0, next.velocity.up_metres_per_second);
  }
  next.clearance_metres = next.pose.position.altitude_metres -
                          environment.surface_elevation_metres;
  ++next.tick;

  const FlightRegime previous_regime = next.regime;
  next.regime = next_regime(*bands, next.regime,
                            next.pose.position.altitude_metres,
                            next.clearance_metres);
  if (next.regime != previous_regime) {
    next.last_transition = FlightRegimeTransition{
        previous_regime, next.regime, next.tick};
    clamp_velocity(planet, next);
  }

  if (!finite_state(next) || !bounded_velocity(planet, next) ||
      !planet_fixed_from_geodetic(planet, next.pose.position)) {
    return std::unexpected{PlanetaryFlightError::invalid_state};
  }
  state = next;
  return {};
}

auto planetary_flight_state_checksum(
    const PlanetaryFlightState& state) noexcept -> std::uint64_t {
  constexpr std::uint64_t offset{1469598103934665603ULL};
  std::uint64_t hash = offset;
  hash_word(hash, state.tick);
  hash_word(hash, state.planet.value);
  hash_word(hash,
            std::bit_cast<std::uint64_t>(state.pose.position.latitude_radians));
  hash_word(hash, std::bit_cast<std::uint64_t>(
                      state.pose.position.longitude_radians));
  hash_word(hash,
            std::bit_cast<std::uint64_t>(state.pose.position.altitude_metres));
  hash_word(hash,
            std::bit_cast<std::uint64_t>(state.pose.heading_radians));
  hash_word(hash, std::bit_cast<std::uint64_t>(
                      state.velocity.east_metres_per_second));
  hash_word(hash, std::bit_cast<std::uint64_t>(
                      state.velocity.north_metres_per_second));
  hash_word(hash, std::bit_cast<std::uint64_t>(
                      state.velocity.up_metres_per_second));
  hash_word(hash, std::bit_cast<std::uint64_t>(state.clearance_metres));
  hash_word(hash, static_cast<std::uint8_t>(state.mode));
  hash_bool(hash, state.controls.forward);
  hash_bool(hash, state.controls.backward);
  hash_bool(hash, state.controls.turn_left);
  hash_bool(hash, state.controls.turn_right);
  hash_bool(hash, state.controls.strafe_left);
  hash_bool(hash, state.controls.strafe_right);
  hash_bool(hash, state.controls.rise);
  hash_bool(hash, state.controls.fall);
  hash_word(hash, static_cast<std::uint8_t>(state.regime));
  hash_bool(hash, state.last_transition.has_value());
  if (state.last_transition) {
    hash_word(hash, static_cast<std::uint8_t>(state.last_transition->from));
    hash_word(hash, static_cast<std::uint8_t>(state.last_transition->to));
    hash_word(hash, state.last_transition->tick);
  }
  return hash;
}

}  // namespace apsis_drift
