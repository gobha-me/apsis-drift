#include "freedom_minor_flight_save_internal.hpp"

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
auto minor_system(const FreedomSaveDocument& origin,
                  const FreedomMinorWorldSelection& selection)
    -> std::expected<PhysicalLocalSystem, SaveSchemaError> {
  if (selection.version == kFreedomMinorWorldVersion) {
    const auto system =
        generate_physical_origin_system(origin.recipe.universe_seed, 2);
    if (system && selection.target.system == system->catalog.id) return *system;
  } else if (selection.version == kFreedomForeignMinorWorldVersion &&
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
      failure("$.world_owner.target", "minor world/system selection refused")};
}
} // namespace

auto hydrate_freedom_minor_flight_document(
    const FreedomFlightSaveDocument& document)
    -> std::expected<FreedomMinorFlightHydration, SaveSchemaError> {
  if (!document.minor || document.world || document.moon)
    return std::unexpected{failure(
        "$.world_owner", "minor requires an exclusive typed world selection")};
  if (const auto valid = validate_freedom_save_document(document.origin);
      !valid)
    return std::unexpected{valid.error()};
  if (document.origin.state.tick != document.flight.tick ||
      document.flight.tick >= std::numeric_limits<SimulationTick>::max() - 1)
    return std::unexpected{failure(
        "$.flight.tick", "history/flight clocks differ or cannot advance")};
  const auto& selection = *document.minor;
  if ((selection.version != kFreedomMinorWorldVersion &&
       selection.version != kFreedomForeignMinorWorldVersion) ||
      selection.physical_recipe != kPhysicalMinorRecipeVersion ||
      selection.rotation_recipe != kPhysicalMinorRotationVersion ||
      selection.dynamics_recipe != kMinorBodyDynamicsVersion)
    return std::unexpected{
        failure("$.world_owner",
                "unsupported minor world/physical/rotation/dynamics recipe")};
  const auto system = minor_system(document.origin, selection);
  if (!system) return std::unexpected{system.error()};
  const auto catalog = generate_secondary_body_catalog(*system);
  if (!catalog)
    return std::unexpected{
        failure("$.world_owner", "secondary source refused")};
  const auto minor =
      generate_physical_minor_descriptor(*catalog, selection.target.body);
  if (!minor)
    return std::unexpected{
        failure("$.world_owner.target",
                "target is not the actual authenticated minor")};
  FreedomFlightModel expected;
  expected.physical_catalog = 2;
  expected.physical_ephemeris = 2;
  expected.assistance = document.model.assistance;
  if (document.model != expected)
    return std::unexpected{failure(
        "$.flight_model",
        "minor cannot carry a planet atmosphere, hold or terrain selection")};
  if (document.flight.craft !=
          CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion} ||
      document.flight.frame.kind != RigidFrameKind::body_relative_inertial ||
      document.flight.frame.body != selection.target.body)
    return std::unexpected{
        failure("$.flight.frame",
                "minor flight requires the owned minor-relative Wayfarer")};
  const RigidBodyWorldContext context{*minor};
  const auto gravity = evaluate_minor_body_gravity(context, document.flight,
                                                   {selection.dynamics_recipe});
  const auto rotation =
      resolve_physical_minor_rotation(*minor, document.flight.tick);
  const auto boundary =
      minor_exclusion_radius_metres(*minor, document.flight.craft);
  if (!boundary || !gravity || gravity->distance_metres <= *boundary)
    return std::unexpected{
        failure("$.flight",
                "Minor inspection must remain outside its stand-off sphere")};
  if (!gravity || !rotation)
    return std::unexpected{failure(
        "$.flight", "minor state, force or rotation validation refused")};
  return FreedomMinorFlightHydration{*minor, *rotation, *gravity};
}

namespace detail {
auto encode_minor_flight(const FreedomFlightSaveDocument& document)
    -> std::expected<std::string, SaveSchemaError> {
  const auto hydrated = hydrate_freedom_minor_flight_document(document);
  if (!hydrated) return std::unexpected{hydrated.error()};
  const auto origin = encode_freedom_save_document_json(document.origin);
  if (!origin) return std::unexpected{origin.error()};
  const auto state = encode_rigid_body_state_json(
      RigidBodyWorldContext{hydrated->minor}, document.flight);
  const auto target = encode_body_target(document.minor->target);
  if (!state || !target)
    return std::unexpected{
        failure("$.flight", "minor state or target cannot encode")};
  Json root = Json::parse(*origin);
  root["format_version"] =
      document.minor->version == kFreedomForeignMinorWorldVersion
          ? kFreedomForeignMinorFlightSaveFormatVersion
      : document.origin.network ? kFreedomNetworkMinorFlightSaveFormatVersion
                                : kFreedomMinorFlightSaveFormatVersion;
  root["state"]["location"] = "minor_flight";
  root["world_owner"] = {{"version", document.minor->version},
                         {"target", Json::parse(*target)},
                         {"physical_recipe", document.minor->physical_recipe},
                         {"rotation_recipe", document.minor->rotation_recipe},
                         {"dynamics_recipe", document.minor->dynamics_recipe}};
  root["flight_model"] = {
      {"physical_catalog", document.model.physical_catalog},
      {"physical_ephemeris", document.model.physical_ephemeris},
      {"assistance", document.model.assistance}};
  root["flight"] = Json::parse(*state);
  auto encoded = root.dump(2) + '\n';
  if (encoded.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        failure("$", "minor flight exceeds save document bound")};
  return encoded;
}

auto decode_minor_flight(const Json& root)
    -> std::expected<FreedomFlightSaveDocument, SaveSchemaError> {
  const bool foreign =
      root.is_object() && root.contains("format_version") &&
      selected_version(root["format_version"],
                       kFreedomForeignMinorFlightSaveFormatVersion);
  const bool network =
      foreign ||
      (root.is_object() && root.contains("format_version") &&
       selected_version(root["format_version"],
                        kFreedomNetworkMinorFlightSaveFormatVersion));
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
                                    kFreedomMinorFlightSaveFormatVersion)))
    return std::unexpected{
        failure("$", "unexpected or missing minor flight field")};
  const auto& world = root["world_owner"];
  const auto& model = root["flight_model"];
  if (!fields(world, {"version", "target", "physical_recipe", "rotation_recipe",
                      "dynamics_recipe"}) ||
      !selected_version(world["version"], foreign
                                              ? kFreedomForeignMinorWorldVersion
                                              : kFreedomMinorWorldVersion) ||
      !selected_version(world["physical_recipe"],
                        kPhysicalMinorRecipeVersion) ||
      !selected_version(world["rotation_recipe"],
                        kPhysicalMinorRotationVersion) ||
      !selected_version(world["dynamics_recipe"], kMinorBodyDynamicsVersion))
    return std::unexpected{failure(
        "$.world_owner", "unsupported or malformed minor world selection")};
  if (!fields(model,
              {"physical_catalog", "physical_ephemeris", "assistance"}) ||
      !selected_version(model["physical_catalog"], 2) ||
      !selected_version(model["physical_ephemeris"], 2) ||
      !model["assistance"].is_boolean())
    return std::unexpected{failure(
        "$.flight_model", "unsupported or malformed minor flight model")};
  if (!root["state"].is_object() || !root["state"].contains("location") ||
      root["state"]["location"] != "minor_flight")
    return std::unexpected{
        failure("$.state.location", "minor flight requires explicit location")};
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
  const FreedomMinorWorldSelection selection{
      foreign ? kFreedomForeignMinorWorldVersion : kFreedomMinorWorldVersion,
      *target};
  const auto system = minor_system(*history, selection);
  if (!system) return std::unexpected{system.error()};
  const auto source = generate_secondary_body_catalog(*system);
  if (!source)
    return std::unexpected{failure("$.world_owner", "minor source refused")};
  const auto minor = generate_physical_minor_descriptor(*source, target->body);
  if (!minor)
    return std::unexpected{
        failure("$.world_owner.target", "foreign or unsupported body")};
  const auto flight = decode_rigid_body_state_json(
      RigidBodyWorldContext{*minor}, root["flight"].dump());
  if (!flight)
    return std::unexpected{failure("$.flight", "minor rigid state refused")};
  FreedomFlightSaveDocument document{*history, *flight, {}, {}};
  document.model.physical_catalog = 2;
  document.model.physical_ephemeris = 2;
  document.model.assistance = model["assistance"].get<bool>();
  document.minor = selection;
  if (const auto valid = hydrate_freedom_minor_flight_document(document);
      !valid)
    return std::unexpected{valid.error()};
  return document;
}
} // namespace detail
} // namespace apsis_drift
