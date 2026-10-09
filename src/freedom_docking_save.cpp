#include "apsis_drift/freedom_docking_save.hpp"

#include <charconv>
#include <limits>
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
} // namespace

auto validate_freedom_docking_document(
    const FreedomDockingSaveDocument& document)
    -> std::expected<void, SaveSchemaError> {
  auto hydrated = hydrate_freedom_flight_document(document.flight);
  if (!hydrated) return std::unexpected{hydrated.error()};
  const auto& state = document.docking;
  const auto& body = document.flight.flight;
  const auto station =
      generate_origin_station(document.flight.origin.recipe.universe_seed);
  if (body.frame.system != generate_first_intersystem_identities(
                               document.flight.origin.recipe.universe_seed)
                               .origin_system ||
      body.frame.planet != station.orbit.host_planet)
    return std::unexpected{
        failure("$.docking", "Origin Station is not in the current frame")};
  const auto geometry = origin_station_geometry(station);
  if (!geometry || state.geometry_version != geometry->version ||
      body.craft != CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion})
    return std::unexpected{
        failure("$.docking", "unsupported geometry or craft")};
  if (state.target.station != station.id || state.target.ordinal < 1 ||
      state.target.ordinal > geometry->ports.size())
    return std::unexpected{
        failure("$.docking.target", "unknown or stale port")};
  if (state.attached) {
    const OriginDockConstraint constraint{state.geometry_version, state.target,
                                          body.craft, body.tick};
    const auto expected =
        release_origin_port(hydrated->system, station, *geometry, constraint);
    if (!expected || *expected != body)
      return std::unexpected{failure("$.flight",
                                     "attached body differs from its "
                                     "same-tick port constraint")};
  }
  return {};
}

auto encode_freedom_docking_document_json(
    const FreedomDockingSaveDocument& document)
    -> std::expected<std::string, SaveSchemaError> {
  if (auto valid = validate_freedom_docking_document(document); !valid)
    return std::unexpected{valid.error()};
  auto encoded = encode_freedom_flight_document_json(document.flight);
  if (!encoded) return std::unexpected{encoded.error()};
  Json root = Json::parse(*encoded);
  root["format_version"] = kFreedomDockingSaveFormatVersion;
  root["state"]["location"] =
      document.docking.attached ? "docked_at_origin_port" : "planetary_flight";
  root["docking"] = {
      {"geometry_version", document.docking.geometry_version},
      {"target",
       {{"station_id", std::to_string(document.docking.target.station.value)},
        {"ordinal", document.docking.target.ordinal}}},
      {"attached", document.docking.attached}};
  auto text = root.dump(2) + '\n';
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "docking save exceeds limit"}};
  return text;
}

auto decode_freedom_docking_document_json(std::string_view text)
    -> std::expected<FreedomDockingSaveDocument, SaveSchemaError> {
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "docking save exceeds limit"}};
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
  if (root["format_version"] != kFreedomDockingSaveFormatVersion)
    return std::unexpected{SaveSchemaError{
        SaveSchemaErrorCode::unsupported_format_version, "$.format_version",
        "unsupported Freedom docking save version"}};
  auto shape = root;
  shape.erase("world_owner");
  if (!exact_fields(shape, {"application", "application_version",
                            "format_version", "mode", "recipe", "state",
                            "flight", "flight_model", "docking"}) ||
      !exact_fields(root["docking"],
                    {"geometry_version", "target", "attached"}))
    return std::unexpected{failure("$", "unexpected or missing docking field")};
  const auto& d = root["docking"];
  if (!exact_fields(d["target"], {"station_id", "ordinal"}) ||
      !d["geometry_version"].is_number_unsigned() ||
      d["geometry_version"] != 1 || !d["attached"].is_boolean() ||
      !d["target"]["ordinal"].is_number_unsigned() ||
      d["target"]["ordinal"].get<std::uint64_t>() > 2)
    return std::unexpected{
        failure("$.docking", "invalid port/geometry/attachment")};
  const auto station_id = decimal(d["target"]["station_id"]);
  if (!station_id)
    return std::unexpected{
        failure("$.docking.target.station_id", "invalid identity")};
  const FreedomDockingState state{1,
                                  {OriginStationId{*station_id},
                                   d["target"]["ordinal"].get<std::uint32_t>()},
                                  d["attached"].get<bool>()};
  if (!root["state"].is_object() || !root["state"].contains("location") ||
      root["state"]["location"] !=
          (state.attached ? "docked_at_origin_port" : "planetary_flight"))
    return std::unexpected{
        failure("$.state.location", "attachment/location mismatch")};
  root.erase("docking");
  root["format_version"] = root.contains("world_owner")
                               ? kFreedomActiveFlightSaveFormatVersion
                               : kFreedomFlightSaveFormatVersion;
  root["state"]["location"] = "planetary_flight";
  auto flight = decode_freedom_flight_document_json(root.dump());
  if (!flight) return std::unexpected{flight.error()};
  FreedomDockingSaveDocument document{std::move(*flight), state};
  if (auto valid = validate_freedom_docking_document(document); !valid)
    return std::unexpected{valid.error()};
  return document;
}
} // namespace apsis_drift
