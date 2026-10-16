#pragma once

#include "apsis_drift/body_ephemeris.hpp"

namespace apsis_drift {
// Fixed bounded recipe: one moon around the first planet, one minor body
// outside all planetary orbital shells. Circular geometry is not an N-body
// stability, atmosphere, approach, collision or landing certificate.
[[nodiscard]] auto generate_secondary_body_catalog(
    const PhysicalLocalSystem&,
    std::uint32_t version = kSecondaryBodyCatalogVersion)
    -> std::expected<SecondaryBodyCatalog, BodyEphemerisError>;
[[nodiscard]] auto validate_secondary_body_catalog(const SecondaryBodyCatalog&)
    -> std::expected<void, BodyEphemerisError>;
} // namespace apsis_drift
