#pragma once

#include <optional>

#include "apsis_drift/atmospheric_flight.hpp"
#include "apsis_drift/freedom_save.hpp"
#include "apsis_drift/orbit_hold.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomFlightSaveFormatVersion{18};
inline constexpr std::uint32_t kFreedomActiveFlightSaveFormatVersion{26};
inline constexpr std::uint32_t kFreedomActiveWorldVersion{1};
// Explicit new owner selection. Absence retains the strict historical home
// frame contract; a foreign frame can never opt itself into another world.
struct FreedomActiveWorldSelection {
  std::uint32_t version{kFreedomActiveWorldVersion};
  SystemId system;
  PlanetId planet;
  friend auto operator==(const FreedomActiveWorldSelection&,
                         const FreedomActiveWorldSelection&) -> bool = default;
};
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
  std::optional<FreedomActiveWorldSelection> world{};
  friend auto operator==(const FreedomFlightSaveDocument&,
                         const FreedomFlightSaveDocument&) -> bool = default;
};
struct FreedomFlightHydration {
  PhysicalLocalSystem system;
  PhysicalPlanetRotationRecipe rotation;
  AtmosphericFlightSample atmosphere;
};
// Format 18 retains the physical origin home frame. Explicit format 26
// selects a real planet frame in the bounded origin/neighbor pair; no study
// fallback or implicit migration.
[[nodiscard]] auto hydrate_freedom_flight_document(
    const FreedomFlightSaveDocument&)
    -> std::expected<FreedomFlightHydration, SaveSchemaError>;
[[nodiscard]] auto encode_freedom_flight_document_json(
    const FreedomFlightSaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_freedom_flight_document_json(std::string_view)
    -> std::expected<FreedomFlightSaveDocument, SaveSchemaError>;
} // namespace apsis_drift
