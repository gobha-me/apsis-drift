#include "apsis_drift/coordinates.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace apsis_drift {
namespace {

struct FaceBasis {
  PlanetFixedDirection normal;
  PlanetFixedDirection u;
  PlanetFixedDirection v;
};

[[nodiscard]] auto finite(double value) noexcept -> bool {
  return std::isfinite(value);
}

[[nodiscard]] auto finite(PlanetFixedPositionMetres value) noexcept -> bool {
  return finite(value.x) && finite(value.y) && finite(value.z);
}

[[nodiscard]] auto finite(PlanetFixedDirection value) noexcept -> bool {
  return finite(value.x) && finite(value.y) && finite(value.z);
}

[[nodiscard]] auto finite(LocalPositionMetres value) noexcept -> bool {
  return finite(value.east) && finite(value.north) && finite(value.up);
}

[[nodiscard]] auto radius_metres(const PlanetDescriptor& planet) noexcept
    -> std::expected<double, CoordinateError> {
  if (planet.radius.value < PlanetRadiusKm::min ||
      planet.radius.value > PlanetRadiusKm::max) {
    return std::unexpected{CoordinateError::invalid_planet_radius};
  }
  return static_cast<double>(planet.radius.value) * 1'000.0;
}

[[nodiscard]] auto dot(PlanetFixedDirection left,
                       PlanetFixedDirection right) noexcept -> double {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] auto cross(PlanetFixedDirection left,
                         PlanetFixedDirection right) noexcept
    -> PlanetFixedDirection {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

[[nodiscard]] auto length(PlanetFixedDirection value) noexcept -> double {
  return std::hypot(value.x, value.y, value.z);
}

[[nodiscard]] auto normalized(PlanetFixedDirection value) noexcept
    -> PlanetFixedDirection {
  const auto magnitude = length(value);
  return {value.x / magnitude, value.y / magnitude, value.z / magnitude};
}

[[nodiscard]] auto direction(PlanetFixedPositionMetres value) noexcept
    -> PlanetFixedDirection {
  return {value.x, value.y, value.z};
}

[[nodiscard]] auto valid_unit_direction(PlanetFixedDirection value) noexcept
    -> bool {
  if (!finite(value)) return false;
  constexpr double tolerance{1.0e-12};
  return std::abs(length(value) - 1.0) <= tolerance;
}

[[nodiscard]] auto valid_local_frame(const LocalTangentFrame& frame) noexcept
    -> bool {
  if (!finite(frame.origin) || !valid_unit_direction(frame.east) ||
      !valid_unit_direction(frame.north) || !valid_unit_direction(frame.up)) {
    return false;
  }
  constexpr double tolerance{1.0e-12};
  return std::abs(dot(frame.east, frame.north)) <= tolerance &&
         std::abs(dot(frame.east, frame.up)) <= tolerance &&
         std::abs(dot(frame.north, frame.up)) <= tolerance &&
         dot(cross(frame.east, frame.north), frame.up) >= 1.0 - tolerance;
}

[[nodiscard]] auto normalized_longitude(double longitude) noexcept -> double {
  constexpr double two_pi{2.0 * std::numbers::pi_v<double>};
  auto result = std::fmod(longitude + std::numbers::pi_v<double>, two_pi);
  if (result < 0.0) result += two_pi;
  return result - std::numbers::pi_v<double>;
}

[[nodiscard]] auto valid_face(CubeFace face) noexcept -> bool {
  switch (face) {
    case CubeFace::positive_x:
    case CubeFace::negative_x:
    case CubeFace::positive_y:
    case CubeFace::negative_y:
    case CubeFace::positive_z:
    case CubeFace::negative_z:
      return true;
  }
  return false;
}

[[nodiscard]] auto face_basis(CubeFace face) noexcept -> FaceBasis {
  switch (face) {
    case CubeFace::positive_x:
      return {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    case CubeFace::negative_x:
      return {{-1.0, 0.0, 0.0}, {0.0, -1.0, 0.0}, {0.0, 0.0, 1.0}};
    case CubeFace::positive_y:
      return {{0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};
    case CubeFace::negative_y:
      return {{0.0, -1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};
    case CubeFace::positive_z:
      return {{0.0, 0.0, 1.0}, {0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0}};
    case CubeFace::negative_z:
      return {{0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}};
  }
  return {};
}

[[nodiscard]] auto canonical_face(PlanetFixedDirection value) noexcept
    -> CubeFace {
  const auto x = std::abs(value.x);
  const auto y = std::abs(value.y);
  const auto z = std::abs(value.z);
  if (x >= y && x >= z) {
    return value.x >= 0.0 ? CubeFace::positive_x : CubeFace::negative_x;
  }
  if (y >= z) {
    return value.y >= 0.0 ? CubeFace::positive_y : CubeFace::negative_y;
  }
  return value.z >= 0.0 ? CubeFace::positive_z : CubeFace::negative_z;
}

[[nodiscard]] auto tiles_per_axis(std::uint8_t lod) noexcept -> std::uint32_t {
  return std::uint32_t{1} << lod;
}

struct TileAxisAddress {
  std::uint32_t index{};
  double within{};
};

[[nodiscard]] auto tile_axis_address(double unit, std::uint32_t count) noexcept
    -> TileAxisAddress {
  unit = std::clamp(unit, 0.0, 1.0);
  if (unit == 1.0) return {count - 1U, 1.0};
  const auto scaled = unit * static_cast<double>(count);
  const auto index = static_cast<std::uint32_t>(std::floor(scaled));
  return {index, scaled - static_cast<double>(index)};
}

[[nodiscard]] auto valid_address(const TerrainTileAddress& address) noexcept
    -> std::expected<void, CoordinateError> {
  if (!valid_face(address.tile.face)) {
    return std::unexpected{CoordinateError::invalid_cube_face};
  }
  if (address.tile.lod > kMaxTerrainLod) {
    return std::unexpected{CoordinateError::invalid_lod};
  }
  const auto count = tiles_per_axis(address.tile.lod);
  if (address.tile.x >= count || address.tile.y >= count) {
    return std::unexpected{CoordinateError::invalid_tile_index};
  }
  if (!finite(address.u) || !finite(address.v)) {
    return std::unexpected{CoordinateError::non_finite_input};
  }
  if (address.u < 0.0 || address.u > 1.0 || address.v < 0.0 ||
      address.v > 1.0) {
    return std::unexpected{CoordinateError::invalid_tile_coordinate};
  }
  return {};
}

}  // namespace

auto planet_fixed_from_geodetic(const PlanetDescriptor& planet,
                                GeodeticPosition position) noexcept
    -> std::expected<PlanetFixedPositionMetres, CoordinateError> {
  const auto radius = radius_metres(planet);
  if (!radius) return std::unexpected{radius.error()};
  if (!finite(position.latitude_radians) ||
      !finite(position.longitude_radians) ||
      !finite(position.altitude_metres)) {
    return std::unexpected{CoordinateError::non_finite_input};
  }
  constexpr double half_pi{std::numbers::pi_v<double> / 2.0};
  if (position.latitude_radians < -half_pi ||
      position.latitude_radians > half_pi) {
    return std::unexpected{CoordinateError::invalid_latitude};
  }
  if (position.altitude_metres <= -*radius) {
    return std::unexpected{CoordinateError::invalid_altitude};
  }

  const auto longitude = normalized_longitude(position.longitude_radians);
  const auto radial_distance = *radius + position.altitude_metres;
  if (std::abs(position.latitude_radians) == half_pi) {
    return PlanetFixedPositionMetres{
        0.0, 0.0, std::copysign(radial_distance, position.latitude_radians)};
  }
  const auto latitude_cosine = std::cos(position.latitude_radians);
  const PlanetFixedPositionMetres result{
      radial_distance * latitude_cosine * std::cos(longitude),
      radial_distance * latitude_cosine * std::sin(longitude),
      radial_distance * std::sin(position.latitude_radians),
  };
  if (!finite(result)) {
    return std::unexpected{CoordinateError::non_finite_input};
  }
  return result;
}

auto geodetic_from_planet_fixed(const PlanetDescriptor& planet,
                                PlanetFixedPositionMetres position) noexcept
    -> std::expected<GeodeticPosition, CoordinateError> {
  const auto radius = radius_metres(planet);
  if (!radius) return std::unexpected{radius.error()};
  if (!finite(position)) {
    return std::unexpected{CoordinateError::non_finite_input};
  }
  const auto radial_distance = std::hypot(position.x, position.y, position.z);
  if (!finite(radial_distance)) {
    return std::unexpected{CoordinateError::non_finite_input};
  }
  if (radial_distance == 0.0) {
    return std::unexpected{CoordinateError::planet_center};
  }
  const auto horizontal_distance = std::hypot(position.x, position.y);
  const auto latitude = std::atan2(position.z, horizontal_distance);
  const auto longitude =
      horizontal_distance == 0.0
          ? 0.0
          : normalized_longitude(std::atan2(position.y, position.x));
  return GeodeticPosition{latitude, longitude, radial_distance - *radius};
}

auto make_local_tangent_frame(const PlanetDescriptor& planet,
                              GeodeticPosition origin) noexcept
    -> std::expected<LocalTangentFrame, CoordinateError> {
  const auto fixed_origin = planet_fixed_from_geodetic(planet, origin);
  if (!fixed_origin) return std::unexpected{fixed_origin.error()};
  const auto canonical_origin =
      geodetic_from_planet_fixed(planet, *fixed_origin);
  if (!canonical_origin) return std::unexpected{canonical_origin.error()};

  const auto latitude = canonical_origin->latitude_radians;
  const auto longitude = canonical_origin->longitude_radians;
  constexpr double half_pi{std::numbers::pi_v<double> / 2.0};
  if (latitude == half_pi) {
    return LocalTangentFrame{
        *fixed_origin, {0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};
  }
  if (latitude == -half_pi) {
    return LocalTangentFrame{
        *fixed_origin, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, -1.0}};
  }
  const auto latitude_sine = std::sin(latitude);
  const auto latitude_cosine = std::cos(latitude);
  const auto longitude_sine = std::sin(longitude);
  const auto longitude_cosine = std::cos(longitude);
  return LocalTangentFrame{
      *fixed_origin,
      {-longitude_sine, longitude_cosine, 0.0},
      {-latitude_sine * longitude_cosine, -latitude_sine * longitude_sine,
       latitude_cosine},
      {latitude_cosine * longitude_cosine, latitude_cosine * longitude_sine,
       latitude_sine},
  };
}

auto local_from_planet_fixed(const LocalTangentFrame& frame,
                             PlanetFixedPositionMetres position) noexcept
    -> std::expected<LocalPositionMetres, CoordinateError> {
  if (!valid_local_frame(frame)) {
    return std::unexpected{CoordinateError::invalid_local_frame};
  }
  if (!finite(position)) {
    return std::unexpected{CoordinateError::non_finite_input};
  }
  const PlanetFixedDirection offset{position.x - frame.origin.x,
                                    position.y - frame.origin.y,
                                    position.z - frame.origin.z};
  const LocalPositionMetres result{dot(offset, frame.east),
                                   dot(offset, frame.north),
                                   dot(offset, frame.up)};
  if (!finite(result)) {
    return std::unexpected{CoordinateError::non_finite_input};
  }
  return result;
}

auto planet_fixed_from_local(const LocalTangentFrame& frame,
                             LocalPositionMetres position) noexcept
    -> std::expected<PlanetFixedPositionMetres, CoordinateError> {
  if (!valid_local_frame(frame)) {
    return std::unexpected{CoordinateError::invalid_local_frame};
  }
  if (!finite(position)) {
    return std::unexpected{CoordinateError::non_finite_input};
  }
  const PlanetFixedPositionMetres result{
      frame.origin.x + position.east * frame.east.x +
          position.north * frame.north.x + position.up * frame.up.x,
      frame.origin.y + position.east * frame.east.y +
          position.north * frame.north.y + position.up * frame.up.y,
      frame.origin.z + position.east * frame.east.z +
          position.north * frame.north.z + position.up * frame.up.z,
  };
  if (!finite(result)) {
    return std::unexpected{CoordinateError::non_finite_input};
  }
  return result;
}

auto terrain_address_from_planet_fixed(const PlanetDescriptor& planet,
                                       PlanetFixedPositionMetres position,
                                       std::uint8_t lod) noexcept
    -> std::expected<TerrainTileAddress, CoordinateError> {
  const auto radius = radius_metres(planet);
  if (!radius) return std::unexpected{radius.error()};
  if (!finite(position)) {
    return std::unexpected{CoordinateError::non_finite_input};
  }
  if (lod > kMaxTerrainLod) {
    return std::unexpected{CoordinateError::invalid_lod};
  }
  const auto magnitude = std::hypot(position.x, position.y, position.z);
  if (!finite(magnitude)) {
    return std::unexpected{CoordinateError::non_finite_input};
  }
  if (magnitude == 0.0) {
    return std::unexpected{CoordinateError::planet_center};
  }
  const auto unit = normalized(direction(position));
  const auto face = canonical_face(unit);
  const auto basis = face_basis(face);
  const auto denominator = dot(unit, basis.normal);
  const auto face_u = std::clamp(dot(unit, basis.u) / denominator, -1.0, 1.0);
  const auto face_v = std::clamp(dot(unit, basis.v) / denominator, -1.0, 1.0);
  const auto count = tiles_per_axis(lod);
  const auto x = tile_axis_address((face_u + 1.0) / 2.0, count);
  const auto y = tile_axis_address((face_v + 1.0) / 2.0, count);
  return TerrainTileAddress{
      {planet.id, face, lod, x.index, y.index}, x.within, y.within};
}

auto terrain_address_from_planet_direction(
    const PlanetDescriptor& planet, PlanetFixedDirection direction,
    std::uint8_t lod) noexcept
    -> std::expected<TerrainTileAddress, CoordinateError> {
  const auto radius = radius_metres(planet);
  if (!radius) return std::unexpected{radius.error()};
  if (!finite(direction)) {
    return std::unexpected{CoordinateError::non_finite_input};
  }
  if (lod > kMaxTerrainLod) {
    return std::unexpected{CoordinateError::invalid_lod};
  }
  if (direction.x == 0.0 && direction.y == 0.0 && direction.z == 0.0) {
    return std::unexpected{CoordinateError::planet_center};
  }

  const auto face = canonical_face(direction);
  const auto basis = face_basis(face);
  const auto denominator = dot(direction, basis.normal);
  const auto face_u =
      std::clamp(dot(direction, basis.u) / denominator, -1.0, 1.0);
  const auto face_v =
      std::clamp(dot(direction, basis.v) / denominator, -1.0, 1.0);
  const auto count = tiles_per_axis(lod);
  const auto x = tile_axis_address((face_u + 1.0) / 2.0, count);
  const auto y = tile_axis_address((face_v + 1.0) / 2.0, count);
  return TerrainTileAddress{
      {planet.id, face, lod, x.index, y.index}, x.within, y.within};
}

auto planet_fixed_from_terrain_address(const PlanetDescriptor& planet,
                                       const TerrainTileAddress& address,
                                       double altitude_metres) noexcept
    -> std::expected<PlanetFixedPositionMetres, CoordinateError> {
  const auto radius = radius_metres(planet);
  if (!radius) return std::unexpected{radius.error()};
  if (!finite(altitude_metres)) {
    return std::unexpected{CoordinateError::non_finite_input};
  }
  if (altitude_metres <= -*radius) {
    return std::unexpected{CoordinateError::invalid_altitude};
  }
  const auto address_valid = valid_address(address);
  if (!address_valid) return std::unexpected{address_valid.error()};
  if (address.tile.planet != planet.id) {
    return std::unexpected{CoordinateError::wrong_planet};
  }

  const auto count = static_cast<double>(tiles_per_axis(address.tile.lod));
  const auto face_u =
      2.0 * (static_cast<double>(address.tile.x) + address.u) / count - 1.0;
  const auto face_v =
      2.0 * (static_cast<double>(address.tile.y) + address.v) / count - 1.0;
  const auto basis = face_basis(address.tile.face);
  const auto unit =
      normalized({basis.normal.x + face_u * basis.u.x + face_v * basis.v.x,
                  basis.normal.y + face_u * basis.u.y + face_v * basis.v.y,
                  basis.normal.z + face_u * basis.u.z + face_v * basis.v.z});
  const auto radial_distance = *radius + altitude_metres;
  if (!finite(radial_distance)) {
    return std::unexpected{CoordinateError::non_finite_input};
  }
  const PlanetFixedPositionMetres result{radial_distance * unit.x,
                                         radial_distance * unit.y,
                                         radial_distance * unit.z};
  if (!finite(result)) {
    return std::unexpected{CoordinateError::non_finite_input};
  }
  return result;
}

auto nominal_terrain_tile_span_metres(const PlanetDescriptor& planet,
                                      std::uint8_t lod) noexcept
    -> std::expected<double, CoordinateError> {
  const auto radius = radius_metres(planet);
  if (!radius) return std::unexpected{radius.error()};
  if (lod > kMaxTerrainLod) {
    return std::unexpected{CoordinateError::invalid_lod};
  }
  return std::numbers::pi_v<double> * *radius /
         (2.0 * static_cast<double>(tiles_per_axis(lod)));
}

auto select_terrain_lod(const PlanetDescriptor& planet,
                        double altitude_metres) noexcept
    -> std::expected<std::uint8_t, CoordinateError> {
  if (!finite(altitude_metres)) {
    return std::unexpected{CoordinateError::non_finite_input};
  }
  if (altitude_metres < 0.0) {
    return std::unexpected{CoordinateError::invalid_altitude};
  }
  const auto span = nominal_terrain_tile_span_metres(planet, 0);
  if (!span) return std::unexpected{span.error()};
  const auto target = kLodTileSpanMultiplier *
                      std::max(altitude_metres, kMinimumLodAltitudeMetres);
  auto selected = std::uint8_t{0};
  auto selected_span = *span;
  while (selected < kMaxTerrainLod && selected_span > target) {
    ++selected;
    selected_span *= 0.5;
  }
  return selected;
}

}  // namespace apsis_drift
