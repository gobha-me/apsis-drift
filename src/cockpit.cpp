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
  readout.speed = format_three_digit(
      "SPD", std::hypot(static_cast<double>(state.velocity.x),
                        static_cast<double>(state.velocity.y)));
  readout.mode = state.mode == FlightMode::autopilot ? "MODE AUTO"
                                                      : "MODE MAN ";
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
  readout.speed = format_three_digit(
      "SPD", std::hypot(state.velocity.east_metres_per_second,
                        state.velocity.north_metres_per_second));
  readout.mode = state.mode == FlightMode::autopilot ? "MODE AUTO"
                                                      : "MODE MAN ";
  if (state.clearance_metres <= kLowClearanceWarning) {
    readout.alert = "! LOW CLR";
    readout.alert_state = CockpitAlert::low_clearance;
  } else {
    readout.alert = std::string(kInstrumentLineWidth, ' ');
  }
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

}  // namespace apsis_drift
