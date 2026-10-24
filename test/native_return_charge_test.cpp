#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace apsis_drift {
namespace {
unsigned checks{};
auto check(bool good, const char* message) -> void {
  ++checks;
  if (!good) throw std::runtime_error(message);
}
template <class T, class E>
auto need(std::expected<T, E> value,
          std::source_location at = std::source_location::current()) -> T {
  if (!value) {
    auto message = "Return-charge fixture line " + std::to_string(at.line());
    if constexpr (std::is_same_v<E, std::string>)
      message += ": " + value.error();
    else if constexpr (requires { value.error().detail; })
      message += ": " + value.error().detail;
    throw std::runtime_error(message);
  }
  if constexpr (!std::is_void_v<T>) {
    if constexpr (std::is_trivially_copyable_v<T>)
      return *value;
    else
      return std::move(*value);
  }
}
auto bytes(const NativeFreedomFlightSession& session) -> std::string {
  auto document =
      std::make_unique<NativeSaveDocument>(need(session.save_document()));
  return need(encode_native_save_document_json(*document));
}
auto continued(const NativeFreedomFlightSession& session)
    -> std::unique_ptr<NativeFreedomFlightSession> {
  const auto saved = bytes(session);
  auto result = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(need(native_select_save_document(
          need(decode_native_save_document_json(saved)))))));
  check(bytes(*result) == saved, "Complete Save/Continue changed voyage bytes");
  return result;
}
auto view(const NativeFreedomFlightSession& session) -> NativeReturnChargeView {
  const auto saved = bytes(session);
  const auto first = need(session.return_charge_view());
  check(need(session.return_charge_view()) == first && bytes(session) == saved,
        "Repeated return-charge reads changed complete authoritative state");
  check(
      first.version == 1 && first.tick == session.document().flight.tick &&
          std::isfinite(first.flight_fraction) && first.flight_fraction >= 0 &&
          first.flight_fraction <= 1 && !first.note.empty() &&
          first.note.size() <= 1024 &&
          first.note.find('\0') == std::string::npos,
      "Return-charge view has an invalid owner clock, quantity or explanation");
  return first;
}
auto unavailable(const NativeFreedomFlightSession& session) -> void {
  const auto result = view(session);
  check(!result.available && result.leg_count == 0 &&
            result.required_charges == 0 && result.charge_shortfall == 0 &&
            result.legs == std::array<NativeReturnChargeLeg, 2>{},
        "Unavailable route leaked endpoint identities or a charge forecast");
}
auto admitted_name(const NativeFreedomFlightSession& session, SystemId system)
    -> std::string {
  const KnowledgeSubject subject{KnowledgeSubjectKind::system, system,
                                 system.value};
  for (const auto fact :
       {KnowledgeFact::identity_type, KnowledgeFact::location}) {
    const auto reading = need(query_freedom_knowledge(
        *session.knowledge(), subject, fact, session.document().flight.tick));
    check(reading &&
              reading->provenance.level >= NavigationKnowledgeLevel::resolved,
          "Named test route lacks independently admitted identity/location");
  }
  const auto identity = need(query_freedom_knowledge(
      *session.knowledge(), subject, KnowledgeFact::identity_type,
      session.document().flight.tick));
  return std::get<KnownIdentity>(identity->value).name;
}
auto route(const NativeFreedomFlightSession& session, SystemId destination,
           SystemId origin) -> void {
  const auto result = view(session);
  const auto source = session.system().catalog.id;
  const std::uint32_t count = destination == origin ? 1 : 2;
  check(result.available && result.leg_count == count &&
            result.required_charges == count &&
            result.jump_charges == session.resources()->jump_charges &&
            result.charge_shortfall ==
                (count > result.jump_charges ? count - result.jump_charges : 0),
        "Actual selected route has the wrong one/two-leg charge budget");
  check(result.legs[0] ==
            NativeReturnChargeLeg{source, destination,
                                  admitted_name(session, source),
                                  admitted_name(session, destination)},
        "First leg invented an endpoint or a generated-but-unadmitted name");
  if (count == 2)
    check(result.legs[1] ==
              NativeReturnChargeLeg{destination, origin,
                                    admitted_name(session, destination),
                                    admitted_name(session, origin)},
          "Return leg is not the admitted destination-to-Origin route");
  else
    check(result.legs[1] == NativeReturnChargeLeg{},
          "One-leg Origin return retained an unused endpoint");
}
auto board(NativeFreedomFlightSession& session) -> void {
  for (unsigned tick = 0; tick < 1194; ++tick)
    need(session.advance_walk({0, -1, 0}));
  need(session.begin_boarding());
  for (SimulationTick tick = 0; tick < kGameplayBoardingTicks; ++tick)
    need(session.advance_walk({}));
  check(session.boarding() && !session.walker() && session.docking()->attached,
        "Actual station walking/boarding did not reach the seated craft");
}
auto open_travel(FreedomTravelSaveDocument document)
    -> std::unique_ptr<NativeFreedomFlightSession> {
  return std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_select_save_document(std::move(document))))));
}
auto ledger_boundaries(const NativeFreedomFlightSession& source,
                       SystemId destination, SystemId origin) -> void {
  // Explicit resource-boundary fixtures; all body, clock, chart, route and
  // pilot history comes from the actual released voyage. No resource
  // acquisition or flown endurance is claimed by these initialized ledger
  // quantities.
  const auto original = bytes(source);
  for (std::uint32_t charges = 0; charges <= kFreedomJumpCapacity; ++charges) {
    auto document = std::make_unique<FreedomTravelSaveDocument>(
        need(source.travel_document()));
    document->voyage.voyage.resources.jump_charges = charges;
    auto selected = open_travel(std::move(*document));
    route(*selected, destination, origin);
    check(need(continued(*selected)->return_charge_view()) == view(*selected),
          "Continue changed an initialized charge-boundary reading");
  }
  constexpr std::array quantities{
      std::uint64_t{0}, std::uint64_t{1}, kFreedomFlightReserveQuanta,
      kFreedomFlightReserveQuanta + 1, kFreedomFlightCapacityQuanta};
  for (const auto quantity : quantities) {
    auto document = std::make_unique<FreedomTravelSaveDocument>(
        need(source.travel_document()));
    document->voyage.voyage.resources.flight_quanta = quantity;
    auto selected = open_travel(std::move(*document));
    const auto result = view(*selected);
    check(result.available &&
              result.flight_fraction ==
                  static_cast<double>(quantity) /
                      static_cast<double>(kFreedomFlightCapacityQuanta) &&
              result.reserve == (quantity <= kFreedomFlightReserveQuanta) &&
              result.operationally_empty == (quantity < 2),
          "Charge budgeting silently added a fuel threshold or altered reserve "
          "flags");
  }
  check(bytes(source) == original, "Ledger fixtures mutated their real source");
}
auto rejected_admission(const NativeFreedomFlightSession& source) -> void {
  const auto original = bytes(source);
  for (unsigned fault = 0; fault < 6; ++fault) {
    auto document = std::make_unique<FreedomTravelSaveDocument>(
        need(source.travel_document()));
    auto& resource = document->voyage.voyage.resources;
    auto& knowledge = document->voyage.knowledge;
    switch (fault) {
      case 0: resource.jump_charges = kFreedomJumpCapacity + 1; break;
      case 1: resource.flight_quanta = kFreedomFlightCapacityQuanta + 1; break;
      case 2: ++resource.tick; break;
      case 3: ++resource.craft.value; break;
      case 4: ++knowledge.recipe.universe_seed.value; break;
      default:
        knowledge.entries.front().transitions.front().tick =
            source.document().flight.tick + 1;
        break;
    }
    check(!native_select_save_document(std::move(*document)),
          "Malformed owner, clock, future fact or resource bounds passed "
          "public admission");
  }
  check(bytes(source) == original,
        "Rejected public admission altered live voyage");
}
auto known_route() -> void {
  auto live = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_planetary_network_new_game({42})))));
  const auto network = need(generate_freedom_system_network({42}));
  const auto origin = network.nodes[0].system;
  const auto neighbor = network.nodes[1].system;
  unavailable(*live);
  need(live->select_jump(neighbor));
  check(live->walker() && live->docking()->attached && !live->jump_available(),
        "Station budget fixture unexpectedly qualified actual departure");
  route(*live, neighbor, origin);
  board(*live);
  check(!live->jump_available(),
        "Docked ship unexpectedly qualified departure");
  route(*live, neighbor, origin);
  need(live->release_port());
  ledger_boundaries(*live, neighbor, origin);
  rejected_admission(*live);
  // An explicit ordinary-planet selection is not an intersystem return budget.
  auto planet_route = continued(*live);
  const auto other = std::ranges::find_if(
      live->system().catalog.planets, [&](const auto& planet) {
        return planet.descriptor.id !=
               live->document().origin.recipe.home_planet;
      });
  check(other != live->system().catalog.planets.end(),
        "Selected seed has no second ordinary-planet route");
  need(planet_route->select_planet_jump(
      {origin, body_id(other->descriptor.id)}));
  unavailable(*planet_route);
  for (unsigned body = 0; body < 2; ++body) {
    auto local = continued(*live);
    const auto companions =
        need(generate_secondary_body_catalog(local->system()));
    need(local->select_local_jump({origin, companions.bodies[body].node.id}));
    unavailable(*local);
  }
  need(live->begin_jump());
  unavailable(*live);
  auto replay = continued(*live);
  const auto before = live->resources()->jump_charges;
  for (SimulationTick tick = 1; tick <= kJumpSpoolTicks + kJumpTransitTicks;
       ++tick) {
    const auto step = need(live->advance({}));
    need(replay->advance({}));
    if (tick == kJumpSpoolTicks) {
      check(step.jump_committed &&
                live->resources()->jump_charges == before - 1,
            "Actual jump did not debit exactly one charge at commitment");
      unavailable(*live);
    }
    if (tick == kJumpSpoolTicks + kJumpTransitTicks)
      check(step.jump_arrived,
            "Actual route did not arrive on its existing schedule");
  }
  check(bytes(*live) == bytes(*replay),
        "Return-charge reads changed frozen outcome/resource replay");
  unavailable(*live); // Selected destination is now the current system.
  need(live->select_jump(origin));
  route(*live, origin, origin);
  need(live->advance({})); // Leave the exact frozen-arrival resource boundary.
  ledger_boundaries(*live, origin, origin);
  need(live->select_jump(network.nodes[2].system));
  route(*live, network.nodes[2].system, origin);
  auto loss = continued(*live);
  const auto digest = need(
      rigid_body_state_checksum({loss->system()}, loss->document().flight));
  need(loss->record_loss(FreedomLossCause::recoverable_destruction,
                         loss->document().origin.state.craft,
                         loss->document().flight.tick, digest));
  check(loss->recovery_pending(),
        "Actual recorded loss did not freeze pending voyage");
  unavailable(*loss);
}
auto discoverable_route() -> void {
  auto live = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_uncharted_network_new_game({42})))));
  const auto network = need(generate_freedom_system_network(
      {42}, kFreedomUnchartedSystemNetworkVersion));
  const auto original = bytes(*live);
  check(!live->select_jump(network.nodes[4].system) && bytes(*live) == original,
        "Unobserved hidden endpoint was selectable or changed the voyage");
  unavailable(*live);
  board(*live);
  need(live->release_port());
  need(live->begin_star_search());
  for (SimulationTick tick = 0; tick < kFreedomStarSearchTicks; ++tick)
    need(live->advance({}));
  const auto target = live->knowledge()->star_search->completed.back().target;
  need(live->select_jump(target));
  route(*live, target, network.nodes[0].system);
  // Stellar evidence is immutable: a valid receipt cannot have either earned
  // identity or location silently demoted/deleted to fabricate eligibility.
  for (const auto fact :
       {KnowledgeFact::identity_type, KnowledgeFact::location}) {
    auto document = std::make_unique<FreedomTravelSaveDocument>(
        need(live->travel_document()));
    auto& entries = document->voyage.knowledge.entries;
    const auto entry = std::ranges::find_if(entries, [&](const auto& value) {
      return value.subject.kind == KnowledgeSubjectKind::system &&
             value.subject.system == target && value.fact == fact;
    });
    check(entry != entries.end(),
          "Actual stellar acquisition omitted a required fact");
    entry->transitions.front().level = NavigationKnowledgeLevel::contact;
    check(
        !native_select_save_document(std::move(*document)),
        "Historical identity/location downgrade passed public save admission");
  }
  check(need(continued(*live)->return_charge_view()) == view(*live),
        "Continue changed actual acquired endpoint budget");
}
auto unsupported() -> void {
  auto legacy = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(need(native_new_game({42})))));
  unavailable(*legacy);
  // Bare flight imports carry no resource/chart ledgers; no quantities or
  // destinations may be inferred from the world's generated truth.
  auto bare = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_select_save_document(legacy->document())))));
  check(!bare->resources() && !bare->knowledge(),
        "Bare flight import manufactured ledgers");
  unavailable(*bare);
}
} // namespace
} // namespace apsis_drift

auto main() -> int {
  using namespace apsis_drift;
  try {
    unsupported();
    known_route();
    discoverable_route();
    std::cout << checks << " return-charge checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << checks << " checks completed: " << error.what() << '\n';
    return 1;
  }
}
