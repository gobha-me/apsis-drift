#include "apsis_drift/freedom_journey_save.hpp"

#include <array>
#include <charconv>
#include <limits>
#include <nlohmann/json.hpp>
#include <numbers>
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
auto decimal(const Json& value) -> std::optional<std::uint64_t> {
  if (!value.is_string()) return {};
  const auto& s = value.get_ref<const std::string&>();
  std::uint64_t result{};
  const auto read = std::from_chars(s.data(), s.data() + s.size(), result);
  if (s.empty() || (s.size() > 1 && s.front() == '0') ||
      read.ec != std::errc{} || read.ptr != s.data() + s.size())
    return {};
  return result;
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

auto validate_freedom_journey_document(
    const FreedomJourneySaveDocument& document)
    -> std::expected<void, SaveSchemaError> {
  if (auto valid = validate_freedom_docking_document(document.voyage); !valid)
    return std::unexpected{valid.error()};
  const auto& docking = document.voyage.docking;
  if (!docking.attached || docking.target.ordinal != 1)
    return std::unexpected{
        failure("$.docking", "station actor requires attached Wayfarer at D1")};
  if (auto valid = validate_origin_walker(document.actor); !valid)
    return std::unexpected{failure("$.actor", valid.error())};
  return {};
}
auto make_freedom_journey_new_game_document(Seed seed)
    -> std::expected<FreedomJourneySaveDocument, SaveSchemaError> {
  auto origin = make_freedom_new_game_document(seed);
  auto system = generate_physical_origin_system(
      seed, kContinuousPhysicalLocalSystemGeneratorVersion);
  const auto station = generate_origin_station(seed);
  const auto geometry = origin_station_geometry(station);
  if (!system || !geometry)
    return std::unexpected{
        failure("$", "Origin physical catalog or geometry unavailable")};
  const OriginDockConstraint constraint{
      1, {station.id, 1}, wayfarer_frame().recipe, origin.state.tick};
  const auto body =
      release_origin_port(*system, station, *geometry, constraint);
  if (!body)
    return std::unexpected{
        failure("$.flight", "Origin D1 constraint unavailable")};
  FreedomJourneySaveDocument document{
      {{std::move(origin), *body, {}}, {1, {station.id, 1}, true}},
      {1, 1, {}, {}, std::numbers::pi / 2.0}};
  document.voyage.flight.model.physical_catalog = system->generator_version;
  document.voyage.flight.model.physical_ephemeris = system->ephemeris_version;
  if (auto valid = validate_freedom_journey_document(document); !valid)
    return std::unexpected{valid.error()};
  return document;
}
auto encode_freedom_journey_document_json(
    const FreedomJourneySaveDocument& document)
    -> std::expected<std::string, SaveSchemaError> {
  if (auto valid = validate_freedom_journey_document(document); !valid)
    return std::unexpected{valid.error()};
  const auto encoded = encode_freedom_docking_document_json(document.voyage);
  if (!encoded) return std::unexpected{encoded.error()};
  auto root = Json::parse(*encoded);
  root["format_version"] = kFreedomJourneySaveFormatVersion;
  root["state"]["location"] = "station_interior";
  const auto& actor = document.actor;
  const auto vector = [](RigidVector3 v) {
    return Json::array({v.x, v.y, v.z});
  };
  root["actor"] = {
      {"geometry_version", actor.geometry_version},
      {"actor_id", std::to_string(actor.actor_id)},
      {"frame",
       {{"kind", "station_interior"},
        {"station_id",
         std::to_string(document.voyage.docking.target.station.value)}}},
      {"foot_position_metres", vector(actor.foot_position_metres)},
      {"velocity_metres_per_second", vector(actor.velocity_metres_per_second)},
      {"heading_radians", actor.heading_radians}};
  auto text = root.dump(2) + '\n';
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "journey save exceeds limit"}};
  return text;
}
auto decode_freedom_journey_document_json(std::string_view text)
    -> std::expected<FreedomJourneySaveDocument, SaveSchemaError> {
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "journey save exceeds limit"}};
  if (!bounded_nesting(text))
    return std::unexpected{SaveSchemaError{
        SaveSchemaErrorCode::malformed_json, "$",
        "JSON nesting exceeds 64 levels or has unbalanced delimiters/quotes"}};
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
  if (root["format_version"] != kFreedomJourneySaveFormatVersion)
    return std::unexpected{SaveSchemaError{
        SaveSchemaErrorCode::unsupported_format_version, "$.format_version",
        "unsupported Freedom journey save version"}};
  auto shape = root;
  if (root.contains("world_owner")) shape.erase("craft_lineage");
  shape.erase("world_owner");
  if (!exact_fields(shape, {"application", "application_version",
                            "format_version", "mode", "recipe", "state",
                            "flight", "flight_model", "docking", "actor"}) ||
      !exact_fields(root["actor"],
                    {"geometry_version", "actor_id", "frame",
                     "foot_position_metres", "velocity_metres_per_second",
                     "heading_radians"}))
    return std::unexpected{
        failure("$.actor", "unexpected or missing actor field")};
  const auto& a = root["actor"];
  if (!a["geometry_version"].is_number_unsigned() ||
      a["geometry_version"] != 1 ||
      !exact_fields(a["frame"], {"kind", "station_id"}) ||
      a["frame"]["kind"] != "station_interior" ||
      !a["heading_radians"].is_number())
    return std::unexpected{
        failure("$.actor", "invalid geometry/frame/heading")};
  const auto actor_id = decimal(a["actor_id"]);
  const auto station_id = decimal(a["frame"]["station_id"]);
  const auto vector = [](const Json& value) -> std::optional<RigidVector3> {
    if (!value.is_array() || value.size() != 3) return {};
    for (const auto& coordinate : value)
      if (!coordinate.is_number()) return {};
    return RigidVector3{value[0].get<double>(), value[1].get<double>(),
                        value[2].get<double>()};
  };
  const auto foot = vector(a["foot_position_metres"]);
  const auto velocity = vector(a["velocity_metres_per_second"]);
  if (!actor_id || !station_id || !foot || !velocity ||
      !root["state"].is_object() || !root["state"].contains("location") ||
      root["state"]["location"] != "station_interior")
    return std::unexpected{
        failure("$.actor", "invalid identity/vector/location")};
  const OriginWalkerState actor{1, *actor_id, *foot, *velocity,
                                a["heading_radians"].get<double>()};
  root.erase("actor");
  root["format_version"] = kFreedomDockingSaveFormatVersion;
  root["state"]["location"] = "docked_at_origin_port";
  auto voyage = decode_freedom_docking_document_json(root.dump());
  if (!voyage) return std::unexpected{voyage.error()};
  if (voyage->docking.target.station.value != *station_id)
    return std::unexpected{failure("$.actor.frame.station_id",
                                   "actor frame differs from shared station")};
  FreedomJourneySaveDocument document{std::move(*voyage), actor};
  if (auto valid = validate_freedom_journey_document(document); !valid)
    return std::unexpected{valid.error()};
  return document;
}
} // namespace apsis_drift
