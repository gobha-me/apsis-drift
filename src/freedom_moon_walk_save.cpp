#include "freedom_moon_walk_save_internal.hpp"

#include "apsis_drift/moon_surface_walker.hpp"
#include "apsis_drift/native_flight_session.hpp"

namespace apsis_drift::detail {
namespace {
using Json = nlohmann::ordered_json;
auto failure(std::string text) -> SaveSchemaError {
  return {SaveSchemaErrorCode::invalid_state, "$.surface_walk",
          std::move(text)};
}
auto fields(const Json& value, std::initializer_list<std::string_view> names)
    -> bool {
  if (!value.is_object() || value.size() != names.size()) return false;
  for (const auto name : names)
    if (!value.contains(std::string{name})) return false;
  return true;
}
auto vector_value(const Json& value)
    -> std::expected<RigidVector3, SaveSchemaError> {
  if (!value.is_array() || value.size() != 3)
    return std::unexpected{
        failure("Moon actor vector requires three components")};
  for (const auto& component : value)
    if (!component.is_number())
      return std::unexpected{failure("Moon actor vector must be numeric")};
  return RigidVector3{value[0].get<double>(), value[1].get<double>(),
                      value[2].get<double>()};
}
auto vector_json(RigidVector3 value) -> Json {
  return Json::array({value.x, value.y, value.z});
}
} // namespace
auto validate_moon_walk(const FreedomSurfaceWalkSaveDocument& document)
    -> std::expected<void, SaveSchemaError> {
  if (document.suit_version != 1)
    return std::unexpected{failure("Unsupported prototype suit policy")};
  if (const auto valid = validate_freedom_recovery_document(document.voyage);
      !valid)
    return std::unexpected{valid.error()};
  const auto flight = recovery_flight(document.voyage.voyage);
  if (!flight.moon)
    return std::unexpected{
        failure("Moon walking requires explicit moon flight")};
  if (!document.actor) return {};
  const auto selected =
      native_select_save_document(NativeSaveDocument{document.voyage});
  if (!selected) return std::unexpected{failure(selected.error())};
  const auto session = NativeFreedomFlightSession::open(*selected);
  if (!session || !session->moon() || !session->starting_assembly() ||
      session->walker() || !session->boarding() ||
      session->boarding()->phase != FreedomBoardingPhase::seated ||
      !session->surface() || !session->surface()->moon_landed)
    return std::unexpected{
        failure("Moon actor requires its supported occupied Wayfarer")};
  auto terrain = MoonSurfaceWalkTerrain::create(
      *session->moon(), *session->surface()->moon_landed, flight.flight.tick);
  if (!terrain) return std::unexpected{failure(terrain.error())};
  if (const auto valid = terrain->validate(*document.actor); !valid)
    return std::unexpected{failure(valid.error())};
  return {};
}
auto encode_moon_walk(const FreedomSurfaceWalkSaveDocument& document)
    -> std::expected<std::string, SaveSchemaError> {
  if (const auto valid = validate_moon_walk(document); !valid)
    return std::unexpected{valid.error()};
  const auto inner = encode_freedom_recovery_document_json(document.voyage);
  if (!inner) return std::unexpected{inner.error()};
  Json actor = nullptr;
  if (document.actor) {
    const auto& value = *document.actor;
    const auto target =
        encode_body_target({value.frame.system, *value.frame.body});
    if (!target)
      return std::unexpected{failure("Moon actor target cannot encode")};
    actor = {
        {"version", value.version},
        {"actor_id", value.actor_id},
        {"frame", {{"kind", "body_fixed"}, {"target", Json::parse(*target)}}},
        {"foot_position_metres", vector_json(value.foot_position_metres)},
        {"velocity_metres_per_second",
         vector_json(value.velocity_metres_per_second)},
        {"heading_radians", value.heading_radians}};
  }
  const Json root{{"format_version", kFreedomMoonSurfaceWalkSaveFormatVersion},
                  {"voyage", Json::parse(*inner)},
                  {"suit_version", document.suit_version},
                  {"actor", std::move(actor)}};
  auto bytes = root.dump(2) + '\n';
  if (bytes.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{failure("Moon walking save exceeds document bound")};
  return bytes;
}
auto decode_moon_walk(const Json& root)
    -> std::expected<FreedomSurfaceWalkSaveDocument, SaveSchemaError> {
  if (!fields(root, {"format_version", "voyage", "suit_version", "actor"}) ||
      !root["suit_version"].is_number_unsigned() || root["suit_version"] != 1U)
    return std::unexpected{failure("Invalid closed moon walking shape")};
  auto inner = decode_freedom_recovery_document_json(root["voyage"].dump());
  if (!inner) return std::unexpected{inner.error()};
  FreedomSurfaceWalkSaveDocument document{std::move(*inner), 1, {}};
  const auto& actor = root["actor"];
  if (!actor.is_null()) {
    if (!fields(actor, {"version", "actor_id", "frame", "foot_position_metres",
                        "velocity_metres_per_second", "heading_radians"}) ||
        !actor["version"].is_number_unsigned() ||
        actor["version"] != kPlanetSurfaceWalkerVersion ||
        !actor["actor_id"].is_number_unsigned() || actor["actor_id"] != 1U ||
        !actor["heading_radians"].is_number() ||
        !fields(actor["frame"], {"kind", "target"}) ||
        actor["frame"]["kind"] != "body_fixed")
      return std::unexpected{failure("Invalid closed moon actor shape")};
    const auto target = decode_body_target(actor["frame"]["target"].dump());
    const auto foot = vector_value(actor["foot_position_metres"]);
    const auto velocity = vector_value(actor["velocity_metres_per_second"]);
    if (!target)
      return std::unexpected{failure("Invalid typed moon actor target")};
    if (!foot) return std::unexpected{foot.error()};
    if (!velocity) return std::unexpected{velocity.error()};
    document.actor = PlanetSurfaceWalkerState{
        1,
        1,
        {RigidFrameKind::body_fixed, target->system, {}, {}, target->body},
        *foot,
        *velocity,
        actor["heading_radians"].get<double>()};
  }
  if (const auto valid = validate_moon_walk(document); !valid)
    return std::unexpected{valid.error()};
  return document;
}
} // namespace apsis_drift::detail
