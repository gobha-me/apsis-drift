#include "apsis_drift/freedom_starting_assembly_save.hpp"
#include <array>
#include <nlohmann/json.hpp>
#include <unordered_set>
namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;
auto failure(std::string path, std::string detail) -> SaveSchemaError {
  return {SaveSchemaErrorCode::invalid_state, std::move(path),
          std::move(detail)};
}
auto exact_fields(const Json& value,
                  std::initializer_list<std::string_view> names) -> bool {
  if (!value.is_object() || value.size() != names.size()) return false;
  for (auto name : names)
    if (!value.contains(std::string{name})) return false;
  return true;
}
// Bound recursion before building a JSON tree or delegating its dump to older
// schemas. Delimiters inside strings (including escaped quotes) are data.
auto bounded_nesting(std::string_view text) -> bool {
  std::array<char, 64> opened{};
  std::size_t depth{};
  bool quoted{}, escaped{};
  for (const char c : text) {
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
      continue;
    }
    if (c == '"')
      quoted = true;
    else if (c == '{' || c == '[') {
      if (depth == opened.size()) return false;
      opened[depth++] = c;
    } else if (c == '}' || c == ']') {
      if (depth == 0 || opened[depth - 1] != (c == '}' ? '{' : '['))
        return false;
      --depth;
    }
  }
  return depth == 0 && !quoted;
}
} // namespace

auto validate_freedom_starting_assembly_document(
    const FreedomStartingAssemblySaveDocument& d)
    -> std::expected<void, SaveSchemaError> {
  if (d.starting_assembly != NativeStartingAssemblySelection{})
    return std::unexpected{failure(
        "$.starting_assembly", "unsupported assembly, pins or hardware pose")};
  return validate_freedom_journey_document(d.journey);
}
auto make_freedom_starting_assembly_new_game_document(Seed seed)
    -> std::expected<FreedomStartingAssemblySaveDocument, SaveSchemaError> {
  auto d = make_freedom_journey_new_game_document(seed);
  if (!d) return std::unexpected{d.error()};
  return FreedomStartingAssemblySaveDocument{std::move(*d), {}};
}
auto encode_freedom_starting_assembly_document_json(
    const FreedomStartingAssemblySaveDocument& d)
    -> std::expected<std::string, SaveSchemaError> {
  if (auto v = validate_freedom_starting_assembly_document(d); !v)
    return std::unexpected{v.error()};
  auto j = encode_freedom_journey_document_json(d.journey);
  if (!j) return std::unexpected{j.error()};
  const auto& a = d.starting_assembly;
  Json root = {{"format_version", kFreedomStartingAssemblySaveFormatVersion},
               {"journey", Json::parse(*j)},
               {"starting_assembly",
                {{"version", a.version},
                 {"preset", a.preset},
                 {"operating_model_sha256", a.operating_model_sha256},
                 {"stowed_model_sha256", a.stowed_model_sha256},
                 {"frame_sha256", a.frame_sha256},
                 {"contact_sha256", a.contact_sha256},
                 {"hardware",
                  {{"roof_transfer", a.hardware.roof_transfer},
                   {"inner_door", a.hardware.inner_door},
                   {"seat_boarding", a.hardware.seat_boarding},
                   {"station_closure", a.hardware.station_closure}}}}}};
  auto text = root.dump(2) + '\n';
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "starting assembly save exceeds limit"}};
  return text;
}
auto decode_freedom_starting_assembly_document_json(std::string_view text)
    -> std::expected<FreedomStartingAssemblySaveDocument, SaveSchemaError> {
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "starting assembly save exceeds limit"}};
  if (!bounded_nesting(text))
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::malformed_json, "$",
                        "unbalanced or excessive JSON nesting"}};
  bool duplicate{};
  std::vector<std::unordered_set<std::string>> keys;
  const Json::parser_callback_t callback = [&](int, Json::parse_event_t event,
                                               Json& v) {
    if (event == Json::parse_event_t::object_start) keys.emplace_back();
    if (event == Json::parse_event_t::key &&
        (keys.empty() || !keys.back().insert(v.get<std::string>()).second))
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
  if (root["format_version"] != kFreedomStartingAssemblySaveFormatVersion)
    return std::unexpected{SaveSchemaError{
        SaveSchemaErrorCode::unsupported_format_version, "$.format_version",
        "unsupported starting assembly version"}};
  if (!exact_fields(root, {"format_version", "journey", "starting_assembly"}))
    return std::unexpected{failure("$", "unexpected wrapper fields")};
  const auto& a = root["starting_assembly"];
  if (!exact_fields(a, {"version", "preset", "operating_model_sha256",
                        "stowed_model_sha256", "frame_sha256", "contact_sha256",
                        "hardware"}) ||
      !a["version"].is_number_unsigned() || a["version"] != 1 ||
      !exact_fields(a["hardware"], {"roof_transfer", "inner_door",
                                    "seat_boarding", "station_closure"}))
    return std::unexpected{
        failure("$.starting_assembly", "invalid closed assembly shape")};
  NativeStartingAssemblySelection selected;
  for (auto name : {"preset", "operating_model_sha256", "stowed_model_sha256",
                    "frame_sha256", "contact_sha256"})
    if (!a[name].is_string())
      return std::unexpected{
          failure("$.starting_assembly", "string identity required")};
  selected.preset = a["preset"];
  selected.operating_model_sha256 = a["operating_model_sha256"];
  selected.stowed_model_sha256 = a["stowed_model_sha256"];
  selected.frame_sha256 = a["frame_sha256"];
  selected.contact_sha256 = a["contact_sha256"];
  const auto& h = a["hardware"];
  for (auto name :
       {"roof_transfer", "inner_door", "seat_boarding", "station_closure"})
    if (!h[name].is_number())
      return std::unexpected{
          failure("$.starting_assembly.hardware", "numeric channel required")};
  selected.hardware = {
      h["roof_transfer"].get<double>(), h["inner_door"].get<double>(),
      h["seat_boarding"].get<double>(), h["station_closure"].get<double>()};
  auto journey = decode_freedom_journey_document_json(root["journey"].dump());
  if (!journey) return std::unexpected{journey.error()};
  FreedomStartingAssemblySaveDocument d{std::move(*journey),
                                        std::move(selected)};
  if (auto v = validate_freedom_starting_assembly_document(d); !v)
    return std::unexpected{v.error()};
  return d;
}
} // namespace apsis_drift
