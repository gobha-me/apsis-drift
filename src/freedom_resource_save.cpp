#include "apsis_drift/freedom_resource_save.hpp"

#include <array>
#include <nlohmann/json.hpp>
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
} // namespace

auto validate_freedom_resource_document(const FreedomResourceSaveDocument& d)
    -> std::expected<void, SaveSchemaError> {
  if (auto valid = validate_freedom_surface_document(d.voyage); !valid)
    return valid;
  if (!validate_freedom_resources(d.resources,
                                  surface_base_flight(d.voyage.base)))
    return std::unexpected{failure(
        "$.resources",
        "unsupported resource recipe, quantity, craft owner or shared tick")};
  return {};
}
auto encode_freedom_resource_document_json(const FreedomResourceSaveDocument& d)
    -> std::expected<std::string, SaveSchemaError> {
  if (auto v = validate_freedom_resource_document(d); !v)
    return std::unexpected{v.error()};
  auto voyage = encode_freedom_surface_document_json(d.voyage);
  if (!voyage) return std::unexpected{voyage.error()};
  const auto& s = d.resources;
  const Json root = {{"format_version", kFreedomResourceSaveFormatVersion},
                     {"voyage", Json::parse(*voyage)},
                     {"resources",
                      {{"version", s.version},
                       {"craft", s.craft.value},
                       {"frame_id", s.frame.id.value},
                       {"frame_version", s.frame.version},
                       {"tick", s.tick},
                       {"flight_quanta", s.flight_quanta},
                       {"jump_charges", s.jump_charges}}}};
  auto text = root.dump(2) + '\n';
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "resource save exceeds limit"}};
  return text;
}
auto decode_freedom_resource_document_json(std::string_view text)
    -> std::expected<FreedomResourceSaveDocument, SaveSchemaError> {
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "resource save exceeds limit"}};
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
  if (root["format_version"] != kFreedomResourceSaveFormatVersion)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::unsupported_format_version,
                        "$.format_version", "unsupported resource format"}};
  if (!fields(root, {"format_version", "voyage", "resources"}) ||
      !fields(root["resources"],
              {"version", "craft", "frame_id", "frame_version", "tick",
               "flight_quanta", "jump_charges"}))
    return std::unexpected{failure("$", "invalid closed resource shape")};
  const auto& s = root["resources"];
  for (auto name : {"version", "craft", "frame_id", "frame_version", "tick",
                    "flight_quanta", "jump_charges"})
    if (!s[name].is_number_unsigned())
      return std::unexpected{
          failure("$.resources", "unsigned integer fields required")};
  // Compare wide parsed values before any narrowing conversion.
  if (s["version"] != kFreedomResourceVersion ||
      s["frame_id"] != kWayfarerFrameId.value ||
      s["frame_version"] != kWayfarerFrameVersion ||
      s["jump_charges"] > kFreedomJumpCapacity ||
      s["flight_quanta"] > kFreedomFlightCapacityQuanta)
    return std::unexpected{
        failure("$.resources", "unsupported recipe or out-of-range quantity")};
  auto voyage = decode_freedom_surface_document_json(root["voyage"].dump());
  if (!voyage) return std::unexpected{voyage.error()};
  FreedomResources ledger{1,
                          {s["craft"].get<std::uint64_t>()},
                          {kWayfarerFrameId, 1},
                          s["tick"].get<SimulationTick>(),
                          s["flight_quanta"].get<std::uint64_t>(),
                          s["jump_charges"].get<std::uint32_t>()};
  FreedomResourceSaveDocument result{std::move(*voyage), ledger};
  if (auto v = validate_freedom_resource_document(result); !v)
    return std::unexpected{v.error()};
  return result;
}
} // namespace apsis_drift
