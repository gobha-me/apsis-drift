#include "apsis_drift/freedom_flight_save.hpp"

#include "apsis_drift/universe_navigation.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <utility>

namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;
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
auto failure(SaveSchemaErrorCode code, std::string path, std::string detail)
    -> SaveSchemaError {
  return {code, std::move(path), std::move(detail)};
}
auto fields(const Json& object, std::initializer_list<std::string_view> names,
            std::string_view path) -> std::expected<void, SaveSchemaError> {
  if (!object.is_object())
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                   std::string{path}, "expected an object")};
  if (object.size() != names.size())
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                   std::string{path},
                                   "unexpected or missing field")};
  for (auto name : names)
    if (!object.contains(std::string{name}))
      return std::unexpected{failure(SaveSchemaErrorCode::missing_field,
                                     std::format("{}.{}", path, name),
                                     "required field is missing")};
  return {};
}
auto version(const Json& object, std::string_view name)
    -> std::expected<std::uint32_t, SaveSchemaError> {
  const auto& x = object.at(std::string{name});
  if (!x.is_number_unsigned() ||
      x.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                   std::format("$.flight_model.{}", name),
                                   "expected an unsigned u32 version")};
  return x.get<std::uint32_t>();
}
auto decimal(const Json& x, std::string_view path =
                                "$.flight_model.orbit_hold.target.planet_id")
    -> std::expected<std::uint64_t, SaveSchemaError> {
  if (!x.is_string())
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                   std::string{path},
                                   "expected canonical decimal string")};
  const auto& digits = x.get_ref<const std::string&>();
  std::uint64_t result{};
  auto read =
      std::from_chars(digits.data(), digits.data() + digits.size(), result);
  if (digits.empty() || (digits.size() > 1 && digits.front() == '0') ||
      read.ec != std::errc{} || read.ptr != digits.data() + digits.size())
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                   std::string{path},
                                   "malformed or overflowing identity")};
  return result;
}
auto number(const Json& x, std::string path)
    -> std::expected<double, SaveSchemaError> {
  if (!x.is_number())
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                   std::move(path),
                                   "expected finite numerical component")};
  const double value = x.get<double>();
  if (!std::isfinite(value))
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_value,
                                   std::move(path),
                                   "non-finite numerical component")};
  return value;
}
auto resolve_world(const FreedomSaveDocument& origin,
                   const FreedomFlightModel& model,
                   const std::optional<FreedomActiveWorldSelection>& world)
    -> std::expected<PhysicalLocalSystem, SaveSchemaError> {
  const auto seed = origin.recipe.universe_seed;
  if (world && (world->version != kFreedomActiveWorldVersion ||
                model.physical_catalog != 2 || model.physical_ephemeris != 2))
    return std::unexpected{failure(
        SaveSchemaErrorCode::incompatible_generator_version, "$.world_owner",
        "unsupported active-world policy or physical recipe")};
  const auto ids = generate_first_intersystem_identities(seed);
  if (world && world->system != ids.origin_system &&
      world->system != ids.target_system)
    return std::unexpected{
        failure(SaveSchemaErrorCode::invalid_state, "$.world_owner.system_id",
                "active world is outside the selected seeded pair")};
  auto system =
      world && world->system == ids.target_system
          ? generate_physical_local_system(ids.target_system_seed,
                                           model.physical_catalog)
          : generate_physical_origin_system(seed, model.physical_catalog);
  if (!system)
    return std::unexpected{
        failure(SaveSchemaErrorCode::incompatible_generator_version,
                "$.flight_model.physical_catalog",
                "physical catalog selection is unsupported")};
  return std::move(*system);
}
} // namespace

auto hydrate_freedom_flight_document(const FreedomFlightSaveDocument& document)
    -> std::expected<FreedomFlightHydration, SaveSchemaError> {
  if (auto valid = validate_freedom_save_document(document.origin); !valid)
    return std::unexpected{valid.error()};
  if (document.origin.state.tick != document.flight.tick ||
      document.flight.tick >= std::numeric_limits<SimulationTick>::max() - 1)
    return std::unexpected{
        failure(SaveSchemaErrorCode::invalid_state, "$.flight.tick",
                "history/flight clocks differ or flight cannot advance")};
  const auto& model = document.model;
  auto system = resolve_world(document.origin, model, document.world);
  if (!system || system->ephemeris_version != model.physical_ephemeris ||
      model.rotation_owner != kPhysicalPlanetRotationOwnerVersion)
    return std::unexpected{failure(
        SaveSchemaErrorCode::incompatible_generator_version, "$.flight_model",
        "physical catalog, ephemeris or rotation owner is unsupported")};
  const auto planet = document.world ? document.world->planet
                                     : document.origin.recipe.home_planet;
  if (document.flight.frame.kind != RigidFrameKind::planet_relative_inertial ||
      document.flight.frame.system != system->catalog.id ||
      document.flight.frame.planet != planet ||
      (document.world &&
       document.flight.craft !=
           CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion}))
    return std::unexpected{
        failure(SaveSchemaErrorCode::invalid_state, "$.flight.frame",
                "saved flight requires its explicitly selected "
                "physical planet in the nonrotating frame")};
  auto rotation = generate_planet_rotation_recipe(*system, planet,
                                                  model.rotation_generator);
  if (!rotation)
    return std::unexpected{
        failure(SaveSchemaErrorCode::incompatible_generator_version,
                "$.flight_model.rotation_generator",
                "physical rotation selection is unsupported")};
  const RigidBodyWorldContext context{*system};
  VacuumIntent intent;
  intent.assistance = model.assistance;
  const auto atmosphere =
      evaluate_atmospheric_flight(context, document.flight, intent, *rotation,
                                  model.atmosphere, model.central);
  if (!atmosphere)
    return std::unexpected{
        failure(SaveSchemaErrorCode::invalid_state, "$.flight",
                "flight state or atmospheric/central selection is invalid")};
  if (!validate_orbit_hold_request(
          context, document.flight, *rotation,
          {1, atmosphere->space_boundary_altitude_metres}, model.hold,
          model.central))
    return std::unexpected{
        failure(SaveSchemaErrorCode::invalid_state, "$.flight_model.orbit_hold",
                "hold selection/target is invalid for the saved physical body "
                "and atmosphere boundary")};
  return FreedomFlightHydration{std::move(*system), *rotation, *atmosphere};
}

auto encode_freedom_flight_document_json(
    const FreedomFlightSaveDocument& document)
    -> std::expected<std::string, SaveSchemaError> {
  const auto hydrated = hydrate_freedom_flight_document(document);
  if (!hydrated) return std::unexpected{hydrated.error()};
  auto origin = encode_freedom_save_document_json(document.origin);
  if (!origin) return std::unexpected{origin.error()};
  const RigidBodyWorldContext context{hydrated->system};
  const auto flight = encode_rigid_body_state_json(context, document.flight);
  if (!flight)
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_state,
                                   "$.flight",
                                   "rigid state cannot be encoded")};
  Json root = Json::parse(*origin);
  root["format_version"] = document.world
                               ? kFreedomActiveFlightSaveFormatVersion
                               : kFreedomFlightSaveFormatVersion;
  if (document.world)
    root["world_owner"] = {
        {"version", document.world->version},
        {"system_id", std::to_string(document.world->system.value)},
        {"planet_id", std::to_string(document.world->planet.value)}};
  root["state"]["location"] = "planetary_flight";
  root["flight"] = Json::parse(*flight);
  const auto& model = document.model;
  Json target = nullptr;
  if (model.hold.target) {
    const auto& t = *model.hold.target;
    target = {{"planet_id", std::to_string(t.planet.value)},
              {"radius_metres", t.radius_metres},
              {"plane_normal",
               {t.plane_normal.x, t.plane_normal.y, t.plane_normal.z}}};
  }
  root["flight_model"] = {
      {"physical_catalog", model.physical_catalog},
      {"physical_ephemeris", model.physical_ephemeris},
      {"rotation_owner", model.rotation_owner},
      {"rotation_generator", model.rotation_generator},
      {"central_body", model.central.version},
      {"atmosphere", model.atmosphere.version},
      {"orbit_hold", {{"version", model.hold.version}, {"target", target}}},
      {"assistance", model.assistance}};
  auto encoded = root.dump(2) + '\n';
  if (encoded.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{failure(SaveSchemaErrorCode::document_too_large, "$",
                                   "flight save exceeds the document limit")};
  return encoded;
}

auto decode_freedom_flight_document_json(std::string_view text)
    -> std::expected<FreedomFlightSaveDocument, SaveSchemaError> {
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{failure(SaveSchemaErrorCode::document_too_large, "$",
                                   "flight save exceeds the document limit")};
  if (!bounded_nesting(text))
    return std::unexpected{failure(SaveSchemaErrorCode::malformed_json, "$",
                                   "unbalanced or excessive JSON nesting")};
  bool duplicate = false;
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
    root = Json::parse(text.begin(), text.end(), callback, true, false);
  } catch (const Json::exception& e) {
    return std::unexpected{
        failure(SaveSchemaErrorCode::malformed_json, "$", e.what())};
  }
  if (duplicate)
    return std::unexpected{failure(SaveSchemaErrorCode::duplicate_key, "$",
                                   "JSON objects cannot repeat a key")};
  // Dispatch before shape checks so strict existing v16/v17 decoders keep their
  // meanings. This is not fallback from a damaged format18 document.
  if (!root.is_object() || !root.contains("format_version") ||
      !root["format_version"].is_number_unsigned())
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                   "$.format_version",
                                   "expected unsigned save version")};
  const bool active =
      root["format_version"] == kFreedomActiveFlightSaveFormatVersion;
  if (root["format_version"] != kFreedomFlightSaveFormatVersion && !active)
    return std::unexpected{
        failure(SaveSchemaErrorCode::unsupported_format_version,
                "$.format_version", "unsupported Freedom flight save version")};
  auto shape =
      active ? fields(root,
                      {"application", "application_version", "format_version",
                       "mode", "recipe", "state", "flight", "flight_model",
                       "world_owner"},
                      "$")
             : fields(root,
                      {"application", "application_version", "format_version",
                       "mode", "recipe", "state", "flight", "flight_model"},
                      "$");
  if (!shape) return std::unexpected{shape.error()};
  std::optional<FreedomActiveWorldSelection> selection;
  if (active) {
    const auto& w = root["world_owner"];
    if (auto valid =
            fields(w, {"version", "system_id", "planet_id"}, "$.world_owner");
        !valid)
      return std::unexpected{valid.error()};
    if (!w["version"].is_number_unsigned() ||
        w["version"] != kFreedomActiveWorldVersion)
      return std::unexpected{
          failure(SaveSchemaErrorCode::incompatible_generator_version,
                  "$.world_owner.version", "unsupported active-world owner")};
    auto system = decimal(w["system_id"], "$.world_owner.system_id"),
         planet = decimal(w["planet_id"], "$.world_owner.planet_id");
    if (!system) return std::unexpected{system.error()};
    if (!planet) return std::unexpected{planet.error()};
    selection = FreedomActiveWorldSelection{
        kFreedomActiveWorldVersion, {*system}, {*planet}};
  }
  if (!root["state"].is_object() || !root["state"].contains("location") ||
      root["state"]["location"] != "planetary_flight")
    return std::unexpected{
        failure(SaveSchemaErrorCode::invalid_state, "$.state.location",
                "format18 requires explicit planetary_flight")};
  Json origin = root;
  origin.erase("flight");
  origin.erase("flight_model");
  origin.erase("world_owner");
  origin["format_version"] = kFreedomSaveFormatVersion;
  origin["state"]["location"] = "docked_at_origin";
  // Shared identity/history validation only; original format18 location was
  // already checked, and a flight projection is mandatory below.
  auto decoded = decode_freedom_save_document_json(origin.dump());
  if (!decoded) return std::unexpected{decoded.error()};
  const auto& m = root["flight_model"];
  if (auto valid =
          fields(m,
                 {"physical_catalog", "physical_ephemeris", "rotation_owner",
                  "rotation_generator", "central_body", "atmosphere",
                  "orbit_hold", "assistance"},
                 "$.flight_model");
      !valid)
    return std::unexpected{valid.error()};
  const auto catalog = version(m, "physical_catalog"),
             ephemeris = version(m, "physical_ephemeris"),
             owner = version(m, "rotation_owner"),
             rotation = version(m, "rotation_generator"),
             central = version(m, "central_body"),
             atmosphere = version(m, "atmosphere");
  for (const auto* v :
       {&catalog, &ephemeris, &owner, &rotation, &central, &atmosphere})
    if (!*v) return std::unexpected{v->error()};
  if (!m["assistance"].is_boolean())
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_type,
                                   "$.flight_model.assistance",
                                   "expected boolean runtime assistance")};
  const auto& hold = m["orbit_hold"];
  if (auto valid =
          fields(hold, {"version", "target"}, "$.flight_model.orbit_hold");
      !valid)
    return std::unexpected{valid.error()};
  const auto hold_version = version(hold, "version");
  if (!hold_version) return std::unexpected{hold_version.error()};
  FreedomFlightModel model{*catalog,
                           *ephemeris,
                           *owner,
                           *rotation,
                           {*central},
                           {*atmosphere},
                           {*hold_version, {}},
                           m["assistance"].get<bool>()};
  const auto& target = hold["target"];
  if (!target.is_null()) {
    if (auto valid =
            fields(target, {"planet_id", "radius_metres", "plane_normal"},
                   "$.flight_model.orbit_hold.target");
        !valid)
      return std::unexpected{valid.error()};
    auto planet = decimal(target["planet_id"]);
    if (!planet) return std::unexpected{planet.error()};
    auto radius = number(target["radius_metres"],
                         "$.flight_model.orbit_hold.target.radius_metres");
    if (!radius) return std::unexpected{radius.error()};
    const auto& normal = target["plane_normal"];
    if (!normal.is_array() || normal.size() != 3)
      return std::unexpected{
          failure(SaveSchemaErrorCode::invalid_type,
                  "$.flight_model.orbit_hold.target.plane_normal",
                  "expected three finite normal components")};
    auto x = number(normal[0],
                    "$.flight_model.orbit_hold.target.plane_normal[0]"),
         y = number(normal[1],
                    "$.flight_model.orbit_hold.target.plane_normal[1]"),
         z = number(normal[2],
                    "$.flight_model.orbit_hold.target.plane_normal[2]");
    if (!x) return std::unexpected{x.error()};
    if (!y) return std::unexpected{y.error()};
    if (!z) return std::unexpected{z.error()};
    model.hold.target = OrbitHoldTarget{{*planet}, *radius, {*x, *y, *z}};
  }
  auto system = resolve_world(*decoded, model, selection);
  if (!system)
    return std::unexpected{
        failure(SaveSchemaErrorCode::incompatible_generator_version,
                "$.flight_model.physical_catalog",
                "physical catalog selection is unsupported")};
  const RigidBodyWorldContext context{*system};
  auto flight = decode_rigid_body_state_json(context, root["flight"].dump());
  if (!flight)
    return std::unexpected{
        failure(SaveSchemaErrorCode::invalid_state, "$.flight",
                "rigid state/owner projection is malformed or incompatible")};
  FreedomFlightSaveDocument document{std::move(*decoded), *flight, model,
                                     selection};
  if (auto valid = hydrate_freedom_flight_document(document); !valid)
    return std::unexpected{valid.error()};
  return document;
}
} // namespace apsis_drift
