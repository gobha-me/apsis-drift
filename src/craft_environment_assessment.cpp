#include "apsis_drift/craft_environment_assessment.hpp"

#include <algorithm>

namespace apsis_drift {
namespace {
using Error = CraftEnvironmentError;
using Rating = CraftEnvironmentRating;
using Axis = CraftEnvironmentAxis;
using Kind = CraftEnvironmentMarginKind;
using Operation = CraftEnvironmentOperation;
// Bounded fictional starter equipment envelope, not measured material physics.
constexpr std::uint32_t minimum_temperature_mk{180'000};
constexpr std::uint32_t maximum_temperature_mk{450'000};
constexpr std::uint32_t radiation_nsv_per_hour{1'000'000};
constexpr std::uint32_t electrical_v_per_m{10'000};
constexpr std::uint32_t gust_mm_per_second{30'000};
constexpr std::uint32_t ground_mm_per_second2{1'000};

auto worse(Rating a, Rating b) -> Rating {
  return static_cast<Rating>(
      std::max(static_cast<unsigned>(a), static_cast<unsigned>(b)));
}
auto rating(std::int64_t remaining, std::uint64_t capacity) -> Rating {
  if (remaining < 0) return Rating::insufficient;
  return static_cast<std::uint64_t>(remaining) * 10 < capacity
             ? Rating::marginal
             : Rating::safe;
}
auto required_operation(Operation op) -> std::expected<CraftOperation, Error> {
  switch (op) {
    case Operation::orbital: return CraftOperation::orbital;
    case Operation::entry:
    case Operation::surface_flight: return CraftOperation::atmosphere;
    case Operation::touchdown: return CraftOperation::terrain_contact;
    case Operation::ascent: return CraftOperation::ascent;
  }
  return std::unexpected{Error::unsupported_operation};
}
auto needs_hazards(Kind kind) -> bool {
  return kind == Kind::minimum_temperature ||
         kind == Kind::maximum_temperature || kind == Kind::radiation ||
         kind == Kind::electrical || kind == Kind::gust ||
         kind == Kind::ground_motion || kind == Kind::support_load;
}
auto needs_physical(Kind kind) -> bool {
  return kind == Kind::gravity || kind == Kind::vertical_thrust ||
         kind == Kind::support_load;
}
auto needs_atmosphere(Kind kind) -> bool {
  return kind == Kind::pressure;
}
} // namespace

auto assess_reference_craft_environment(
    const PlanetAmbientEnvironment& environment, CraftFrameRecipe craft,
    Operation operation, std::uint32_t version)
    -> std::expected<CraftEnvironmentAssessment, Error> {
  if (version != kStarterEnvironmentAssessmentVersion)
    return std::unexpected{Error::unsupported_version};
  const auto required = required_operation(operation);
  if (!required) return std::unexpected{required.error()};
  if (!validate_planet_ambient_environment(environment))
    return std::unexpected{Error::invalid_environment};
  const auto frame = resolve_craft_frame(craft);
  if (!frame) return std::unexpected{Error::invalid_craft};
  const auto& p = frame->properties;
  CraftEnvironmentAssessment result;
  result.environment = environment.recipe;
  result.craft = craft;
  result.operation = operation;
  result.operation_supported = supports_operation(p, *required);
  result.rating =
      result.operation_supported ? Rating::safe : Rating::insufficient;
  if (operation == Operation::orbital) return result;
  const auto append = [&result](Kind kind, Axis axis, std::int64_t remaining,
                                std::uint64_t capacity) {
    const auto assessed = kind == Kind::vertical_thrust && remaining <= 0
                              ? Rating::insufficient
                              : rating(remaining, capacity);
    result.margins[result.count++] = {kind, axis, remaining, capacity,
                                      assessed};
    auto& group = result.axes[static_cast<std::size_t>(axis)];
    group = worse(group, assessed);
    result.rating = worse(result.rating, assessed);
  };
  const auto temperature_span = maximum_temperature_mk - minimum_temperature_mk;
  append(Kind::minimum_temperature, Axis::shielding_thermal,
         std::int64_t{environment.minimum_temperature_millikelvin} -
             minimum_temperature_mk,
         temperature_span);
  append(Kind::maximum_temperature, Axis::shielding_thermal,
         std::int64_t{maximum_temperature_mk} -
             environment.maximum_temperature_millikelvin,
         temperature_span);
  append(Kind::radiation, Axis::shielding_thermal,
         std::int64_t{radiation_nsv_per_hour} -
             environment.radiation_nanosieverts_per_hour,
         radiation_nsv_per_hour);
  append(Kind::electrical, Axis::shielding_thermal,
         std::int64_t{electrical_v_per_m} -
             environment.electric_field_volts_per_metre,
         electrical_v_per_m);
  append(Kind::pressure, Axis::structural,
         std::int64_t{p.max_pressure_millibars} -
             environment.surface_pressure.value,
         p.max_pressure_millibars);
  append(Kind::gust, Axis::structural,
         std::int64_t{gust_mm_per_second} -
             environment.gust_millimetres_per_second,
         gust_mm_per_second);
  // Convert nominal milli-g to mm/s^2 using exact 9.80665 m/s^2 and ceiling.
  const auto gravity =
      (std::uint64_t{environment.surface_gravity.value} * 980'665 + 99'999) /
      100'000;
  append(Kind::gravity, Axis::propulsion,
         std::int64_t{p.max_surface_gravity_mm_per_second2} -
             static_cast<std::int64_t>(gravity),
         p.max_surface_gravity_mm_per_second2);
  const auto weight = std::uint64_t{p.dry_mass_kg} * gravity;
  const auto thrust = std::uint64_t{p.positive_force_newtons[1]} * 1'000;
  append(Kind::vertical_thrust, Axis::propulsion,
         static_cast<std::int64_t>(thrust) - static_cast<std::int64_t>(weight),
         thrust);
  if (operation == Operation::touchdown) {
    append(Kind::ground_motion, Axis::structural,
           std::int64_t{ground_mm_per_second2} -
               environment.ground_acceleration_mm_per_second2,
           ground_mm_per_second2);
    std::uint64_t support = p.supports[0].rated_load_newtons;
    for (unsigned i = 1; i < p.support_count; ++i)
      support =
          std::min(support, std::uint64_t{p.supports[i].rated_load_newtons});
    support *= 1'000;
    const auto load =
        weight + std::uint64_t{p.dry_mass_kg} *
                     environment.ground_acceleration_mm_per_second2;
    append(Kind::support_load, Axis::structural,
           static_cast<std::int64_t>(support) - static_cast<std::int64_t>(load),
           support);
  }
  return result;
}

auto assess_craft_environment(const PhysicalLocalSystem& system,
                              const PlanetAmbientRecipe& recipe,
                              CraftFrameRecipe craft, Operation operation,
                              std::uint32_t version)
    -> std::expected<CraftEnvironmentAssessment, Error> {
  const auto e = resolve_planet_ambient_environment(system, recipe);
  if (!e) return std::unexpected{Error::invalid_environment};
  return assess_reference_craft_environment(*e, craft, operation, version);
}

auto query_known_craft_environment(const PhysicalLocalSystem& system,
                                   const PlanetAmbientRecipe& recipe,
                                   CraftFrameRecipe craft, Operation operation,
                                   const FreedomKnowledge& knowledge,
                                   SimulationTick tick, std::uint32_t version)
    -> std::expected<KnownCraftEnvironmentAssessment, Error> {
  const auto truth =
      assess_craft_environment(system, recipe, craft, operation, version);
  if (!truth) return std::unexpected{truth.error()};
  const auto& owner = knowledge.recipe;
  if (owner.physical_catalog != recipe.physical_catalog ||
      owner.ephemeris != recipe.ephemeris || owner.ambient != recipe.version ||
      (recipe.catalog_kind == LocalSystemKind::origin_home &&
       owner.universe_seed != recipe.origin_universe_seed))
    return std::unexpected{Error::owner_mismatch};
  if (!validate_freedom_knowledge(knowledge, tick))
    return std::unexpected{Error::invalid_knowledge};
  if (recipe.catalog_kind == LocalSystemKind::procedural) {
    const auto ids = generate_first_intersystem_identities(owner.universe_seed);
    if (owner.version != kFreedomTravelKnowledgeVersion ||
        recipe.system_seed != ids.target_system_seed)
      return std::unexpected{Error::owner_mismatch};
  }
  const KnowledgeSubject subject{KnowledgeSubjectKind::planet, recipe.system,
                                 recipe.planet.value};
  const auto physical = query_freedom_knowledge(knowledge, subject,
                                                KnowledgeFact::physical, tick);
  const auto atmosphere = query_freedom_knowledge(
      knowledge, subject, KnowledgeFact::atmosphere, tick);
  const auto hazards =
      query_freedom_knowledge(knowledge, subject, KnowledgeFact::hazards, tick);
  if (!physical || !atmosphere || !hazards)
    return std::unexpected{Error::invalid_knowledge};
  const bool physical_known =
      *physical && std::holds_alternative<KnownPhysical>((*physical)->value);
  const bool atmosphere_known =
      *atmosphere &&
      std::holds_alternative<KnownAtmosphere>((*atmosphere)->value);
  const bool hazards_known =
      *hazards && std::holds_alternative<KnownHazards>((*hazards)->value);
  KnownCraftEnvironmentAssessment result;
  result.craft = craft;
  result.operation = operation;
  result.count = truth->count;
  result.operation_supported = truth->operation_supported;
  result.rating =
      result.operation_supported ? Rating::safe : Rating::insufficient;
  for (std::size_t i = 0; i < result.count; ++i) {
    const auto& margin = truth->margins[i];
    const bool known = (!needs_physical(margin.kind) || physical_known) &&
                       (!needs_atmosphere(margin.kind) || atmosphere_known) &&
                       (!needs_hazards(margin.kind) || hazards_known);
    if (known) result.margins[i] = margin;
    auto& axis = result.axes[static_cast<std::size_t>(margin.axis)];
    axis = worse(axis, known ? margin.rating : Rating::unknown);
    result.rating =
        worse(result.rating, known ? margin.rating : Rating::unknown);
  }
  return result;
}
auto craft_environment_rating_name(Rating value) noexcept -> std::string_view {
  switch (value) {
    case Rating::safe: return "SAFE";
    case Rating::marginal: return "MARGINAL";
    case Rating::insufficient: return "INSUFFICIENT";
    case Rating::unknown: return "UNKNOWN";
  }
  return "UNKNOWN";
}
} // namespace apsis_drift
