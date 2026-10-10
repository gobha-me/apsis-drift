#include "apsis_drift/freedom_surface_walk_save.hpp"

#include "apsis_drift/native_flight_session.hpp"

#include <array>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;
auto failure(std::string text) -> SaveSchemaError {
  return {SaveSchemaErrorCode::invalid_state, "$.surface_walk",
          std::move(text)};
}
auto fields(const Json& j, std::initializer_list<std::string_view> keys)
    -> bool {
  if (!j.is_object() || j.size() != keys.size()) return false;
  for (auto key : keys)
    if (!j.contains(std::string{key})) return false;
  return true;
}
auto nesting(std::string_view text) -> bool {
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
auto vector_json(RigidVector3 v) -> Json {
  return Json::array({v.x, v.y, v.z});
}
auto vector_value(const Json& j)
    -> std::expected<RigidVector3, SaveSchemaError> {
  if (!j.is_array() || j.size() != 3)
    return std::unexpected{
        failure("Surface vector must have three components")};
  for (const auto& value : j)
    if (!value.is_number())
      return std::unexpected{
          failure("Surface vector requires numeric components")};
  return RigidVector3{j[0].get<double>(), j[1].get<double>(),
                      j[2].get<double>()};
}
} // namespace

auto validate_freedom_surface_walk_document(
    const FreedomSurfaceWalkSaveDocument& d)
    -> std::expected<void, SaveSchemaError> {
  if (d.suit_version != 1)
    return std::unexpected{failure("Unsupported prototype suit policy")};
  if (auto valid = validate_freedom_recovery_document(d.voyage); !valid)
    return std::unexpected{valid.error()};
  if (!d.actor) return {};
  const auto flight = recovery_flight(d.voyage.voyage);
  const auto hydrated = hydrate_freedom_flight_document(flight);
  if (!hydrated) return std::unexpected{hydrated.error()};
  const auto planet =
      find_local_system_planet(hydrated->system, *flight.flight.frame.planet);
  if (!planet)
    return std::unexpected{failure("Surface body owner unavailable")};
  // Open only the inner historical voyage; there is no recursive format30.
  const auto session = NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom, d.voyage, (*planet)->descriptor, {}});
  if (!session || !session->starting_assembly() || session->walker() ||
      !session->boarding() ||
      session->boarding()->phase != FreedomBoardingPhase::seated ||
      !session->surface() || !session->surface()->landed)
    return std::unexpected{
        failure("Ground actor requires its supported occupied Wayfarer")};
  auto terrain = PlanetSurfaceWalkTerrain::create(
      session->system(), session->rotation(), *session->surface()->landed,
      flight.flight.tick);
  if (!terrain) return std::unexpected{failure(terrain.error())};
  if (auto valid = terrain->validate(*d.actor); !valid)
    return std::unexpected{failure(valid.error())};
  return {};
}

auto encode_freedom_surface_walk_document_json(
    const FreedomSurfaceWalkSaveDocument& d)
    -> std::expected<std::string, SaveSchemaError> {
  if (auto valid = validate_freedom_surface_walk_document(d); !valid)
    return std::unexpected{valid.error()};
  const auto base = encode_freedom_recovery_document_json(d.voyage);
  if (!base) return std::unexpected{base.error()};
  Json actor = nullptr;
  if (d.actor) {
    const auto& a = *d.actor;
    actor = {{"version", a.version},
             {"actor_id", a.actor_id},
             {"system", a.frame.system.value},
             {"planet", a.frame.planet->value},
             {"foot_position_metres", vector_json(a.foot_position_metres)},
             {"velocity_metres_per_second",
              vector_json(a.velocity_metres_per_second)},
             {"heading_radians", a.heading_radians}};
  }
  const Json root{{"format_version", kFreedomSurfaceWalkSaveFormatVersion},
                  {"voyage", Json::parse(*base)},
                  {"suit_version", d.suit_version},
                  {"actor", std::move(actor)}};
  auto text = root.dump(2) + '\n';
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{failure("Surface walk document exceeds save bound")};
  return text;
}

auto decode_freedom_surface_walk_document_json(std::string_view text)
    -> std::expected<FreedomSurfaceWalkSaveDocument, SaveSchemaError> {
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "Surface save exceeds bound"}};
  if (!nesting(text))
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::malformed_json, "$",
                        "Unbalanced or excessive JSON nesting"}};
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
  try {
    const auto root = Json::parse(text.begin(), text.end(), callback);
    if (duplicate)
      return std::unexpected{SaveSchemaError{SaveSchemaErrorCode::duplicate_key,
                                             "$", "Repeated JSON key"}};
    if (!root.is_object() || !root.contains("format_version") ||
        !root["format_version"].is_number_unsigned())
      return std::unexpected{failure("Unsigned format required")};
    if (root["format_version"] != kFreedomSurfaceWalkSaveFormatVersion)
      return std::unexpected{SaveSchemaError{
          SaveSchemaErrorCode::unsupported_format_version, "$.format_version",
          "Unsupported surface walk format"}};
    if (!fields(root, {"format_version", "voyage", "suit_version", "actor"}) ||
        !root["suit_version"].is_number_unsigned() || root["suit_version"] != 1)
      return std::unexpected{failure("Invalid closed surface walk policy")};
    auto base = decode_freedom_recovery_document_json(root["voyage"].dump());
    if (!base) return std::unexpected{base.error()};
    FreedomSurfaceWalkSaveDocument d{std::move(*base), 1, {}};
    const auto& a = root["actor"];
    if (!a.is_null()) {
      if (!fields(a, {"version", "actor_id", "system", "planet",
                      "foot_position_metres", "velocity_metres_per_second",
                      "heading_radians"}))
        return std::unexpected{failure("Invalid closed surface actor")};
      for (auto key : {"version", "actor_id", "system", "planet"})
        if (!a[key].is_number_unsigned())
          return std::unexpected{failure("Unsigned actor ownership required")};
      if (a["version"] != kPlanetSurfaceWalkerVersion || a["actor_id"] != 1 ||
          !a["heading_radians"].is_number())
        return std::unexpected{failure("Unsupported actor or heading")};
      const auto foot = vector_value(a["foot_position_metres"]),
                 velocity = vector_value(a["velocity_metres_per_second"]);
      if (!foot) return std::unexpected{foot.error()};
      if (!velocity) return std::unexpected{velocity.error()};
      d.actor =
          PlanetSurfaceWalkerState{1,
                                   1,
                                   {RigidFrameKind::planet_fixed,
                                    SystemId{a["system"].get<std::uint64_t>()},
                                    PlanetId{a["planet"].get<std::uint64_t>()},
                                    {}},
                                   *foot,
                                   *velocity,
                                   a["heading_radians"].get<double>()};
    }
    if (auto valid = validate_freedom_surface_walk_document(d); !valid)
      return std::unexpected{valid.error()};
    return d;
  } catch (const Json::exception& e) {
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::malformed_json, "$", e.what()}};
  }
}
} // namespace apsis_drift
