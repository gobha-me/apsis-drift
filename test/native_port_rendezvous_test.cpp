#include "apsis_drift/godot/saved_flight.hpp"
#include "native_minor_route_fixture.hpp"

#include <array>
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
template <class F>
auto refuses(NativeFreedomFlightSession& s, F action, const char* why) -> void {
  const auto saved = bytes(s);
  // The snapshot must survive action mutation.
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Snapshot.
  const auto aid = s.port_rendezvous();
  check(!action() && bytes(s) == saved && s.port_rendezvous() == aid, why);
}
auto returned(bool assisted) -> NativeFreedomFlightSession {
  auto s = departure();
  need(s.set_assistance(assisted));
  need(s.select_local_jump(minor_target(s)));
  leg(s);
  refuses(
      s, [&] { return s.begin_port_rendezvous(); },
      "A genuine minor owner cannot arm Origin rendezvous");
  need(s.select_local_jump(home_target(s)));
  leg(s);
  check(!s.moon() && !s.minor() && s.document().model.assistance == assisted &&
            s.resources()->jump_charges == 1,
        "Actual charged return preserves home assistance and real reserve "
        "charge");
  return s;
}
auto admission_and_cancellation(const NativeFreedomFlightSession& source)
    -> void {
  auto s = source;
  refuses(
      s, [&] { return s.begin_port_rendezvous(); },
      "Unselected port cannot arm rendezvous");
  need(s.select_port(1));
  const auto before = bytes(s);
  const auto timer = s.port_rendezvous();
  need(s.port_rendezvous_available());
  check(bytes(s) == before && s.port_rendezvous() == timer,
        "Rendezvous availability is a pure actual-owner query");
  need(s.begin_port_rendezvous());
  check(s.port_rendezvous().active &&
            s.port_rendezvous().remaining_ticks == kNativePortRendezvousTicks &&
            bytes(s) == before &&
            s.document().model.assistance == source.document().model.assistance,
        "Arming is zero-time/save-neutral and preserves actual assistance");
  auto loaded = continued(s);
  check(!loaded.port_rendezvous().active && !loaded.port_approach().active,
        "Continue retains exact physical/history/resource bytes with transient "
        "guidance off");
  for (const auto value :
       {std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), -0.01, 1.01}) {
    NativeFlightControls invalid{};
    invalid.positive_translation.x = value;
    refuses(
        s, [&] { return s.advance(invalid); },
        "Invalid input cannot cancel guidance or change clock/resources");
  }
  for (const auto seconds :
       {-1.0, 0.0, 0.1, std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity()})
    refuses(
        s, [&] { return s.advance({}, SimulationSeconds{seconds}); },
        "Invalid fixed step preserves whole active rendezvous state");
  auto manual = s;
  auto direct = continued(s);
  NativeFlightControls demand{};
  demand.positive_translation.x = .01;
  const auto step = need(manual.advance(demand));
  const auto direct_step = need(direct.advance(demand));
  check(!manual.port_rendezvous().active && bytes(manual) == bytes(direct) &&
            step.fuel_debit_quanta == direct_step.fuel_debit_quanta,
        "Valid manual demand cancels aid and commits exactly ordinary "
        "motion/fuel");
  s.cancel_port_rendezvous();
  check(!s.port_rendezvous().active && bytes(s) == before,
        "Explicit cancellation changes no physical/history/resource state");
  need(s.begin_port_rendezvous());
  need(s.select_port(1));
  check(!s.port_rendezvous().active,
        "Explicit port reselection clears the old transient attempt");
  need(s.begin_port_rendezvous());
  need(s.set_assistance(!s.document().model.assistance));
  check(!s.port_rendezvous().active,
        "User assistance change cannot retain cached torque/rate-mode demands");
  auto gear = source;
  need(gear.select_port(1));
  need(gear.set_landing_gear(true));
  refuses(
      gear, [&] { return gear.begin_port_rendezvous(); },
      "Deployed gear refuses rendezvous atomically");
  auto held = source;
  need(held.select_port(1));
  need(held.hold_current_orbit());
  refuses(
      held, [&] { return held.begin_port_rendezvous(); },
      "Orbit hold and rendezvous cannot own thrust together");
  auto jumping = source;
  need(jumping.select_port(1));
  need(jumping.select_local_jump(minor_target(jumping)));
  need(jumping.begin_jump());
  refuses(
      jumping, [&] { return jumping.begin_port_rendezvous(); },
      "A real jump spool refuses rendezvous without side effects");
  auto lost = source;
  const auto recovery = need(lost.recovery_document());
  need(lost.record_loss(
      FreedomLossCause::recoverable_destruction,
      lost.document().origin.state.craft, lost.document().flight.tick,
      need(freedom_recovery_source_checksum(recovery.voyage))));
  refuses(
      lost, [&] { return lost.begin_port_rendezvous(); },
      "Pending loss refuses rendezvous without physical or transient effects");
}
// Explicit valid-save scalar refusal fixtures, never flown trajectories or
// evidence that the player physically reached these unsupported poses.
auto pose_fixture(const NativeFreedomFlightSession& source, RigidVector3 offset,
                  RigidVector3 velocity = {}, RigidVector3 spin = {},
                  bool aligned = true) -> NativeFreedomFlightSession {
  auto idle = source;
  need(
      idle.advance({})); // Leave the exact-arrival equality boundary genuinely.
  need(idle.select_port(1));
  auto d = need(idle.travel_document());
  auto* boarding =
      std::get_if<FreedomBoardingSaveDocument>(&d.voyage.voyage.voyage.base);
  ensure(boarding != nullptr,
         "Scalar refusal fixture must retain its actual seated boarding owner");
  auto& flight = boarding->voyage.flight;
  const auto station =
      generate_origin_station(flight.origin.recipe.universe_seed);
  const auto geometry = need(origin_station_geometry(station));
  auto local =
      need(resolve_origin_port_pose(idle.system(), station, geometry,
                                    {station.id, 1}, flight.flight.tick))
          .station_relative;
  local.craft = flight.flight.craft;
  local.position_metres.x += offset.x;
  local.position_metres.y += offset.y;
  local.position_metres.z += offset.z;
  local.linear_velocity_metres_per_second = velocity;
  local.angular_velocity_radians_per_second = spin;
  if (!aligned) local.orientation = {};
  const RigidBodyWorldContext context{idle.system(), &station};
  const auto global = need(reframe_rigid_body(
      context, local,
      {{RigidFrameKind::system_inertial, idle.system().catalog.id, {}, {}},
       local.tick}));
  flight.flight = need(
      reframe_rigid_body(context, global, {flight.flight.frame, local.tick}));
  return need(NativeFreedomFlightSession::open(
      need(native_select_save_document(NativeSaveDocument{d}))));
}
auto bounds(const NativeFreedomFlightSession& source) -> void {
  for (const auto offset :
       {RigidVector3{0, -50001, 0}, RigidVector3{5001, -1000, 0},
        RigidVector3{0, 1000, 0}, RigidVector3{0, -149, 0}}) {
    auto s = pose_fixture(source, offset);
    refuses(
        s, [&] { return s.begin_port_rendezvous(); },
        "Unsupported range/lateral/approach-side/near-region pose refuses "
        "atomically");
  }
  auto fast = pose_fixture(source, {0, -1000, 0}, {41, 0, 0});
  refuses(
      fast, [&] { return fast.begin_port_rendezvous(); },
      "Unsupported station-relative speed refuses admission");
  auto spin = pose_fixture(source, {0, -1000, 0}, {}, {.021, 0, 0});
  refuses(
      spin, [&] { return spin.begin_port_rendezvous(); },
      "Unsupported body spin refuses admission");
  auto alignment = pose_fixture(source, {0, -1000, 0}, {}, {}, false);
  refuses(
      alignment, [&] { return alignment.begin_port_rendezvous(); },
      "Unsupported attitude refuses admission");
  auto empty = source;
  need(empty.advance({}));
  need(empty.select_port(1));
  auto document = need(empty.travel_document());
  document.voyage.voyage.resources.flight_quanta = 0;
  empty = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(NativeSaveDocument{document}))));
  refuses(
      empty, [&] { return empty.begin_port_rendezvous(); },
      "Labelled empty-resource fixture refuses powered guidance without a "
      "gift");
}
auto cadence(const NativeFreedomFlightSession& source) -> void {
  auto armed = source;
  need(armed.select_port(1));
  need(armed.begin_port_rendezvous());
  auto oracle = armed;
  constexpr unsigned ticks = 480;
  for (unsigned i = 0; i < ticks; ++i)
    need(oracle.advance({}));
  const auto expected = bytes(oracle);
  const std::array<double, 12> neutral{};
  for (const unsigned width : {1U, 2U, 8U}) {
    SavedFlightWorld world{armed, {}, {}, 0};
    for (unsigned i = 0; i < ticks / width; ++i)
      world.advance(kSimulationStep.count() * width, neutral, false);
    check(bytes(world.session) == expected &&
              world.session.port_rendezvous() == oracle.port_rendezvous() &&
              world.dropped_seconds == 0,
          "One/two/eight fixed-tick presentation batches preserve exact "
          "guidance/body/fuel/cadence");
    const auto paused = bytes(world.session);
    world.advance(.1, neutral, true);
    check(bytes(world.session) == paused,
          "Paused presentation batch does not consume rendezvous time or "
          "resources");
  }
}
auto actual_route(bool assisted) -> void {
  auto s = returned(assisted);
  if (!assisted) {
    admission_and_cancellation(s);
    bounds(s);
  }
  cadence(s);
  need(s.select_port(1));
  const auto source = bytes(s);
  const auto start = s.document().flight.tick;
  const auto charges = s.resources()->jump_charges;
  const auto first_fuel = s.resources()->flight_quanta;
  const auto metadata = *s.knowledge();
  need(s.begin_port_rendezvous());
  unsigned ticks{};
  std::uint64_t bill{};
  while (s.port_rendezvous().active && ticks < kNativePortRendezvousTicks) {
    const auto before = s.document().flight.tick;
    const auto fuel = s.resources()->flight_quanta;
    const auto step = need(s.advance({}));
    ++ticks;
    bill += step.fuel_debit_quanta;
    check(!step.propulsion_refused && !s.recovery_pending() &&
              !s.docking()->attached &&
              s.document().flight.tick == before + 1 &&
              s.resources()->tick == before + 1 &&
              fuel - s.resources()->flight_quanta == step.fuel_debit_quanta &&
              s.resources()->jump_charges == charges &&
              s.document().model.assistance == assisted,
          "Every actual far tick owns one clock and real debit with no "
          "capture/charge/profile rewrite");
  }
  check(ticks > 0 && ticks < kNativePortRendezvousTicks &&
            !s.port_rendezvous().active && s.port_approach().active &&
            s.port_approach().remaining_ticks == kNativePortApproachTicks &&
            s.port_approach_available() && !s.docking()->attached &&
            s.document().flight.tick == start + ticks && bill > 0 &&
            first_fuel - s.resources()->flight_quanta == bill,
        "Actual real-thruster rendezvous reaches unchanged near gate before "
        "its first tick and without auto capture");
  check(s.knowledge()->surface_marker == metadata.surface_marker &&
            s.knowledge()->surface_landmarks == metadata.surface_landmarks &&
            s.knowledge()->moon_surface_landmarks ==
                metadata.moon_surface_landmarks,
        "Physical return guidance retains personal marker and both inspection "
        "components");
  auto near_copy = continued(s);
  check(!near_copy.port_rendezvous().active &&
            !near_copy.port_approach().active &&
            near_copy.port_approach_available(),
        "Near snapshot Continue preserves exact reached pose while dropping "
        "both transient aids");
  unsigned close{};
  for (; close < kNativePortApproachTicks; ++close) {
    if (need(s.assess_port()).decision == OriginDockDecision::capture_ready)
      break;
    const auto step = need(s.advance({}));
    check(
        !step.propulsion_refused && !s.docking()->attached &&
            s.resources()->jump_charges == charges,
        "Unchanged close aid pays ordinary propulsion and never auto captures");
  }
  check(close < kNativePortApproachTicks &&
            need(s.assess_port()).decision == OriginDockDecision::capture_ready,
        "Existing near controller reaches explicit capture gate");
  const auto capture_tick = s.document().flight.tick;
  const auto pre_capture_fuel = s.resources()->flight_quanta;
  need(s.capture_port());
  check(s.docking()->attached && !s.port_approach().active &&
            !s.port_rendezvous().active &&
            s.document().flight.tick == capture_tick &&
            s.resources()->flight_quanta == pre_capture_fuel &&
            s.resources()->jump_charges == charges,
        "Only explicit capture attaches, without service or clock advancement");
  need(s.replenish_resources());
  check(s.document().flight.tick == capture_tick &&
            s.resources()->flight_quanta == kFreedomFlightCapacityQuanta &&
            s.resources()->jump_charges == 3,
        "Only actual attached service replenishes resources at the real "
        "capture tick");
  static_cast<void>(continued(s));
  check(!source.empty(), "Actual return source was retained before guidance");
  std::cout << (assisted ? "assisted" : "advanced") << " far_ticks=" << ticks
            << " near_ticks=" << close << " fuel_quanta=" << bill << '\n';
}
} // namespace
int main() {
  try {
    auto outside =
        need(NativeFreedomFlightSession::open(need(native_new_game(Seed{42}))));
    refuses(
        outside, [&] { return outside.begin_port_rendezvous(); },
        "Station walking is not seated free-flight rendezvous");
    actual_route(false);
    actual_route(true);
    std::cout << checks << " native port rendezvous checks, 0 failures\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Native rendezvous failure after " << checks
              << " checks: " << e.what() << '\n';
    return 1;
  }
}
