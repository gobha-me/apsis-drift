#include "freedom_moon_flight_save_internal.hpp"

#include <limits>

#include "apsis_drift/freedom_system_network.hpp"
#include "apsis_drift/universe_navigation.hpp"

namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;
auto failure(std::string path, std::string detail) -> SaveSchemaError {
  return {SaveSchemaErrorCode::invalid_state, std::move(path),
          std::move(detail)};
}
auto fields(const Json& value, std::initializer_list<std::string_view> names)
    -> bool {
  if (!value.is_object() || value.size() != names.size()) return false;
  for (const auto name : names)
    if (!value.contains(std::string{name})) return false;
  return true;
}
auto selected_version(const Json& value, std::uint32_t version) -> bool {
  return value.is_number_unsigned() && value == version;
}
// World2 authenticates a selected catalog owner; world1 remains Origin-only.
auto moon_system(const FreedomSaveDocument& origin,
                 const FreedomMoonWorldSelection& selection)
    -> std::expected<PhysicalLocalSystem, SaveSchemaError> {
  if (selection.version == kFreedomMoonWorldVersion) {
    const auto system =
        generate_physical_origin_system(origin.recipe.universe_seed, 2);
    if (system && selection.target.system == system->catalog.id) return *system;
  } else if (selection.version == kFreedomForeignMoonWorldVersion &&
             origin.network) {
    const auto origin_system =
        generate_physical_origin_system(origin.recipe.universe_seed, 2);
    const auto network = generate_freedom_system_network(
        origin.recipe.universe_seed, origin.network->version);
    if (origin_system && network &&
        selection.target.system != origin_system->catalog.id) {
      const auto system =
          resolve_freedom_network_system(*network, selection.target.system);
      if (system) return *system;
    }
  }
  return std::unexpected{
      failure("$.world_owner.target", "moon world/system selection refused")};
}
} // namespace

auto hydrate_freedom_moon_flight_document(
    const FreedomFlightSaveDocument& document)
    -> std::expected<FreedomMoonFlightHydration, SaveSchemaError> {
  if (!document.moon || document.world || document.minor)
    return std::unexpected{failure(
        "$.world_owner", "moon requires an exclusive typed world selection")};
  if (const auto valid = validate_freedom_save_document(document.origin);
      !valid)
    return std::unexpected{valid.error()};
  if (document.origin.state.tick != document.flight.tick ||
      document.flight.tick >= std::numeric_limits<SimulationTick>::max() - 1)
    return std::unexpected{failure(
        "$.flight.tick", "history/flight clocks differ or cannot advance")};
  const auto& selection = *document.moon;
  if ((selection.version != kFreedomMoonWorldVersion &&
       selection.version != kFreedomForeignMoonWorldVersion) ||
      selection.physical_recipe != kPhysicalMoonRecipeVersion ||
      selection.rotation_recipe != kPhysicalMoonRotationVersion ||
      selection.dynamics_recipe != kMoonBodyDynamicsVersion ||
      selection.terrain_recipe != kMoonTerrainRecipeVersion)
    return std::unexpected{failure(
        "$.world_owner",
        "unsupported moon world/physical/rotation/dynamics/terrain recipe")};
  const auto system = moon_system(document.origin, selection);
  if (!system) return std::unexpected{system.error()};
  const auto catalog = generate_secondary_body_catalog(*system);
  if (!catalog)
    return std::unexpected{
        failure("$.world_owner", "secondary source refused")};
  const auto moon =
      generate_physical_moon_descriptor(*catalog, selection.target.body);
  if (!moon)
    return std::unexpected{failure(
        "$.world_owner.target", "target is not the actual authenticated moon")};
  FreedomFlightModel expected;
  expected.physical_catalog = 2;
  expected.physical_ephemeris = 2;
  expected.assistance = document.model.assistance;
  if (document.model != expected)
    return std::unexpected{failure(
        "$.flight_model",
        "moon cannot carry a planet atmosphere, hold or terrain selection")};
  if (document.flight.craft !=
          CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion} ||
      document.flight.frame.kind != RigidFrameKind::body_relative_inertial ||
      document.flight.frame.body != selection.target.body)
    return std::unexpected{
        failure("$.flight.frame",
                "moon flight requires the owned moon-relative Wayfarer")};
  const RigidBodyWorldContext context{*moon};
  const auto gravity = evaluate_moon_body_gravity(context, document.flight,
                                                  {selection.dynamics_recipe});
  const auto rotation =
      resolve_physical_moon_rotation(*moon, document.flight.tick);
  if (!gravity || !rotation)
    return std::unexpected{failure(
        "$.flight", "moon state, force or rotation validation refused")};
  return FreedomMoonFlightHydration{*moon, *rotation, *gravity};
}

namespace detail {
auto encode_moon_flight(const FreedomFlightSaveDocument& document)
    -> std::expected<std::string, SaveSchemaError> {
  const auto hydrated = hydrate_freedom_moon_flight_document(document);
  if (!hydrated) return std::unexpected{hydrated.error()};
  const auto origin = encode_freedom_save_document_json(document.origin);
  if (!origin) return std::unexpected{origin.error()};
  const auto state = encode_rigid_body_state_json(
      RigidBodyWorldContext{hydrated->moon}, document.flight);
  const auto target = encode_body_target(document.moon->target);
  if (!state || !target)
    return std::unexpected{
        failure("$.flight", "moon state or target cannot encode")};
  Json root = Json::parse(*origin);
  root["format_version"] =
      document.moon->version == kFreedomForeignMoonWorldVersion
          ? kFreedomForeignMoonFlightSaveFormatVersion
      : document.origin.network ? kFreedomNetworkMoonFlightSaveFormatVersion
                                : kFreedomMoonFlightSaveFormatVersion;
  root["state"]["location"] = "moon_flight";
  root["world_owner"] = {{"version", document.moon->version},
                         {"target", Json::parse(*target)},
                         {"physical_recipe", document.moon->physical_recipe},
                         {"rotation_recipe", document.moon->rotation_recipe},
                         {"dynamics_recipe", document.moon->dynamics_recipe},
                         {"terrain_recipe", document.moon->terrain_recipe}};
  root["flight_model"] = {
      {"physical_catalog", document.model.physical_catalog},
      {"physical_ephemeris", document.model.physical_ephemeris},
      {"assistance", document.model.assistance}};
  root["flight"] = Json::parse(*state);
  auto encoded = root.dump(2) + '\n';
  if (encoded.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        failure("$", "moon flight exceeds save document bound")};
  return encoded;
}

auto decode_moon_flight(const Json& root)
    -> std::expected<FreedomFlightSaveDocument, SaveSchemaError> {
  const bool foreign =
      root.is_object() && root.contains("format_version") &&
      selected_version(root["format_version"],
                       kFreedomForeignMoonFlightSaveFormatVersion);
  const bool network =
      foreign || (root.is_object() && root.contains("format_version") &&
                  selected_version(root["format_version"],
                                   kFreedomNetworkMoonFlightSaveFormatVersion));
  auto shape = root;
  shape.erase("craft_lineage");
  if (network) {
    if (!root.contains("network"))
      return std::unexpected{failure("$.network", "network selection missing")};
    shape.erase("network");
    shape.erase("derelicts");
  }
  if (!fields(shape,
              {"application", "application_version", "format_version", "mode",
               "recipe", "state", "world_owner", "flight_model", "flight"}) ||
      !(network || selected_version(root["format_version"],
                                    kFreedomMoonFlightSaveFormatVersion)))
    return std::unexpected{
        failure("$", "unexpected or missing moon flight field")};
  const auto& world = root["world_owner"];
  const auto& model = root["flight_model"];
  if (!fields(world, {"version", "target", "physical_recipe", "rotation_recipe",
                      "dynamics_recipe", "terrain_recipe"}) ||
      !selected_version(world["version"], foreign
                                              ? kFreedomForeignMoonWorldVersion
                                              : kFreedomMoonWorldVersion) ||
      !selected_version(world["physical_recipe"], kPhysicalMoonRecipeVersion) ||
      !selected_version(world["rotation_recipe"],
                        kPhysicalMoonRotationVersion) ||
      !selected_version(world["dynamics_recipe"], kMoonBodyDynamicsVersion) ||
      !selected_version(world["terrain_recipe"], kMoonTerrainRecipeVersion))
    return std::unexpected{failure(
        "$.world_owner", "unsupported or malformed moon world selection")};
  if (!fields(model,
              {"physical_catalog", "physical_ephemeris", "assistance"}) ||
      !selected_version(model["physical_catalog"], 2) ||
      !selected_version(model["physical_ephemeris"], 2) ||
      !model["assistance"].is_boolean())
    return std::unexpected{failure(
        "$.flight_model", "unsupported or malformed moon flight model")};
  if (!root["state"].is_object() || !root["state"].contains("location") ||
      root["state"]["location"] != "moon_flight")
    return std::unexpected{
        failure("$.state.location", "moon flight requires explicit location")};
  auto origin = root;
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
  const auto history = decode_freedom_save_document_json(origin.dump());
  const auto target = decode_body_target(world["target"].dump());
  if (!history) return std::unexpected{history.error()};
  if (!target)
    return std::unexpected{
        failure("$.world_owner.target", "malformed typed body target")};
  const FreedomMoonWorldSelection selection{
      foreign ? kFreedomForeignMoonWorldVersion : kFreedomMoonWorldVersion,
      *target};
  const auto system = moon_system(*history, selection);
  if (!system) return std::unexpected{system.error()};
  const auto source = generate_secondary_body_catalog(*system);
  if (!source)
    return std::unexpected{failure("$.world_owner", "moon source refused")};
  const auto moon = generate_physical_moon_descriptor(*source, target->body);
  if (!moon)
    return std::unexpected{
        failure("$.world_owner.target", "foreign or unsupported body")};
  const auto flight = decode_rigid_body_state_json(RigidBodyWorldContext{*moon},
                                                   root["flight"].dump());
  if (!flight)
    return std::unexpected{failure("$.flight", "moon rigid state refused")};
  FreedomFlightSaveDocument document{*history, *flight, {}, {}};
  document.model.physical_catalog = 2;
  document.model.physical_ephemeris = 2;
  document.model.assistance = model["assistance"].get<bool>();
  document.moon = selection;
  if (const auto valid = hydrate_freedom_moon_flight_document(document); !valid)
    return std::unexpected{valid.error()};
  return document;
}
} // namespace detail
} // namespace apsis_drift
