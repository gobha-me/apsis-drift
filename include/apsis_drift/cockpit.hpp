#pragma once

#include <cstddef>
#include <string>

#include "apsis_drift/planetary_flight.hpp"
#include "apsis_drift/render_profile.hpp"
#include "apsis_drift/simulation.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift {

inline constexpr int kMinimumCockpitCols{80};
inline constexpr int kMinimumCockpitRows{24};
inline constexpr std::size_t kInstrumentLineWidth{9};
inline constexpr float kLowClearanceWarning{24.0F};

enum class CockpitAlert { none, low_clearance, invalid_telemetry };

struct FlightInstrumentReadout {
  std::string heading;
  std::string altitude;
  std::string clearance;
  std::string speed;
  std::string mode;
  std::string alert;
  CockpitAlert alert_state{CockpitAlert::none};

  auto operator==(const FlightInstrumentReadout&) const -> bool = default;
};

struct FlightRegimeReadout {
  std::string regime;
  std::string transition;
  bool valid{};

  auto operator==(const FlightRegimeReadout&) const -> bool = default;
};

enum class CockpitLayoutMode { too_small, compact, wide };

struct CockpitLayout {
  CockpitLayoutMode mode{CockpitLayoutMode::too_small};
  termforge::Rect screen{};
  termforge::Rect header{};
  termforge::Rect left_instruments{};
  termforge::Rect viewport_frame{};
  termforge::Rect viewport{};
  termforge::Rect right_instruments{};
  termforge::Rect messages{};
  termforge::Rect status{};

  [[nodiscard]] constexpr auto supported() const noexcept -> bool {
    return mode != CockpitLayoutMode::too_small;
  }

  constexpr auto operator==(const CockpitLayout&) const noexcept
      -> bool = default;
};

// Compute cockpit regions in terminal cells. The driver-provided `cell_pixels`
// keeps the logical pixel viewport's aspect ratio correct on Kitty and ANSI
// half-block paths without moving terminal policy into the game.
[[nodiscard]] auto compute_cockpit_layout(
    int cols, int rows, termforge::Extent cell_pixels,
    ViewportSize viewport) noexcept -> CockpitLayout;

// Build fixed-width cockpit lines exclusively from authoritative simulation
// state. Invalid numeric telemetry remains renderable as explicit sentinels.
[[nodiscard]] auto format_flight_instruments(const FlightState& state)
    -> FlightInstrumentReadout;

// Build cockpit-ready fixed-width regime and most-recent-transition lines.
// Presentation decides when and where to show them; simulation remains the
// sole owner of transition timing.
[[nodiscard]] auto format_flight_regime(
    const PlanetaryFlightState& state) -> FlightRegimeReadout;

}  // namespace apsis_drift
