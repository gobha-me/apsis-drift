#pragma once

#include <cstdint>
#include <expected>

#include "apsis_drift/surface_signals.hpp"

namespace apsis_drift::detail {

struct SurfaceSignalPlacementLimits {
  std::uint16_t attempts{kSurfaceSignalPlacementAttempts};
  std::int32_t maximum_relief_metres{kSurfaceSignalMaximumReliefMetres};
};

[[nodiscard]] auto generate_surface_signals_with_limits(
    const PlanetDescriptor& planet, TerrainTileCache& cache,
    SurfaceSignalPlacementLimits limits)
    -> std::expected<SurfaceSignalCatalog, SurfaceSignalError>;

}  // namespace apsis_drift::detail
