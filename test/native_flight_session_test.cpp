#include "apsis_drift/native_flight_session.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace apsis_drift;
using V = RigidVector3;
int failures{};
auto check(bool x, std::string_view message) -> void {
  if (!x) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
template <class T, class E> auto required(std::expected<T, E> x) -> T {
  if (!x)
    throw std::runtime_error("required native flight session fixture failed");
  return *x;
}
auto length(V a) -> double {
  return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
}
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scalars(RigidBodyState& s) -> std::array<double*, 13> {
  return {&s.position_metres.x,
          &s.position_metres.y,
          &s.position_metres.z,
          &s.orientation.w,
          &s.orientation.x,
          &s.orientation.y,
          &s.orientation.z,
          &s.linear_velocity_metres_per_second.x,
          &s.linear_velocity_metres_per_second.y,
          &s.linear_velocity_metres_per_second.z,
          &s.angular_velocity_radians_per_second.x,
          &s.angular_velocity_radians_per_second.y,
          &s.angular_velocity_radians_per_second.z};
}
auto bits(RigidBodyState a, RigidBodyState b) -> bool {
  auto x = scalars(a), y = scalars(b);
  for (std::size_t n = 0; n < x.size(); ++n)
    if (std::bit_cast<std::uint64_t>(*x[n]) !=
        std::bit_cast<std::uint64_t>(*y[n]))
      return false;
  return a.tick == b.tick && a.frame == b.frame && a.craft == b.craft;
}
auto contents(const std::filesystem::path& path) -> std::string {
  std::ifstream in{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{in}, {}};
}
struct Fixture {
  explicit Fixture(Seed seed = Seed{42})
      : system(required(generate_physical_origin_system(seed))),
        context(system),
        planet(system.catalog.planets[kOriginHomePlanetOrdinal].descriptor),
        rotation(required(generate_planet_rotation_recipe(system, planet.id))),
        origin(make_freedom_new_game_document(seed)) {
    origin.state.tick = 25;
    origin.state.discoveries.push_back({{77}, 8});
    origin.state.world_deltas.push_back(
        {"signal:77", SaveWorldDeltaKind::discovered, 9});
  }
  PhysicalLocalSystem system;
  RigidBodyWorldContext context;
  const PlanetDescriptor& planet;
  PhysicalPlanetRotationRecipe rotation;
  FreedomSaveDocument origin;
  auto document(double altitude = 1000) const -> FreedomFlightSaveDocument {
    RigidBodyState s;
    s.tick = 25;
    s.frame = {RigidFrameKind::planet_relative_inertial,
               system.catalog.id,
               planet.id,
               {}};
    s.position_metres = {planet.radius.value * 1000.0 + altitude, 0, 0};
    s.linear_velocity_metres_per_second = {0, 0, -100};
    return {origin, s, {}};
  }
  auto selected(FreedomFlightSaveDocument d) const -> NativeStartup {
    return {NativeStartup::Mode::freedom, std::move(d), planet, {}};
  }
  auto open(FreedomFlightSaveDocument d) const -> NativeFreedomFlightSession {
    return required(NativeFreedomFlightSession::open(selected(std::move(d))));
  }
  auto held(double altitude = 500000) const -> FreedomFlightSaveDocument {
    auto d = document(altitude);
    auto g = required(evaluate_central_body_gravity(context, d.flight));
    const double r = d.flight.position_metres.x;
    d.flight.linear_velocity_metres_per_second = {
        0,
        std::sqrt(g.gravitational_parameter_metres_cubed_per_second_squared /
                  r),
        0};
    d.model.hold.target = OrbitHoldTarget{planet.id, r, {0, 0, 1}};
    d.flight.position_metres.x += 1000;
    return d;
  }
};
auto invalid(const Fixture& f) -> void {
  check(
      !NativeFreedomFlightSession::open(required(native_new_game(Seed{42}))) &&
          !NativeFreedomFlightSession::open(
              required(native_legacy_new_game(Seed{42}))),
      "flight owner refuses docked/career selections, no replacement spawn");
  auto wrong = f.selected(f.document());
  wrong.mode = NativeStartup::Mode::legacy_career;
  check(!NativeFreedomFlightSession::open(wrong),
        "mode/variant mismatch refuses");
  auto d = f.document();
  d.origin.state.tick = 24;
  check(!NativeFreedomFlightSession::open(f.selected(d)),
        "mismatched saved clocks refuse session activation");
  auto session = f.open(f.held());
  const auto before = session.document();
  const auto observed = required(session.observe());
  for (unsigned n = 0; n < 5; ++n)
    check(required(session.observe()) == observed &&
              session.document() == before,
          "read-only observation cannot mutate state/model/history/clock");
  for (std::size_t n = 0; n < 12; ++n)
    for (double value : {-.1, 1.1, std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity()}) {
      NativeFlightControls input;
      std::array<double*, 12> xs{
          &input.positive_translation.x, &input.positive_translation.y,
          &input.positive_translation.z, &input.negative_translation.x,
          &input.negative_translation.y, &input.negative_translation.z,
          &input.positive_rotation.x,    &input.positive_rotation.y,
          &input.positive_rotation.z,    &input.negative_rotation.x,
          &input.negative_rotation.y,    &input.negative_rotation.z};
      *xs[n] = value;
      check(!session.advance(input) && session.document() == before &&
                bits(session.document().flight, before.flight),
            "bad semantic actuator channels refuse the complete session "
            "atomically");
    }
  for (double dt : {0., -.01, .1, std::numeric_limits<double>::quiet_NaN()})
    check(!session.advance({}, SimulationSeconds{dt}) &&
              session.document() == before,
          "invalid frame-time step cannot advance session clocks");
  check(!session.set_hold({99, {}}) && session.document() == before,
        "unsupported hold version refuses without replacing prior selection");
  auto target = *before.model.hold.target;
  target.radius_metres = 1;
  check(!session.set_hold({1, target}) && session.document() == before,
        "invalid hold target is atomic");
  check(!session.save_as("relative.json") && !session.save_as({}) &&
            session.document() == before,
        "invalid native save path cannot mutate session");
  d = f.document();
  d.flight.tick = std::numeric_limits<SimulationTick>::max() - 2;
  d.origin.state.tick = d.flight.tick;
  auto last = f.open(d);
  check(!last.advance({}) && last.document() == d,
        "last saved clock cannot commit an unpersistable candidate");
  auto moved = std::move(session);
  check(required(moved.observe()) == observed,
        "moving owner does not retain a dangling world-context pointer");
  // Pure planner must refuse malformed channels too, before any integrator.
  VacuumIntent input;
  input.negative_rotation.y = std::numeric_limits<double>::quiet_NaN();
  check(!evaluate_orbit_hold_correction(
            f.context, before.flight, input, f.rotation,
            {1, observed.atmosphere.space_boundary_altitude_metres},
            before.model.hold),
        "standalone pure correction refuses invalid actuator intent");
}
auto ordinary_and_hold(const Fixture& f) -> void {
  for (bool assistance : {false, true})
    for (bool hold : {false, true})
      for (bool manual : {false, true}) {
        auto d = hold ? f.held() : f.document();
        d.model.assistance = assistance;
        auto session = f.open(d);
        auto expected = d.flight;
        NativeFlightControls controls;
        if (manual) {
          controls.negative_translation.z = .25;
          controls.positive_translation.x = .1;
        }
        controls.positive_rotation.y = .025;
        VacuumIntent intent{
            controls.positive_translation, controls.negative_translation,
            controls.positive_rotation, controls.negative_rotation, assistance};
        const auto edge = required(session.observe())
                              .atmosphere.space_boundary_altitude_metres;
        for (unsigned n = 0; n < 120; ++n) {
          auto actual = required(session.advance(controls));
          if (hold) {
            auto normal = required(advance_orbit_hold_dynamics(
                f.context, expected, intent, f.rotation, {1, edge},
                d.model.hold));
            check(bits(session.document().flight, expected) &&
                      actual.hold.status == normal.status &&
                      actual.actuation
                              .aerodynamic_linear_impulse_newton_seconds ==
                          V{} &&
                      actual.actuation.central.propulsion
                              .world_linear_impulse_newton_seconds ==
                          normal.actuation.propulsion
                              .world_linear_impulse_newton_seconds,
                  "vacuum session preserves exact selected "
                  "hold/Advanced/manual behavior");
          } else {
            auto normal = required(advance_atmospheric_flight(
                f.context, expected, intent, f.rotation));
            check(
                bits(session.document().flight, expected) &&
                    actual.hold.status == OrbitHoldStatus::disabled &&
                    actual.actuation.observation_after ==
                        normal.observation_after &&
                    actual.actuation
                            .aerodynamic_angular_impulse_newton_metre_seconds ==
                        normal.aerodynamic_angular_impulse_newton_metre_seconds,
                "disabled hold composes exact ordinary atmospheric flight");
          }
          check(session.document().origin.state.tick ==
                        session.document().flight.tick &&
                    session.document().origin.state.discoveries ==
                        d.origin.state.discoveries,
                "rigid/history clocks advance together without fabricated "
                "history");
        }
      }
  auto d = f.held();
  auto session = f.open(d);
  session.set_assistance(false);
  check(session.document().flight == d.flight &&
            session.document().model.hold == d.model.hold &&
            !session.document().model.assistance,
        "Advanced selection retains paused target without advancing physics");
  check(required(session.advance({})).hold.status ==
            OrbitHoldStatus::paused_advanced,
        "saved Advanced mode governs subsequent empty controls");
}
auto boundary_composition(const Fixture& f) -> void {
  const auto base = f.document();
  const double edge = required(evaluate_atmospheric_flight(
                                   f.context, base.flight, {}, f.rotation))
                          .space_boundary_altitude_metres;
  for (double direction : {-1., 1.}) {
    auto d = f.document(edge - direction * .1);
    d.flight.linear_velocity_metres_per_second = {direction * 5000, 0, -2000};
    d.model.hold.target = OrbitHoldTarget{
        f.planet.id, f.planet.radius.value * 1000.0 + edge + 10000, {0, 0, 1}};
    auto session = f.open(d);
    const auto before = d.flight;
    auto old_gravity_only = before;
    const auto old = required(advance_orbit_hold_dynamics(
        f.context, old_gravity_only, {}, f.rotation, {1, edge}, d.model.hold));
    const auto result = required(session.advance({}));
    const auto& after = session.document().flight;
    check(direction * (length(after.position_metres) -
                       f.planet.radius.value * 1000.0 - edge) >
              0,
          "hold-session tick physically crosses declared atmosphere boundary");
    check(length(result.actuation.aerodynamic_linear_impulse_newton_seconds) >
                  0 &&
              (direction > 0 ||
               length(sub(after.linear_velocity_metres_per_second,
                          old_gravity_only.linear_velocity_metres_per_second)) >
                   1e-12),
          "crossing tick evaluates air at RK4 stages, unlike central-only "
          "frame-start dispatch");
    check(result.hold.status ==
                  (direction < 0 ? OrbitHoldStatus::saturated
                                 : OrbitHoldStatus::unavailable_environment) &&
              result.hold.status == old.status,
          "environment/authority status retains explicit hold allocation "
          "meanings");
    const auto plan = required(evaluate_orbit_hold_correction(
        f.context, before, {}, f.rotation, {1, edge}, d.model.hold));
    auto direct = before;
    const auto composed = required(advance_atmospheric_flight(
        f.context, direct, plan.commands, f.rotation));
    check(bits(after, direct) &&
              result.actuation.aerodynamic_linear_impulse_newton_seconds ==
                  composed.aerodynamic_linear_impulse_newton_seconds &&
              result.actuation.central.gravity_impulse_newton_seconds ==
                  composed.central.gravity_impulse_newton_seconds,
          "session uses bounded plan once through existing coupled "
          "environment/propulsion kernel");
    check(result.actuation.observation_after ==
              required(session.observe()).orbit,
          "post-step orbital observation agrees with the live session state "
          "and shared edge");
  }
}
auto continuation(const Fixture& f, const std::filesystem::path& directory)
    -> void {
  const auto source =
      directory /
      (std::to_string(f.origin.recipe.universe_seed.value) + "-source.json");
  auto initial = f.held();
  check(write_freedom_flight_file_atomically(source, initial).has_value(),
        "session source fixture writes");
  const auto original = contents(source);
  auto a = required(
           NativeFreedomFlightSession::open(required(native_continue(source)))),
       b = a;
  for (unsigned n = 0; n < 600; ++n) {
    a.set_assistance((n / 97) % 2 == 0);
    b.set_assistance((n / 97) % 2 == 0);
    if (n == 150) {
      check(a.set_hold({}).has_value() && b.set_hold({}).has_value(),
            "explicit hold disable changes selection only");
    }
    if (n == 320) {
      check(a.set_hold(initial.model.hold).has_value() &&
                b.set_hold(initial.model.hold).has_value(),
            "explicit valid hold reengagement retains chosen reference");
    }
    if (n == 35 || n == 250 || n == 450) {
      const auto path =
          directory / (std::to_string(f.origin.recipe.universe_seed.value) +
                       "-resume.json");
      const auto selected_source = b.source_save();
      check(b.save_as(path).has_value() && b.source_save() == selected_source &&
                contents(source) == original,
            "explicit session Save As retains source identity and untouched "
            "original bytes");
      b = required(
          NativeFreedomFlightSession::open(required(native_continue(path))));
      check(
          a.document() == b.document() &&
              required(a.observe()) == required(b.observe()),
          "reopened session restores exact model/body/history and observation");
    }
    NativeFlightControls controls;
    if (n > 200 && n < 260) controls.negative_translation.z = .15;
    if (n > 400) controls.positive_rotation.y = .025;
    const auto x = required(a.advance(controls)),
               y = required(b.advance(controls));
    check(a.document() == b.document() &&
              bits(a.document().flight, b.document().flight) &&
              x.hold.status == y.hold.status &&
              x.hold.requested_force_body_newtons ==
                  y.hold.requested_force_body_newtons &&
              x.applied_hold_force_body_newtons ==
                  y.applied_hold_force_body_newtons &&
              x.actuation.central.propulsion
                      .world_linear_impulse_newton_seconds ==
                  y.actuation.central.propulsion
                      .world_linear_impulse_newton_seconds &&
              x.actuation.observation_after == y.actuation.observation_after,
          "complete reopened session continues exact planned/applied "
          "propulsion and observations");
  }
  check(contents(source) == original,
        "ordinary simulation never autosaves or rewrites source");
  const RigidBodyWorldContext context{a.system()};
  std::cout << "native-session " << f.origin.recipe.universe_seed.value
            << " replay "
            << required(rigid_body_state_checksum(context, a.document().flight))
            << '\n';
}
} // namespace
int main() {
  try {
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("apsis-native-flight-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    Fixture f;
    invalid(f);
    ordinary_and_hold(f);
    boundary_composition(f);
    for (Seed seed :
         {Seed{0}, Seed{42}, Seed{std::numeric_limits<std::uint64_t>::max()}}) {
      Fixture ref{seed};
      continuation(ref, directory);
    }
    std::filesystem::remove_all(directory);
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 2;
  }
  return failures ? 1 : 0;
}
