#include "apsis_drift/freedom_boarding_save.hpp"

#include <array>
#include <charconv>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;
auto failure(std::string path, std::string detail) -> SaveSchemaError {
  return {SaveSchemaErrorCode::invalid_state, std::move(path),
          std::move(detail)};
}
auto fields(const Json& j, std::initializer_list<std::string_view> names)
    -> bool {
  if (!j.is_object() || j.size() != names.size()) return false;
  for (auto n : names)
    if (!j.contains(std::string{n})) return false;
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
auto decimal(const Json& j) -> std::optional<std::uint64_t> {
  if (!j.is_string()) return {};
  const auto& s = j.get_ref<const std::string&>();
  std::uint64_t v{};
  const auto p = std::from_chars(s.data(), s.data() + s.size(), v);
  if (s.empty() || (s.size() > 1 && s.front() == '0') || p.ec != std::errc{} ||
      p.ptr != s.data() + s.size())
    return {};
  return v;
}
auto vector_json(RigidVector3 v) -> Json {
  return Json::array({v.x, v.y, v.z});
}
auto vector_value(const Json& j) -> std::optional<RigidVector3> {
  if (!j.is_array() || j.size() != 3) return {};
  for (const auto& v : j)
    if (!v.is_number()) return {};
  return RigidVector3{j[0].get<double>(), j[1].get<double>(),
                      j[2].get<double>()};
}
auto actor_json(const OriginWalkerState& a) -> Json {
  return {
      {"geometry_version", a.geometry_version},
      {"actor_id", std::to_string(a.actor_id)},
      {"foot_position_metres", vector_json(a.foot_position_metres)},
      {"velocity_metres_per_second", vector_json(a.velocity_metres_per_second)},
      {"heading_radians", a.heading_radians}};
}
auto actor_value(const Json& j) -> std::optional<OriginWalkerState> {
  if (!fields(j, {"geometry_version", "actor_id", "foot_position_metres",
                  "velocity_metres_per_second", "heading_radians"}) ||
      !j["geometry_version"].is_number_unsigned() ||
      j["geometry_version"] != 1 || !j["heading_radians"].is_number())
    return {};
  const auto id = decimal(j["actor_id"]);
  const auto foot = vector_value(j["foot_position_metres"]),
             velocity = vector_value(j["velocity_metres_per_second"]);
  if (!id || !foot || !velocity) return {};
  return OriginWalkerState{1, *id, *foot, *velocity,
                           j["heading_radians"].get<double>()};
}
auto assembly_json() -> Json {
  const NativeStartingAssemblySelection a;
  return {{"version", a.version},
          {"preset", a.preset},
          {"operating_model_sha256", a.operating_model_sha256},
          {"stowed_model_sha256", a.stowed_model_sha256},
          {"frame_sha256", a.frame_sha256},
          {"contact_sha256", a.contact_sha256},
          {"hardware",
           {{"roof_transfer", a.hardware.roof_transfer},
            {"inner_door", a.hardware.inner_door},
            {"seat_boarding", a.hardware.seat_boarding},
            {"station_closure", a.hardware.station_closure}}}};
}
constexpr std::array<std::string_view, 4> phases{"station", "boarding",
                                                 "seated", "disembarking"};
} // namespace

auto validate_freedom_boarding_document(const FreedomBoardingSaveDocument& d)
    -> std::expected<void, SaveSchemaError> {
  if (auto v = validate_freedom_docking_document(d.voyage); !v)
    return std::unexpected{v.error()};
  if (d.starting_assembly != NativeStartingAssemblySelection{})
    return std::unexpected{
        failure("$.starting_assembly", "unsupported geometry/rest reference")};
  const auto& b = d.boarding;
  const auto& route = b.route;
  if (auto v = validate_origin_walker(b.station_entry); !v)
    return std::unexpected{failure("$.boarding.station_entry", v.error())};
  if (auto v = validate_origin_gameplay_boarding(route); !v)
    return std::unexpected{failure("$.boarding.route", v.error())};
  auto eye = b.station_entry.foot_position_metres;
  eye.y += kOriginWalkerEyeHeightMetres;
  const auto tick = d.voyage.flight.flight.tick;
  const bool complete = route.elapsed_ticks == kGameplayBoardingTicks;
  const auto expected = route.direction == GameplayBoardingDirection::board
                            ? (complete ? FreedomBoardingPhase::seated
                                        : FreedomBoardingPhase::boarding)
                            : (complete ? FreedomBoardingPhase::station
                                        : FreedomBoardingPhase::disembarking);
  if (b.phase != expected || route.station_entry_eye != eye ||
      route.station_entry_heading_radians != b.station_entry.heading_radians ||
      b.started_tick > tick || tick - b.started_tick < route.elapsed_ticks ||
      (!complete && tick - b.started_tick != route.elapsed_ticks))
    return std::unexpected{
        failure("$.boarding", "route phase, entry and shared clock disagree")};
  if (b.phase != FreedomBoardingPhase::seated &&
      (!d.voyage.docking.attached || d.voyage.docking.target.ordinal != 1))
    return std::unexpected{
        failure("$.boarding", "station actor/transition requires attached D1")};
  if ((b.phase == FreedomBoardingPhase::station) != d.station_actor.has_value())
    return std::unexpected{
        failure("$.station_actor", "only station phase owns a floor actor")};
  if (d.station_actor) {
    if (auto v = validate_origin_walker(*d.station_actor); !v)
      return std::unexpected{failure("$.station_actor", v.error())};
    if (d.station_actor->actor_id != b.station_entry.actor_id)
      return std::unexpected{
          failure("$.station_actor", "actor identity changed")};
  }
  auto hydrated = hydrate_freedom_flight_document(d.voyage.flight);
  if (!hydrated) return std::unexpected{hydrated.error()};
  const auto station =
      generate_origin_station(d.voyage.flight.origin.recipe.universe_seed);
  const auto geometry = origin_station_geometry(station);
  if (!geometry)
    return std::unexpected{
        failure("$.boarding", "Origin geometry unavailable")};
  const auto pose = resolve_origin_port_pose(hydrated->system, station,
                                             *geometry, {station.id, 1}, tick);
  if (!pose ||
      route.craft_station_position != pose->station_relative.position_metres ||
      route.craft_station_orientation != pose->station_relative.orientation)
    return std::unexpected{failure(
        "$.boarding.route", "route transform differs from canonical D1")};
  return {};
}
auto encode_freedom_boarding_document_json(const FreedomBoardingSaveDocument& d)
    -> std::expected<std::string, SaveSchemaError> {
  if (auto v = validate_freedom_boarding_document(d); !v)
    return std::unexpected{v.error()};
  auto voyage = encode_freedom_docking_document_json(d.voyage);
  if (!voyage) return std::unexpected{voyage.error()};
  const auto& b = d.boarding;
  const auto& s = b.route;
  const auto q = s.craft_station_orientation;
  Json root = {
      {"format_version", kFreedomBoardingSaveFormatVersion},
      {"voyage", Json::parse(*voyage)},
      {"starting_assembly", assembly_json()},
      {"boarding",
       {{"phase", phases[static_cast<std::size_t>(b.phase)]},
        {"station_entry", actor_json(b.station_entry)},
        {"started_tick", std::to_string(b.started_tick)},
        {"route",
         {{"direction", s.direction == GameplayBoardingDirection::board
                            ? "board"
                            : "disembark"},
          {"elapsed_ticks", s.elapsed_ticks},
          {"station_entry_eye", vector_json(s.station_entry_eye)},
          {"station_entry_heading_radians", s.station_entry_heading_radians},
          {"craft_station_position", vector_json(s.craft_station_position)},
          {"craft_station_orientation", Json::array({q.w, q.x, q.y, q.z})}}}}},
      {"station_actor",
       d.station_actor ? actor_json(*d.station_actor) : Json(nullptr)}};
  auto text = root.dump(2) + '\n';
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "boarding save exceeds limit"}};
  return text;
}
auto decode_freedom_boarding_document_json(std::string_view text)
    -> std::expected<FreedomBoardingSaveDocument, SaveSchemaError> {
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "boarding save exceeds limit"}};
  if (!nesting(text))
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::malformed_json, "$",
                        "unbalanced or excessive JSON nesting"}};
  bool duplicate{};
  std::vector<std::unordered_set<std::string>> keys;
  const Json::parser_callback_t callback = [&](int, Json::parse_event_t e,
                                               Json& v) {
    if (e == Json::parse_event_t::object_start) keys.emplace_back();
    if (e == Json::parse_event_t::key &&
        (keys.empty() || !keys.back().insert(v.get<std::string>()).second))
      duplicate = true;
    if (e == Json::parse_event_t::object_end && !keys.empty()) keys.pop_back();
    return true;
  };
  Json root;
  try {
    root = Json::parse(text.begin(), text.end(), callback);
  } catch (const Json::exception& e) {
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::malformed_json, "$", e.what()}};
  }
  if (duplicate)
    return std::unexpected{SaveSchemaError{SaveSchemaErrorCode::duplicate_key,
                                           "$", "repeated JSON key"}};
  if (!root.is_object() || !root.contains("format_version") ||
      !root["format_version"].is_number_unsigned())
    return std::unexpected{
        failure("$.format_version", "unsigned version required")};
  if (root["format_version"] != kFreedomBoardingSaveFormatVersion)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::unsupported_format_version,
                        "$.format_version", "unsupported boarding version"}};
  if (!fields(root, {"format_version", "voyage", "starting_assembly",
                     "boarding", "station_actor"}) ||
      !root["starting_assembly"].is_object() ||
      !root["starting_assembly"].contains("version") ||
      !root["starting_assembly"]["version"].is_number_unsigned() ||
      root["starting_assembly"] != assembly_json())
    return std::unexpected{
        failure("$", "invalid wrapper or assembly reference")};
  const auto& b = root["boarding"];
  if (!fields(b, {"phase", "station_entry", "started_tick", "route"}) ||
      !b["phase"].is_string())
    return std::unexpected{
        failure("$.boarding", "invalid closed boarding shape")};
  std::size_t phase = phases.size();
  for (std::size_t i = 0; i < phases.size(); ++i)
    if (b["phase"] == phases[i]) phase = i;
  const auto entry = actor_value(b["station_entry"]);
  const auto started = decimal(b["started_tick"]);
  const auto& s = b["route"];
  if (phase == phases.size() || !entry || !started ||
      !fields(s, {"direction", "elapsed_ticks", "station_entry_eye",
                  "station_entry_heading_radians", "craft_station_position",
                  "craft_station_orientation"}) ||
      (s["direction"] != "board" && s["direction"] != "disembark") ||
      !s["elapsed_ticks"].is_number_unsigned() ||
      s["elapsed_ticks"] > kGameplayBoardingTicks ||
      !s["station_entry_heading_radians"].is_number())
    return std::unexpected{
        failure("$.boarding", "invalid phase, clock or route")};
  const auto eye = vector_value(s["station_entry_eye"]),
             position = vector_value(s["craft_station_position"]);
  const auto& q = s["craft_station_orientation"];
  if (!eye || !position || !q.is_array() || q.size() != 4)
    return std::unexpected{failure("$.boarding.route", "invalid transform")};
  for (const auto& x : q)
    if (!x.is_number())
      return std::unexpected{
          failure("$.boarding.route", "numeric quaternion required")};
  std::optional<OriginWalkerState> actor;
  if (!root["station_actor"].is_null()) {
    actor = actor_value(root["station_actor"]);
    if (!actor)
      return std::unexpected{
          failure("$.station_actor", "invalid station actor")};
  }
  auto voyage = decode_freedom_docking_document_json(root["voyage"].dump());
  if (!voyage) return std::unexpected{voyage.error()};
  FreedomBoardingSaveDocument d{
      std::move(*voyage),
      {},
      {static_cast<FreedomBoardingPhase>(phase),
       *entry,
       *started,
       {s["direction"] == "board" ? GameplayBoardingDirection::board
                                  : GameplayBoardingDirection::disembark,
        s["elapsed_ticks"].get<std::uint32_t>(),
        *eye,
        s["station_entry_heading_radians"].get<double>(),
        *position,
        {q[0].get<double>(), q[1].get<double>(), q[2].get<double>(),
         q[3].get<double>()}}},
      actor};
  if (auto v = validate_freedom_boarding_document(d); !v)
    return std::unexpected{v.error()};
  return d;
}
} // namespace apsis_drift
