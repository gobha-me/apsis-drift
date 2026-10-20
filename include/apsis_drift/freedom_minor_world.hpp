#pragma once
#include "apsis_drift/freedom_save.hpp"
#include "apsis_drift/minor_body_dynamics.hpp"
#include "apsis_drift/physical_minor_rotation.hpp"
namespace apsis_drift {
inline constexpr std::uint32_t kFreedomMinorFlightSaveFormatVersion{46};
inline constexpr std::uint32_t kFreedomMinorWorldVersion{1};
struct FreedomMinorWorldSelection {
  std::uint32_t version{kFreedomMinorWorldVersion};
  BodyTarget target;
  std::uint32_t physical_recipe{kPhysicalMinorRecipeVersion};
  std::uint32_t rotation_recipe{kPhysicalMinorRotationVersion};
  std::uint32_t dynamics_recipe{kMinorBodyDynamicsVersion};
  friend auto operator==(const FreedomMinorWorldSelection&,
                         const FreedomMinorWorldSelection&) -> bool = default;
};
struct FreedomMinorFlightHydration {
  PhysicalMinorDescriptor minor;
  PhysicalMinorRotationGeometry rotation;
  MinorBodyGravity gravity;
};
struct FreedomFlightSaveDocument;
[[nodiscard]] auto hydrate_freedom_minor_flight_document(
    const FreedomFlightSaveDocument&)
    -> std::expected<FreedomMinorFlightHydration, SaveSchemaError>;
} // namespace apsis_drift
