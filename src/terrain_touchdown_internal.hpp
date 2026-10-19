#pragma once

#include "apsis_drift/terrain_touchdown.hpp"
#include "godot/contact_geometry_internal.hpp"

namespace apsis_drift::detail {
[[nodiscard]] auto certify_contact_first_surface(
    const std::array<PlanetFixedPositionMetres, 3>& vertices,
    const godot_spike::detail::NominalContactRectangle& rectangle,
    PlanetFixedDirection outward_normal)
    -> std::expected<void, TerrainSupportError>;
// Arithmetic test seam only. Production admits patches exclusively from its
// validated snapshot. This function does not authenticate caller-owned terrain.
[[nodiscard]] auto terrain_pad_observation(const PlanetDescriptor&,
                                           const godot_spike::ContactPatch&)
    -> std::expected<TouchdownPadObservation, TerrainSupportError>;
} // namespace apsis_drift::detail
