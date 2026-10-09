#include "apsis_drift/freedom_surface_save.hpp"

#include <array>
#include <nlohmann/json.hpp>
#include <type_traits>
#include <unordered_set>

namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;
auto failure(std::string path, std::string message) -> SaveSchemaError {
  return {SaveSchemaErrorCode::invalid_state, std::move(path),
          std::move(message)};
}
auto fields(const Json& j, std::initializer_list<std::string_view> keys)
    -> bool {
  if (!j.is_object() || j.size() != keys.size()) return false;
  for (auto k : keys)
    if (!j.contains(std::string{k})) return false;
  return true;
}
auto bounded_nesting(std::string_view text) -> bool {
  std::array<char, 64> stack{};
  std::size_t depth{};
  bool quoted{}, escaped{};
  for (char c : text) {
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
    } else if (c == '"')
      quoted = true;
    else if (c == '{' || c == '[') {
      if (depth == stack.size()) return false;
      stack[depth++] = c;
    } else if (c == '}' || c == ']') {
      if (!depth || stack[depth - 1] != (c == '}' ? '{' : '[')) return false;
      --depth;
    }
  }
  return !depth && !quoted;
}
auto encode_base(const FreedomSurfaceBaseSave& base)
    -> std::expected<std::string, SaveSchemaError> {
  return std::visit(
      [](const auto& d) -> std::expected<std::string, SaveSchemaError> {
        using T = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<T, FreedomFlightSaveDocument>)
          return encode_freedom_flight_document_json(d);
        else if constexpr (std::is_same_v<T, FreedomDockingSaveDocument>)
          return encode_freedom_docking_document_json(d);
        else if constexpr (std::is_same_v<T, FreedomJourneySaveDocument>)
          return encode_freedom_journey_document_json(d);
        else if constexpr (std::is_same_v<T,
                                          FreedomStartingAssemblySaveDocument>)
          return encode_freedom_starting_assembly_document_json(d);
        else
          return encode_freedom_boarding_document_json(d);
      },
      base);
}
auto decode_base(const Json& j)
    -> std::expected<FreedomSurfaceBaseSave, SaveSchemaError> {
  if (!j.is_object() || !j.contains("format_version") ||
      !j["format_version"].is_number_unsigned())
    return std::unexpected{
        failure("$.base", "unsigned nested format required")};
  const auto text = j.dump();
  const auto& version = j["format_version"];
  if (version == kFreedomFlightSaveFormatVersion) {
    auto d = decode_freedom_flight_document_json(text);
    if (!d) return std::unexpected{d.error()};
    return FreedomSurfaceBaseSave{std::move(*d)};
  }
  if (version == kFreedomDockingSaveFormatVersion) {
    auto d = decode_freedom_docking_document_json(text);
    if (!d) return std::unexpected{d.error()};
    return FreedomSurfaceBaseSave{std::move(*d)};
  }
  if (version == kFreedomJourneySaveFormatVersion) {
    auto d = decode_freedom_journey_document_json(text);
    if (!d) return std::unexpected{d.error()};
    return FreedomSurfaceBaseSave{std::move(*d)};
  }
  if (version == kFreedomStartingAssemblySaveFormatVersion) {
    auto d = decode_freedom_starting_assembly_document_json(text);
    if (!d) return std::unexpected{d.error()};
    return FreedomSurfaceBaseSave{std::move(*d)};
  }
  if (version == kFreedomBoardingSaveFormatVersion) {
    auto d = decode_freedom_boarding_document_json(text);
    if (!d) return std::unexpected{d.error()};
    return FreedomSurfaceBaseSave{std::move(*d)};
  }
  return std::unexpected{SaveSchemaError{
      SaveSchemaErrorCode::unsupported_format_version, "$.base.format_version",
      "unsupported surface base version"}};
}
auto attached_or_outside(const FreedomSurfaceBaseSave& base) -> bool {
  return std::visit(
      [](const auto& d) -> bool {
        using T = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<T, FreedomFlightSaveDocument>)
          return false;
        else if constexpr (std::is_same_v<T, FreedomDockingSaveDocument>)
          return d.docking.attached;
        else if constexpr (std::is_same_v<T, FreedomBoardingSaveDocument>)
          return d.voyage.docking.attached || d.station_actor.has_value() ||
                 d.boarding.phase != FreedomBoardingPhase::seated;
        else
          return true; // Both journey-only variants have a station actor.
      },
      base);
}
} // namespace

auto surface_base_flight(const FreedomSurfaceBaseSave& base)
    -> FreedomFlightSaveDocument {
  return std::visit(
      [](const auto& d) -> FreedomFlightSaveDocument {
        using T = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<T, FreedomFlightSaveDocument>)
          return d;
        else if constexpr (std::is_same_v<T, FreedomDockingSaveDocument>)
          return d.flight;
        else if constexpr (std::is_same_v<T,
                                          FreedomStartingAssemblySaveDocument>)
          return d.journey.voyage.flight;
        else
          return d.voyage.flight;
      },
      base);
}

auto validate_freedom_surface_document(const FreedomSurfaceSaveDocument& d)
    -> std::expected<void, SaveSchemaError> {
  const auto base = encode_base(d.base);
  if (!base) return std::unexpected{base.error()};
  const auto& flight = surface_base_flight(d.base);
  const auto hydrated = hydrate_freedom_flight_document(flight);
  if (!hydrated) return std::unexpected{hydrated.error()};
  if (d.surface.gear_deployed && attached_or_outside(d.base))
    return std::unexpected{
        failure("$.surface.gear_deployed",
                "stow gear before station capture or walking")};
  if (!d.surface.landed) return {};
  if (!d.surface.gear_deployed || attached_or_outside(d.base))
    return std::unexpected{
        failure("$.surface", "landed craft requires deployed gear, seated "
                             "pilot and no station constraint")};
  const auto& anchor = *d.surface.landed;
  if (anchor.fixed.craft != flight.flight.craft ||
      anchor.fixed.frame.planet != flight.flight.frame.planet ||
      anchor.fixed.frame.system != flight.flight.frame.system)
    return std::unexpected{failure(
        "$.surface.landed", "anchor must belong to the same craft and planet")};
  if (!validate_landed_craft(hydrated->system, hydrated->rotation, anchor,
                             flight.flight.tick))
    return std::unexpected{
        failure("$.surface.landed", "unsupported or invalid physical anchor")};
  const auto resolved = resolve_landed_craft(
      hydrated->system, hydrated->rotation, anchor, flight.flight.tick);
  if (!resolved || *resolved != flight.flight)
    return std::unexpected{
        failure("$.surface.landed",
                "stored flight differs from the exact constrained pose")};
  return {};
}

auto encode_freedom_surface_document_json(const FreedomSurfaceSaveDocument& d)
    -> std::expected<std::string, SaveSchemaError> {
  if (auto valid = validate_freedom_surface_document(d); !valid)
    return std::unexpected{valid.error()};
  const auto base = encode_base(d.base);
  if (!base) return std::unexpected{base.error()};
  Json landed = nullptr;
  if (d.surface.landed) {
    const auto hydrated =
        hydrate_freedom_flight_document(surface_base_flight(d.base));
    if (!hydrated) return std::unexpected{hydrated.error()};
    const auto& a = *d.surface.landed;
    const auto fixed =
        encode_rigid_body_state_json({hydrated->system}, a.fixed);
    if (!fixed)
      return std::unexpected{failure("$.surface.landed.fixed",
                                     "cannot encode canonical fixed anchor")};
    landed = {{"version", a.version},
              {"terrain_policy", a.terrain_policy},
              {"rotation_generator", a.rotation_generator},
              {"rotation_owner", a.rotation_owner},
              {"fixed", Json::parse(*fixed)}};
  }
  const Json root = {
      {"format_version", kFreedomSurfaceSaveFormatVersion},
      {"base", Json::parse(*base)},
      {"surface",
       {{"gear_deployed", d.surface.gear_deployed}, {"landed", landed}}}};
  auto text = root.dump(2) + '\n';
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "surface save exceeds limit"}};
  return text;
}

auto decode_freedom_surface_document_json(std::string_view text)
    -> std::expected<FreedomSurfaceSaveDocument, SaveSchemaError> {
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "surface save exceeds limit"}};
  if (!bounded_nesting(text))
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::malformed_json, "$",
                        "unbalanced or excessive JSON nesting"}};
  bool duplicate{};
  std::vector<std::unordered_set<std::string>> keys;
  const Json::parser_callback_t callback = [&](int, Json::parse_event_t event,
                                               Json& value) {
    if (event == Json::parse_event_t::object_start) keys.emplace_back();
    if (event == Json::parse_event_t::key &&
        (keys.empty() || !keys.back().insert(value.get<std::string>()).second))
      duplicate = true;
    if (event == Json::parse_event_t::object_end && !keys.empty())
      keys.pop_back();
    return true;
  };
  Json root;
  try {
    root = Json::parse(text.begin(), text.end(), callback);
  } catch (const Json::exception& e) {
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::malformed_json, "$", e.what()}};
  }
  if (duplicate)
    return std::unexpected{SaveSchemaError{SaveSchemaErrorCode::duplicate_key,
                                           "$", "repeated JSON key"}};
  if (!root.is_object() || !root.contains("format_version") ||
      !root["format_version"].is_number_unsigned())
    return std::unexpected{
        failure("$.format_version", "unsigned version required")};
  if (root["format_version"] != kFreedomSurfaceSaveFormatVersion)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::unsupported_format_version,
                        "$.format_version", "unsupported surface version"}};
  if (!fields(root, {"format_version", "base", "surface"}) ||
      !fields(root["surface"], {"gear_deployed", "landed"}) ||
      !root["surface"]["gear_deployed"].is_boolean())
    return std::unexpected{failure("$", "invalid closed surface shape")};
  auto base = decode_base(root["base"]);
  if (!base) return std::unexpected{base.error()};
  FreedomSurfaceState surface{root["surface"]["gear_deployed"].get<bool>(), {}};
  const auto& a = root["surface"]["landed"];
  if (!a.is_null()) {
    if (!fields(a, {"version", "terrain_policy", "rotation_generator",
                    "rotation_owner", "fixed"}) ||
        !a["version"].is_number_unsigned() || a["version"] != 1 ||
        !a["terrain_policy"].is_number_unsigned() || a["terrain_policy"] != 1 ||
        !a["rotation_generator"].is_number_unsigned() ||
        a["rotation_generator"] != 1 ||
        !a["rotation_owner"].is_number_unsigned() || a["rotation_owner"] != 1)
      return std::unexpected{
          failure("$.surface.landed", "unsupported anchor shape/version")};
    const auto hydrated =
        hydrate_freedom_flight_document(surface_base_flight(*base));
    if (!hydrated) return std::unexpected{hydrated.error()};
    const auto fixed =
        decode_rigid_body_state_json({hydrated->system}, a["fixed"].dump());
    if (!fixed)
      return std::unexpected{
          failure("$.surface.landed.fixed", "malformed canonical anchor")};
    surface.landed = LandedCraftAnchor{1, 1, 1, 1, *fixed};
  }
  FreedomSurfaceSaveDocument result{std::move(*base), surface};
  if (auto valid = validate_freedom_surface_document(result); !valid)
    return std::unexpected{valid.error()};
  return result;
}
} // namespace apsis_drift
