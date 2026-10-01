#pragma once

#include "apsis_drift/freedom_flight_save.hpp"
#include "apsis_drift/origin_docking.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomDockingSaveFormatVersion{19};
struct FreedomDockingState {
  std::uint32_t geometry_version{1};
  OriginPortId target;
  bool attached{};
  friend auto operator==(const FreedomDockingState&, const FreedomDockingState&)
      -> bool = default;
};
struct FreedomDockingSaveDocument {
  FreedomFlightSaveDocument flight;
  FreedomDockingState docking;
  friend auto operator==(const FreedomDockingSaveDocument&,
                         const FreedomDockingSaveDocument&) -> bool = default;
};
// One canonical body/clock. The attachment derives its constraint from these
// saved fields; it does not own a second pose or time source.
[[nodiscard]] auto validate_freedom_docking_document(
    const FreedomDockingSaveDocument&) -> std::expected<void, SaveSchemaError>;
[[nodiscard]] auto encode_freedom_docking_document_json(
    const FreedomDockingSaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_freedom_docking_document_json(std::string_view)
    -> std::expected<FreedomDockingSaveDocument, SaveSchemaError>;
} // namespace apsis_drift
