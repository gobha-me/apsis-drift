#pragma once

#include <cstddef>
#include <string>

#include "apsis_drift/planetary_flight.hpp"
#include "apsis_drift/render_profile.hpp"
#include "apsis_drift/signal_collection.hpp"
#include "apsis_drift/simulation.hpp"
#include "apsis_drift/system_flight.hpp"
#include "apsis_drift/system_rendering.hpp"
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
  std::string drive;
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

struct SignalScannerReadout {
  std::string target;
  std::string bearing;
  std::string distance;
  std::string motion;
  std::string arrival;
  std::string strength;
  std::string cue;
  SignalScannerStatus status{SignalScannerStatus::no_signal};

  auto operator==(const SignalScannerReadout&) const -> bool = default;
};

struct SignalCollectionReadout {
  std::string cue;
  std::string message;
  SignalCollectionStatus status{SignalCollectionStatus::approach};
  unsigned progress_percent{};

  auto operator==(const SignalCollectionReadout&) const -> bool = default;
};

struct SystemNavigationReadout {
  std::string target;
  std::string bearing;
  std::string elevation;
  std::string distance;
  std::string motion;
  std::string arrival;
  std::string cue;

  auto operator==(const SystemNavigationReadout&) const -> bool = default;
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
[[nodiscard]] auto format_flight_instruments(
    const PlanetaryFlightState& state) -> FlightInstrumentReadout;
[[nodiscard]] auto format_flight_instruments(
    const SystemFlightState& state) -> FlightInstrumentReadout;

// Build cockpit-ready fixed-width regime and most-recent-transition lines.
// Presentation decides when and where to show them; simulation remains the
// sole owner of transition timing.
[[nodiscard]] auto format_flight_regime(
    const PlanetaryFlightState& state) -> FlightRegimeReadout;

// Scanner lines are fixed width and communicate direction and status in text,
// so the Kitty and ANSI cockpit paths do not depend on color alone.
[[nodiscard]] auto format_signal_scanner(
    const SignalNavigationSolution& navigation) -> SignalScannerReadout;

// Collection cues remain fixed width for the instrument rail and provide a
// textual message so progress and failure never depend on color alone.
[[nodiscard]] auto format_signal_collection(
    const SignalCollectionState& collection) -> SignalCollectionReadout;

// Local-system navigation remains textual as well as graphical so selection
// and steering do not depend on color or Kitty graphics.
[[nodiscard]] auto format_system_navigation(
    const SystemNavigationSolution& navigation) -> SystemNavigationReadout;
[[nodiscard]] auto format_system_navigation(
    const SystemNavigationSolution& navigation,
    const SystemFlightGuidance& guidance) -> SystemNavigationReadout;

}  // namespace apsis_drift
