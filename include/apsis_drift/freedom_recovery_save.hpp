#pragma once

#include "apsis_drift/freedom_travel_save.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomRecoverySaveFormatVersion{29};
inline constexpr std::uint32_t kFreedomRecoveryVersion{1};
using FreedomRecoveryVoyage =
    std::variant<FreedomKnowledgeSaveDocument, FreedomTravelSaveDocument>;
enum class FreedomLossCause : std::uint8_t {
  recoverable_destruction,
  irrecoverable_destruction
};
struct FreedomSafeStationCheckpoint {
  std::uint32_t version{kFreedomRecoveryVersion};
  OriginPortId port;
  SimulationTick tick{};
  friend auto operator==(const FreedomSafeStationCheckpoint&,
                         const FreedomSafeStationCheckpoint&) -> bool = default;
};
struct FreedomLossRecord {
  FreedomLossCause cause{};
  // Immutable retired history. This cannot contain another recovery wrapper
  // and is never opened as a second active session or presented as a wreck.
  FreedomRecoveryVoyage retired;
  std::uint64_t source_checksum{};
  friend auto operator==(const FreedomLossRecord&, const FreedomLossRecord&)
      -> bool = default;
};
struct FreedomRecoveryState {
  std::uint32_t version{kFreedomRecoveryVersion};
  std::optional<FreedomSafeStationCheckpoint> checkpoint;
  // Only the most recent loss is retained; no recursively growing snapshots.
  std::optional<FreedomLossRecord> latest;
  bool pending{};
  friend auto operator==(const FreedomRecoveryState&,
                         const FreedomRecoveryState&) -> bool = default;
};
struct FreedomRecoverySaveDocument {
  // Sole playable owner. Pending loss freezes it until explicit continuation.
  FreedomRecoveryVoyage voyage;
  FreedomRecoveryState recovery;
  friend auto operator==(const FreedomRecoverySaveDocument&,
                         const FreedomRecoverySaveDocument&) -> bool = default;
};
struct FreedomRecoveryStation {
  OriginPortId port;
  bool fallback{};
  friend auto operator==(const FreedomRecoveryStation&,
                         const FreedomRecoveryStation&) -> bool = default;
};
[[nodiscard]] auto recovery_knowledge(const FreedomRecoveryVoyage&)
    -> const FreedomKnowledgeSaveDocument&;
[[nodiscard]] auto recovery_knowledge(FreedomRecoveryVoyage&&)
    -> const FreedomKnowledgeSaveDocument& = delete;
[[nodiscard]] auto recovery_flight(const FreedomRecoveryVoyage&)
    -> FreedomFlightSaveDocument;
[[nodiscard]] auto freedom_recovery_source_checksum(
    const FreedomRecoveryVoyage&)
    -> std::expected<std::uint64_t, SaveSchemaError>;
[[nodiscard]] auto validate_freedom_recovery_document(
    const FreedomRecoverySaveDocument&) -> std::expected<void, SaveSchemaError>;
// Declared application loss only. Exceptions, invalid saves, fuel exhaustion
// and command failures do not call this function. Repeated matching events and
// completions are idempotent; an old event cannot destroy a replacement.
[[nodiscard]] auto record_freedom_loss(const FreedomRecoverySaveDocument&,
                                       FreedomLossCause, StarterCraftId,
                                       SimulationTick, std::uint64_t checksum)
    -> std::expected<FreedomRecoverySaveDocument, SaveSchemaError>;
[[nodiscard]] auto resolve_freedom_recovery_station(
    const FreedomRecoverySaveDocument&)
    -> std::expected<FreedomRecoveryStation, SaveSchemaError>;
[[nodiscard]] auto complete_freedom_recovery(const FreedomRecoverySaveDocument&)
    -> std::expected<FreedomRecoverySaveDocument, SaveSchemaError>;
[[nodiscard]] auto freedom_recovery_explanation(
    const FreedomRecoverySaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto encode_freedom_recovery_document_json(
    const FreedomRecoverySaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_freedom_recovery_document_json(std::string_view)
    -> std::expected<FreedomRecoverySaveDocument, SaveSchemaError>;
} // namespace apsis_drift
