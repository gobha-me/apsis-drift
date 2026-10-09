#pragma once

#include "apsis_drift/freedom_knowledge.hpp"
#include "apsis_drift/freedom_surface_save.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomLocalObservationPolicy{1};
inline constexpr SimulationTick kFreedomLocalObservationInterval{120};
enum class LocalObservationEvent : std::uint8_t {
  sensor_tick,
  touchdown,
  port_capture,
  system_arrival
};
// Sensor model 1 uses authoritative position and integer clock, never a camera.
// Committed events require the actual validated constraint; reads/load do not
// invoke this writer. No provider-local state or unsaved dwell counter exists.
[[nodiscard]] auto observe_freedom_local_ship(
    const FreedomKnowledge&, const FreedomSurfaceSaveDocument&,
    LocalObservationEvent = LocalObservationEvent::sensor_tick)
    -> std::expected<FreedomKnowledge, FreedomKnowledgeError>;
} // namespace apsis_drift
