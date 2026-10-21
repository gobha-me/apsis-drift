#include "freedom_local_travel_save_internal.hpp"
#include "freedom_resource_save_internal.hpp"

#include <algorithm>
#include <iterator>
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
  Json result{{"version", f.version},
              {"source", Json::parse(*source)},
              {"destination", Json::parse(*target)},
              {"attempt", f.request.attempt},
              {"source_checksum", f.source_checksum},
              {"arrival_tick", f.arrival_tick},
              {"arrival", Json::parse(*arrival)}};
  if (f.request.recovery)
    result["recovery_input"] = drive_json(*f.request.recovery);
  if (f.recovery) result["recovery"] = uncertainty_json(*f.recovery);
  return result;
}
auto frozen_value(const Json& j)
    -> std::expected<FrozenLocalBodyJumpArrival, SaveSchemaError> {
  const bool recovering =
      j.is_object() && j.contains("version") &&
      j["version"].is_number_unsigned() &&
      (j["version"] == kFreedomRecoveryLocalBodyJumpVersion ||
       j["version"] == kFreedomRecoveryMinorLocalBodyJumpVersion);
  auto shape = j;
  if (recovering) {
    if (!j.contains("recovery_input") || !j.contains("recovery"))
      return std::unexpected{failure("Missing local recovery input/sample")};
    shape.erase("recovery_input");
    shape.erase("recovery");
  }
  if (!fields(shape, {"version", "source", "destination", "attempt",
                      "source_checksum", "arrival_tick", "arrival"}) ||
      !uints(j, {"version", "attempt", "source_checksum", "arrival_tick"}) ||
      (j["version"] != kFreedomLocalBodyJumpVersion &&
       j["version"] != kFreedomMinorLocalBodyJumpVersion && !recovering))
    return std::unexpected{failure("Invalid closed local commitment")};
  auto source = decode_freedom_flight_document_json(j["source"].dump());
  auto arrival = decode_freedom_flight_document_json(j["arrival"].dump());
  const auto target = decode_body_target(j["destination"].dump());
  if (!source || !arrival || !target)
    return std::unexpected{failure("Invalid local commitment owner")};
  const auto home_model =
      (source->moon || source->minor) ? arrival->model : source->model;
  FreedomLocalBodyJumpRequest request{
      j["version"].get<std::uint32_t>(), std::move(*source), *target,
      j["attempt"].get<std::uint64_t>(), home_model};
  if (recovering) {
    const auto input =
        drive_value(j["recovery_input"], request.source.flight.tick);
    if (!input) return std::unexpected{input.error()};
    request.recovery = *input;
  }
  const auto regenerated = freeze_local_body_jump_arrival(request);
  if (!regenerated || regenerated->arrival != *arrival ||
      regenerated->arrival_tick != j["arrival_tick"].get<SimulationTick>() ||
      regenerated->source_checksum != j["source_checksum"].get<std::uint64_t>())
    return std::unexpected{
        failure("Local commitment differs from its immutable solution")};
  if (recovering) {
    const auto sample = uncertainty_value(j["recovery"]);
    if (!sample || !regenerated->recovery || *regenerated->recovery != *sample)
      return std::unexpected{
          failure("Local recovery sample differs from its input")};
  }
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
         r.version != kFreedomRecoveryJumpTargetingVersion && !r.recovery &&
         !f.recovery && f.preview.volume && f.preview.volume->clear() &&
         f.point_assessment.clear() &&
         r.universe_seed == first_source.origin.recipe.universe_seed &&
         r.craft == first_source.origin.state.craft &&
         r.lineage == first_source.origin.lineage &&
         r.network == first_source.origin.network &&
         r.destination == ids.origin_system &&
         f.preview.arrival_tick <= first_source.flight.tick &&
         r.attempt < std::numeric_limits<std::uint64_t>::max() &&
         r.attempt + 1 == first_attempt &&
         validate_freedom_resources(bill).has_value() &&
         bill.craft == r.craft && bill.frame == r.source.craft &&
         bill.tick == r.source.tick && bill.jump_charges;
}
auto local_json(const FreedomLocalTravelState& local)
    -> std::expected<Json, SaveSchemaError> {
  Json selected = nullptr, committed = nullptr, outbound = nullptr;
  if (local.selected) {
    const auto value = encode_body_target(*local.selected);
    if (!value)
      return std::unexpected{failure("Invalid origin selected target")};
    selected = Json::parse(*value);
  }
  if (local.committed) {
    auto value = frozen_json(*local.committed);
    if (!value) return std::unexpected{value.error()};
    committed = std::move(*value);
  }
  if (local.outbound) {
    auto value = frozen_json(*local.outbound);
    if (!value) return std::unexpected{value.error()};
    outbound = std::move(*value);
  }
  return Json{{"selected", std::move(selected)},
              {"committed", std::move(committed)},
              {"commitment_resources",
               local.commitment_resources
                   ? travel_resources_json(*local.commitment_resources)
                   : Json(nullptr)},
              {"outbound", std::move(outbound)}};
}
auto local_value(const Json& j)
    -> std::expected<FreedomLocalTravelState, SaveSchemaError> {
  if (!fields(j, {"selected", "committed", "commitment_resources", "outbound"}))
    return std::unexpected{failure("Invalid closed origin checkpoint")};
  FreedomLocalTravelState value;
  if (!j["selected"].is_null()) {
    const auto target = decode_body_target(j["selected"].dump());
    if (!target) return std::unexpected{failure("Invalid origin target")};
    value.selected = *target;
  }
  if (!j["committed"].is_null()) {
    auto frozen = frozen_value(j["committed"]);
    if (!frozen) return std::unexpected{frozen.error()};
    value.committed = std::move(*frozen);
  }
  if (!j["outbound"].is_null()) {
    auto frozen = frozen_value(j["outbound"]);
    if (!frozen) return std::unexpected{frozen.error()};
    value.outbound = std::move(*frozen);
  }
  if (!j["commitment_resources"].is_null()) {
    const auto bill = travel_resources_value(j["commitment_resources"]);
    if (!bill) return std::unexpected{bill.error()};
    value.commitment_resources = *bill;
  }
  return value;
}
auto origin_routes_json(const FreedomOriginLocalRoutes& routes)
    -> std::expected<Json, SaveSchemaError> {
  auto inactive = local_json(routes.inactive.local);
  auto legacy = local_json(routes.legacy.local);
  if (!inactive) return std::unexpected{inactive.error()};
  if (!legacy) return std::unexpected{legacy.error()};
  Json excursion = nullptr, inactive_excursion = nullptr, prior_moon = nullptr;
  if (routes.excursion_outbound) {
    auto value = frozen_json(*routes.excursion_outbound);
    if (!value) return std::unexpected{value.error()};
    excursion = std::move(*value);
  }
  if (routes.inactive.excursion_outbound) {
    auto value = frozen_json(*routes.inactive.excursion_outbound);
    if (!value) return std::unexpected{value.error()};
    inactive_excursion = std::move(*value);
  }
  if (routes.legacy.prior_moon) {
    auto value = local_json(routes.legacy.prior_moon->local);
    if (!value) return std::unexpected{value.error()};
    prior_moon = std::move(*value);
  }
  return Json{{"version", routes.version},
              {"active", static_cast<unsigned>(routes.active)},
              {"inactive",
               {{"local", std::move(*inactive)},
                {"excursion_outbound", std::move(inactive_excursion)}}},
              {"excursion_outbound", std::move(excursion)},
              {"legacy",
               {{"version", routes.legacy.version},
                {"local", std::move(*legacy)},
                {"prior_moon", std::move(prior_moon)},
                {"next_attempt", routes.legacy.next_attempt}}}};
}
auto origin_routes_value(const Json& j)
    -> std::expected<FreedomOriginLocalRoutes, SaveSchemaError> {
  if (!fields(j, {"version", "active", "inactive", "excursion_outbound",
                  "legacy"}) ||
      !uints(j, {"version", "active"}) ||
      j["version"] != kFreedomOriginLocalRoutesVersion ||
      j["active"] > static_cast<unsigned>(FreedomLocalRouteKind::minor) ||
      !fields(j["inactive"], {"local", "excursion_outbound"}) ||
      !fields(j["legacy"],
              {"version", "local", "prior_moon", "next_attempt"}) ||
      !uints(j["legacy"], {"version", "next_attempt"}) ||
      (j["legacy"]["version"] != kFreedomLocalTravelStateVersion &&
       j["legacy"]["version"] != kFreedomMinorTravelStateVersion))
    return std::unexpected{failure("Invalid closed origin route history")};
  FreedomOriginLocalRoutes routes;
  routes.active =
      static_cast<FreedomLocalRouteKind>(j["active"].get<unsigned>());
  auto inactive = local_value(j["inactive"]["local"]);
  auto legacy = local_value(j["legacy"]["local"]);
  if (!inactive) return std::unexpected{inactive.error()};
  if (!legacy) return std::unexpected{legacy.error()};
  routes.inactive.local = std::move(*inactive);
  routes.legacy.local = std::move(*legacy);
  routes.legacy.version = j["legacy"]["version"].get<std::uint32_t>();
  routes.legacy.next_attempt = j["legacy"]["next_attempt"].get<std::uint64_t>();
  if (!j["excursion_outbound"].is_null()) {
    auto value = frozen_value(j["excursion_outbound"]);
    if (!value) return std::unexpected{value.error()};
    routes.excursion_outbound = std::move(*value);
  }
  if (!j["inactive"]["excursion_outbound"].is_null()) {
    auto value = frozen_value(j["inactive"]["excursion_outbound"]);
    if (!value) return std::unexpected{value.error()};
    routes.inactive.excursion_outbound = std::move(*value);
  }
  if (!j["legacy"]["prior_moon"].is_null()) {
    auto value = local_value(j["legacy"]["prior_moon"]);
    if (!value) return std::unexpected{value.error()};
    routes.legacy.prior_moon = FreedomLocalMoonHistory{std::move(*value)};
  }
  return routes;
}
auto route_policy(FreedomLocalRouteKind kind) -> std::uint32_t {
  return kind == FreedomLocalRouteKind::moon
             ? kFreedomLocalBodyJumpVersion
             : kFreedomMinorLocalBodyJumpVersion;
}
auto empty_receipts(const FreedomLocalTravelState& local) -> bool {
  return !local.committed && !local.commitment_resources && !local.outbound;
}
auto legacy_resources_only(const FreedomTravelSaveDocument& d) -> bool {
  const auto legacy = [](const std::optional<FreedomResources>& value) {
    return !value ||
           (value->version == kFreedomResourceVersion && !value->drive);
  };
  const auto& s = d.travel;
  if (d.voyage.voyage.resources.version != kFreedomResourceVersion ||
      d.voyage.voyage.resources.drive || !legacy(s.commitment_resources) ||
      (s.local && !legacy(s.local->commitment_resources)) ||
      (s.prior_moon && !legacy(s.prior_moon->local.commitment_resources)))
    return false;
  if (s.origin_routes) {
    const auto& r = *s.origin_routes;
    if (!legacy(r.inactive.local.commitment_resources) ||
        !legacy(r.legacy.local.commitment_resources) ||
        (r.legacy.prior_moon &&
         !legacy(r.legacy.prior_moon->local.commitment_resources)))
      return false;
  }
  return true;
}
auto validate_origin_local(const FreedomTravelSaveDocument& d)
    -> std::expected<void, SaveSchemaError> {
  if (const auto valid = validate_freedom_knowledge_document(d.voyage); !valid)
    return valid;
  const auto& s = d.travel;
  if (s.version != kFreedomOriginLocalTravelStateVersion || !s.origin_routes ||
      s.origin_routes->version != kFreedomOriginLocalRoutesVersion ||
      !s.local || s.selected || s.prior_moon || !s.next_attempt ||
      !d.craft_binding ||
      *d.craft_binding != NativeStartingAssemblySelection{} ||
      d.voyage.knowledge.recipe.version !=
          (surface_base_flight(d.voyage.voyage.voyage.base).origin.network
               ? kFreedomNetworkKnowledgeVersion
               : kFreedomTravelKnowledgeVersion))
    return std::unexpected{failure(
        "Origin routes require explicit version, binding and knowledge")};
  const auto& routes = *s.origin_routes;
  if (routes.active != FreedomLocalRouteKind::moon &&
      routes.active != FreedomLocalRouteKind::minor)
    return std::unexpected{failure("Unknown active origin route")};
  const auto& surface = d.voyage.voyage.voyage;
  const auto flight = surface_base_flight(surface.base);
  const auto now = flight.flight.tick;
  const auto current = target_of(flight);
  const auto system =
      generate_physical_origin_system(flight.origin.recipe.universe_seed, 2);
  if (!system || !current || !valid_pilot_binding(d, flight))
    return std::unexpected{
        failure("Origin route owner or seated pilot differs")};
  const auto catalog = generate_secondary_body_catalog(*system);
  if (!catalog)
    return std::unexpected{failure("Origin body catalog unavailable")};
  const BodyTarget home{system->catalog.id,
                        body_id(flight.origin.recipe.home_planet)};
  const auto target = [&](FreedomLocalRouteKind kind) {
    return BodyTarget{
        system->catalog.id,
        catalog->bodies[kind == FreedomLocalRouteKind::moon ? 0 : 1].node.id};
  };
  const auto active_target = target(routes.active);
  const auto inactive_kind = routes.active == FreedomLocalRouteKind::moon
                                 ? FreedomLocalRouteKind::minor
                                 : FreedomLocalRouteKind::moon;
  if ((*current != home && *current != active_target) ||
      (s.local->selected &&
       *s.local->selected != (*current == home ? active_target : home)) ||
      routes.inactive.local.selected)
    return std::unexpected{
        failure("Origin selection requires home or its active companion")};
  // Authenticate only the retained old structured proofs. Never rewind the
  // current voyage/knowledge or infer an omitted historical outbound pose.
  const auto& legacy = routes.legacy;
  if ((legacy.version != kFreedomLocalTravelStateVersion &&
       legacy.version != kFreedomMinorTravelStateVersion) ||
      (legacy.version == kFreedomLocalTravelStateVersion &&
       legacy.prior_moon) ||
      !legacy.next_attempt ||
      legacy.next_attempt == std::numeric_limits<std::uint64_t>::max())
    return std::unexpected{failure("Invalid immutable origin legacy span")};
  auto prior = d;
  prior.travel.version = legacy.version;
  prior.travel.local = legacy.local;
  prior.travel.prior_moon = legacy.prior_moon;
  prior.travel.origin_routes.reset();
  prior.travel.next_attempt = legacy.next_attempt;
  const auto legacy_kind = legacy.version == kFreedomLocalTravelStateVersion
                               ? FreedomLocalRouteKind::moon
                               : FreedomLocalRouteKind::minor;
  const auto& old = legacy.local;
  if (old.selected && *old.selected != target(legacy_kind))
    return std::unexpected{
        failure("Legacy selection is not its home departure")};
  if (!old.outbound) {
    if (!empty_receipts(old) ||
        !prior_history(prior, flight, legacy.next_attempt))
      return std::unexpected{
          failure("Empty legacy span has no authentic prior home history")};
  } else {
    if (!old.committed || !old.commitment_resources)
      return std::unexpected{
          failure("Legacy commitment and bill are unpaired")};
    const auto& first = *old.outbound;
    const auto& last = *old.committed;
    const auto& bill = *old.commitment_resources;
    if (!validate_frozen_local_body_jump_arrival(first) ||
        !validate_frozen_local_body_jump_arrival(last) ||
        first.version != route_policy(legacy_kind) ||
        last.version != first.version ||
        target_of(first.request.source) != home ||
        first.request.destination != target(legacy_kind) ||
        target_of(last.request.source) != target(legacy_kind) ||
        last.request.destination != home ||
        !same_history(first.request.source, flight) ||
        !same_history(last.request.source, flight) ||
        !prior_history(prior, first.request.source, first.request.attempt) ||
        last.request.home_model != first.request.home_model ||
        last.request.attempt < first.request.attempt ||
        (last.request.attempt - first.request.attempt) % 2 != 1 ||
        last.request.attempt == std::numeric_limits<std::uint64_t>::max() ||
        legacy.next_attempt != last.request.attempt + 1 ||
        first.arrival_tick > last.request.source.flight.tick ||
        last.arrival_tick > now ||
        !validate_freedom_resources(bill, last.request.source) ||
        !bill.jump_charges)
      return std::unexpected{failure(
          "Legacy closed route differs from its original proof or bill")};
  }
  SimulationTick legacy_tick{};
  if (s.committed) legacy_tick = s.committed->preview.arrival_tick;
  if (legacy.prior_moon && legacy.prior_moon->local.committed)
    legacy_tick =
        std::max(legacy_tick, legacy.prior_moon->local.committed->arrival_tick);
  if (legacy.local.committed)
    legacy_tick = std::max(legacy_tick, legacy.local.committed->arrival_tick);
  const auto checkpoint =
      [&](const FreedomLocalTravelState& local,
          const std::optional<FrozenLocalBodyJumpArrival>& excursion,
          FreedomLocalRouteKind kind, bool inactive) -> bool {
    if (!local.committed) return empty_receipts(local) && !excursion;
    if (!local.outbound || !local.commitment_resources || !excursion)
      return false;
    const auto& first = *local.outbound;
    const auto& out = *excursion;
    const auto& last = *local.committed;
    const auto& bill = *local.commitment_resources;
    if (!validate_frozen_local_body_jump_arrival(first) ||
        !validate_frozen_local_body_jump_arrival(out) ||
        !validate_frozen_local_body_jump_arrival(last) ||
        first.version != route_policy(kind) || out.version != first.version ||
        last.version != first.version ||
        target_of(first.request.source) != home ||
        first.request.destination != target(kind) ||
        target_of(out.request.source) != home ||
        out.request.destination != target(kind) ||
        !same_history(first.request.source, flight) ||
        !same_history(out.request.source, flight) ||
        !same_history(last.request.source, flight) ||
        first.request.source.flight.tick < legacy_tick ||
        first.request.attempt < legacy.next_attempt ||
        out.request.attempt < first.request.attempt ||
        (first.request.attempt - legacy.next_attempt) % 2 != 0 ||
        (out.request.attempt - legacy.next_attempt) % 2 != 0 ||
        first.request.attempt == std::numeric_limits<std::uint64_t>::max() ||
        out.request.attempt == std::numeric_limits<std::uint64_t>::max() ||
        last.request.attempt == std::numeric_limits<std::uint64_t>::max() ||
        (first.request.attempt == out.request.attempt
             ? first != out
             : first.arrival_tick > out.request.source.flight.tick) ||
        out.request.source.flight.tick > now ||
        last.request.source.flight.tick > now ||
        !validate_freedom_resources(bill, last.request.source) ||
        !bill.jump_charges)
      return false;
    const bool returning = target_of(last.request.source) == target(kind);
    if (returning) {
      if (last.request.destination != home ||
          last.request.attempt != out.request.attempt + 1 ||
          last.request.home_model != out.request.home_model ||
          out.arrival_tick > last.request.source.flight.tick)
        return false;
    } else if (inactive || last != out)
      return false;
    return !inactive || last.arrival_tick <= now;
  };
  if (!checkpoint(*s.local, routes.excursion_outbound, routes.active, false) ||
      !checkpoint(routes.inactive.local, routes.inactive.excursion_outbound,
                  inactive_kind, true))
    return std::unexpected{
        failure("Origin checkpoint first, excursion, owner or bill differs")};
  const FrozenLocalBodyJumpArrival* latest = nullptr;
  const FreedomResources* latest_bill = nullptr;
  for (const auto* local : {&*s.local, &routes.inactive.local}) {
    if (!local->committed) continue;
    const auto& frozen = *local->committed;
    if (latest && latest->request.attempt == frozen.request.attempt)
      return std::unexpected{
          failure("Origin routes duplicate a global commitment attempt")};
    if (!latest || latest->request.attempt < frozen.request.attempt) {
      latest = &frozen;
      latest_bill = &*local->commitment_resources;
    }
  }
  const auto* legacy_terminal =
      legacy.local.committed
          ? &*legacy.local.committed
          : (legacy.prior_moon && legacy.prior_moon->local.committed
                 ? &*legacy.prior_moon->local.committed
                 : nullptr);
  const auto* legacy_bill =
      legacy.local.commitment_resources
          ? &*legacy.local.commitment_resources
          : (legacy.prior_moon && legacy.prior_moon->local.commitment_resources
                 ? &*legacy.prior_moon->local.commitment_resources
                 : nullptr);
  if (legacy_terminal && !legacy_bill)
    return std::unexpected{
        failure("Legacy terminal arrival has no paired bill")};
  if (!latest && legacy_terminal && now == legacy_terminal->arrival_tick &&
      (flight.flight != legacy_terminal->arrival.flight ||
       flight.origin != legacy_terminal->arrival.origin ||
       d.voyage.voyage.resources.flight_quanta != legacy_bill->flight_quanta ||
       d.voyage.voyage.resources.jump_charges != legacy_bill->jump_charges - 1))
    return std::unexpected{
        failure("Origin adoption differs from the exact inherited arrival")};
  if (s.next_attempt !=
      (latest ? latest->request.attempt + 1 : legacy.next_attempt))
    return std::unexpected{
        failure("Origin next attempt differs from its latest commitment")};
  const FrozenLocalBodyJumpArrival* known[] = {
      s.local->outbound ? &*s.local->outbound : nullptr,
      routes.excursion_outbound ? &*routes.excursion_outbound : nullptr,
      s.local->committed ? &*s.local->committed : nullptr,
      routes.inactive.local.outbound ? &*routes.inactive.local.outbound
                                     : nullptr,
      routes.inactive.excursion_outbound ? &*routes.inactive.excursion_outbound
                                         : nullptr,
      routes.inactive.local.committed ? &*routes.inactive.local.committed
                                      : nullptr};
  auto first_attempt = std::numeric_limits<std::uint64_t>::max();
  for (const auto* first :
       {s.local->outbound ? &*s.local->outbound : nullptr,
        routes.inactive.local.outbound ? &*routes.inactive.local.outbound
                                       : nullptr})
    if (first) first_attempt = std::min(first_attempt, first->request.attempt);
  if (latest && first_attempt != legacy.next_attempt)
    return std::unexpected{failure(
        "Origin first new departure is not adjacent to its legacy span")};
  for (std::size_t i = 0; i < std::size(known); ++i)
    for (std::size_t j = i + 1; j < std::size(known); ++j)
      if (known[i] && known[j] &&
          known[i]->request.attempt == known[j]->request.attempt &&
          *known[i] != *known[j])
        return std::unexpected{failure(
            "Origin receipts reuse an attempt for different frozen legs")};
      else if (known[i] && known[j] &&
               known[i]->request.attempt != known[j]->request.attempt) {
        const auto& earlier =
            known[i]->request.attempt < known[j]->request.attempt ? *known[i]
                                                                  : *known[j];
        const auto& later =
            known[i]->request.attempt < known[j]->request.attempt ? *known[j]
                                                                  : *known[i];
        if (earlier.arrival_tick > later.request.source.flight.tick)
          return std::unexpected{
              failure("Origin known receipts reverse the shared clock")};
      }
  if (s.local->committed && routes.inactive.local.committed) {
    const auto& active = *s.local->committed;
    const auto& inactive = *routes.inactive.local.committed;
    const auto& earlier =
        active.request.attempt < inactive.request.attempt ? active : inactive;
    const auto& later =
        active.request.attempt < inactive.request.attempt ? inactive : active;
    const auto& later_out = active.request.attempt < inactive.request.attempt
                                ? *routes.inactive.excursion_outbound
                                : *routes.excursion_outbound;
    if (earlier.arrival_tick > later.request.source.flight.tick ||
        earlier.arrival_tick > later_out.request.source.flight.tick)
      return std::unexpected{
          failure("Origin checkpoints overlap separate excursions")};
  }
  switch (s.phase) {
    case FreedomJumpPhase::idle:
      if (s.spool_tick)
        return std::unexpected{
            failure("Idle origin travel retains a spool clock")};
      break;
    case FreedomJumpPhase::spool:
      if (!s.local->selected || !free_pilot(d) || surface.surface.landed ||
          surface.surface.moon_landed || s.spool_tick > now ||
          now - s.spool_tick >= kJumpSpoolTicks ||
          s.spool_tick > std::numeric_limits<SimulationTick>::max() -
                             kJumpSpoolTicks - kJumpTransitTicks - 3)
        return std::unexpected{
            failure("Invalid origin spool or supported-surface constraint")};
      break;
    case FreedomJumpPhase::transit: {
      if (!s.local->committed || latest != &*s.local->committed ||
          !s.local->selected ||
          !std::holds_alternative<FreedomFlightSaveDocument>(surface.base) ||
          surface.surface.landed || surface.surface.moon_landed)
        return std::unexpected{
            failure("Origin transit requires active frozen free flight")};
      const auto& f = *latest;
      const auto& r = f.request;
      const auto& bill = *latest_bill;
      const auto& ledger = d.voyage.voyage.resources;
      auto source_view = flight;
      source_view.flight.tick = r.source.flight.tick;
      source_view.origin.state.tick = r.source.origin.state.tick;
      if (s.spool_tick >
              std::numeric_limits<SimulationTick>::max() - kJumpSpoolTicks ||
          r.source.flight.tick != s.spool_tick + kJumpSpoolTicks ||
          r.source.flight.tick > now || now >= f.arrival_tick ||
          *s.local->selected != r.destination || source_view != r.source ||
          ledger.flight_quanta != bill.flight_quanta ||
          ledger.jump_charges != bill.jump_charges - 1)
        return std::unexpected{failure(
            "Origin transit source, clock or one-charge debit differs")};
      return {};
    }
    default: return std::unexpected{failure("Unknown origin travel phase")};
  }
  if (*current == home) {
    for (const auto* local : {&*s.local, &routes.inactive.local})
      if (local->committed && local->committed->request.destination != home)
        return std::unexpected{
            failure("Home cannot retain an unfinished companion excursion")};
  } else if (!s.local->committed || latest != &*s.local->committed ||
             latest->request.destination != *current) {
    return std::unexpected{
        failure("Current companion is not the latest active arrival")};
  }
  if (latest &&
      (latest->request.destination != *current || latest->arrival_tick > now ||
       (s.phase == FreedomJumpPhase::spool &&
        latest->arrival_tick > s.spool_tick) ||
       (now == latest->arrival_tick &&
        (flight.flight != latest->arrival.flight ||
         flight.origin != latest->arrival.origin ||
         d.voyage.voyage.resources.flight_quanta !=
             latest_bill->flight_quanta ||
         d.voyage.voyage.resources.jump_charges !=
             latest_bill->jump_charges - 1))))
    return std::unexpected{failure(
        "Origin current arrival owner, instant pose or charge differs")};
  return {};
}
} // namespace

auto travel_local_frozen_json(const FrozenLocalBodyJumpArrival& value)
    -> std::expected<Json, SaveSchemaError> {
  return frozen_json(value);
}
auto travel_local_frozen_value(const Json& value)
    -> std::expected<FrozenLocalBodyJumpArrival, SaveSchemaError> {
  return frozen_value(value);
}
auto travel_valid_pilot_binding(const FreedomTravelSaveDocument& d,
                                const FreedomFlightSaveDocument& flight)
    -> bool {
  return valid_pilot_binding(d, flight);
}
auto travel_free_pilot(const FreedomTravelSaveDocument& d) -> bool {
  return free_pilot(d);
}
auto travel_target_of(const FreedomFlightSaveDocument& d)
    -> std::optional<BodyTarget> {
  return target_of(d);
}
auto validate_local_travel(const FreedomTravelSaveDocument& d)
    -> std::expected<void, SaveSchemaError> {
  if (!legacy_resources_only(d))
    return std::unexpected{
        failure("Drive recovery requires explicit travel57")};
  if (d.travel.composed)
    return std::unexpected{
        failure("Composed routes require explicit version5")};
  if (d.travel.version == kFreedomOriginLocalTravelStateVersion)
    return validate_origin_local(d);
  if (d.travel.origin_routes)
    return std::unexpected{
        failure("Origin route history requires explicit version4")};
  if (const auto v = validate_freedom_knowledge_document(d.voyage); !v)
    return v;
  const auto& s = d.travel;
  const bool minor_route = s.version == kFreedomMinorTravelStateVersion;
  if ((s.version != kFreedomLocalTravelStateVersion && !minor_route) ||
      (!minor_route && s.prior_moon) || !s.local || s.selected ||
      !s.next_attempt || !d.craft_binding ||
      *d.craft_binding != NativeStartingAssemblySelection{} ||
      d.voyage.knowledge.recipe.version !=
          (surface_base_flight(d.voyage.voyage.voyage.base).origin.network
               ? kFreedomNetworkKnowledgeVersion
               : kFreedomTravelKnowledgeVersion))
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
      {"format_version",
       surface_base_flight(d.voyage.voyage.voyage.base).origin.network
           ? kFreedomNetworkTravelSaveFormatVersion
       : s.version == kFreedomOriginLocalTravelStateVersion
           ? kFreedomOriginLocalTravelSaveFormatVersion
       : s.version == kFreedomMinorTravelStateVersion
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
  if (s.version == kFreedomOriginLocalTravelStateVersion) {
    auto routes = origin_routes_json(*s.origin_routes);
    if (!routes) return std::unexpected{routes.error()};
    root["travel"]["origin_routes"] = std::move(*routes);
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
  const bool network_format =
      root["format_version"] == kFreedomNetworkTravelSaveFormatVersion;
  const bool minor_route =
      root["format_version"] == kFreedomMinorTravelSaveFormatVersion ||
      (network_format && s.is_object() && uints(s, {"version"}) &&
       s["version"] == kFreedomMinorTravelStateVersion);
  const bool origin_route =
      root["format_version"] == kFreedomOriginLocalTravelSaveFormatVersion ||
      (network_format && s.is_object() && uints(s, {"version"}) &&
       s["version"] == kFreedomOriginLocalTravelStateVersion);
  const auto version = origin_route
                           ? kFreedomOriginLocalTravelStateVersion
                           : (minor_route ? kFreedomMinorTravelStateVersion
                                          : kFreedomLocalTravelStateVersion);
  auto shape = s;
  if (origin_route) {
    if (!shape.contains("origin_routes"))
      return std::unexpected{failure("Missing origin route history")};
    shape.erase("origin_routes");
  }
  if (minor_route) {
    if (!shape.contains("prior_moon"))
      return std::unexpected{failure("Missing retained Moon field")};
    shape.erase("prior_moon");
  }
  if (!fields(shape,
              {"version", "phase", "selected", "spool_tick", "next_attempt",
               "committed", "commitment_resources", "local"}) ||
      !uints(s, {"version", "phase", "spool_tick", "next_attempt"}) ||
      s["version"] != version || !s["selected"].is_null() ||
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
  if (surface_base_flight(voyage->voyage.voyage.base)
          .origin.network.has_value() != network_format)
    return std::unexpected{
        failure("local travel format/network selection mismatch")};
  FreedomTravelSaveDocument d;
  d.voyage = std::move(*voyage);
  d.craft_binding = NativeStartingAssemblySelection{};
  d.travel.version = version;
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
  if (origin_route) {
    auto routes = origin_routes_value(s["origin_routes"]);
    if (!routes) return std::unexpected{routes.error()};
    d.travel.origin_routes = std::move(*routes);
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
