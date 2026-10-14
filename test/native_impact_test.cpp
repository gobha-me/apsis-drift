#include "apsis_drift/godot/saved_contact_geometry.hpp"
#include "apsis_drift/godot/saved_flight.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "contact_geometry_internal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace apsis_drift;
int failures{}, checks{};
auto check(bool value, std::string_view message) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) {
    if constexpr (std::is_same_v<E, std::string>)
      throw std::runtime_error(value.error());
    else if constexpr (std::is_same_v<E, SaveSchemaError>)
      throw std::runtime_error(value.error().detail);
    else
      throw std::runtime_error("Native impact fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
using V = RigidVector3;
auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto mul(V a, double s) -> V {
  return {a.x * s, a.y * s, a.z * s};
}
auto rotate(RigidOrientation q, V a) -> V {
  const auto s = 2 / (((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z);
  return {
      (1 - s * (q.y * q.y + q.z * q.z)) * a.x +
          s * (q.x * q.y - q.w * q.z) * a.y + s * (q.x * q.z + q.w * q.y) * a.z,
      s * (q.x * q.y + q.w * q.z) * a.x +
          (1 - s * (q.x * q.x + q.z * q.z)) * a.y +
          s * (q.y * q.z - q.w * q.x) * a.z,
      s * (q.x * q.z - q.w * q.y) * a.x + s * (q.y * q.z + q.w * q.x) * a.y +
          (1 - s * (q.x * q.x + q.y * q.y)) * a.z};
}
struct Fixtures {
  NativeFreedomFlightSession station =
      need(NativeFreedomFlightSession::open(need(native_new_game({42}))));
  Fixtures() {
    for (unsigned tick = 0; tick < 2960; ++tick)
      need(station.advance_walk({0, -1, 0}));
    need(station.begin_boarding());
    for (unsigned tick = 0; tick < kGameplayBoardingTicks; ++tick)
      need(station.advance_walk({}));
  }
  auto airborne(double closing, bool gear) const -> NativeFreedomFlightSession {
    const auto& owner = station.system();
    auto flight = station.document();
    const auto planet =
        need(find_local_system_planet(owner, *flight.flight.frame.planet));
    auto cache = need(TerrainTileCache::create(1));
    const auto triangle =
        need(godot_spike::detail::build_selected_contact_triangle(
            owner, planet->descriptor,
            *godot_spike::saved_contact_recipe(flight.model.terrain_policy),
            {planet->descriptor.id, CubeFace::positive_x, 4000, 4000, 12, 13,
             0},
            cache));
    const auto a = triangle.vertices[0], b = triangle.vertices[1],
               c = triangle.vertices[2];
    const V center{a.x + .3 * ((b.x - a.x) + (c.x - a.x)),
                   a.y + .3 * ((b.y - a.y) + (c.y - a.y)),
                   a.z + .3 * ((b.z - a.z) + (c.z - a.z))};
    const V normal{triangle.outward_normal.x, triangle.outward_normal.y,
                   triangle.outward_normal.z};
    const auto w = 1 + normal.y, length = std::hypot(w, normal.z, normal.x);
    auto fixed = flight.flight;
    fixed.frame = {RigidFrameKind::planet_fixed,
                   owner.catalog.id,
                   planet->descriptor.id,
                   {}};
    fixed.orientation = {w / length, normal.z / length, 0, -normal.x / length};
    const auto properties = need(resolve_craft_frame(fixed.craft)).properties;
    double bottom = properties.hull_min_mm[1] / 1000.0;
    if (gear)
      for (unsigned i = 0; i < properties.support_count; ++i)
        bottom =
            std::min(bottom, properties.supports[i].contact_mm[1] / 1000.0);
    const V bottom_local{-properties.center_of_mass_mm[0] / 1000.0,
                         bottom - properties.center_of_mass_mm[1] / 1000.0,
                         -properties.center_of_mass_mm[2] / 1000.0};
    fixed.position_metres =
        add(add(center, mul(normal, .02)),
            mul(rotate(fixed.orientation, bottom_local), -1));
    fixed.linear_velocity_metres_per_second = mul(normal, -closing);
    fixed.angular_velocity_radians_per_second = {};
    fixed = need(canonicalize_rigid_body_state({owner}, fixed));
    flight.flight = need(reframe_rigid_body(
        {owner}, fixed, {station.document().flight.frame, fixed.tick},
        station.rotation()));
    flight.model.assistance = false;
    auto base =
        std::get<FreedomBoardingSaveDocument>(station.surface_document().base);
    base.voyage.flight = flight;
    base.voyage.docking.attached = false;
    auto document =
        std::get<FreedomKnowledgeSaveDocument>(need(station.save_document()));
    document.voyage.voyage.base = std::move(base);
    document.voyage.voyage.surface = {gear, {}};
    return need(NativeFreedomFlightSession::open(
        need(native_select_save_document(std::move(document)))));
  }
};
auto bytes(const NativeFreedomFlightSession& session) -> std::string {
  return need(encode_native_save_document_json(need(session.save_document())));
}
auto resume(const NativeFreedomFlightSession& session)
    -> NativeFreedomFlightSession {
  return need(NativeFreedomFlightSession::open(need(native_select_save_document(
      need(decode_native_save_document_json(bytes(session)))))));
}
auto hit(NativeFreedomFlightSession session) -> NativeFreedomFlightSession {
  need(session.enable_impact_consequences());
  for (unsigned i = 0; i < 32 && !session.condition()->latest; ++i)
    need(session.advance({}));
  check(session.condition()->latest.has_value(),
        "Actual swept terrain contact produces an authoritative event");
  return session;
}
auto live_loss_batches(const NativeFreedomFlightSession& source) -> void {
  using godot_spike::SavedFlightWorld;
  const auto original = bytes(source);
  const std::array<double, 12> neutral{};
  auto invalid = [&](double elapsed, std::span<const double> fractions) {
    SavedFlightWorld world{source, {}, {}, .5};
    (void)need(world.clock.advance(kSimulationStep * .25));
    const auto accumulator = world.clock.accumulator();
    bool refused{};
    try {
      world.advance(elapsed, fractions, false);
    } catch (const std::exception&) {
      refused = true;
    }
    check(refused && bytes(world.session) == original &&
              !world.session.recovery_pending() && !world.last_step &&
              world.clock.accumulator() == accumulator &&
              world.dropped_seconds == .5,
          "Invalid native batch preserves source, scheduling and no loss");
  };
  for (const auto elapsed :
       {-1.0, 61.0, std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity()})
    invalid(elapsed, neutral);
  invalid(kSimulationStep.count(), {});
  invalid(kSimulationStep.count(), std::span{neutral}.first(11));
  const std::array<double, 13> too_many{};
  invalid(kSimulationStep.count(), too_many);
  for (const auto value : {-0.1, 1.1, std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity()}) {
    auto malformed = neutral;
    malformed[5] = value;
    invalid(kSimulationStep.count(), malformed);
  }
  // Independent fixed-tick physics supplies each whole pending-save oracle;
  // the native scheduler must stop at that same real event at every cadence.
  struct Schedule {
    double retained_steps, elapsed_steps;
  };
  constexpr std::array<Schedule, 4> schedules{
      {{0, 1}, {0, 2}, {.25, 2.5}, {0, 7200}}};
  for (const auto main : {0.0, .25}) {
    auto oracle = source;
    NativeFlightControls controls;
    controls.negative_translation.z = main;
    const auto last = need(oracle.advance(controls));
    const bool pending = oracle.recovery_pending() && oracle.condition() &&
                         oracle.condition()->latest.has_value();
    check(pending && oracle.condition()->latest &&
              oracle.condition()->latest->catastrophic &&
              oracle.document().flight.tick ==
                  source.document().flight.tick + 1,
          "Existing airborne fixture reaches actual loss in one fixed tick");
    if (!pending) continue;
    const auto expected = bytes(oracle);
    check(oracle.condition()->latest->serial ==
                  source.condition()->next_event &&
              oracle.condition()->next_event ==
                  source.condition()->next_event + 1 &&
              source.resources()->flight_quanta -
                      oracle.resources()->flight_quanta ==
                  last.fuel_debit_quanta &&
              (main == 0 ? last.fuel_debit_quanta == 0
                         : last.fuel_debit_quanta > 0),
          "Actual loss records one event and precisely its applied fuel bill");
    auto fractions = neutral;
    fractions[5] = main;
    for (const auto schedule : schedules) {
      SavedFlightWorld world{source, {}, {}, .5};
      (void)need(
          world.clock.advance(kSimulationStep * schedule.retained_steps));
      const auto elapsed = kSimulationStep * schedule.elapsed_steps;
      world.advance(elapsed.count(), fractions, false);
      const auto expected_drop =
          .5 + (kSimulationStep *
                (schedule.retained_steps + schedule.elapsed_steps - 1))
                   .count();
      check(bytes(world.session) == expected && world.last_step &&
                world.last_step->fuel_debit_quanta == last.fuel_debit_quanta &&
                world.last_step->actuation.after == last.actuation.after &&
                world.clock.accumulator() == SimulationSeconds::zero() &&
                std::abs(world.dropped_seconds - expected_drop) < 1e-12,
            "One/multi-tick loss preserves exact full owner and last impact "
            "step while dropping only the unadvanced frame tail");
      const auto drop = world.dropped_seconds;
      world.advance(1.0, fractions, true);
      check(bytes(world.session) == expected && world.dropped_seconds == drop &&
                world.clock.accumulator() == SimulationSeconds::zero(),
            "Paused pending loss cannot consume time, fuel or another event");
      bool refused{};
      try {
        world.advance(kSimulationStep.count(), fractions, false);
      } catch (const std::exception&) {
        refused = true;
      }
      check(
          refused && bytes(world.session) == expected &&
              world.dropped_seconds == drop && world.last_step &&
              world.last_step->fuel_debit_quanta == last.fuel_debit_quanta &&
              world.clock.accumulator() == SimulationSeconds::zero(),
          "Further pending advance refuses transactionally without rebilling");
    }
  }
  check(bytes(source) == original,
        "Native scheduler qualification never mutates its initialized source");
}
auto run(const std::filesystem::path& output) -> void {
  const Fixtures fixtures;
  auto clear = fixtures.airborne(0, false);
  const auto legacy = bytes(clear);
  auto legacy_resume = resume(clear);
  check(!legacy_resume.condition() && bytes(legacy_resume) == legacy,
        "Unselected complete voyage retains exact old bytes and no condition");
  need(clear.save_as(output / "unselected.json"));
  need(clear.enable_impact_consequences());
  need(clear.save_as(output / "selected-clear.json"));
  check(clear.condition()->forebody == 0 &&
            bytes(resume(clear)) == bytes(clear),
        "Explicit selection survives Save/Continue without invented damage");
  const auto before = bytes(clear);
  NativeFlightControls bad;
  bad.positive_translation.x = std::numeric_limits<double>::quiet_NaN();
  check(!clear.advance(bad) && bytes(clear) == before,
        "Nonfinite control refusal preserves physics, condition and gross "
        "resources atomically");

  auto gentle = fixtures.airborne(.1, true);
  need(gentle.enable_impact_consequences());
  for (unsigned tick = 0; tick < 32 && !gentle.surface()->landed; ++tick)
    need(gentle.advance({}));
  check(gentle.surface()->landed.has_value() &&
            gentle.condition()->forebody == 0 &&
            gentle.condition()->landing_structure == 0 &&
            !gentle.condition()->latest,
        "Selected gentle touchdown uses the existing landed path without "
        "inventing impact damage");
  for (unsigned tick = 0; tick < 32; ++tick)
    need(gentle.advance({}));
  check(!gentle.condition()->latest &&
            gentle.condition()->tick == gentle.document().flight.tick &&
            bytes(resume(gentle)) == bytes(gentle),
        "Selected landed idle and Continue preserve a pristine craft");
  need(gentle.release_surface());
  for (unsigned tick = 0; tick < 32; ++tick)
    need(gentle.advance({}));
  check(!gentle.surface()->landed && gentle.condition()->forebody == 0 &&
            gentle.condition()->landing_structure == 0,
        "Selected liftoff permits separating support contact without damage");

  auto forebody = hit(fixtures.airborne(4, false));
  check(forebody.condition()->forebody == 1 &&
            condition_view_cracked(*forebody.condition()) &&
            !condition_visibility_degraded(*forebody.condition()),
        "Actual forebody impact records the first glazing cue");
  need(forebody.save_as(output / "forebody1.json"));
  auto continued = resume(forebody);
  const auto serial = forebody.condition()->next_event;
  for (unsigned tick = 0; tick < 32; ++tick) {
    need(forebody.advance({}));
    need(continued.advance({}));
  }
  check(bytes(forebody) == bytes(continued) &&
            forebody.condition()->next_event == serial &&
            forebody.condition()->forebody == 1,
        "Resting gravity and Continue cannot rebill the same contact episode");
  auto harder = hit(fixtures.airborne(12, false));
  check(harder.condition()->forebody == 2 &&
            condition_visibility_degraded(*harder.condition()),
        "Harder actual forebody contact reduces visibility");
  need(harder.save_as(output / "forebody2.json"));
  auto landing = hit(fixtures.airborne(12, true));
  check(landing.condition()->landing_structure >= 2 &&
            !condition_allows_landing(*landing.condition()),
        "Confirmed support impact damages landing capability separately from "
        "glazing");
  need(landing.save_as(output / "landing-damaged.json"));
  const auto checksum = need(
      rigid_body_state_checksum({landing.system()}, landing.document().flight));
  check(!landing.commit_touchdown(checksum),
        "Damaged landing structure cannot certify safe touchdown");

  auto catastrophic_before = fixtures.airborne(80, false);
  need(catastrophic_before.enable_impact_consequences());
  need(catastrophic_before.save_as(output / "catastrophic-before.json"));
  live_loss_batches(catastrophic_before);
  auto loss = hit(std::move(catastrophic_before));
  check(loss.recovery_pending() && loss.condition()->latest->catastrophic &&
            !loss.advance({}),
        "Actual catastrophic impact commits one frozen standard loss");
  need(loss.save_as(output / "catastrophic-pending.json"));
  auto pending = resume(loss);
  const auto craft = loss.condition()->craft;
  need(loss.complete_recovery());
  need(pending.complete_recovery());
  check(
      bytes(loss) == bytes(pending) && loss.condition()->craft != craft &&
          loss.condition()->forebody == 0 && !loss.recovery_pending() &&
          loss.walker(),
      "Live and resumed recovery install one pristine individual replacement");
  const auto replacement = bytes(loss);
  need(loss.complete_recovery());
  check(bytes(loss) == replacement,
        "Repeated explicit completion cannot replace or reset twice");
  need(loss.advance_walk({}));
  check(loss.condition()->tick == loss.document().flight.tick &&
            bytes(resume(loss)) == bytes(loss),
        "Replacement station walking retains shared condition clock");
  need(loss.save_as(output / "recovered.json"));
}
} // namespace

int main(int argc, char** argv) {
  const bool fixtures = argc == 3 && std::string_view{argv[1]} == "--fixtures";
  if (argc != 1 && !fixtures) return 2;
  const auto output = fixtures
                          ? std::filesystem::absolute(argv[2])
                          : std::filesystem::temp_directory_path() /
                                ("apsis-native-impact-" +
                                 std::to_string(std::chrono::steady_clock::now()
                                                    .time_since_epoch()
                                                    .count()));
  try {
    std::filesystem::create_directories(output);
    run(output);
    if (!fixtures) std::filesystem::remove_all(output);
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    ++failures;
  }
  std::cout << "Native terrain impact: " << checks << " checks, " << failures
            << " failures\n";
  return failures ? 1 : 0;
}
