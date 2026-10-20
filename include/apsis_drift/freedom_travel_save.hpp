#pragma once

#include "apsis_drift/freedom_jump_targeting.hpp"
#include "apsis_drift/freedom_knowledge_save.hpp"
#include "apsis_drift/freedom_local_jump.hpp"
#include "apsis_drift/freedom_starting_assembly_save.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomTravelSaveFormatVersion{27};
inline constexpr std::uint32_t kFreedomTravelStateVersion{1};
inline constexpr std::uint32_t kFreedomLocalTravelSaveFormatVersion{45};
inline constexpr std::uint32_t kFreedomLocalTravelStateVersion{2};
inline constexpr std::uint32_t kFreedomMinorTravelSaveFormatVersion{47};
inline constexpr std::uint32_t kFreedomMinorTravelStateVersion{3};
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
