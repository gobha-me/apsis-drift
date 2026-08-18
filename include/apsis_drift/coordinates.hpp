#pragma once

#include <cstdint>
#include <expected>

#include "apsis_drift/planet.hpp"

namespace apsis_drift {

// Authoritative spatial calculations use double-precision metres and radians.
// System space is right-handed and inertial. Planet-fixed space is centered on
// one planet with +z at spin north, +x at zero longitude, and +y at 90 degrees
// east. Analytic ephemeris owns generated planet-center position and velocity;
// the system-flight handoff adds the complete planet-fixed orientation.
struct SystemPositionMetres {
  double x{};
  double y{};
  double z{};

  friend auto operator==(const SystemPositionMetres&,
                         const SystemPositionMetres&) -> bool = default;
};

struct SystemVelocityMetresPerSecond {
  double x{};
  double y{};
  double z{};

  friend auto operator==(const SystemVelocityMetresPerSecond&,
                         const SystemVelocityMetresPerSecond&)
      -> bool = default;
};

struct SystemDirection {
  double x{};
  double y{};
  double z{};

  friend auto operator==(const SystemDirection&, const SystemDirection&)
      -> bool = default;
};

struct PlanetFixedPositionMetres {
  double x{};
  double y{};
  double z{};

  friend auto operator==(const PlanetFixedPositionMetres&,
                         const PlanetFixedPositionMetres&) -> bool = default;
};

struct GeodeticPosition {
  double latitude_radians{};
  double longitude_radians{};
  double altitude_metres{};

  friend auto operator==(const GeodeticPosition&, const GeodeticPosition&)
      -> bool = default;
};

// Local flight space is an east/north/up tangent frame: +x east, +y north,
// and +z away from the planet center.
struct LocalPositionMetres {
  double east{};
  double north{};
  double up{};

  friend auto operator==(const LocalPositionMetres&, const LocalPositionMetres&)
      -> bool = default;
};

struct PlanetFixedDirection {
  double x{};
  double y{};
  double z{};

  friend auto operator==(const PlanetFixedDirection&,
                         const PlanetFixedDirection&) -> bool = default;
};

struct LocalTangentFrame {
  PlanetFixedPositionMetres origin;
  PlanetFixedDirection east;
  PlanetFixedDirection north;
  PlanetFixedDirection up;

  friend auto operator==(const LocalTangentFrame&, const LocalTangentFrame&)
      -> bool = default;
};

enum class CubeFace : std::uint8_t {
  positive_x,
  negative_x,
  positive_y,
  negative_y,
  positive_z,
  negative_z,
};

inline constexpr std::uint8_t kMaxTerrainLod{16};
inline constexpr double kMinimumLodAltitudeMetres{32.0};
inline constexpr double kLodTileSpanMultiplier{8.0};

struct TerrainTileKey {
  PlanetId planet;
  CubeFace face{};
  std::uint8_t lod{};
  std::uint32_t x{};
  std::uint32_t y{};

  friend auto operator==(const TerrainTileKey&, const TerrainTileKey&)
      -> bool = default;
};

struct TerrainTileAddress {
  TerrainTileKey tile;
  double u{};
  double v{};

  friend auto operator==(const TerrainTileAddress&, const TerrainTileAddress&)
      -> bool = default;
};

enum class CoordinateError : std::uint8_t {
  non_finite_input,
  invalid_planet_radius,
  invalid_latitude,
  invalid_altitude,
  planet_center,
  invalid_local_frame,
  invalid_cube_face,
  invalid_lod,
  wrong_planet,
  invalid_tile_index,
  invalid_tile_coordinate,
};

[[nodiscard]] auto planet_fixed_from_geodetic(
    const PlanetDescriptor& planet, GeodeticPosition position) noexcept
    -> std::expected<PlanetFixedPositionMetres, CoordinateError>;

[[nodiscard]] auto geodetic_from_planet_fixed(
    const PlanetDescriptor& planet,
    PlanetFixedPositionMetres position) noexcept
    -> std::expected<GeodeticPosition, CoordinateError>;

[[nodiscard]] auto make_local_tangent_frame(const PlanetDescriptor& planet,
                                            GeodeticPosition origin) noexcept
    -> std::expected<LocalTangentFrame, CoordinateError>;

[[nodiscard]] auto local_from_planet_fixed(
    const LocalTangentFrame& frame,
    PlanetFixedPositionMetres position) noexcept
    -> std::expected<LocalPositionMetres, CoordinateError>;

[[nodiscard]] auto planet_fixed_from_local(
    const LocalTangentFrame& frame, LocalPositionMetres position) noexcept
    -> std::expected<PlanetFixedPositionMetres, CoordinateError>;

[[nodiscard]] auto terrain_address_from_planet_fixed(
    const PlanetDescriptor& planet, PlanetFixedPositionMetres position,
    std::uint8_t lod) noexcept
    -> std::expected<TerrainTileAddress, CoordinateError>;

// Resolves an already available surface direction without measuring or
// normalizing it. Cube-face coordinates are scale invariant, so callers such
// as renderers can reuse a direction they have already computed.
[[nodiscard]] auto terrain_address_from_planet_direction(
    const PlanetDescriptor& planet, PlanetFixedDirection direction,
    std::uint8_t lod) noexcept
    -> std::expected<TerrainTileAddress, CoordinateError>;

[[nodiscard]] auto planet_fixed_from_terrain_address(
    const PlanetDescriptor& planet, const TerrainTileAddress& address,
    double altitude_metres = 0.0) noexcept
    -> std::expected<PlanetFixedPositionMetres, CoordinateError>;

[[nodiscard]] auto nominal_terrain_tile_span_metres(
    const PlanetDescriptor& planet, std::uint8_t lod) noexcept
    -> std::expected<double, CoordinateError>;

[[nodiscard]] auto select_terrain_lod(const PlanetDescriptor& planet,
                                      double altitude_metres) noexcept
    -> std::expected<std::uint8_t, CoordinateError>;

}  // namespace apsis_drift
