#include "apsis_drift/cockpit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string_view>

namespace apsis_drift {
namespace {

inline constexpr int kWideCockpitCols{120};
inline constexpr int kWideCockpitRows{32};
inline constexpr int kCompactRailCols{12};
inline constexpr int kWideRailCols{18};
inline constexpr int kHeaderRows{1};
inline constexpr int kMessageRows{3};
inline constexpr int kStatusRows{1};
inline constexpr int kRailGutterCols{1};
inline constexpr int kMaximumTerminalAxis{65535};
inline constexpr double kRadiansToDegrees{
    180.0 / 3.141592653589793238462643383279502884};

[[nodiscard]] auto valid_mode(FlightMode mode) noexcept -> bool {
  return mode == FlightMode::manual || mode == FlightMode::autopilot;
}

[[nodiscard]] auto valid_regime(FlightRegime regime) noexcept -> bool {
  return regime == FlightRegime::orbital ||
         regime == FlightRegime::atmospheric ||
         regime == FlightRegime::terrain_flight;
}

[[nodiscard]] auto valid_target_motion_cue(TargetMotionCue cue) noexcept
    -> bool {
  switch (cue) {
    case TargetMotionCue::holding:
    case TargetMotionCue::closing:
    case TargetMotionCue::opening:
    case TargetMotionCue::brake: return true;
  }
  return false;
}

[[nodiscard]] auto short_regime_name(FlightRegime regime) noexcept
    -> std::string_view {
  switch (regime) {
    case FlightRegime::orbital: return "ORB";
    case FlightRegime::atmospheric: return "ATM";
    case FlightRegime::terrain_flight: return "TERR";
  }
  return "----";
}

[[nodiscard]] auto telemetry_is_finite(const FlightState& state) noexcept
    -> bool {
  return std::isfinite(state.pose.x) && std::isfinite(state.pose.y) &&
         std::isfinite(state.pose.yaw) &&
         std::isfinite(state.pose.altitude) &&
         std::isfinite(state.clearance) &&
         std::isfinite(state.velocity.x) &&
         std::isfinite(state.velocity.y) &&
         std::isfinite(state.velocity.vertical) && valid_mode(state.mode);
}

[[nodiscard]] auto rounded_in_range(double value, long long minimum,
                                    long long maximum) noexcept
    -> std::optional<long long> {
  if (!std::isfinite(value) || value <= static_cast<double>(minimum) - 0.5 ||
      value >= static_cast<double>(maximum) + 0.5) {
    return std::nullopt;
  }
  return static_cast<long long>(std::round(value));
}

[[nodiscard]] auto format_altitude(float altitude) -> std::string {
  const auto rounded = rounded_in_range(altitude, -9999, 99999);
  return rounded ? std::format("ALT {:05}", *rounded) : "ALT #####";
}

[[nodiscard]] auto format_three_digit(std::string_view label,
                                      double value) -> std::string {
  const auto rounded = rounded_in_range(value, 0, 999);
  return rounded ? std::format("{} {:03}  ", label, *rounded)
                 : std::format("{} ###  ", label);
}

[[nodiscard]] auto format_speed(double value) -> std::string {
  const auto rounded = rounded_in_range(value, 0, 999);
  if (rounded) return std::format("SPD {:03}  ", *rounded);
  if (std::isfinite(value) && value >= 0.0 && value < 9'950.0) {
    return std::format("SPD {:.1f}k ", value / 1'000.0);
  }
  if (std::isfinite(value) && value >= 0.0 && value < 9'950'000.0) {
    return std::format("SPD {:.1f}M ", value / 1'000'000.0);
  }
  return "SPD ###  ";
}

[[nodiscard]] auto format_drive(FlightDriveState drive) -> std::string {
  switch (drive) {
    case FlightDriveState::idle: return "THR IDLE ";
    case FlightDriveState::coast: return "COAST    ";
    case FlightDriveState::forward: return "THR FWD  ";
    case FlightDriveState::reverse: return "THR REV  ";
    case FlightDriveState::maneuvering: return "THR MANUV";
    case FlightDriveState::braking: return "BRAKING  ";
  }
  return "THR ---- ";
}

[[nodiscard]] auto format_closing_speed(double value) -> std::string {
  const auto rounded = rounded_in_range(std::abs(value), 0, 999);
  if (rounded) {
    return std::format("CLS {}{:03} ", value < 0.0 ? '-' : '+', *rounded);
  }
  if (std::isfinite(value) && std::abs(value) < 9'950.0) {
    return std::format("CLS {:+.1f}k", value / 1'000.0);
  }
  return "CLS #### ";
}

[[nodiscard]] auto format_arrival(
    std::optional<double> seconds) -> std::string {
  if (!seconds || !std::isfinite(*seconds) || *seconds < 0.0) {
    return "ETA --:--";
  }
  const auto rounded = static_cast<long long>(std::ceil(*seconds));
  if (rounded > 99 * 60 + 59) return "ETA >99m ";
  return std::format("ETA {:02}:{:02}", rounded / 60, rounded % 60);
}

[[nodiscard]] auto aspect_fit(termforge::Rect available,
                              termforge::Extent cell_pixels,
                              ViewportSize viewport) noexcept
    -> termforge::Rect {
  if (available.empty() || cell_pixels.empty() || viewport.width <= 0 ||
      viewport.height <= 0) {
    return {};
  }

  using i64 = std::int64_t;
  int width = available.w;
  int height = static_cast<int>(
      (i64{width} * viewport.height * cell_pixels.w) /
      (i64{viewport.width} * cell_pixels.h));
  height = std::max(1, height);
  if (height > available.h) {
    height = available.h;
    width = static_cast<int>(
        (i64{height} * viewport.width * cell_pixels.h) /
        (i64{viewport.height} * cell_pixels.w));
    width = std::clamp(width, 1, available.w);
  }

  return {available.x + (available.w - width) / 2,
          available.y + (available.h - height) / 2, width, height};
}

}  // namespace

auto compute_cockpit_layout(int cols, int rows,
                            termforge::Extent cell_pixels,
                            ViewportSize viewport) noexcept
    -> CockpitLayout {
  CockpitLayout layout;
  if (cols <= 0 || rows <= 0) return layout;
  layout.screen = {0, 0, cols, rows};

  if (cols > kMaximumTerminalAxis || rows > kMaximumTerminalAxis ||
      cols < kMinimumCockpitCols || rows < kMinimumCockpitRows ||
      cell_pixels.empty() || cell_pixels.w > kMaximumTerminalAxis ||
      cell_pixels.h > kMaximumTerminalAxis || !validate_viewport(viewport)) {
    return layout;
  }

  const bool wide = cols >= kWideCockpitCols && rows >= kWideCockpitRows;
  layout.mode = wide ? CockpitLayoutMode::wide
                     : CockpitLayoutMode::compact;
  const int rail_cols = wide ? kWideRailCols : kCompactRailCols;

  layout.header = {0, 0, cols, kHeaderRows};
  layout.messages = {0, rows - kStatusRows - kMessageRows, cols,
                     kMessageRows};
  layout.status = {0, rows - kStatusRows, cols, kStatusRows};

  const int deck_y = kHeaderRows;
  const int deck_rows = rows - kHeaderRows - kMessageRows - kStatusRows;
  layout.left_instruments = {0, deck_y, rail_cols, deck_rows};
  layout.right_instruments = {cols - rail_cols, deck_y, rail_cols,
                              deck_rows};

  const termforge::Rect center{
      rail_cols + kRailGutterCols,
      deck_y,
      cols - 2 * (rail_cols + kRailGutterCols),
      deck_rows,
  };
  const termforge::Rect viewport_available{
      center.x + 1,
      center.y + 1,
      std::max(0, center.w - 2),
      std::max(0, center.h - 2),
  };
  layout.viewport = aspect_fit(viewport_available, cell_pixels, viewport);
  if (layout.viewport.empty()) {
    layout.mode = CockpitLayoutMode::too_small;
    return layout;
  }
  layout.viewport_frame = {
      layout.viewport.x - 1,
      layout.viewport.y - 1,
      layout.viewport.w + 2,
      layout.viewport.h + 2,
  };
  return layout;
}

auto format_flight_instruments(const FlightState& state)
    -> FlightInstrumentReadout {
  FlightInstrumentReadout readout;
  const bool valid = telemetry_is_finite(state);
  if (!valid) {
    readout.heading = "HDG ---  ";
    readout.altitude = "ALT -----";
    readout.clearance = "CLR ---  ";
    readout.speed = "SPD ---  ";
    readout.mode = valid_mode(state.mode)
                       ? (state.mode == FlightMode::autopilot ? "MODE AUTO"
                                                               : "MODE MAN ")
                       : "MODE ----";
    readout.drive = "THR ---- ";
    readout.alert = "TELEM ERR";
    readout.alert_state = CockpitAlert::invalid_telemetry;
    return readout;
  }

  double heading = std::fmod(
      static_cast<double>(state.pose.yaw) * kRadiansToDegrees, 360.0);
  if (heading < 0.0) heading += 360.0;
  int heading_degrees = static_cast<int>(std::round(heading)) % 360;
  readout.heading = std::format("HDG {:03}  ", heading_degrees);
  readout.altitude = format_altitude(state.pose.altitude);
  readout.clearance = format_three_digit("CLR", state.clearance);
  readout.speed = format_speed(std::hypot(
      static_cast<double>(state.velocity.x),
      static_cast<double>(state.velocity.y),
      static_cast<double>(state.velocity.vertical)));
  readout.mode = state.mode == FlightMode::autopilot ? "MODE AUTO"
                                                      : "MODE MAN ";
  const double speed = std::hypot(
      static_cast<double>(state.velocity.x),
      static_cast<double>(state.velocity.y),
      static_cast<double>(state.velocity.vertical));
  readout.drive = state.mode == FlightMode::autopilot
                      ? "THR AUTO "
                      : (speed > 0.5 ? "COAST    " : "THR IDLE ");
  if (state.clearance <= kLowClearanceWarning) {
    readout.alert = "! LOW CLR";
    readout.alert_state = CockpitAlert::low_clearance;
  } else {
    readout.alert = std::string(kInstrumentLineWidth, ' ');
  }
  return readout;
}

auto format_flight_instruments(const PlanetaryFlightState& state)
    -> FlightInstrumentReadout {
  FlightInstrumentReadout readout;
  const bool valid =
      std::isfinite(state.pose.position.altitude_metres) &&
      std::isfinite(state.pose.heading_radians) &&
      std::isfinite(state.clearance_metres) &&
      std::isfinite(state.velocity.east_metres_per_second) &&
      std::isfinite(state.velocity.north_metres_per_second) &&
      valid_mode(state.mode);
  if (!valid) {
    readout.heading = "HDG ---  ";
    readout.altitude = "ALT -----";
    readout.clearance = "CLR ---  ";
    readout.speed = "SPD ---  ";
    readout.mode = valid_mode(state.mode)
                       ? (state.mode == FlightMode::autopilot ? "MODE AUTO"
                                                               : "MODE MAN ")
                       : "MODE ----";
    readout.drive = "THR ---- ";
    readout.alert = "TELEM ERR";
    readout.alert_state = CockpitAlert::invalid_telemetry;
    return readout;
  }

  double heading =
      std::fmod(state.pose.heading_radians * kRadiansToDegrees, 360.0);
  if (heading < 0.0) heading += 360.0;
  readout.heading = std::format(
      "HDG {:03}  ", static_cast<int>(std::round(heading)) % 360);
  readout.altitude =
      format_altitude(static_cast<float>(state.pose.position.altitude_metres));
  readout.clearance =
      format_three_digit("CLR", state.clearance_metres);
  readout.speed = format_speed(std::hypot(
      state.velocity.east_metres_per_second,
      state.velocity.north_metres_per_second,
      state.velocity.up_metres_per_second));
  readout.mode = state.mode == FlightMode::autopilot ? "MODE AUTO"
                                                      : "MODE MAN ";
  const auto drive = flight_drive_state(state);
  readout.drive = drive ? format_drive(*drive) : "THR ---- ";
  if (state.clearance_metres <= kLowClearanceWarning) {
    readout.alert = "! LOW CLR";
    readout.alert_state = CockpitAlert::low_clearance;
  } else {
    readout.alert = std::string(kInstrumentLineWidth, ' ');
  }
  return readout;
}

auto format_flight_instruments(const SystemFlightState& state)
    -> FlightInstrumentReadout {
  FlightInstrumentReadout readout;
  const double speed = std::hypot(state.velocity.x, state.velocity.y,
                                  state.velocity.z);
  const bool valid = std::isfinite(state.forward.x) &&
                     std::isfinite(state.forward.y) &&
                     std::isfinite(state.forward.z) && std::isfinite(speed) &&
                     valid_mode(state.mode);
  if (!valid) {
    readout = {.heading = "HDG ---  ", .altitude = "SYS ---- ",
               .clearance = "TIME --- ", .speed = "SPD ---  ",
               .mode = "MODE ----", .drive = "THR ---- ",
               .alert = "TELEM ERR",
               .alert_state = CockpitAlert::invalid_telemetry};
    return readout;
  }
  double heading = std::fmod(
      std::atan2(state.forward.y, state.forward.x) * kRadiansToDegrees,
      360.0);
  if (heading < 0.0) heading += 360.0;
  readout.heading = std::format(
      "HDG {:03}  ", static_cast<int>(std::round(heading)) % 360);
  readout.altitude = "SYS FLT  ";
  readout.clearance = std::format(
      "TIME {:>2}x ", system_time_scale_value(state.time_scale));
  readout.speed = format_speed(speed);
  readout.mode = state.mode == FlightMode::autopilot ? "MODE AUTO"
                                                      : "MODE MAN ";
  const bool maneuver = state.controls.turn_left || state.controls.turn_right ||
                        state.controls.strafe_left ||
                        state.controls.strafe_right || state.controls.rise ||
                        state.controls.fall;
  readout.drive = state.mode == FlightMode::autopilot
                      ? "THR AUTO "
                      : (state.controls.forward
                             ? "THR FWD  "
                             : (state.controls.backward
                                    ? "BRAKING  "
                                    : (maneuver ? "THR MANUV" : "COAST    ")));
  readout.alert = std::string(kInstrumentLineWidth, ' ');
  return readout;
}

auto format_flight_instruments(const OriginStationFlightState& state)
    -> FlightInstrumentReadout {
  FlightInstrumentReadout readout;
  const double speed =
      std::hypot(state.relative_velocity.x, state.relative_velocity.y,
                 state.relative_velocity.z);
  const bool valid = std::isfinite(state.forward.x) &&
                     std::isfinite(state.forward.y) &&
                     std::isfinite(state.forward.z) && std::isfinite(speed) &&
                     valid_mode(state.mode);
  if (!valid) {
    return {.heading = "HDG ---  ", .altitude = "SYS ---- ",
            .clearance = "DOCK --- ", .speed = "SPD ---  ",
            .mode = "MODE ----", .drive = "THR ---- ",
            .alert = "TELEM ERR",
            .alert_state = CockpitAlert::invalid_telemetry};
  }
  double heading = std::fmod(
      std::atan2(state.forward.y, state.forward.x) * kRadiansToDegrees,
      360.0);
  if (heading < 0.0) heading += 360.0;
  readout.heading = std::format(
      "HDG {:03}  ", static_cast<int>(std::round(heading)) % 360);
  readout.altitude = "SYS RTN  ";
  readout.clearance = "DOCK ARMED";
  readout.speed = format_speed(speed);
  readout.mode = state.mode == FlightMode::autopilot ? "MODE AUTO"
                                                      : "MODE MAN ";
  const bool maneuver = state.controls.turn_left || state.controls.turn_right ||
                        state.controls.strafe_left ||
                        state.controls.strafe_right || state.controls.rise ||
                        state.controls.fall;
  readout.drive = state.mode == FlightMode::autopilot
                      ? "THR AUTO "
                      : (state.controls.forward
                             ? "THR FWD  "
                             : (state.controls.backward
                                    ? "BRAKING  "
                                    : (maneuver ? "THR MANUV" : "COAST    ")));
  readout.alert = std::string(kInstrumentLineWidth, ' ');
  return readout;
}

auto format_flight_regime(const PlanetaryFlightState& state)
    -> FlightRegimeReadout {
  FlightRegimeReadout readout;
  const bool transition_valid =
      !state.last_transition ||
      (valid_regime(state.last_transition->from) &&
       valid_regime(state.last_transition->to) &&
       state.last_transition->from != state.last_transition->to &&
       state.last_transition->to == state.regime &&
       state.last_transition->tick <= state.tick);
  readout.valid = valid_regime(state.regime) && transition_valid;
  if (!readout.valid) {
    readout.regime = "REG ---- ";
    readout.transition = "TRANS ERR";
    return readout;
  }

  readout.regime = std::format("REG {:<5}", short_regime_name(state.regime));
  if (state.last_transition) {
    readout.transition =
        std::format("{:<4}>{:<4}",
                    short_regime_name(state.last_transition->from),
                    short_regime_name(state.last_transition->to));
  } else {
    readout.transition = std::string(kInstrumentLineWidth, ' ');
  }
  return readout;
}

auto format_thermal_instruments(
    const PlanetDescriptor& planet,
    const PlanetaryFlightState& state) -> ThermalInstrumentReadout {
  const auto assessment = resolve_thermal_assessment(planet, state);
  if (!assessment) {
    return {.load = "HEAT --- ",
            .trend = "TEMP ?   ",
            .limit = "LIM ---  ",
            .flight_path_angle = "FPA ---- ",
            .cue = "THERM ERR",
            .valid = false};
  }
  const std::string_view trend =
      assessment->trend == ThermalTrend::heating
          ? "TEMP +   "
          : (assessment->trend == ThermalTrend::cooling ? "TEMP -   "
                                                         : "TEMP =   ");
  const std::string_view cue =
      assessment->cue == ThermalCue::abort_climb
          ? "ABRT CLMB"
          : (assessment->cue == ThermalCue::slow_and_rise
                 ? "SLOW+RISE"
                 : (assessment->cue == ThermalCue::cooling ? "COOLING  "
                                                            : "HEAT OK  "));
  return {
      .load = std::format("HEAT {:03}%", assessment->load_percent),
      .trend = std::string{trend},
      .limit = "LIM 100% ",
      .flight_path_angle = std::format(
          "{:<9}", std::format("FPA {:+.0f}",
                                assessment->flight_path_angle_degrees)),
      .cue = std::string{cue},
      .trend_state = assessment->trend,
      .cue_state = assessment->cue,
      .valid = true,
  };
}

auto format_signal_scanner(const SignalNavigationSolution& navigation)
    -> SignalScannerReadout {
  SignalScannerReadout readout{
      .target = "TGT --/--",
      .bearing = "BRG ---  ",
      .distance = "DST ---- ",
      .motion = "CLS ---- ",
      .arrival = "ETA --:--",
      .strength = "SIG ---  ",
      .cue = "NO SIGNAL",
      .status = navigation.status,
  };
  if (navigation.status == SignalScannerStatus::no_signal) return readout;

  const bool valid = navigation.selected.has_value() &&
                     navigation.ordinal < kSurfaceSignalCount &&
                     std::isfinite(navigation.absolute_bearing_radians) &&
                     std::isfinite(navigation.relative_bearing_radians) &&
                     std::isfinite(navigation.distance_metres) &&
                     navigation.distance_metres >= 0.0 &&
                     std::isfinite(
                         navigation.motion.closing_speed_metres_per_second) &&
                     std::isfinite(
                         navigation.motion.stopping_distance_metres) &&
                     navigation.motion.stopping_distance_metres >= 0.0 &&
                     (!navigation.motion.arrival_estimate_seconds ||
                      (std::isfinite(
                           *navigation.motion.arrival_estimate_seconds) &&
                       *navigation.motion.arrival_estimate_seconds >= 0.0)) &&
                     valid_target_motion_cue(navigation.motion.cue) &&
                     navigation.strength_basis_points <= 10'000;
  if (!valid) {
    readout.status = SignalScannerStatus::no_signal;
    readout.cue = "SCAN ERR ";
    return readout;
  }

  readout.target = std::format("TGT {:02}/{:02}", navigation.ordinal + 1U,
                               kSurfaceSignalCount);
  double degrees = std::fmod(
      navigation.absolute_bearing_radians * kRadiansToDegrees, 360.0);
  if (degrees < 0.0) degrees += 360.0;
  readout.bearing = std::format(
      "BRG {:03}  ", static_cast<int>(std::round(degrees)) % 360);

  char distance_unit{'m'};
  auto rounded_distance =
      rounded_in_range(navigation.distance_metres, 0, 9999);
  if (!rounded_distance) {
    distance_unit = 'k';
    rounded_distance =
        rounded_in_range(navigation.distance_metres / 1'000.0, 0, 9999);
  }
  readout.distance =
      rounded_distance ? std::format("DST {:>4}{}", *rounded_distance,
                                     distance_unit)
                       : "DST #### ";
  readout.motion = format_closing_speed(
      navigation.motion.closing_speed_metres_per_second);
  readout.arrival =
      format_arrival(navigation.motion.arrival_estimate_seconds);
  const auto strength_percent = static_cast<unsigned>(
      (navigation.strength_basis_points + 50U) / 100U);
  readout.strength = std::format("SIG {:03}% ", strength_percent);

  switch (navigation.status) {
    case SignalScannerStatus::no_signal:
      readout.cue = "NO SIGNAL";
      break;
    case SignalScannerStatus::out_of_range:
      readout.cue = "OUT RANGE";
      break;
    case SignalScannerStatus::occluded:
      readout.cue = "OCCLUDED ";
      break;
    case SignalScannerStatus::reached:
      readout.cue = "REACHED! ";
      break;
    case SignalScannerStatus::tracking: {
      if (navigation.motion.cue == TargetMotionCue::brake) {
        readout.cue = "BRAKE NOW";
        break;
      }
      if (navigation.motion.cue == TargetMotionCue::opening) {
        readout.cue = "OPENING! ";
        break;
      }
      const double relative_degrees =
          navigation.relative_bearing_radians * kRadiansToDegrees;
      if (std::abs(relative_degrees) <= 5.0) {
        readout.cue = navigation.motion.cue == TargetMotionCue::closing
                          ? "CLOSING  "
                          : "THRUST >>";
      } else if (relative_degrees > 0.0) {
        readout.cue = "TURN RGHT";
      } else {
        readout.cue = "TURN LEFT";
      }
      break;
    }
  }
  return readout;
}

auto format_signal_collection(const SignalCollectionState& collection)
    -> SignalCollectionReadout {
  SignalCollectionReadout readout{
      .cue = "APPROACH ",
      .message = " Approach selected signal | enter and hold within 1000m ",
      .status = collection.status,
  };
  switch (collection.status) {
    case SignalCollectionStatus::approach:
      if (collection.consecutive_in_range_ticks != 0 ||
          collection.completion_tick) {
        readout.cue = "SCAN ERR ";
        readout.message = " Scan state invalid | progress unavailable ";
      }
      break;
    case SignalCollectionStatus::in_range: {
      if (collection.consecutive_in_range_ticks == 0 ||
          collection.consecutive_in_range_ticks >
              kSignalCollectionAcquireTicks ||
          collection.completion_tick) {
        readout.cue = "SCAN ERR ";
        readout.message = " Scan state invalid | progress unavailable ";
        break;
      }
      readout.progress_percent = static_cast<unsigned>(
          (collection.consecutive_in_range_ticks * 100U +
           kSignalCollectionAcquireTicks / 2U) /
          kSignalCollectionAcquireTicks);
      readout.cue = std::format("LOCK {:03}%", readout.progress_percent);
      readout.message = std::format(
          " Target lock {:03}% | remain within 1000m ",
          readout.progress_percent);
      break;
    }
    case SignalCollectionStatus::scanning: {
      if (collection.consecutive_in_range_ticks <=
              kSignalCollectionAcquireTicks ||
          collection.consecutive_in_range_ticks >=
              kSignalCollectionTotalInRangeTicks ||
          collection.completion_tick) {
        readout.cue = "SCAN ERR ";
        readout.message = " Scan state invalid | progress unavailable ";
        break;
      }
      const auto scan_ticks = collection.consecutive_in_range_ticks -
                              kSignalCollectionAcquireTicks;
      readout.progress_percent = static_cast<unsigned>(
          (scan_ticks * 100U + kSignalCollectionScanTicks / 2U) /
          kSignalCollectionScanTicks);
      readout.cue = std::format("SCAN {:03}%", readout.progress_percent);
      readout.message = std::format(
          " Scanning {:03}% | remain within 1000m ",
          readout.progress_percent);
      break;
    }
    case SignalCollectionStatus::complete:
      if (!collection.target ||
          collection.consecutive_in_range_ticks <
              kSignalCollectionTotalInRangeTicks ||
          !collection.completion_tick) {
        readout.cue = "SCAN ERR ";
        readout.message = " Scan state invalid | progress unavailable ";
        break;
      }
      readout.progress_percent = 100;
      readout.cue = "COLLECTED";
      readout.message = " Signal collected | persistent delta recorded ";
      break;
    case SignalCollectionStatus::aborted:
      if (collection.consecutive_in_range_ticks != 0 ||
          collection.completion_tick) {
        readout.cue = "SCAN ERR ";
        readout.message = " Scan state invalid | progress unavailable ";
        break;
      }
      readout.cue = "SCAN LOST";
      readout.message =
          " Scan lost | re-enter 1000m radius to restart ";
      break;
    default:
      readout.cue = "SCAN ERR ";
      readout.message = " Scan state invalid | progress unavailable ";
      break;
  }
  return readout;
}

auto format_system_navigation(const SystemNavigationSolution& navigation)
    -> SystemNavigationReadout {
  SystemNavigationReadout result;
  result.arrival = "ETA --:--";
  const auto short_name = navigation.display_name.substr(
      0, std::min<std::size_t>(4, navigation.display_name.size()));
  result.target = std::format("TGT {:<4} ", short_name);

  const double bearing_degrees = navigation.bearing_radians *
                                 kRadiansToDegrees;
  const double elevation_degrees = navigation.elevation_radians *
                                   kRadiansToDegrees;
  const auto bearing = rounded_in_range(std::abs(bearing_degrees), 0, 180);
  const auto elevation = rounded_in_range(std::abs(elevation_degrees), 0, 90);
  result.bearing = bearing
                       ? std::format("BRG {}{:03} ",
                                     bearing_degrees < 0.0 ? 'L' : 'R',
                                     *bearing)
                       : "BRG #### ";
  result.elevation = elevation
                         ? std::format("ELV {}{:02}  ",
                                       elevation_degrees < 0.0 ? '-' : '+',
                                       *elevation)
                         : "ELV ###  ";

  const double kilometres = navigation.distance_metres / 1'000.0;
  if (std::isfinite(kilometres) && kilometres >= 0.0 &&
      kilometres < 9'999.5) {
    result.distance = std::format("RNG {:04.0f}k", kilometres);
  } else if (std::isfinite(kilometres) && kilometres >= 0.0 &&
             kilometres < 9'999'500.0) {
    result.distance = std::format("RNG {:04.0f}M", kilometres / 1'000.0);
  } else if (std::isfinite(kilometres) && kilometres >= 0.0 &&
             kilometres < 9'999'500'000.0) {
    result.distance = std::format("RNG {:04.0f}G", kilometres / 1'000'000.0);
  } else {
    result.distance = "RNG #### ";
  }
  const double closing_speed = navigation.closing_speed_metres_per_second;
  const auto rounded_speed =
      rounded_in_range(std::abs(closing_speed), 0, 999);
  if (rounded_speed) {
    result.motion = std::format("CLS {}{:03} ",
                                closing_speed < 0.0 ? '-' : '+',
                                *rounded_speed);
  } else if (std::isfinite(closing_speed) &&
             std::abs(closing_speed) < 999'500.0) {
    result.motion = std::format("CLS {}{:03}k",
                                closing_speed < 0.0 ? '-' : '+',
                                static_cast<long long>(std::lround(
                                    std::abs(closing_speed) / 1'000.0)));
  } else if (std::isfinite(closing_speed) &&
             std::abs(closing_speed) < 9'950'000.0) {
    result.motion = std::format("CLS {:+.1f}M",
                                closing_speed / 1'000'000.0);
  } else {
    result.motion = "CLS #### ";
  }

  constexpr double steering_tolerance_degrees{3.0};
  if (!navigation.in_front ||
      std::abs(bearing_degrees) > steering_tolerance_degrees) {
    result.cue = bearing_degrees < 0.0 ? "TURN LEFT" : "TURN RGHT";
  } else if (std::abs(elevation_degrees) >
             steering_tolerance_degrees) {
    result.cue = elevation_degrees < 0.0 ? "PITCH DN " : "PITCH UP ";
  } else {
    switch (navigation.motion) {
      case SystemTargetMotion::holding: result.cue = "ON TARGET"; break;
      case SystemTargetMotion::closing: result.cue = "CLOSING  "; break;
      case SystemTargetMotion::opening: result.cue = "OPENING  "; break;
    }
  }
  if (result.cue.empty()) result.cue = "NAV ERROR";
  return result;
}

auto format_system_navigation(const SystemNavigationSolution& navigation,
                              const SystemFlightGuidance& guidance)
    -> SystemNavigationReadout {
  auto result = format_system_navigation(navigation);
  result.motion = format_closing_speed(
      guidance.closing_speed_metres_per_second);
  result.arrival = format_arrival(guidance.arrival_estimate_seconds);
  switch (guidance.cue) {
    case SystemFlightCue::hold: result.cue = "HOLD      "; break;
    case SystemFlightCue::closing: result.cue = "CLOSING  "; break;
    case SystemFlightCue::opening: result.cue = "OPENING  "; break;
    case SystemFlightCue::brake: result.cue = "BRAKE NOW"; break;
    case SystemFlightCue::orbit_ready: result.cue = "ORBIT RDY"; break;
  }
  return result;
}

auto format_system_flight_status(const SystemFlightState& state,
                                 const SystemFlightGuidance& guidance)
    -> SystemFlightStatusReadout {
  const auto valid_scale = [](SystemTimeScale scale) {
    return scale == SystemTimeScale::one || scale == SystemTimeScale::four ||
           scale == SystemTimeScale::sixteen;
  };
  const bool valid = valid_mode(state.mode) && valid_scale(state.time_scale) &&
                     std::isfinite(guidance.target_radius_metres) &&
                     guidance.target_radius_metres > 0.0 &&
                     std::isfinite(guidance.distance_metres) &&
                     guidance.distance_metres >= 0.0 &&
                     std::isfinite(guidance.closing_speed_metres_per_second) &&
                     std::isfinite(guidance.relative_speed_metres_per_second) &&
                     guidance.relative_speed_metres_per_second >= 0.0;
  if (!valid) {
    return {.message = " SYSTEM GUIDANCE INVALID ",
            .insertion_refusal = " INSERT BLOCKED | GUIDANCE INVALID ",
            .insertion_ready = false};
  }

  const auto mode =
      state.mode == FlightMode::autopilot ? "AUTO HOLD" : "MANUAL";
  const auto scale = system_time_scale_value(state.time_scale);
  std::string_view cue{"HOLD"};
  switch (guidance.cue) {
    case SystemFlightCue::hold: break;
    case SystemFlightCue::closing: cue = "CLOSING"; break;
    case SystemFlightCue::opening: cue = "OPENING"; break;
    case SystemFlightCue::brake: cue = "BRAKE NOW"; break;
    case SystemFlightCue::orbit_ready: cue = "ORBIT RDY"; break;
  }

  SystemFlightStatusReadout result{
      .message = guidance.orbit_insertion_ready
                     ? std::format(" ORBIT RDY | ENTER INSERT ORBIT | {} | {}x ",
                                   mode, scale)
                     : std::format(
                           " {} | {}x | {} | [] time | SPACE mode | WAIT ORBIT RDY ",
                           mode, scale, cue),
      .insertion_refusal = " INSERT BLOCKED",
      .insertion_ready = guidance.orbit_insertion_ready,
  };
  if (guidance.orbit_insertion_ready) {
    result.insertion_refusal.clear();
    return result;
  }

  const double distance_radii =
      guidance.distance_metres / guidance.target_radius_metres;
  const double altitude_metres =
      guidance.distance_metres - guidance.target_radius_metres;
  if (altitude_metres < kMinimumFlightClearanceMetres) {
    result.insertion_refusal += std::format(
        " | ALT {:.0f}<16m", altitude_metres);
  }
  if (distance_radii > kSystemOrbitInsertionRadiusRadii) {
    result.insertion_refusal +=
        std::format(" | RNG {:.1f}R>3R", distance_radii);
  }
  if (guidance.relative_speed_metres_per_second >
      kSystemOrbitInsertionMaximumSpeed) {
    result.insertion_refusal += std::format(
        " | REL {:.1f}k>4k",
        guidance.relative_speed_metres_per_second / 1'000.0);
  }
  const double radial_speed =
      std::abs(guidance.closing_speed_metres_per_second);
  if (radial_speed > kSystemOrbitInsertionMaximumRadialSpeed) {
    result.insertion_refusal +=
        std::format(" | RAD {:.0f}>250m/s", radial_speed);
  }
  if (result.insertion_refusal == " INSERT BLOCKED") {
    result.insertion_refusal += " | OUTSIDE SAFE CORRIDOR";
  }
  result.insertion_refusal += " ";
  return result;
}

}  // namespace apsis_drift
