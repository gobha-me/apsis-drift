#include "apsis_drift/freedom_save.hpp"

#include <array>
#include <charconv>
#include <format>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "apsis_drift/version.hpp"

namespace apsis_drift {
namespace {

using Json = nlohmann::ordered_json;

[[nodiscard]] auto error(SaveSchemaErrorCode code, std::string path,
                         std::string detail) -> SaveSchemaError {
  return {code, std::move(path), std::move(detail)};
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

[[nodiscard]] auto versions_array(const SaveGeneratorVersions& v)
    -> std::array<std::uint32_t, 15> {
  return {v.seed_derivation,       v.planet_descriptor,
          v.origin_home_planet,    v.terrain_tiles,
          v.origin_station,        v.home_signal_contract,
          v.surface_signals,       v.local_sun,
          v.local_system,          v.analytic_ephemeris,
          v.intersystem_contract,  v.intersystem_jump,
          v.system_flight,         v.origin_station_flight,
          v.origin_system_contract};
}

[[nodiscard]] auto exact_fields(const Json& value,
                                std::initializer_list<std::string_view> fields,
                                std::string_view path)
    -> std::expected<void, SaveSchemaError> {
  if (!value.is_object())
    return std::unexpected{error(SaveSchemaErrorCode::invalid_type,
                                 std::string{path}, "expected an object")};
  if (value.size() != fields.size())
    return std::unexpected{error(SaveSchemaErrorCode::invalid_value,
                                 std::string{path},
                                 "unexpected or missing field")};
  for (const auto field : fields) {
    if (!value.contains(std::string{field}))
      return std::unexpected{error(SaveSchemaErrorCode::missing_field,
                                   std::format("{}.{}", path, field),
                                   "required field is missing")};
  }
  return {};
}

[[nodiscard]] auto read_decimal(const Json& object, std::string_view field,
                                std::string_view path)
    -> std::expected<std::uint64_t, SaveSchemaError> {
  const auto& value = object.at(std::string{field});
  const auto field_path = std::format("{}.{}", path, field);
  if (!value.is_string())
    return std::unexpected{error(SaveSchemaErrorCode::invalid_type, field_path,
                                 "expected a decimal string")};
  const auto& digits = value.get_ref<const std::string&>();
  if (digits.empty() || (digits.size() > 1 && digits.front() == '0'))
    return std::unexpected{error(SaveSchemaErrorCode::invalid_value, field_path,
                                 "expected canonical unsigned decimal")};
  std::uint64_t result{};
  const auto parsed =
      std::from_chars(digits.data(), digits.data() + digits.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size())
    return std::unexpected{error(SaveSchemaErrorCode::invalid_value, field_path,
                                 "unsigned decimal overflows or is malformed")};
  return result;
}

[[nodiscard]] auto parse_kind(const Json& value, std::string path)
    -> std::expected<SaveWorldDeltaKind, SaveSchemaError> {
  if (!value.is_string())
    return std::unexpected{error(SaveSchemaErrorCode::invalid_type, path,
                                 "expected a world-delta kind")};
  const auto kind = value.get<std::string>();
  if (kind == "discovered") return SaveWorldDeltaKind::discovered;
  if (kind == "collected") return SaveWorldDeltaKind::collected;
  if (kind == "completed") return SaveWorldDeltaKind::completed;
  if (kind == "removed") return SaveWorldDeltaKind::removed;
  return std::unexpected{error(SaveSchemaErrorCode::invalid_value, path,
                               "unknown world-delta kind")};
}

[[nodiscard]] auto kind_name(SaveWorldDeltaKind kind) -> std::string_view {
  switch (kind) {
    case SaveWorldDeltaKind::discovered: return "discovered";
    case SaveWorldDeltaKind::collected: return "collected";
    case SaveWorldDeltaKind::completed: return "completed";
    case SaveWorldDeltaKind::removed: return "removed";
  }
  return {};
}

[[nodiscard]] auto valid_key(std::string_view key) -> bool {
  if (key.empty() || key.size() > kMaximumSaveObjectKeyBytes) return false;
  for (const unsigned char c : key) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
        c == '_' || c == '.' || c == ':' || c == '/')
      continue;
    return false;
  }
  return true;
}

} // namespace

auto derive_freedom_craft_identity(Seed seed, std::uint64_t generation)
    -> std::expected<StarterCraftId, SaveSchemaError> {
  const auto starter = derive_seed(seed, SeedDomain::starter_craft).value;
  if (!generation) return StarterCraftId{starter};
  constexpr auto span = std::numeric_limits<std::uint64_t>::max();
  if (!starter || generation == span)
    return std::unexpected{error(SaveSchemaErrorCode::invalid_state,
                                 "$.craft_lineage.generation",
                                 "identity ring cannot advance")};
  // Addition modulo UINT64_MAX on [0, UINT64_MAX-1], then shift to the
  // nonzero ID set. The subtraction branch prevents unsigned overflow.
  const auto base = starter - 1;
  const auto remaining = span - base;
  const auto index =
      generation < remaining ? base + generation : generation - remaining;
  return StarterCraftId{index + 1};
}

auto make_freedom_new_game_document(Seed universe_seed) -> FreedomSaveDocument {
  auto recipe = make_save_recipe(universe_seed);
  return {
      recipe,
      {.station = recipe.origin_station,
       .craft = {derive_seed(universe_seed, SeedDomain::starter_craft).value},
       .tick = 0}};
}

auto validate_freedom_save_document(const FreedomSaveDocument& document)
    -> std::expected<void, SaveSchemaError> {
  const auto& recipe = document.recipe;
  if (recipe.generator_versions != current_save_generator_versions())
    return std::unexpected{error(
        SaveSchemaErrorCode::incompatible_generator_version,
        "$.recipe.generator_versions", "generator versions are incompatible")};
  if (recipe.origin_system_ordinal != kOriginSystemOrdinal ||
      recipe.home_planet_ordinal != kOriginHomePlanetOrdinal ||
      recipe.active_planet_ordinal != kOriginHomePlanetOrdinal)
    return std::unexpected{
        error(SaveSchemaErrorCode::invalid_value, "$.recipe",
              "Freedom station start requires origin ordinals")};
  if (recipe != make_save_recipe(recipe.universe_seed))
    return std::unexpected{error(SaveSchemaErrorCode::identity_mismatch,
                                 "$.recipe", "recipe does not match its seed")};
  if (document.lineage &&
      (document.lineage->version != kFreedomCraftLineageVersion ||
       !document.lineage->generation))
    return std::unexpected{error(SaveSchemaErrorCode::invalid_state,
                                 "$.craft_lineage",
                                 "unsupported or initial lineage selection")};
  const auto craft = derive_freedom_craft_identity(
      recipe.universe_seed,
      document.lineage ? document.lineage->generation : 0);
  if (!craft) return std::unexpected{craft.error()};
  if (document.state.station != recipe.origin_station ||
      document.state.craft != *craft)
    return std::unexpected{
        error(SaveSchemaErrorCode::identity_mismatch, "$.state",
              "station or craft identity does not match its seed")};
  if (document.state.tick == std::numeric_limits<SimulationTick>::max())
    return std::unexpected{error(SaveSchemaErrorCode::invalid_state,
                                 "$.state.tick",
                                 "clock cannot advance without overflow")};
  if (document.state.discoveries.size() > kMaximumSaveDiscoveries ||
      document.state.world_deltas.size() > kMaximumSaveWorldDeltas)
    return std::unexpected{error(SaveSchemaErrorCode::invalid_state, "$.state",
                                 "history exceeds save-format bounds")};
  std::unordered_set<std::uint64_t> signal_ids;
  for (const auto& discovery : document.state.discoveries) {
    if (discovery.tick > document.state.tick ||
        !signal_ids.insert(discovery.signal.value).second)
      return std::unexpected{error(SaveSchemaErrorCode::invalid_state,
                                   "$.state.discoveries",
                                   "invalid discovery history")};
  }
  for (const auto& delta : document.state.world_deltas) {
    if (delta.tick > document.state.tick || !valid_key(delta.object_key) ||
        kind_name(delta.kind).empty())
      return std::unexpected{error(SaveSchemaErrorCode::invalid_state,
                                   "$.state.world_deltas",
                                   "invalid world-delta history")};
  }
  return {};
}

auto encode_freedom_save_document_json(const FreedomSaveDocument& document)
    -> std::expected<std::string, SaveSchemaError> {
  if (auto valid = validate_freedom_save_document(document); !valid)
    return std::unexpected{valid.error()};
  Json discoveries = Json::array();
  for (const auto& entry : document.state.discoveries)
    discoveries.push_back({{"signal_id", std::to_string(entry.signal.value)},
                           {"tick", std::to_string(entry.tick)}});
  Json deltas = Json::array();
  for (const auto& entry : document.state.world_deltas)
    deltas.push_back({{"object_key", entry.object_key},
                      {"kind", kind_name(entry.kind)},
                      {"tick", std::to_string(entry.tick)}});
  const auto& recipe = document.recipe;
  Json root = {
      {"application", kSaveApplication},
      {"application_version", kApplicationVersion},
      {"format_version", document.lineage
                             ? kFreedomCraftLineageSaveFormatVersion
                             : kFreedomSaveFormatVersion},
      {"mode", "freedom"},
      {"recipe",
       {{"universe_seed", std::to_string(recipe.universe_seed.value)},
        {"origin_system_ordinal", std::to_string(recipe.origin_system_ordinal)},
        {"home_planet_ordinal", std::to_string(recipe.home_planet_ordinal)},
        {"active_planet_ordinal", std::to_string(recipe.active_planet_ordinal)},
        {"generator_versions", versions_array(recipe.generator_versions)},
        {"origin_station_id", std::to_string(recipe.origin_station.value)},
        {"home_planet_id", std::to_string(recipe.home_planet.value)}}},
      {"state",
       {{"location", "docked_at_origin"},
        {"station_id", std::to_string(document.state.station.value)},
        {"starter_craft_id", std::to_string(document.state.craft.value)},
        {"tick", std::to_string(document.state.tick)},
        {"discoveries", std::move(discoveries)},
        {"world_deltas", std::move(deltas)}}}};
  if (document.lineage)
    root["craft_lineage"] = {
        {"version", document.lineage->version},
        {"generation", std::to_string(document.lineage->generation)}};
  auto encoded = root.dump(2);
  encoded.push_back('\n');
  if (encoded.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{error(SaveSchemaErrorCode::document_too_large, "$",
                                 "encoded save exceeds the byte bound")};
  return encoded;
}

auto decode_freedom_save_document_json(std::string_view json_text)
    -> std::expected<FreedomSaveDocument, SaveSchemaError> {
  if (json_text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{error(SaveSchemaErrorCode::document_too_large, "$",
                                 "save exceeds the byte bound")};
  if (!bounded_nesting(json_text))
    return std::unexpected{
        error(SaveSchemaErrorCode::malformed_json, "$",
              "unbalanced or excessive origin JSON nesting")};
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
    root =
        Json::parse(json_text.begin(), json_text.end(), callback, true, false);
  } catch (const nlohmann::json::exception& exception) {
    return std::unexpected{
        error(SaveSchemaErrorCode::malformed_json, "$", exception.what())};
  }
  if (duplicate)
    return std::unexpected{error(SaveSchemaErrorCode::duplicate_key, "$",
                                 "JSON objects cannot repeat a key")};
  const bool lineage =
      root.is_object() && root.contains("format_version") &&
      root["format_version"].is_number_unsigned() &&
      root["format_version"] == kFreedomCraftLineageSaveFormatVersion;
  const auto fields =
      lineage ? exact_fields(root,
                             {"application", "application_version",
                              "format_version", "mode", "recipe", "state",
                              "craft_lineage"},
                             "$")
              : exact_fields(root,
                             {"application", "application_version",
                              "format_version", "mode", "recipe", "state"},
                             "$");
  if (!fields) return std::unexpected{fields.error()};
  if (!root.at("application").is_string() ||
      root.at("application") != kSaveApplication ||
      !root.at("mode").is_string() || root.at("mode") != "freedom")
    return std::unexpected{error(SaveSchemaErrorCode::invalid_value, "$",
                                 "application or mode is not Freedom")};
  if (!root.at("format_version").is_number_unsigned() ||
      (root.at("format_version") != kFreedomSaveFormatVersion &&
       root.at("format_version") != kFreedomCraftLineageSaveFormatVersion))
    return std::unexpected{
        error(SaveSchemaErrorCode::unsupported_format_version,
              "$.format_version", "unsupported Freedom save version")};
  if (!root.at("application_version").is_string() ||
      root.at("application_version").get<std::string>().empty() ||
      root.at("application_version").get<std::string>().size() >
          kMaximumSaveApplicationVersionBytes)
    return std::unexpected{error(SaveSchemaErrorCode::invalid_value,
                                 "$.application_version",
                                 "invalid writer version")};
  for (const unsigned char c :
       root.at("application_version").get<std::string>()) {
    if (c < 0x20U || c > 0x7eU)
      return std::unexpected{error(SaveSchemaErrorCode::invalid_value,
                                   "$.application_version",
                                   "invalid writer version")};
  }
  const auto& recipe_json = root.at("recipe");
  if (auto fields = exact_fields(recipe_json,
                                 {"universe_seed", "origin_system_ordinal",
                                  "home_planet_ordinal",
                                  "active_planet_ordinal", "generator_versions",
                                  "origin_station_id", "home_planet_id"},
                                 "$.recipe");
      !fields)
    return std::unexpected{fields.error()};
  const auto& state_json = root.at("state");
  if (auto fields = exact_fields(state_json,
                                 {"location", "station_id", "starter_craft_id",
                                  "tick", "discoveries", "world_deltas"},
                                 "$.state");
      !fields)
    return std::unexpected{fields.error()};
  auto seed = read_decimal(recipe_json, "universe_seed", "$.recipe");
  auto system = read_decimal(recipe_json, "origin_system_ordinal", "$.recipe");
  auto home = read_decimal(recipe_json, "home_planet_ordinal", "$.recipe");
  auto active = read_decimal(recipe_json, "active_planet_ordinal", "$.recipe");
  auto station = read_decimal(recipe_json, "origin_station_id", "$.recipe");
  auto planet = read_decimal(recipe_json, "home_planet_id", "$.recipe");
  if (!seed) return std::unexpected{seed.error()};
  if (!system) return std::unexpected{system.error()};
  if (!home) return std::unexpected{home.error()};
  if (!active) return std::unexpected{active.error()};
  if (!station) return std::unexpected{station.error()};
  if (!planet) return std::unexpected{planet.error()};
  const auto& versions = recipe_json.at("generator_versions");
  if (!versions.is_array() || versions.size() != 15)
    return std::unexpected{error(SaveSchemaErrorCode::invalid_type,
                                 "$.recipe.generator_versions",
                                 "expected 15 generator versions")};
  const auto expected_versions =
      versions_array(current_save_generator_versions());
  for (std::size_t index = 0; index < expected_versions.size(); ++index) {
    if (!versions[index].is_number_unsigned() ||
        versions[index] != expected_versions[index])
      return std::unexpected{
          error(SaveSchemaErrorCode::incompatible_generator_version,
                "$.recipe.generator_versions",
                "generator versions are incompatible")};
  }
  if (*system != kOriginSystemOrdinal || *home != kOriginHomePlanetOrdinal ||
      *active != kOriginHomePlanetOrdinal)
    return std::unexpected{error(SaveSchemaErrorCode::invalid_value, "$.recipe",
                                 "Freedom station ordinals are invalid")};
  auto document = make_freedom_new_game_document(Seed{*seed});
  if (lineage) {
    const auto& selected = root.at("craft_lineage");
    if (auto shape = exact_fields(selected, {"version", "generation"},
                                  "$.craft_lineage");
        !shape)
      return std::unexpected{shape.error()};
    if (!selected.at("version").is_number_unsigned() ||
        selected.at("version") != kFreedomCraftLineageVersion)
      return std::unexpected{
          error(SaveSchemaErrorCode::unsupported_format_version,
                "$.craft_lineage.version", "unsupported lineage rule")};
    auto generation = read_decimal(selected, "generation", "$.craft_lineage");
    if (!generation) return std::unexpected{generation.error()};
    document.lineage = FreedomCraftLineage{1, *generation};
  }

  if (*station != document.recipe.origin_station.value ||
      *planet != document.recipe.home_planet.value)
    return std::unexpected{error(SaveSchemaErrorCode::identity_mismatch,
                                 "$.recipe",
                                 "recipe identities do not match seed")};
  if (!state_json.at("location").is_string() ||
      state_json.at("location") != "docked_at_origin")
    return std::unexpected{error(SaveSchemaErrorCode::invalid_state,
                                 "$.state.location",
                                 "unsupported Freedom location")};
  auto state_station = read_decimal(state_json, "station_id", "$.state");
  auto craft = read_decimal(state_json, "starter_craft_id", "$.state");
  auto tick = read_decimal(state_json, "tick", "$.state");
  if (!state_station) return std::unexpected{state_station.error()};
  if (!craft) return std::unexpected{craft.error()};
  if (!tick) return std::unexpected{tick.error()};
  document.state.station = {*state_station};
  document.state.craft = {*craft};
  document.state.tick = *tick;
  const auto& discoveries = state_json.at("discoveries");
  const auto& deltas = state_json.at("world_deltas");
  if (!discoveries.is_array() || discoveries.size() > kMaximumSaveDiscoveries ||
      !deltas.is_array() || deltas.size() > kMaximumSaveWorldDeltas)
    return std::unexpected{error(SaveSchemaErrorCode::invalid_state, "$.state",
                                 "history shape or bound is invalid")};
  for (std::size_t index = 0; index < discoveries.size(); ++index) {
    const auto path = std::format("$.state.discoveries[{}]", index);
    if (auto fields =
            exact_fields(discoveries[index], {"signal_id", "tick"}, path);
        !fields)
      return std::unexpected{fields.error()};
    auto id = read_decimal(discoveries[index], "signal_id", path);
    auto at = read_decimal(discoveries[index], "tick", path);
    if (!id) return std::unexpected{id.error()};
    if (!at) return std::unexpected{at.error()};
    document.state.discoveries.push_back({SurfaceSignalId{*id}, *at});
  }
  for (std::size_t index = 0; index < deltas.size(); ++index) {
    const auto path = std::format("$.state.world_deltas[{}]", index);
    if (auto fields =
            exact_fields(deltas[index], {"object_key", "kind", "tick"}, path);
        !fields)
      return std::unexpected{fields.error()};
    if (!deltas[index].at("object_key").is_string())
      return std::unexpected{error(SaveSchemaErrorCode::invalid_type, path,
                                   "expected object key string")};
    auto kind = parse_kind(deltas[index].at("kind"), path + ".kind");
    auto at = read_decimal(deltas[index], "tick", path);
    if (!kind) return std::unexpected{kind.error()};
    if (!at) return std::unexpected{at.error()};
    document.state.world_deltas.push_back(
        {deltas[index].at("object_key").get<std::string>(), *kind, *at});
  }
  if (auto valid = validate_freedom_save_document(document); !valid)
    return std::unexpected{valid.error()};
  return document;
}

} // namespace apsis_drift
