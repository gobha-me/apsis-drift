#pragma once

#include <cstdint>
#include <expected>
#include <optional>

#include "apsis_drift/planetary_flight.hpp"
#include "apsis_drift/surface_signals.hpp"

namespace apsis_drift {

inline constexpr double kSignalScannerMaximumRangeMetres{2'000'000.0};
inline constexpr double kSignalScannerReachedRadiusMetres{1'000.0};
inline constexpr double kSignalScannerDistanceToleranceMetres{1.0e-6};

enum class SignalSelectionCommand : std::uint8_t {
  previous,
  next,
};

enum class SignalScannerStatus : std::uint8_t {
  no_signal,
  tracking,
  out_of_range,
  occluded,
  reached,
};

struct SignalScannerState {
  std::optional<SurfaceSignalId> selected;

  friend auto operator==(const SignalScannerState&,
                         const SignalScannerState&) -> bool = default;
};

struct SignalNavigationSolution {
  SignalScannerStatus status{SignalScannerStatus::no_signal};
  std::optional<SurfaceSignalId> selected;
  std::uint32_t ordinal{};
  double absolute_bearing_radians{};
  double relative_bearing_radians{};
  double distance_metres{};
  TargetRelativeMotion motion;
  std::uint16_t strength_basis_points{};

  friend auto operator==(const SignalNavigationSolution&,
                         const SignalNavigationSolution&) -> bool = default;
};

enum class SignalScannerError : std::uint8_t {
  invalid_planet,
  invalid_catalog,
  invalid_flight_state,
  invalid_selection,
  invalid_command,
  coordinate_failure,
};

// Selection follows the catalog's compatibility-ordered signal ordinals.
// Rejected commands leave state untouched.
[[nodiscard]] auto advance_signal_selection(
    const SurfaceSignalCatalog& catalog, SignalScannerState& state,
    SignalSelectionCommand command) noexcept
    -> std::expected<void, SignalScannerError>;

// Navigation is derived from authoritative planetary flight state and the
// immutable generated signal approach point. It never mutates generated or
// discovery state.
[[nodiscard]] auto resolve_signal_navigation(
    const PlanetDescriptor& planet, const SurfaceSignalCatalog& catalog,
    const PlanetaryFlightState& flight,
    const SignalScannerState& scanner) noexcept
    -> std::expected<SignalNavigationSolution, SignalScannerError>;

}  // namespace apsis_drift
