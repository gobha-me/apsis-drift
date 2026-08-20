#include "apsis_drift/save_schema.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
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
#include "apsis_drift/version.hpp"
#include "apsis_drift/world_delta_journal.hpp"

namespace apsis_drift {
namespace {

using Json = nlohmann::ordered_json;

[[nodiscard]] auto failure(SaveSchemaErrorCode code, std::string path,
                           std::string detail) -> SaveSchemaError {
  return SaveSchemaError{code, std::move(path), std::move(detail)};
}

[[nodiscard]] auto valid_application_version(
    std::string_view value) noexcept -> bool {
  return !value.empty() &&
         value.size() <= kMaximumSaveApplicationVersionBytes &&
         std::ranges::none_of(value, [](unsigned char byte) {
           return byte < 0x20U || byte > 0x7eU;
         });
}

[[nodiscard]] auto target_arrival_required(
    IntersystemTravelPhase phase) noexcept -> bool {
  return phase == IntersystemTravelPhase::outbound_jump_committed ||
         phase == IntersystemTravelPhase::target_system_flight ||
         phase == IntersystemTravelPhase::target_planet_flight ||
         phase == IntersystemTravelPhase::return_jump_spooling;
}

[[nodiscard]] auto origin_arrival_required(
    IntersystemTravelPhase phase) noexcept -> bool {
  return phase == IntersystemTravelPhase::return_jump_committed ||
         phase == IntersystemTravelPhase::origin_system_return;
}

[[nodiscard]] auto arrival_required(IntersystemTravelPhase phase) noexcept
    -> bool {
  return target_arrival_required(phase) || origin_arrival_required(phase);
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

[[nodiscard]] auto rule_profile_name(IntersystemRuleProfile value)
    -> std::string_view {
  switch (value) {
    case IntersystemRuleProfile::assisted: return "assisted";
    case IntersystemRuleProfile::pilot: return "pilot";
  }
  return "unknown";
}

[[nodiscard]] auto arrival_quality_name(IntersystemArrivalQuality value)
    -> std::string_view {
  switch (value) {
    case IntersystemArrivalQuality::aligned: return "aligned";
    case IntersystemArrivalQuality::offset: return "offset";
    case IntersystemArrivalQuality::opposed: return "opposed";
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

[[nodiscard]] auto read_i32(const Json& parent, std::string_view name,
                            std::string path)
    -> std::expected<std::int32_t, SaveSchemaError> {
  auto value = require_field(parent, name, path);
  if (!value) return std::unexpected{value.error()};
  if (!(*value)->is_number_integer() && !(*value)->is_number_unsigned()) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                   std::format("{}.{}", path, name),
                                   "expected a signed integer")};
  }
  if ((*value)->is_number_unsigned()) {
    const auto number = (*value)->get<std::uint64_t>();
    if (number > static_cast<std::uint64_t>(
                     std::numeric_limits<std::int32_t>::max())) {
      return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                     std::format("{}.{}", path, name),
                                     "integer exceeds int32 range")};
    }
    return static_cast<std::int32_t>(number);
  }
  const auto number = (*value)->get<std::int64_t>();
  if (number < std::numeric_limits<std::int32_t>::min() ||
      number > std::numeric_limits<std::int32_t>::max()) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                   std::format("{}.{}", path, name),
                                   "integer exceeds int32 range")};
  }
  return static_cast<std::int32_t>(number);
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

[[nodiscard]] auto read_rule_profile(const Json& parent,
                                     std::string_view name,
                                     std::string path)
    -> std::expected<IntersystemRuleProfile, SaveSchemaError> {
  auto value = read_string(parent, name, path);
  if (!value) return std::unexpected{value.error()};
  if (*value == "assisted") return IntersystemRuleProfile::assisted;
  if (*value == "pilot") return IntersystemRuleProfile::pilot;
  return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                 std::format("{}.{}", path, name),
                                 "unknown intersystem rule profile")};
}

[[nodiscard]] auto read_arrival_quality(const Json& parent,
                                        std::string_view name,
                                        std::string path)
    -> std::expected<IntersystemArrivalQuality, SaveSchemaError> {
  auto value = read_string(parent, name, path);
  if (!value) return std::unexpected{value.error()};
  if (*value == "aligned") return IntersystemArrivalQuality::aligned;
  if (*value == "offset") return IntersystemArrivalQuality::offset;
  if (*value == "opposed") return IntersystemArrivalQuality::opposed;
  return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                 std::format("{}.{}", path, name),
                                 "unknown intersystem arrival quality")};
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
  Json jump_alignment = nullptr;
  if (state.jump_alignment) {
    const auto& alignment = *state.jump_alignment;
    jump_alignment = Json{
        {"heading_error_millidegrees",
         alignment.heading_error_millidegrees},
        {"velocity_error_basis_points",
         alignment.velocity_error_basis_points},
        {"controls", Json{{"forward", alignment.controls.forward},
                           {"backward", alignment.controls.backward},
                           {"turn_left", alignment.controls.turn_left},
                           {"turn_right", alignment.controls.turn_right}}},
    };
  }
  Json arrival_solution = nullptr;
  if (state.arrival_solution) {
    const auto& arrival = *state.arrival_solution;
    Json reference_planet = nullptr;
    if (arrival.reference_planet) {
      reference_planet = encoded_id("planet-", *arrival.reference_planet);
    }
    Json assessment = nullptr;
    if (arrival.assessment) {
      assessment = Json{
          {"heading_error_millidegrees",
           arrival.assessment->heading_error_millidegrees},
          {"velocity_error_basis_points",
           arrival.assessment->velocity_error_basis_points},
          {"quality", arrival_quality_name(arrival.assessment->quality)},
      };
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
        {"assessment", std::move(assessment)},
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
      {"rule_profile", rule_profile_name(state.rule_profile)},
      {"current_system_id", encoded_id("system-", state.current_system)},
      {"current_planet_id", std::move(current_planet)},
      {"committed_jump_destination_id", std::move(committed_destination)},
      {"phase_started_tick", std::move(phase_started_tick)},
      {"jump_alignment", std::move(jump_alignment)},
      {"arrival_solution", std::move(arrival_solution)},
  };
}

[[nodiscard]] auto decode_intersystem_contract(const Json& json,
                                               Seed universe_seed)
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
  auto rule_profile =
      read_rule_profile(json, "rule_profile", std::string{path});
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
  if (!rule_profile) return std::unexpected{rule_profile.error()};
  if (!current_system) return std::unexpected{current_system.error()};
  if (!current_planet) return std::unexpected{current_planet.error()};
  if (!destination) return std::unexpected{destination.error()};
  if (!phase_tick) return std::unexpected{phase_tick.error()};

  std::optional<IntersystemJumpAlignmentState> jump_alignment;
  {
    auto alignment_field = require_field(json, "jump_alignment",
                                         std::string{path});
    if (!alignment_field) return std::unexpected{alignment_field.error()};
    if (!(**alignment_field).is_null()) {
      if (!(**alignment_field).is_object()) {
        return std::unexpected{failure(
            SaveSchemaErrorCode::invalid_type,
            "$.state.intersystem_contract.jump_alignment",
            "expected an object or null")};
      }
      constexpr std::string_view alignment_path{
          "$.state.intersystem_contract.jump_alignment"};
      auto heading = read_i32(**alignment_field,
                              "heading_error_millidegrees",
                              std::string{alignment_path});
      auto velocity = read_i32(**alignment_field,
                               "velocity_error_basis_points",
                               std::string{alignment_path});
      auto controls = read_object(**alignment_field, "controls",
                                  std::string{alignment_path});
      if (!heading) return std::unexpected{heading.error()};
      if (!velocity) return std::unexpected{velocity.error()};
      if (!controls) return std::unexpected{controls.error()};
      auto forward = read_bool(**controls, "forward",
                               std::format("{}.controls", alignment_path));
      auto backward = read_bool(**controls, "backward",
                                std::format("{}.controls", alignment_path));
      auto turn_left = read_bool(**controls, "turn_left",
                                 std::format("{}.controls", alignment_path));
      auto turn_right = read_bool(**controls, "turn_right",
                                  std::format("{}.controls", alignment_path));
      if (!forward) return std::unexpected{forward.error()};
      if (!backward) return std::unexpected{backward.error()};
      if (!turn_left) return std::unexpected{turn_left.error()};
      if (!turn_right) return std::unexpected{turn_right.error()};
      jump_alignment = IntersystemJumpAlignmentState{
          .heading_error_millidegrees = *heading,
          .velocity_error_basis_points = *velocity,
          .controls = {.forward = *forward,
                       .backward = *backward,
                       .turn_left = *turn_left,
                       .turn_right = *turn_right},
      };
    }
  }

  std::optional<IntersystemArrivalSolution> arrival_solution;
  {
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
      std::optional<IntersystemArrivalAssessment> assessment;
      {
        auto assessment_field = require_field(
            arrival, "assessment", std::string{arrival_path});
        if (!assessment_field) {
          return std::unexpected{assessment_field.error()};
        }
        if (!(**assessment_field).is_null()) {
          if (!(**assessment_field).is_object()) {
            return std::unexpected{failure(
                SaveSchemaErrorCode::invalid_type,
                "$.state.intersystem_contract.arrival_solution.assessment",
                "expected an object or null")};
          }
          constexpr std::string_view assessment_path{
              "$.state.intersystem_contract.arrival_solution.assessment"};
          auto heading = read_i32(**assessment_field,
                                  "heading_error_millidegrees",
                                  std::string{assessment_path});
          auto velocity_error = read_i32(**assessment_field,
                                         "velocity_error_basis_points",
                                         std::string{assessment_path});
          auto quality = read_arrival_quality(**assessment_field, "quality",
                                              std::string{assessment_path});
          if (!heading) return std::unexpected{heading.error()};
          if (!velocity_error) {
            return std::unexpected{velocity_error.error()};
          }
          if (!quality) return std::unexpected{quality.error()};
          assessment = IntersystemArrivalAssessment{
              .heading_error_millidegrees = *heading,
              .velocity_error_basis_points = *velocity_error,
              .quality = *quality,
          };
        }
      }
      arrival_solution = IntersystemArrivalSolution{
          .destination = *arrival_destination,
          .reference_planet = *reference_planet,
          .arrival_tick = *arrival_tick,
          .position = {*px, *py, *pz},
          .velocity = {*vx, *vy, *vz},
          .assessment = assessment,
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
      .rule_profile = *rule_profile,
      .current_system = *current_system,
      .current_planet = *current_planet,
      .committed_jump_destination = *destination,
      .phase_started_tick = *phase_tick,
      .jump_alignment = std::move(jump_alignment),
      .arrival_solution = std::move(arrival_solution),
  };
  if (arrival_required(state.travel_phase) && !state.arrival_solution) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::invalid_state,
        "$.state.intersystem_contract.arrival_solution",
        "current travel phase requires an immutable arrival solution")};
  }
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
      {"thermal",
       Json{{"load_units", state.thermal.load_units},
            {"abort_latched", state.thermal.abort_latched}}},
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
  auto thermal = read_object(json, "thermal", std::string{path});
  if (!thermal) return std::unexpected{thermal.error()};
  auto thermal_load =
      read_u32(**thermal, "load_units", "$.state.flight.thermal");
  auto thermal_abort =
      read_bool(**thermal, "abort_latched", "$.state.flight.thermal");
  if (!tick) return std::unexpected{tick.error()};
  if (!planet) return std::unexpected{planet.error()};
  if (!pose) return std::unexpected{pose.error()};
  if (!velocity) return std::unexpected{velocity.error()};
  if (!clearance) return std::unexpected{clearance.error()};
  if (!mode) return std::unexpected{mode.error()};
  if (!controls) return std::unexpected{controls.error()};
  if (!regime) return std::unexpected{regime.error()};
  if (!transition) return std::unexpected{transition.error()};
  if (!thermal_load) return std::unexpected{thermal_load.error()};
  if (!thermal_abort) return std::unexpected{thermal_abort.error()};

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
      .thermal = {*thermal_load, *thermal_abort},
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

[[nodiscard]] auto encode_origin_return(const OriginReturnState& state)
    -> Json {
  return Json{
      {"tick", decimal(state.tick)},
      {"system_id", encoded_id("system-", state.system)},
      {"station_id", encoded_id("station-", state.station)},
      {"station_relative_position_metres",
       Json{{"x", decimal(state.relative_position.x)},
            {"y", decimal(state.relative_position.y)},
            {"z", decimal(state.relative_position.z)}}},
      {"station_relative_velocity_metres_per_second",
       Json{{"x", decimal(state.relative_velocity.x)},
            {"y", decimal(state.relative_velocity.y)},
            {"z", decimal(state.relative_velocity.z)}}},
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
  };
}

[[nodiscard]] auto decode_origin_return(const Json& json)
    -> std::expected<OriginReturnState, SaveSchemaError> {
  constexpr std::string_view path{"$.state.origin_return"};
  if (!json.is_object()) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                   std::string{path}, "expected an object")};
  }
  auto tick = read_u64(json, "tick", std::string{path});
  auto system =
      read_id<SystemId>(json, "system_id", std::string{path}, "system-");
  auto station = read_id<OriginStationId>(json, "station_id", std::string{path},
                                          "station-");
  auto position =
      read_object(json, "station_relative_position_metres", std::string{path});
  auto velocity = read_object(
      json, "station_relative_velocity_metres_per_second", std::string{path});
  auto forward = read_object(json, "forward", std::string{path});
  auto up = read_object(json, "up", std::string{path});
  auto mode = read_mode(json, "mode", std::string{path});
  auto controls = read_object(json, "controls", std::string{path});
  if (!tick) return std::unexpected{tick.error()};
  if (!system) return std::unexpected{system.error()};
  if (!station) return std::unexpected{station.error()};
  if (!position) return std::unexpected{position.error()};
  if (!velocity) return std::unexpected{velocity.error()};
  if (!forward) return std::unexpected{forward.error()};
  if (!up) return std::unexpected{up.error()};
  if (!mode) return std::unexpected{mode.error()};
  if (!controls) return std::unexpected{controls.error()};
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
  auto decoded_position = vector(
      **position, "$.state.origin_return.station_relative_position_metres");
  auto decoded_velocity = vector(
      **velocity,
      "$.state.origin_return.station_relative_velocity_metres_per_second");
  auto decoded_forward = vector(**forward, "$.state.origin_return.forward");
  auto decoded_up = vector(**up, "$.state.origin_return.up");
  if (!decoded_position) return std::unexpected{decoded_position.error()};
  if (!decoded_velocity) return std::unexpected{decoded_velocity.error()};
  if (!decoded_forward) return std::unexpected{decoded_forward.error()};
  if (!decoded_up) return std::unexpected{decoded_up.error()};
  const auto control = [&](std::string_view name)
      -> std::expected<bool, SaveSchemaError> {
    return read_bool(**controls, name, "$.state.origin_return.controls");
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
  return OriginReturnState{
      .tick = *tick,
      .system = *system,
      .station = *station,
      .relative_position = {(*decoded_position)[0], (*decoded_position)[1],
                            (*decoded_position)[2]},
      .relative_velocity = {(*decoded_velocity)[0], (*decoded_velocity)[1],
                            (*decoded_velocity)[2]},
      .forward = {(*decoded_forward)[0], (*decoded_forward)[1],
                  (*decoded_forward)[2]},
      .up = {(*decoded_up)[0], (*decoded_up)[1], (*decoded_up)[2]},
      .mode = *mode,
      .controls = {*control_forward, *backward, *turn_left, *turn_right,
                   *strafe_left, *strafe_right, *rise, *fall},
  };
}

[[nodiscard]] auto validate_legacy_signal_run_semantics(
    const SaveDocument& document) -> std::expected<void, SaveSchemaError> {
  const auto& state = document.state;
  if (state.first_objective == FirstObjectiveStatus::offered) {
    if (state.first_objective_target.value != 0) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state,
          "$.state.first_objective.target_signal_id",
          "an offered objective cannot bind a generated signal")};
    }
    if (!state.discoveries.empty()) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state, "$.state.discoveries[0]",
          "an offered objective cannot contain discovered signals")};
    }
    if (!state.world_deltas.empty()) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state, "$.state.world_deltas[0]",
          "an offered objective cannot contain generated-world deltas")};
    }
    return {};
  }

  if (state.first_objective_target.value == 0) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::invalid_state,
        "$.state.first_objective.target_signal_id",
        "an accepted objective requires a generated signal target")};
  }

  const auto system_seed =
      derive_seed(document.recipe.universe_seed, SeedDomain::system,
                  document.recipe.origin_system_ordinal);
  const auto planet_seed =
      derive_seed(system_seed, SeedDomain::planet,
                  document.recipe.active_planet_ordinal);
  const auto planet = generate_planet_descriptor(planet_seed);
  auto cache = TerrainTileCache::create();
  if (!cache) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::invalid_state, "$.state",
        "cannot regenerate the deterministic Signal Run catalog")};
  }
  auto catalog = generate_surface_signals(planet, *cache);
  if (!catalog) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::invalid_state, "$.state",
        "cannot regenerate the deterministic Signal Run catalog")};
  }
  const auto signal_in_catalog = [&](SurfaceSignalId id) {
    for (const auto& signal : catalog->signals) {
      if (signal.id == id) return true;
    }
    return false;
  };
  if (!signal_in_catalog(state.first_objective_target)) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::identity_mismatch,
        "$.state.first_objective.target_signal_id",
        "objective target does not match the deterministic signal catalog")};
  }

  std::unordered_map<std::uint64_t, SimulationTick> discovery_ticks;
  discovery_ticks.reserve(state.discoveries.size());
  for (std::size_t index = 0; index < state.discoveries.size(); ++index) {
    const auto& discovery = state.discoveries[index];
    if (!signal_in_catalog(discovery.signal)) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::identity_mismatch,
          std::format("$.state.discoveries[{}].signal_id", index),
          "discovery does not match the deterministic signal catalog")};
    }
    if (state.flight && discovery.tick > state.flight->tick) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state,
          std::format("$.state.discoveries[{}].tick", index),
          "discovery tick cannot be later than the active flight tick")};
    }
    discovery_ticks.emplace(discovery.signal.value, discovery.tick);
  }

  for (std::size_t index = 0; index < state.world_deltas.size(); ++index) {
    const auto& delta = state.world_deltas[index];
    const auto signal = parse_surface_signal_object_key(delta.object_key);
    if (!signal || !signal_in_catalog(*signal)) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::identity_mismatch,
          std::format("$.state.world_deltas[{}].object_key", index),
          "world delta does not match the deterministic signal catalog")};
    }
    if (state.flight && delta.tick > state.flight->tick) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state,
          std::format("$.state.world_deltas[{}].tick", index),
          "world-delta tick cannot be later than the active flight tick")};
    }
    const auto discovered = discovery_ticks.find(signal->value);
    if (discovered != discovery_ticks.end() &&
        delta.tick < discovered->second) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state,
          std::format("$.state.world_deltas[{}].tick", index),
          "world-delta tick cannot precede discovery of the same signal")};
    }
  }

  if (!discovery_ticks.contains(state.first_objective_target.value)) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::invalid_state, "$.state.discoveries",
        "the objective target must have a matching discovery")};
  }
  const auto journal = WorldDeltaJournal::create(state.world_deltas);
  if (!journal) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::invalid_state, "$.state.world_deltas",
        "world deltas cannot form a valid Signal Run journal")};
  }
  const auto target_key =
      surface_signal_object_key(state.first_objective_target);
  const auto* target_delta = journal->state(target_key);
  if (state.first_objective == FirstObjectiveStatus::active) {
    if (target_delta &&
        target_delta->kind != SaveWorldDeltaKind::discovered) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state,
          "$.state.first_objective.status",
          "an active objective cannot contain a terminal target delta")};
    }
  } else if (target_delta == nullptr ||
             target_delta->kind != SaveWorldDeltaKind::collected) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::invalid_state, "$.state.world_deltas",
        "a completed objective requires a collected target delta")};
  }
  return {};
}

}  // namespace

auto current_save_generator_versions() noexcept -> SaveGeneratorVersions {
  return SaveGeneratorVersions{
      .seed_derivation = kSeedDerivationVersion,
      .planet_descriptor = kPlanetGeneratorVersion,
      .origin_home_planet = kOriginHomePlanetGeneratorVersion,
      .terrain_tiles = kTerrainTileGeneratorVersion,
      .origin_station = kOriginStationGeneratorVersion,
      .surface_signals = kSurfaceSignalGeneratorVersion,
      .local_sun = kLocalSunGeneratorVersion,
      .local_system = kLocalSystemGeneratorVersion,
      .analytic_ephemeris = kAnalyticEphemerisVersion,
      .intersystem_contract = kIntersystemContractVersion,
      .intersystem_jump = kIntersystemJumpVersion,
      .system_flight = kSystemFlightVersion,
      .origin_return = kOriginReturnVersion,
  };
}

auto make_save_recipe(Seed universe_seed, std::uint64_t active_planet_ordinal)
    -> SaveRecipe {
  const auto system_seed =
      derive_seed(universe_seed, SeedDomain::system, kOriginSystemOrdinal);
  const auto planet_seed =
      derive_seed(system_seed, SeedDomain::planet, active_planet_ordinal);
  const auto station = generate_origin_station(universe_seed);
  const auto home = generate_origin_home_planet(system_seed);
  const auto planet = generate_planet_descriptor(planet_seed);
  return SaveRecipe{
      .universe_seed = universe_seed,
      .origin_system_ordinal = kOriginSystemOrdinal,
      .home_planet_ordinal = kOriginHomePlanetOrdinal,
      .active_planet_ordinal = active_planet_ordinal,
      .generator_versions = current_save_generator_versions(),
      .origin_station = station.id,
      .home_planet = home.id,
      .station_host_planet = station.orbit.host_planet,
      .station_orbit = station.orbit,
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
  if (document.recipe.home_planet_ordinal != kOriginHomePlanetOrdinal) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::invalid_value, "$.recipe.home_planet_ordinal",
        "current save formats support one tutorial home at ordinal zero")};
  }
  const auto expected = make_save_recipe(document.recipe.universe_seed,
                                         document.recipe.active_planet_ordinal);
  if (document.recipe.origin_station != expected.origin_station) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::identity_mismatch, "$.recipe.origin_station_id",
        "stored origin station does not match deterministic regeneration")};
  }
  if (document.recipe.home_planet != expected.home_planet ||
      document.recipe.station_host_planet != expected.station_host_planet ||
      document.recipe.station_orbit != expected.station_orbit) {
    return std::unexpected{failure(SaveSchemaErrorCode::identity_mismatch,
                                   "$.recipe.station_orbit",
                                   "stored home planet or station orbit does "
                                   "not match deterministic regeneration")};
  }
  if (document.recipe.active_planet != expected.active_planet) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::identity_mismatch, "$.recipe.active_planet_id",
        "stored active planet does not match deterministic regeneration")};
  }
  if (document.state.intersystem_contract) {
    const auto& contract = *document.state.intersystem_contract;
    if (contract.identities.universe_seed != document.recipe.universe_seed) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state, "$.state.intersystem_contract",
          "intersystem contract does not match the save recipe or state machine")};
    }
    if (arrival_required(contract.travel_phase) &&
        !contract.arrival_solution) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state,
          "$.state.intersystem_contract.arrival_solution",
          "current travel phase requires an immutable arrival solution")};
    }
    if (!validate_intersystem_contract_state(contract)) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state, "$.state.intersystem_contract",
          "intersystem contract does not match the save recipe or state machine")};
    }
    if (contract.arrival_solution) {
      const auto destination =
          target_arrival_required(contract.travel_phase)
              ? generate_local_system(contract.identities.target_system_seed)
              : generate_origin_system(contract.identities.universe_seed);
      if (!validate_intersystem_arrival_solution(contract, destination,
                                                 *contract.arrival_solution)) {
        return std::unexpected{
            failure(SaveSchemaErrorCode::invalid_state,
                    "$.state.intersystem_contract.arrival_solution",
                    "intersystem arrival solution does not match deterministic "
                    "regeneration")};
      }
    }
    const bool target_system_flight =
        contract.travel_phase == IntersystemTravelPhase::target_system_flight;
    const bool target_planet_flight =
        contract.travel_phase == IntersystemTravelPhase::target_planet_flight;
    const bool return_jump_spooling =
        contract.travel_phase == IntersystemTravelPhase::return_jump_spooling;
    const bool origin_system_return =
        contract.travel_phase == IntersystemTravelPhase::origin_system_return;
    if (target_system_flight) {
      if (!document.state.system_flight || document.state.flight ||
          document.state.origin_return) {
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
          document.state.origin_return ||
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
      if (contract.rule_profile == IntersystemRuleProfile::assisted &&
          document.state.flight->thermal.abort_latched) {
        return std::unexpected{failure(
            SaveSchemaErrorCode::invalid_state,
            "$.state.flight.thermal.abort_latched",
            "Assisted flight cannot contain a Pilot thermal-abort latch")};
      }
    } else if (return_jump_spooling) {
      if (!document.state.system_flight || document.state.flight ||
          document.state.origin_return || !contract.phase_started_tick ||
          document.state.system_flight->tick != *contract.phase_started_tick ||
          document.state.system_flight->target !=
              contract.identities.target_planet) {
        return std::unexpected{failure(
            SaveSchemaErrorCode::invalid_state, "$.state.system_flight",
            "return spooling requires its frozen target-system flight state")};
      }
      const auto system =
          generate_local_system(contract.identities.target_system_seed);
      if (!validate_system_flight_state(system,
                                        *document.state.system_flight)) {
        return std::unexpected{failure(
            SaveSchemaErrorCode::invalid_state, "$.state.system_flight",
            "return spool flight does not match the target system")};
      }
    } else if (origin_system_return) {
      const auto origin_system =
          generate_origin_system(contract.identities.universe_seed);
      if (document.state.flight || document.state.system_flight ||
          (contract.arrival_solution && !document.state.origin_return) ||
          (document.state.origin_return &&
           !validate_origin_return_state(contract, origin_system,
                                         *document.state.origin_return))) {
        return std::unexpected{failure(
            SaveSchemaErrorCode::invalid_state, "$.state.origin_return",
            "origin return requires exactly one matching station-approach state")};
      }
    } else if (document.state.flight || document.state.system_flight ||
               document.state.origin_return) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state, "$.state",
          "the current travel phase cannot contain an active craft state")};
    }
    for (const auto& discovery : document.state.discoveries) {
      if (discovery.signal != contract.identities.target_objective ||
          discovery.tick > contract.universe_tick) {
        return std::unexpected{failure(
            SaveSchemaErrorCode::invalid_state, "$.state.discoveries",
            "intersystem discoveries must name the bound target at a valid tick")};
      }
    }
    const bool objective_complete =
        contract.mission_phase ==
            IntersystemMissionPhase::objective_complete ||
        contract.mission_phase == IntersystemMissionPhase::returned ||
        contract.mission_phase == IntersystemMissionPhase::turned_in;
    const auto target_key =
        surface_signal_object_key(contract.identities.target_objective);
    const bool valid_completed_delta =
        document.state.world_deltas.size() == 1U &&
        document.state.world_deltas.front().object_key == target_key &&
        document.state.world_deltas.front().kind ==
            SaveWorldDeltaKind::collected &&
        document.state.world_deltas.front().tick <= contract.universe_tick;
    if ((objective_complete && !valid_completed_delta) ||
        (!objective_complete && !document.state.world_deltas.empty())) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state, "$.state",
          "intersystem objective state and its collected delta disagree")};
    }
  } else {
    if (document.state.system_flight || document.state.origin_return) {
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state, "$.state.system_flight",
          "legacy Signal Run saves cannot contain intersystem flight")};
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
      if (document.state.flight->thermal.abort_latched) {
        return std::unexpected{failure(
            SaveSchemaErrorCode::invalid_state,
            "$.state.flight.thermal.abort_latched",
            "legacy flight cannot contain a Pilot thermal-abort latch")};
      }
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
  if (!document.state.intersystem_contract) {
    return validate_legacy_signal_run_semantics(document);
  }
  return {};
}

auto encode_save_document_json(const SaveDocument& document)
    -> std::expected<std::string, SaveSchemaError> {
  if (auto valid = validate_save_document(document); !valid) {
    return std::unexpected{valid.error()};
  }
  if (!valid_application_version(kApplicationVersion)) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::invalid_value, "$.application_version",
        "the current build version cannot be encoded as save provenance")};
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
  Json origin_return = nullptr;
  if (document.state.origin_return) {
    origin_return = encode_origin_return(*document.state.origin_return);
  }
  Json state;
  if (document.state.intersystem_contract) {
    state = Json{
        {"career_kind", "intersystem_contract"},
        {"intersystem_contract",
         encode_intersystem_contract(*document.state.intersystem_contract)},
        {"flight", std::move(flight)},
        {"system_flight", std::move(system_flight)},
        {"origin_return", std::move(origin_return)},
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
        {"origin_return", std::move(origin_return)},
        {"discoveries", std::move(discoveries)},
        {"world_deltas", std::move(deltas)},
    };
  }
  const Json root{
      {"application", kSaveApplication},
      {"application_version", kApplicationVersion},
      {"format_version", kSaveFormatVersion},
      {"recipe",
       Json{{"universe_seed", decimal(document.recipe.universe_seed.value)},
            {"origin_system_ordinal",
             decimal(document.recipe.origin_system_ordinal)},
            {"home_planet_ordinal",
             decimal(document.recipe.home_planet_ordinal)},
            {"active_planet_ordinal",
             decimal(document.recipe.active_planet_ordinal)},
            {"generator_versions",
             Json{{"seed_derivation", versions.seed_derivation},
                  {"planet_descriptor", versions.planet_descriptor},
                  {"origin_home_planet", versions.origin_home_planet},
                  {"terrain_tiles", versions.terrain_tiles},
                  {"origin_station", versions.origin_station},
                  {"surface_signals", versions.surface_signals},
                  {"local_sun", versions.local_sun},
                  {"local_system", versions.local_system},
                  {"analytic_ephemeris", versions.analytic_ephemeris},
                  {"intersystem_contract",
                   versions.intersystem_contract},
                  {"intersystem_jump", versions.intersystem_jump},
                  {"system_flight", versions.system_flight},
                  {"origin_return", versions.origin_return}}},
            {"origin_station_id",
             encoded_id("station-", document.recipe.origin_station)},
            {"home_planet_id",
             encoded_id("planet-", document.recipe.home_planet)},
            {"station_host_planet_id",
             encoded_id("planet-", document.recipe.station_host_planet)},
            {"station_orbit",
             Json{{"radius_kilometres",
                   decimal(document.recipe.station_orbit.radius_kilometres)},
                  {"period_ticks",
                   decimal(document.recipe.station_orbit.period_ticks)},
                  {"epoch_phase_turns",
                   document.recipe.station_orbit.epoch_phase_turns},
                  {"inclination_microdegrees",
                   document.recipe.station_orbit.inclination_microdegrees},
                  {"ascending_node_turns",
                   document.recipe.station_orbit.ascending_node_turns}}},
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
  if (*format_version >= 1U && *format_version <= 11U) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::unsupported_alpha_format_version,
        "$.format_version",
        std::format(
            "save format {} predates the format-12 orbiting-home alpha reset "
            "and is not supported; the source file was not modified",
            *format_version))};
  }
  if (*format_version != kSaveFormatVersion) {
    std::string detail = std::format(
        "save format version {} is unsupported by this build", *format_version);
    if (const auto writer = root.find("application_version");
        writer != root.end() && writer->is_string()) {
      const auto& value = writer->get_ref<const std::string&>();
      if (valid_application_version(value)) {
        detail += std::format(" (written by Apsis Drift {})", value);
      }
    }
    return std::unexpected{failure(
        SaveSchemaErrorCode::unsupported_format_version, "$.format_version",
        std::move(detail))};
  }
  auto application_version = read_string(root, "application_version", "$");
  if (!application_version) {
    return std::unexpected{application_version.error()};
  }
  if (!valid_application_version(*application_version)) {
    return std::unexpected{failure(
        SaveSchemaErrorCode::invalid_value, "$.application_version",
        std::format(
            "application version must contain 1 to {} printable ASCII bytes",
            kMaximumSaveApplicationVersionBytes))};
  }
  auto recipe_json = read_object(root, "recipe", "$");
  auto state_json = read_object(root, "state", "$");
  if (!recipe_json) return std::unexpected{recipe_json.error()};
  if (!state_json) return std::unexpected{state_json.error()};

  auto universe_seed = read_u64(**recipe_json, "universe_seed", "$.recipe");
  auto system_ordinal =
      read_u64(**recipe_json, "origin_system_ordinal", "$.recipe");
  auto home_ordinal =
      read_u64(**recipe_json, "home_planet_ordinal", "$.recipe");
  auto planet_ordinal =
      read_u64(**recipe_json, "active_planet_ordinal", "$.recipe");
  auto versions_json =
      read_object(**recipe_json, "generator_versions", "$.recipe");
  auto station = read_id<OriginStationId>(**recipe_json, "origin_station_id",
                                          "$.recipe", "station-");
  auto home =
      read_id<PlanetId>(**recipe_json, "home_planet_id", "$.recipe", "planet-");
  auto station_host = read_id<PlanetId>(**recipe_json, "station_host_planet_id",
                                        "$.recipe", "planet-");
  auto station_orbit = read_object(**recipe_json, "station_orbit", "$.recipe");
  auto planet = read_id<PlanetId>(**recipe_json, "active_planet_id", "$.recipe",
                                  "planet-");
  if (!universe_seed)
    return std::unexpected{universe_seed.error()};
  if (!system_ordinal)
    return std::unexpected{system_ordinal.error()};
  if (!home_ordinal)
    return std::unexpected{home_ordinal.error()};
  if (!planet_ordinal)
    return std::unexpected{planet_ordinal.error()};
  if (!versions_json)
    return std::unexpected{versions_json.error()};
  if (!station)
    return std::unexpected{station.error()};
  if (!home)
    return std::unexpected{home.error()};
  if (!station_host)
    return std::unexpected{station_host.error()};
  if (!station_orbit)
    return std::unexpected{station_orbit.error()};
  if (!planet)
    return std::unexpected{planet.error()};

  auto seed_version = read_u32(**versions_json, "seed_derivation",
                               "$.recipe.generator_versions");
  auto planet_version = read_u32(**versions_json, "planet_descriptor",
                                 "$.recipe.generator_versions");
  auto home_version = read_u32(**versions_json, "origin_home_planet",
                               "$.recipe.generator_versions");
  auto terrain_version =
      read_u32(**versions_json, "terrain_tiles", "$.recipe.generator_versions");
  auto station_version = read_u32(**versions_json, "origin_station",
                                  "$.recipe.generator_versions");
  auto signal_version = read_u32(**versions_json, "surface_signals",
                                 "$.recipe.generator_versions");
  auto sun_version = read_u32(**versions_json, "local_sun",
                              "$.recipe.generator_versions");
  auto system_version = read_u32(**versions_json, "local_system",
                                 "$.recipe.generator_versions");
  auto ephemeris_version = read_u32(**versions_json, "analytic_ephemeris",
                                    "$.recipe.generator_versions");
  auto contract_version = read_u32(**versions_json, "intersystem_contract",
                                   "$.recipe.generator_versions");
  auto jump_version = read_u32(**versions_json, "intersystem_jump",
                               "$.recipe.generator_versions");
  auto system_flight_version =
      read_u32(**versions_json, "system_flight", "$.recipe.generator_versions");
  auto origin_return_version =
      read_u32(**versions_json, "origin_return", "$.recipe.generator_versions");
  if (!seed_version)
    return std::unexpected{seed_version.error()};
  if (!planet_version)
    return std::unexpected{planet_version.error()};
  if (!home_version)
    return std::unexpected{home_version.error()};
  if (!terrain_version)
    return std::unexpected{terrain_version.error()};
  if (!station_version)
    return std::unexpected{station_version.error()};
  if (!signal_version)
    return std::unexpected{signal_version.error()};
  if (!sun_version)
    return std::unexpected{sun_version.error()};
  if (!system_version)
    return std::unexpected{system_version.error()};
  if (!ephemeris_version)
    return std::unexpected{ephemeris_version.error()};
  if (!contract_version)
    return std::unexpected{contract_version.error()};
  if (!jump_version)
    return std::unexpected{jump_version.error()};
  if (!system_flight_version) {
    return std::unexpected{system_flight_version.error()};
  }
  if (!origin_return_version) {
    return std::unexpected{origin_return_version.error()};
  }
  SaveGeneratorVersions versions{
      .seed_derivation = *seed_version,
      .planet_descriptor = *planet_version,
      .origin_home_planet = *home_version,
      .terrain_tiles = *terrain_version,
      .origin_station = *station_version,
      .surface_signals = *signal_version,
      .local_sun = *sun_version,
      .local_system = *system_version,
      .analytic_ephemeris = *ephemeris_version,
      .intersystem_contract = *contract_version,
      .intersystem_jump = *jump_version,
      .system_flight = *system_flight_version,
      .origin_return = *origin_return_version,
  };
  if (versions != current_save_generator_versions()) {
    return std::unexpected{
        failure(SaveSchemaErrorCode::incompatible_generator_version,
                "$.recipe.generator_versions",
                "save requires a generator version unsupported by this build")};
  }

  auto station_radius =
      read_u64(**station_orbit, "radius_kilometres", "$.recipe.station_orbit");
  auto station_period =
      read_u64(**station_orbit, "period_ticks", "$.recipe.station_orbit");
  auto station_phase =
      read_u32(**station_orbit, "epoch_phase_turns", "$.recipe.station_orbit");
  auto station_inclination = read_i32(
      **station_orbit, "inclination_microdegrees", "$.recipe.station_orbit");
  auto station_node = read_u32(**station_orbit, "ascending_node_turns",
                               "$.recipe.station_orbit");
  if (!station_radius)
    return std::unexpected{station_radius.error()};
  if (!station_period)
    return std::unexpected{station_period.error()};
  if (!station_phase)
    return std::unexpected{station_phase.error()};
  if (!station_inclination) {
    return std::unexpected{station_inclination.error()};
  }
  if (!station_node)
    return std::unexpected{station_node.error()};

  auto career_kind = read_string(**state_json, "career_kind", "$.state");
  if (!career_kind) return std::unexpected{career_kind.error()};
  const bool intersystem = *career_kind == "intersystem_contract";
  if (!intersystem && *career_kind != "legacy_signal_run") {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                   "$.state.career_kind",
                                   "unknown save career kind")};
  }
  auto flight_json = require_field(**state_json, "flight", "$.state");
  auto system_flight_json =
      require_field(**state_json, "system_flight", "$.state");
  auto origin_return_json =
      require_field(**state_json, "origin_return", "$.state");
  auto discoveries_json = read_array(**state_json, "discoveries", "$.state");
  auto deltas_json = read_array(**state_json, "world_deltas", "$.state");
  if (!flight_json) return std::unexpected{flight_json.error()};
  if (!system_flight_json) {
    return std::unexpected{system_flight_json.error()};
  }
  if (!origin_return_json) {
    return std::unexpected{origin_return_json.error()};
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
        decode_intersystem_contract(**contract_json, Seed{*universe_seed});
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
  if (!(**system_flight_json).is_null()) {
    auto decoded = decode_system_flight(**system_flight_json);
    if (!decoded) return std::unexpected{decoded.error()};
    system_flight = std::move(*decoded);
  }
  std::optional<OriginReturnState> origin_return;
  if (!(**origin_return_json).is_null()) {
    auto decoded = decode_origin_return(**origin_return_json);
    if (!decoded) return std::unexpected{decoded.error()};
    origin_return = std::move(*decoded);
  }
  std::vector<SaveDiscovery> discoveries;
  if ((**discoveries_json).size() > kMaximumSaveDiscoveries) {
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                   "$.state.discoveries",
                                   "discovery count exceeds the save bound")};
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
                                   "world-delta count exceeds the save bound")};
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
              .home_planet_ordinal = *home_ordinal,
              .active_planet_ordinal = *planet_ordinal,
              .generator_versions = versions,
              .origin_station = *station,
              .home_planet = *home,
              .station_host_planet = *station_host,
              .station_orbit =
                  OriginStationOrbit{
                      .host_planet = *station_host,
                      .radius_kilometres = *station_radius,
                      .period_ticks = *station_period,
                      .epoch_phase_turns = *station_phase,
                      .inclination_microdegrees = *station_inclination,
                      .ascending_node_turns = *station_node,
                  },
              .active_planet = *planet,
          },
      .state =
          SaveMutableState{
              .location = location,
              .first_objective = objective,
              .first_objective_target = target,
              .flight = std::move(flight),
              .system_flight = std::move(system_flight),
              .origin_return = std::move(origin_return),
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
