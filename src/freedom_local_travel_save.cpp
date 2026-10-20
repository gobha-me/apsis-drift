#include "freedom_local_travel_save_internal.hpp"

#include <limits>
#include <type_traits>

namespace apsis_drift::detail {
namespace {
using Json = nlohmann::ordered_json;
auto failure(std::string message) -> SaveSchemaError {
  return {SaveSchemaErrorCode::invalid_state, "$.travel.local",
          std::move(message)};
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
auto frozen_json(const FrozenLocalBodyJumpArrival& f)
    -> std::expected<Json, SaveSchemaError> {
  const auto source = encode_freedom_flight_document_json(f.request.source);
  const auto arrival = encode_freedom_flight_document_json(f.arrival);
  const auto target = encode_body_target(f.request.destination);
  if (!source || !arrival || !target)
    return std::unexpected{failure("Local frozen owner cannot encode")};
  return Json{{"version", f.version},
              {"source", Json::parse(*source)},
              {"destination", Json::parse(*target)},
              {"attempt", f.request.attempt},
              {"source_checksum", f.source_checksum},
              {"arrival_tick", f.arrival_tick},
              {"arrival", Json::parse(*arrival)}};
}
auto frozen_value(const Json& j)
    -> std::expected<FrozenLocalBodyJumpArrival, SaveSchemaError> {
  if (!fields(j, {"version", "source", "destination", "attempt",
                  "source_checksum", "arrival_tick", "arrival"}) ||
      !uints(j, {"version", "attempt", "source_checksum", "arrival_tick"}) ||
      (j["version"] != kFreedomLocalBodyJumpVersion &&
       j["version"] != kFreedomMinorLocalBodyJumpVersion))
    return std::unexpected{failure("Invalid closed local commitment")};
  auto source = decode_freedom_flight_document_json(j["source"].dump());
  auto arrival = decode_freedom_flight_document_json(j["arrival"].dump());
  const auto target = decode_body_target(j["destination"].dump());
  if (!source || !arrival || !target)
    return std::unexpected{failure("Invalid local commitment owner")};
  const auto home_model =
      (source->moon || source->minor) ? arrival->model : source->model;
  const auto regenerated = freeze_local_body_jump_arrival(
      {j["version"].get<std::uint32_t>(), std::move(*source), *target,
       j["attempt"].get<std::uint64_t>(), home_model});
  if (!regenerated || regenerated->arrival != *arrival ||
      regenerated->arrival_tick != j["arrival_tick"].get<SimulationTick>() ||
      regenerated->source_checksum != j["source_checksum"].get<std::uint64_t>())
    return std::unexpected{
        failure("Local commitment differs from its immutable solution")};
  return *regenerated;
}
auto same_history(const FreedomFlightSaveDocument& a,
                  const FreedomFlightSaveDocument& b) -> bool {
  auto history = a.origin;
  history.state.tick = b.origin.state.tick;
  return history == b.origin && a.flight.craft == b.flight.craft;
}
auto target_of(const FreedomFlightSaveDocument& d)
    -> std::optional<BodyTarget> {
  if (d.minor) return d.minor->target;
  if (d.moon) return d.moon->target;
  if (d.world) return BodyTarget{d.world->system, body_id(d.world->planet)};
  return {};
}
auto valid_pilot_binding(const FreedomTravelSaveDocument& d,
                         const FreedomFlightSaveDocument& flight) -> bool {
  const auto& base = d.voyage.voyage.voyage.base;
  if (!d.seated_pilot)
    return !std::holds_alternative<FreedomFlightSaveDocument>(base);
  if (!std::holds_alternative<FreedomFlightSaveDocument>(base) ||
      d.seated_pilot->phase != FreedomBoardingPhase::seated ||
      !validate_freedom_boarding_state(*d.seated_pilot, flight.flight.tick))
    return false;
  const auto home =
      generate_physical_origin_system(flight.origin.recipe.universe_seed, 2);
  const auto station =
      generate_origin_station(flight.origin.recipe.universe_seed);
  const auto geometry = origin_station_geometry(station);
  if (!home || !geometry) return false;
  const auto pose = resolve_origin_port_pose(
      *home, station, *geometry, {station.id, 1}, flight.flight.tick);
  return pose &&
         d.seated_pilot->route.craft_station_position ==
             pose->station_relative.position_metres &&
         d.seated_pilot->route.craft_station_orientation ==
             pose->station_relative.orientation;
}
auto free_pilot(const FreedomTravelSaveDocument& d) -> bool {
  return std::visit(
      [](const auto& base) {
        using T = std::decay_t<decltype(base)>;
        if constexpr (std::is_same_v<T, FreedomFlightSaveDocument>)
          return true;
        else if constexpr (std::is_same_v<T, FreedomBoardingSaveDocument>)
          return !base.station_actor && !base.voyage.docking.attached &&
                 base.boarding.phase == FreedomBoardingPhase::seated;
        else
          return false;
      },
      d.voyage.voyage.voyage.base);
}
// A prior intersystem commitment stays historical. It must have returned to
// the genuine home before the first local departure and cannot be relabelled
// as a Moon commitment or silently discarded when selecting the local route.
auto prior_history(const FreedomTravelSaveDocument& d,
                   const FreedomFlightSaveDocument& first_source,
                   std::uint64_t first_attempt) -> bool {
  const auto& s = d.travel;
  if (s.committed.has_value() != s.commitment_resources.has_value())
    return false;
  if (s.prior_moon) {
    const auto& local = s.prior_moon->local;
    if (local.selected || !local.committed || !local.outbound ||
        !local.commitment_resources)
      return false;
    const auto& first = *local.outbound;
    const auto& last = *local.committed;
    const auto& bill = *local.commitment_resources;
    if (!validate_frozen_local_body_jump_arrival(first) ||
        !validate_frozen_local_body_jump_arrival(last) || first.version != 1 ||
        last.version != 1 || first.request.source.moon ||
        first.request.source.minor || !last.request.source.moon ||
        last.arrival.moon || last.arrival.minor ||
        last.request.home_model != first.request.home_model ||
        first_source.model != first.request.home_model ||
        !same_history(first.request.source, first_source) ||
        !same_history(last.request.source, first_source) ||
        last.request.attempt < first.request.attempt ||
        (last.request.attempt - first.request.attempt) % 2 != 1 ||
        last.request.attempt == std::numeric_limits<std::uint64_t>::max() ||
        first_attempt != last.request.attempt + 1 ||
        first.arrival_tick > last.request.source.flight.tick ||
        last.arrival_tick > first_source.flight.tick ||
        !validate_freedom_resources(bill, last.request.source) ||
        !bill.jump_charges)
      return false;
    auto prior = d;
    prior.travel.prior_moon.reset();
    return prior_history(prior, first.request.source, first.request.attempt);
  }
  if (!s.committed) return first_attempt == 1;
  const auto& f = *s.committed;
  const auto& r = f.preview.request;
  const auto& bill = *s.commitment_resources;
  const auto ids = generate_first_intersystem_identities(
      first_source.origin.recipe.universe_seed);
  return validate_frozen_freedom_jump_arrival(f).has_value() &&
         f.preview.volume && f.preview.volume->clear() &&
         f.point_assessment.clear() &&
         r.universe_seed == first_source.origin.recipe.universe_seed &&
         r.craft == first_source.origin.state.craft &&
         r.lineage == first_source.origin.lineage &&
         r.destination == ids.origin_system &&
         f.preview.arrival_tick <= first_source.flight.tick &&
         r.attempt < std::numeric_limits<std::uint64_t>::max() &&
         r.attempt + 1 == first_attempt &&
         validate_freedom_resources(bill).has_value() &&
         bill.craft == r.craft && bill.frame == r.source.craft &&
         bill.tick == r.source.tick && bill.jump_charges;
}
} // namespace

auto validate_local_travel(const FreedomTravelSaveDocument& d)
    -> std::expected<void, SaveSchemaError> {
  if (const auto v = validate_freedom_knowledge_document(d.voyage); !v)
    return v;
  const auto& s = d.travel;
  const bool minor_route = s.version == kFreedomMinorTravelStateVersion;
  if ((s.version != kFreedomLocalTravelStateVersion && !minor_route) ||
      (!minor_route && s.prior_moon) || !s.local || s.selected ||
      !s.next_attempt || !d.craft_binding ||
      *d.craft_binding != NativeStartingAssemblySelection{} ||
      d.voyage.knowledge.recipe.version != kFreedomTravelKnowledgeVersion)
    return std::unexpected{failure("Local travel requires explicit version, "
                                   "knowledge and actual craft binding")};
  const auto& local = *s.local;
  const auto& surface = d.voyage.voyage.voyage;
  const auto flight = surface_base_flight(surface.base);
  const auto now = flight.flight.tick;
  const auto current = target_of(flight);
  const auto system =
      generate_physical_origin_system(flight.origin.recipe.universe_seed, 2);
  if (!current || !system || current->system != system->catalog.id ||
      !valid_pilot_binding(d, flight))
    return std::unexpected{failure("Local travel requires the actual home/Moon "
                                   "and a retained seated route")};
  const auto catalog = generate_secondary_body_catalog(*system);
  if (!catalog)
    return std::unexpected{failure("Local body source unavailable")};
  const auto destination_body =
      minor_route ? catalog->bodies[1].node.id : catalog->bodies[0].node.id;
  const BodyTarget home_target{system->catalog.id,
                               body_id(flight.origin.recipe.home_planet)};
  const BodyTarget moon_target{system->catalog.id, destination_body};
  if ((*current != home_target && *current != moon_target) ||
      (local.selected &&
       (*local.selected !=
        (*current == home_target ? moon_target : home_target))) ||
      local.committed.has_value() != local.commitment_resources.has_value() ||
      local.committed.has_value() != local.outbound.has_value())
    return std::unexpected{
        failure("Invalid local target or unpaired history/bill")};
  if (!local.outbound) {
    if (*current != home_target || !prior_history(d, flight, s.next_attempt))
      return std::unexpected{
          failure("Local adoption has no authentic home history")};
  } else {
    const auto& outbound = *local.outbound;
    const auto& f = *local.committed;
    const auto& r = f.request;
    const auto& bill = *local.commitment_resources;
    if (!validate_frozen_local_body_jump_arrival(outbound) ||
        outbound.version != (minor_route ? kFreedomMinorLocalBodyJumpVersion
                                         : kFreedomLocalBodyJumpVersion) ||
        outbound.request.source.moon || outbound.request.source.minor ||
        outbound.request.destination != moon_target ||
        !same_history(outbound.request.source, flight) ||
        !prior_history(d, outbound.request.source, outbound.request.attempt) ||
        !validate_frozen_local_body_jump_arrival(f) ||
        !same_history(r.source, flight) ||
        r.home_model != outbound.request.home_model ||
        r.attempt < outbound.request.attempt ||
        r.attempt == std::numeric_limits<std::uint64_t>::max() ||
        s.next_attempt != r.attempt + 1 ||
        ((minor_route ? r.source.minor.has_value()
                      : r.source.moon.has_value()) !=
         ((r.attempt - outbound.request.attempt) % 2 == 1)) ||
        (r.attempt == outbound.request.attempt && f != outbound) ||
        (r.attempt != outbound.request.attempt &&
         outbound.arrival_tick > r.source.flight.tick) ||
        !validate_freedom_resources(bill, r.source) || !bill.jump_charges)
      return std::unexpected{failure(
          "Local history, original home profile or commitment bill differs")};
  }
  switch (s.phase) {
    case FreedomJumpPhase::idle:
      if (s.spool_tick)
        return std::unexpected{
            failure("Idle local travel retains a spool clock")};
      break;
    case FreedomJumpPhase::spool:
      if (!local.selected || !free_pilot(d) || surface.surface.landed ||
          surface.surface.moon_landed || s.spool_tick > now ||
          now - s.spool_tick >= kJumpSpoolTicks ||
          s.spool_tick > std::numeric_limits<SimulationTick>::max() -
                             kJumpSpoolTicks - kJumpTransitTicks - 3)
        return std::unexpected{
            failure("Invalid local spool or supported-surface constraint")};
      break;
    case FreedomJumpPhase::transit: {
      if (!local.committed || !local.selected ||
          !std::holds_alternative<FreedomFlightSaveDocument>(surface.base) ||
          surface.surface.landed || surface.surface.moon_landed)
        return std::unexpected{
            failure("Local transit requires frozen free flight")};
      const auto& f = *local.committed;
      const auto& r = f.request;
      const auto& bill = *local.commitment_resources;
      const auto& ledger = d.voyage.voyage.resources;
      auto source_view = flight;
      source_view.flight.tick = r.source.flight.tick;
      source_view.origin.state.tick = r.source.origin.state.tick;
      if (s.spool_tick >
              std::numeric_limits<SimulationTick>::max() - kJumpSpoolTicks ||
          r.source.flight.tick != s.spool_tick + kJumpSpoolTicks ||
          r.source.flight.tick > now || now >= f.arrival_tick ||
          *local.selected != r.destination || source_view != r.source ||
          ledger.flight_quanta != bill.flight_quanta ||
          ledger.jump_charges != bill.jump_charges - 1)
        return std::unexpected{
            failure("Local transit source, clock or one-charge debit differs")};
      return {};
    }
    default: return std::unexpected{failure("Unknown local travel phase")};
  }
  if (local.committed) {
    const auto& f = *local.committed;
    if (f.request.destination != *current || f.arrival_tick > now ||
        (s.phase == FreedomJumpPhase::spool && f.arrival_tick > s.spool_tick) ||
        (now == f.arrival_tick &&
         (flight.flight != f.arrival.flight ||
          flight.origin != f.arrival.origin ||
          d.voyage.voyage.resources.flight_quanta !=
              local.commitment_resources->flight_quanta ||
          d.voyage.voyage.resources.jump_charges !=
              local.commitment_resources->jump_charges - 1)))
      return std::unexpected{failure("Local arrival history differs from "
                                     "current owner, instant pose or charge")};
  }
  return {};
}

auto encode_local_travel(const FreedomTravelSaveDocument& d)
    -> std::expected<std::string, SaveSchemaError> {
  if (const auto v = validate_local_travel(d); !v)
    return std::unexpected{v.error()};
  const auto voyage = encode_freedom_knowledge_document_json(d.voyage);
  if (!voyage) return std::unexpected{voyage.error()};
  const auto& s = d.travel;
  const auto& local = *s.local;
  Json old = nullptr, commitment = nullptr, outbound = nullptr,
       selected = nullptr, pilot = nullptr;
  if (s.committed) {
    auto encoded = travel_frozen_json(*s.committed);
    if (!encoded) return std::unexpected{encoded.error()};
    old = std::move(*encoded);
  }
  if (local.committed) {
    auto encoded = frozen_json(*local.committed);
    auto first = frozen_json(*local.outbound);
    if (!encoded) return std::unexpected{encoded.error()};
    if (!first) return std::unexpected{first.error()};
    commitment = std::move(*encoded);
    outbound = std::move(*first);
  }
  if (local.selected) {
    const auto target = encode_body_target(*local.selected);
    if (!target)
      return std::unexpected{failure("Local selected target cannot encode")};
    selected = Json::parse(*target);
  }
  if (d.seated_pilot) {
    const auto encoded = encode_freedom_boarding_state_json(
        *d.seated_pilot,
        surface_base_flight(d.voyage.voyage.voyage.base).flight.tick);
    if (!encoded) return std::unexpected{encoded.error()};
    pilot = Json::parse(*encoded);
  }
  Json root{
      {"format_version", s.version == kFreedomMinorTravelStateVersion
                             ? kFreedomMinorTravelSaveFormatVersion
                             : kFreedomLocalTravelSaveFormatVersion},
      {"voyage", Json::parse(*voyage)},
      {"travel",
       {{"version", s.version},
        {"phase", static_cast<unsigned>(s.phase)},
        {"selected", nullptr},
        {"spool_tick", s.spool_tick},
        {"next_attempt", s.next_attempt},
        {"committed", std::move(old)},
        {"commitment_resources",
         s.commitment_resources ? travel_resources_json(*s.commitment_resources)
                                : Json(nullptr)},
        {"local",
         {{"selected", std::move(selected)},
          {"committed", std::move(commitment)},
          {"commitment_resources",
           local.commitment_resources
               ? travel_resources_json(*local.commitment_resources)
               : Json(nullptr)},
          {"outbound", std::move(outbound)}}}}},
      {"craft_binding", travel_binding_json(*d.craft_binding)},
      {"seated_pilot", std::move(pilot)}};
  if (s.version == kFreedomMinorTravelStateVersion) {
    Json prior = nullptr;
    if (s.prior_moon) {
      const auto& p = s.prior_moon->local;
      auto committed_prior = frozen_json(*p.committed);
      auto outbound_prior = frozen_json(*p.outbound);
      if (!committed_prior) return std::unexpected{committed_prior.error()};
      if (!outbound_prior) return std::unexpected{outbound_prior.error()};
      prior = {{"version", 2},
               {"committed", std::move(*committed_prior)},
               {"outbound", std::move(*outbound_prior)},
               {"commitment_resources",
                travel_resources_json(*p.commitment_resources)}};
    }
    root["travel"]["prior_moon"] = std::move(prior);
  }
  auto text = root.dump(2) + '\n';
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{failure("Local travel save exceeds document bound")};
  return text;
}

auto decode_local_travel(const Json& root)
    -> std::expected<FreedomTravelSaveDocument, SaveSchemaError> {
  if (!fields(root, {"format_version", "voyage", "travel", "craft_binding",
                     "seated_pilot"}))
    return std::unexpected{failure("Invalid closed local travel root")};
  const auto& s = root["travel"];
  const bool minor_route =
      root["format_version"] == kFreedomMinorTravelSaveFormatVersion;
  auto shape = s;
  if (minor_route) {
    if (!shape.contains("prior_moon"))
      return std::unexpected{failure("Missing retained Moon field")};
    shape.erase("prior_moon");
  }
  if (!fields(shape,
              {"version", "phase", "selected", "spool_tick", "next_attempt",
               "committed", "commitment_resources", "local"}) ||
      !uints(s, {"version", "phase", "spool_tick", "next_attempt"}) ||
      s["version"] != (minor_route ? kFreedomMinorTravelStateVersion
                                   : kFreedomLocalTravelStateVersion) ||
      !s["selected"].is_null() ||
      s["phase"] > static_cast<unsigned>(FreedomJumpPhase::transit))
    return std::unexpected{
        failure("Invalid local travel version or shared phase")};
  const auto& local = s["local"];
  const auto& binding = root["craft_binding"];
  if (!fields(local,
              {"selected", "committed", "commitment_resources", "outbound"}) ||
      !fields(binding, {"version", "preset", "operating_model_sha256",
                        "stowed_model_sha256", "frame_sha256", "contact_sha256",
                        "hardware"}) ||
      !uints(binding, {"version"}))
    return std::unexpected{
        failure("Invalid closed local state or authored binding")};
  const auto canonical_binding =
      travel_binding_json(NativeStartingAssemblySelection{});
  for (const auto& [key, value] : canonical_binding.items())
    if (binding[key] != value)
      return std::unexpected{
          failure("Unsupported authored local craft binding")};
  auto voyage = decode_freedom_knowledge_document_json(root["voyage"].dump());
  if (!voyage) return std::unexpected{voyage.error()};
  FreedomTravelSaveDocument d;
  d.voyage = std::move(*voyage);
  d.craft_binding = NativeStartingAssemblySelection{};
  d.travel.version = minor_route ? kFreedomMinorTravelStateVersion
                                 : kFreedomLocalTravelStateVersion;
  d.travel.phase = static_cast<FreedomJumpPhase>(s["phase"].get<unsigned>());
  d.travel.spool_tick = s["spool_tick"].get<SimulationTick>();
  d.travel.next_attempt = s["next_attempt"].get<std::uint64_t>();
  d.travel.local.emplace();
  auto& target = *d.travel.local;
  if (!s["committed"].is_null()) {
    auto value = travel_frozen_value(s["committed"]);
    if (!value) return std::unexpected{value.error()};
    d.travel.committed = std::move(*value);
  }
  if (!s["commitment_resources"].is_null()) {
    const auto value = travel_resources_value(s["commitment_resources"]);
    if (!value) return std::unexpected{value.error()};
    d.travel.commitment_resources = *value;
  }
  if (!local["selected"].is_null()) {
    const auto value = decode_body_target(local["selected"].dump());
    if (!value)
      return std::unexpected{failure("Invalid typed local destination")};
    target.selected = *value;
  }
  if (!local["committed"].is_null()) {
    auto value = frozen_value(local["committed"]);
    if (!value) return std::unexpected{value.error()};
    target.committed = std::move(*value);
  }
  if (!local["commitment_resources"].is_null()) {
    const auto value = travel_resources_value(local["commitment_resources"]);
    if (!value) return std::unexpected{value.error()};
    target.commitment_resources = *value;
  }
  if (!local["outbound"].is_null()) {
    auto value = frozen_value(local["outbound"]);
    if (!value) return std::unexpected{value.error()};
    target.outbound = std::move(*value);
  }
  if (minor_route && !s["prior_moon"].is_null()) {
    const auto& p = s["prior_moon"];
    if (!fields(p,
                {"version", "committed", "outbound", "commitment_resources"}) ||
        !uints(p, {"version"}) || p["version"] != 2)
      return std::unexpected{failure("Invalid retained Moon history")};
    auto c = frozen_value(p["committed"]);
    auto o = frozen_value(p["outbound"]);
    auto bill = travel_resources_value(p["commitment_resources"]);
    if (!c || !o || !bill)
      return std::unexpected{failure("Invalid retained Moon proof")};
    d.travel.prior_moon = FreedomLocalMoonHistory{
        FreedomLocalTravelState{{}, std::move(*c), *bill, std::move(*o)}};
  }
  if (!root["seated_pilot"].is_null()) {
    auto value = decode_freedom_boarding_state_json(
        root["seated_pilot"].dump(),
        surface_base_flight(d.voyage.voyage.voyage.base).flight.tick);
    if (!value) return std::unexpected{value.error()};
    d.seated_pilot = *value;
  }
  if (const auto v = validate_local_travel(d); !v)
    return std::unexpected{v.error()};
  return d;
}
} // namespace apsis_drift::detail
