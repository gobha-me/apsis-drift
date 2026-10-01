#pragma once

#include "apsis_drift/freedom_docking_save.hpp"
#include "apsis_drift/origin_walker.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomJourneySaveFormatVersion{20};
// Actor coordinates belong to the Origin station. Only the nested voyage owns
// the simulation tick, station identity and generated universe.
struct FreedomJourneySaveDocument {
  FreedomDockingSaveDocument voyage;
  OriginWalkerState actor;
  friend auto operator==(const FreedomJourneySaveDocument&,
                         const FreedomJourneySaveDocument&) -> bool = default;
};
[[nodiscard]] auto make_freedom_journey_new_game_document(Seed)
    -> std::expected<FreedomJourneySaveDocument, SaveSchemaError>;
[[nodiscard]] auto validate_freedom_journey_document(
    const FreedomJourneySaveDocument&) -> std::expected<void, SaveSchemaError>;
[[nodiscard]] auto encode_freedom_journey_document_json(
    const FreedomJourneySaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_freedom_journey_document_json(std::string_view)
    -> std::expected<FreedomJourneySaveDocument, SaveSchemaError>;
} // namespace apsis_drift
