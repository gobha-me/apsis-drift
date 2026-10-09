#pragma once

#include "apsis_drift/terrain_touchdown.hpp"

namespace apsis_drift::detail {
// Arithmetic test seam only. Production admits patches exclusively from its
// validated snapshot. This function does not authenticate caller-owned terrain.
[[nodiscard]] auto terrain_pad_observation(const PlanetDescriptor&,
                                           const godot_spike::ContactPatch&)
    -> std::expected<TouchdownPadObservation, TerrainSupportError>;
} // namespace apsis_drift::detail
