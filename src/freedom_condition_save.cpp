#include "apsis_drift/freedom_condition_save.hpp"

#include <array>
#include <nlohmann/json.hpp>
#include <type_traits>
#include <unordered_set>

namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;
auto failure(std::string message) -> SaveSchemaError {
  return {SaveSchemaErrorCode::invalid_state, "$.condition",
          std::move(message)};
}
auto fields(const Json& j, std::initializer_list<std::string_view> names)
    -> bool {
  if (!j.is_object() || j.size() != names.size()) return false;
  for (auto n : names)
    if (!j.contains(std::string(n))) return false;
  return true;
}
auto nesting(std::string_view text) -> bool {
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
auto encode_voyage(const FreedomConditionVoyage& voyage)
    -> std::expected<std::string, SaveSchemaError> {
  return std::visit(
      [](const auto& d) {
        using T = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<T, FreedomKnowledgeSaveDocument>)
          return encode_freedom_knowledge_document_json(d);
        else if constexpr (std::is_same_v<T, FreedomTravelSaveDocument>)
          return encode_freedom_travel_document_json(d);
        else if constexpr (std::is_same_v<T, FreedomRecoverySaveDocument>)
          return encode_freedom_recovery_document_json(d);
        else
          return encode_freedom_surface_walk_document_json(d);
      },
      voyage);
}
auto decode_voyage(const Json& j)
    -> std::expected<FreedomConditionVoyage, SaveSchemaError> {
  if (!j.is_object() || !j.contains("format_version") ||
      !j["format_version"].is_number_unsigned())
    return std::unexpected(failure("Invalid nested voyage format"));
  const auto text = j.dump();
  if (j["format_version"] == kFreedomKnowledgeSaveFormatVersion) {
    auto d = decode_freedom_knowledge_document_json(text);
    if (!d) return std::unexpected(d.error());
    return FreedomConditionVoyage{std::move(*d)};
  }
  if (j["format_version"] == kFreedomTravelSaveFormatVersion ||
      j["format_version"] == kFreedomLocalTravelSaveFormatVersion ||
      j["format_version"] == kFreedomMinorTravelSaveFormatVersion ||
      j["format_version"] == kFreedomOriginLocalTravelSaveFormatVersion) {
    auto d = decode_freedom_travel_document_json(text);
    if (!d) return std::unexpected(d.error());
    return FreedomConditionVoyage{std::move(*d)};
  }
  if (j["format_version"] == kFreedomRecoverySaveFormatVersion) {
    auto d = decode_freedom_recovery_document_json(text);
    if (!d) return std::unexpected(d.error());
    return FreedomConditionVoyage{std::move(*d)};
  }
  if (j["format_version"] == kFreedomSurfaceWalkSaveFormatVersion ||
      j["format_version"] == kFreedomMoonSurfaceWalkSaveFormatVersion) {
    auto d = decode_freedom_surface_walk_document_json(text);
    if (!d) return std::unexpected(d.error());
    return FreedomConditionVoyage{std::move(*d)};
  }
  return std::unexpected(failure("Unsupported or recursive condition voyage"));
}
auto recovery(const FreedomConditionVoyage& voyage)
    -> const FreedomRecoverySaveDocument* {
  if (const auto* d = std::get_if<FreedomRecoverySaveDocument>(&voyage))
    return d;
  if (const auto* d = std::get_if<FreedomSurfaceWalkSaveDocument>(&voyage))
    return &d->voyage;
  return nullptr;
}
auto supported(const FreedomConditionVoyage& voyage) -> bool {
  if (const auto* d = std::get_if<FreedomTravelSaveDocument>(&voyage))
    return d->craft_binding == NativeStartingAssemblySelection{};
  if (recovery(voyage))
    return true; // Its existing validator authenticates the starter selection.
  const auto& base =
      std::get<FreedomKnowledgeSaveDocument>(voyage).voyage.voyage.base;
  return std::visit(
      [](const auto& d) {
        using T = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<T, FreedomStartingAssemblySaveDocument> ||
                      std::is_same_v<T, FreedomBoardingSaveDocument>)
          return d.starting_assembly == NativeStartingAssemblySelection{};
        else
          return false;
      },
      base);
}
} // namespace

auto condition_flight(const FreedomConditionVoyage& voyage)
    -> FreedomFlightSaveDocument {
  return std::visit(
      [](const auto& d) {
        using T = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<T, FreedomKnowledgeSaveDocument>)
          return surface_base_flight(d.voyage.voyage.base);
        else if constexpr (std::is_same_v<T, FreedomTravelSaveDocument>)
          return surface_base_flight(d.voyage.voyage.voyage.base);
        else if constexpr (std::is_same_v<T, FreedomRecoverySaveDocument>)
          return recovery_flight(d.voyage);
        else
          return recovery_flight(d.voyage.voyage);
      },
      voyage);
}
auto validate_freedom_condition_document(const FreedomConditionSaveDocument& d)
    -> std::expected<void, SaveSchemaError> {
  if (d.consequence_version != kImpactConsequencesVersion)
    return std::unexpected(failure("Unsupported consequence policy"));
  if (const auto encoded = encode_voyage(d.voyage); !encoded)
    return std::unexpected(encoded.error());
  if (!supported(d.voyage))
    return std::unexpected(
        failure("Consequences require explicitly selected Wayfarer hardware"));
  if (const auto valid =
          validate_craft_condition(d.condition, condition_flight(d.voyage));
      !valid)
    return std::unexpected(failure(valid.error()));
  const auto* r = recovery(d.voyage);
  if (d.retired_condition) {
    if (!r || !r->recovery.latest)
      return std::unexpected(
          failure("Retired condition requires the matching recovery loss"));
    if (const auto valid = validate_craft_condition(
            *d.retired_condition, recovery_flight(r->recovery.latest->retired));
        !valid)
      return std::unexpected(failure("Retired condition owner/tick mismatch"));
    if (r->recovery.pending && *d.retired_condition != d.condition)
      return std::unexpected(
          failure("Pending loss must retain its exact source condition"));
    if (!r->recovery.pending && d.retired_condition->craft == d.condition.craft)
      return std::unexpected(
          failure("Replacement cannot retain the retired condition owner"));
  }
  if (r && r->recovery.pending && !d.retired_condition)
    return std::unexpected(
        failure("Selected pending loss requires its retained condition"));
  if (d.condition.latest && d.condition.latest->catastrophic &&
      (!r || !r->recovery.pending))
    return std::unexpected(failure(
        "Catastrophic condition requires one pending recovery transaction"));
  return {};
}
auto encode_freedom_condition_document_json(
    const FreedomConditionSaveDocument& d)
    -> std::expected<std::string, SaveSchemaError> {
  if (auto valid = validate_freedom_condition_document(d); !valid)
    return std::unexpected(valid.error());
  auto voyage = encode_voyage(d.voyage);
  if (!voyage) return std::unexpected(voyage.error());
  auto condition = encode_craft_condition_json(d.condition);
  if (!condition) return std::unexpected(condition.error());
  Json retired = nullptr;
  if (d.retired_condition) {
    auto value = encode_craft_condition_json(*d.retired_condition);
    if (!value) return std::unexpected(value.error());
    retired = Json::parse(*value);
  }
  const Json root{{"format_version", kFreedomConditionSaveFormatVersion},
                  {"voyage", Json::parse(*voyage)},
                  {"consequences", {{"version", d.consequence_version}}},
                  {"condition", Json::parse(*condition)},
                  {"retired_condition", retired}};
  auto text = root.dump(2) + '\n';
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected(
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "Condition save exceeds bound"});
  return text;
}
auto decode_freedom_condition_document_json(std::string_view text)
    -> std::expected<FreedomConditionSaveDocument, SaveSchemaError> {
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected(
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "Condition save exceeds bound"});
  if (!nesting(text))
    return std::unexpected(SaveSchemaError{SaveSchemaErrorCode::malformed_json,
                                           "$", "Invalid condition nesting"});
  bool duplicate{};
  std::vector<std::unordered_set<std::string>> keys;
  const Json::parser_callback_t cb = [&](int, Json::parse_event_t e,
                                         Json& value) {
    if (e == Json::parse_event_t::object_start) keys.emplace_back();
    if (e == Json::parse_event_t::key &&
        (keys.empty() || !keys.back().insert(value.get<std::string>()).second))
      duplicate = true;
    if (e == Json::parse_event_t::object_end && !keys.empty()) keys.pop_back();
    return true;
  };
  try {
    const auto root = Json::parse(text.begin(), text.end(), cb);
    if (duplicate)
      return std::unexpected(SaveSchemaError{SaveSchemaErrorCode::duplicate_key,
                                             "$", "Repeated condition key"});
    if (!root.is_object() || !root.contains("format_version") ||
        !root["format_version"].is_number_unsigned())
      return std::unexpected(failure("Unsigned condition format required"));
    if (root["format_version"] != kFreedomConditionSaveFormatVersion)
      return std::unexpected(
          SaveSchemaError{SaveSchemaErrorCode::unsupported_format_version,
                          "$.format_version", "Unsupported condition format"});
    if (!fields(root, {"format_version", "voyage", "consequences", "condition",
                       "retired_condition"}) ||
        !fields(root["consequences"], {"version"}) ||
        !root["consequences"]["version"].is_number_unsigned() ||
        root["consequences"]["version"] != kImpactConsequencesVersion)
      return std::unexpected(
          failure("Invalid closed consequence shape/version"));
    auto voyage = decode_voyage(root["voyage"]);
    if (!voyage) return std::unexpected(voyage.error());
    auto condition = decode_craft_condition_json(root["condition"].dump());
    if (!condition) return std::unexpected(condition.error());
    FreedomConditionSaveDocument d{1, std::move(*voyage), *condition, {}};
    if (!root["retired_condition"].is_null()) {
      auto retired =
          decode_craft_condition_json(root["retired_condition"].dump());
      if (!retired) return std::unexpected(retired.error());
      d.retired_condition = *retired;
    }
    if (auto valid = validate_freedom_condition_document(d); !valid)
      return std::unexpected(valid.error());
    return d;
  } catch (const Json::exception& e) {
    return std::unexpected(
        SaveSchemaError{SaveSchemaErrorCode::malformed_json, "$", e.what()});
  }
}
} // namespace apsis_drift
