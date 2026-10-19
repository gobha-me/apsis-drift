#pragma once
#include <cstdint>
namespace apsis_drift {
enum class TerrainSupportError : std::uint8_t {
  unresolved_geometry,
  invalid_geometry,
  first_surface_not_certified
};
} // namespace apsis_drift
