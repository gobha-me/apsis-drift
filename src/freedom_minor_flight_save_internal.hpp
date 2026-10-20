#pragma once

#include <nlohmann/json.hpp>

#include "apsis_drift/freedom_flight_save.hpp"

namespace apsis_drift::detail {
// Decoder is called only after the ordinary flight entry point has checked
// size, nesting, duplicate keys and the explicit unsigned format46 dispatch.
[[nodiscard]] auto decode_minor_flight(const nlohmann::ordered_json&)
    -> std::expected<FreedomFlightSaveDocument, SaveSchemaError>;
[[nodiscard]] auto encode_minor_flight(const FreedomFlightSaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
} // namespace apsis_drift::detail
