#pragma once
#include "apsis_drift/freedom_condition_save.hpp"
#include "apsis_drift/origin_service_sortie_save.hpp"
#include <utility>

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomAssistanceSaveFormatVersion{59};
inline constexpr std::uint32_t kFreedomAssistanceVersion{1};
using FreedomAssistanceVoyage =
    std::variant<FreedomKnowledgeSaveDocument, FreedomTravelSaveDocument,
                 FreedomRecoverySaveDocument, FreedomSurfaceWalkSaveDocument,
                 FreedomConditionSaveDocument>;
// A failed route retires this one active contact. Its last authenticated
// state is historical, not a frozen live body or a predicted future pose.
// The player may keep advancing; no service credit/refund/replacement occurs.
enum class OriginServiceUnavailableReason : std::uint8_t {
  route_unavailable = 1
};
struct OriginServiceUnavailable {
  OriginServiceSortie last;
  SimulationTick failed_tick{};
  OriginServiceUnavailableReason reason{
      OriginServiceUnavailableReason::route_unavailable};
  friend auto operator==(const OriginServiceUnavailable&,
                         const OriginServiceUnavailable&) -> bool = default;
};
using FreedomAssistanceActor =
    std::variant<OriginServiceCraftState, OriginServiceSortie,
                 OriginServiceUnavailable>;
struct FreedomAssistanceSaveDocument {
  std::uint32_t version{kFreedomAssistanceVersion};
  FreedomAssistanceVoyage voyage;
  FreedomAssistanceActor actor;
  friend auto operator==(const FreedomAssistanceSaveDocument&,
                         const FreedomAssistanceSaveDocument&)
      -> bool = default;
};
// Immutable, purpose-specific owner. One actual Moon descriptor already owns
// the complete Origin catalog; no duplicate simulated universe is introduced.
class OriginAssistanceWorldContext {
 public:
  [[nodiscard]] auto origin() const -> const PhysicalLocalSystem& {
    return moon_.catalog.owner;
  }
  [[nodiscard]] auto moon() const -> const PhysicalMoonDescriptor& {
    return moon_;
  }

 private:
  PhysicalMoonDescriptor moon_;
  explicit OriginAssistanceWorldContext(PhysicalMoonDescriptor moon)
      : moon_(std::move(moon)) {}
  friend auto make_origin_assistance_world_context(Seed)
      -> std::expected<OriginAssistanceWorldContext, SaveSchemaError>;
};
[[nodiscard]] auto make_origin_assistance_world_context(Seed)
    -> std::expected<OriginAssistanceWorldContext, SaveSchemaError>;
[[nodiscard]] auto assistance_player_flight(const FreedomAssistanceVoyage&)
    -> FreedomFlightSaveDocument;
[[nodiscard]] auto assistance_player_resources(const FreedomAssistanceVoyage&)
    -> const FreedomResources&;
[[nodiscard]] auto assistance_player_attachment(const FreedomAssistanceVoyage&)
    -> std::optional<OriginDockConstraint>;
[[nodiscard]] auto validate_freedom_assistance_document(
    const OriginAssistanceWorldContext&, const FreedomAssistanceSaveDocument&)
    -> std::expected<void, SaveSchemaError>;
[[nodiscard]] auto validate_freedom_assistance_document(
    const FreedomAssistanceSaveDocument&)
    -> std::expected<void, SaveSchemaError>;
[[nodiscard]] auto encode_freedom_assistance_document_json(
    const FreedomAssistanceSaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_freedom_assistance_document_json(std::string_view)
    -> std::expected<FreedomAssistanceSaveDocument, SaveSchemaError>;
// Explicit NewGame selector only. Existing factories and Continue stay exact.
[[nodiscard]] auto make_freedom_assistance_document(
    FreedomKnowledgeSaveDocument)
    -> std::expected<FreedomAssistanceSaveDocument, SaveSchemaError>;
} // namespace apsis_drift
