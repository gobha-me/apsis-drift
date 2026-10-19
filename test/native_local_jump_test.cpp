#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/save_file.hpp"
#include "moon_contact_fixture.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace apsis_drift;
int checks{};
auto check(bool good, const char* text) -> void {
  ++checks;
  if (!good) throw std::runtime_error(text);
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) {
    if constexpr (std::is_same_v<E, std::string>)
      std::cerr << value.error() << '\n';
    else if constexpr (std::is_same_v<E, SaveSchemaError>)
      std::cerr << value.error().path << ": " << value.error().detail << '\n';
    throw std::runtime_error("Native local route refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto bytes(const NativeFreedomFlightSession& session) -> std::string {
  return need(encode_native_save_document_json(need(session.save_document())));
}
auto continued(const NativeFreedomFlightSession& session)
    -> NativeFreedomFlightSession {
  const auto encoded = bytes(session);
  auto next =
      need(NativeFreedomFlightSession::open(need(native_select_save_document(
          need(decode_native_save_document_json(encoded))))));
  check(bytes(next) == encoded,
        "Continue must preserve complete native voyage bytes");
  return next;
}
auto leg(NativeFreedomFlightSession& session) -> void {
  const auto charges = session.resources()->jump_charges;
  const auto attempt = session.travel()->next_attempt;
  need(session.begin_jump());
  session = continued(session);
  for (SimulationTick tick = 1; tick <= kJumpSpoolTicks + kJumpTransitTicks;
       ++tick) {
    const auto step = need(session.advance({}));
    if (tick < kJumpSpoolTicks)
      check(session.resources()->jump_charges == charges,
            "Spooling cannot bill a local charge");
    if (tick == kJumpSpoolTicks) {
      check(step.jump_committed &&
                session.resources()->jump_charges == charges - 1 &&
                session.travel()->next_attempt == attempt + 1,
            "Local commitment spends exactly one charge and attempt");
      check(!session.cancel_jump() && !session.set_assistance(false),
            "Committed local transit refuses cancellation and conflicting "
            "controls");
      session = continued(session);
    }
    if (tick > kJumpSpoolTicks) {
      check(
          step.fuel_debit_quanta == 0 &&
              session.resources()->flight_quanta ==
                  session.travel()->local->commitment_resources->flight_quanta,
          "Frozen transit advances one clock without flight fuel");
    }
    if (tick == kJumpSpoolTicks + 1 ||
        tick == kJumpSpoolTicks + kJumpTransitTicks - 1)
      session = continued(session);
    if (tick == kJumpSpoolTicks + kJumpTransitTicks)
      check(step.jump_arrived,
            "The native session installs the real scheduled local arrival");
  }
  session = continued(session);
}
auto supported_branch(const NativeFreedomFlightSession& arrival) -> void {
  // Explicit contact fixture branch: flight approach/descent is NOT claimed by
  // this placement. Native commands, actual support and outside Continue are.
  auto branch = arrival;
  need(branch.advance({}));
  auto travel = need(branch.travel_document());
  auto flight = branch.document();
  const auto contact = test_fixture::make_moon_contact_fixture();
  auto fixed = contact.fixed;
  fixed.tick = flight.flight.tick;
  flight.flight =
      need(reframe_rigid_body(RigidBodyWorldContext{contact.moon}, fixed,
                              {flight.flight.frame, fixed.tick}, contact.moon));
  travel.voyage.voyage.voyage.base = flight;
  travel.voyage.voyage.voyage.surface = {};
  branch = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(NativeSaveDocument{travel}))));
  need(branch.set_landing_gear(true));
  const auto source = need(rigid_body_state_checksum(
      RigidBodyWorldContext{*branch.moon()}, branch.document().flight));
  need(branch.commit_touchdown(source));
  check(branch.surface()->moon_landed.has_value(),
        "Actual native touchdown installs certified Moon support");
  branch = continued(branch);
  need(branch.begin_surface_walk());
  check(branch.surface_walker() &&
            branch.surface_walker()->frame.body == branch.moon()->body,
        "The native suited actor walks on its actual typed Moon");
  branch = continued(branch);
  const auto anchor = branch.surface()->moon_landed;
  const auto fuel = branch.resources()->flight_quanta;
  const auto start = branch.document().flight.tick;
  for (unsigned i = 0; i < 30; ++i)
    need(branch.advance_surface_walk({}));
  check(branch.document().flight.tick == start + 30 &&
            branch.resources()->tick == start + 30 &&
            branch.resources()->flight_quanta == fuel &&
            branch.surface()->moon_landed == anchor,
        "Outside movement owns one voyage clock and preserves the grounded "
        "ship/fuel");
  branch = continued(branch);
  need(branch.return_from_surface_walk());
  need(branch.release_surface());
  check(!branch.surface_walker() && !branch.surface()->moon_landed,
        "Same suited pilot returns and releases the actual landed craft");
  branch = continued(branch);
}
auto run() -> void {
  auto session =
      need(NativeFreedomFlightSession::open(need(native_new_game(Seed{42}))));
  check(session.walker().has_value(),
        "The route begins with the actual station actor");
  for (unsigned i = 0; i < 1194; ++i)
    need(session.advance_walk({0, -1, 0}));
  need(session.begin_boarding());
  for (unsigned i = 0; i < kGameplayBoardingTicks; ++i)
    need(session.advance_walk({}));
  check(session.boarding()->phase == FreedomBoardingPhase::seated,
        "Actual station walk reaches the occupied cockpit");
  need(session.release_port());
  NativeFlightControls withdraw{};
  withdraw.negative_translation.y = .25;
  for (unsigned i = 0; i < 720; ++i)
    need(session.advance(withdraw));
  const auto craft = session.document().origin.state.craft;
  const auto binding = session.starting_assembly();
  const auto model = session.document().model;
  const auto knowledge = session.knowledge();
  const auto catalog = need(generate_secondary_body_catalog(session.system()));
  const BodyTarget moon{session.system().catalog.id, catalog.bodies[0].node.id};
  need(session.select_local_jump(moon));
  const auto before = bytes(session);
  auto bad = NativeFlightControls{};
  bad.positive_translation.x = std::numeric_limits<double>::quiet_NaN();
  need(session.begin_jump());
  const auto spooling = bytes(session);
  check(!session.advance(bad) && bytes(session) == spooling,
        "Invalid local controls preserve source, ledger and phase");
  need(session.advance({}));
  need(session.cancel_jump());
  check(session.resources()->jump_charges == 3 &&
            session.travel()->next_attempt == 1,
        "Canceled local spool consumes no charge or attempt");
  check(!before.empty(), "Local selection saves the complete voyage");
  leg(session);
  check(session.moon() && session.moon()->body == moon.body &&
            !session.document().world && session.document().moon &&
            session.resources()->jump_charges == 2 &&
            session.document().origin.state.craft == craft &&
            session.starting_assembly() == binding,
        "Real local arrival retains the same craft and binding on the actual "
        "Moon");
  check(session.knowledge()->entries == knowledge->entries,
        "Local travel preserves existing survey evidence");
  supported_branch(session);
  const BodyTarget home{session.system().catalog.id,
                        body_id(session.document().origin.recipe.home_planet)};
  need(session.select_local_jump(home));
  leg(session);
  check(!session.moon() && session.document().world &&
            session.document().model == model &&
            session.resources()->jump_charges == 1 &&
            session.travel()->next_attempt == 3 &&
            session.document().origin.state.craft == craft &&
            session.boarding()->phase == FreedomBoardingPhase::seated,
        "Return installs the original genuine home profile and leaves one "
        "reserve charge");
  need(session.select_port(1));
  check(session.assess_port().has_value(),
        "Returned craft can select its real station approach");
  session = continued(session);
}
} // namespace
int main() {
  try {
    run();
    std::cout << checks << " native local-route checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
