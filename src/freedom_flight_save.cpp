#include "apsis_drift/freedom_flight_save.hpp"
#include "apsis_drift/planet_optics.hpp"
#include "freedom_minor_flight_save_internal.hpp"

#include "freedom_moon_flight_save_internal.hpp"

#include "apsis_drift/freedom_system_network.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
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
  if (origin.network) {
    if (!world || world->version != kFreedomNetworkActiveWorldVersion ||
        model.physical_catalog != 2 || model.physical_ephemeris != 2)
      return std::unexpected{failure(
          SaveSchemaErrorCode::incompatible_generator_version, "$.world_owner",
          "network requires explicit active-world2 and physical recipe2")};
    const auto network =
        generate_freedom_system_network(seed, origin.network->version);
    if (!network)
      return std::unexpected{failure(SaveSchemaErrorCode::invalid_state,
                                     "$.network",
                                     "selected system network refused")};
    auto system = resolve_freedom_network_system(*network, world->system,
                                                 model.physical_catalog);
    if (!system)
      return std::unexpected{
          failure(SaveSchemaErrorCode::invalid_state, "$.world_owner.system_id",
                  "active world is outside selected network")};
    return std::move(*system);
  }
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
// Shared document checks precede any coordinate transformation. These do not
// authenticate a caller-supplied world; the public view does that separately.
auto validate_flight_origin(const FreedomFlightSaveDocument& document)
    -> std::expected<void, SaveSchemaError> {
  if (auto valid = validate_freedom_save_document(document.origin); !valid)
    return std::unexpected{valid.error()};
  if (document.origin.lineage && !document.world)
    return std::unexpected{
        failure(SaveSchemaErrorCode::invalid_state, "$.world_owner",
                "replacement lineage requires explicit world ownership")};
  if (document.origin.state.tick != document.flight.tick ||
      document.flight.tick >= std::numeric_limits<SimulationTick>::max() - 1)
    return std::unexpected{
        failure(SaveSchemaErrorCode::invalid_state, "$.flight.tick",
                "history/flight clocks differ or flight cannot advance")};
  if (document.model.optics != 0 &&
      document.model.optics != kPlanetOpticsVersion)
    return std::unexpected{failure(
        SaveSchemaErrorCode::incompatible_generator_version,
        "$.flight_model.optics", "unsupported planetary optics selection")};
  if (document.model.companions > 1 ||
      (document.model.companions &&
       document.model.optics != kPlanetOpticsVersion))
    return std::unexpected{failure(
        SaveSchemaErrorCode::incompatible_generator_version,
        "$.flight_model.companions",
        "distant companions require an explicit supported optical selection")};
  if (document.model.terrain_policy != kFreedomLegacyTerrainPolicy &&
      document.model.terrain_policy != kFreedomLocalDetailTerrainPolicy &&
      document.model.terrain_policy != kFreedomSeaDatumTerrainPolicy)
    return std::unexpected{
        failure(SaveSchemaErrorCode::incompatible_generator_version,
                "$.flight_model.terrain_policy",
                "unsupported physical terrain policy")};
  if (document.model.terrain_policy == kFreedomSeaDatumTerrainPolicy &&
      document.model.optics != kPlanetOpticsVersion)
    return std::unexpected{
        failure(SaveSchemaErrorCode::incompatible_generator_version,
                "$.flight_model.terrain_policy",
                "sea-datum terrain requires an explicit optics1 envelope")};
  return {};
}

// Private callers have already resolved this exact world from the document or
// compared the supplied complete owner with that resolved world. Keep the rigid
// and model checks here; hydration evaluates atmosphere and hold once
// afterward.
auto relative_state_for_selected_world(
    const FreedomFlightSaveDocument& document,
    const PhysicalLocalSystem& system)
    -> std::expected<RigidBodyState, SaveSchemaError> {
  const auto& model = document.model;
  if (system.ephemeris_version != model.physical_ephemeris ||
      model.rotation_owner != kPhysicalPlanetRotationOwnerVersion)
    return std::unexpected{failure(
        SaveSchemaErrorCode::incompatible_generator_version, "$.flight_model",
        "physical catalog, ephemeris or rotation owner is unsupported")};
  const auto planet = document.world ? document.world->planet
                                     : document.origin.recipe.home_planet;
  if ((model.frame_policy != kFreedomRelativeFramePolicy &&
       model.frame_policy != kFreedomReleaseSystemFramePolicy) ||
      (model.frame_policy == kFreedomReleaseSystemFramePolicy &&
       !document.world) ||
      model.central.version != kCentralBodyDynamicsVersion ||
      model.atmosphere.version != kAtmosphericFlightVersion ||
      model.hold.version != kOrbitHoldVersion ||
      !generate_planet_rotation_recipe(system, planet,
                                       model.rotation_generator))
    return std::unexpected{failure(
        SaveSchemaErrorCode::incompatible_generator_version, "$.flight_model",
        "unsupported frame/force/rotation selection")};
  if (document.flight.frame.system != system.catalog.id ||
      (document.world &&
       document.flight.craft !=
           CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion}) ||
      !find_local_system_planet(system, planet))
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_state,
                                   "$.flight.frame",
                                   "rigid body and force owner differ")};
  const bool relative =
      document.flight.frame.kind == RigidFrameKind::planet_relative_inertial;
  const bool selected_system =
      model.frame_policy == kFreedomReleaseSystemFramePolicy &&
      document.flight.frame.kind == RigidFrameKind::system_inertial;
  if ((!relative && !selected_system) ||
      (relative && document.flight.frame.planet != planet))
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_state,
                                   "$.flight.frame",
                                   "unsupported canonical flight frame")};
  const auto converted =
      reframe_rigid_body(RigidBodyWorldContext{system}, document.flight,
                         {{RigidFrameKind::planet_relative_inertial,
                           system.catalog.id,
                           planet,
                           {}},
                          document.flight.tick});
  if (!converted)
    return std::unexpected{
        failure(SaveSchemaErrorCode::invalid_state, "$.flight",
                "rigid state or same-tick conversion refused")};
  return *converted;
}
} // namespace

auto relative_freedom_flight_state(const FreedomFlightSaveDocument& document,
                                   const PhysicalLocalSystem& system)
    -> std::expected<RigidBodyState, SaveSchemaError> {
  if (document.minor) {
    const auto hydrated = hydrate_freedom_minor_flight_document(document);
    if (!hydrated) return std::unexpected{hydrated.error()};
    if (hydrated->minor.catalog.owner != system)
      return std::unexpected{failure(SaveSchemaErrorCode::invalid_state,
                                     "$.world_owner",
                                     "supplied minor owner differs")};
    return document.flight;
  }
  if (document.moon) {
    const auto hydrated = hydrate_freedom_moon_flight_document(document);
    if (!hydrated) return std::unexpected{hydrated.error()};
    if (hydrated->moon.catalog.owner != system)
      return std::unexpected{
          failure(SaveSchemaErrorCode::invalid_state, "$.world_owner",
                  "supplied physical owner differs from the saved moon")};
    return document.flight;
  }
  if (auto valid = validate_flight_origin(document); !valid)
    return std::unexpected{valid.error()};
  const auto& model = document.model;
  const auto expected_system =
      resolve_world(document.origin, model, document.world);
  if (!expected_system) return std::unexpected{expected_system.error()};
  if (*expected_system != system)
    return std::unexpected{
        failure(SaveSchemaErrorCode::invalid_state, "$.world_owner",
                "supplied physical owner differs from the saved world")};
  const auto relative = relative_state_for_selected_world(document, system);
  if (!relative) return std::unexpected{relative.error()};
  const auto planet = document.world ? document.world->planet
                                     : document.origin.recipe.home_planet;
  const auto rotation =
      generate_planet_rotation_recipe(system, planet, model.rotation_generator);
  const auto physical_planet = find_local_system_planet(system, planet);
  const auto profile = resolve_atmospheric_flight_profile(
      (*physical_planet)->descriptor, model.atmosphere);
  if (!rotation || !profile ||
      !validate_orbit_hold_request(RigidBodyWorldContext{system}, *relative,
                                   *rotation,
                                   {1, profile->space_boundary_altitude_metres},
                                   model.hold, model.central))
    return std::unexpected{failure(SaveSchemaErrorCode::invalid_state,
                                   "$.flight_model.orbit_hold",
                                   "invalid physical model or hold target")};
  return *relative;
}

auto hydrate_freedom_flight_document(const FreedomFlightSaveDocument& document)
    -> std::expected<FreedomFlightHydration, SaveSchemaError> {
  if (document.minor) {
    auto hydrated = hydrate_freedom_minor_flight_document(document);
    if (!hydrated) return std::unexpected{hydrated.error()};
    auto system = hydrated->minor.catalog.owner;
    return FreedomFlightHydration{
        std::move(system), {}, {}, {}, std::move(*hydrated)};
  }
  if (document.moon) {
    auto hydrated = hydrate_freedom_moon_flight_document(document);
    if (!hydrated) return std::unexpected{hydrated.error()};
    auto system = hydrated->moon.catalog.owner;
    return FreedomFlightHydration{
        std::move(system), {}, {}, std::move(*hydrated)};
  }
  auto system = resolve_world(document.origin, document.model, document.world);
  if (!system) return std::unexpected{system.error()};
  if (auto valid = validate_flight_origin(document); !valid)
    return std::unexpected{valid.error()};
  const auto relative = relative_state_for_selected_world(document, *system);
  if (!relative) return std::unexpected{relative.error()};
  const auto& model = document.model;
  const auto planet = document.world ? document.world->planet
                                     : document.origin.recipe.home_planet;
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
  const auto atmosphere = evaluate_atmospheric_flight(
      context, *relative, intent, *rotation, model.atmosphere, model.central);
  if (!atmosphere)
    return std::unexpected{
        failure(SaveSchemaErrorCode::invalid_state, "$.flight",
                "flight state or atmospheric/central selection is invalid")};
  if (!validate_orbit_hold_request(
          context, *relative, *rotation,
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
  if (document.minor) return detail::encode_minor_flight(document);
  if (document.moon) return detail::encode_moon_flight(document);
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
  root["format_version"] =
      document.origin.network ? kFreedomNetworkFlightSaveFormatVersion
      : document.model.optics ? kFreedomOpticalFlightSaveFormatVersion
      : document.model.frame_policy == kFreedomReleaseSystemFramePolicy
          ? kFreedomReleaseFlightSaveFormatVersion
      : document.model.terrain_policy == kFreedomLocalDetailTerrainPolicy
          ? kFreedomTerrainFlightSaveFormatVersion
      : document.world ? kFreedomActiveFlightSaveFormatVersion
                       : kFreedomFlightSaveFormatVersion;
  if (document.world)
    root["world_owner"] = {
        {"version", document.world->version},
        {"system_id", std::to_string(document.world->system.value)},
        {"planet_id", std::to_string(document.world->planet.value)}};
  root["state"]["location"] =
      document.flight.frame.kind == RigidFrameKind::system_inertial
          ? "system_flight"
          : "planetary_flight";
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
  if (document.origin.network ||
      model.terrain_policy == kFreedomLocalDetailTerrainPolicy ||
      model.frame_policy == kFreedomReleaseSystemFramePolicy || model.optics)
    root["flight_model"]["terrain_policy"] = model.terrain_policy;
  if (document.origin.network ||
      model.frame_policy == kFreedomReleaseSystemFramePolicy || model.optics)
    root["flight_model"]["frame_policy"] = model.frame_policy;
  if (document.origin.network || model.optics)
    root["flight_model"]["optics"] = model.optics;
  if (model.companions) root["flight_model"]["companions"] = model.companions;
  auto encoded = root.dump(2) + '\n';
  if (encoded.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{failure(SaveSchemaErrorCode::document_too_large, "$",
                                   "flight save exceeds the document limit")};
  return encoded;
}

namespace {
auto decode_flight(std::string_view text, bool system_import)
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
  if (!system_import &&
      (root["format_version"] == kFreedomMinorFlightSaveFormatVersion ||
       root["format_version"] == kFreedomNetworkMinorFlightSaveFormatVersion))
    return detail::decode_minor_flight(root);
  if (!system_import &&
      (root["format_version"] == kFreedomMoonFlightSaveFormatVersion ||
       root["format_version"] == kFreedomNetworkMoonFlightSaveFormatVersion ||
       root["format_version"] == kFreedomForeignMoonFlightSaveFormatVersion))
    return detail::decode_moon_flight(root);
  const bool importing =
      system_import &&
      root["format_version"] == kFreedomSystemFlightImportFormatVersion;
  if (system_import && !importing)
    return std::unexpected{failure(
        SaveSchemaErrorCode::unsupported_format_version, "$.format_version",
        "unsupported system-flight import version")};
  const bool network =
      !system_import &&
      root["format_version"] == kFreedomNetworkFlightSaveFormatVersion;
  // Network50 explicitly carries all presentation policies, including optics0.
  const bool optical =
      network || (!system_import && root["format_version"] ==
                                        kFreedomOpticalFlightSaveFormatVersion);
  const bool release =
      !system_import &&
      (root["format_version"] == kFreedomReleaseFlightSaveFormatVersion ||
       (optical && root.contains("flight_model") &&
        root["flight_model"].is_object() &&
        root["flight_model"].value("frame_policy", Json{}) ==
            kFreedomReleaseSystemFramePolicy));
  const bool selected =
      optical || release || importing ||
      root["format_version"] == kFreedomTerrainFlightSaveFormatVersion;
  const bool active =
      network || release || importing ||
      root["format_version"] == kFreedomActiveFlightSaveFormatVersion ||
      (selected && root.contains("world_owner"));
  if (root["format_version"] != kFreedomFlightSaveFormatVersion && !active &&
      !selected)
    return std::unexpected{
        failure(SaveSchemaErrorCode::unsupported_format_version,
                "$.format_version", "unsupported Freedom flight save version")};
  auto shape_root = root;
  // A bare full-flight import cannot carry an unqualified recovery lineage.
  if (active && !importing) shape_root.erase("craft_lineage");
  if (network) {
    if (!root.contains("network"))
      return std::unexpected{
          failure(SaveSchemaErrorCode::missing_field, "$.network",
                  "network flight requires explicit selection")};
    shape_root.erase("network");
    shape_root.erase("derelicts");
  }
  auto shape =
      active ? fields(shape_root,
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
        w["version"] != (network ? kFreedomNetworkActiveWorldVersion
                                 : kFreedomActiveWorldVersion))
      return std::unexpected{
          failure(SaveSchemaErrorCode::incompatible_generator_version,
                  "$.world_owner.version", "unsupported active-world owner")};
    auto system = decimal(w["system_id"], "$.world_owner.system_id"),
         planet = decimal(w["planet_id"], "$.world_owner.planet_id");
    if (!system) return std::unexpected{system.error()};
    if (!planet) return std::unexpected{planet.error()};
    selection =
        FreedomActiveWorldSelection{network ? kFreedomNetworkActiveWorldVersion
                                            : kFreedomActiveWorldVersion,
                                    {*system},
                                    {*planet}};
  }
  if (!root["state"].is_object() || !root["state"].contains("location") ||
      root["state"]["location"] !=
          (importing || (release && root["flight"].is_object() &&
                         root["flight"].contains("frame") &&
                         root["flight"]["frame"].is_object() &&
                         root["flight"]["frame"].contains("kind") &&
                         root["flight"]["frame"]["kind"] == "system_inertial")
               ? "system_flight"
               : "planetary_flight"))
    return std::unexpected{
        failure(SaveSchemaErrorCode::invalid_state, "$.state.location",
                importing ? "format35 requires explicit system_flight"
                          : "format18 requires explicit planetary_flight")};
  Json origin = root;
  origin.erase("flight");
  origin.erase("flight_model");
  origin.erase("world_owner");
  origin["format_version"] = network && origin.contains("derelicts")
                                 ? kFreedomDerelictSaveFormatVersion
                             : network ? kFreedomNetworkSaveFormatVersion
                             : origin.contains("craft_lineage")
                                 ? kFreedomCraftLineageSaveFormatVersion
                                 : kFreedomSaveFormatVersion;
  origin["state"]["location"] = "docked_at_origin";
  // Shared identity/history validation only; original format18 location was
  // already checked, and a flight projection is mandatory below.
  auto decoded = decode_freedom_save_document_json(origin.dump());
  if (!decoded) return std::unexpected{decoded.error()};
  const auto& m = root["flight_model"];
  auto model_shape = m;
  if (selected && model_shape.is_object()) model_shape.erase("terrain_policy");
  if ((release || optical) && model_shape.is_object())
    model_shape.erase("frame_policy");
  if (optical && model_shape.is_object()) {
    model_shape.erase("optics");
    model_shape.erase("companions");
    if (m.contains("companions") &&
        (!m["companions"].is_number_unsigned() || m["companions"] != 1U))
      return std::unexpected{
          failure(SaveSchemaErrorCode::incompatible_generator_version,
                  "$.flight_model.companions",
                  "unsupported distant companion selection")};
    if (!m.contains("optics") || !m["optics"].is_number_unsigned() ||
        (m["optics"] != kPlanetOpticsVersion &&
         !(network && m["optics"] == 0U)) ||
        !m.contains("frame_policy") ||
        !m["frame_policy"].is_number_unsigned() ||
        (m["frame_policy"] != kFreedomRelativeFramePolicy &&
         m["frame_policy"] != kFreedomReleaseSystemFramePolicy))
      return std::unexpected{failure(
          SaveSchemaErrorCode::incompatible_generator_version,
          "$.flight_model.optics",
          network
              ? "network save requires supported optics and an explicit frame "
                "policy"
              : "optical save requires optics1 and an explicit frame policy")};
  }
  if (release &&
      (!m.contains("frame_policy") || !m["frame_policy"].is_number_unsigned() ||
       m["frame_policy"] != kFreedomReleaseSystemFramePolicy))
    return std::unexpected{
        failure(SaveSchemaErrorCode::incompatible_generator_version,
                "$.flight_model.frame_policy",
                "release save requires explicit policy2")};
  if (auto valid =
          fields(model_shape,
                 {"physical_catalog", "physical_ephemeris", "rotation_owner",
                  "rotation_generator", "central_body", "atmosphere",
                  "orbit_hold", "assistance"},
                 "$.flight_model");
      !valid)
    return std::unexpected{valid.error()};
  if (selected &&
      (!m.contains("terrain_policy") ||
       !m["terrain_policy"].is_number_unsigned() ||
       (m["terrain_policy"] != kFreedomLocalDetailTerrainPolicy &&
        !(optical && m["terrain_policy"] == kFreedomSeaDatumTerrainPolicy) &&
        ((!importing && !release && !optical) ||
         m["terrain_policy"] != kFreedomLegacyTerrainPolicy))))
    return std::unexpected{failure(
        SaveSchemaErrorCode::incompatible_generator_version,
        "$.flight_model.terrain_policy",
        importing ? "system import requires explicit terrain policy1 or2"
                  : "selected terrain save requires explicit policy2")};
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
  model.terrain_policy = selected ? m["terrain_policy"].get<std::uint32_t>()
                                  : kFreedomLegacyTerrainPolicy;
  model.frame_policy = optical   ? m["frame_policy"].get<std::uint32_t>()
                       : release ? kFreedomReleaseSystemFramePolicy
                                 : kFreedomRelativeFramePolicy;
  model.optics = optical ? m["optics"].get<std::uint32_t>() : 0;
  model.companions = optical && m.contains("companions") ? 1U : 0U;
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
  if (importing) {
    // All owner, model, source and clock checks precede the coordinate change.
    if (system->ephemeris_version != model.physical_ephemeris ||
        model.rotation_owner != kPhysicalPlanetRotationOwnerVersion ||
        model.central.version != kCentralBodyDynamicsVersion ||
        model.atmosphere.version != kAtmosphericFlightVersion ||
        model.hold.version != kOrbitHoldVersion)
      return std::unexpected{failure(
          SaveSchemaErrorCode::incompatible_generator_version, "$.flight_model",
          "unsupported system import force/rotation/hold selection")};
    if (document.origin.state.tick != flight->tick ||
        flight->tick >= std::numeric_limits<SimulationTick>::max() - 1 ||
        root["flight"]["version"] != 2 ||
        flight->frame.kind != RigidFrameKind::system_inertial ||
        flight->frame.system != selection->system ||
        flight->craft !=
            CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion})
      return std::unexpected{
          failure(SaveSchemaErrorCode::invalid_state, "$.flight",
                  "system import requires matching clocks and owned Wayfarer "
                  "rigid-v2 system state")};
    const auto planet = find_local_system_planet(*system, selection->planet);
    if (!planet)
      return std::unexpected{
          failure(SaveSchemaErrorCode::invalid_state, "$.world_owner.planet_id",
                  "system import force body is not in the selected world")};
    const auto rotation_recipe = generate_planet_rotation_recipe(
        *system, selection->planet, model.rotation_generator);
    const auto profile = resolve_atmospheric_flight_profile(
        (*planet)->descriptor, model.atmosphere);
    if (!rotation_recipe || !profile)
      return std::unexpected{failure(
          SaveSchemaErrorCode::incompatible_generator_version, "$.flight_model",
          "system import rotation/atmosphere owner is invalid")};
    if (model.hold.target) {
      const auto& target = *model.hold.target;
      const auto n = target.plane_normal;
      if (target.planet != selection->planet ||
          target.radius_metres <= (*planet)->descriptor.radius.value * 1000.0 +
                                      profile->space_boundary_altitude_metres ||
          target.radius_metres > kRigidBodyMaximumPositionMetres ||
          std::abs(n.x) > 1 || std::abs(n.y) > 1 || std::abs(n.z) > 1 ||
          (n.x == 0 && std::signbit(n.x)) || (n.y == 0 && std::signbit(n.y)) ||
          (n.z == 0 && std::signbit(n.z)) ||
          std::abs((n.x * n.x + n.y * n.y) + n.z * n.z - 1) >
              kRigidBodyOrientationSquaredNormTolerance)
        return std::unexpected{
            failure(SaveSchemaErrorCode::invalid_state,
                    "$.flight_model.orbit_hold.target",
                    "system import hold target is invalid for its force body")};
    }
    const auto converted =
        reframe_rigid_body(context, *flight,
                           {{RigidFrameKind::planet_relative_inertial,
                             selection->system,
                             selection->planet,
                             {}},
                            flight->tick});
    if (!converted)
      return std::unexpected{failure(
          SaveSchemaErrorCode::invalid_state, "$.flight",
          "system import same-tick conversion is unsafe or unsupported")};
    document.flight = *converted;
  }
  if (auto valid = hydrate_freedom_flight_document(document); !valid)
    return std::unexpected{valid.error()};
  return document;
}
} // namespace

auto decode_freedom_flight_document_json(std::string_view text)
    -> std::expected<FreedomFlightSaveDocument, SaveSchemaError> {
  return decode_flight(text, false);
}
auto decode_freedom_system_flight_import_json(std::string_view text)
    -> std::expected<FreedomFlightSaveDocument, SaveSchemaError> {
  return decode_flight(text, true);
}
} // namespace apsis_drift
