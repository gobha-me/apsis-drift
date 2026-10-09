#include "apsis_drift/freedom_travel_save.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <type_traits>
#include <unordered_set>

namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;
auto failure(std::string message) -> SaveSchemaError {
  return {SaveSchemaErrorCode::invalid_state, "$.travel", std::move(message)};
}
auto fields(const Json& j, std::initializer_list<std::string_view> names)
    -> bool {
  if (!j.is_object() || j.size() != names.size()) return false;
  for (auto name : names)
    if (!j.contains(std::string{name})) return false;
  return true;
}
auto uints(const Json& j, std::initializer_list<std::string_view> names)
    -> bool {
  for (auto name : names)
    if (!j.contains(std::string{name}) ||
        !j[std::string{name}].is_number_unsigned())
      return false;
  return true;
}
auto parse(std::string_view text) -> std::expected<Json, SaveSchemaError> {
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "travel save exceeds limit"}};
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
      if (depth == stack.size())
        return std::unexpected{failure("excessive nesting")};
      stack[depth++] = c;
    } else if (c == '}' || c == ']') {
      if (!depth || stack[depth - 1] != (c == '}' ? '{' : '['))
        return std::unexpected{failure("unbalanced nesting")};
      --depth;
    }
  }
  if (depth || quoted) return std::unexpected{failure("unbalanced nesting")};
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
    auto root = Json::parse(text.begin(), text.end(), callback);
    if (duplicate)
      return std::unexpected{SaveSchemaError{SaveSchemaErrorCode::duplicate_key,
                                             "$", "repeated travel key"}};
    return root;
  } catch (const Json::exception& e) {
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::malformed_json, "$", e.what()}};
  }
}
auto source_world(Seed seed, SystemId system)
    -> std::expected<PhysicalLocalSystem, SaveSchemaError> {
  const auto ids = generate_first_intersystem_identities(seed);
  if (system != ids.origin_system && system != ids.target_system)
    return std::unexpected{failure("unknown frozen source world")};
  auto result = system == ids.origin_system
                    ? generate_physical_origin_system(seed, 2)
                    : generate_physical_local_system(ids.target_system_seed, 2);
  if (!result)
    return std::unexpected{failure("unsupported frozen source recipe")};
  return std::move(*result);
}
auto resources_json(const FreedomResources& r) -> Json {
  return {{"version", r.version},
          {"craft", r.craft.value},
          {"frame_id", r.frame.id.value},
          {"frame_version", r.frame.version},
          {"tick", r.tick},
          {"flight_quanta", r.flight_quanta},
          {"jump_charges", r.jump_charges}};
}
auto resources_value(const Json& r)
    -> std::expected<FreedomResources, SaveSchemaError> {
  if (!fields(r, {"version", "craft", "frame_id", "frame_version", "tick",
                  "flight_quanta", "jump_charges"}) ||
      !uints(r, {"version", "craft", "frame_id", "frame_version", "tick",
                 "flight_quanta", "jump_charges"}) ||
      r["version"] != kFreedomResourceVersion ||
      r["frame_id"] != kWayfarerFrameId.value ||
      r["frame_version"] != kWayfarerFrameVersion ||
      r["jump_charges"] > kFreedomJumpCapacity)
    return std::unexpected{failure("invalid commitment resource shape")};
  FreedomResources result{1,
                          {r["craft"].get<std::uint64_t>()},
                          {kWayfarerFrameId, kWayfarerFrameVersion},
                          r["tick"].get<std::uint64_t>(),
                          r["flight_quanta"].get<std::uint64_t>(),
                          r["jump_charges"].get<std::uint32_t>()};
  if (!validate_freedom_resources(result))
    return std::unexpected{failure("invalid commitment resource quantity")};
  return result;
}
auto frozen_json(const FrozenFreedomJumpArrival& f)
    -> std::expected<Json, SaveSchemaError> {
  const auto& r = f.preview.request;
  const auto system = source_world(r.universe_seed, r.source.frame.system);
  if (!system) return std::unexpected{system.error()};
  const auto body =
      encode_rigid_body_state_json(RigidBodyWorldContext{*system}, r.source);
  if (!body) return std::unexpected{failure("frozen source cannot encode")};
  return Json{
      {"request",
       {{"version", r.version},
        {"universe_seed", r.universe_seed.value},
        {"craft", r.craft.value},
        {"source_system", r.source.frame.system.value},
        {"source", Json::parse(*body)},
        {"destination", r.destination.value},
        {"profile", static_cast<unsigned>(r.profile)},
        {"attempt", r.attempt}}},
      {"sample_seed", f.sample_seed.value},
      {"point", {f.point.x, f.point.y, f.point.z}},
      {"arrival_tick", f.preview.arrival_tick},
      {"envelope_radius_metres", *f.preview.distance.envelope_radius_metres}};
}
auto frozen_value(const Json& f)
    -> std::expected<FrozenFreedomJumpArrival, SaveSchemaError> {
  if (!fields(f, {"request", "sample_seed", "point", "arrival_tick",
                  "envelope_radius_metres"}) ||
      !uints(f, {"sample_seed", "arrival_tick", "envelope_radius_metres"}))
    return std::unexpected{failure("invalid frozen shape")};
  const auto& r = f["request"];
  if (!fields(r, {"version", "universe_seed", "craft", "source_system",
                  "source", "destination", "profile", "attempt"}) ||
      !uints(r, {"version", "universe_seed", "craft", "source_system",
                 "destination", "profile", "attempt"}) ||
      r["version"] != kFreedomJumpTargetingVersion ||
      r["profile"] > static_cast<unsigned>(IntersystemRuleProfile::pilot))
    return std::unexpected{failure("unsupported frozen request")};
  FreedomJumpRequest request;
  request.universe_seed = {r["universe_seed"].get<std::uint64_t>()};
  request.craft = {r["craft"].get<std::uint64_t>()};
  request.destination = {r["destination"].get<std::uint64_t>()};
  request.profile =
      static_cast<IntersystemRuleProfile>(r["profile"].get<unsigned>());
  request.attempt = r["attempt"].get<std::uint64_t>();
  const auto system = source_world(request.universe_seed,
                                   {r["source_system"].get<std::uint64_t>()});
  if (!system) return std::unexpected{system.error()};
  const auto source = decode_rigid_body_state_json(
      RigidBodyWorldContext{*system}, r["source"].dump());
  if (!source) return std::unexpected{failure("invalid frozen actual source")};
  request.source = *source;
  const auto regenerated = freeze_freedom_jump_arrival(request);
  if (!regenerated)
    return std::unexpected{
        failure("frozen request is outside qualified targeting")};
  const auto& point = f["point"];
  if (!point.is_array() || point.size() != 3 || !point[0].is_number() ||
      !point[1].is_number() || !point[2].is_number())
    return std::unexpected{failure("invalid frozen point")};
  const SystemPositionMetres stored{
      point[0].get<double>(), point[1].get<double>(), point[2].get<double>()};
  if (regenerated->sample_seed.value != f["sample_seed"].get<std::uint64_t>() ||
      regenerated->point != stored ||
      regenerated->preview.arrival_tick !=
          f["arrival_tick"].get<std::uint64_t>() ||
      *regenerated->preview.distance.envelope_radius_metres !=
          f["envelope_radius_metres"].get<std::uint64_t>())
    return std::unexpected{failure(
        "stored commitment does not match immutable policy/source binding")};
  return *regenerated;
}
auto binding_json(const NativeStartingAssemblySelection& a) -> Json {
  return {{"version", a.version},
          {"preset", a.preset},
          {"operating_model_sha256", a.operating_model_sha256},
          {"stowed_model_sha256", a.stowed_model_sha256},
          {"frame_sha256", a.frame_sha256},
          {"contact_sha256", a.contact_sha256},
          {"hardware",
           {a.hardware.roof_transfer, a.hardware.inner_door,
            a.hardware.seat_boarding, a.hardware.station_closure}}};
}
auto free_pilot(const FreedomSurfaceBaseSave& base) -> bool {
  return std::visit(
      [](const auto& d) {
        using T = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<T, FreedomFlightSaveDocument>)
          return true;
        else if constexpr (std::is_same_v<T, FreedomDockingSaveDocument>)
          return !d.docking.attached;
        else if constexpr (std::is_same_v<T, FreedomBoardingSaveDocument>)
          return !d.station_actor && !d.voyage.docking.attached &&
                 d.boarding.phase == FreedomBoardingPhase::seated;
        else
          return false;
      },
      base);
}
} // namespace
auto validate_freedom_travel_document(const FreedomTravelSaveDocument& d)
    -> std::expected<void, SaveSchemaError> {
  if (auto v = validate_freedom_knowledge_document(d.voyage); !v) return v;
  const auto& voyage = d.voyage.voyage.voyage;
  const auto flight = surface_base_flight(voyage.base);
  const auto& s = d.travel;
  const auto& ledger = d.voyage.voyage.resources;
  if (!flight.world ||
      d.voyage.knowledge.recipe.version != kFreedomTravelKnowledgeVersion ||
      s.version != kFreedomTravelStateVersion || !s.next_attempt ||
      (!d.craft_binding ||
       *d.craft_binding != NativeStartingAssemblySelection{}))
    return std::unexpected{
        failure("unsupported travel/knowledge/world/craft selection")};
  const auto route =
      generate_first_universe_route(flight.origin.recipe.universe_seed);
  const auto current = flight.world->system;
  const auto now = flight.flight.tick;
  if (d.seated_pilot) {
    if (!std::holds_alternative<FreedomFlightSaveDocument>(voyage.base) ||
        d.seated_pilot->phase != FreedomBoardingPhase::seated)
      return std::unexpected{
          failure("seated pilot requires a sole free-flight owner")};
    if (auto v = validate_freedom_boarding_state(*d.seated_pilot, now); !v)
      return v;
    // The route is history in the origin station, not a port in this world.
    auto home =
        generate_physical_origin_system(flight.origin.recipe.universe_seed, 2);
    const auto station =
        generate_origin_station(flight.origin.recipe.universe_seed);
    const auto geometry = origin_station_geometry(station);
    if (!home || !geometry)
      return std::unexpected{failure("Origin geometry unavailable")};
    const auto pose = resolve_origin_port_pose(*home, station, *geometry,
                                               {station.id, 1}, now);
    if (!pose ||
        d.seated_pilot->route.craft_station_position !=
            pose->station_relative.position_metres ||
        d.seated_pilot->route.craft_station_orientation !=
            pose->station_relative.orientation)
      return std::unexpected{
          failure("saved pilot route differs from canonical entry")};
  }

  if (s.selected &&
      (*s.selected == current ||
       (*s.selected != route.origin && *s.selected != route.destination)))
    return std::unexpected{
        failure("selected destination is not the other known endpoint")};
  if (s.committed.has_value() != s.commitment_resources.has_value())
    return std::unexpected{failure("unpaired commitment bill")};
  if (s.committed) {
    const auto& f = *s.committed;
    const auto& r = f.preview.request;
    const auto& before = *s.commitment_resources;
    if (!validate_frozen_freedom_jump_arrival(f) || !f.preview.volume ||
        !f.preview.volume->clear() || !f.point_assessment.clear() ||
        f.preview.arrival_tick >=
            std::numeric_limits<SimulationTick>::max() - 2 ||
        r.universe_seed != flight.origin.recipe.universe_seed ||
        r.craft != ledger.craft ||
        r.attempt == std::numeric_limits<std::uint64_t>::max() ||
        s.next_attempt != r.attempt + 1 ||
        !validate_freedom_resources(before) || before.craft != r.craft ||
        before.frame != r.source.craft || before.tick != r.source.tick ||
        !before.jump_charges)
      return std::unexpected{
          failure("invalid or unqualified committed solution/bill")};
  } else if (s.next_attempt != 1 || current != route.origin)
    return std::unexpected{
        failure("world/attempt history without a commitment")};
  switch (s.phase) {
    case FreedomJumpPhase::idle:
      if (s.spool_tick)
        return std::unexpected{failure("idle travel retains a spool clock")};
      break;
    case FreedomJumpPhase::spool:
      if (!s.selected || !free_pilot(voyage.base) || voyage.surface.landed ||
          s.spool_tick > now || now - s.spool_tick >= kJumpSpoolTicks ||
          s.spool_tick > std::numeric_limits<SimulationTick>::max() -
                             kJumpSpoolTicks - kJumpTransitTicks - 3)
        return std::unexpected{failure("invalid physical spool/clock")};
      break;
    case FreedomJumpPhase::transit: {
      if (!s.committed || !s.selected ||
          !std::holds_alternative<FreedomFlightSaveDocument>(voyage.base) ||
          voyage.surface.landed)
        return std::unexpected{
            failure("transit requires a frozen free-flight owner")};
      const auto& f = *s.committed;
      const auto& r = f.preview.request;
      const auto& before = *s.commitment_resources;
      if (s.spool_tick >
              std::numeric_limits<SimulationTick>::max() - kJumpSpoolTicks ||
          r.source.tick != s.spool_tick + kJumpSpoolTicks ||
          r.source.tick > now || now >= f.preview.arrival_tick ||
          *s.selected != r.destination || current != r.source.frame.system ||
          before.flight_quanta != ledger.flight_quanta ||
          before.jump_charges - 1 != ledger.jump_charges ||
          r.profile != (flight.model.assistance
                            ? IntersystemRuleProfile::assisted
                            : IntersystemRuleProfile::pilot))
        return std::unexpected{
            failure("transit source, charge or clock differs from commitment")};
      auto pose = flight.flight;
      pose.tick = r.source.tick;
      if (pose != r.source)
        return std::unexpected{failure("transit source pose changed")};
      return {};
    }
    default: return std::unexpected{failure("unknown native travel phase")};
  }
  if (s.committed && (s.committed->preview.request.destination != current ||
                      s.committed->preview.arrival_tick > now ||
                      (s.phase == FreedomJumpPhase::spool &&
                       s.committed->preview.arrival_tick > s.spool_tick)))
    return std::unexpected{
        failure("arrival history differs from current world/clock")};
  return {};
}
auto encode_freedom_travel_document_json(const FreedomTravelSaveDocument& d)
    -> std::expected<std::string, SaveSchemaError> {
  if (auto v = validate_freedom_travel_document(d); !v)
    return std::unexpected{v.error()};
  const auto voyage = encode_freedom_knowledge_document_json(d.voyage);
  if (!voyage) return std::unexpected{voyage.error()};
  Json frozen = nullptr;
  if (d.travel.committed) {
    auto encoded = frozen_json(*d.travel.committed);
    if (!encoded) return std::unexpected{encoded.error()};
    frozen = std::move(*encoded);
  }
  Json pilot = nullptr;
  if (d.seated_pilot) {
    auto encoded = encode_freedom_boarding_state_json(
        *d.seated_pilot,
        surface_base_flight(d.voyage.voyage.voyage.base).flight.tick);
    if (!encoded) return std::unexpected{encoded.error()};
    pilot = Json::parse(*encoded);
  }
  const auto& s = d.travel;
  const Json root = {
      {"format_version", kFreedomTravelSaveFormatVersion},
      {"voyage", Json::parse(*voyage)},
      {"travel",
       {{"version", s.version},
        {"phase", static_cast<unsigned>(s.phase)},
        {"selected", s.selected ? Json(s.selected->value) : Json(nullptr)},
        {"spool_tick", s.spool_tick},
        {"next_attempt", s.next_attempt},
        {"committed", std::move(frozen)},
        {"commitment_resources", s.commitment_resources
                                     ? resources_json(*s.commitment_resources)
                                     : Json(nullptr)}}},
      {"craft_binding",
       d.craft_binding ? binding_json(*d.craft_binding) : Json(nullptr)},
      {"seated_pilot", std::move(pilot)}};
  auto text = root.dump(2) + '\n';
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{failure("travel save exceeds limit")};
  return text;
}
auto decode_freedom_travel_document_json(std::string_view text)
    -> std::expected<FreedomTravelSaveDocument, SaveSchemaError> {
  auto root = parse(text);
  if (!root) return std::unexpected{root.error()};
  if (!root->is_object() || !uints(*root, {"format_version"}))
    return std::unexpected{failure("unsigned travel version required")};
  if ((*root)["format_version"] != kFreedomTravelSaveFormatVersion)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::unsupported_format_version,
                        "$.format_version", "unsupported travel format"}};
  if (!fields(*root, {"format_version", "voyage", "travel", "craft_binding",
                      "seated_pilot"}))
    return std::unexpected{failure("invalid closed travel shape")};
  const auto& s = (*root)["travel"];
  if (!fields(s, {"version", "phase", "selected", "spool_tick", "next_attempt",
                  "committed", "commitment_resources"}) ||
      !uints(s, {"version", "phase", "spool_tick", "next_attempt"}) ||
      s["version"] != kFreedomTravelStateVersion ||
      s["phase"] > static_cast<unsigned>(FreedomJumpPhase::transit))
    return std::unexpected{failure("unsupported travel state")};
  auto voyage =
      decode_freedom_knowledge_document_json((*root)["voyage"].dump());
  if (!voyage) return std::unexpected{voyage.error()};
  FreedomTravelSaveDocument result;
  result.voyage = std::move(*voyage);
  result.travel.phase =
      static_cast<FreedomJumpPhase>(s["phase"].get<unsigned>());
  result.travel.spool_tick = s["spool_tick"].get<std::uint64_t>();
  result.travel.next_attempt = s["next_attempt"].get<std::uint64_t>();
  if (!s["selected"].is_null()) {
    if (!s["selected"].is_number_unsigned())
      return std::unexpected{failure("invalid selected endpoint")};
    result.travel.selected = SystemId{s["selected"].get<std::uint64_t>()};
  }
  if (!s["committed"].is_null()) {
    auto value = frozen_value(s["committed"]);
    if (!value) return std::unexpected{value.error()};
    result.travel.committed = std::move(*value);
  }
  if (!s["commitment_resources"].is_null()) {
    auto value = resources_value(s["commitment_resources"]);
    if (!value) return std::unexpected{value.error()};
    result.travel.commitment_resources = *value;
  }
  const auto& binding = (*root)["craft_binding"];
  if (!binding.is_null()) {
    // The existing supported asset selection is closed and pinned. No silent
    // lookup/defaulting can replace a saved selection with a different skin.
    if (!fields(binding, {"version", "preset", "operating_model_sha256",
                          "stowed_model_sha256", "frame_sha256",
                          "contact_sha256", "hardware"}) ||
        !uints(binding, {"version"}))
      return std::unexpected{failure("invalid saved craft binding shape")};
    const auto canonical = binding_json(NativeStartingAssemblySelection{});
    for (const auto& [key, value] : canonical.items())
      if (binding[key] != value)
        return std::unexpected{failure("unsupported saved craft binding")};
    result.craft_binding = NativeStartingAssemblySelection{};
  }
  if (!(*root)["seated_pilot"].is_null()) {
    auto pilot = decode_freedom_boarding_state_json(
        (*root)["seated_pilot"].dump(),
        surface_base_flight(result.voyage.voyage.voyage.base).flight.tick);
    if (!pilot) return std::unexpected{pilot.error()};
    result.seated_pilot = *pilot;
  }
  if (auto v = validate_freedom_travel_document(result); !v)
    return std::unexpected{v.error()};
  return result;
}
} // namespace apsis_drift
