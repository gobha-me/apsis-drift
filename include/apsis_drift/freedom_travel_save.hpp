#pragma once

#include "apsis_drift/freedom_jump_targeting.hpp"
#include "apsis_drift/freedom_knowledge_save.hpp"
#include "apsis_drift/freedom_starting_assembly_save.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomTravelSaveFormatVersion{27};
inline constexpr std::uint32_t kFreedomTravelStateVersion{1};
enum class FreedomJumpPhase : std::uint8_t { idle, spool, transit };
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
