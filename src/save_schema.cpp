#include "apsis_drift/save_schema.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "apsis_drift/celestial.hpp"
#include "apsis_drift/coordinates.hpp"
#include "apsis_drift/planet.hpp"
#include "apsis_drift/seed.hpp"
#include "apsis_drift/terrain_tiles.hpp"

namespace apsis_drift {
namespace {

using Json = nlohmann::ordered_json;

[[nodiscard]] auto failure(SaveSchemaErrorCode code, std::string path,
                           std::string detail) -> SaveSchemaError {
  return SaveSchemaError{code, std::move(path), std::move(detail)};
}

[[nodiscard]] auto decimal(std::uint64_t value) -> std::string {
  return std::to_string(value);
}

[[nodiscard]] auto decimal(double value) -> std::string {
  std::array<char, 64> buffer{};
  const auto result = std::to_chars(
      buffer.data(), buffer.data() + buffer.size(), value,
      std::chars_format::general, std::numeric_limits<double>::max_digits10);
  if (result.ec != std::errc{}) return {};
  return {buffer.data(), result.ptr};
}

template <typename Id>
[[nodiscard]] auto encoded_id(std::string_view prefix, Id id) -> std::string {
  return std::format("{}{:016x}", prefix, id.value);
}

[[nodiscard]] auto valid_location(OriginLocation value) noexcept -> bool {
  return value == OriginLocation::docked_at_origin ||
         value == OriginLocation::in_flight;
}

[[nodiscard]] auto valid_objective(FirstObjectiveStatus value) noexcept
    -> bool {
  return value == FirstObjectiveStatus::offered ||
         value == FirstObjectiveStatus::active ||
         value == FirstObjectiveStatus::completed;
}

[[nodiscard]] auto valid_delta_kind(SaveWorldDeltaKind value) noexcept -> bool {
  switch (value) {
    case SaveWorldDeltaKind::discovered:
    case SaveWorldDeltaKind::collected:
    case SaveWorldDeltaKind::completed:
    case SaveWorldDeltaKind::removed:
      return true;
  }
  return false;
}

[[nodiscard]] auto location_name(OriginLocation value) -> std::string_view {
  switch (value) {
    case OriginLocation::docked_at_origin:
      return "docked_at_origin";
    case OriginLocation::in_flight:
      return "in_flight";
  }
  return "unknown";
}

[[nodiscard]] auto objective_name(FirstObjectiveStatus value)
    -> std::string_view {
  switch (value) {
    case FirstObjectiveStatus::offered:
      return "offered";
    case FirstObjectiveStatus::active:
      return "active";
    case FirstObjectiveStatus::completed:
      return "completed";
  }
  return "unknown";
}

[[nodiscard]] auto mode_name(FlightMode value) -> std::string_view {
  switch (value) {
    case FlightMode::manual:
      return "manual";
    case FlightMode::autopilot:
      return "autopilot";
  }
  return "unknown";
}

[[nodiscard]] auto regime_name(FlightRegime value) -> std::string_view {
  switch (value) {
    case FlightRegime::orbital:
      return "orbital";
    case FlightRegime::atmospheric:
      return "atmospheric";
    case FlightRegime::terrain_flight:
      return "terrain_flight";
  }
  return "unknown";
}

[[nodiscard]] auto delta_kind_name(SaveWorldDeltaKind value)
    -> std::string_view {
  switch (value) {
    case SaveWorldDeltaKind::discovered:
      return "discovered";
    case SaveWorldDeltaKind::collected:
      return "collected";
    case SaveWorldDeltaKind::completed:
      return "completed";
    case SaveWorldDeltaKind::removed:
      return "removed";
  }
  return "unknown";
}

[[nodiscard]] auto valid_object_key(std::string_view key) noexcept -> bool {
  if (key.empty() || key.size() > kMaximumSaveObjectKeyBytes) return false;
  for (const char raw_character : key) {
    const auto character = static_cast<unsigned char>(raw_character);
    const bool lower = character >= 'a' && character <= 'z';
    const bool digit = character >= '0' && character <= '9';
    if (!lower && !digit && character != '-' && character != '_' &&
        character != '.' && character != ':' && character != '/') {
      return false;
    }
  }
  return true;
}

[[nodiscard]] auto validate_flight(const SaveRecipe& recipe,
                                   const PlanetaryFlightState& state)
    -> std::expected<void, SaveSchemaError> {
  if (state.planet != recipe.active_planet) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::identity_mismatch, "$.state.flight.planet_id",
        "flight planet does not match the active generated planet")};
  }
  const auto system_seed = derive_seed(recipe.universe_seed, SeedDomain::system,
                                       recipe.origin_system_ordinal);
  const auto planet_seed = derive_seed(system_seed, SeedDomain::planet,
                                       recipe.active_planet_ordinal);
  const auto planet = generate_planet_descriptor(planet_seed);
  if (!validate_planetary_flight_state(planet, state)) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::invalid_state, "$.state.flight",
        "flight state violates the authoritative planetary flight contract")};
  }
  return {};
}

[[nodiscard]] auto require_field(const Json& object, std::string_view name,
                                 std::string path)
    -> std::expected<const Json*, SaveSchemaError> {
  if (!object.is_object()) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                   std::move(path), "expected an object")};
  }
  const auto found = object.find(std::string{name});
  if (found == object.end()) {
    return std::unexpected{failure(SaveSchemaErrorCode::missing_field,
                                   std::format("{}.{}", path, name),
                                   "required field is missing")};
  }
  return &*found;
}

[[nodiscard]] auto read_object(const Json& parent, std::string_view name,
                               std::string path)
    -> std::expected<const Json*, SaveSchemaError> {
  auto value = require_field(parent, name, path);
  if (!value) return std::unexpected{value.error()};
  if (!(*value)->is_object()) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                   std::format("{}.{}", path, name),
                                   "expected an object")};
  }
  return *value;
}

[[nodiscard]] auto read_array(const Json& parent, std::string_view name,
                              std::string path)
    -> std::expected<const Json*, SaveSchemaError> {
  auto value = require_field(parent, name, path);
  if (!value) return std::unexpected{value.error()};
  if (!(*value)->is_array()) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                   std::format("{}.{}", path, name),
                                   "expected an array")};
  }
  return *value;
}

[[nodiscard]] auto read_string(const Json& parent, std::string_view name,
                               std::string path)
    -> std::expected<std::string, SaveSchemaError> {
  auto value = require_field(parent, name, path);
  if (!value) return std::unexpected{value.error()};
  if (!(*value)->is_string()) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                   std::format("{}.{}", path, name),
                                   "expected a string")};
  }
  return (*value)->get<std::string>();
}

[[nodiscard]] auto read_bool(const Json& parent, std::string_view name,
                             std::string path)
    -> std::expected<bool, SaveSchemaError> {
  auto value = require_field(parent, name, path);
  if (!value) return std::unexpected{value.error()};
  if (!(*value)->is_boolean()) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                   std::format("{}.{}", path, name),
                                   "expected a boolean")};
  }
  return (*value)->get<bool>();
}

[[nodiscard]] auto read_u32(const Json& parent, std::string_view name,
                            std::string path)
    -> std::expected<std::uint32_t, SaveSchemaError> {
  auto value = require_field(parent, name, path);
  if (!value) return std::unexpected{value.error()};
  if (!(*value)->is_number_unsigned()) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                   std::format("{}.{}", path, name),
                                   "expected an unsigned integer")};
  }
  const auto number = (*value)->get<std::uint64_t>();
  if (number > std::numeric_limits<std::uint32_t>::max()) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                   std::format("{}.{}", path, name),
                                   "integer exceeds uint32 range")};
  }
  return static_cast<std::uint32_t>(number);
}

[[nodiscard]] auto parse_u64(std::string_view value, std::string path)
    -> std::expected<std::uint64_t, SaveSchemaError> {
  std::uint64_t result{};
  const auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (value.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != value.data() + value.size() ||
      (value.size() > 1 && value.front() == '0')) {
    return std::unexpected{
        failure(SaveSchemaErrorCode::invalid_value, std::move(path),
                "expected a canonical unsigned decimal string")};
  }
  return result;
}

[[nodiscard]] auto read_u64(const Json& parent, std::string_view name,
                            std::string path)
    -> std::expected<std::uint64_t, SaveSchemaError> {
  const auto field_path = std::format("{}.{}", path, name);
  auto value = read_string(parent, name, path);
  if (!value) return std::unexpected{value.error()};
  return parse_u64(*value, field_path);
}

[[nodiscard]] auto parse_double(std::string_view value, std::string path)
    -> std::expected<double, SaveSchemaError> {
  double result{};
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(),
                                      result, std::chars_format::general);
  if (value.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != value.data() + value.size() || !std::isfinite(result)) {
    return std::unexpected{
        failure(SaveSchemaErrorCode::invalid_value, std::move(path),
                "expected a finite decimal floating-point string")};
  }
  return result;
}

[[nodiscard]] auto read_double(const Json& parent, std::string_view name,
                               std::string path)
    -> std::expected<double, SaveSchemaError> {
  const auto field_path = std::format("{}.{}", path, name);
  auto value = read_string(parent, name, path);
  if (!value) return std::unexpected{value.error()};
  return parse_double(*value, field_path);
}

template <typename Id>
[[nodiscard]] auto parse_id(std::string_view value, std::string_view prefix,
                            std::string path)
    -> std::expected<Id, SaveSchemaError> {
  if (value.size() != prefix.size() + 16 ||
      value.substr(0, prefix.size()) != prefix) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                   std::move(path),
                                   "identifier has the wrong prefix or width")};
  }
  const auto digits = value.substr(prefix.size());
  for (const char digit : digits) {
    if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f'))) {
      return std::unexpected{
          failure(SaveSchemaErrorCode::invalid_value, std::move(path),
                  "identifier must use lowercase hexadecimal digits")};
    }
  }
  std::uint64_t parsed{};
  const auto result =
      std::from_chars(digits.data(), digits.data() + digits.size(), parsed, 16);
  if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size()) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                   std::move(path),
                                   "identifier is outside uint64 range")};
  }
  return Id{parsed};
}

template <typename Id>
[[nodiscard]] auto read_id(const Json& parent, std::string_view name,
                           std::string path, std::string_view prefix)
    -> std::expected<Id, SaveSchemaError> {
  const auto field_path = std::format("{}.{}", path, name);
  auto value = read_string(parent, name, path);
  if (!value) return std::unexpected{value.error()};
  return parse_id<Id>(*value, prefix, field_path);
}

[[nodiscard]] auto read_location(const Json& parent, std::string_view name,
                                 std::string path)
    -> std::expected<OriginLocation, SaveSchemaError> {
  auto value = read_string(parent, name, path);
  if (!value) return std::unexpected{value.error()};
  if (*value == "docked_at_origin") return OriginLocation::docked_at_origin;
  if (*value == "in_flight") return OriginLocation::in_flight;
  return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                 std::format("{}.{}", path, name),
                                 "unknown origin location")};
}

[[nodiscard]] auto read_objective(const Json& parent, std::string_view name,
                                  std::string path)
    -> std::expected<FirstObjectiveStatus, SaveSchemaError> {
  auto value = read_string(parent, name, path);
  if (!value) return std::unexpected{value.error()};
  if (*value == "offered") return FirstObjectiveStatus::offered;
  if (*value == "active") return FirstObjectiveStatus::active;
  if (*value == "completed") return FirstObjectiveStatus::completed;
  return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                 std::format("{}.{}", path, name),
                                 "unknown first-objective state")};
}

[[nodiscard]] auto read_mode(const Json& parent, std::string_view name,
                             std::string path)
    -> std::expected<FlightMode, SaveSchemaError> {
  auto value = read_string(parent, name, path);
  if (!value) return std::unexpected{value.error()};
  if (*value == "manual") return FlightMode::manual;
  if (*value == "autopilot") return FlightMode::autopilot;
  return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                 std::format("{}.{}", path, name),
                                 "unknown flight mode")};
}

[[nodiscard]] auto read_regime(const Json& parent, std::string_view name,
                               std::string path)
    -> std::expected<FlightRegime, SaveSchemaError> {
  auto value = read_string(parent, name, path);
  if (!value) return std::unexpected{value.error()};
  if (*value == "orbital") return FlightRegime::orbital;
  if (*value == "atmospheric") return FlightRegime::atmospheric;
  if (*value == "terrain_flight") return FlightRegime::terrain_flight;
  return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                 std::format("{}.{}", path, name),
                                 "unknown flight regime")};
}

[[nodiscard]] auto read_delta_kind(const Json& parent, std::string_view name,
                                   std::string path)
    -> std::expected<SaveWorldDeltaKind, SaveSchemaError> {
  auto value = read_string(parent, name, path);
  if (!value) return std::unexpected{value.error()};
  if (*value == "discovered") return SaveWorldDeltaKind::discovered;
  if (*value == "collected") return SaveWorldDeltaKind::collected;
  if (*value == "completed") return SaveWorldDeltaKind::completed;
  if (*value == "removed") return SaveWorldDeltaKind::removed;
  return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                 std::format("{}.{}", path, name),
                                 "unknown world-delta kind")};
}

[[nodiscard]] auto encode_flight(const PlanetaryFlightState& state) -> Json {
  Json transition = nullptr;
  if (state.last_transition) {
    transition = Json{{"from", regime_name(state.last_transition->from)},
                      {"to", regime_name(state.last_transition->to)},
                      {"tick", decimal(state.last_transition->tick)}};
  }
  return Json{
      {"tick", decimal(state.tick)},
      {"planet_id", encoded_id("planet-", state.planet)},
      {"pose",
       Json{{"latitude_radians", decimal(state.pose.position.latitude_radians)},
            {"longitude_radians",
             decimal(state.pose.position.longitude_radians)},
            {"altitude_metres", decimal(state.pose.position.altitude_metres)},
            {"heading_radians", decimal(state.pose.heading_radians)}}},
      {"velocity", Json{{"east_metres_per_second",
                         decimal(state.velocity.east_metres_per_second)},
                        {"north_metres_per_second",
                         decimal(state.velocity.north_metres_per_second)},
                        {"up_metres_per_second",
                         decimal(state.velocity.up_metres_per_second)}}},
      {"clearance_metres", decimal(state.clearance_metres)},
      {"mode", mode_name(state.mode)},
      {"controls", Json{{"forward", state.controls.forward},
                        {"backward", state.controls.backward},
                        {"turn_left", state.controls.turn_left},
                        {"turn_right", state.controls.turn_right},
                        {"strafe_left", state.controls.strafe_left},
                        {"strafe_right", state.controls.strafe_right},
                        {"rise", state.controls.rise},
                        {"fall", state.controls.fall}}},
      {"regime", regime_name(state.regime)},
      {"last_transition", std::move(transition)},
  };
}

[[nodiscard]] auto decode_flight(const Json& json)
    -> std::expected<PlanetaryFlightState, SaveSchemaError> {
  constexpr std::string_view path{"$.state.flight"};
  if (!json.is_object()) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                   std::string{path}, "expected an object")};
  }
  auto tick = read_u64(json, "tick", std::string{path});
  auto planet =
      read_id<PlanetId>(json, "planet_id", std::string{path}, "planet-");
  auto pose = read_object(json, "pose", std::string{path});
  auto velocity = read_object(json, "velocity", std::string{path});
  auto clearance = read_double(json, "clearance_metres", std::string{path});
  auto mode = read_mode(json, "mode", std::string{path});
  auto controls = read_object(json, "controls", std::string{path});
  auto regime = read_regime(json, "regime", std::string{path});
  auto transition = require_field(json, "last_transition", std::string{path});
  if (!tick) return std::unexpected{tick.error()};
  if (!planet) return std::unexpected{planet.error()};
  if (!pose) return std::unexpected{pose.error()};
  if (!velocity) return std::unexpected{velocity.error()};
  if (!clearance) return std::unexpected{clearance.error()};
  if (!mode) return std::unexpected{mode.error()};
  if (!controls) return std::unexpected{controls.error()};
  if (!regime) return std::unexpected{regime.error()};
  if (!transition) return std::unexpected{transition.error()};

  auto latitude =
      read_double(**pose, "latitude_radians", "$.state.flight.pose");
  auto longitude =
      read_double(**pose, "longitude_radians", "$.state.flight.pose");
  auto altitude = read_double(**pose, "altitude_metres", "$.state.flight.pose");
  auto heading = read_double(**pose, "heading_radians", "$.state.flight.pose");
  auto east = read_double(**velocity, "east_metres_per_second",
                          "$.state.flight.velocity");
  auto north = read_double(**velocity, "north_metres_per_second",
                           "$.state.flight.velocity");
  auto up = read_double(**velocity, "up_metres_per_second",
                        "$.state.flight.velocity");
  auto forward = read_bool(**controls, "forward", "$.state.flight.controls");
  auto backward = read_bool(**controls, "backward", "$.state.flight.controls");
  auto turn_left =
      read_bool(**controls, "turn_left", "$.state.flight.controls");
  auto turn_right =
      read_bool(**controls, "turn_right", "$.state.flight.controls");
  auto strafe_left =
      read_bool(**controls, "strafe_left", "$.state.flight.controls");
  auto strafe_right =
      read_bool(**controls, "strafe_right", "$.state.flight.controls");
  auto rise = read_bool(**controls, "rise", "$.state.flight.controls");
  auto fall = read_bool(**controls, "fall", "$.state.flight.controls");
  if (!latitude) return std::unexpected{latitude.error()};
  if (!longitude) return std::unexpected{longitude.error()};
  if (!altitude) return std::unexpected{altitude.error()};
  if (!heading) return std::unexpected{heading.error()};
  if (!east) return std::unexpected{east.error()};
  if (!north) return std::unexpected{north.error()};
  if (!up) return std::unexpected{up.error()};
  if (!forward) return std::unexpected{forward.error()};
  if (!backward) return std::unexpected{backward.error()};
  if (!turn_left) return std::unexpected{turn_left.error()};
  if (!turn_right) return std::unexpected{turn_right.error()};
  if (!strafe_left) return std::unexpected{strafe_left.error()};
  if (!strafe_right) return std::unexpected{strafe_right.error()};
  if (!rise) return std::unexpected{rise.error()};
  if (!fall) return std::unexpected{fall.error()};

  std::optional<FlightRegimeTransition> last_transition;
  if (!(**transition).is_null()) {
    if (!(**transition).is_object()) {
      return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                     "$.state.flight.last_transition",
                                     "expected an object or null")};
    }
    auto from =
        read_regime(**transition, "from", "$.state.flight.last_transition");
    auto to = read_regime(**transition, "to", "$.state.flight.last_transition");
    auto transition_tick =
        read_u64(**transition, "tick", "$.state.flight.last_transition");
    if (!from) return std::unexpected{from.error()};
    if (!to) return std::unexpected{to.error()};
    if (!transition_tick) return std::unexpected{transition_tick.error()};
    last_transition = FlightRegimeTransition{*from, *to, *transition_tick};
  }

  return PlanetaryFlightState{
      .tick = *tick,
      .planet = *planet,
      .pose = {{*latitude, *longitude, *altitude}, *heading},
      .velocity = {*east, *north, *up},
      .clearance_metres = *clearance,
      .mode = *mode,
      .controls = {*forward, *backward, *turn_left, *turn_right, *strafe_left,
                   *strafe_right, *rise, *fall},
      .regime = *regime,
      .last_transition = last_transition,
  };
}

}  // namespace

auto current_save_generator_versions() noexcept -> SaveGeneratorVersions {
  return SaveGeneratorVersions{
      .seed_derivation = kSeedDerivationVersion,
      .planet_descriptor = kPlanetGeneratorVersion,
      .terrain_tiles = kTerrainTileGeneratorVersion,
      .origin_station = kOriginStationGeneratorVersion,
      .surface_signals = kSurfaceSignalGeneratorVersion,
      .local_sun = kLocalSunGeneratorVersion,
  };
}

auto make_save_recipe(Seed universe_seed, std::uint64_t active_planet_ordinal)
    -> SaveRecipe {
  const auto system_seed =
      derive_seed(universe_seed, SeedDomain::system, kOriginSystemOrdinal);
  const auto planet_seed =
      derive_seed(system_seed, SeedDomain::planet, active_planet_ordinal);
  const auto station = generate_origin_station(universe_seed);
  const auto planet = generate_planet_descriptor(planet_seed);
  return SaveRecipe{
      .universe_seed = universe_seed,
      .origin_system_ordinal = kOriginSystemOrdinal,
      .active_planet_ordinal = active_planet_ordinal,
      .generator_versions = current_save_generator_versions(),
      .origin_station = station.id,
      .active_planet = planet.id,
  };
}

auto validate_save_document(const SaveDocument& document)
    -> std::expected<void, SaveSchemaError> {
  if (document.recipe.generator_versions != current_save_generator_versions()) {
    return std::unexpected{
        failure(SaveSchemaErrorCode::incompatible_generator_version,
                "$.recipe.generator_versions",
                "save requires a generator version unsupported by this build")};
  }
  if (document.recipe.origin_system_ordinal != kOriginSystemOrdinal) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::invalid_value, "$.recipe.origin_system_ordinal",
        "current save formats support only the origin system")};
  }
  const auto expected = make_save_recipe(document.recipe.universe_seed,
                                         document.recipe.active_planet_ordinal);
  if (document.recipe.origin_station != expected.origin_station) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::identity_mismatch, "$.recipe.origin_station_id",
        "stored origin station does not match deterministic regeneration")};
  }
  if (document.recipe.active_planet != expected.active_planet) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::identity_mismatch, "$.recipe.active_planet_id",
        "stored active planet does not match deterministic regeneration")};
  }
  if (!valid_location(document.state.location) ||
      !valid_objective(document.state.first_objective)) {
    return std::unexpected{
        failure(SaveSchemaErrorCode::invalid_state, "$.state",
                "location or first-objective state is invalid")};
  }
  if (document.state.location == OriginLocation::docked_at_origin) {
    if (document.state.flight) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state, "$.state.flight",
          "a docked save cannot contain active planetary flight state")};
    }
  } else {
    if (document.state.first_objective == FirstObjectiveStatus::offered) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state, "$.state.first_objective.status",
          "an offered objective cannot already be in flight")};
    }
    if (!document.state.flight) {
      return std::unexpected{
          failure(SaveSchemaErrorCode::invalid_state, "$.state.flight",
                  "an in-flight save requires planetary flight state")};
    }
    if (auto flight =
            validate_flight(document.recipe, document.state.flight.value());
        !flight) {
      return flight;
    }
  }
  if (document.state.discoveries.size() > kMaximumSaveDiscoveries) {
    return std::unexpected{
        failure(SaveSchemaErrorCode::invalid_state, "$.state.discoveries",
                "discovery count exceeds the save-format bound")};
  }
  std::unordered_set<std::uint64_t> discoveries;
  for (std::size_t index = 0; index < document.state.discoveries.size();
       ++index) {
    if (!discoveries.insert(document.state.discoveries[index].signal.value)
             .second) {
      return std::unexpected{
          failure(SaveSchemaErrorCode::invalid_state,
                  std::format("$.state.discoveries[{}].signal_id", index),
                  "a discovery signal may appear only once")};
    }
  }
  if (document.state.world_deltas.size() > kMaximumSaveWorldDeltas) {
    return std::unexpected{
        failure(SaveSchemaErrorCode::invalid_state, "$.state.world_deltas",
                "world-delta count exceeds the save-format bound")};
  }
  for (std::size_t index = 0; index < document.state.world_deltas.size();
       ++index) {
    const auto& delta = document.state.world_deltas[index];
    if (!valid_object_key(delta.object_key) || !valid_delta_kind(delta.kind)) {
      return std::unexpected{
          failure(SaveSchemaErrorCode::invalid_state,
                  std::format("$.state.world_deltas[{}]", index),
                  "world delta has an invalid object key or kind")};
    }
  }
  return {};
}

auto encode_save_document_json(const SaveDocument& document)
    -> std::expected<std::string, SaveSchemaError> {
  if (auto valid = validate_save_document(document); !valid) {
    return std::unexpected{valid.error()};
  }
  const auto& versions = document.recipe.generator_versions;
  Json discoveries = Json::array();
  for (const auto& discovery : document.state.discoveries) {
    discoveries.push_back(
        Json{{"signal_id", encoded_id("signal-", discovery.signal)},
             {"tick", decimal(discovery.tick)}});
  }
  Json deltas = Json::array();
  for (const auto& delta : document.state.world_deltas) {
    deltas.push_back(Json{{"object_key", delta.object_key},
                          {"kind", delta_kind_name(delta.kind)},
                          {"tick", decimal(delta.tick)}});
  }
  Json flight = nullptr;
  if (document.state.flight) flight = encode_flight(*document.state.flight);
  const Json root{
      {"application", kSaveApplication},
      {"format_version", kSaveFormatVersion},
      {"recipe",
       Json{{"universe_seed", decimal(document.recipe.universe_seed.value)},
            {"origin_system_ordinal",
             decimal(document.recipe.origin_system_ordinal)},
            {"active_planet_ordinal",
             decimal(document.recipe.active_planet_ordinal)},
            {"generator_versions",
             Json{{"seed_derivation", versions.seed_derivation},
                  {"planet_descriptor", versions.planet_descriptor},
                  {"terrain_tiles", versions.terrain_tiles},
                  {"origin_station", versions.origin_station},
                  {"surface_signals", versions.surface_signals},
                  {"local_sun", versions.local_sun}}},
            {"origin_station_id",
             encoded_id("station-", document.recipe.origin_station)},
            {"active_planet_id",
             encoded_id("planet-", document.recipe.active_planet)}}},
      {"state",
       Json{{"location", location_name(document.state.location)},
            {"first_objective",
             Json{{"status", objective_name(document.state.first_objective)},
                  {"target_signal_id",
                   encoded_id("signal-",
                              document.state.first_objective_target)}}},
            {"flight", std::move(flight)},
            {"discoveries", std::move(discoveries)},
            {"world_deltas", std::move(deltas)}}},
  };
  auto output = root.dump(2);
  output.push_back('\n');
  if (output.size() > kMaximumSaveDocumentBytes) {
    return std::unexpected{
        failure(SaveSchemaErrorCode::document_too_large, "$",
                "encoded save exceeds the save-format byte bound")};
  }
  return output;
}

auto decode_save_document_json(std::string_view json_text)
    -> std::expected<SaveDocument, SaveSchemaError> {
  if (json_text.size() > kMaximumSaveDocumentBytes) {
    return std::unexpected{
        failure(SaveSchemaErrorCode::document_too_large, "$",
                "save exceeds the save-format byte bound")};
  }
  bool duplicate_key{};
  std::vector<std::unordered_set<std::string>> object_keys;
  const Json::parser_callback_t callback = [&](int, Json::parse_event_t event,
                                               Json& parsed) {
    if (event == Json::parse_event_t::object_start) {
      object_keys.emplace_back();
    } else if (event == Json::parse_event_t::key) {
      if (object_keys.empty() ||
          !object_keys.back().insert(parsed.get<std::string>()).second) {
        duplicate_key = true;
      }
    } else if (event == Json::parse_event_t::object_end &&
               !object_keys.empty()) {
      object_keys.pop_back();
    }
    return true;
  };

  Json root;
  try {
    root =
        Json::parse(json_text.begin(), json_text.end(), callback, true, false);
  } catch (const nlohmann::json::parse_error& error) {
    return std::unexpected{
        failure(SaveSchemaErrorCode::malformed_json, "$", error.what())};
  } catch (const nlohmann::json::exception& error) {
    return std::unexpected{
        failure(SaveSchemaErrorCode::invalid_value, "$", error.what())};
  }
  if (duplicate_key) {
    return std::unexpected{failure(SaveSchemaErrorCode::duplicate_key, "$",
                                   "JSON objects cannot repeat a key")};
  }
  if (!root.is_object()) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type, "$",
                                   "save root must be an object")};
  }

  auto application = read_string(root, "application", "$");
  auto format_version = read_u32(root, "format_version", "$");
  if (!application) return std::unexpected{application.error()};
  if (!format_version) return std::unexpected{format_version.error()};
  if (*application != kSaveApplication) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                   "$.application",
                                   "save belongs to another application")};
  }
  if (*format_version != 1U && *format_version != kSaveFormatVersion) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::unsupported_format_version, "$.format_version",
        "save format version is unsupported by this build")};
  }
  auto recipe_json = read_object(root, "recipe", "$");
  auto state_json = read_object(root, "state", "$");
  if (!recipe_json) return std::unexpected{recipe_json.error()};
  if (!state_json) return std::unexpected{state_json.error()};

  auto universe_seed = read_u64(**recipe_json, "universe_seed", "$.recipe");
  auto system_ordinal =
      read_u64(**recipe_json, "origin_system_ordinal", "$.recipe");
  auto planet_ordinal =
      read_u64(**recipe_json, "active_planet_ordinal", "$.recipe");
  auto versions_json =
      read_object(**recipe_json, "generator_versions", "$.recipe");
  auto station = read_id<OriginStationId>(**recipe_json, "origin_station_id",
                                          "$.recipe", "station-");
  auto planet = read_id<PlanetId>(**recipe_json, "active_planet_id", "$.recipe",
                                  "planet-");
  if (!universe_seed) return std::unexpected{universe_seed.error()};
  if (!system_ordinal) return std::unexpected{system_ordinal.error()};
  if (!planet_ordinal) return std::unexpected{planet_ordinal.error()};
  if (!versions_json) return std::unexpected{versions_json.error()};
  if (!station) return std::unexpected{station.error()};
  if (!planet) return std::unexpected{planet.error()};

  auto seed_version = read_u32(**versions_json, "seed_derivation",
                               "$.recipe.generator_versions");
  auto planet_version = read_u32(**versions_json, "planet_descriptor",
                                 "$.recipe.generator_versions");
  auto terrain_version =
      read_u32(**versions_json, "terrain_tiles", "$.recipe.generator_versions");
  auto station_version = read_u32(**versions_json, "origin_station",
                                  "$.recipe.generator_versions");
  auto signal_version = read_u32(**versions_json, "surface_signals",
                                 "$.recipe.generator_versions");
  std::expected<std::uint32_t, SaveSchemaError> sun_version{
      kLocalSunGeneratorVersion};
  if (*format_version >= 2U) {
    sun_version = read_u32(**versions_json, "local_sun",
                           "$.recipe.generator_versions");
  }
  if (!seed_version) return std::unexpected{seed_version.error()};
  if (!planet_version) return std::unexpected{planet_version.error()};
  if (!terrain_version) return std::unexpected{terrain_version.error()};
  if (!station_version) return std::unexpected{station_version.error()};
  if (!signal_version) return std::unexpected{signal_version.error()};
  if (!sun_version) return std::unexpected{sun_version.error()};
  const SaveGeneratorVersions versions{
      .seed_derivation = *seed_version,
      .planet_descriptor = *planet_version,
      .terrain_tiles = *terrain_version,
      .origin_station = *station_version,
      .surface_signals = *signal_version,
      .local_sun = *sun_version,
  };
  if (versions != current_save_generator_versions()) {
    return std::unexpected{
        failure(SaveSchemaErrorCode::incompatible_generator_version,
                "$.recipe.generator_versions",
                "save requires a generator version unsupported by this build")};
  }

  auto location = read_location(**state_json, "location", "$.state");
  auto objective_json = read_object(**state_json, "first_objective", "$.state");
  auto flight_json = require_field(**state_json, "flight", "$.state");
  auto discoveries_json = read_array(**state_json, "discoveries", "$.state");
  auto deltas_json = read_array(**state_json, "world_deltas", "$.state");
  if (!location) return std::unexpected{location.error()};
  if (!objective_json) return std::unexpected{objective_json.error()};
  if (!flight_json) return std::unexpected{flight_json.error()};
  if (!discoveries_json) return std::unexpected{discoveries_json.error()};
  if (!deltas_json) return std::unexpected{deltas_json.error()};

  auto objective =
      read_objective(**objective_json, "status", "$.state.first_objective");
  auto target = read_id<SurfaceSignalId>(**objective_json, "target_signal_id",
                                         "$.state.first_objective", "signal-");
  if (!objective) return std::unexpected{objective.error()};
  if (!target) return std::unexpected{target.error()};

  std::optional<PlanetaryFlightState> flight;
  if (!(**flight_json).is_null()) {
    auto decoded = decode_flight(**flight_json);
    if (!decoded) return std::unexpected{decoded.error()};
    flight = std::move(*decoded);
  }
  std::vector<SaveDiscovery> discoveries;
  if ((**discoveries_json).size() > kMaximumSaveDiscoveries) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                   "$.state.discoveries",
                                   "discovery count exceeds the v1 bound")};
  }
  discoveries.reserve((**discoveries_json).size());
  for (std::size_t index = 0; index < (**discoveries_json).size(); ++index) {
    const auto& entry = (**discoveries_json)[index];
    const auto path = std::format("$.state.discoveries[{}]", index);
    if (!entry.is_object()) {
      return std::unexpected{failure(SaveSchemaErrorCode::invalid_type, path,
                                     "expected an object")};
    }
    auto signal = read_id<SurfaceSignalId>(entry, "signal_id", path, "signal-");
    auto tick = read_u64(entry, "tick", path);
    if (!signal) return std::unexpected{signal.error()};
    if (!tick) return std::unexpected{tick.error()};
    discoveries.push_back(SaveDiscovery{*signal, *tick});
  }

  std::vector<SaveWorldDelta> deltas;
  if ((**deltas_json).size() > kMaximumSaveWorldDeltas) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                   "$.state.world_deltas",
                                   "world-delta count exceeds the v1 bound")};
  }
  deltas.reserve((**deltas_json).size());
  for (std::size_t index = 0; index < (**deltas_json).size(); ++index) {
    const auto& entry = (**deltas_json)[index];
    const auto path = std::format("$.state.world_deltas[{}]", index);
    if (!entry.is_object()) {
      return std::unexpected{failure(SaveSchemaErrorCode::invalid_type, path,
                                     "expected an object")};
    }
    auto object_key = read_string(entry, "object_key", path);
    auto kind = read_delta_kind(entry, "kind", path);
    auto tick = read_u64(entry, "tick", path);
    if (!object_key) return std::unexpected{object_key.error()};
    if (!kind) return std::unexpected{kind.error()};
    if (!tick) return std::unexpected{tick.error()};
    deltas.push_back(SaveWorldDelta{std::move(*object_key), *kind, *tick});
  }

  SaveDocument document{
      .recipe =
          SaveRecipe{
              .universe_seed = Seed{*universe_seed},
              .origin_system_ordinal = *system_ordinal,
              .active_planet_ordinal = *planet_ordinal,
              .generator_versions = versions,
              .origin_station = *station,
              .active_planet = *planet,
          },
      .state =
          SaveMutableState{
              .location = *location,
              .first_objective = *objective,
              .first_objective_target = *target,
              .flight = std::move(flight),
              .discoveries = std::move(discoveries),
              .world_deltas = std::move(deltas),
          },
  };
  if (auto valid = validate_save_document(document); !valid) {
    return std::unexpected{valid.error()};
  }
  return document;
}

}  // namespace apsis_drift
