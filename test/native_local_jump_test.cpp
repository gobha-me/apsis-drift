#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/save_file.hpp"
#include "moon_contact_fixture.hpp"

#include <cmath>
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
auto moon_marker_projection(const NativeFreedomFlightSession& session,
                            const FreedomMoonSurfaceMarkerPoint& point)
    -> void {
  const auto body = need(session.relative_state());
  auto fixed = session.surface()->moon_landed->fixed;
  fixed.tick = body.tick;
  fixed.position_metres = {point.position.x, point.position.y,
                           point.position.z};
  // Independent existing frame transform, rather than the presenter's
  // quaternion arithmetic, supplies the exact-tick target reference.
  const auto target =
      need(reframe_rigid_body(RigidBodyWorldContext{*session.moon()}, fixed,
                              {body.frame, body.tick}, *session.moon()));
  const auto view = need(session.surface_marker_view());
  check(view.current_body && view.target_craft_relative_metres &&
            view.straight_line_distance_metres,
        "Actual Moon marker has its current-body direction and range");
  const auto& relative = *view.target_craft_relative_metres;
  check(
      view.current_body &&
          std::hypot(
              relative.x - (target.position_metres.x - body.position_metres.x),
              relative.y - (target.position_metres.y - body.position_metres.y),
              relative.z -
                  (target.position_metres.z - body.position_metres.z)) < 1e-7,
      "Moon marker bearing uses the authoritative current-tick fixed frame");
  const auto& observer = session.surface_walker()
                             ? session.surface_walker()->foot_position_metres
                             : body.position_metres;
  const auto range = session.surface_walker()
                         ? std::hypot(point.position.x - observer.x,
                                      point.position.y - observer.y,
                                      point.position.z - observer.z)
                         : std::hypot(relative.x, relative.y, relative.z);
  check(view.straight_line_distance_metres == range,
        "Moon range comes from the actual outside foot or occupied craft");
}
auto moon_marker_branch(NativeFreedomFlightSession session) -> void {
  const auto before = bytes(session);
  check(!session.replace_surface_marker() && !session.clear_surface_marker() &&
            bytes(session) == before,
        "Unselected Moon metadata refuses Replace/Clear without changing Save");
  const auto tick = session.document().flight.tick;
  // Keep an independent pre-command snapshot to detect accidental fact grants.
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Immutability
  const auto entries = session.knowledge()->entries;
  const auto resources = session.resources();
  const auto actor = *session.surface_walker();
  need(session.mark_surface_marker());
  auto marked = session.knowledge()->surface_marker;
  auto point = std::get<FreedomMoonSurfaceMarkerPoint>(*marked->point);
  check(marked->version == kFreedomMoonSurfaceMarkerVersion &&
            point.system == actor.frame.system &&
            point.body == actor.frame.body &&
            point.position ==
                MoonFixedPositionMetres{actor.foot_position_metres.x,
                                        actor.foot_position_metres.y,
                                        actor.foot_position_metres.z} &&
            point.tick == tick && session.document().flight.tick == tick &&
            session.knowledge()->entries == entries &&
            session.resources() == resources &&
            !need(session.surface_marker_view()).known_body_name &&
            need(session.surface_marker_view()).straight_line_distance_metres ==
                0,
        "Moon Mark copies only the actual supported typed foot and clock, "
        "without resources, facts or a generated name");
  const auto recorded = bytes(session);
  check(!session.mark_surface_marker() && bytes(session) == recorded,
        "Moon Mark cannot overwrite a present point");
  session = continued(session);
  moon_marker_projection(session, point);
  for (unsigned i = 0; i < 30; ++i)
    need(session.advance_surface_walk({-1, 0, 0}));
  check(session.knowledge()->surface_marker == marked &&
            *need(session.surface_marker_view()).straight_line_distance_metres >
                .1,
        "Moon walking retains the fixed point while increasing its real range");
  moon_marker_projection(session, point);
  const auto before_read = bytes(session);
  static_cast<void>(need(session.surface_marker_view()));
  check(bytes(session) == before_read,
        "Reading Moon marker projection changes no complete saved state");
  need(session.replace_surface_marker());
  marked = session.knowledge()->surface_marker;
  point = std::get<FreedomMoonSurfaceMarkerPoint>(*marked->point);
  check(point.tick == session.document().flight.tick &&
            need(session.surface_marker_view()).straight_line_distance_metres ==
                0,
        "Moon Replace explicitly captures the new supported foot and tick");

  // Separately labelled supported-actor fixture: reuse the existing walker's
  // fixed eight-direction corpus, not a reached gameplay location or seed
  // search. Real top facets and complete actor admission must accept a basin.
  const auto sampler = need(MoonTerrainSampler::create(*session.moon()));
  bool basin{};
  for (const auto direction :
       {MoonFixedDirection{1, 0, 0}, MoonFixedDirection{-1, 0, 0},
        MoonFixedDirection{0, 1, 0}, MoonFixedDirection{0, -1, 0},
        MoonFixedDirection{0, 0, 1}, MoonFixedDirection{0, 0, -1},
        MoonFixedDirection{1, 1, 1}, MoonFixedDirection{-1, -1, -1}}) {
    const auto ground = need(query_moon_contact_surface(sampler, direction));
    if (ground.radial_distance_metres >= session.moon()->radius_metres)
      continue;
    auto document = need(session.surface_walk_document());
    document.actor->foot_position_metres = {
        ground.position.x, ground.position.y, ground.position.z};
    auto supported = need(NativeFreedomFlightSession::open(
        need(native_select_save_document(NativeSaveDocument{document}))));
    need(supported.replace_surface_marker());
    const auto saved_point = std::get<FreedomMoonSurfaceMarkerPoint>(
        *supported.knowledge()->surface_marker->point);
    check(
        std::hypot(saved_point.position.x, saved_point.position.y,
                   saved_point.position.z) < session.moon()->radius_metres &&
            need(supported.surface_marker_view())
                    .straight_line_distance_metres == 0,
        "Actual supported negative-relief Moon marker is not planetary water");
    static_cast<void>(continued(supported));
    basin = true;
    break;
  }
  check(basin, "Existing actual Moon direction corpus contains a dry basin");

  auto lost = session;
  need(lost.record_loss(
      FreedomLossCause::recoverable_destruction,
      lost.document().origin.state.craft, lost.document().flight.tick,
      need(rigid_body_state_checksum(RigidBodyWorldContext{*lost.moon()},
                                     lost.document().flight))));
  const auto pending_loss = bytes(lost);
  check(!lost.mark_surface_marker() && !lost.replace_surface_marker() &&
            !lost.clear_surface_marker() && bytes(lost) == pending_loss,
        "Pending Moon loss refuses marker commands atomically");
  need(lost.complete_recovery());
  check(lost.knowledge()->surface_marker == marked &&
            !need(lost.surface_marker_view()).current_body &&
            !need(lost.surface_marker_view()).known_body_name,
        "Standard home replacement retains Moon navigation metadata without "
        "a fabricated bearing or Moon name");
  static_cast<void>(continued(lost));

  // Saved planetary metadata fixture, not a claim that this local route
  // physically visited that planetary point. Actual Moon Replace must require
  // the explicit command and keep only the one newly recorded Moon point.
  auto document = need(session.surface_walk_document());
  auto& travel = std::get<FreedomTravelSaveDocument>(document.voyage.voyage);
  const auto& home = session.system().catalog.planets.front().descriptor;
  travel.voyage.knowledge.surface_marker = FreedomSurfaceMarkerState{
      1, FreedomSurfaceMarkerPoint{
             session.system().catalog.id,
             home.id,
             {static_cast<double>(home.radius.value) * 1000 + 123.5, 0, 0},
             session.document().flight.tick}};
  session = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(NativeSaveDocument{document}))));
  const auto planetary = bytes(session);
  check(!need(session.surface_marker_view()).current_body &&
            !session.mark_surface_marker() && bytes(session) == planetary,
        "Moon arrival cannot silently overwrite a retained planetary marker");
  need(session.replace_surface_marker());
  marked = session.knowledge()->surface_marker;
  check(
      marked->version == 2 &&
          std::holds_alternative<FreedomMoonSurfaceMarkerPoint>(*marked->point),
      "Explicit cross-body replacement selects one genuine Moon point");
  need(session.return_from_surface_walk());
  const auto seated = bytes(session);
  check(!session.mark_surface_marker() && !session.replace_surface_marker() &&
            bytes(session) == seated,
        "Seated Moon commands cannot invent an outside marker");
  moon_marker_projection(
      session, std::get<FreedomMoonSurfaceMarkerPoint>(*marked->point));
  need(session.release_surface());
  const BodyTarget home_target{
      session.system().catalog.id,
      body_id(session.document().origin.recipe.home_planet)};
  const auto below_departure_volume = bytes(session);
  const auto refused_home = session.select_local_jump(home_target);
  check(
      !refused_home &&
          refused_home.error().find("Departure volume is obstructed") !=
              std::string::npos &&
          refused_home.error().find("Fly clear of nearby bodies and terrain") !=
              std::string::npos &&
          bytes(session) == below_departure_volume,
      "Actual low-Moon home selection explains departure clearance without "
      "changing the complete Save, clock, fuel or jump charges");
  // The existing jump provider excludes the whole Moon relief envelope plus
  // the craft hull. Releasing gear support alone is not departure clearance.
  const auto ascent_tick = session.document().flight.tick;
  const auto ascent_fuel = session.resources()->flight_quanta;
  const auto ascent_charges = session.resources()->jump_charges;
  const double departure_radius =
      session.moon()->radius_metres + kMoonTerrainMaximumReliefMetres + 100;
  NativeFlightControls ascent;
  ascent.positive_translation.y = .5;
  bool clear{};
  for (unsigned i = 0; i < 18000; ++i) {
    const auto& position = session.document().flight.position_metres;
    if (std::hypot(position.x, position.y, position.z) > departure_radius) {
      clear = true;
      break;
    }
    const auto step = need(session.advance(ascent));
    check(!step.propulsion_refused && !session.surface()->moon_landed,
          "Actual marker ascent retains paid thrust and leaves support");
  }
  check(clear && session.document().flight.tick > ascent_tick &&
            session.resources()->flight_quanta < ascent_fuel &&
            session.resources()->jump_charges == ascent_charges &&
            session.knowledge()->surface_marker == marked,
        "Bounded actual Moon ascent reaches the unchanged departure envelope "
        "with its own clock/fuel and the exact retained marker");
  session = continued(session);
  need(session.select_local_jump(home_target));
  leg(session);
  const auto retained = need(session.surface_marker_view());
  check(!session.moon() && session.knowledge()->surface_marker == marked &&
            retained.selected && retained.point == marked->point &&
            !retained.current_body && !retained.target_craft_relative_metres &&
            !retained.straight_line_distance_metres &&
            !retained.known_body_name,
        "Actual charged home travel retains the Moon point without a false "
        "local bearing or identity");
  const auto home_tick = session.document().flight.tick;
  need(session.clear_surface_marker());
  check(session.knowledge()->surface_marker->version == 2 &&
            !session.knowledge()->surface_marker->point &&
            session.document().flight.tick == home_tick,
        "Off-body Moon Clear retains its selected version and shared clock");
  static_cast<void>(continued(session));
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
  moon_marker_branch(branch);
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
