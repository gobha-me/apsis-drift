#pragma once

#include <optional>

#include "apsis_drift/atmospheric_flight.hpp"
#include "apsis_drift/freedom_moon_world.hpp"
#include "apsis_drift/freedom_save.hpp"
#include "apsis_drift/orbit_hold.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomFlightSaveFormatVersion{18};
inline constexpr std::uint32_t kFreedomActiveFlightSaveFormatVersion{26};
inline constexpr std::uint32_t kFreedomTerrainFlightSaveFormatVersion{31};
// Top-level input only; never emitted as a canonical or nested save.
inline constexpr std::uint32_t kFreedomSystemFlightImportFormatVersion{35};
inline constexpr std::uint32_t kFreedomReleaseFlightSaveFormatVersion{36};
inline constexpr std::uint32_t kFreedomOpticalFlightSaveFormatVersion{39};
inline constexpr std::uint32_t kFreedomRelativeFramePolicy{1};
inline constexpr std::uint32_t kFreedomReleaseSystemFramePolicy{2};
inline constexpr std::uint32_t kFreedomLegacyTerrainPolicy{1};
inline constexpr std::uint32_t kFreedomLocalDetailTerrainPolicy{2};
// Explicit optics1 envelopes may select the triangulated sea-datum top.
inline constexpr std::uint32_t kFreedomSeaDatumTerrainPolicy{3};
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
  // Policy1 retains all historical source/contact arithmetic. Policy2 binds
  // physical local-detail recipe2 to its matching versioned top triangles.
  std::uint32_t terrain_policy{kFreedomLegacyTerrainPolicy};
  // Policy2 retains a system-inertial body after the explicit release event;
  // its named world owner still selects the existing force/view body.
  std::uint32_t frame_policy{kFreedomRelativeFramePolicy};
  // Zero retains historical clear appearance; optics1 is explicitly selected.
  std::uint32_t optics{};
  // Zero preserves historical absence; one admits the separate distant catalog.
  std::uint32_t companions{};
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
  // Explicit format42; exclusive with the historical planet selection.
  std::optional<FreedomMoonWorldSelection> moon{};
  friend auto operator==(const FreedomFlightSaveDocument&,
                         const FreedomFlightSaveDocument&) -> bool = default;
};
struct FreedomFlightHydration {
  PhysicalLocalSystem system;
  PhysicalPlanetRotationRecipe rotation;
  AtmosphericFlightSample atmosphere;
  // Present only for the typed moon route. Planet rotation/atmosphere above
  // are unavailable in that case; consumers must dispatch on this selection.
  std::optional<FreedomMoonFlightHydration> moon{};
};
// Strict same-tick view of the sole canonical body; never changes the source,
// selects a force body, or recursively hydrates a complete flight.
[[nodiscard]] auto relative_freedom_flight_state(
    const FreedomFlightSaveDocument&, const PhysicalLocalSystem&)
    -> std::expected<RigidBodyState, SaveSchemaError>;

// Format 18 retains the physical origin home frame. Explicit format 26
// selects a real planet frame in the bounded origin/neighbor pair; no study
// fallback or implicit migration. Format36 explicitly retains frame policy2
// with a relative attached or system-inertial released canonical body.
[[nodiscard]] auto hydrate_freedom_flight_document(
    const FreedomFlightSaveDocument&)
    -> std::expected<FreedomFlightHydration, SaveSchemaError>;
[[nodiscard]] auto encode_freedom_flight_document_json(
    const FreedomFlightSaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_freedom_flight_document_json(std::string_view)
    -> std::expected<FreedomFlightSaveDocument, SaveSchemaError>;
// Complete format35 identity/model plus physical rigid-v2 system state, never
// bare rigid JSON. Authenticate, then translate at the source tick into the
// explicitly selected planet frame. Does not advance, write or change controls.
// The historical decoder above and all legacy nested wrappers refuse format35.
[[nodiscard]] auto decode_freedom_system_flight_import_json(std::string_view)
    -> std::expected<FreedomFlightSaveDocument, SaveSchemaError>;
} // namespace apsis_drift
