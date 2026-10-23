#include "apsis_drift/native_flight_session.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <type_traits>

namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;
unsigned checks{};
auto check(bool ok, const char* message) -> void {
  ++checks;
  if (!ok) throw std::runtime_error(message);
}
template <class T, class E>
auto need(std::expected<T, E> result,
          std::source_location where = std::source_location::current()) -> T {
  if (!result) {
    auto message =
        "Charted-arrivals scenario line " + std::to_string(where.line());
    if constexpr (std::is_same_v<E, std::string>)
      message += ": " + result.error();
    else if constexpr (requires { result.error().detail; })
      message += ": " + result.error().detail;
    throw std::runtime_error(message);
  }
  if constexpr (!std::is_void_v<T>) return std::move(*result);
}
auto bytes(const NativeFreedomFlightSession& session) -> std::string {
  return need(encode_native_save_document_json(need(session.save_document())));
}
auto reopen(const NativeFreedomFlightSession& source)
    -> NativeFreedomFlightSession {
  const auto saved = bytes(source);
  auto session =
      need(NativeFreedomFlightSession::open(need(native_select_save_document(
          need(decode_native_save_document_json(saved))))));
  check(bytes(session) == saved,
        "Charted-arrivals whole Save/Continue changed bytes");
  return session;
}
auto phase(const NativeFreedomFlightSession& session,
           const std::optional<std::filesystem::path>& output,
           std::string_view name, Json& phases) -> void {
  (void)reopen(session);
  const auto before = bytes(session);
  const auto view = need(session.charted_arrival_view());
  const auto preview = session.jump_preview();
  check(bytes(session) == before,
        "Charted reader/preview mutated whole voyage");
  if (output) need(session.save_as(*output / (std::string{name} + ".json")));
  phases.push_back(
      {{"file", std::string{name} + ".json"},
       {"tick", std::to_string(session.document().flight.tick)},
       {"enabled", view.enabled},
       {"eligible", view.eligible},
       {"request_version", preview ? preview->request.version : 0U},
       {"retained_request_version",
        session.travel() && session.travel()->committed
            ? session.travel()->committed->preview.request.version
            : 0U},
       {"note", view.note}});
}
auto set_preference(NativeFreedomFlightSession& session, bool enabled) -> void {
  const auto document = session.document();
  const auto resources = session.resources();
  const auto travel = session.travel();
  const auto before = *session.knowledge();
  need(session.set_charted_arrivals(enabled));
  auto after = *session.knowledge();
  after.charted_arrivals = before.charted_arrivals;
  check(session.document() == document && session.resources() == resources &&
            session.travel() == travel && after == before &&
            need(session.charted_arrival_view()).enabled == enabled,
        "Personal preference changed clock, physics, resources, facts or "
        "retained receipts");
  const auto selected = bytes(session);
  need(session.set_charted_arrivals(enabled));
  check(bytes(session) == selected,
        "Idempotent preference changed complete Save");
}
auto board(NativeFreedomFlightSession& session) -> void {
  for (unsigned tick = 0; tick < 1194; ++tick)
    need(session.advance_walk({0, -1, 0}));
  need(session.begin_boarding());
  for (SimulationTick tick = 0; tick < kGameplayBoardingTicks; ++tick)
    need(session.advance_walk({}));
  check(!session.walker() && session.boarding() &&
            session.boarding()->phase == FreedomBoardingPhase::seated &&
            session.docking() && session.docking()->attached,
        "Actual factory station walk and boarding did not seat the pilot");
}
auto known_route(const std::optional<std::filesystem::path>& output,
                 Json& phases) -> void {
  auto session = need(NativeFreedomFlightSession::open(
      need(native_planetary_network_new_game({42}))));
  const auto network = need(generate_freedom_system_network({42}));
  check(session.knowledge()->recipe.version ==
                kFreedomPlanetaryKnowledgeVersion &&
            !session.knowledge()->charted_arrivals && session.walker() &&
            session.docking()->attached,
        "Actual planetary New Game did not preserve absent default selection");
  phase(session, output, "station-off", phases);
  set_preference(session, true);
  phase(session, output, "station-on", phases);
  set_preference(session, false);
  board(session);
  need(session.select_jump(network.nodes[1].system));
  check(need(session.jump_preview()).request.version ==
            kFreedomNetworkJumpTargetingVersion,
        "Absent selection changed the old known outbound policy");
  phase(session, output, "seated-off", phases);
  set_preference(session, true);
  check(need(session.charted_arrival_view()).eligible &&
            need(session.jump_preview()).request.version ==
                kFreedomChartedJumpTargetingVersion,
        "Known selected primary did not select request4");
  phase(session, output, "seated-on", phases);
  need(session.release_port());
  check(session.jump_available().has_value(),
        "Actual station release did not provide a qualified space jump source");
  phase(session, output, "released", phases);
  const auto charges = session.resources()->jump_charges;
  need(session.begin_jump());
  const auto spooling = bytes(session);
  check(!session.set_charted_arrivals(false) && bytes(session) == spooling,
        "Active-spool preference mutation changed whole Save");
  phase(session, output, "outbound-spool", phases);
  auto continued = reopen(session);
  for (SimulationTick tick = 1; tick <= kJumpSpoolTicks + kJumpTransitTicks;
       ++tick) {
    const auto step = need(session.advance({}));
    need(continued.advance({}));
    if (tick < kJumpSpoolTicks)
      check(session.resources()->jump_charges == charges,
            "Uncommitted spool spent a jump charge");
    if (tick == kJumpSpoolTicks) {
      check(step.jump_committed &&
                session.resources()->jump_charges == charges - 1 &&
                session.travel()->committed &&
                session.travel()->committed->preview.request.version ==
                    kFreedomChartedJumpTargetingVersion,
            "Actual commitment did not retain request4 and exactly one charge "
            "debit");
      phase(session, output, "outbound-committed", phases);
      continued = reopen(continued);
    }
    if (tick == kJumpSpoolTicks + kJumpTransitTicks)
      check(step.jump_arrived,
            "Actual charted transit did not arrive on existing schedule");
  }
  check(
      bytes(session) == bytes(continued) &&
          session.system().catalog.id == network.nodes[1].system &&
          session.resources()->jump_charges == charges - 1 &&
          session.travel()->committed &&
          session.document().flight.orientation ==
              session.travel()->committed->preview.request.source.orientation &&
          session.document().flight.angular_velocity_radians_per_second ==
              session.travel()
                  ->committed->preview.request.source
                  .angular_velocity_radians_per_second,
      "Actual charted arrival/Continue lost body motion, world or one-charge "
      "bill");
  phase(session, output, "outbound-arrived", phases);
  const auto receipt = *session.travel()->committed;
  need(session.select_jump(network.nodes[0].system));
  check(need(session.charted_arrival_view()).enabled &&
            !need(session.charted_arrival_view()).eligible &&
            !need(session.charted_arrival_view()).note.empty() &&
            need(session.jump_preview()).request.version ==
                kFreedomNetworkJumpTargetingVersion,
        "Origin selection did not truthfully retain old station placement");
  phase(session, output, "origin-selected", phases);
  set_preference(session, false);
  check(*session.travel()->committed == receipt &&
            !session.knowledge()->charted_arrivals,
        "Disabling preference reinterpreted authentic request4 history");
  phase(session, output, "preference-off-history", phases);
  set_preference(session, true);
  check(*session.travel()->committed == receipt,
        "Re-enabling preference changed retained frozen request4");
  phase(session, output, "preference-on-history", phases);
}
auto unknown_route(const std::optional<std::filesystem::path>& output,
                   Json& phases) -> void {
  auto session = need(NativeFreedomFlightSession::open(
      need(native_uncharted_network_new_game({42}))));
  check(session.knowledge()->recipe.version ==
                kFreedomUnchartedKnowledgeVersion &&
            session.walker(),
        "Discoverable New Game did not begin on the actual station");
  board(session);
  need(session.release_port());
  set_preference(session, true);
  check(session.star_search_view().can_begin,
        "Actual released discoverable starter lacks stellar acquisition "
        "prerequisite");
  phase(session, output, "unknown-before-search", phases);
  const auto resources = *session.resources();
  need(session.begin_star_search());
  for (SimulationTick tick = 1; tick <= kFreedomStarSearchTicks; ++tick) {
    need(session.advance({}));
    if (tick == kFreedomStarSearchTicks - 1) {
      check(session.star_search_view().acquired_ticks ==
                    kFreedomStarSearchTicks - 1 &&
                session.knowledge()->star_search->completed.empty(),
            "1199 real stellar acquisition ticks falsely completed a search");
      phase(session, output, "unknown-search-near", phases);
      session = reopen(session);
    }
  }
  check(!session.star_search_view().active &&
            session.knowledge()->star_search->completed.size() == 1 &&
            session.resources()->jump_charges == resources.jump_charges,
        "Actual1200tick search failed to earn one system without jump debits");
  const auto target = session.knowledge()->star_search->completed.back().target;
  const auto network = need(generate_freedom_system_network(
      {42}, kFreedomUnchartedSystemNetworkVersion));
  const auto destination =
      need(resolve_freedom_network_system(network, target));
  const auto primary = destination.catalog.planets.front().descriptor.id;
  check(
      !need(query_freedom_knowledge(
          *session.knowledge(),
          {KnowledgeSubjectKind::planet, target, primary.value},
          KnowledgeFact::identity_type, session.document().flight.tick)) &&
          !need(query_freedom_knowledge(
              *session.knowledge(),
              {KnowledgeSubjectKind::planet, target, primary.value},
              KnowledgeFact::location, session.document().flight.tick)),
      "Stellar acquisition fabricated remote primary identity/location facts");
  need(session.select_jump(target));
  const auto view = need(session.charted_arrival_view());
  const auto preview = need(session.jump_preview());
  check(view.supported && view.enabled && !view.eligible &&
            !view.note.empty() &&
            preview.request.version == kFreedomNetworkJumpTargetingVersion &&
            !preview.request.charted_arrival,
        "Earned system with unknown primary failed truthful oldwide fallback");
  phase(session, output, "unknown-primary-selected", phases);
}
} // namespace
auto run_native_charted_arrivals_scenario(
    const std::optional<std::filesystem::path>& root) -> void {
  const auto output =
      root ? std::optional{*root / "charted-arrivals"} : std::nullopt;
  if (output) std::filesystem::create_directories(*output);
  auto legacy =
      need(NativeFreedomFlightSession::open(need(native_new_game({42}))));
  const auto original = bytes(legacy);
  check(!need(legacy.charted_arrival_view()).supported &&
            !legacy.set_charted_arrivals(true) &&
            !legacy.set_charted_arrivals(false) && bytes(legacy) == original,
        "Unsupported old chart accepted preference or changed startup bytes");
  Json phases = Json::array();
  known_route(output, phases);
  unknown_route(output, phases);
  if (output) {
    std::ofstream manifest{*output / "manifest.json"};
    manifest << Json{{"version", 1},
                     {"completed", true},
                     {"failures", 0},
                     {"checks", checks},
                     {"scope",
                      "Actual seed42 planetary/discoverable New Games, station "
                      "walking/boarding/release, "
                      "one charged known request4 spool/transit/arrival, "
                      "original Origin request2 preview, "
                      "personal preference/retained-history Save/Continue, and "
                      "actual stellar acquisition "
                      "with unknown-primary wide fallback. No pose, velocity, "
                      "time, resource, finance or "
                      "knowledge initialization. No flown survey, return, "
                      "landing or port-capture claim."},
                     {"walking_ticks_per_start", 1194},
                     {"boarding_ticks_per_start", kGameplayBoardingTicks},
                     {"phases", phases}}
                    .dump(2)
             << '\n';
    if (!manifest)
      throw std::runtime_error("Unable to write charted arrivals manifest");
  }
  std::cout << "Charted arrivals scenario: " << checks
            << " checks, 0 failures\n";
}
} // namespace apsis_drift
