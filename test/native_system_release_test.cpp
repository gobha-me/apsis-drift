#include "apsis_drift/native_flight_session.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
using V = RigidVector3;
int checks{}, failures{};
auto check(bool accepted, std::string_view message) -> void {
  ++checks;
  if (!accepted) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <class T, class E>
auto need(std::expected<T, E> value,
          std::source_location location = std::source_location::current())
    -> T {
  if (!value) {
    std::string message =
        "System release fixture at line " + std::to_string(location.line());
    if constexpr (std::is_same_v<E, std::string>)
      message += ": " + value.error();
    else if constexpr (requires { value.error().detail; })
      message += ": " + value.error().detail;
    throw std::runtime_error(message);
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto read(const std::filesystem::path& path) -> std::string {
  std::ifstream stream{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{stream}, {}};
}
auto write(const std::filesystem::path& path, std::string_view bytes) -> void {
  std::ofstream stream{path, std::ios::binary};
  stream << bytes;
  if (!stream) throw std::runtime_error("Cannot write release checkpoint");
}
auto bytes(const NativeFreedomFlightSession& session) -> std::string {
  return need(encode_native_save_document_json(need(session.save_document())));
}
auto reopen(const NativeFreedomFlightSession& session)
    -> NativeFreedomFlightSession {
  const auto original = bytes(session);
  auto result =
      need(NativeFreedomFlightSession::open(need(native_select_save_document(
          need(decode_native_save_document_json(original))))));
  check(bytes(result) == original,
        "Complete selected voyage survives exact Save/Continue");
  return result;
}
auto same(double a, double b) -> bool {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
auto same(V a, V b) -> bool {
  return same(a.x, b.x) && same(a.y, b.y) && same(a.z, b.z);
}
auto same(RigidOrientation a, RigidOrientation b) -> bool {
  return same(a.w, b.w) && same(a.x, b.x) && same(a.y, b.y) && same(a.z, b.z);
}
auto close(double actual, double expected, double offset) -> bool {
  return std::abs(actual - expected) <=
         4 * std::numeric_limits<double>::epsilon() *
             std::max({1.0, std::abs(expected), std::abs(offset)});
}
auto close(V actual, V expected, V offset) -> bool {
  return close(actual.x, expected.x, offset.x) &&
         close(actual.y, expected.y, offset.y) &&
         close(actual.z, expected.z, offset.z);
}
auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto subtract(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto host_position(const PlanetEphemeris& host) -> V {
  return {host.position.x, host.position.y, host.position.z};
}
auto host_velocity(const PlanetEphemeris& host) -> V {
  return {host.velocity.x, host.velocity.y, host.velocity.z};
}
auto selected_start() -> NativeStartup {
  auto selected = need(native_new_game({42}));
  auto& knowledge = std::get<FreedomKnowledgeSaveDocument>(selected.document);
  auto& assembly = std::get<FreedomStartingAssemblySaveDocument>(
      knowledge.voyage.voyage.base);
  auto& flight = assembly.journey.voyage.flight;
  check(flight.model.frame_policy == kFreedomRelativeFramePolicy,
        "Ordinary New Game retains historical relative frame policy");
  flight.model.frame_policy = kFreedomReleaseSystemFramePolicy;
  flight.world = FreedomActiveWorldSelection{1, flight.flight.frame.system,
                                             *flight.flight.frame.planet};
  // Explicit world ownership selects the existing wider knowledge domain;
  // retain all granted entries and provenance while selecting its recipe.
  knowledge.knowledge.recipe.version = kFreedomTravelKnowledgeVersion;
  knowledge.knowledge.recipe.world_domain = 1;
  return selected;
}
auto selected_flight(NativeStartup& selected) -> FreedomFlightSaveDocument& {
  return std::get<FreedomStartingAssemblySaveDocument>(
             std::get<FreedomKnowledgeSaveDocument>(selected.document)
                 .voyage.voyage.base)
      .journey.voyage.flight;
}
auto invalid_selection() -> void {
  auto selected = selected_start();
  const auto original =
      need(encode_native_save_document_json(selected.document));
  const auto owner =
      need(hydrate_freedom_flight_document(selected_flight(selected))).system;
  const auto refuses = [&](NativeStartup bad) {
    check(!NativeFreedomFlightSession::open(std::move(bad)) &&
              need(encode_native_save_document_json(selected.document)) ==
                  original,
          "Invalid selected owner/model/clock refuses without changing source");
  };
  std::optional<NativeStartup> bad{selected};
  selected_flight(*bad).world.reset();
  refuses(std::move(*bad));
  bad.emplace(selected);
  selected_flight(*bad).world->planet = PlanetId{0};
  refuses(std::move(*bad));
  bad.emplace(selected);
  selected_flight(*bad).world->system = SystemId{0};
  refuses(std::move(*bad));
  bad.emplace(selected);
  selected_flight(*bad).model.frame_policy = 99;
  refuses(std::move(*bad));
  bad.emplace(selected);
  ++selected_flight(*bad).origin.state.tick;
  refuses(std::move(*bad));
  bad.emplace(selected);
  selected_flight(*bad).flight.frame.planet.reset();
  refuses(std::move(*bad));
  bad.emplace(selected);
  selected_flight(*bad).flight.position_metres.x =
      std::numeric_limits<double>::quiet_NaN();
  refuses(std::move(*bad));
  bad.emplace(selected);
  selected_flight(*bad).flight.linear_velocity_metres_per_second.z =
      std::numeric_limits<double>::infinity();
  refuses(std::move(*bad));
  bad.emplace(selected);
  selected_flight(*bad).flight.position_metres.y =
      kRigidBodyMaximumPositionMetres * 2;
  refuses(std::move(*bad));
  auto foreign = need(generate_physical_origin_system({43}, 2));
  check(!relative_freedom_flight_state(selected_flight(selected), foreign),
        "Relative observation refuses another complete physical owner");
  auto flight = selected_flight(selected);
  flight.flight.angular_velocity_radians_per_second.y =
      std::numeric_limits<double>::quiet_NaN();
  check(!relative_freedom_flight_state(flight, owner),
        "Relative observation cannot hide nonfinite canonical state");
  flight = selected_flight(selected);
  flight.flight.tick = std::numeric_limits<SimulationTick>::max() - 1;
  flight.origin.state.tick = flight.flight.tick;
  check(!relative_freedom_flight_state(flight, owner),
        "Unadvanceable shared clock refuses before relative observation");
}
auto independent_relative(const NativeFreedomFlightSession& session)
    -> RigidBodyState {
  auto result = session.document().flight;
  const auto& selection = *session.document().world;
  const auto host = need(resolve_planet_ephemeris(
      session.system(), selection.planet, {result.tick, 0}));
  result.frame = {RigidFrameKind::planet_relative_inertial,
                  selection.system,
                  selection.planet,
                  {}};
  result.position_metres =
      subtract(result.position_metres, host_position(host));
  result.linear_velocity_metres_per_second =
      subtract(result.linear_velocity_metres_per_second, host_velocity(host));
  return need(canonicalize_rigid_body_state({session.system()}, result));
}
auto check_system_oracle(const NativeFreedomFlightSession& session,
                         const RigidBodyState& relative) -> void {
  const auto& actual = session.document().flight;
  const auto host = need(resolve_planet_ephemeris(
      session.system(), *relative.frame.planet, {relative.tick, 0}));
  // Independent final-tick arithmetic: never call the forward handoff to
  // construct this oracle. The established relative integrator owns forces.
  check(actual.frame == RigidCoordinateFrame{RigidFrameKind::system_inertial,
                                             relative.frame.system,
                                             {},
                                             {}} &&
            actual.tick == relative.tick && actual.craft == relative.craft &&
            same(actual.orientation, relative.orientation) &&
            same(actual.angular_velocity_radians_per_second,
                 relative.angular_velocity_radians_per_second) &&
            close(actual.position_metres,
                  add(relative.position_metres, host_position(host)),
                  host_position(host)) &&
            close(actual.linear_velocity_metres_per_second,
                  add(relative.linear_velocity_metres_per_second,
                      host_velocity(host)),
                  host_velocity(host)),
        "Canonical system state equals final ephemeris plus relative dynamics; "
        "tick/craft/attitude/body spin retain exact bits");
  check(need(session.relative_state()) == independent_relative(session),
        "Shared relative adapter equals independent same-tick subtraction");
}
auto gear_handoff(const NativeFreedomFlightSession& source,
                  const NativeFlightControls& held) -> void {
  const auto unchanged = bytes(source);
  auto deployed = source;
  auto expected = deployed.document();
  expected.flight = independent_relative(source);
  need(deployed.set_landing_gear(true));
  check(
      deployed.document() == expected && deployed.surface()->gear_deployed &&
          !deployed.surface()->landed &&
          deployed.resources() == source.resources() &&
          deployed.knowledge() == source.knowledge() &&
          deployed.travel() == source.travel() &&
          deployed.boarding() == source.boarding() &&
          deployed.starting_assembly() == source.starting_assembly() &&
          need(deployed.recovery_document()).recovery ==
              need(source.recovery_document()).recovery,
      "Explicit deploy selects same-tick relative body and retains all owners");
  check(same(deployed.document().flight.orientation,
             source.document().flight.orientation) &&
            same(deployed.document().flight.angular_velocity_radians_per_second,
                 source.document().flight.angular_velocity_radians_per_second),
        "Gear handoff preserves exact attitude and body-resolved spin");
  auto resumed = reopen(deployed);
  need(deployed.advance(held));
  need(resumed.advance(held));
  check(bytes(deployed) == bytes(resumed),
        "Held body controls continue identically after gear Save/Continue");
  auto impacted = deployed;
  need(impacted.enable_impact_consequences());
  const auto condition = impacted.condition();
  const auto relative = impacted.document().flight;
  need(impacted.set_landing_gear(false));
  check(impacted.document().flight == relative &&
            impacted.condition() == condition &&
            !impacted.surface()->gear_deployed,
        "Stow preserves relative impact owner and never disables consequences");
  (void)reopen(impacted);
  const auto before_stow = deployed.document().flight;
  need(deployed.set_landing_gear(false));
  check_system_oracle(deployed, before_stow);
  check(!deployed.surface()->gear_deployed && !deployed.surface()->landed,
        "Airborne deployed-to-stowed transition selects system representation");
  resumed = reopen(deployed);
  need(deployed.advance(held));
  need(resumed.advance(held));
  check(
      bytes(deployed) == bytes(resumed) && bytes(source) == unchanged,
      "System continuation retains held controls and untouched source voyage");
}
auto port_handoff(const NativeFreedomFlightSession& source) -> void {
  const auto original = bytes(source);
  const auto& aid_before = source.port_approach();
  const auto& maneuver_before = source.surface_maneuver();
  need(source.port_approach_available());
  check(bytes(source) == original && source.port_approach() == aid_before &&
            source.surface_maneuver() == maneuver_before,
        "System approach availability never changes body, owner or transient "
        "state");
  auto approached = source;
  auto expected = source.document();
  expected.flight = independent_relative(source);
  need(approached.begin_port_approach());
  check(approached.document() == expected &&
            approached.port_approach().active &&
            approached.port_approach().remaining_ticks ==
                kNativePortApproachTicks &&
            approached.resources() == source.resources() &&
            approached.knowledge() == source.knowledge() &&
            approached.travel() == source.travel() &&
            approached.boarding() == source.boarding() &&
            approached.docking() == source.docking() &&
            approached.starting_assembly() == source.starting_assembly() &&
            need(approached.recovery_document()).recovery ==
                need(source.recovery_document()).recovery,
        "Explicit approach arms existing relative aid at same tick with every "
        "owner");
  check(
      same(approached.document().flight.orientation,
           source.document().flight.orientation) &&
          same(approached.document().flight.angular_velocity_radians_per_second,
               source.document().flight.angular_velocity_radians_per_second),
      "Approach representation preserves exact nonidentity attitude and body "
      "spin");
  auto resumed = reopen(approached);
  check(!resumed.port_approach().active,
        "Continue retains relative body but does not serialize transient "
        "approach");
  need(resumed.begin_port_approach());
  auto canceled = approached;
  canceled.cancel_port_approach();
  check(!canceled.port_approach().active && canceled.document() == expected &&
            bytes(canceled) == bytes(approached),
        "Cancel keeps valid relative frame without inventing system-return "
        "action");
  // Active-jump refusal uses the existing real route selection, not an altered
  // save or an invented travel owner. This clone never changes the source
  // trace.
  auto spooling = canceled;
  need(spooling.begin_jump());
  const auto spool_bytes = bytes(spooling);
  const auto spool_aid = spooling.port_approach();
  check(!spooling.begin_port_approach() && bytes(spooling) == spool_bytes &&
            spooling.port_approach() == spool_aid,
        "An actual active jump retains its owner and refuses approach");
  auto pending = source;
  const auto recovery = need(pending.recovery_document());
  need(pending.record_loss(
      FreedomLossCause::irrecoverable_destruction,
      pending.document().origin.state.craft, pending.document().flight.tick,
      need(freedom_recovery_source_checksum(recovery.voyage))));
  const auto pending_bytes = bytes(pending);
  check(!pending.port_approach_available() && !pending.begin_port_approach() &&
            bytes(pending) == pending_bytes &&
            pending.port_approach() == aid_before,
        "Recorded pending recovery refuses system approach without "
        "representation change");
  auto gear = source;
  need(gear.set_landing_gear(true));
  const auto gear_bytes = bytes(gear);
  check(!gear.port_approach_available() && !gear.begin_port_approach() &&
            bytes(gear) == gear_bytes,
        "Existing deployed-gear approach gate remains transactional");
  NativeFlightControls invalid;
  invalid.positive_translation.x = std::numeric_limits<double>::quiet_NaN();
  const auto active_bytes = bytes(approached);
  const auto active_aid = approached.port_approach();
  check(!approached.advance(invalid) &&
            !approached.advance({}, SimulationSeconds{.1}) &&
            bytes(approached) == active_bytes &&
            approached.port_approach() == active_aid &&
            approached.surface_maneuver() == maneuver_before,
        "Invalid controls or step retain active approach and whole voyage");
  bool ready = false, propulsion = false;
  unsigned ticks{};
  while (approached.port_approach().active &&
         ticks < kNativePortApproachTicks) {
    const auto step = need(approached.advance({}));
    const auto repeated = need(resumed.advance({}));
    const auto& forces = step.actuation.central.propulsion;
    const auto& repeated_forces = repeated.actuation.central.propulsion;
    propulsion = propulsion || ((forces.positive_force_newtons != V{} ||
                                 forces.negative_force_newtons != V{}) &&
                                step.fuel_debit_quanta > 0);
    check(bytes(approached) == bytes(resumed) &&
              approached.port_approach() == resumed.port_approach() &&
              approached.surface_maneuver() == resumed.surface_maneuver() &&
              forces.positive_force_newtons ==
                  repeated_forces.positive_force_newtons &&
              forces.negative_force_newtons ==
                  repeated_forces.negative_force_newtons &&
              step.fuel_debit_quanta == repeated.fuel_debit_quanta &&
              step.propulsion_refused == repeated.propulsion_refused,
          "Every real approach tick retains complete resumed bytes, transients "
          "and applied thrust");
    ++ticks;
    ready = need(approached.assess_port()).decision ==
            OriginDockDecision::capture_ready;
    if (ready && propulsion) break;
  }
  check(ready && propulsion && ticks > 0 && approached.port_approach().active &&
            !approached.docking()->attached &&
            bytes(approached) == bytes(resumed),
        "Actual bounded aid uses propulsion and reaches capture gate without "
        "auto-capture");
  if (!ready || !propulsion || !approached.port_approach().active) return;
  const auto capture_tick = approached.document().flight.tick;
  need(approached.capture_port());
  need(resumed.capture_port());
  check(approached.docking()->attached && !approached.port_approach().active &&
            approached.document().flight.tick == capture_tick &&
            bytes(approached) == bytes(resumed),
        "Explicit actual capture retains relative constraint and all resumed "
        "owners");
  const auto captured = bytes(approached);
  check(!approached.port_approach_available() &&
            !approached.begin_port_approach() && bytes(approached) == captured,
        "Attached approach refusal leaves complete captured voyage unchanged");
  const auto constrained = approached.document().flight;
  need(approached.release_port());
  check_system_oracle(approached, constrained);
  (void)reopen(approached);
  check(bytes(source) == original, "Rendezvous/cancel/guard/capture copies "
                                   "never change exported source route");
}
auto jump_handoff(const NativeFreedomFlightSession& source) -> void {
  const auto original = bytes(source);
  const auto source_body = source.document().flight;
  const auto& original_aid = source.port_approach();
  const auto& original_maneuver = source.surface_maneuver();
  const auto route = generate_first_intersystem_identities(
      source.document().origin.recipe.universe_seed);
  auto selected = source;
  check(!selected.select_jump({0}) && bytes(selected) == original &&
            selected.port_approach() == original_aid &&
            selected.surface_maneuver() == original_maneuver,
        "Unknown system jump selection preserves canonical body and whole "
        "source");
  need(selected.select_jump(route.target_system));
  check(
      selected.document().flight == source_body &&
          selected.travel()->selected == route.target_system &&
          selected.document().world == source.document().world &&
          selected.document().origin == source.document().origin &&
          selected.resources() == source.resources() &&
          selected.knowledge() == source.knowledge() &&
          selected.boarding() == source.boarding() &&
          selected.starting_assembly() == source.starting_assembly() &&
          need(selected.recovery_document()).recovery ==
              need(source.recovery_document()).recovery,
      "Known selection keeps exact canonical system body, tick and all owners");
  selected = reopen(selected);
  const auto before_queries = bytes(selected);
  const auto preview = need(selected.jump_preview());
  auto request = preview.request;
  request.source = independent_relative(selected);
  check(preview == need(preview_freedom_jump(request)) &&
            preview.request.source == independent_relative(selected) &&
            same(preview.request.source.orientation, source_body.orientation) &&
            same(preview.request.source.angular_velocity_radians_per_second,
                 source_body.angular_velocity_radians_per_second),
        "System preview consumes independent same-tick relative source with "
        "exact attitude/spin");
  need(selected.jump_available());
  check(
      bytes(selected) == before_queries &&
          selected.document().flight == source_body &&
          selected.port_approach() == original_aid &&
          selected.surface_maneuver() == original_maneuver,
      "Preview and availability never convert the live body or mutate owners");
  auto pending = selected;
  const auto recovery = need(pending.recovery_document());
  need(pending.record_loss(
      FreedomLossCause::irrecoverable_destruction,
      pending.document().origin.state.craft, pending.document().flight.tick,
      need(freedom_recovery_source_checksum(recovery.voyage))));
  const auto pending_bytes = bytes(pending);
  check(!pending.select_jump(route.target_system) &&
            !pending.jump_available() && !pending.begin_jump() &&
            bytes(pending) == pending_bytes,
        "Pending recovery guards still refuse selected system jump actions "
        "transactionally");
  auto begun = selected;
  auto relative_document = selected.document();
  relative_document.flight = independent_relative(selected);
  need(begun.begin_jump());
  check(begun.document() == relative_document &&
            begun.travel()->phase == FreedomJumpPhase::spool &&
            begun.travel()->spool_tick == source_body.tick &&
            begun.resources() == selected.resources() &&
            begun.knowledge() == selected.knowledge() &&
            begun.boarding() == selected.boarding() &&
            begun.starting_assembly() == selected.starting_assembly() &&
            need(begun.recovery_document()).recovery ==
                need(selected.recovery_document()).recovery,
        "Explicit begin commits same-tick relative source and unchanged "
        "whole-voyage owners");
  auto canceled = reopen(begun);
  need(canceled.advance({}));
  const auto canceled_charges = canceled.resources()->jump_charges;
  const auto canceled_attempt = canceled.travel()->next_attempt;
  need(canceled.cancel_jump());
  check(canceled.document().flight.frame.kind ==
                RigidFrameKind::planet_relative_inertial &&
            canceled.document().flight.tick == source_body.tick + 1 &&
            canceled.resources()->jump_charges == canceled_charges &&
            canceled.travel()->next_attempt == canceled_attempt &&
            canceled.travel()->phase == FreedomJumpPhase::idle,
        "Actual uncommitted cancel retains relative body without jump refund "
        "or system return");
  (void)reopen(canceled);
  auto live = begun;
  auto resumed = reopen(live);
  const auto charges = live.resources()->jump_charges;
  const auto attempt = live.travel()->next_attempt;
  NativeFlightControls invalid;
  invalid.positive_translation.x = std::numeric_limits<double>::quiet_NaN();
  const auto spooling_bytes = bytes(live);
  check(!live.advance(invalid) && !live.advance({}, SimulationSeconds{.1}) &&
            !live.select_jump(route.target_system) &&
            bytes(live) == spooling_bytes,
        "Invalid controls/step or active-spool reselection preserve complete "
        "jump source");
  std::optional<FrozenFreedomJumpArrival> frozen;
  for (SimulationTick tick = 1; tick <= kJumpSpoolTicks + kJumpTransitTicks;
       ++tick) {
    const auto step = need(live.advance({}));
    const auto repeated = need(resumed.advance({}));
    check(step.fuel_debit_quanta == repeated.fuel_debit_quanta &&
              step.jump_committed == repeated.jump_committed &&
              step.jump_arrived == repeated.jump_arrived,
          "Each actual jump tick retains resumed commitment/arrival/debit "
          "behavior");
    if (tick < kJumpSpoolTicks)
      check(live.resources()->jump_charges == charges && !step.jump_committed,
            "Existing actual spool does not bill a charge early");
    if (tick == kJumpSpoolTicks) {
      check(step.jump_committed &&
                live.travel()->phase == FreedomJumpPhase::transit &&
                live.resources()->jump_charges == charges - 1 &&
                live.travel()->next_attempt == attempt + 1 &&
                live.travel()->committed.has_value(),
            "Qualified actual source commits one existing charge and frozen "
            "arrival");
      if (!step.jump_committed || !live.travel()->committed) return;
      frozen = live.travel()->committed;
      const auto committed = bytes(live);
      check(!live.cancel_jump() && !live.select_jump(route.target_system) &&
                bytes(live) == committed &&
                need(live.jump_preview()) == frozen->preview,
            "Committed transit refuses cancel/reselection and preserves frozen "
            "preview");
    }
    if (tick > kJumpSpoolTicks)
      check(step.fuel_debit_quanta == 0 &&
                live.resources()->flight_quanta ==
                    live.travel()->commitment_resources->flight_quanta &&
                live.resources()->jump_charges == charges - 1,
            "Existing transit clocks do not refund charges or add flight-fuel "
            "debit");
    if (tick == 1 || tick == kJumpSpoolTicks - 1 || tick == kJumpSpoolTicks ||
        tick == kJumpSpoolTicks + 1 ||
        tick == kJumpSpoolTicks + kJumpTransitTicks - 1 ||
        tick == kJumpSpoolTicks + kJumpTransitTicks) {
      check(bytes(live) == bytes(resumed),
            "Full outer-owner saves match at every real jump phase boundary");
      resumed = reopen(resumed);
    }
    if (tick == kJumpSpoolTicks + kJumpTransitTicks)
      check(step.jump_arrived,
            "Existing scheduled arrival occurs without a fabricated leg");
  }
  check(frozen &&
            live.document().flight.frame.kind ==
                RigidFrameKind::planet_relative_inertial &&
            live.document().model.frame_policy ==
                kFreedomReleaseSystemFramePolicy &&
            live.system().catalog.id == route.target_system &&
            live.document().world->system == route.target_system &&
            live.document().flight.tick ==
                source_body.tick + kJumpSpoolTicks + kJumpTransitTicks &&
            live.travel()->phase == FreedomJumpPhase::idle && !live.docking() &&
            live.boarding() == source.boarding() &&
            live.starting_assembly() == source.starting_assembly() &&
            need(live.recovery_document()).recovery ==
                need(source.recovery_document()).recovery &&
            same(live.document().flight.orientation,
                 frozen->preview.request.source.orientation) &&
            same(live.document().flight.angular_velocity_radians_per_second,
                 frozen->preview.request.source
                     .angular_velocity_radians_per_second) &&
            bytes(live) == bytes(resumed) && bytes(source) == original,
        "Actual arrival retains existing relative semantics, "
        "pilot/assembly/recovery and exact frozen attitude/spin");
}
auto impact_handoff(const NativeFreedomFlightSession& source,
                    const NativeFlightControls& held) -> void {
  const auto original = bytes(source);
  const auto& aid_before = source.port_approach();
  const auto& maneuver_before = source.surface_maneuver();
  need(source.impact_consequences_available());
  check(bytes(source) == original && source.port_approach() == aid_before &&
            source.surface_maneuver() == maneuver_before,
        "System impact availability is read-only for canonical body and whole "
        "voyage");
  auto expected = source.document();
  expected.flight = independent_relative(source);
  const auto pristine = need(make_craft_condition(expected));
  auto enabled = source;
  need(enabled.enable_impact_consequences());
  check(enabled.document() == expected && enabled.condition() == pristine &&
            enabled.resources() == source.resources() &&
            enabled.knowledge() == source.knowledge() &&
            enabled.travel() == source.travel() &&
            enabled.docking() == source.docking() &&
            enabled.boarding() == source.boarding() &&
            enabled.starting_assembly() == source.starting_assembly() &&
            need(enabled.recovery_document()).recovery ==
                need(source.recovery_document()).recovery &&
            enabled.port_approach() == aid_before &&
            enabled.surface_maneuver() == maneuver_before &&
            same(enabled.document().flight.orientation,
                 source.document().flight.orientation) &&
            same(enabled.document().flight.angular_velocity_radians_per_second,
                 source.document().flight.angular_velocity_radians_per_second),
        "Explicit impact selection creates existing condition on same-tick "
        "relative body with all owners");
  need(validate_craft_condition(*enabled.condition(), enabled.document()));
  (void)need(enabled.condition_document());
  const auto activated = bytes(enabled);
  need(enabled.enable_impact_consequences());
  check(bytes(enabled) == activated && !enabled.impact_consequences_available(),
        "Existing selected-condition activation remains idempotent");
  (void)reopen(enabled);
  const bool ready =
      need(enabled.assess_port()).decision == OriginDockDecision::capture_ready;
  check(ready, "Fresh selected-condition copy retains the actual capture-ready "
               "envelope");
  if (!ready) return;
  need(enabled.capture_port());
  const auto captured_owner = enabled;
  auto resumed = reopen(enabled);
  need(enabled.release_port());
  need(resumed.release_port());
  check(!enabled.docking()->attached &&
            enabled.document() == captured_owner.document() &&
            enabled.document().flight.frame.kind ==
                RigidFrameKind::planet_relative_inertial &&
            enabled.condition() == captured_owner.condition() &&
            enabled.resources() == captured_owner.resources() &&
            enabled.knowledge() == captured_owner.knowledge() &&
            enabled.travel() == captured_owner.travel() &&
            enabled.boarding() == captured_owner.boarding() &&
            enabled.starting_assembly() == captured_owner.starting_assembly() &&
            need(enabled.recovery_document()).recovery ==
                need(captured_owner.recovery_document()).recovery &&
            bytes(enabled) == bytes(resumed),
        "Actual capture/Continue/release retains exact relative constraint, "
        "tick, condition and all owners");
  const auto released = bytes(enabled);
  resumed = reopen(resumed);
  NativeFlightControls invalid;
  invalid.positive_translation.x = std::numeric_limits<double>::quiet_NaN();
  check(!enabled.advance(invalid) && bytes(enabled) == released &&
            enabled.port_approach() == aid_before &&
            enabled.surface_maneuver() == maneuver_before,
        "Invalid input preserves the complete newly selected condition voyage");
  const auto fuel = enabled.resources()->flight_quanta;
  const auto step = need(enabled.advance(held));
  const auto repeated = need(resumed.advance(held));
  check(
      bytes(enabled) == bytes(resumed) &&
          enabled.document().flight.frame.kind ==
              RigidFrameKind::planet_relative_inertial &&
          enabled.document().flight.tick == source.document().flight.tick + 1 &&
          enabled.condition()->tick == enabled.document().flight.tick &&
          enabled.condition()->forebody == 0 &&
          enabled.condition()->landing_structure == 0 &&
          !enabled.condition()->latest &&
          enabled.condition()->next_event == pristine.next_event &&
          !enabled.recovery_pending() && step.fuel_debit_quanta > 0 &&
          step.actuation.central.propulsion.positive_force_newtons.x > 0 &&
          step.actuation.central.propulsion.negative_force_newtons.y > 0 &&
          enabled.resources()->flight_quanta == fuel - step.fuel_debit_quanta &&
          step.fuel_debit_quanta == repeated.fuel_debit_quanta &&
          step.actuation.central.propulsion.positive_force_newtons ==
              repeated.actuation.central.propulsion.positive_force_newtons &&
          step.actuation.central.propulsion.negative_force_newtons ==
              repeated.actuation.central.propulsion.negative_force_newtons,
      "Actual held station-space tick resumes exactly without inventing "
      "terrain damage or loss");
  (void)reopen(enabled);
  auto spooling = source;
  need(spooling.begin_jump());
  const auto spool_bytes = bytes(spooling);
  check(!spooling.impact_consequences_available() &&
            !spooling.enable_impact_consequences() &&
            bytes(spooling) == spool_bytes && !spooling.condition(),
        "Existing active-jump guard refuses impact activation without owner "
        "loss");
  auto pending = source;
  const auto recovery = need(pending.recovery_document());
  need(pending.record_loss(
      FreedomLossCause::irrecoverable_destruction,
      pending.document().origin.state.craft, pending.document().flight.tick,
      need(freedom_recovery_source_checksum(recovery.voyage))));
  const auto pending_bytes = bytes(pending);
  check(!pending.impact_consequences_available() &&
            !pending.enable_impact_consequences() &&
            bytes(pending) == pending_bytes && !pending.condition() &&
            !pending.port_approach().active &&
            pending.surface_maneuver().kind == NativeSurfaceManeuverKind::off,
        "Existing recorded-recovery guard refuses impact activation without a "
        "new condition");
  check(bytes(source) == original, "Impact activation and guard copies never "
                                   "alter the exported source trace");
}
auto checkpoint(const NativeFreedomFlightSession& session,
                const std::filesystem::path& output, std::string_view name,
                Json& manifest) -> void {
  const auto path = output / (std::string{name} + ".json");
  need(session.save_as(path));
  const auto& document = session.document();
  const auto relative = need(session.relative_state());
  manifest["checkpoints"].push_back(
      {{"name", name},
       {"file", path.filename().string()},
       {"tick", std::to_string(document.flight.tick)},
       {"checksum", std::to_string(need(rigid_body_state_checksum(
                        {session.system()}, document.flight)))},
       {"attached", session.docking()->attached},
       {"flight_quanta", std::to_string(session.resources()->flight_quanta)},
       {"canonical", Json::parse(need(encode_rigid_body_state_json(
                         {session.system()}, document.flight)))},
       {"relative", Json::parse(need(encode_rigid_body_state_json(
                        {session.system()}, relative)))}});
}
auto refused_commands(NativeFreedomFlightSession& session) -> void {
  const auto before = bytes(session);
  const auto approach = session.port_approach();
  const auto maneuver = session.surface_maneuver();
  const auto refuses = [&](auto action, std::string_view message) {
    check(!action() && bytes(session) == before &&
              session.port_approach() == approach &&
              session.surface_maneuver() == maneuver,
          message);
  };
  NativeFlightControls bad;
  bad.positive_translation.x = std::numeric_limits<double>::quiet_NaN();
  refuses([&] { return session.advance(bad); },
          "Nonfinite system control preserves complete voyage");
  refuses([&] { return session.advance({}, SimulationSeconds{.1}); },
          "Nonfixed system clock step preserves complete voyage");
  refuses([&] { return session.set_landing_gear(false); },
          "Already stowed system gear cannot invent another handoff");
  const auto checksum = need(
      rigid_body_state_checksum({session.system()}, session.document().flight));
  refuses([&] { return session.commit_touchdown(checksum); },
          "System contact command refuses transactionally");
  refuses([&] { return session.release_surface(); },
          "System surface release refuses transactionally");
  FreedomSurfaceWalkSaveDocument outside{
      need(session.recovery_document()), 1, {}};
  const auto inside = need(encode_freedom_surface_walk_document_json(outside));
  outside.actor.emplace();
  outside.actor->frame = {RigidFrameKind::planet_fixed,
                          session.document().world->system,
                          session.document().world->planet,
                          {}};
  check(!validate_freedom_surface_walk_document(outside) &&
            !encode_freedom_surface_walk_document_json(outside) &&
            bytes(session) == before,
        "Present ground actor with canonical system flight refuses safely "
        "without changing complete voyage");
  auto malformed = Json::parse(inside);
  malformed["actor"] = {{"version", kPlanetSurfaceWalkerVersion},
                        {"actor_id", 1U},
                        {"system", session.document().world->system.value},
                        {"planet", session.document().world->planet.value},
                        {"foot_position_metres", {0., 0., 0.}},
                        {"velocity_metres_per_second", {0., 0., 0.}},
                        {"heading_radians", 0.}};
  check(!decode_freedom_surface_walk_document_json(malformed.dump()) &&
            !decode_native_save_document_json(malformed.dump()) &&
            bytes(session) == before,
        "Format30 system-flight/ground-actor corruption refuses direct and "
        "native decode without dereferencing absent planet");
}
auto run(const std::filesystem::path& output) -> void {
  // Invalid inputs precede the real station/boarding/flight route.
  invalid_selection();
  auto station = need(NativeFreedomFlightSession::open(selected_start()));
  const auto outside_bytes = bytes(station);
  check(!station.begin_port_approach() && bytes(station) == outside_bytes,
        "Existing outside/attached approach guard preserves station source");
  for (unsigned tick = 0; tick < 2960; ++tick)
    need(station.advance_walk({0, -1, 0}));
  need(station.begin_boarding());
  for (unsigned tick = 0; tick < kGameplayBoardingTicks; ++tick)
    need(station.advance_walk({}));
  check(station.boarding()->phase == FreedomBoardingPhase::seated &&
            !station.walker() && station.docking()->attached,
        "Selected route uses actual walking and completed boarding");
  need(station.set_assistance(false));
  need(station.select_jump(
      generate_first_intersystem_identities({42}).target_system));
  station =
      need(NativeFreedomFlightSession::open(need(native_select_save_document(
          NativeSaveDocument{need(station.recovery_document())}))));
  check(station.travel()->phase == FreedomJumpPhase::idle &&
            station.travel()->selected.has_value() &&
            std::holds_alternative<FreedomRecoverySaveDocument>(
                need(station.save_document())),
        "Actual selected starter retains complete recovery/travel/boarding "
        "chain");
  const auto attached_bytes = bytes(station);
  check(!station.set_landing_gear(true) && bytes(station) == attached_bytes,
        "Attached selected voyage refuses gear without changing whole source");
  const auto pilot = station.boarding();
  const auto assembly = station.starting_assembly();
  const auto origin = station.document().origin;
  const auto binding = station.craft_binding();
  auto conditioned = station;
  need(conditioned.enable_impact_consequences());
  const auto constrained = conditioned.document().flight;
  const auto pristine_condition = *conditioned.condition();
  need(conditioned.release_port());
  check(!conditioned.docking()->attached &&
            conditioned.document().flight == constrained &&
            conditioned.condition() == pristine_condition,
        "Existing attached condition can depart in its unchanged supported "
        "relative frame");

  NativeFlightControls held;
  held.positive_translation.x = .005;
  held.negative_translation.y = .025;
  held.negative_translation.z = .01;
  held.positive_rotation.y = .0001;
  NativeFlightControls braking;
  // Three withdrawal ticks deliver 3*.025*144000 force-ticks. One
  // .06*208000 positive-Y tick arrests that outward motion with a small
  // inward remainder, while the real body remains outside the collar.
  braking.positive_translation.y = .06;
  Json manifest = {
      {"schema", "apsis.native-system-release-fixtures/1"},
      {"seed", "42"},
      {"scope", "Explicit policy2 complete station release, ordinary held "
                "relative-force integration, exact Save/Continue and capture; "
                "not whole #200 qualification"},
      {"frame_policy", kFreedomReleaseSystemFramePolicy},
      {"checkpoints", Json::array()},
      {"steps",
       {{{"ticks", 3},
         {"positive_translation", {.005, 0., 0.}},
         {"negative_translation", {0., .025, .01}},
         {"positive_rotation", {0., .0001, 0.}},
         {"negative_rotation", {0., 0., 0.}}},
        {{"ticks", 2},
         {"positive_translation", {0., 0., 0.}},
         {"negative_translation", {0., 0., 0.}},
         {"positive_rotation", {0., 0., 0.}},
         {"negative_rotation", {0., 0., 0.}}},
        {{"ticks", 1},
         {"positive_translation", {0., .06, 0.}},
         {"negative_translation", {0., 0., 0.}},
         {"positive_rotation", {0., 0., 0.}},
         {"negative_rotation", {0., 0., 0.}}}}},
      {"oracle", "Existing relative atmospheric integrator, then independent "
                 "same-final-tick host ephemeris addition; p/v tolerance "
                 "4*binary64_epsilon*max(1,abs(expected),abs(host_component)); "
                 "exact craft/tick/quaternion/body spin and complete saves"}};
  checkpoint(station, output, "attached", manifest);
  const auto source_path = output / "attached.json";
  const auto source_bytes = read(source_path);
  auto live = need(
      NativeFreedomFlightSession::open(need(native_continue(source_path))));
  const auto attached = live.document().flight;
  need(live.release_port());
  check(!live.docking()->attached &&
            live.document().flight.tick == attached.tick,
        "Actual release changes attachment/frame without advancing clock");
  check_system_oracle(live, attached);
  const auto released_relative = need(live.relative_state());
  const auto host = need(resolve_planet_ephemeris(
      live.system(), *attached.frame.planet, {attached.tick, 0}));
  check(
      close(released_relative.position_metres, attached.position_metres,
            host_position(host)) &&
          close(released_relative.linear_velocity_metres_per_second,
                attached.linear_velocity_metres_per_second,
                host_velocity(host)),
      "Release roundtrip error remains within documented ephemeris tolerance");
  checkpoint(live, output, "released", manifest);
  refused_commands(live);
  auto resumed = reopen(live);
  for (unsigned tick = 0; tick < 6; ++tick) {
    const auto controls = tick < 3    ? held
                          : tick == 5 ? braking
                                      : NativeFlightControls{};
    if (tick == 5) {
      check(need(live.assess_port()).decision == OriginDockDecision::retreating,
            "Actual withdrawal must be braked before physical capture");
      port_handoff(live);
    }
    auto expected = independent_relative(live);
    const VacuumIntent intent{
        controls.positive_translation, controls.negative_translation,
        controls.positive_rotation, controls.negative_rotation, false};
    const auto normal = need(advance_atmospheric_flight(
        {live.system()}, expected, intent, live.rotation(),
        live.document().model.atmosphere, live.document().model.central));
    const auto ledger = *live.resources();
    const auto step = need(live.advance(controls));
    need(resumed.advance(controls));
    check_system_oracle(live, expected);
    check(step.actuation.central.propulsion.positive_force_newtons ==
                  normal.central.propulsion.positive_force_newtons &&
              step.actuation.central.propulsion.negative_force_newtons ==
                  normal.central.propulsion.negative_force_newtons &&
              step.actuation.central.propulsion.positive_torque_newton_metres ==
                  normal.central.propulsion.positive_torque_newton_metres &&
              !step.propulsion_refused &&
              live.resources()->flight_quanta ==
                  ledger.flight_quanta - step.fuel_debit_quanta &&
              ((tick >= 3 && tick < 5) || step.fuel_debit_quanta > 0),
          "System adapter retains actual gross channels and exact fuel debit");
    check(bytes(live) == bytes(resumed),
          "Uninterrupted and reopened system ticks retain every outer owner");
    if (tick == 1) resumed = reopen(resumed);
  }
  check(live.document().flight.orientation != RigidOrientation{} &&
            live.document().flight.angular_velocity_radians_per_second != V{},
        "Held real rotation exercises nonidentity quaternion and nonzero spin");
  checkpoint(live, output, "stepped", manifest);
  gear_handoff(live, held);
  jump_handoff(live);
  impact_handoff(live, held);
  check(
      live.boarding() == pilot && live.starting_assembly() == assembly &&
          live.craft_binding().selection() != nullptr &&
          *live.craft_binding().selection() == *binding.selection() &&
          live.document().origin.recipe == origin.recipe &&
          live.document().origin.lineage == origin.lineage &&
          live.document().origin.state.discoveries ==
              origin.state.discoveries &&
          live.document().origin.state.world_deltas ==
              origin.state.world_deltas,
      "Real system continuation preserves craft/pilot/assembly/origin history");
  check(need(live.assess_port()).decision == OriginDockDecision::capture_ready,
        "Real positive-Y braking returns actual motion to the port capture "
        "envelope");
  const auto capture_tick = live.document().flight.tick;
  need(live.capture_port());
  need(resumed.capture_port());
  check(live.docking()->attached &&
            live.document().flight.frame.kind ==
                RigidFrameKind::planet_relative_inertial &&
            live.document().flight.tick == capture_tick &&
            live.boarding() == pilot && bytes(live) == bytes(resumed),
        "Actual capture commits relative constrained body and complete seated "
        "voyage");
  live = reopen(live);
  checkpoint(live, output, "captured", manifest);
  check(
      read(source_path) == source_bytes,
      "Release/ticks/capture/Save/Continue never rewrite attached source file");
  write(output / "manifest.json", manifest.dump(2) + "\n");
}
} // namespace

auto main(int argc, char** argv) -> int {
  const bool fixtures = argc == 3 && std::string_view{argv[1]} == "--fixtures";
  if (argc != 1 && !fixtures) {
    std::cerr << "usage: native-system-release-tests [--fixtures DIRECTORY]\n";
    return 2;
  }
  const auto output = fixtures
                          ? std::filesystem::absolute(argv[2])
                          : std::filesystem::temp_directory_path() /
                                ("apsis-native-system-release-" +
                                 std::to_string(std::chrono::steady_clock::now()
                                                    .time_since_epoch()
                                                    .count()));
  try {
    std::filesystem::create_directories(output);
    run(output);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    if (!fixtures) std::filesystem::remove_all(output);
    return 1;
  }
  if (!fixtures) std::filesystem::remove_all(output);
  std::cout << checks << " native system release checks\n";
  return failures ? 1 : 0;
}
