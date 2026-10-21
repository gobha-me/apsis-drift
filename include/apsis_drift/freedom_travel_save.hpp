#pragma once

#include "apsis_drift/freedom_jump_targeting.hpp"
#include "apsis_drift/freedom_knowledge_save.hpp"
#include "apsis_drift/freedom_local_jump.hpp"
#include "apsis_drift/freedom_planet_jump.hpp"
#include "apsis_drift/freedom_starting_assembly_save.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomTravelSaveFormatVersion{27};
// Explicit network selection; logical travel states remain1..4.
inline constexpr std::uint32_t kFreedomNetworkTravelSaveFormatVersion{53};
inline constexpr std::uint32_t kFreedomTravelStateVersion{1};
inline constexpr std::uint32_t kFreedomLocalTravelSaveFormatVersion{45};
inline constexpr std::uint32_t kFreedomLocalTravelStateVersion{2};
inline constexpr std::uint32_t kFreedomMinorTravelSaveFormatVersion{47};
inline constexpr std::uint32_t kFreedomMinorTravelStateVersion{3};
inline constexpr std::uint32_t kFreedomOriginLocalTravelSaveFormatVersion{48};
inline constexpr std::uint32_t kFreedomOriginLocalTravelStateVersion{4};
inline constexpr std::uint32_t kFreedomOriginLocalRoutesVersion{1};
inline constexpr std::uint32_t kFreedomComposedTravelSaveFormatVersion{56};
inline constexpr std::uint32_t kFreedomComposedTravelStateVersion{5};
inline constexpr std::uint32_t kFreedomComposedRoutesVersion{1};
enum class FreedomJumpPhase : std::uint8_t { idle, spool, transit };
struct FreedomLocalTravelState {
  std::optional<BodyTarget> selected;
  std::optional<FrozenLocalBodyJumpArrival> committed;
  std::optional<FreedomResources> commitment_resources;
  // Original home-to-Moon proof retains the actual home model through return.
  // Subsequent commitments never overwrite or infer this source profile.
  std::optional<FrozenLocalBodyJumpArrival> outbound;
  friend auto operator==(const FreedomLocalTravelState&,
                         const FreedomLocalTravelState&) -> bool = default;
};
struct FreedomLocalMoonHistory {
  FreedomLocalTravelState local;
  friend auto operator==(const FreedomLocalMoonHistory&,
                         const FreedomLocalMoonHistory&) -> bool = default;
};
enum class FreedomLocalRouteKind : std::uint8_t { moon, minor };
struct FreedomLocalRouteCheckpoint {
  FreedomLocalTravelState local;
  std::optional<FrozenLocalBodyJumpArrival> excursion_outbound;
  friend auto operator==(const FreedomLocalRouteCheckpoint&,
                         const FreedomLocalRouteCheckpoint&) -> bool = default;
};
// Original old-format proofs, not a nested travel/voyage or an inferred last
// outbound. New version4 checkpoints start empty on explicit home adoption.
struct FreedomLocalTravelLegacySpan {
  std::uint32_t version{};
  FreedomLocalTravelState local;
  std::optional<FreedomLocalMoonHistory> prior_moon;
  std::uint64_t next_attempt{};
  friend auto operator==(const FreedomLocalTravelLegacySpan&,
                         const FreedomLocalTravelLegacySpan&) -> bool = default;
};
struct FreedomOriginLocalRoutes {
  std::uint32_t version{kFreedomOriginLocalRoutesVersion};
  FreedomLocalRouteKind active{FreedomLocalRouteKind::moon};
  // The opposite route's fixed checkpoint. travel.local owns the active one.
  FreedomLocalRouteCheckpoint inactive;
  std::optional<FrozenLocalBodyJumpArrival> excursion_outbound;
  FreedomLocalTravelLegacySpan legacy;
  friend auto operator==(const FreedomOriginLocalRoutes&,
                         const FreedomOriginLocalRoutes&) -> bool = default;
};
enum class FreedomComposedRouteKind : std::uint8_t {
  system,
  planet,
  moon,
  minor
};
struct FreedomComposedLocalCheckpoint {
  std::optional<FrozenLocalBodyJumpArrival> committed;
  std::optional<FreedomResources> commitment_resources;
  // The current actual home departure, refreshed only on a real outbound
  // commitment. Its home model owns the matching companion return.
  std::optional<FrozenLocalBodyJumpArrival> outbound;
  friend auto operator==(const FreedomComposedLocalCheckpoint&,
                         const FreedomComposedLocalCheckpoint&)
      -> bool = default;
};
struct FreedomComposedTravelState {
  std::uint32_t version{kFreedomComposedRoutesVersion};
  FreedomComposedRouteKind active{FreedomComposedRouteKind::system};
  std::optional<BodyTarget> selected_body;
  std::optional<FrozenFreedomPlanetJumpArrival> planet_committed;
  std::optional<FreedomResources> planet_commitment_resources;
  FreedomComposedLocalCheckpoint moon, minor;
  friend auto operator==(const FreedomComposedTravelState&,
                         const FreedomComposedTravelState&) -> bool = default;
};
struct FreedomTravelState {
  std::uint32_t version{kFreedomTravelStateVersion};
  FreedomJumpPhase phase{FreedomJumpPhase::idle};
  std::optional<SystemId> selected;
  SimulationTick spool_tick{};
  std::uint64_t next_attempt{1};
  // The latest commitment remains as bounded history after arrival. During
  // transit its request is the immutable source and its point is authoritative.
  std::optional<FrozenFreedomJumpArrival> committed;
  std::optional<FreedomResources> commitment_resources;
  // Explicit version2 only. Existing intersystem commitment/bill remain prior
  // history; phase, spool clock and attempt counter own the selected local leg.
  std::optional<FreedomLocalTravelState> local{};
  std::optional<FreedomLocalMoonHistory> prior_moon{};
  std::optional<FreedomOriginLocalRoutes> origin_routes{};
  // Explicit recipe5 journeys only. Fixed independent route receipts share
  // the phase and attempt counter above; old formats never adopt this state.
  std::optional<FreedomComposedTravelState> composed{};
  friend auto operator==(const FreedomTravelState&, const FreedomTravelState&)
      -> bool = default;
};
struct FreedomTravelSaveDocument {
  // Exactly one current voyage/flight, not a second source checkpoint.
  FreedomKnowledgeSaveDocument voyage;
  FreedomTravelState travel;
  // Preserve the existing authored spacecraft selection across world handoff.
  std::optional<NativeStartingAssemblySelection> craft_binding;
  // When there is no current port, preserve the completed, actual entry route.
  // A nested boarding voyage owns this state instead when a port is selected.
  std::optional<FreedomBoardingState> seated_pilot;
  friend auto operator==(const FreedomTravelSaveDocument&,
                         const FreedomTravelSaveDocument&) -> bool = default;
};
[[nodiscard]] auto validate_freedom_travel_document(
    const FreedomTravelSaveDocument&) -> std::expected<void, SaveSchemaError>;
[[nodiscard]] auto encode_freedom_travel_document_json(
    const FreedomTravelSaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_freedom_travel_document_json(std::string_view)
    -> std::expected<FreedomTravelSaveDocument, SaveSchemaError>;
} // namespace apsis_drift
