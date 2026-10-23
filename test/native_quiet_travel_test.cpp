#include "apsis_drift/godot/saved_flight.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "moon_contact_fixture.hpp"
#include "native_minor_route_fixture.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace apsis_drift;
using World = godot_spike::SavedFlightWorld;
using V = RigidVector3;
static_assert(std::is_aggregate_v<World>);
unsigned checks{};
constexpr std::array<double, 12> neutral{};
auto check(bool ok, const char* message) -> void {
  ++checks;
  if (!ok) throw std::runtime_error(message);
}
template <class T, class E>
auto need(std::expected<T, E> value,
          std::source_location at = std::source_location::current()) -> T {
  if (!value) {
    auto message = "Quiet fixture line " + std::to_string(at.line());
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
enum class Initial {
  orbit,
  low_air,
  high_air,
  falling_air,
  near_station,
  far_station,
  closing_station
};
// Explicit numerical initial conditions, not a flown station/ascent voyage.
// Large save/session scratch is heap owned; the normal8MiB stack is unchanged.
auto document(Initial initial = Initial::orbit, bool assistance = true,
              SimulationTick tick = 1200, bool system_frame = false)
    -> std::unique_ptr<FreedomTravelSaveDocument> {
  const Seed seed{42};
  auto owner = std::make_unique<PhysicalLocalSystem>(
      need(generate_physical_origin_system(seed, 2)));
  const auto& planet = owner->catalog.planets.front().descriptor;
  auto flight = std::make_unique<FreedomFlightSaveDocument>();
  flight->origin = make_freedom_new_game_document(seed);
  flight->origin.state.tick = tick;
  flight->flight.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  flight->flight.frame = {RigidFrameKind::planet_relative_inertial,
                          owner->catalog.id,
                          planet.id,
                          {}};
  flight->flight.tick = tick;
  flight->flight.position_metres = {planet.radius.value * 1500.0, 0, 0};
  flight->model.physical_catalog = 2;
  flight->model.physical_ephemeris = 2;
  flight->model.assistance = assistance;
  flight->model.optics = 1;
  flight->model.companions = 1;
  flight->world = FreedomActiveWorldSelection{1, owner->catalog.id, planet.id};
  const auto gravity =
      need(evaluate_central_body_gravity({*owner}, flight->flight));
  flight->flight.linear_velocity_metres_per_second = {
      0, 0,
      std::sqrt(
          gravity.gravitational_parameter_metres_cubed_per_second_squared /
          gravity.distance_metres)};
  flight->flight.orientation =
      need(normalize_rigid_orientation({.8, .1, .2, .3}));
  flight->flight.angular_velocity_radians_per_second = {.01, -.02, .03};
  if (initial == Initial::low_air || initial == Initial::high_air ||
      initial == Initial::falling_air) {
    const auto air = need(resolve_atmospheric_flight_profile(planet));
    const auto margin = initial == Initial::low_air ? -1.0 : .1;
    flight->flight.position_metres = {planet.radius.value * 1000.0 +
                                          air.space_boundary_altitude_metres +
                                          100000.0 + margin,
                                      0, 0};
    flight->flight.linear_velocity_metres_per_second = {
        initial == Initial::falling_air ? -120.0 : 0, 0, 0};
  } else if (initial != Initial::orbit) {
    const auto station = generate_origin_station(seed);
    const auto p =
        need(resolve_origin_station_ephemeris(*owner, station, {tick, 0}));
    const double gap = initial == Initial::near_station ? 9999.0 : 10000.1;
    flight->flight.position_metres = {p.host_relative_position.x + gap,
                                      p.host_relative_position.y,
                                      p.host_relative_position.z};
    flight->flight.linear_velocity_metres_per_second = {
        p.host_relative_velocity.x +
            (initial == Initial::closing_station ? -120.0 : 0),
        p.host_relative_velocity.y, p.host_relative_velocity.z};
  }
  flight->flight =
      need(canonicalize_rigid_body_state({*owner}, flight->flight));
  if (system_frame) {
    flight->model.frame_policy = kFreedomReleaseSystemFramePolicy;
    flight->flight = need(reframe_rigid_body(
        {*owner}, flight->flight,
        {{RigidFrameKind::system_inertial, owner->catalog.id, {}, {}}, tick}));
  }
  FreedomResources resources{1, flight->origin.state.craft,
                             flight->flight.craft, tick};
  auto mapped = std::make_unique<FreedomKnowledgeSaveDocument>(
      FreedomKnowledgeSaveDocument{
          {{std::move(*flight), {}}, resources},
          need(make_freedom_starting_knowledge(seed, 3))});
  return std::make_unique<FreedomTravelSaveDocument>(
      std::move(*mapped), FreedomTravelState{},
      std::optional<NativeStartingAssemblySelection>{
          NativeStartingAssemblySelection{}},
      std::optional<FreedomBoardingState>{});
}
auto open(const FreedomTravelSaveDocument& value)
    -> std::unique_ptr<NativeFreedomFlightSession> {
  return std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_select_save_document(value)))));
}
auto fixture(Initial initial = Initial::orbit, bool assistance = true,
             SimulationTick tick = 1200, bool system_frame = false)
    -> std::unique_ptr<NativeFreedomFlightSession> {
  auto value = document(initial, assistance, tick, system_frame);
  return open(*value);
}
auto world(const NativeFreedomFlightSession& session)
    -> std::unique_ptr<World> {
  NativeFlightStep sentinel;
  sentinel.fuel_debit_quanta = 77;
  return std::make_unique<World>(session, FixedStepClock{},
                                 std::optional<NativeFlightStep>{sentinel},
                                 .25);
}
template <class Action> auto refused(World& selected, Action action) -> void {
  const auto before = bytes(selected.session);
  const auto clock = selected.clock.accumulator();
  const auto dropped = selected.dropped_seconds;
  const auto multiplier = selected.quiet_travel_view().multiplier;
  const auto debit = selected.last_step->fuel_debit_quanta;
  bool rejected{};
  try {
    action();
  } catch (const std::exception&) {
    rejected = true;
  }
  check(rejected && bytes(selected.session) == before &&
            selected.clock.accumulator() == clock &&
            selected.dropped_seconds == dropped &&
            selected.quiet_travel_view().multiplier == multiplier &&
            selected.last_step->fuel_debit_quanta == debit,
        "Refused batch changed session, clock, last actuation or pacing");
}
auto malformed() -> void {
  auto session = fixture();
  auto selected = world(*session);
  need(selected->set_quiet_travel_multiplier(4));
  for (auto bad : {0U, 3U, 5U, std::numeric_limits<std::uint32_t>::max()}) {
    const auto before = bytes(selected->session);
    check(!selected->set_quiet_travel_multiplier(bad) &&
              selected->quiet_travel_view().multiplier == 4 &&
              bytes(selected->session) == before,
          "Invalid requested multiplier changed state");
  }
  for (double elapsed :
       {-1.0, std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), std::nextafter(60.0, 61.0)})
    for (bool paused : {false, true})
      refused(*selected, [&] { selected->advance(elapsed, neutral, paused); });
  const std::array<double, 13> large{};
  for (std::size_t size : {0U, 11U, 13U})
    refused(*selected, [&] {
      selected->advance(kSimulationStep.count(), std::span{large}.first(size),
                        false);
    });
  for (double value : {-0.01, 1.01, std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity()}) {
    auto bad = neutral;
    bad.back() = value;
    refused(*selected,
            [&] { selected->advance(kSimulationStep.count(), bad, false); });
  }
  const auto before = bytes(selected->session);
  for (unsigned i = 0; i < 3; ++i) {
    const auto view = selected->quiet_travel_view();
    check(view.available && view.multiplier == 4 && !view.note.empty() &&
              view.note.size() <= 1024 &&
              view.note.find('\0') == std::string::npos,
          "Read packet lost bounded actual facts");
    selected->advance(60, neutral, true);
  }
  check(bytes(selected->session) == before &&
            selected->quiet_travel_view().multiplier == 4 &&
            selected->clock.accumulator() == SimulationSeconds::zero(),
        "Paused polling consumed selected rate or clock");
  selected->reset_quiet_travel();
  check(selected->quiet_travel_view().multiplier == 1 &&
            bytes(selected->session) == before,
        "Reset changed the voyage instead of the transient request");
}
auto cadence() -> void {
  for (bool system_frame : {false, true}) {
    auto source = fixture(Initial::orbit, true, 1200, system_frame);
    need(source->begin_orbital_survey());
    if (!system_frame) need(source->hold_current_orbit());
    for (std::uint32_t multiplier : {1U, 2U, 4U}) {
      for (int ordinary : {1, 2, 4, 8, 15}) {
        auto selected = world(*source);
        auto peer = std::make_unique<NativeFreedomFlightSession>(*source);
        const auto before = bytes(*source);
        need(selected->set_quiet_travel_multiplier(multiplier));
        check(bytes(selected->session) == before,
              "Choosing a pace entered deterministic Save data");
        selected->advance(kSimulationStep.count() * ordinary, neutral, false);
        const auto expected =
            std::min(kMaxCatchUpSteps, ordinary * static_cast<int>(multiplier));
        NativeFlightStep last;
        for (int tick = 0; tick < expected; ++tick)
          last = need(peer->advance({}));
        check(bytes(selected->session) == bytes(*peer) &&
                  selected->last_step->fuel_debit_quanta ==
                      last.fuel_debit_quanta &&
                  selected->session.document().flight.tick ==
                      source->document().flight.tick + expected &&
                  selected->session.knowledge()
                          ->orbital_surveys->active->acquired_ticks ==
                      static_cast<std::uint32_t>(expected),
              "Paced ordinary120Hz ticks differ from independent "
              "fuel/survey/physics owner");
        check(selected->session.resources()->flight_quanta <
                  source->resources()->flight_quanta,
              "Neutral assistance was forbidden or its real actuation escaped "
              "fuel cost");
      }
    }
  }
  auto source = fixture();
  auto selected = world(*source);
  need(selected->set_quiet_travel_multiplier(4));
  selected->advance(60, neutral, false);
  check(selected->session.document().flight.tick ==
                source->document().flight.tick + 15 &&
            selected->dropped_seconds ==
                .25 + (SimulationSeconds{60} - kMaxCatchUp).count(),
        "Low frame rate failed to reserve fifteen ordinary ticks or changed "
        "ordinary drop policy");
  const auto before = bytes(selected->session);
  selected->advance(0, neutral, false);
  selected->advance(kSimulationStep.count() / 2, neutral, false);
  check(bytes(selected->session) == before,
        "Unexecuted bonus work became debt or fractional work became a tick");
  selected->advance(kSimulationStep.count() / 2, neutral, false);
  check(selected->session.document().flight.tick ==
            source->document().flight.tick + 19,
        "One real accumulated tick did not provide only its boundedfour "
        "opportunities");
  auto continued = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(need(native_select_save_document(
          need(decode_native_save_document_json(bytes(selected->session))))))));
  auto reopened = world(*continued);
  check(reopened->quiet_travel_view().multiplier == 1 &&
            bytes(reopened->session) == bytes(selected->session),
        "Continue retained a hidden pace or changed the voyage");
}
auto pilot_and_boundary() -> void {
  auto source = fixture();
  for (std::size_t channel = 0; channel < neutral.size(); ++channel) {
    auto selected = world(*source);
    auto peer = std::make_unique<NativeFreedomFlightSession>(*source);
    need(selected->set_quiet_travel_multiplier(4));
    auto input = neutral;
    input[channel] = .1;
    selected->advance(kSimulationStep.count(), input, false);
    const NativeFlightControls control{{input[0], input[1], input[2]},
                                       {input[3], input[4], input[5]},
                                       {input[6], input[7], input[8]},
                                       {input[9], input[10], input[11]}};
    need(peer->advance(control));
    check(selected->quiet_travel_view().multiplier == 1 &&
              bytes(selected->session) == bytes(*peer),
          "A pilot actuator failed to cancel before ordinary work");
  }
  auto opposing = world(*source);
  need(opposing->set_quiet_travel_multiplier(4));
  auto input = neutral;
  input[0] = input[3] = .2;
  opposing->advance(kSimulationStep.count(), input, false);
  check(opposing->quiet_travel_view().multiplier == 1 &&
            opposing->session.document().flight.tick ==
                source->document().flight.tick + 1,
        "Opposing nonzero pilot fractions accelerated despite zero net demand");
  for (auto initial : {Initial::low_air, Initial::near_station}) {
    auto constrained = fixture(initial, false);
    auto selected = world(*constrained);
    check(
        !selected->quiet_travel_view().available &&
            !selected->set_quiet_travel_multiplier(2) &&
            selected->set_quiet_travel_multiplier(1).has_value(),
        "Actual buffer/proximity constraint did not refuse faster scheduling");
  }
  for (auto initial : {Initial::high_air, Initial::far_station}) {
    auto eligible = fixture(initial, false);
    auto selected = world(*eligible);
    check(selected->quiet_travel_view().available &&
              selected->set_quiet_travel_multiplier(4).has_value(),
          "Actual outside boundary rejected eligible scheduling");
  }
  for (auto initial : {Initial::falling_air, Initial::closing_station}) {
    for (int ordinary : {1, 4, 15}) {
      auto entering = fixture(initial, false);
      auto selected = world(*entering);
      auto peer = std::make_unique<NativeFreedomFlightSession>(*entering);
      need(selected->set_quiet_travel_multiplier(4));
      selected->advance(kSimulationStep.count() * ordinary, neutral, false);
      for (int tick = 0; tick < ordinary; ++tick)
        need(peer->advance({}));
      check(selected->quiet_travel_view().multiplier == 1 &&
                !selected->quiet_travel_view().available &&
                bytes(selected->session) == bytes(*peer),
            "Crossing a guard spent bonus time, skipped ordinary work or "
            "retained pace with no bonus budget");
    }
  }
  auto exhausted_clock = fixture(
      Initial::orbit, false, std::numeric_limits<SimulationTick>::max() - 3);
  auto overflow = world(*exhausted_clock);
  need(overflow->set_quiet_travel_multiplier(4));
  refused(*overflow,
          [&] { overflow->advance(kSimulationStep.count(), neutral, false); });
}
auto paid_deadline_and_dry() -> void {
  // Retain an actual public chart5 owner, enrollment, reservation and completed
  // boarding route. Only the following free-space pose/current clock is an
  // initialized numerical boundary, not a claim of a flown four-hour job.
  auto station = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_planetary_network_new_game({42})))));
  need(station->enable_paid_operations(
      need(station->paid_operations_view()).token));
  const auto offer = need(station->survey_commission_view());
  need(station->accept_survey_commission(offer.offer_until,
                                         offer.offer_sequence));
  for (unsigned tick = 0; tick < 1194; ++tick)
    need(station->advance_walk({0, -1, 0}));
  need(station->begin_boarding());
  for (SimulationTick tick = 0; tick < kGameplayBoardingTicks; ++tick)
    need(station->advance_walk({}));
  need(station->release_port());
  need(station->select_jump(offer.system));
  auto value = std::make_unique<FreedomTravelSaveDocument>(
      need(station->travel_document()));
  auto& flight =
      std::get<FreedomBoardingSaveDocument>(value->voyage.voyage.voyage.base)
          .voyage.flight;
  const auto boundary =
      need(station->survey_commission_view()).deadline_tick - 1;
  flight.flight.tick = boundary;
  flight.origin.state.tick = boundary;
  value->voyage.voyage.resources.tick = boundary;
  const auto& planet = station->system().catalog.planets.front().descriptor;
  flight.flight.position_metres = {planet.radius.value * 1500.0, 0, 0};
  const auto gravity =
      need(evaluate_central_body_gravity({station->system()}, flight.flight));
  flight.flight.linear_velocity_metres_per_second = {
      0, 0,
      std::sqrt(
          gravity.gravitational_parameter_metres_cubed_per_second_squared /
          gravity.distance_metres)};
  // Birth/boarding history, current world, craft/pilot binding, selected target
  // and financial/reservation anchors retain their actual original values.
  // The next ordinary tick crosses both reservation expiry and prepaid berth.
  auto source = open(*value);
  auto selected = world(*source);
  auto peer = std::make_unique<NativeFreedomFlightSession>(*source);
  check(need(source->paid_operations_view()).cash == 180 &&
            !need(source->survey_commission_view()).expired,
        "Initialized clock boundary did not start before real billing/expiry");
  need(selected->set_quiet_travel_multiplier(4));
  selected->advance(kSimulationStep.count(), neutral, false);
  for (unsigned tick = 0; tick < 4; ++tick)
    need(peer->advance({}));
  check(bytes(selected->session) == bytes(*peer) &&
            need(selected->session.survey_commission_view()).expired &&
            need(selected->session.paid_operations_view()).cash == 175,
        "Paced ticks paused deadlines, deferred berth cost or financial owner");
  auto dry_document = document();
  dry_document->voyage.voyage.resources.flight_quanta = 0;
  auto dry = open(*dry_document);
  auto dry_world = world(*dry);
  auto dry_peer = std::make_unique<NativeFreedomFlightSession>(*dry);
  need(dry_world->set_quiet_travel_multiplier(4));
  dry_world->advance(kSimulationStep.count(), neutral, false);
  for (unsigned tick = 0; tick < 4; ++tick)
    need(dry_peer->advance({}));
  check(bytes(dry_world->session) == bytes(*dry_peer) &&
            dry_world->session.resources()->flight_quanta == 0,
        "Quiet travel required fuel or invented free assisted propulsion");
}
auto actual_modes() -> void {
  auto station = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(need(native_new_game({42})))));
  auto walking = world(*station);
  check(!walking->quiet_travel_view().available &&
            !walking->set_quiet_travel_multiplier(4),
        "Actual walking station accelerated");
  for (unsigned tick = 0; tick < 1194; ++tick)
    need(station->advance_walk({0, -1, 0}));
  need(station->begin_boarding());
  auto boarding = world(*station);
  check(!boarding->quiet_travel_view().available,
        "Actual boarding transition accelerated");
  for (SimulationTick tick = 0; tick < kGameplayBoardingTicks; ++tick)
    need(station->advance_walk({}));
  auto attached = world(*station);
  check(!attached->quiet_travel_view().available,
        "Actual seated attachment accelerated");
  need(station->release_port());
  auto near = world(*station);
  check(!near->quiet_travel_view().available,
        "Fresh actual release ignored station proximity");
  // Keep the real seated/entry record; initialize only the declared free-space
  // boundary, not a claim that this test flew a station-to-orbit ascent.
  auto selected_doc = std::make_unique<FreedomKnowledgeSaveDocument>(
      std::get<FreedomKnowledgeSaveDocument>(need(station->save_document())));
  auto& flight =
      std::get<FreedomBoardingSaveDocument>(selected_doc->voyage.voyage.base)
          .voyage.flight;
  const auto& planet = station->system().catalog.planets.front().descriptor;
  flight.flight.position_metres = {planet.radius.value * 1500.0, 0, 0};
  flight.flight.linear_velocity_metres_per_second = {};
  auto seated = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_select_save_document(*selected_doc)))));
  auto allowed = world(*seated);
  check(allowed->session.boarding() && allowed->session.surface() &&
            allowed->quiet_travel_view().available,
        "Mere seated boarding/idle surface history permanently disabled quiet "
        "travel");
  // Initialized same-tick pose20km below the actual selected port. Preserve
  // the real seated route; this is an aid-admission boundary, not flown travel.
  auto guided_doc = std::make_unique<FreedomKnowledgeSaveDocument>(
      std::get<FreedomKnowledgeSaveDocument>(need(station->save_document())));
  auto& guided =
      std::get<FreedomBoardingSaveDocument>(guided_doc->voyage.voyage.base)
          .voyage.flight;
  const auto port_station = generate_origin_station({42});
  const auto port_geometry = need(origin_station_geometry(port_station));
  guided.flight =
      need(release_origin_port(station->system(), port_station, port_geometry,
                               {port_geometry.version,
                                {port_station.id, 1},
                                guided.flight.craft,
                                guided.flight.tick}));
  const auto outward = port_geometry.ports.front().outward_normal;
  guided.flight.position_metres.x += 20000 * outward.x;
  guided.flight.position_metres.y += 20000 * outward.y;
  guided.flight.position_metres.z += 20000 * outward.z;
  auto maneuver = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(
          need(native_select_save_document(*guided_doc)))));
  auto guiding = world(*maneuver);
  check(guiding->quiet_travel_view().available,
        "Initialized far port source is not clear-space eligible");
  need(guiding->session.begin_port_rendezvous());
  check(!guiding->quiet_travel_view().available &&
            !guiding->set_quiet_travel_multiplier(4),
        "Actual active rendezvous admitted accelerated work");
  guiding->session.cancel_port_rendezvous();
  check(guiding->quiet_travel_view().available,
        "Canceled aid record permanently disabled pacing");
  for (unsigned companion : {0U, 1U}) {
    auto departure = std::make_unique<NativeFreedomFlightSession>(
        minor_route_fixture::departure());
    const auto catalog =
        need(generate_secondary_body_catalog(departure->system()));
    need(departure->select_local_jump(
        {departure->system().catalog.id, catalog.bodies[companion].node.id}));
    minor_route_fixture::leg(*departure);
    auto unsupported = world(*departure);
    check(!unsupported->quiet_travel_view().available &&
              !unsupported->set_quiet_travel_multiplier(4),
          "Actual Moon/minor arrival admitted primary-planet pacing");
  }
  auto jump_source = fixture();
  auto jumping = world(*jump_source);
  need(jumping->set_quiet_travel_multiplier(4));
  need(jumping->session.select_jump(
      generate_first_intersystem_identities({42}).target_system));
  need(jumping->session.begin_jump());
  jumping->advance(kSimulationStep.count(), neutral, false);
  check(jumping->quiet_travel_view().multiplier == 1 &&
            !jumping->quiet_travel_view().available,
        "Active jump failed to return scheduling to normal");
  auto pending = fixture();
  auto recovery = std::make_unique<FreedomRecoverySaveDocument>(
      need(pending->recovery_document()));
  need(pending->record_loss(
      FreedomLossCause::recoverable_destruction,
      pending->document().origin.state.craft, pending->document().flight.tick,
      need(freedom_recovery_source_checksum(recovery->voyage))));
  auto lost = world(*pending);
  check(!lost->quiet_travel_view().available &&
            !lost->set_quiet_travel_multiplier(4) &&
            lost->set_quiet_travel_multiplier(1).has_value(),
        "Pending loss admitted acceleration or refused normal cancellation");
}
auto npc_modes() -> void {
  // Initialized, provider-validated service/beacon boundaries. No dispatched
  // sortie, delivery, failure history or service flight is claimed here.
  auto contact = std::make_unique<test_fixture::MoonContactFixture>(
      test_fixture::make_moon_contact_fixture());
  auto anchor = need(prepare_moon_landed_craft(
      contact->moon, contact->source, true,
      need(rigid_body_state_checksum({contact->moon}, contact->source))));
  for (unsigned mode = 0; mode < 5; ++mode) {
    auto value = document();
    const auto player = value->voyage.voyage.resources.craft;
    auto context = need(make_origin_assistance_world_context({42}));
    const auto tick = mode == 4 ? 1199U : 1200U;
    auto service =
        need(initialize_origin_service_craft(context.origin(), tick));
    auto beacon = OriginServiceBeacon{1, player, {}, anchor};
    beacon.anchor.fixed.tick = tick;
    OriginServiceSortie sortie{1,
                               service,
                               beacon,
                               OriginServiceSortiePhase::requested,
                               tick,
                               tick,
                               tick + kOriginServiceSortiePhaseTicks,
                               false,
                               {},
                               {}};
    if (mode == 2 || mode == 3) {
      sortie.phase = mode == 2 ? OriginServiceSortiePhase::completed
                               : OriginServiceSortiePhase::canceled;
      sortie.deadline_tick = 0;
    }
    need(validate_origin_service_sortie(context.origin(), context.moon(),
                                        sortie));
    FreedomAssistanceActor actor = service;
    if (mode == 1 || mode == 2 || mode == 3) actor = sortie;
    if (mode == 4)
      actor = OriginServiceUnavailable{
          sortie, 1200, OriginServiceUnavailableReason::route_unavailable};
    auto wrapped = std::make_unique<FreedomAssistanceSaveDocument>(
        FreedomAssistanceSaveDocument{1, std::move(*value), actor});
    auto session = std::make_unique<NativeFreedomFlightSession>(
        need(NativeFreedomFlightSession::open(
            need(native_select_save_document(*wrapped)))));
    auto selected = world(*session);
    check(selected->quiet_travel_view().available == (mode != 1),
          "NPC service guard confused active sortie with home/terminal/history "
          "record");
    if (mode != 1) {
      need(selected->set_quiet_travel_multiplier(4));
      auto peer = std::make_unique<NativeFreedomFlightSession>(*session);
      selected->advance(kSimulationStep.count(), neutral, false);
      for (unsigned tick_index = 0; tick_index < 4; ++tick_index)
        need(peer->advance({}));
      check(bytes(selected->session) == bytes(*peer),
            "Paced world ticks diverged from actual NPC/financial/physical "
            "owners");
    }
  }
}
} // namespace
auto main() -> int {
  try {
    malformed();
    cadence();
    pilot_and_boundary();
    paid_deadline_and_dry();
    actual_modes();
    npc_modes();
    std::cout << "Quiet travel: " << checks << " checks, 0 failures\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Quiet travel after " << checks << " checks: " << error.what()
              << '\n';
    return 1;
  }
}
