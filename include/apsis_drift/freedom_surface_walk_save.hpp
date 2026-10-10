#pragma once

#include "apsis_drift/freedom_recovery_save.hpp"
#include "apsis_drift/planet_surface_walker.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomSurfaceWalkSaveFormatVersion{30};
// Explicit selection of prototype suited walking, with no oxygen timer. The
// complete unchanged voyage remains the only craft, knowledge and clock owner.
// A present ground actor supersedes the retained cabin occupancy; flight is
// unavailable until that same actor returns. An absent actor is inside.
struct FreedomSurfaceWalkSaveDocument {
  FreedomRecoverySaveDocument voyage;
  std::uint32_t suit_version{1};
  std::optional<PlanetSurfaceWalkerState> actor;
  friend auto operator==(const FreedomSurfaceWalkSaveDocument&,
                         const FreedomSurfaceWalkSaveDocument&)
      -> bool = default;
};
[[nodiscard]] auto validate_freedom_surface_walk_document(
    const FreedomSurfaceWalkSaveDocument&)
    -> std::expected<void, SaveSchemaError>;
[[nodiscard]] auto encode_freedom_surface_walk_document_json(
    const FreedomSurfaceWalkSaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_freedom_surface_walk_document_json(std::string_view)
    -> std::expected<FreedomSurfaceWalkSaveDocument, SaveSchemaError>;
} // namespace apsis_drift
