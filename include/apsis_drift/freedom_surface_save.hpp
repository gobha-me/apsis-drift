#pragma once

#include "apsis_drift/freedom_boarding_save.hpp"
#include "apsis_drift/landed_craft.hpp"

#include <variant>

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomSurfaceSaveFormatVersion{23};
using FreedomSurfaceBaseSave =
    std::variant<FreedomFlightSaveDocument, FreedomDockingSaveDocument,
                 FreedomJourneySaveDocument,
                 FreedomStartingAssemblySaveDocument,
                 FreedomBoardingSaveDocument>;
struct FreedomSurfaceState {
  bool gear_deployed{};
  std::optional<LandedCraftAnchor> landed;
  friend auto operator==(const FreedomSurfaceState&, const FreedomSurfaceState&)
      -> bool = default;
};
// The unchanged base remains the sole craft/history/clock owner. The surface
// layer adds deployment and an optional fixed constraint for that same craft.
// No terrain catalog, transient assisted command or extra vehicle is stored.
struct FreedomSurfaceSaveDocument {
  FreedomSurfaceBaseSave base;
  FreedomSurfaceState surface;
  friend auto operator==(const FreedomSurfaceSaveDocument&,
                         const FreedomSurfaceSaveDocument&) -> bool = default;
};
[[nodiscard]] auto surface_base_flight(const FreedomSurfaceBaseSave&)
    -> FreedomFlightSaveDocument;
[[nodiscard]] auto validate_freedom_surface_document(
    const FreedomSurfaceSaveDocument&) -> std::expected<void, SaveSchemaError>;
[[nodiscard]] auto encode_freedom_surface_document_json(
    const FreedomSurfaceSaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_freedom_surface_document_json(std::string_view)
    -> std::expected<FreedomSurfaceSaveDocument, SaveSchemaError>;
} // namespace apsis_drift
