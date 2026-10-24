#include "apsis_drift/freedom_derelict_recovery.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {
using namespace apsis_drift;
using State = FreedomDerelictRecoveryState;
constexpr Seed seed{42};
unsigned checks{};
auto check(bool ok, std::string_view message) -> void {
  ++checks;
  if (!ok) throw std::runtime_error(std::string{message});
}
template <class T, class E>
auto need(std::expected<T, E> value,
          std::source_location at = std::source_location::current()) -> T {
  if (!value) {
    auto error = "Recovery fixture line " + std::to_string(at.line());
    if constexpr (std::is_same_v<E, std::string>) error += ": " + value.error();
    throw std::runtime_error(error);
  }
  if constexpr (!std::is_void_v<T>) {
    if constexpr (std::is_trivially_copyable_v<T>)
      return *value;
    else
      return std::move(*value);
  }
}
struct Fixture {
  std::unique_ptr<FreedomDerelictDescriptor> descriptor{
      std::make_unique<FreedomDerelictDescriptor>(
          need(generate_freedom_derelict(seed)))};
  StarterCraftId craft{need(derive_freedom_craft_identity(seed, 0))};
  // These are initialized canonical observer boundaries, not an actual flown
  // approach, retrieval voyage, transfer animation or profitable player trip.
  [[nodiscard]] auto observer(SimulationTick tick, double gap = 125,
                              double speed = 0) const
      -> FreedomDerelictObservation {
    const auto pose = need(resolve_freedom_derelict(*descriptor, tick));
    return {craft,
            tick,
            {pose.position_metres.x, pose.position_metres.y,
             pose.position_metres.z + gap},
            {pose.linear_velocity_metres_per_second.x + speed,
             pose.linear_velocity_metres_per_second.y,
             pose.linear_velocity_metres_per_second.z}};
  }
  [[nodiscard]] auto inspected() const -> FreedomDerelictState {
    return {1, observer(10), observer(11)};
  }
};
auto assessment_boundaries(const Fixture& fixture) -> void {
  for (double gap : {100.0, 125.0, 150.0}) {
    for (double speed : {0.0, 2.0}) {
      const auto observation = fixture.observer(100, gap, speed);
      const auto result =
          need(assess_freedom_derelict_retrieval(seed, observation));
      check(result.eligible && std::abs(result.distance_metres - gap) < 1e-7 &&
                std::abs(result.relative_speed_metres_per_second - speed) <
                    1e-7,
            "Canonical source-relative distance/speed inclusive endpoints "
            "refused");
    }
  }
  for (const auto& pair : std::array<std::array<double, 2>, 4>{
           {{99.99, 0}, {150.01, 0}, {125, 2.01}, {0, 0}}}) {
    const auto result = need(assess_freedom_derelict_retrieval(
        seed, fixture.observer(100, pair[0], pair[1])));
    check(!result.eligible, "Finite outside-envelope observation was treated "
                            "as an eligible retrieval");
  }
  const auto original = fixture.observer(100);
  for (unsigned component = 0; component < 6; ++component) {
    for (double value : {std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity()}) {
      auto bad = original;
      std::array<double*, 6> fields{&bad.position_metres.x,
                                    &bad.position_metres.y,
                                    &bad.position_metres.z,
                                    &bad.velocity_metres_per_second.x,
                                    &bad.velocity_metres_per_second.y,
                                    &bad.velocity_metres_per_second.z};
      *fields[component] = value;
      check(!assess_freedom_derelict_retrieval(seed, bad),
            "Nonfinite component reached source separation arithmetic");
    }
  }
  auto bad = original;
  bad.position_metres.x = kRigidBodyMaximumPositionMetres * 2;
  check(!assess_freedom_derelict_retrieval(seed, bad),
        "Out-of-bound observer position accepted");
  bad = original;
  bad.velocity_metres_per_second.y =
      kRigidBodyMaximumVelocityMetresPerSecond * 2;
  check(!assess_freedom_derelict_retrieval(seed, bad),
        "Out-of-bound observer velocity accepted");
  bad = original;
  bad.craft = {};
  check(!assess_freedom_derelict_retrieval(seed, bad),
        "Missing observer craft accepted");
  bad = original;
  bad.tick = std::numeric_limits<SimulationTick>::max();
  check(!assess_freedom_derelict_retrieval(seed, bad),
        "Reserved maximum observation tick accepted");
}
auto validation_and_begin(const Fixture& fixture) -> void {
  const State empty;
  const auto records = fixture.inspected();
  const auto observation = fixture.observer(100);
  check(validate_freedom_derelict_recovery(seed, empty, {}, 0).has_value() &&
            !empty.active && !empty.canister,
        "Selected empty state needs neither hidden observations nor physical "
        "cargo");
  auto bad = empty;
  bad.version = 0;
  check(!validate_freedom_derelict_recovery(seed, bad, records, 100) &&
            !begin_freedom_derelict_retrieval(seed, bad, records, observation),
        "Unsupported policy entered retrieval");
  bad.version = 2;
  check(!validate_freedom_derelict_recovery(seed, bad, records, 100),
        "Future policy accepted");
  check(!validate_freedom_derelict_recovery(
            seed, empty, records, std::numeric_limits<SimulationTick>::max()) &&
            !begin_freedom_derelict_retrieval(seed, empty, {}, observation) &&
            !sell_freedom_derelict_canister(seed, empty, records, 100),
        "Invalid global clock, missing inspection or notes-only sale accepted");
  auto forged = records;
  forged.inspection->position_metres.x =
      std::numeric_limits<double>::quiet_NaN();
  check(!begin_freedom_derelict_retrieval(seed, empty, forged, observation) &&
            !validate_freedom_derelict_recovery({43}, empty, records, 100),
        "Malformed/foreign source inspection was accepted");
  auto active =
      need(begin_freedom_derelict_retrieval(seed, empty, records, observation));
  check(active.active && active.active->started_tick == 100 &&
            active.active->last_tick == 100 &&
            active.active->acquired_ticks == 0 &&
            active.active->observer == observation &&
            validate_freedom_derelict_recovery(seed, active, records, 1000)
                .has_value(),
        "Begin gained time or historical active reading refused at a later "
        "owner tick");
  check(!begin_freedom_derelict_retrieval(seed, active, records, observation) &&
            !advance_freedom_derelict_retrieval(seed, empty, records,
                                                observation),
        "Repeated begin or idle advance implicitly started work");
  check(!begin_freedom_derelict_retrieval(seed, empty, records,
                                          fixture.observer(10)) &&
            !begin_freedom_derelict_retrieval(seed, empty, records,
                                              fixture.observer(100, 200)),
        "Future inspection or insufficient stand-off was accepted");
  for (unsigned damage = 0; damage < 7; ++damage) {
    bad = active;
    switch (damage) {
      case 0: bad.active->started_tick = 101; break;
      case 1: bad.active->last_tick = 101; break;
      case 2: bad.active->observer.tick = 99; break;
      case 3: bad.active->acquired_ticks = 1; break;
      case 4:
        bad.active->acquired_ticks = kFreedomDerelictRetrievalTicks;
        break;
      case 5: bad.active->observer.position_metres.z += 200; break;
      case 6: bad.active->observer.craft = {}; break;
    }
    check(
        !validate_freedom_derelict_recovery(seed, bad, records, 100),
        "Malformed active clock, count or finite source observation accepted");
  }
  check(!validate_freedom_derelict_recovery(seed, active, records, 99),
        "Active observation from the future accepted");
  const auto maximum = std::numeric_limits<SimulationTick>::max();
  const auto last_start = maximum - 1 - kFreedomDerelictRetrievalTicks;
  check(begin_freedom_derelict_retrieval(seed, empty, records,
                                         fixture.observer(last_start))
                .has_value() &&
            !begin_freedom_derelict_retrieval(seed, empty, records,
                                              fixture.observer(last_start + 1)),
        "Begin cannot complete within the last representable ordinary clock");
}
auto cancellation(const Fixture& fixture) -> void {
  const auto records = fixture.inspected();
  const auto started = need(begin_freedom_derelict_retrieval(
      seed, {}, records, fixture.observer(100)));
  const auto one = need(advance_freedom_derelict_retrieval(
      seed, started, records, fixture.observer(101)));
  check(one.active && one.active->acquired_ticks == 1 &&
            one.active->started_tick == 100 &&
            one.active->observer == fixture.observer(101),
        "Consecutive real observation failed to earn precisely one interval");
  check(!advance_freedom_derelict_retrieval(seed, one, records,
                                            fixture.observer(101)) &&
            !advance_freedom_derelict_retrieval(seed, one, records,
                                                fixture.observer(100)),
        "Duplicate or backward tick advanced progress");
  auto malformed = fixture.observer(103);
  malformed.position_metres.x = std::numeric_limits<double>::quiet_NaN();
  check(!advance_freedom_derelict_retrieval(seed, one, records, malformed),
        "Malformed forward gap canceled instead of refusing");
  const auto gap = need(advance_freedom_derelict_retrieval(
      seed, one, records, fixture.observer(103)));
  check(
      !gap.active && !gap.canister && one.active->acquired_ticks == 1,
      "Forward gap backfilled, banked or claimed an item instead of canceling");
  auto changed = fixture.observer(102);
  changed.craft = need(derive_freedom_craft_identity(seed, 1));
  for (const auto& observer :
       {fixture.observer(102, 151), fixture.observer(102, 99),
        fixture.observer(102, 125, 2.01), changed}) {
    const auto canceled =
        need(advance_freedom_derelict_retrieval(seed, one, records, observer));
    check(!canceled.active && !canceled.canister,
          "Envelope exit or craft change retained unfinished work");
  }
  check(cancel_freedom_derelict_retrieval(one) == State{} &&
            lose_freedom_derelict_canister(one) == State{} &&
            cancel_freedom_derelict_retrieval(gap) == gap,
        "Explicit cancel/loss claimed cargo or changed already empty state");
  const auto retried = need(begin_freedom_derelict_retrieval(
      seed, gap, records, fixture.observer(104)));
  check(retried.active->acquired_ticks == 0 &&
            retried.active->started_tick == 104,
        "Explicit retry inherited canceled acquisition time");
}
auto complete_and_deplete(const Fixture& fixture) -> void {
  const auto records = fixture.inspected();
  auto state = need(begin_freedom_derelict_retrieval(seed, {}, records,
                                                     fixture.observer(100)));
  // One explicit sequence of3600 canonical post-tick observations. The pure
  // provider authenticates these samples; this is not a physical flight pilot.
  for (std::uint32_t interval = 1; interval <= kFreedomDerelictRetrievalTicks;
       ++interval) {
    state = need(advance_freedom_derelict_retrieval(
        seed, state, records, fixture.observer(100 + interval)));
    if (interval == kFreedomDerelictRetrievalTicks - 1)
      check(state.active && state.active->acquired_ticks == 3599 &&
                !state.canister,
            "3599 intervals claimed cargo early");
  }
  check(
      !state.active && state.canister && state.canister->started_tick == 100 &&
          state.canister->completed == fixture.observer(3700) &&
          state.canister->disposition == FreedomCanisterDisposition::carried &&
          !state.canister->sold_tick &&
          validate_freedom_derelict_recovery(seed, state, records, 4000)
              .has_value(),
      "Exactly3600 consecutive intervals did not create one authentic "
      "permanent claim");
  const auto carried = state;
  const auto lost = lose_freedom_derelict_canister(carried);
  const auto sold =
      need(sell_freedom_derelict_canister(seed, carried, records, 3800));
  check(lost.canister->disposition == FreedomCanisterDisposition::lost &&
            lost.canister->completed == carried.canister->completed &&
            lose_freedom_derelict_canister(lost) == lost &&
            !sell_freedom_derelict_canister(seed, lost, records, 4000),
        "Loss erased source depletion, changed notes or sold cargo from "
        "retained history");
  check(sold.canister->disposition == FreedomCanisterDisposition::sold &&
            sold.canister->sold_tick == 3800 &&
            sold.canister->completed == carried.canister->completed &&
            lose_freedom_derelict_canister(sold) == sold &&
            !sell_freedom_derelict_canister(seed, sold, records, 4000) &&
            !sell_freedom_derelict_canister(seed, carried, records, 3699),
        "Sale repeated, rewrote recovery provenance or preceded claim "
        "completion");
  for (const auto& depleted : {carried, lost, sold})
    check(!begin_freedom_derelict_retrieval(seed, depleted, records,
                                            fixture.observer(4000)) &&
              !advance_freedom_derelict_retrieval(seed, depleted, records,
                                                  fixture.observer(4000)) &&
              cancel_freedom_derelict_retrieval(depleted) == depleted,
          "A carried, lost or sold source respawned or was erased by "
          "cancellation");
  for (unsigned damage = 0; damage < 8; ++damage) {
    auto bad = carried;
    switch (damage) {
      case 0:
        bad.active =
            FreedomDerelictRetrieval{100, 100, 0, fixture.observer(100)};
        break;
      case 1: ++bad.canister->started_tick; break;
      case 2:
        bad.canister->disposition =
            static_cast<FreedomCanisterDisposition>(255);
        break;
      case 3: bad.canister->sold_tick = 3800; break;
      case 4:
        bad.canister->disposition = FreedomCanisterDisposition::sold;
        break;
      case 5:
        bad = sold;
        bad.canister->sold_tick = 3699;
        break;
      case 6:
        bad = sold;
        bad.canister->sold_tick = 4001;
        break;
      case 7: bad.canister->completed.position_metres.z += 200; break;
    }
    check(!validate_freedom_derelict_recovery(seed, bad, records, 4000),
          "Malformed exclusive claim, enum, completion or sale timestamps "
          "accepted");
  }
  check(carried.canister->disposition == FreedomCanisterDisposition::carried &&
            !carried.canister->sold_tick && records == fixture.inspected(),
        "Pure cancellation/loss/sale modified its source or earned inspection");
}
} // namespace
auto main() -> int {
  try {
    const Fixture fixture;
    assessment_boundaries(fixture);
    validation_and_begin(fixture);
    cancellation(fixture);
    complete_and_deplete(fixture);
    std::cout << "Derelict recovery: " << checks << " checks, 0 failures\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Derelict recovery after " << checks
              << " checks: " << error.what() << '\n';
    return 1;
  }
}
