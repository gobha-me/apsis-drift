#include "apsis_drift/godot/saved_flight.hpp"
#include "native_minor_route_fixture.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace apsis_drift;
using namespace apsis_drift::minor_route_fixture;
using godot_spike::SavedFlightWorld;
unsigned checks{};
auto check(bool good, const char* why) -> void {
  ++checks;
  ensure(good, why);
}
auto length(RigidVector3 v) -> double {
  return std::hypot(v.x, v.y, v.z);
}
template <class F>
auto refuses(NativeFreedomFlightSession& s, F action, const char* why) -> void {
  const auto saved = bytes(s);
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Snapshot.
  const auto docking = s.port_docking();
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Snapshot.
  const auto approach = s.port_approach();
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Snapshot.
  const auto rendezvous = s.port_rendezvous();
  check(!action() && bytes(s) == saved && s.port_docking() == docking &&
            s.port_approach() == approach && s.port_rendezvous() == rendezvous,
        why);
}
auto released() -> NativeFreedomFlightSession {
  auto s =
      need(NativeFreedomFlightSession::open(need(native_new_game(Seed{42}))));
  for (unsigned i = 0; i < 1194; ++i)
    need(s.advance_walk({0, -1, 0}));
  need(s.begin_boarding());
  for (unsigned i = 0; i < kGameplayBoardingTicks; ++i)
    need(s.advance_walk({}));
  need(s.release_port());
  return s;
}
// Explicit valid-save initial-condition fixtures, not claims of flown D2
// arrivals or misalignment trajectories. They retain the real boarded pilot,
// universe, shared clock, resources and knowledge from the command-driven
// source.
auto fixture(const NativeFreedomFlightSession& source, unsigned port,
             bool assisted, RigidVector3 offset = {.4, -12, .25},
             double yaw = .08, RigidVector3 velocity = {.03, 0, -.02},
             RigidVector3 spin = {0, .003, 0}, bool empty = false)
    -> NativeFreedomFlightSession {
  auto idle = source;
  need(idle.set_assistance(assisted));
  need(idle.select_port(port));
  auto document = need(idle.save_document());
  auto* known = std::get_if<FreedomKnowledgeSaveDocument>(&document);
  ensure(known != nullptr, "Docking fixture requires the real knowledge owner");
  auto* boarding =
      std::get_if<FreedomBoardingSaveDocument>(&known->voyage.voyage.base);
  ensure(boarding != nullptr, "Docking fixture requires the real seated pilot");
  auto& flight = boarding->voyage.flight;
  const auto station =
      generate_origin_station(flight.origin.recipe.universe_seed);
  const auto geometry = need(origin_station_geometry(station));
  auto local =
      need(resolve_origin_port_pose(idle.system(), station, geometry,
                                    {station.id, port}, flight.flight.tick))
          .station_relative;
  local.craft = flight.flight.craft;
  local.position_metres.x += offset.x;
  local.position_metres.y += offset.y;
  local.position_metres.z += offset.z;
  const auto q = local.orientation;
  const auto c = std::cos(yaw / 2), sn = std::sin(yaw / 2);
  local.orientation = {c * q.w - sn * q.y, c * q.x + sn * q.z,
                       c * q.y + sn * q.w, c * q.z - sn * q.x};
  local.linear_velocity_metres_per_second = velocity;
  local.angular_velocity_radians_per_second = spin;
  const RigidBodyWorldContext context{idle.system(), &station};
  const auto system_body = reframe_rigid_body(
      context, local,
      {{RigidFrameKind::system_inertial, idle.system().catalog.id, {}, {}},
       local.tick});
  ensure(system_body.has_value(),
         "Docking fixture station-to-system handoff refused");
  const auto planet_body =
      reframe_rigid_body(context, *system_body,
                         {{RigidFrameKind::planet_relative_inertial,
                           idle.system().catalog.id,
                           station.orbit.host_planet,
                           {}},
                          local.tick});
  ensure(planet_body.has_value(),
         "Docking fixture system-to-planet handoff refused");
  flight.flight = *planet_body;
  if (empty) known->voyage.resources.flight_quanta = 0;
  return need(NativeFreedomFlightSession::open(
      need(native_select_save_document(document))));
}
auto boundaries(const NativeFreedomFlightSession& source) -> void {
  auto outside =
      need(NativeFreedomFlightSession::open(need(native_new_game(Seed{42}))));
  refuses(
      outside, [&] { return outside.begin_port_docking(); },
      "A walking pilot cannot arm powered docking");
  for (const auto offset : {RigidVector3{0, -151, 0}, RigidVector3{0, .5, 0},
                            RigidVector3{0, -12, 4.9}}) {
    auto s = fixture(source, 1, false, offset, 0, {}, {});
    refuses(
        s, [&] { return s.begin_port_docking(); },
        "Range, approach-side and whole-hull corridor refuse atomically");
  }
  auto angle = fixture(source, 1, false, {0, -12, 0}, .36, {}, {});
  refuses(
      angle, [&] { return angle.begin_port_docking(); },
      "Excessive initial misalignment refuses atomically");
  auto fast = fixture(source, 1, false, {0, -12, 0}, 0, {5.1, 0, 0}, {});
  refuses(
      fast, [&] { return fast.begin_port_docking(); },
      "Excessive station-relative speed refuses atomically");
  auto spin = fixture(source, 1, false, {0, -12, 0}, 0, {}, {0, .021, 0});
  refuses(
      spin, [&] { return spin.begin_port_docking(); },
      "Excessive initial spin refuses atomically");
  auto empty = fixture(source, 1, false, {0, -12, 0}, 0, {}, {}, true);
  refuses(
      empty, [&] { return empty.begin_port_docking(); },
      "Empty flight fuel refuses powered docking without a gift");
  auto gear = fixture(source, 1, false);
  need(gear.set_landing_gear(true));
  refuses(
      gear, [&] { return gear.begin_port_docking(); },
      "Deployed gear cannot enter the stowed-hull docking action");
  auto held = fixture(source, 1, false);
  need(held.hold_current_orbit());
  refuses(
      held, [&] { return held.begin_port_docking(); },
      "Orbit hold and docking cannot own thrust together");
  auto jumping = fixture(source, 1, false);
  need(jumping.select_local_jump(minor_target(jumping)));
  need(jumping.begin_jump());
  refuses(
      jumping, [&] { return jumping.begin_port_docking(); },
      "An actual jump spool refuses docking without canceling the jump");
  auto lost = fixture(source, 1, false);
  const auto recovery = need(lost.recovery_document());
  need(lost.record_loss(
      FreedomLossCause::recoverable_destruction,
      lost.document().origin.state.craft, lost.document().flight.tick,
      need(freedom_recovery_source_checksum(recovery.voyage))));
  refuses(
      lost, [&] { return lost.begin_port_docking(); },
      "Pending recorded loss cannot arm docking or rewrite recovery");
  auto minor = departure();
  need(minor.select_local_jump(minor_target(minor)));
  leg(minor);
  refuses(
      minor, [&] { return minor.begin_port_docking(); },
      "A genuinely arrived minor owner cannot acquire an Origin docking "
      "action");
}
auto cancellation(const NativeFreedomFlightSession& source) -> void {
  auto s = fixture(source, 1, false);
  const auto saved = bytes(s);
  const auto off = s.port_docking();
  need(s.port_docking_available());
  check(bytes(s) == saved && s.port_docking() == off,
        "Availability is a pure actual-world query");
  need(s.begin_port_docking());
  check(s.port_docking().active &&
            s.port_docking().remaining_ticks == kNativePortDockingTicks &&
            s.port_docking().phase == NativePortDockingPhase::aligning &&
            bytes(s) == saved,
        "Arming changes only bounded transient state, not the relative fixture "
        "Save");
  auto loaded = continued(s);
  check(
      !loaded.port_docking().active && !loaded.port_approach().active &&
          !loaded.port_rendezvous().active && bytes(loaded) == saved,
      "Continue preserves the complete voyage and resets every transient aid");
  for (const auto invalid :
       {std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(), -.01, 1.01}) {
    for (unsigned channel = 0; channel < 4; ++channel) {
      NativeFlightControls command{};
      const std::array fields{
          &command.positive_translation.x, &command.negative_translation.z,
          &command.positive_rotation.y, &command.negative_rotation.x};
      *fields[channel] = invalid;
      refuses(
          s, [&] { return s.advance(command); },
          "Invalid or nonfinite thrust/torque preserves the whole active "
          "action");
    }
  }
  for (const auto seconds :
       {-1.0, 0.0, .1, std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity()})
    refuses(
        s, [&] { return s.advance({}, SimulationSeconds{seconds}); },
        "Invalid fixed step cannot spend fuel/time or cancel docking");
  auto manual = s;
  auto direct = continued(s);
  NativeFlightControls command{};
  command.negative_rotation.z = .01;
  command.positive_translation.x = .02;
  const auto a = need(manual.advance(command));
  const auto b = need(direct.advance(command));
  check(!manual.port_docking().active && bytes(manual) == bytes(direct) &&
            a.fuel_debit_quanta == b.fuel_debit_quanta,
        "Valid manual demand cancels docking and commits exactly ordinary "
        "physics/fuel");
  s.cancel_port_docking();
  check(!s.port_docking().active && bytes(s) == saved,
        "Explicit cancellation changes no physical state or saved voyage");
  need(s.begin_port_docking());
  need(s.select_port(1));
  check(!s.port_docking().active,
        "Explicit port selection clears the old docking action");
  need(s.begin_port_docking());
  need(s.set_assistance(true));
  check(!s.port_docking().active,
        "Changing assistance clears old docking torque ownership");
  auto aligned = fixture(source, 1, false, {0, -12, 0}, 0, {}, {});
  need(aligned.begin_port_approach());
  need(aligned.begin_port_docking());
  check(aligned.port_docking().active && !aligned.port_approach().active &&
            !aligned.port_rendezvous().active,
        "Docking replaces the old near aid without competing commands");
  need(aligned.begin_port_approach());
  check(!aligned.port_docking().active && aligned.port_approach().active,
        "Explicit old approach replaces docking and retains manual-capture "
        "semantics");
  auto far = fixture(source, 1, false, {0, -160, 0}, 0, {}, {});
  need(far.begin_port_rendezvous());
  refuses(
      far, [&] { return far.begin_port_docking(); },
      "An inadmissible close action cannot cancel the active far rendezvous");
}
auto cadence(const NativeFreedomFlightSession& source) -> void {
  auto armed = fixture(source, 2, true);
  need(armed.begin_port_docking());
  auto oracle = armed;
  constexpr unsigned ticks = 240;
  for (unsigned i = 0; i < ticks; ++i)
    need(oracle.advance({}));
  const auto expected = bytes(oracle);
  const std::array<double, 12> neutral{};
  for (const unsigned width : {1U, 2U, 8U}) {
    SavedFlightWorld world{armed, {}, {}, 0};
    for (unsigned i = 0; i < ticks / width; ++i)
      world.advance(kSimulationStep.count() * width, neutral, false);
    check(bytes(world.session) == expected &&
              world.session.port_docking() == oracle.port_docking() &&
              world.dropped_seconds == 0,
          "One/two/eight-tick frame batches preserve exact "
          "docking/physics/fuel state");
    const auto aid = world.session.port_docking();
    world.advance(.1, neutral, true);
    check(bytes(world.session) == expected &&
              world.session.port_docking() == aid,
          "Paused frame spends no docking budget, fuel or simulation time");
  }
}
auto convergence(const NativeFreedomFlightSession& source, unsigned port,
                 bool assisted) -> void {
  const double outward = port == 1 ? 120 : 12;
  auto s = fixture(source, port, assisted, {.4, -outward, .25});
  const auto initial = need(s.assess_port());
  check(initial.separation_metres > outward - 1 &&
            initial.alignment_radians > .05 && !s.docking()->attached,
        "Convergence starts offset, tilted and free, not already at the latch");
  need(s.begin_port_docking());
  const auto start = s.document().flight.tick;
  const auto fuel = s.resources()->flight_quanta;
  const auto charges = s.resources()->jump_charges;
  const auto marker = s.knowledge()->surface_marker;
  unsigned ticks{};
  std::uint64_t bill{};
  bool force{}, torque{}, aligned{}, closing{};
  while (s.port_docking().active && ticks < kNativePortDockingTicks) {
    const auto tick = s.document().flight.tick;
    const auto before_fuel = s.resources()->flight_quanta;
    const auto step = need(s.advance({}));
    ++ticks;
    bill += step.fuel_debit_quanta;
    const auto& actual = step.actuation.central.propulsion;
    force = force || length(actual.applied_force_newtons) > 0;
    torque = torque || length(actual.applied_torque_newton_metres) > 0;
    aligned =
        aligned || s.port_docking().phase == NativePortDockingPhase::aligning;
    closing =
        closing || s.port_docking().phase == NativePortDockingPhase::closing;
    check(!step.propulsion_refused && s.document().flight.tick == tick + 1 &&
              s.resources()->tick == tick + 1 &&
              before_fuel - s.resources()->flight_quanta ==
                  step.fuel_debit_quanta &&
              s.resources()->jump_charges == charges &&
              s.document().model.assistance == assisted &&
              (!s.docking()->attached ||
               s.port_docking().phase == NativePortDockingPhase::complete),
          "Each docking tick owns actual fuel, one clock and unchanged "
          "profile/charges; no early attachment");
  }
  if (!s.docking()->attached)
    std::cerr << "Docking stopped: port=" << port << " assisted=" << assisted
              << " ticks=" << ticks << " note=" << s.port_docking().note
              << '\n';
  check(ticks > 1 && ticks < kNativePortDockingTicks && s.docking()->attached &&
            s.port_docking().phase == NativePortDockingPhase::complete &&
            s.port_docking().latch_assessment && force && torque && aligned &&
            closing && bill > 0 &&
            fuel - s.resources()->flight_quanta == bill &&
            s.document().flight.tick == start + ticks,
        "Tilted D1/D2 docking physically aligns, translates and brakes before "
        "automatic capture");
  const auto& latch = *s.port_docking().latch_assessment;
  check(latch.port.ordinal == port && latch.tick == s.document().flight.tick &&
            latch.decision == OriginDockDecision::capture_ready &&
            latch.hull_inside_reservation &&
            latch.separation_metres <= kNativePortDockingLatchMetres &&
            latch.alignment_radians <= kNativePortDockingLatchRadians &&
            length(latch.collar_velocity_station_metres_per_second) <=
                kNativePortDockingLatchSpeed &&
            latch.angular_speed_radians_per_second <=
                kNativePortDockingLatchAngularSpeed,
        "Stored actual pre-latch measurement meets millimetre/submilliradian "
        "gates and old capture contract");
  check(
      s.knowledge()->surface_marker == marker && !s.port_approach().active &&
          !s.port_rendezvous().active,
      "Physical capture preserves personal marker and leaves no competing aid");
  auto loaded = continued(s);
  check(loaded.docking()->attached && !loaded.port_docking().active &&
            loaded.port_docking().phase == NativePortDockingPhase::off,
        "Attached Save/Continue preserves the canonical constraint, not a "
        "saved docking controller");
  std::cout << "port=" << port << " assisted=" << assisted << " ticks=" << ticks
            << " fuel=" << bill << " latch_m=" << latch.separation_metres
            << " latch_rad=" << latch.alignment_radians << '\n';
}
} // namespace
auto main() -> int {
  try {
    const auto source = released();
    boundaries(source);
    cancellation(source);
    cadence(source);
    for (const auto assisted : {false, true})
      for (const unsigned port : {1U, 2U})
        convergence(source, port, assisted);
    std::cout << checks << " native smooth port docking checks, 0 failures\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Native smooth docking failure after " << checks
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
