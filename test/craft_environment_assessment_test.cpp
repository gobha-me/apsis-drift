#include "apsis_drift/craft_environment_assessment.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <source_location>

namespace {
using namespace apsis_drift;
using Rating = CraftEnvironmentRating;
using Operation = CraftEnvironmentOperation;
using Kind = CraftEnvironmentMarginKind;
int checks{}, failures{};
std::uint64_t fingerprint{14695981039346656037ULL};
auto check(bool ok, std::string_view note,
           std::source_location at = std::source_location::current()) -> void {
  ++checks;
  if (ok) return;
  ++failures;
  std::cerr << "FAIL line " << at.line() << ": " << note << '\n';
}
template <typename T, typename E> auto need(std::expected<T, E> value) -> T {
  if (!value) {
    std::cerr << "Required assessment fixture refused\n";
    std::exit(1);
  }
  return std::move(*value);
}
auto hash(std::uint64_t word) -> void {
  for (unsigned i = 0; i < 8; ++i) {
    fingerprint ^= word & 255U;
    fingerprint *= 1099511628211ULL;
    word >>= 8U;
  }
}
auto remember(const CraftEnvironmentAssessment& result) -> void {
  hash(result.environment.system.value);
  hash(result.environment.planet.value);
  hash(result.craft.id.value);
  hash(static_cast<unsigned>(result.operation));
  hash(static_cast<unsigned>(result.rating));
  hash(result.count);
  for (std::size_t i = 0; i < result.count; ++i) {
    const auto& margin = result.margins[i];
    hash(static_cast<unsigned>(margin.kind));
    hash(static_cast<unsigned>(margin.axis));
    hash(static_cast<std::uint64_t>(margin.remaining));
    hash(margin.reference_capacity);
    hash(static_cast<unsigned>(margin.rating));
  }
}
auto margin(const CraftEnvironmentAssessment& r, Kind kind)
    -> CraftEnvironmentMargin {
  for (std::size_t i = 0; i < r.count; ++i)
    if (r.margins[i].kind == kind) return r.margins[i];
  std::cerr << "Missing required margin\n";
  std::exit(1);
}
auto fixtures() -> void {
  const Seed seed{42};
  const auto world = need(generate_physical_origin_system(seed, 2));
  const auto snapshot = need(generate_physical_origin_system(seed, 2));
  const auto recipe = need(generate_planet_ambient_recipe(
      world, generate_origin_home_planet(world.catalog.seed).id));
  const auto environment =
      need(resolve_planet_ambient_environment(world, recipe));
  const auto craft = wayfarer_frame().recipe;
  const auto result = need(
      assess_craft_environment(world, recipe, craft, Operation::touchdown));
  check(result.count == 10 && result.rating == Rating::safe,
        "Authored home has safe fixed starter reference on all three axes");
  auto one_g = environment;
  one_g.surface_gravity.value = 1000;
  const auto reference = need(
      assess_reference_craft_environment(one_g, craft, Operation::touchdown));
  check(
      margin(reference, Kind::vertical_thrust).remaining == 129544000 &&
          margin(reference, Kind::vertical_thrust).reference_capacity ==
              208000000,
      "Independent 8000 kg, 1-g ceiling oracle retains 129544 N lift reserve");
  check(need(assess_craft_environment(world, recipe, craft, Operation::orbital))
                .count == 0,
        "Orbital operation does not inherit surface climate exposure");
  auto changed = environment;
  changed.radiation_nanosieverts_per_hour = 1'000'001;
  auto assessed = need(
      assess_reference_craft_environment(changed, craft, Operation::touchdown));
  check(assessed.axes[0] == Rating::insufficient &&
            assessed.axes[1] == result.axes[1] &&
            assessed.axes[2] == result.axes[2],
        "Radiation cannot spend hull or thrust reserve");
  changed = environment;
  changed.surface_gravity.value = 1800;
  assessed = need(
      assess_reference_craft_environment(changed, craft, Operation::ascent));
  check(assessed.axes[2] == Rating::marginal &&
            margin(assessed, Kind::gravity).remaining == 348 &&
            margin(assessed, Kind::vertical_thrust).remaining == 66784000,
        "High gravity reduces propulsion margins without changing shielding");
  changed = environment;
  changed.surface_gravity.value = 1000;
  changed.ground_acceleration_mm_per_second2 = 20'000;
  assessed = need(
      assess_reference_craft_environment(changed, craft, Operation::touchdown));
  check(margin(assessed, Kind::support_load).remaining == -78456000 &&
            assessed.axes[1] == Rating::insufficient &&
            assessed.axes[2] == result.axes[2],
        "Whole dry weight plus ground acceleration can exceed individual pad "
        "rating");
  check(
      need(assess_reference_craft_environment(changed, craft, Operation::entry))
              .rating == Rating::safe,
      "Ground motion does not affect an airborne operation");
  for (const auto radiation :
       {899999U, 900000U, 900001U, 999999U, 1000000U, 1000001U}) {
    changed = environment;
    changed.radiation_nanosieverts_per_hour = radiation;
    const auto m = margin(need(assess_reference_craft_environment(
                              changed, craft, Operation::surface_flight)),
                          Kind::radiation);
    const auto expected = radiation <= 900000    ? Rating::safe
                          : radiation <= 1000000 ? Rating::marginal
                                                 : Rating::insufficient;
    check(m.rating == expected,
          "Ten-percent headroom and inclusive rating edges");
  }
  for (unsigned bad = 0; bad < 6; ++bad) {
    changed = environment;
    auto frame = craft;
    auto op = Operation::touchdown;
    std::uint32_t version{1};
    switch (bad) {
      case 0: version = 0; break;
      case 1: frame.version = 999; break;
      case 2: frame.id.value = 0; break;
      case 3: op = static_cast<Operation>(255); break;
      case 4: changed.surface_gravity.value = 0; break;
      case 5: changed.maximum_temperature_millikelvin = 1'400'001; break;
    }
    check(!assess_reference_craft_environment(changed, frame, op, version),
          "Invalid recipe, operation, frame and environment refuse");
  }
  auto forged = recipe;
  forged.system_seed.value ^= 1;
  check(!assess_craft_environment(world, forged, craft, Operation::entry),
        "Matching numerical climate cannot replace owned generated truth");
  auto k = need(make_freedom_starting_knowledge(seed, 3));
  const auto initial = k;
  auto known = need(query_known_craft_environment(world, recipe, craft,
                                                  Operation::touchdown, k, 0));
  check(known.rating == Rating::unknown &&
            std::ranges::none_of(known.margins,
                                 [](const auto& m) { return m.has_value(); }),
        "Starting chart grants no private margin or sign");
  const auto pilot = make_freedom_new_game_document(seed).state.craft;
  const KnowledgeSubject subject{KnowledgeSubjectKind::planet, recipe.system,
                                 recipe.planet.value};
  SimulationTick tick{};
  for (const auto fact : {KnowledgeFact::physical, KnowledgeFact::atmosphere,
                          KnowledgeFact::hazards}) {
    ++tick;
    const KnowledgeEvidence contact{subject,
                                    fact,
                                    {NavigationKnowledgeLevel::probable,
                                     KnowledgeSource::local_ship, pilot.value,
                                     tick, pilot}};
    k = need(apply_freedom_knowledge(k, contact, tick));
    known = need(query_known_craft_environment(world, recipe, craft,
                                               Operation::touchdown, k, tick));
    check(known.rating == Rating::unknown,
          "Probable reading retains redacted dependent margins");
    auto resolved = contact;
    resolved.transition.level = NavigationKnowledgeLevel::resolved;
    resolved.transition.tick = ++tick;
    k = need(apply_freedom_knowledge(k, resolved, tick));
  }
  known = need(query_known_craft_environment(world, recipe, craft,
                                             Operation::touchdown, k, tick));
  check(known.rating == result.rating && known.axes == result.axes,
        "Resolved ledger and simulation agree on the same result");
  for (std::size_t i = 0; i < result.count; ++i)
    check(known.margins[i] == result.margins[i],
          "Resolved exact margins agree");
  auto invalid_k = k;
  ++invalid_k.recipe.ambient;
  check(!query_known_craft_environment(world, recipe, craft, Operation::entry,
                                       invalid_k, tick),
        "Unsupported hazard selection refuses transactionally");
  check(!query_known_craft_environment(
            world, recipe, craft, Operation::entry, k,
            std::numeric_limits<SimulationTick>::max()),
        "Clock overflow refuses before returning knowledge");
  const auto unrelated =
      need(generate_physical_local_system(Seed{987654321}, 2));
  const auto unrelated_recipe = need(generate_planet_ambient_recipe(
      unrelated, unrelated.catalog.planets.front().descriptor.id));
  check(!query_known_craft_environment(unrelated, unrelated_recipe, craft,
                                       Operation::entry, k, tick),
        "Unrelated procedural world cannot masquerade as unknown owned body");
  check(world == snapshot &&
            initial == need(make_freedom_starting_knowledge(seed, 3)),
        "Queries neither mutate world nor grant knowledge");
}
auto matrix() -> void {
  for (std::uint64_t seed = 0; seed < 24; ++seed) {
    const auto world = need(generate_physical_origin_system(Seed{seed}, 2));
    for (const auto& body : world.catalog.planets) {
      const auto recipe =
          need(generate_planet_ambient_recipe(world, body.descriptor.id));
      for (const auto frame :
           {starter_shuttle_frame().recipe, wayfarer_frame().recipe})
        for (const auto operation :
             {Operation::orbital, Operation::entry, Operation::surface_flight,
              Operation::touchdown, Operation::ascent}) {
          const auto result =
              need(assess_craft_environment(world, recipe, frame, operation));
          check(result.count <= kCraftEnvironmentMarginCount,
                "Every generated operation remains bounded");
          remember(result);
        }
    }
  }
}
} // namespace
auto main() -> int {
  fixtures();
  matrix();
  std::cout << checks << " environment assessment checks, " << failures
            << " failures, fingerprint " << fingerprint << '\n';
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
