// Opt-in actual seed42 voyage exporter, not a gameplay autopilot. Every
// movement uses native commands and actual120Hz gravity/actuation/contact.
// The contact fixture supplies a navigation target ONLY: no source/ship pose,
// velocity, fuel, clock or world is assigned from it. Partial outputs on any
// refusal remain evidence; never silently substitute a supported ground start.
#include "../moon_contact_fixture.hpp"
#include "../native_foreign_moon_fixture.hpp"
#include "apsis_drift/freedom_assistance_save.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/save_file.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
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
using V = RigidVector3;
using Q = RigidOrientation;
auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(V a, double s) -> V {
  return {a.x * s, a.y * s, a.z * s};
}
auto norm(V a) -> double {
  return std::hypot(a.x, a.y, a.z);
}
auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto inv(Q q) -> Q {
  return {q.w, -q.x, -q.y, -q.z};
}
auto product(Q a, Q b) -> Q {
  return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
          a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
          a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
auto rotate(Q q, V v) -> V {
  const V u{q.x, q.y, q.z};
  const auto a = cross(u, v), b = cross(u, a);
  const double n = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  return add(v, scale(add(scale(a, q.w), b), 2 / n));
}
auto split(double v, double positive, double negative, double& p, double& n)
    -> void {
  p = std::clamp(v / positive, 0., 1.);
  n = std::clamp(-v / negative, 0., 1.);
}
auto fixed_view(const NativeFreedomFlightSession& s) -> RigidBodyState {
  return need(reframe_rigid_body(RigidBodyWorldContext{*s.moon()},
                                 s.document().flight,
                                 {{RigidFrameKind::body_fixed,
                                   s.system().catalog.id,
                                   {},
                                   {},
                                   s.moon()->body},
                                  s.document().flight.tick},
                                 *s.moon()));
}
auto contact_detail(const NativeFreedomFlightSession& s) -> void {
  const auto snap =
      MoonContactSnapshot::create(*s.moon(), s.document().flight, true);
  if (!snap) {
    std::cout << "contact snapshot refused "
              << static_cast<unsigned>(snap.error()) << '\n';
    return;
  }
  const auto query = snap->query();
  std::cout << "contact supports=" << unsigned(query.support_count)
            << " assessment=" << bool(query.assessment);
  if (query.assessment)
    std::cout << " class="
              << static_cast<unsigned>(query.assessment->classification)
              << " failed=" << query.assessment->failed_margins;
  std::cout << '\n';
  for (unsigned i = 0; i < query.support_count; ++i) {
    const auto& p = query.supports[i];
    std::cout << " pad" << i
              << " geometry=" << (p.geometry && p.geometry->has_value())
              << " support=" << (p.support && p.support->has_value());
    if (p.geometry && *p.geometry)
      std::cout << " gaps=" << (**p.geometry).minimum_gap_metres << ','
                << (**p.geometry).maximum_gap_metres;
    if (p.geometry && !*p.geometry)
      std::cout << " error=" << static_cast<unsigned>(p.geometry->error());
    std::cout << '\n';
  }
  const auto hull = snap->hull_clearance();
  std::cout << "hull=" << bool(hull)
            << " certified=" << (hull && hull->certified) << '\n';
}
auto control(const NativeFreedomFlightSession& s, const RigidBodyState& target)
    -> NativeFlightControls {
  const auto& body = s.document().flight;
  const auto fixed = fixed_view(s);
  const auto rotation =
      need(resolve_physical_moon_rotation(*s.moon(), body.tick));
  const auto craft = need(resolve_craft_frame(body.craft)).properties;
  const auto error = sub(target.position_metres, fixed.position_metres);
  const double distance = norm(error);
  const double speed =
      std::min({80., std::sqrt(2 * 1.5 * distance), .4 * distance});
  const auto desired = distance > 0 ? scale(error, speed / distance) : V{};
  auto acceleration =
      scale(sub(desired, fixed.linear_velocity_metres_per_second), 1.5);
  const double an = norm(acceleration);
  if (an > 4) acceleration = scale(acceleration, 4 / an);
  const auto gravity =
      need(evaluate_moon_body_gravity(RigidBodyWorldContext{*s.moon()}, body));
  const auto gfixed = rotate(inv(rotation.fixed_to_system),
                             gravity.acceleration_metres_per_second_squared);
  const V omega{rotation.angular_velocity_radians_per_second.x,
                rotation.angular_velocity_radians_per_second.y,
                rotation.angular_velocity_radians_per_second.z};
  // Exact constant-spin frame equation: aI=R(aF+2wXvF+wX(wXrF)).
  const auto requested_fixed = sub(
      add(add(acceleration,
              scale(cross(omega, fixed.linear_velocity_metres_per_second), 2)),
          cross(omega, cross(omega, fixed.position_metres))),
      gfixed);
  const auto force_body =
      scale(rotate(inv(body.orientation),
                   rotate(rotation.fixed_to_system, requested_fixed)),
            craft.dry_mass_kg);
  NativeFlightControls c;
  split(force_body.x, craft.positive_force_newtons[0],
        craft.negative_force_newtons[0], c.positive_translation.x,
        c.negative_translation.x);
  split(force_body.y, craft.positive_force_newtons[1],
        craft.negative_force_newtons[1], c.positive_translation.y,
        c.negative_translation.y);
  split(force_body.z, craft.positive_force_newtons[2],
        craft.negative_force_newtons[2], c.positive_translation.z,
        c.negative_translation.z);
  auto qe = product(inv(body.orientation),
                    product(rotation.fixed_to_system, target.orientation));
  if (qe.w < 0) qe = {-qe.w, -qe.x, -qe.y, -qe.z};
  const auto v = V{qe.x, qe.y, qe.z};
  const double sn = norm(v);
  const auto correction = sn > 1e-14
                              ? scale(v, 1.8 * 2 * std::atan2(sn, qe.w) / sn)
                              : scale(v, 3.6);
  const auto rate = add(correction, rotate(inv(body.orientation), omega));
  split(rate.x, craft.max_angular_rate_milliradians_per_second[0] * .001,
        craft.max_angular_rate_milliradians_per_second[0] * .001,
        c.positive_rotation.x, c.negative_rotation.x);
  split(rate.y, craft.max_angular_rate_milliradians_per_second[1] * .001,
        craft.max_angular_rate_milliradians_per_second[1] * .001,
        c.positive_rotation.y, c.negative_rotation.y);
  split(rate.z, craft.max_angular_rate_milliradians_per_second[2] * .001,
        craft.max_angular_rate_milliradians_per_second[2] * .001,
        c.positive_rotation.z, c.negative_rotation.z);
  return c;
}
auto phase(const std::filesystem::path& directory,
           const NativeFreedomFlightSession& s, const char* name) -> void {
  const auto path = directory / (std::string{name} + ".json");
  if (std::filesystem::exists(path))
    throw std::runtime_error("Refuse phase overwrite");
  const auto saved = bytes(s);
  need(s.save_as(path));
  check(bytes(s) == saved, "Phase Save cannot mutate live voyage");
  const auto restored =
      need(NativeFreedomFlightSession::open(need(native_continue(path))));
  check(bytes(restored) == saved,
        "Phase file must restore complete exact native voyage");
  std::cout << "PHASE " << name << " tick=" << s.document().flight.tick
            << " bytes=" << saved.size() << " frame="
            << static_cast<unsigned>(s.document().flight.frame.kind)
            << " actor=" << bool(s.surface_walker()) << " heading="
            << (s.surface_walker() ? s.surface_walker()->heading_radians : 0)
            << '\n';
}
template <class T>
auto service_knowledge(T& value) -> FreedomKnowledgeSaveDocument& {
  if constexpr (std::is_same_v<T, FreedomKnowledgeSaveDocument>)
    return value;
  else if constexpr (std::is_same_v<T, FreedomTravelSaveDocument>)
    return value.voyage;
  else if constexpr (std::is_same_v<T, FreedomSurfaceWalkSaveDocument>)
    return service_knowledge(value.voyage);
  else
    return std::visit(
        [](auto& inner) -> FreedomKnowledgeSaveDocument& {
          return service_knowledge(inner);
        },
        value.voyage);
}
auto service_checkpoints(const std::filesystem::path& directory,
                         const NativeFreedomFlightSession& actual_landed)
    -> void {
  const auto original = bytes(actual_landed);
  check(actual_landed.moon() && actual_landed.surface() &&
            actual_landed.surface()->moon_landed,
        "Service sidecars require the genuine already flown landing");
  auto saved = need(actual_landed.save_document());
  auto voyage = std::visit(
      [](const auto& value) -> FreedomAssistanceVoyage {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_constructible_v<FreedomAssistanceVoyage, T>)
          return value;
        else
          throw std::runtime_error(
              "Service sidecar needs a complete existing typed voyage");
      },
      saved);
  // LABELLED metadata fixture: only flight fuel becomes0 and one initial
  // source-bound station actor is explicitly selected at the SAME clock.
  // No flown exhaustion, historical auto-adoption or NPC arrival is claimed.
  std::visit(
      [](auto& value) {
        service_knowledge(value).voyage.resources.flight_quanta = 0;
      },
      voyage);
  const auto world = need(make_origin_assistance_world_context(
      actual_landed.document().origin.recipe.universe_seed));
  const auto actor = need(initialize_origin_service_craft(
      world.origin(), actual_landed.document().flight.tick));
  FreedomAssistanceSaveDocument selected{1, std::move(voyage), actor};
  auto service = need(NativeFreedomFlightSession::open(need(
      native_select_save_document(NativeSaveDocument{std::move(selected)}))));
  phase(directory, service, "service-landed");
  const auto initial = service.document();
  const auto resource = *service.resources();
  need(service.request_origin_assistance());
  check(
      service.document() == initial && *service.resources() == resource,
      "Real service request is zero-time and does not alter player fuel/state");
  phase(directory, service, "service-requested");
  const auto per_tick = actor.descriptor.transfer_quanta_per_tick;
  const auto target = per_tick * 600;
  unsigned ticks{};
  for (; ticks < 60000; ++ticks) {
    need(service.advance({}));
    const auto* sortie =
        std::get_if<OriginServiceSortie>(&*service.origin_assistance());
    if (!sortie)
      throw std::runtime_error(
          "Service route became unavailable before partial delivery");
    if (sortie->service.delivery) {
      const auto delivered = sortie->service.delivery->delivered_quanta;
      check(delivered <= target,
            "Partial sidecar must stop at exactly600actual credits");
      if (delivered == target) {
        check(sortie->phase == OriginServiceSortiePhase::delivering &&
                  service.resources()->flight_quanta == target &&
                  sortie->service.body.tick == service.document().flight.tick,
              "Partial delivery retains real same-clock NPC/player fuel "
              "evidence");
        phase(directory, service, "service-partial600");
        break;
      }
    }
  }
  check(ticks < 60000, "Bounded real service route reaches partial600 without "
                       "a fallback fixture");
  check(bytes(actual_landed) == original,
        "Service sidecars preserve the original actual landed source exactly");
  std::cout << "SERVICE SIDECARS actual_ticks=" << ticks + 1
            << " credits=600 · labelled initial zero-fuel metadata only\n";
}
auto integration(const std::filesystem::path& directory) -> int {
  const auto started = std::chrono::steady_clock::now();
  if (!std::filesystem::create_directory(directory))
    throw std::runtime_error(
        "Output directory must be new and its parent must exist");
  auto s =
      need(NativeFreedomFlightSession::open(need(native_new_game(Seed{42}))));
  for (unsigned i = 0; i < 1194; ++i)
    need(s.advance_walk({0, -1, 0}));
  need(s.begin_boarding());
  for (unsigned i = 0; i < kGameplayBoardingTicks; ++i)
    need(s.advance_walk({}));
  need(s.release_port());
  NativeFlightControls withdraw;
  withdraw.negative_translation.y = .25;
  for (unsigned i = 0; i < 720; ++i)
    need(s.advance(withdraw));
  const auto catalog = need(generate_secondary_body_catalog(s.system()));
  need(s.select_local_jump({s.system().catalog.id, catalog.bodies[0].node.id}));
  leg(s);
  std::cout << "REAL ROUTE ARRIVED tick=" << s.document().flight.tick
            << " charges=" << s.resources()->jump_charges << '\n';
  phase(directory, s, "arrival");
  need(s.set_assistance(true));
  const auto target = test_fixture::make_moon_contact_fixture().fixed;
  std::cout << "target navigation only COM=" << target.position_metres.x << ','
            << target.position_metres.y << ',' << target.position_metres.z
            << " no source placement\n";
  unsigned blocked_ticks{};
  for (unsigned i = 0; i < 180000; ++i) {
    const auto current = fixed_view(s);
    const double distance =
        norm(sub(target.position_metres, current.position_metres));
    if (distance < 30 && (!s.surface() || !s.surface()->gear_deployed)) {
      need(s.set_landing_gear(true));
      phase(directory, s, "approach");
    }
    const auto previous = s;
    const auto controls = control(s, target);
    const auto step = s.advance(controls);
    if (!step) {
      std::cout << "ADVANCE REFUSED i=" << i
                << " tick=" << s.document().flight.tick
                << " distance=" << distance << " reason=" << step.error()
                << '\n';
      contact_detail(s);
      s = continued(s);
      return 2;
    }
    if (i % 120 == 0) {
      const auto f = fixed_view(s);
      std::cout << "descent i=" << i << " tick=" << s.document().flight.tick
                << " distance="
                << norm(sub(target.position_metres, f.position_metres))
                << " speed=" << norm(f.linear_velocity_metres_per_second)
                << " pos=" << f.position_metres.x << ',' << f.position_metres.y
                << ',' << f.position_metres.z
                << " fuel=" << s.resources()->flight_quanta << " landed="
                << (s.surface() && s.surface()->moon_landed.has_value())
                << " note=" << s.surface_maneuver().note << '\n';
      std::cout.flush();
    }
    if (s.surface() && s.surface()->moon_landed) {
      std::cout << "GENUINE TOUCHDOWN i=" << i
                << " tick=" << s.document().flight.tick << '\n';
      contact_detail(s);
      phase(directory, previous, "touchdown-source");
      phase(directory, s, "landed");
      const auto service_source = s;
      s = continued(s);
      need(s.begin_surface_walk());
      s = continued(s);
      // Real neutral supported tick changes ONLY view heading, requested for
      // the outside Continue fixture; out/back translational inputs remain
      // unchanged.
      need(s.advance_surface_walk({0, 0, .7}));
      phase(directory, s, "outside");
      const auto foot = s.surface_walker()->foot_position_metres;
      for (unsigned j = 0; j < 240; ++j)
        need(s.advance_surface_walk({-1, 0, 0}));
      for (unsigned j = 0; j < 240; ++j)
        need(s.advance_surface_walk({1, 0, 0}));
      std::cout << "ACTUAL WALK out/back footerror="
                << norm(sub(s.surface_walker()->foot_position_metres, foot))
                << " return=" << need(s.surface_return_access()).within_reach
                << '\n';
      s = continued(s);
      need(s.return_from_surface_walk());
      phase(directory, s, "returned");
      need(s.release_surface());
      phase(directory, s, "released");
      const auto released = fixed_view(s);
      NativeFlightControls up;
      up.positive_translation.y = .1;
      for (unsigned j = 0; j < 600; ++j) {
        need(s.advance(up));
        if (s.surface()->moon_landed)
          throw std::runtime_error("Ascent recaptured landing");
      }
      const auto ascending = fixed_view(s);
      std::cout << "ACTUAL ASCENT height_delta="
                << norm(ascending.position_metres) -
                       norm(released.position_metres)
                << " speed="
                << norm(ascending.linear_velocity_metres_per_second)
                << " ticks=" << s.document().flight.tick << '\n';
      phase(directory, s, "ascent");
      s = continued(s);
      std::cout << "COMPLETE real route landing/walk/return/ascent clocks and "
                   "complete Save/Continue preserved checks="
                << checks << '\n';
      // Existing eight phase exports and original report precede this bounded
      // sidecar branch; the original live route is never modified by it.
      service_checkpoints(directory, service_source);
      return 0;
    }
    if (distance < 1 && s.surface_maneuver().note ==
                            "Moon terrain contact: inward motion stopped")
      ++blocked_ticks;
    else
      blocked_ticks = 0;
    if (blocked_ticks >= 600) {
      std::cout
          << "BLOCKED repeated actual collision response at final contact i="
          << i << '\n';
      contact_detail(s);
      s = continued(s);
      return 3;
    }
    if (std::chrono::steady_clock::now() - started >
        std::chrono::seconds(240)) {
      std::cout << "BOUNDED TIMEOUT i=" << i << '\n';
      return 4;
    }
  }
  std::cout << "BOUNDED TICK LIMIT\n";
  return 5;
}
} // namespace
int main(int argc, char** argv) {
  std::cout << std::setprecision(17);
  if (argc > 2) {
    std::cerr << "usage: apsis-drift-freedom-moon-fixture [OUTPUT_DIRECTORY]\n";
    return 1;
  }
  try {
    const auto directory =
        std::filesystem::absolute(argc == 2 ? argv[1] : "moon-voyage-fixture")
            .lexically_normal();
    const auto result = integration(directory);
    if (result != 0) return result;
    // Separate charged foreign outing; the continuous Origin route above
    // remains unchanged. Its only saved-space boundary is labelled explicitly.
    const auto foreign_checks =
        test_fixture::run_foreign_moon_outing(2, directory);
    std::cout << "FOREIGN OUTING checks=" << foreign_checks << '\n';
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 1;
  }
}
