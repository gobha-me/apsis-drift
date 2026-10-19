#include "freedom_moon_surface_save_internal.hpp"

namespace apsis_drift::detail {
namespace {
using Json = nlohmann::ordered_json;
auto failure(std::string text) -> SaveSchemaError {
  return {SaveSchemaErrorCode::invalid_state, "$.surface", std::move(text)};
}
auto fields(const Json& value, std::initializer_list<std::string_view> names)
    -> bool {
  if (!value.is_object() || value.size() != names.size()) return false;
  for (const auto name : names)
    if (!value.contains(std::string{name})) return false;
  return true;
}
} // namespace
auto validate_moon_surface(const FreedomSurfaceSaveDocument& document)
    -> std::expected<void, SaveSchemaError> {
  const auto* flight = std::get_if<FreedomFlightSaveDocument>(&document.base);
  if (!flight || !flight->moon || document.surface.landed)
    return std::unexpected{
        failure("Moon surface requires its sole typed flight owner")};
  const auto hydrated = hydrate_freedom_moon_flight_document(*flight);
  if (!hydrated) return std::unexpected{hydrated.error()};
  if (!document.surface.moon_landed) return {};
  if (!document.surface.gear_deployed)
    return std::unexpected{failure("Landed moon craft requires deployed gear")};
  const auto& anchor = *document.surface.moon_landed;
  if (anchor.fixed.craft != flight->flight.craft ||
      anchor.terrain_version != flight->moon->terrain_recipe ||
      anchor.rotation_version != flight->moon->rotation_recipe ||
      anchor.fixed.frame.body != flight->moon->target.body ||
      anchor.fixed.frame.system != flight->moon->target.system)
    return std::unexpected{failure("Moon anchor and flight owners differ")};
  if (!validate_moon_landed_craft(hydrated->moon, anchor, flight->flight.tick))
    return std::unexpected{
        failure("Moon landing support or hull cannot certify")};
  const auto pose =
      resolve_moon_landed_craft(hydrated->moon, anchor, flight->flight.tick);
  if (!pose || *pose != flight->flight)
    return std::unexpected{
        failure("Moon flight differs from its exact constrained pose")};
  return {};
}
auto encode_moon_surface(const FreedomSurfaceSaveDocument& document)
    -> std::expected<std::string, SaveSchemaError> {
  if (const auto valid = validate_moon_surface(document); !valid)
    return std::unexpected{valid.error()};
  const auto& flight = std::get<FreedomFlightSaveDocument>(document.base);
  const auto base = encode_freedom_flight_document_json(flight);
  if (!base) return std::unexpected{base.error()};
  Json anchor = nullptr;
  if (document.surface.moon_landed) {
    const auto hydrated = hydrate_freedom_moon_flight_document(flight);
    if (!hydrated) return std::unexpected{hydrated.error()};
    const auto& value = *document.surface.moon_landed;
    const auto fixed = encode_rigid_body_state_json(
        RigidBodyWorldContext{hydrated->moon}, value.fixed);
    if (!fixed) return std::unexpected{failure("Moon anchor cannot encode")};
    anchor = {{"version", value.version},
              {"terrain_version", value.terrain_version},
              {"rotation_version", value.rotation_version},
              {"fixed", Json::parse(*fixed)}};
  }
  const Json root{{"format_version", kFreedomMoonSurfaceSaveFormatVersion},
                  {"base", Json::parse(*base)},
                  {"surface",
                   {{"gear_deployed", document.surface.gear_deployed},
                    {"moon_landed", std::move(anchor)}}}};
  auto encoded = root.dump(2) + '\n';
  if (encoded.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{failure("Moon surface save exceeds document bound")};
  return encoded;
}
auto decode_moon_surface(const Json& root)
    -> std::expected<FreedomSurfaceSaveDocument, SaveSchemaError> {
  if (!fields(root, {"format_version", "base", "surface"}) ||
      !fields(root["surface"], {"gear_deployed", "moon_landed"}) ||
      !root["surface"]["gear_deployed"].is_boolean())
    return std::unexpected{failure("Invalid closed moon surface shape")};
  auto flight = decode_freedom_flight_document_json(root["base"].dump());
  if (!flight) return std::unexpected{flight.error()};
  if (!flight->moon)
    return std::unexpected{
        failure("Moon surface requires explicit moon flight")};
  FreedomSurfaceState surface{root["surface"]["gear_deployed"].get<bool>(), {}};
  const auto& value = root["surface"]["moon_landed"];
  if (!value.is_null()) {
    if (!fields(value,
                {"version", "terrain_version", "rotation_version", "fixed"}) ||
        !value["version"].is_number_unsigned() ||
        value["version"] != kMoonLandedCraftAnchorVersion ||
        !value["terrain_version"].is_number_unsigned() ||
        value["terrain_version"] != kMoonTerrainRecipeVersion ||
        !value["rotation_version"].is_number_unsigned() ||
        value["rotation_version"] != kPhysicalMoonRotationVersion)
      return std::unexpected{failure("Invalid moon anchor shape or version")};
    const auto hydrated = hydrate_freedom_moon_flight_document(*flight);
    if (!hydrated) return std::unexpected{hydrated.error()};
    const auto fixed = decode_rigid_body_state_json(
        RigidBodyWorldContext{hydrated->moon}, value["fixed"].dump());
    if (!fixed)
      return std::unexpected{failure("Invalid moon anchor fixed state")};
    surface.moon_landed = MoonLandedCraftAnchor{1, 1, 1, *fixed};
  }
  FreedomSurfaceSaveDocument document{std::move(*flight), surface};
  if (const auto valid = validate_moon_surface(document); !valid)
    return std::unexpected{valid.error()};
  return document;
}
} // namespace apsis_drift::detail
