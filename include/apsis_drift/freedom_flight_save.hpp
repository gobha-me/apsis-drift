#pragma once

#include "apsis_drift/atmospheric_flight.hpp"
#include "apsis_drift/freedom_save.hpp"
#include "apsis_drift/orbit_hold.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomFlightSaveFormatVersion{18};
struct FreedomFlightModel {
  std::uint32_t physical_catalog{kPhysicalLocalSystemGeneratorVersion};
  std::uint32_t physical_ephemeris{kAnalyticEphemerisVersion};
  std::uint32_t rotation_owner{kPhysicalPlanetRotationOwnerVersion};
  std::uint32_t rotation_generator{kPlanetRotationGeneratorVersion};
  CentralBodyDynamicsRecipe central;
  AtmosphericFlightRecipe atmosphere;
  OrbitHoldRequest hold;
  bool assistance{true};
  friend auto operator==(const FreedomFlightModel&, const FreedomFlightModel&)
      -> bool = default;
};
struct FreedomFlightSaveDocument {
  // Origin identity/history, not a claim that the flying craft is docked.
  // Format18 explicitly replaces the serialized location with planetary_flight.
  FreedomSaveDocument origin;
  RigidBodyState flight;
  FreedomFlightModel model;
  friend auto operator==(const FreedomFlightSaveDocument&,
                         const FreedomFlightSaveDocument&) -> bool = default;
};
struct FreedomFlightHydration {
  PhysicalLocalSystem system;
  PhysicalPlanetRotationRecipe rotation;
  AtmosphericFlightSample atmosphere;
};
// Current saved-flight domain: the generated physical origin home planet in
// its nonrotating frame. No undock/spawn, interbody migration or study
// fallback.
[[nodiscard]] auto hydrate_freedom_flight_document(
    const FreedomFlightSaveDocument&)
    -> std::expected<FreedomFlightHydration, SaveSchemaError>;
[[nodiscard]] auto encode_freedom_flight_document_json(
    const FreedomFlightSaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_freedom_flight_document_json(std::string_view)
    -> std::expected<FreedomFlightSaveDocument, SaveSchemaError>;
} // namespace apsis_drift
