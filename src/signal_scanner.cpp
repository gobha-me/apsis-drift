#include "apsis_drift/signal_scanner.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "apsis_drift/coordinates.hpp"

namespace apsis_drift {
namespace {

[[nodiscard]] auto finite(PlanetFixedPositionMetres value) noexcept -> bool {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] auto valid_catalog(const SurfaceSignalCatalog& catalog) noexcept
    -> bool {
  const auto valid_face = [](CubeFace face) {
    switch (face) {
      case CubeFace::positive_x:
      case CubeFace::negative_x:
      case CubeFace::positive_y:
      case CubeFace::negative_y:
      case CubeFace::positive_z:
      case CubeFace::negative_z: return true;
    }
    return false;
  };
  for (std::size_t index = 0; index < catalog.signals.size(); ++index) {
    const auto& signal = catalog.signals[index];
    constexpr auto tile_count = std::uint32_t{1} << kSurfaceSignalPlacementLod;
    if (signal.ordinal != index ||
        signal.anchor.tile.planet != catalog.planet ||
        signal.anchor.tile.lod != kSurfaceSignalPlacementLod ||
        !valid_face(signal.anchor.tile.face) ||
        signal.anchor.tile.x >= tile_count ||
        signal.anchor.tile.y >= tile_count || !std::isfinite(signal.anchor.u) ||
        !std::isfinite(signal.anchor.v) || signal.anchor.u < 0.0 ||
        signal.anchor.u > 1.0 || signal.anchor.v < 0.0 ||
        signal.anchor.v > 1.0 ||
        signal.strength_basis_points <
            kSurfaceSignalMinimumStrengthBasisPoints ||
        signal.strength_basis_points >
            kSurfaceSignalMaximumStrengthBasisPoints) {
      return false;
    }
    for (std::size_t prior = 0; prior < index; ++prior) {
      if (catalog.signals[prior].id == signal.id) return false;
    }
  }
  return true;
}

[[nodiscard]] auto signal_index(const SurfaceSignalCatalog& catalog,
                                SurfaceSignalId id) noexcept
    -> std::optional<std::size_t> {
  for (std::size_t index = 0; index < catalog.signals.size(); ++index) {
    if (catalog.signals[index].id == id) return index;
  }
  return std::nullopt;
}

[[nodiscard]] auto canonical_angle(double angle) noexcept -> double {
  constexpr double tau = std::numbers::pi_v<double> * 2.0;
  angle = std::fmod(angle + std::numbers::pi_v<double>, tau);
  if (angle < 0.0) angle += tau;
  return angle - std::numbers::pi_v<double>;
}

[[nodiscard]] auto squared_magnitude(PlanetFixedPositionMetres value) noexcept
    -> double {
  return value.x * value.x + value.y * value.y + value.z * value.z;
}

[[nodiscard]] auto occluded_by_reference_sphere(
    const PlanetDescriptor& planet, const SurfaceSignal& signal,
    PlanetFixedPositionMetres craft, PlanetFixedPositionMetres target) noexcept
    -> bool {
  const PlanetFixedPositionMetres segment{
      target.x - craft.x, target.y - craft.y, target.z - craft.z};
  const double length_squared = squared_magnitude(segment);
  if (!std::isfinite(length_squared) || length_squared <= 0.0) return false;
  const double projection =
      -(craft.x * segment.x + craft.y * segment.y + craft.z * segment.z) /
      length_squared;
  if (projection <= 0.0 || projection >= 1.0) return false;
  const PlanetFixedPositionMetres closest{
      craft.x + projection * segment.x,
      craft.y + projection * segment.y,
      craft.z + projection * segment.z,
  };
  const double closest_radius_squared = squared_magnitude(closest);
  const double reference_radius =
      static_cast<double>(planet.radius.value) * 1'000.0 +
      std::min(0.0, static_cast<double>(signal.surface_elevation_metres));
  return closest_radius_squared < reference_radius * reference_radius;
}

} // namespace

auto advance_signal_selection(const SurfaceSignalCatalog& catalog,
                              SignalScannerState& state,
                              SignalSelectionCommand command) noexcept
    -> std::expected<void, SignalScannerError> {
  if (!valid_catalog(catalog)) {
    return std::unexpected{SignalScannerError::invalid_catalog};
  }
  if (command != SignalSelectionCommand::previous &&
      command != SignalSelectionCommand::next) {
    return std::unexpected{SignalScannerError::invalid_command};
  }

  std::size_t selected{};
  if (state.selected) {
    const auto current = signal_index(catalog, *state.selected);
    if (!current) {
      return std::unexpected{SignalScannerError::invalid_selection};
    }
    if (command == SignalSelectionCommand::next) {
      selected = (*current + 1U) % catalog.signals.size();
    } else {
      selected =
          (*current + catalog.signals.size() - 1U) % catalog.signals.size();
    }
  } else if (command == SignalSelectionCommand::previous) {
    selected = catalog.signals.size() - 1U;
  }
  state.selected = catalog.signals[selected].id;
  return {};
}

auto resolve_signal_navigation(const PlanetDescriptor& planet,
                               const SurfaceSignalCatalog& catalog,
                               const PlanetaryFlightState& flight,
                               const SignalScannerState& scanner) noexcept
    -> std::expected<SignalNavigationSolution, SignalScannerError> {
  if (catalog.planet != planet.id) {
    return std::unexpected{SignalScannerError::invalid_planet};
  }
  if (!valid_catalog(catalog)) {
    return std::unexpected{SignalScannerError::invalid_catalog};
  }
  if (flight.planet != planet.id ||
      !std::isfinite(flight.pose.position.latitude_radians) ||
      !std::isfinite(flight.pose.position.longitude_radians) ||
      !std::isfinite(flight.pose.position.altitude_metres) ||
      !std::isfinite(flight.pose.heading_radians)) {
    return std::unexpected{SignalScannerError::invalid_flight_state};
  }
  if (!scanner.selected) return SignalNavigationSolution{};

  const auto index = signal_index(catalog, *scanner.selected);
  if (!index) {
    return std::unexpected{SignalScannerError::invalid_selection};
  }
  const auto& signal = catalog.signals[*index];
  const auto craft = planet_fixed_from_geodetic(planet, flight.pose.position);
  const auto target = planet_fixed_from_terrain_address(
      planet, signal.anchor,
      static_cast<double>(signal.approach_altitude_metres));
  const auto frame = make_local_tangent_frame(planet, flight.pose.position);
  if (!craft || !target || !frame || !finite(*craft) || !finite(*target)) {
    return std::unexpected{SignalScannerError::coordinate_failure};
  }
  const auto local_target = local_from_planet_fixed(*frame, *target);
  if (!local_target) {
    return std::unexpected{SignalScannerError::coordinate_failure};
  }

  const double dx = target->x - craft->x;
  const double dy = target->y - craft->y;
  const double dz = target->z - craft->z;
  const double distance = std::hypot(dx, dy, dz);
  const double horizontal = std::hypot(local_target->east, local_target->north);
  if (!std::isfinite(distance) || !std::isfinite(horizontal)) {
    return std::unexpected{SignalScannerError::coordinate_failure};
  }
  const auto motion = resolve_target_relative_motion(
      planet, flight, *local_target, kSignalScannerReachedRadiusMetres);
  if (!motion) {
    return std::unexpected{SignalScannerError::invalid_flight_state};
  }

  const double absolute = horizontal == 0.0
                              ? canonical_angle(flight.pose.heading_radians)
                              : canonical_angle(std::atan2(local_target->north,
                                                           local_target->east));
  const double relative =
      horizontal == 0.0
          ? 0.0
          : canonical_angle(absolute - flight.pose.heading_radians);
  SignalScannerStatus status{SignalScannerStatus::tracking};
  if (distance <= kSignalScannerReachedRadiusMetres +
                      kSignalScannerDistanceToleranceMetres) {
    status = SignalScannerStatus::reached;
  } else if (distance > kSignalScannerMaximumRangeMetres +
                            kSignalScannerDistanceToleranceMetres) {
    status = SignalScannerStatus::out_of_range;
  } else if (occluded_by_reference_sphere(planet, signal, *craft, *target)) {
    status = SignalScannerStatus::occluded;
  }
  return SignalNavigationSolution{
      .status = status,
      .selected = signal.id,
      .ordinal = signal.ordinal,
      .absolute_bearing_radians = absolute,
      .relative_bearing_radians = relative,
      .distance_metres = distance,
      .motion = *motion,
      .strength_basis_points = signal.strength_basis_points,
  };
}

} // namespace apsis_drift
