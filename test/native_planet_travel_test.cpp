#include "../src/godot/contact_geometry_internal.hpp"
#include "apsis_drift/godot/saved_flight.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/planet_surface_walker.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace apsis_drift;
unsigned checks{};
auto check(bool value, const char* message) -> void {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) {
    if constexpr (std::is_same_v<E, std::string>)
      std::cerr << value.error() << '\n';
    else if constexpr (requires { value.error().detail; })
      std::cerr << value.error().detail << '\n';
    else if constexpr (std::is_enum_v<E>)
      std::cerr << "Refusal " << static_cast<unsigned>(value.error()) << '\n';
    throw std::runtime_error("Native planetary fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto bytes(const NativeFreedomFlightSession& s) -> std::string {
  return need(encode_native_save_document_json(need(s.save_document())));
}
auto continued(const NativeFreedomFlightSession& s)
    -> NativeFreedomFlightSession {
  const auto text = bytes(s);
  auto result =
      need(NativeFreedomFlightSession::open(need(native_select_save_document(
          need(decode_native_save_document_json(text))))));
  check(bytes(result) == text,
        "Continue preserves the complete selected voyage bytes");
  return result;
}
auto initial_source() -> FreedomKnowledgeSaveDocument {
  auto s = need(NativeFreedomFlightSession::open(
      need(native_planetary_network_new_game({42}))));
  for (unsigned t = 0; t < 1194; ++t)
    need(s.advance_walk({0, -1, 0}));
  need(s.begin_boarding());
  for (SimulationTick t = 0; t < kGameplayBoardingTicks; ++t)
    need(s.advance_walk({}));
  need(s.release_port());
  auto document =
      std::get<FreedomKnowledgeSaveDocument>(need(s.save_document()));
  auto& boarded =
      std::get<FreedomBoardingSaveDocument>(document.voyage.voyage.base);
  auto& flight = boarded.voyage.flight;
  const auto home = need(
      find_local_system_planet(s.system(), flight.origin.recipe.home_planet));
  // Explicit saved-space test boundary, not a flown station ascent. Retain the
  // actual native boarded pilot, owner, clock, model, resources and knowledge.
  flight.flight.position_metres = {
      0, 0, static_cast<double>(home->descriptor.radius.value) * 10000};
  flight.flight.linear_velocity_metres_per_second = {};
  flight.flight.angular_velocity_radians_per_second = {};
  flight.flight = need(canonicalize_rigid_body_state(
      RigidBodyWorldContext{s.system()}, flight.flight));
  return document;
}
auto fixture() -> NativeFreedomFlightSession {
  static const auto source = initial_source();
  return need(NativeFreedomFlightSession::open(
      need(native_select_save_document(source))));
}
auto current_target(const NativeFreedomFlightSession& s) -> BodyTarget {
  return {s.system().catalog.id, body_id(*s.document().flight.frame.planet)};
}
auto other_planet(const NativeFreedomFlightSession& s) -> BodyTarget {
  const auto rows = need(resolve_freedom_known_planets(
      *s.knowledge(), current_target(s), s.document().flight.tick));
  for (const auto& row : rows)
    if (!row.current) return row.target;
  throw std::runtime_error("Expected another known ordinary planet");
}
auto companion(const NativeFreedomFlightSession& s, bool minor) -> BodyTarget {
  const auto source = need(generate_physical_origin_system({42}, 2));
  const auto catalog = need(generate_secondary_body_catalog(source));
  check(s.document().origin.recipe.universe_seed == Seed{42},
        "Fixed companion fixture seed remains42");
  return {source.catalog.id, catalog.bodies[minor ? 1 : 0].node.id};
}
auto atomic_refusal(NativeFreedomFlightSession& s, BodyTarget target) -> void {
  const auto before = bytes(s);
  const auto available = s.planet_jump_selection_available(target);
  check(!available && bytes(s) == before, "Refused planet query is read-only");
  const auto selected = s.select_planet_jump(target);
  check(!selected && selected.error() == available.error() &&
            bytes(s) == before,
        "Refused planet selection is atomic and shares the query's actual "
        "policy");
}
auto select_planet(NativeFreedomFlightSession& s, BodyTarget target) -> void {
  const auto before = s.document();
  const auto resources = s.resources();
  const auto knowledge = s.knowledge();
  const auto pilot = s.boarding();
  const auto encoded = bytes(s);
  need(s.planet_jump_selection_available(target));
  check(bytes(s) == encoded,
        "Available planet query never adopts or mutates a save");
  need(s.select_planet_jump(target));
  check(s.document() == before && s.resources() == resources &&
            s.knowledge() == knowledge && s.boarding() == pilot &&
            s.travel()->version == 5 && s.travel()->composed &&
            s.travel()->composed->active == FreedomComposedRouteKind::planet &&
            s.travel()->composed->selected_body == target &&
            !s.jump_preview() && !s.local_jump_preview(),
        "Explicit planet selection is zero-time and uses only its typed "
        "preview, with all physical/metadata owners retained");
}
auto leg(NativeFreedomFlightSession& s) -> void {
  const auto tick = s.document().flight.tick;
  const auto charges = s.resources()->jump_charges;
  const auto attempt = s.travel()->next_attempt;
  const auto pilot = s.boarding();
  const auto marker = s.knowledge()->surface_marker;
  const auto samples = s.knowledge()->field_samples;
  const auto inspections = s.knowledge()->surface_landmarks;
  need(s.begin_jump());
  auto peer = continued(s);
  for (SimulationTick i = 1; i <= kJumpSpoolTicks + kJumpTransitTicks; ++i) {
    const auto result = need(s.advance({}));
    need(peer.advance({}));
    check(s.document().flight.tick == tick + i &&
              s.resources()->tick == tick + i,
          "One actual native jump tick advances the sole body/origin/resource "
          "clock");
    if (i < kJumpSpoolTicks)
      check(
          !result.jump_committed && s.resources()->jump_charges == charges &&
              s.travel()->next_attempt == attempt,
          "Ordinary physical spool does not spend a charge before commitment");
    if (i == kJumpSpoolTicks) {
      check(result.jump_committed &&
                s.resources()->jump_charges == charges - 1 &&
                s.travel()->next_attempt == attempt + 1 &&
                s.travel()->phase == FreedomJumpPhase::transit,
            "Actual commitment records one frozen leg and one charge/attempt "
            "debit");
      const auto before = bytes(s);
      NativeFlightControls invalid;
      invalid.positive_rotation.x = std::numeric_limits<double>::quiet_NaN();
      check(!s.advance(invalid) && !s.cancel_jump() &&
                !s.select_planet_jump({}) && bytes(s) == before,
            "Invalid transit input and control/selection refusals retain the "
            "complete frozen transaction");
    }
    if (i > kJumpSpoolTicks)
      check(result.fuel_debit_quanta == 0,
            "Frozen transit never invents propulsion fuel usage");
    if (i == 1 || i == kJumpSpoolTicks - 1 || i == kJumpSpoolTicks ||
        i == kJumpSpoolTicks + 1 ||
        i == kJumpSpoolTicks + kJumpTransitTicks - 1 ||
        i == kJumpSpoolTicks + kJumpTransitTicks) {
      check(bytes(s) == bytes(peer), "Continued and uninterrupted native phase "
                                     "boundaries remain bit-exact");
      peer = continued(peer);
    }
    if (i == kJumpSpoolTicks + kJumpTransitTicks)
      check(result.jump_arrived && s.travel()->phase == FreedomJumpPhase::idle,
            "Actual600tick leg installs its frozen arrival at the exact "
            "scheduled clock");
  }
  check(s.boarding() == pilot && s.knowledge()->surface_marker == marker &&
            s.knowledge()->field_samples == samples &&
            s.knowledge()->surface_landmarks == inspections,
        "Real travel preserves actual seated pilot and independent "
        "personal/exposure/sample metadata");
  s = continued(s);
}
auto invalid_and_choice() -> void {
  auto old = need(
      NativeFreedomFlightSession::open(need(native_network_new_game({42}))));
  const auto before = bytes(old);
  check(!old.planet_jump_selection_available(other_planet(fixture())) &&
            !old.select_planet_jump(other_planet(fixture())) &&
            bytes(old) == before && old.knowledge()->recipe.version == 4,
        "Historical recipe4 refuses new travel without upgrading its selected "
        "chart");
  auto s = fixture();
  const auto home = current_target(s);
  const auto target = other_planet(s);
  atomic_refusal(s, {});
  atomic_refusal(s, home);
  auto forged = target;
  forged.body.kind = BodyKind::moon;
  atomic_refusal(s, forged);
  forged = target;
  forged.body.version = 2;
  atomic_refusal(s, forged);
  forged = target;
  forged.system = {0};
  atomic_refusal(s, forged);
  select_planet(s, target);
  const auto charges = s.resources()->jump_charges;
  need(s.begin_jump());
  atomic_refusal(s, home);
  for (unsigned i = 0; i < 2; ++i)
    need(s.advance({}));
  need(s.cancel_jump());
  check(s.travel()->phase == FreedomJumpPhase::idle &&
            s.travel()->next_attempt == 1 &&
            s.resources()->jump_charges == charges &&
            !s.travel()->composed->planet_committed,
        "Canceled physical spool retains its explicit chart/choice but no "
        "receipt, bill or charge");
  const auto network = need(generate_freedom_system_network({42}));
  need(s.select_jump(network.nodes[1].system));
  check(s.travel()->version == 5 &&
            s.travel()->composed->active == FreedomComposedRouteKind::system &&
            !s.travel()->composed->selected_body && s.jump_preview() &&
            !s.planet_jump_preview(),
        "Changing an uncommitted choice retains explicit composed5 rather than "
        "rewinding a legacy version");
  auto at_station = need(NativeFreedomFlightSession::open(
      need(native_planetary_network_new_game({42}))));
  const auto body = at_station.document();
  need(at_station.select_planet_jump(other_planet(at_station)));
  check(at_station.document() == body && at_station.travel()->version == 5 &&
            !at_station.begin_jump(),
        "Harmless attached/standing chart choice cannot authorize an unseated "
        "launch");
}
auto rotate(RigidOrientation q, RigidVector3 a) -> RigidVector3 {
  const auto n = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
  return {
      ((q.w * q.w + q.x * q.x - q.y * q.y - q.z * q.z) * a.x +
       2 * (q.x * q.y - q.w * q.z) * a.y + 2 * (q.x * q.z + q.w * q.y) * a.z) /
          n,
      (2 * (q.x * q.y + q.w * q.z) * a.x +
       (q.w * q.w - q.x * q.x + q.y * q.y - q.z * q.z) * a.y +
       2 * (q.y * q.z - q.w * q.x) * a.z) /
          n,
      (2 * (q.x * q.z - q.w * q.y) * a.x + 2 * (q.y * q.z + q.w * q.x) * a.y +
       (q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z) * a.z) /
          n};
}
auto destination_contact(const NativeFreedomFlightSession& arrival) -> void {
  // Separate valid contact-placement fixture after a real charged arrival.
  // It is NOT an uninterrupted descent/ascent or an automatic landing grant.
  const auto original = bytes(arrival);
  auto selected = need(arrival.travel_document());
  auto flight = arrival.document();
  const auto& owner = arrival.system();
  const auto target =
      need(find_local_system_planet(owner, flight.world->planet));
  const auto& planet = target->descriptor;
  const auto rotation = need(generate_planet_rotation_recipe(
      owner, planet.id, flight.model.rotation_generator));
  const RigidBodyWorldContext context{owner};
  const auto recipe =
      godot_spike::saved_contact_recipe(flight.model.terrain_policy);
  if (!recipe) throw std::runtime_error("Selected contact recipe unavailable");
  auto cache = need(TerrainTileCache::create(1));
  constexpr std::array faces{CubeFace::positive_x, CubeFace::negative_x,
                             CubeFace::positive_z, CubeFace::negative_z};
  constexpr std::array<std::array<std::uint32_t, 2>, 4> tiles{
      {{2000, 2000}, {2000, 6000}, {6000, 2000}, {6000, 6000}}};
  bool supported{};
  unsigned candidate{};
  for (; candidate < 17; ++candidate) {
    const auto face = candidate ? faces[(candidate - 1) / tiles.size()]
                                : CubeFace::positive_x;
    const auto tile = candidate ? tiles[(candidate - 1) % tiles.size()]
                                : std::array<std::uint32_t, 2>{4000, 4000};
    const auto triangle =
        need(godot_spike::detail::build_selected_contact_triangle(
            owner, planet, *recipe,
            {planet.id, face, tile[0], tile[1], 12, 13, 0}, cache));
    const auto a = triangle.vertices[0], b = triangle.vertices[1],
               c = triangle.vertices[2];
    const RigidVector3 center{a.x + .3 * ((b.x - a.x) + (c.x - a.x)),
                              a.y + .3 * ((b.y - a.y) + (c.y - a.y)),
                              a.z + .3 * ((b.z - a.z) + (c.z - a.z))};
    const auto normal = triangle.outward_normal;
    const auto w = 1 + normal.y, magnitude = std::hypot(w, normal.z, normal.x);
    RigidBodyState fixed;
    fixed.craft = flight.flight.craft;
    fixed.tick = flight.flight.tick + 1;
    fixed.frame = {
        RigidFrameKind::planet_fixed, owner.catalog.id, planet.id, {}};
    fixed.orientation = {w / magnitude, normal.z / magnitude, 0,
                         -normal.x / magnitude};
    const auto offset = rotate(fixed.orientation, {0, -2.08, -.7});
    fixed.position_metres = {center.x - .05 * normal.x - offset.x,
                             center.y - .05 * normal.y - offset.y,
                             center.z - .05 * normal.z - offset.z};
    const auto source =
        need(reframe_rigid_body(context, fixed,
                                {{RigidFrameKind::planet_relative_inertial,
                                  owner.catalog.id,
                                  planet.id,
                                  {}},
                                 fixed.tick},
                                rotation));
    const auto anchor =
        prepare_landed_craft(owner, rotation, source, true,
                             need(rigid_body_state_checksum(context, source)),
                             flight.model.terrain_policy);
    if (!anchor) continue;
    auto ground =
        PlanetSurfaceWalkTerrain::create(owner, rotation, *anchor, fixed.tick);
    if (!ground || !ground->entry()) continue;
    flight.flight = source;
    flight.origin.state.tick = fixed.tick;
    selected.voyage.voyage.resources.tick = fixed.tick;
    supported = true;
    break;
  }
  std::cout << "Otherplanet contact fixture candidate=" << candidate
            << " supported=" << supported << '\n';
  check(supported, "Existing17fixed candidates must genuinely admit one "
                   "destination contact/entry; no fallback or tuning");
  selected.voyage.voyage.voyage.base = flight;
  selected.voyage.voyage.voyage.surface = {};
  selected.seated_pilot = arrival.boarding();
  auto s = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(selected))));
  const auto resources = s.resources();
  const auto receipt = s.travel();
  need(s.set_landing_gear(true));
  const auto checksum =
      need(rigid_body_state_checksum(context, s.document().flight));
  const auto deployed = bytes(s);
  check(!s.commit_touchdown(checksum ^ 1) && bytes(s) == deployed,
        "Stale otherplanet touchdown is atomic");
  need(s.commit_touchdown(checksum));
  check(s.surface()->landed &&
            s.surface()->landed->fixed.frame.planet == planet.id &&
            s.resources() == resources && s.travel() == receipt,
        "Actual contact retains the genuine otherplanet owner and full charged "
        "travel history");
  need(s.begin_surface_walk());
  check(s.surface_walker() && s.surface_walker()->frame.planet == planet.id &&
            need(s.surface_return_access()).within_reach,
        "Supported otherplanet actor leaves the same craft through its real "
        "hatch");
  for (unsigned i = 0; i < 12; ++i)
    need(s.advance_surface_walk({-1, 0, 0}));
  s = continued(s);
  need(s.return_from_surface_walk());
  check(!s.surface_walker() &&
            s.resources()->flight_quanta == resources->flight_quanta &&
            s.resources()->jump_charges == resources->jump_charges &&
            s.travel() == receipt && bytes(arrival) == original,
        "Real supported walk/Continue/reboard retains receipt and fuel without "
        "changing the separate charged flight route");
}
auto planet_roundtrip() -> void {
  auto s = fixture();
  const auto home = current_target(s);
  select_planet(s, other_planet(s));
  leg(s);
  check(s.document().world->planet != PlanetId{home.body.value} &&
            !s.select_port(1) && !s.replenish_resources(),
        "Other ordinary planet has no invented Origin port or free service");
  check(!s.planet_jump_preview() && !s.travel()->composed->selected_body,
        "Arrival clears only the transient planet destination and retains the "
        "frozen receipt");
  const auto absent_companion = bytes(s);
  check(!s.select_local_jump(companion(s, false)) &&
            bytes(s) == absent_companion,
        "Another Origin planet does not masquerade as the actual home "
        "companion departure");
  destination_contact(s);
  select_planet(s, home);
  leg(s);
  check(s.resources()->jump_charges == 1 && s.travel()->next_attempt == 3 &&
            current_target(s) == home &&
            s.travel()->composed->planet_committed->request.destination == home,
        "Actual ordinary-planet return is a second charged leg with a retained "
        "latest receipt");
  auto system_branch = continued(s);
  const auto planet_receipt = s.travel()->composed->planet_committed;
  const auto planet_bill = s.travel()->composed->planet_commitment_resources;
  need(s.select_local_jump(companion(s, false)));
  leg(s);
  check(s.moon() && s.travel()->composed->planet_committed == planet_receipt &&
            s.travel()->composed->planet_commitment_resources == planet_bill &&
            s.travel()->composed->moon.outbound &&
            s.resources()->jump_charges == 0,
        "A later actual Moon leg remains usable after planet travel and "
        "preserves its existing receipt/bill");
  const auto network = need(generate_freedom_system_network({42}));
  need(system_branch.select_jump(network.nodes[1].system));
  leg(system_branch);
  check(system_branch.system().catalog.id == network.nodes[1].system &&
            system_branch.travel()->composed->planet_committed ==
                planet_receipt &&
            system_branch.travel()->composed->planet_commitment_resources ==
                planet_bill &&
            system_branch.travel()->committed &&
            system_branch.resources()->jump_charges == 0,
        "Independent Continued return can spend its same remaining charge on a "
        "real system leg without discarding planet history");
}
auto prior_history(FreedomComposedRouteKind kind) -> void {
  auto s = fixture();
  const auto home = current_target(s);
  const auto network = need(generate_freedom_system_network({42}));
  if (kind == FreedomComposedRouteKind::system) {
    need(s.select_jump(network.nodes[1].system));
    leg(s);
    need(s.select_jump(home.system));
    leg(s);
  } else {
    need(s.select_local_jump(
        companion(s, kind == FreedomComposedRouteKind::minor)));
    leg(s);
    const auto before = bytes(s);
    check(!s.select_jump(network.nodes[1].system) &&
              !s.select_planet_jump(home) && bytes(s) == before,
          "A genuine companion cannot directly depart for a system or ordinary "
          "planet");
    need(s.select_local_jump(home));
    leg(s);
  }
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- History
  // snapshot
  const auto retained = *s.travel();
  select_planet(s, other_planet(s));
  leg(s);
  const auto& after = *s.travel();
  check(after.committed == retained.committed &&
            after.commitment_resources == retained.commitment_resources &&
            after.composed->moon == retained.composed->moon &&
            after.composed->minor == retained.composed->minor &&
            after.composed->planet_committed && after.next_attempt == 4 &&
            s.resources()->jump_charges == 0,
        "Planet selection/commitment retains every genuine earlier "
        "system/companion receipt and paired bill");
  select_planet(s, home);
  const auto selected = bytes(s);
  check(!s.begin_jump() && bytes(s) == selected &&
            s.travel()->next_attempt == 4,
        "Retained composition never grants an unbilled charge or resets "
        "exhausted attempts");
}
} // namespace
int main() {
  try {
    invalid_and_choice();
    planet_roundtrip();
    for (const auto kind :
         {FreedomComposedRouteKind::system, FreedomComposedRouteKind::moon,
          FreedomComposedRouteKind::minor})
      prior_history(kind);
    std::cout << checks << " native planetary travel checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
