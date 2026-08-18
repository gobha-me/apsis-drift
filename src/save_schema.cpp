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
#include "apsis_drift/local_system.hpp"
#include "apsis_drift/intersystem_jump.hpp"
#include "apsis_drift/planet.hpp"
#include "apsis_drift/seed.hpp"
#include "apsis_drift/terrain_tiles.hpp"
#include "apsis_drift/system_flight.hpp"

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

[[nodiscard]] auto mission_phase_name(IntersystemMissionPhase value)
    -> std::string_view {
  switch (value) {
    case IntersystemMissionPhase::offered: return "offered";
    case IntersystemMissionPhase::accepted: return "accepted";
    case IntersystemMissionPhase::active: return "active";
    case IntersystemMissionPhase::objective_complete:
      return "objective_complete";
    case IntersystemMissionPhase::returned: return "returned";
    case IntersystemMissionPhase::turned_in: return "turned_in";
  }
  return "unknown";
}

[[nodiscard]] auto travel_phase_name(IntersystemTravelPhase value)
    -> std::string_view {
  switch (value) {
    case IntersystemTravelPhase::docked_at_origin:
      return "docked_at_origin";
    case IntersystemTravelPhase::origin_system_flight:
      return "origin_system_flight";
    case IntersystemTravelPhase::outbound_jump_spooling:
      return "outbound_jump_spooling";
    case IntersystemTravelPhase::outbound_jump_committed:
      return "outbound_jump_committed";
    case IntersystemTravelPhase::target_system_flight:
      return "target_system_flight";
    case IntersystemTravelPhase::target_planet_flight:
      return "target_planet_flight";
    case IntersystemTravelPhase::return_jump_spooling:
      return "return_jump_spooling";
    case IntersystemTravelPhase::return_jump_committed:
      return "return_jump_committed";
    case IntersystemTravelPhase::origin_system_return:
      return "origin_system_return";
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

[[nodiscard]] auto read_mission_phase(const Json& parent,
                                      std::string_view name,
                                      std::string path)
    -> std::expected<IntersystemMissionPhase, SaveSchemaError> {
  auto value = read_string(parent, name, path);
  if (!value) return std::unexpected{value.error()};
  if (*value == "offered") return IntersystemMissionPhase::offered;
  if (*value == "accepted") return IntersystemMissionPhase::accepted;
  if (*value == "active") return IntersystemMissionPhase::active;
  if (*value == "objective_complete") {
    return IntersystemMissionPhase::objective_complete;
  }
  if (*value == "returned") return IntersystemMissionPhase::returned;
  if (*value == "turned_in") return IntersystemMissionPhase::turned_in;
  return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                 std::format("{}.{}", path, name),
                                 "unknown intersystem mission phase")};
}

[[nodiscard]] auto read_travel_phase(const Json& parent,
                                     std::string_view name,
                                     std::string path)
    -> std::expected<IntersystemTravelPhase, SaveSchemaError> {
  auto value = read_string(parent, name, path);
  if (!value) return std::unexpected{value.error()};
  if (*value == "docked_at_origin") {
    return IntersystemTravelPhase::docked_at_origin;
  }
  if (*value == "origin_system_flight") {
    return IntersystemTravelPhase::origin_system_flight;
  }
  if (*value == "outbound_jump_spooling") {
    return IntersystemTravelPhase::outbound_jump_spooling;
  }
  if (*value == "outbound_jump_committed") {
    return IntersystemTravelPhase::outbound_jump_committed;
  }
  if (*value == "target_system_flight") {
    return IntersystemTravelPhase::target_system_flight;
  }
  if (*value == "target_planet_flight") {
    return IntersystemTravelPhase::target_planet_flight;
  }
  if (*value == "return_jump_spooling") {
    return IntersystemTravelPhase::return_jump_spooling;
  }
  if (*value == "return_jump_committed") {
    return IntersystemTravelPhase::return_jump_committed;
  }
  if (*value == "origin_system_return") {
    return IntersystemTravelPhase::origin_system_return;
  }
  return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                 std::format("{}.{}", path, name),
                                 "unknown intersystem travel phase")};
}

template <typename Id>
[[nodiscard]] auto read_optional_id(const Json& parent, std::string_view name,
                                    std::string path,
                                    std::string_view prefix)
    -> std::expected<std::optional<Id>, SaveSchemaError> {
  auto field = require_field(parent, name, path);
  if (!field) return std::unexpected{field.error()};
  if ((*field)->is_null()) return std::optional<Id>{};
  if (!(*field)->is_string()) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                   std::format("{}.{}", path, name),
                                   "expected an identifier or null")};
  }
  auto parsed = parse_id<Id>((*field)->get<std::string>(), prefix,
                             std::format("{}.{}", path, name));
  if (!parsed) return std::unexpected{parsed.error()};
  return std::optional<Id>{*parsed};
}

[[nodiscard]] auto read_optional_tick(const Json& parent,
                                      std::string_view name,
                                      std::string path)
    -> std::expected<std::optional<SimulationTick>, SaveSchemaError> {
  auto field = require_field(parent, name, path);
  if (!field) return std::unexpected{field.error()};
  if ((*field)->is_null()) return std::optional<SimulationTick>{};
  if (!(*field)->is_string()) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                   std::format("{}.{}", path, name),
                                   "expected a tick string or null")};
  }
  auto parsed = parse_u64((*field)->get<std::string>(),
                          std::format("{}.{}", path, name));
  if (!parsed) return std::unexpected{parsed.error()};
  return std::optional<SimulationTick>{*parsed};
}

[[nodiscard]] auto encode_intersystem_contract(
    const IntersystemContractState& state) -> Json {
  const auto& ids = state.identities;
  Json current_planet = nullptr;
  if (state.current_planet) {
    current_planet = encoded_id("planet-", *state.current_planet);
  }
  Json committed_destination = nullptr;
  if (state.committed_jump_destination) {
    committed_destination =
        encoded_id("system-", *state.committed_jump_destination);
  }
  Json phase_started_tick = nullptr;
  if (state.phase_started_tick) {
    phase_started_tick = decimal(*state.phase_started_tick);
  }
  Json arrival_solution = nullptr;
  if (state.arrival_solution) {
    const auto& arrival = *state.arrival_solution;
    Json reference_planet = nullptr;
    if (arrival.reference_planet) {
      reference_planet = encoded_id("planet-", *arrival.reference_planet);
    }
    arrival_solution = Json{
        {"destination_system_id",
         encoded_id("system-", arrival.destination)},
        {"reference_planet_id", std::move(reference_planet)},
        {"arrival_tick", decimal(arrival.arrival_tick)},
        {"position_metres",
         Json{{"x", decimal(arrival.position.x)},
              {"y", decimal(arrival.position.y)},
              {"z", decimal(arrival.position.z)}}},
        {"velocity_metres_per_second",
         Json{{"x", decimal(arrival.velocity.x)},
              {"y", decimal(arrival.velocity.y)},
              {"z", decimal(arrival.velocity.z)}}},
    };
  }
  return Json{
      {"identities",
       Json{{"origin_system_id", encoded_id("system-", ids.origin_system)},
            {"origin_station_id",
             encoded_id("station-", ids.origin_station)},
            {"target_system_id", encoded_id("system-", ids.target_system)},
            {"target_star_id", encoded_id("star-", ids.target_star)},
            {"target_planet_id", encoded_id("planet-", ids.target_planet)},
            {"target_objective_id",
             encoded_id("signal-", ids.target_objective)},
            {"mission_id", encoded_id("mission-", ids.mission)}}},
      {"universe_tick", decimal(state.universe_tick)},
      {"mission_phase", mission_phase_name(state.mission_phase)},
      {"travel_phase", travel_phase_name(state.travel_phase)},
      {"current_system_id", encoded_id("system-", state.current_system)},
      {"current_planet_id", std::move(current_planet)},
      {"committed_jump_destination_id", std::move(committed_destination)},
      {"phase_started_tick", std::move(phase_started_tick)},
      {"arrival_solution", std::move(arrival_solution)},
  };
}

[[nodiscard]] auto decode_intersystem_contract(const Json& json,
                                               Seed universe_seed,
                                               std::uint32_t format_version)
    -> std::expected<IntersystemContractState, SaveSchemaError> {
  constexpr std::string_view path{"$.state.intersystem_contract"};
  if (!json.is_object()) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                   std::string{path}, "expected an object")};
  }
  auto identities = read_object(json, "identities", std::string{path});
  auto universe_tick = read_u64(json, "universe_tick", std::string{path});
  auto mission_phase =
      read_mission_phase(json, "mission_phase", std::string{path});
  auto travel_phase =
      read_travel_phase(json, "travel_phase", std::string{path});
  auto current_system = read_id<SystemId>(json, "current_system_id",
                                          std::string{path}, "system-");
  auto current_planet = read_optional_id<PlanetId>(
      json, "current_planet_id", std::string{path}, "planet-");
  auto destination = read_optional_id<SystemId>(
      json, "committed_jump_destination_id", std::string{path}, "system-");
  auto phase_tick =
      read_optional_tick(json, "phase_started_tick", std::string{path});
  if (!identities) return std::unexpected{identities.error()};
  if (!universe_tick) return std::unexpected{universe_tick.error()};
  if (!mission_phase) return std::unexpected{mission_phase.error()};
  if (!travel_phase) return std::unexpected{travel_phase.error()};
  if (!current_system) return std::unexpected{current_system.error()};
  if (!current_planet) return std::unexpected{current_planet.error()};
  if (!destination) return std::unexpected{destination.error()};
  if (!phase_tick) return std::unexpected{phase_tick.error()};

  std::optional<IntersystemArrivalSolution> arrival_solution;
  if (format_version >= 4U) {
    auto arrival_field = require_field(json, "arrival_solution",
                                       std::string{path});
    if (!arrival_field) return std::unexpected{arrival_field.error()};
    if (!(**arrival_field).is_null()) {
      if (!(**arrival_field).is_object()) {
        return std::unexpected{failure(
            SaveSchemaErrorCode::invalid_type,
            "$.state.intersystem_contract.arrival_solution",
            "expected an object or null")};
      }
      const auto& arrival = **arrival_field;
      constexpr std::string_view arrival_path{
          "$.state.intersystem_contract.arrival_solution"};
      auto arrival_destination = read_id<SystemId>(
          arrival, "destination_system_id", std::string{arrival_path},
          "system-");
      auto reference_planet = read_optional_id<PlanetId>(
          arrival, "reference_planet_id", std::string{arrival_path},
          "planet-");
      auto arrival_tick =
          read_u64(arrival, "arrival_tick", std::string{arrival_path});
      auto position = read_object(arrival, "position_metres",
                                  std::string{arrival_path});
      auto velocity = read_object(arrival, "velocity_metres_per_second",
                                  std::string{arrival_path});
      if (!arrival_destination) {
        return std::unexpected{arrival_destination.error()};
      }
      if (!reference_planet) {
        return std::unexpected{reference_planet.error()};
      }
      if (!arrival_tick) return std::unexpected{arrival_tick.error()};
      if (!position) return std::unexpected{position.error()};
      if (!velocity) return std::unexpected{velocity.error()};
      auto px = read_double(**position, "x",
                            std::format("{}.position_metres", arrival_path));
      auto py = read_double(**position, "y",
                            std::format("{}.position_metres", arrival_path));
      auto pz = read_double(**position, "z",
                            std::format("{}.position_metres", arrival_path));
      auto vx = read_double(
          **velocity, "x",
          std::format("{}.velocity_metres_per_second", arrival_path));
      auto vy = read_double(
          **velocity, "y",
          std::format("{}.velocity_metres_per_second", arrival_path));
      auto vz = read_double(
          **velocity, "z",
          std::format("{}.velocity_metres_per_second", arrival_path));
      if (!px) return std::unexpected{px.error()};
      if (!py) return std::unexpected{py.error()};
      if (!pz) return std::unexpected{pz.error()};
      if (!vx) return std::unexpected{vx.error()};
      if (!vy) return std::unexpected{vy.error()};
      if (!vz) return std::unexpected{vz.error()};
      arrival_solution = IntersystemArrivalSolution{
          .destination = *arrival_destination,
          .reference_planet = *reference_planet,
          .arrival_tick = *arrival_tick,
          .position = {*px, *py, *pz},
          .velocity = {*vx, *vy, *vz},
      };
    }
  }

  const auto expected = generate_first_intersystem_identities(universe_seed);
  auto origin_system = read_id<SystemId>(**identities, "origin_system_id",
                                         "$.state.intersystem_contract.identities",
                                         "system-");
  auto origin_station = read_id<OriginStationId>(
      **identities, "origin_station_id",
      "$.state.intersystem_contract.identities", "station-");
  auto target_system = read_id<SystemId>(**identities, "target_system_id",
                                         "$.state.intersystem_contract.identities",
                                         "system-");
  auto target_star = read_id<StarId>(**identities, "target_star_id",
                                     "$.state.intersystem_contract.identities",
                                     "star-");
  auto target_planet = read_id<PlanetId>(
      **identities, "target_planet_id",
      "$.state.intersystem_contract.identities", "planet-");
  auto target_objective = read_id<SurfaceSignalId>(
      **identities, "target_objective_id",
      "$.state.intersystem_contract.identities", "signal-");
  auto mission = read_id<MissionId>(**identities, "mission_id",
                                    "$.state.intersystem_contract.identities",
                                    "mission-");
  if (!origin_system) return std::unexpected{origin_system.error()};
  if (!origin_station) return std::unexpected{origin_station.error()};
  if (!target_system) return std::unexpected{target_system.error()};
  if (!target_star) return std::unexpected{target_star.error()};
  if (!target_planet) return std::unexpected{target_planet.error()};
  if (!target_objective) return std::unexpected{target_objective.error()};
  if (!mission) return std::unexpected{mission.error()};
  if (*origin_system != expected.origin_system ||
      *origin_station != expected.origin_station ||
      *target_system != expected.target_system ||
      *target_star != expected.target_star ||
      *target_planet != expected.target_planet ||
      *target_objective != expected.target_objective ||
      *mission != expected.mission) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::identity_mismatch,
        "$.state.intersystem_contract.identities",
        "stored first-contract identities do not match deterministic regeneration")};
  }
  IntersystemContractState state{
      .identities = expected,
      .universe_tick = *universe_tick,
      .mission_phase = *mission_phase,
      .travel_phase = *travel_phase,
      .current_system = *current_system,
      .current_planet = *current_planet,
      .committed_jump_destination = *destination,
      .phase_started_tick = *phase_tick,
      .arrival_solution = std::move(arrival_solution),
  };
  if (!validate_intersystem_contract_state(state)) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::invalid_state,
        "$.state.intersystem_contract",
        "intersystem contract violates its authoritative state machine")};
  }
  return state;
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

[[nodiscard]] auto encode_system_flight(const SystemFlightState& state)
    -> Json {
  return Json{
      {"tick", decimal(state.tick)},
      {"system_id", encoded_id("system-", state.system)},
      {"target_planet_id", encoded_id("planet-", state.target)},
      {"position_metres",
       Json{{"x", decimal(state.position.x)},
            {"y", decimal(state.position.y)},
            {"z", decimal(state.position.z)}}},
      {"velocity_metres_per_second",
       Json{{"x", decimal(state.velocity.x)},
            {"y", decimal(state.velocity.y)},
            {"z", decimal(state.velocity.z)}}},
      {"forward", Json{{"x", decimal(state.forward.x)},
                        {"y", decimal(state.forward.y)},
                        {"z", decimal(state.forward.z)}}},
      {"up", Json{{"x", decimal(state.up.x)},
                   {"y", decimal(state.up.y)},
                   {"z", decimal(state.up.z)}}},
      {"mode", mode_name(state.mode)},
      {"controls", Json{{"forward", state.controls.forward},
                        {"backward", state.controls.backward},
                        {"turn_left", state.controls.turn_left},
                        {"turn_right", state.controls.turn_right},
                        {"strafe_left", state.controls.strafe_left},
                        {"strafe_right", state.controls.strafe_right},
                        {"rise", state.controls.rise},
                        {"fall", state.controls.fall}}},
      {"time_scale", static_cast<std::uint32_t>(state.time_scale)},
  };
}

[[nodiscard]] auto decode_system_flight(const Json& json)
    -> std::expected<SystemFlightState, SaveSchemaError> {
  constexpr std::string_view path{"$.state.system_flight"};
  if (!json.is_object()) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                   std::string{path}, "expected an object")};
  }
  auto tick = read_u64(json, "tick", std::string{path});
  auto system = read_id<SystemId>(json, "system_id", std::string{path},
                                  "system-");
  auto target = read_id<PlanetId>(json, "target_planet_id",
                                  std::string{path}, "planet-");
  auto position = read_object(json, "position_metres", std::string{path});
  auto velocity = read_object(json, "velocity_metres_per_second",
                              std::string{path});
  auto forward = read_object(json, "forward", std::string{path});
  auto up = read_object(json, "up", std::string{path});
  auto mode = read_mode(json, "mode", std::string{path});
  auto controls = read_object(json, "controls", std::string{path});
  auto scale = read_u32(json, "time_scale", std::string{path});
  if (!tick) return std::unexpected{tick.error()};
  if (!system) return std::unexpected{system.error()};
  if (!target) return std::unexpected{target.error()};
  if (!position) return std::unexpected{position.error()};
  if (!velocity) return std::unexpected{velocity.error()};
  if (!forward) return std::unexpected{forward.error()};
  if (!up) return std::unexpected{up.error()};
  if (!mode) return std::unexpected{mode.error()};
  if (!controls) return std::unexpected{controls.error()};
  if (!scale) return std::unexpected{scale.error()};

  const auto vector = [](const Json& object, std::string path_value)
      -> std::expected<std::array<double, 3>, SaveSchemaError> {
    auto x = read_double(object, "x", path_value);
    auto y = read_double(object, "y", path_value);
    auto z = read_double(object, "z", path_value);
    if (!x) return std::unexpected{x.error()};
    if (!y) return std::unexpected{y.error()};
    if (!z) return std::unexpected{z.error()};
    return std::array<double, 3>{*x, *y, *z};
  };
  auto decoded_position = vector(**position, "$.state.system_flight.position_metres");
  auto decoded_velocity = vector(
      **velocity, "$.state.system_flight.velocity_metres_per_second");
  auto decoded_forward = vector(**forward, "$.state.system_flight.forward");
  auto decoded_up = vector(**up, "$.state.system_flight.up");
  if (!decoded_position) return std::unexpected{decoded_position.error()};
  if (!decoded_velocity) return std::unexpected{decoded_velocity.error()};
  if (!decoded_forward) return std::unexpected{decoded_forward.error()};
  if (!decoded_up) return std::unexpected{decoded_up.error()};

  auto control = [&](std::string_view name)
      -> std::expected<bool, SaveSchemaError> {
    return read_bool(**controls, name, "$.state.system_flight.controls");
  };
  auto control_forward = control("forward");
  auto backward = control("backward");
  auto turn_left = control("turn_left");
  auto turn_right = control("turn_right");
  auto strafe_left = control("strafe_left");
  auto strafe_right = control("strafe_right");
  auto rise = control("rise");
  auto fall = control("fall");
  if (!control_forward) return std::unexpected{control_forward.error()};
  if (!backward) return std::unexpected{backward.error()};
  if (!turn_left) return std::unexpected{turn_left.error()};
  if (!turn_right) return std::unexpected{turn_right.error()};
  if (!strafe_left) return std::unexpected{strafe_left.error()};
  if (!strafe_right) return std::unexpected{strafe_right.error()};
  if (!rise) return std::unexpected{rise.error()};
  if (!fall) return std::unexpected{fall.error()};
  SystemTimeScale time_scale{};
  if (*scale == 1U) time_scale = SystemTimeScale::one;
  else if (*scale == 4U) time_scale = SystemTimeScale::four;
  else if (*scale == 16U) time_scale = SystemTimeScale::sixteen;
  else {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                   "$.state.system_flight.time_scale",
                                   "time scale must be 1, 4, or 16")};
  }
  return SystemFlightState{
      .tick = *tick,
      .system = *system,
      .target = *target,
      .position = {(*decoded_position)[0], (*decoded_position)[1],
                   (*decoded_position)[2]},
      .velocity = {(*decoded_velocity)[0], (*decoded_velocity)[1],
                   (*decoded_velocity)[2]},
      .forward = {(*decoded_forward)[0], (*decoded_forward)[1],
                  (*decoded_forward)[2]},
      .up = {(*decoded_up)[0], (*decoded_up)[1], (*decoded_up)[2]},
      .mode = *mode,
      .controls = {*control_forward, *backward, *turn_left, *turn_right,
                   *strafe_left, *strafe_right, *rise, *fall},
      .time_scale = time_scale,
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
      .local_system = kLocalSystemGeneratorVersion,
      .analytic_ephemeris = kAnalyticEphemerisVersion,
      .intersystem_contract = kIntersystemContractVersion,
      .intersystem_jump = kIntersystemJumpVersion,
      .system_flight = kSystemFlightVersion,
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
  if (document.state.intersystem_contract) {
    const auto& contract = *document.state.intersystem_contract;
    if (contract.identities.universe_seed != document.recipe.universe_seed ||
        !validate_intersystem_contract_state(contract)) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state, "$.state.intersystem_contract",
          "intersystem contract does not match the save recipe or state machine")};
    }
    if (contract.arrival_solution &&
        !validate_intersystem_arrival_solution(
            contract, *contract.arrival_solution)) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state,
          "$.state.intersystem_contract.arrival_solution",
          "Assisted arrival solution does not match the contract")};
    }
    const bool target_system_flight =
        contract.travel_phase == IntersystemTravelPhase::target_system_flight;
    const bool target_planet_flight =
        contract.travel_phase == IntersystemTravelPhase::target_planet_flight;
    if (target_system_flight) {
      if (!document.state.system_flight || document.state.flight) {
        return std::unexpected{failure(
            SaveSchemaErrorCode::invalid_state, "$.state.system_flight",
            "target-system flight requires exactly one system-flight state")};
      }
      const auto system = generate_local_system(contract.identities.target_system_seed);
      if (document.state.system_flight->tick != contract.universe_tick ||
          document.state.system_flight->target !=
              contract.identities.target_planet ||
          !validate_system_flight_state(system,
                                        *document.state.system_flight)) {
        return std::unexpected{failure(
            SaveSchemaErrorCode::invalid_state, "$.state.system_flight",
            "system flight does not match the target contract and clock")};
      }
    } else if (target_planet_flight) {
      if (!document.state.flight || document.state.system_flight ||
          document.state.flight->planet != contract.identities.target_planet ||
          document.state.flight->tick != contract.universe_tick) {
        return std::unexpected{failure(
            SaveSchemaErrorCode::invalid_state, "$.state.flight",
            "target-planet travel requires exactly one matching orbital state")};
      }
      const auto system = generate_local_system(contract.identities.target_system_seed);
      const auto body = find_local_system_planet(
          system, contract.identities.target_planet);
      if (!body || !validate_planetary_flight_state((*body)->descriptor,
                                                    *document.state.flight)) {
        return std::unexpected{failure(
            SaveSchemaErrorCode::invalid_state, "$.state.flight",
            "target-planet flight violates the planetary flight contract")};
      }
    } else if (document.state.flight || document.state.system_flight) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state, "$.state",
          "the current travel phase cannot contain an active craft state")};
    }
    if (!document.state.discoveries.empty() ||
        !document.state.world_deltas.empty()) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state, "$.state",
          "the first intersystem approach cannot yet contain Signal Run deltas")};
    }
  } else {
    if (document.state.system_flight) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state, "$.state.system_flight",
          "legacy Signal Run saves cannot contain system flight")};
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
    } else if (document.state.first_objective ==
               FirstObjectiveStatus::offered) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state, "$.state.first_objective.status",
          "an offered objective cannot already be in flight")};
    } else if (!document.state.flight) {
      return std::unexpected{
          failure(SaveSchemaErrorCode::invalid_state, "$.state.flight",
                  "an in-flight save requires planetary flight state")};
    } else {
      if (auto flight = validate_flight(document.recipe,
                                        document.state.flight.value());
          !flight) {
        return flight;
      }
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
  Json system_flight = nullptr;
  if (document.state.system_flight) {
    system_flight = encode_system_flight(*document.state.system_flight);
  }
  Json state;
  if (document.state.intersystem_contract) {
    state = Json{
        {"career_kind", "intersystem_contract"},
        {"intersystem_contract",
         encode_intersystem_contract(*document.state.intersystem_contract)},
        {"flight", std::move(flight)},
        {"system_flight", std::move(system_flight)},
        {"discoveries", std::move(discoveries)},
        {"world_deltas", std::move(deltas)},
    };
  } else {
    state = Json{
        {"career_kind", "legacy_signal_run"},
        {"location", location_name(document.state.location)},
        {"first_objective",
         Json{{"status", objective_name(document.state.first_objective)},
              {"target_signal_id",
               encoded_id("signal-", document.state.first_objective_target)}}},
        {"flight", std::move(flight)},
        {"system_flight", std::move(system_flight)},
        {"discoveries", std::move(discoveries)},
        {"world_deltas", std::move(deltas)},
    };
  }
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
                  {"local_sun", versions.local_sun},
                  {"local_system", versions.local_system},
                  {"analytic_ephemeris", versions.analytic_ephemeris},
                  {"intersystem_contract",
                   versions.intersystem_contract},
                  {"intersystem_jump", versions.intersystem_jump},
                  {"system_flight", versions.system_flight}}},
            {"origin_station_id",
             encoded_id("station-", document.recipe.origin_station)},
            {"active_planet_id",
             encoded_id("planet-", document.recipe.active_planet)}}},
      {"state", std::move(state)},
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
  if (*format_version != 1U && *format_version != 2U &&
      *format_version != 3U && *format_version != 4U &&
      *format_version != kSaveFormatVersion) {
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
  std::expected<std::uint32_t, SaveSchemaError> system_version{
      kLocalSystemGeneratorVersion};
  std::expected<std::uint32_t, SaveSchemaError> ephemeris_version{
      kAnalyticEphemerisVersion};
  std::expected<std::uint32_t, SaveSchemaError> contract_version{
      kIntersystemContractVersion};
  std::expected<std::uint32_t, SaveSchemaError> jump_version{
      kIntersystemJumpVersion};
  std::expected<std::uint32_t, SaveSchemaError> system_flight_version{
      kSystemFlightVersion};
  if (*format_version >= 2U) {
    sun_version = read_u32(**versions_json, "local_sun",
                           "$.recipe.generator_versions");
  }
  if (*format_version >= 3U) {
    system_version = read_u32(**versions_json, "local_system",
                              "$.recipe.generator_versions");
    ephemeris_version = read_u32(**versions_json, "analytic_ephemeris",
                                 "$.recipe.generator_versions");
    contract_version = read_u32(**versions_json, "intersystem_contract",
                                "$.recipe.generator_versions");
  }
  if (*format_version >= 4U) {
    jump_version = read_u32(**versions_json, "intersystem_jump",
                            "$.recipe.generator_versions");
  }
  if (*format_version >= 5U) {
    system_flight_version = read_u32(**versions_json, "system_flight",
                                     "$.recipe.generator_versions");
  }
  if (!seed_version) return std::unexpected{seed_version.error()};
  if (!planet_version) return std::unexpected{planet_version.error()};
  if (!terrain_version) return std::unexpected{terrain_version.error()};
  if (!station_version) return std::unexpected{station_version.error()};
  if (!signal_version) return std::unexpected{signal_version.error()};
  if (!sun_version) return std::unexpected{sun_version.error()};
  if (!system_version) return std::unexpected{system_version.error()};
  if (!ephemeris_version) return std::unexpected{ephemeris_version.error()};
  if (!contract_version) return std::unexpected{contract_version.error()};
  if (!jump_version) return std::unexpected{jump_version.error()};
  if (!system_flight_version) {
    return std::unexpected{system_flight_version.error()};
  }
  const SaveGeneratorVersions versions{
      .seed_derivation = *seed_version,
      .planet_descriptor = *planet_version,
      .terrain_tiles = *terrain_version,
      .origin_station = *station_version,
      .surface_signals = *signal_version,
      .local_sun = *sun_version,
      .local_system = *system_version,
      .analytic_ephemeris = *ephemeris_version,
      .intersystem_contract = *contract_version,
      .intersystem_jump = *jump_version,
      .system_flight = *system_flight_version,
  };
  if (versions != current_save_generator_versions()) {
    return std::unexpected{
        failure(SaveSchemaErrorCode::incompatible_generator_version,
                "$.recipe.generator_versions",
                "save requires a generator version unsupported by this build")};
  }

  bool intersystem{};
  if (*format_version >= 3U) {
    auto career_kind = read_string(**state_json, "career_kind", "$.state");
    if (!career_kind) return std::unexpected{career_kind.error()};
    if (*career_kind == "intersystem_contract") {
      intersystem = true;
    } else if (*career_kind != "legacy_signal_run") {
      return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                     "$.state.career_kind",
                                     "unknown save career kind")};
    }
  }
  auto flight_json = require_field(**state_json, "flight", "$.state");
  std::expected<const Json*, SaveSchemaError> system_flight_json{nullptr};
  if (*format_version >= 5U) {
    system_flight_json =
        require_field(**state_json, "system_flight", "$.state");
  }
  auto discoveries_json = read_array(**state_json, "discoveries", "$.state");
  auto deltas_json = read_array(**state_json, "world_deltas", "$.state");
  if (!flight_json) return std::unexpected{flight_json.error()};
  if (!system_flight_json) {
    return std::unexpected{system_flight_json.error()};
  }
  if (!discoveries_json) return std::unexpected{discoveries_json.error()};
  if (!deltas_json) return std::unexpected{deltas_json.error()};

  OriginLocation location{OriginLocation::docked_at_origin};
  FirstObjectiveStatus objective{FirstObjectiveStatus::offered};
  SurfaceSignalId target{};
  std::optional<IntersystemContractState> contract;
  if (intersystem) {
    auto contract_json =
        read_object(**state_json, "intersystem_contract", "$.state");
    if (!contract_json) return std::unexpected{contract_json.error()};
    auto decoded =
        decode_intersystem_contract(**contract_json, Seed{*universe_seed},
                                    *format_version);
    if (!decoded) return std::unexpected{decoded.error()};
    contract = std::move(*decoded);
  } else {
    auto decoded_location =
        read_location(**state_json, "location", "$.state");
    auto objective_json =
        read_object(**state_json, "first_objective", "$.state");
    if (!decoded_location) {
      return std::unexpected{decoded_location.error()};
    }
    if (!objective_json) return std::unexpected{objective_json.error()};
    auto decoded_objective = read_objective(
        **objective_json, "status", "$.state.first_objective");
    auto decoded_target = read_id<SurfaceSignalId>(
        **objective_json, "target_signal_id", "$.state.first_objective",
        "signal-");
    if (!decoded_objective) {
      return std::unexpected{decoded_objective.error()};
    }
    if (!decoded_target) return std::unexpected{decoded_target.error()};
    location = *decoded_location;
    objective = *decoded_objective;
    target = *decoded_target;
  }

  std::optional<PlanetaryFlightState> flight;
  if (!(**flight_json).is_null()) {
    auto decoded = decode_flight(**flight_json);
    if (!decoded) return std::unexpected{decoded.error()};
    flight = std::move(*decoded);
  }
  std::optional<SystemFlightState> system_flight;
  if (*format_version >= 5U && *system_flight_json != nullptr &&
      !(**system_flight_json).is_null()) {
    auto decoded = decode_system_flight(**system_flight_json);
    if (!decoded) return std::unexpected{decoded.error()};
    system_flight = std::move(*decoded);
  } else if (*format_version == 4U && contract &&
             contract->travel_phase ==
                 IntersystemTravelPhase::target_system_flight &&
             contract->arrival_solution) {
    const auto system = generate_local_system(contract->identities.target_system_seed);
    auto migrated = initial_system_flight_state(
        system, contract->identities.target_planet,
        *contract->arrival_solution);
    if (!migrated) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state, "$.state.intersystem_contract",
          "format-4 arrival could not initialize system flight")};
    }
    system_flight = std::move(*migrated);
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
              .location = location,
              .first_objective = objective,
              .first_objective_target = target,
              .flight = std::move(flight),
              .system_flight = std::move(system_flight),
              .discoveries = std::move(discoveries),
              .world_deltas = std::move(deltas),
              .intersystem_contract = std::move(contract),
          },
  };
  if (auto valid = validate_save_document(document); !valid) {
    return std::unexpected{valid.error()};
  }
  return document;
}

}  // namespace apsis_drift
