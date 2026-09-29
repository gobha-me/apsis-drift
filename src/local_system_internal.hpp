#pragma once

#include "apsis_drift/local_system.hpp"

namespace apsis_drift::detail {
// Internal geometry only: public callers must validate catalog ownership,
// recipe, planet membership and time first. Never a descriptor bypass API.
[[nodiscard]] auto resolve_validated_circular_orbit(const PlanetOrbit& orbit,
                                                    EphemerisQueryTime time)
    -> std::expected<PlanetEphemeris, LocalSystemError>;
[[nodiscard]] auto resolve_validated_station_orbit(
    const OriginStationDescriptor& station, const PlanetEphemeris& host,
    EphemerisQueryTime time)
    -> std::expected<OriginStationEphemeris, LocalSystemError>;
} // namespace apsis_drift::detail
