#include "apsis_drift/craft_frame.hpp"
#include "apsis_drift/planet.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

namespace {
using namespace apsis_drift;

int failures{};

auto check(bool condition, std::string_view message) -> void {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}

template <typename T>
auto check_error(const std::expected<T, CraftFrameError>& result,
                 CraftFrameError error, std::string_view message) -> void {
  check(!result && result.error() == error, message);
}

constexpr auto flag(CraftOperation operation) -> std::uint32_t {
  return static_cast<std::uint32_t>(operation);
}

template <typename Mutation>
auto rejects(Mutation mutation, CraftFrameError error, std::string_view message)
    -> void {
  auto properties = starter_shuttle_frame().properties;
  mutation(properties);
  check_error(validate_craft_frame_properties(properties), error, message);
}

auto identity_contract() -> void {
  static_assert(std::is_const_v<decltype(CraftFrameDescriptor::recipe)>);
  static_assert(std::is_const_v<decltype(CraftFrameDescriptor::properties)>);
  const auto& frame = starter_shuttle_frame();
  const CraftFrameProperties expected_properties{
      .axes = CraftBodyAxes::right_up_back,
      .occupant_capacity = 4,
      .pilot_seats = 1,
      .operations = 255,
      .dry_mass_kg = 8000,
      .principal_inertia_kg_m2 = {260000, 380000, 140000},
      .hull_min_mm = {-7000, -600, -10000},
      .hull_max_mm = {7000, 3335, 9320},
      .center_of_mass_mm = {0, 0, 0},
      .positive_force_newtons = {112000, 208000, 72000},
      .negative_force_newtons = {112000, 144000, 360000},
      .torque_newton_metres = {728000, 1064000, 560000},
      .max_angular_rate_milliradians_per_second = {1150, 900, 1700},
      .drag_area_square_mm = {54000000, 80000000, 3840000},
      .max_surface_gravity_mm_per_second2 = 18000,
      .max_pressure_millibars = 2500,
      .max_touchdown_vertical_mm_per_second = 2000,
      .max_touchdown_horizontal_mm_per_second = 3000,
      .max_touchdown_angular_milliradians_per_second = 100,
      .max_slope_millidegrees = 12000,
      .support_count = 3,
      .supports = {{{{0, -1328, -6700}, 375, 550, 300, 160000},
                    {{-3500, -1328, 2700}, 375, 550, 300, 160000},
                    {{3500, -1328, 2700}, 375, 550, 300, 160000},
                    {}}}};
  check(frame.properties == expected_properties,
        "every immutable starter quantity has an exact reviewed golden");
  check(frame.recipe == CraftFrameRecipe{kStarterShuttleFrameId,
                                         kStarterShuttleFrameVersion},
        "starter identity is explicit and versioned");
  check(!frame.diagnostic_name.empty(), "starter has a diagnostic name");
  check(validate_craft_frame_properties(frame.properties).has_value(),
        "registered starter properties validate");
  const auto resolved = resolve_craft_frame(frame.recipe);
  check(resolved && *resolved == frame,
        "recipe reconstructs the exact immutable descriptor");
  check_error(resolve_craft_frame({CraftFrameId{0}, frame.recipe.version}),
              CraftFrameError::unknown_id, "zero frame ID is not registered");
  check_error(resolve_craft_frame({CraftFrameId{2}, frame.recipe.version}),
              CraftFrameError::unknown_id, "future frame ID is not registered");
  check_error(resolve_craft_frame(
                  {CraftFrameId{std::numeric_limits<std::uint64_t>::max()},
                   frame.recipe.version}),
              CraftFrameError::unknown_id, "maximum frame ID is rejected");
  for (const auto version : {std::uint32_t{0}, std::uint32_t{2},
                             std::numeric_limits<std::uint32_t>::max()}) {
    check_error(resolve_craft_frame({frame.recipe.id, version}),
                CraftFrameError::unsupported_version,
                "unsupported descriptor version is rejected");
  }
  for (const auto operation :
       {CraftOperation::vacuum, CraftOperation::atmosphere,
        CraftOperation::terrain_contact, CraftOperation::landed,
        CraftOperation::liftoff, CraftOperation::ascent,
        CraftOperation::orbital, CraftOperation::docking}) {
    check(supports_operation(frame.properties, operation),
          "starter independently names each supported operation");
  }
  check(!supports_operation(frame.properties, static_cast<CraftOperation>(0)),
        "zero operation is not a supported class");
  check(!supports_operation(frame.properties,
                            static_cast<CraftOperation>(1U << 31U)),
        "unknown operation is not supported");

  const auto checksum = craft_frame_checksum(frame);
  const auto diagnostic = craft_frame_diagnostic_json(frame);
  check(checksum.has_value() && diagnostic.has_value(),
        "canonical frame has checksum and diagnostic projections");
  // Independently calculated from 319 explicit bytes: domain/name ASCII,
  // little-endian fixed widths and two's-complement signed positions; FNV-1a.
  check(checksum && *checksum == 3921801715917627138ULL,
        "starter field-wise checksum matches the independent golden");
  constexpr std::string_view expected_diagnostic =
      R"({"frame_id":"1","descriptor_version":1,"name":"freedom-shuttle-v1","axes":"right-up-back",)"
      R"("occupant_capacity":4,"pilot_seats":1,"operations":255,"dry_mass_kg":8000,)"
      R"("principal_inertia_kg_m2":[260000,380000,140000],"hull_min_mm":[-7000,-600,-10000],)"
      R"("hull_max_mm":[7000,3335,9320],"center_of_mass_mm":[0,0,0],)"
      R"("positive_force_newtons":[112000,208000,72000],"negative_force_newtons":[112000,144000,360000],)"
      R"("torque_newton_metres":[728000,1064000,560000],"max_angular_rate_milliradians_per_second":[1150,900,1700],)"
      R"("drag_area_square_mm":[54000000,80000000,3840000],"max_surface_gravity_mm_per_second2":18000,)"
      R"("max_pressure_millibars":2500,"max_touchdown_vertical_mm_per_second":2000,)"
      R"("max_touchdown_horizontal_mm_per_second":3000,"max_touchdown_angular_milliradians_per_second":100,)"
      R"("max_slope_millidegrees":12000,"supports":[)"
      R"({"contact_mm":[0,-1328,-6700],"half_width_mm":375,"half_length_mm":550,"stroke_mm":300,"rated_load_newtons":160000},)"
      R"({"contact_mm":[-3500,-1328,2700],"half_width_mm":375,"half_length_mm":550,"stroke_mm":300,"rated_load_newtons":160000},)"
      R"({"contact_mm":[3500,-1328,2700],"half_width_mm":375,"half_length_mm":550,"stroke_mm":300,"rated_load_newtons":160000}],)"
      R"("checksum":"3921801715917627138"})";
  check(diagnostic && *diagnostic == expected_diagnostic,
        "canonical diagnostic JSON has exact names, order, units and values");
  check(resolved && craft_frame_checksum(*resolved) == checksum &&
            craft_frame_diagnostic_json(*resolved) == diagnostic,
        "resolved projections are identical");

  auto altered = frame.properties;
  ++altered.dry_mass_kg;
  const CraftFrameDescriptor impostor{frame.recipe, frame.diagnostic_name,
                                      altered};
  check(validate_craft_frame_properties(altered).has_value(),
        "a physically plausible alteration remains a valid definition");
  check_error(craft_frame_checksum(impostor),
              CraftFrameError::definition_mismatch,
              "physical validity does not authorize replacing a stable frame");
  check_error(craft_frame_diagnostic_json(impostor),
              CraftFrameError::definition_mismatch,
              "diagnostics refuse descriptor identity substitution");
  const CraftFrameDescriptor renamed{frame.recipe, "different-name",
                                     frame.properties};
  check_error(craft_frame_checksum(renamed),
              CraftFrameError::definition_mismatch,
              "canonical diagnostic name belongs to immutable identity");
}

auto invalid_properties_contract() -> void {
  rejects([](auto& p) { p.axes = static_cast<CraftBodyAxes>(0); },
          CraftFrameError::invalid_axes, "unknown body convention rejects");
  rejects([](auto& p) { p.occupant_capacity = 0; },
          CraftFrameError::invalid_capacity, "zero occupant capacity rejects");
  rejects([](auto& p) { p.pilot_seats = 0; }, CraftFrameError::invalid_capacity,
          "zero pilot capacity rejects");
  rejects([](auto& p) { p.pilot_seats = p.occupant_capacity + 1; },
          CraftFrameError::invalid_capacity, "pilots cannot exceed occupants");
  rejects([](auto& p) { p.operations = 0; },
          CraftFrameError::invalid_operations, "empty operation set rejects");
  rejects([](auto& p) { p.operations |= 1U << 31U; },
          CraftFrameError::invalid_operations, "unknown operation bits reject");
  rejects(
      [](auto& p) { p.operations &= ~flag(CraftOperation::terrain_contact); },
      CraftFrameError::invalid_operations,
      "landed capability requires terrain contact");
  rejects([](auto& p) { p.operations &= ~flag(CraftOperation::landed); },
          CraftFrameError::invalid_operations,
          "liftoff capability requires landed capability");
  rejects([](auto& p) { p.dry_mass_kg = 0; }, CraftFrameError::invalid_mass,
          "zero mass rejects");
  rejects(
      [](auto& p) {
        p.dry_mass_kg = std::numeric_limits<std::uint32_t>::max();
      },
      CraftFrameError::invalid_mass, "unbounded mass rejects");
  for (std::size_t axis = 0; axis < 3; ++axis) {
    rejects([axis](auto& p) { p.principal_inertia_kg_m2[axis] = 0; },
            CraftFrameError::invalid_inertia,
            "every principal moment is positive");
    rejects(
        [axis](auto& p) {
          p.principal_inertia_kg_m2[axis] =
              std::numeric_limits<std::uint64_t>::max();
        },
        CraftFrameError::invalid_inertia, "inertia overflow rejects safely");
    rejects([axis](auto& p) { p.hull_max_mm[axis] = p.hull_min_mm[axis]; },
            CraftFrameError::invalid_bounds, "zero hull extent rejects");
    rejects([axis](auto& p) { p.hull_min_mm[axis] = p.hull_max_mm[axis] + 1; },
            CraftFrameError::invalid_bounds, "inverted hull extent rejects");
    rejects(
        [axis](auto& p) {
          p.hull_min_mm[axis] = std::numeric_limits<std::int32_t>::min();
          p.hull_max_mm[axis] = std::numeric_limits<std::int32_t>::max();
        },
        CraftFrameError::invalid_bounds,
        "signed extent overflow rejects safely");
    rejects(
        [axis](auto& p) {
          p.center_of_mass_mm[axis] = p.hull_max_mm[axis] + 1;
        },
        CraftFrameError::invalid_bounds, "mass center outside hull rejects");
    rejects([axis](auto& p) { p.positive_force_newtons[axis] = 0; },
            CraftFrameError::invalid_authority,
            "positive-axis authority is required");
    rejects([axis](auto& p) { p.negative_force_newtons[axis] = 0; },
            CraftFrameError::invalid_authority,
            "negative-axis authority is required");
    rejects([axis](auto& p) { p.torque_newton_metres[axis] = 0; },
            CraftFrameError::invalid_authority,
            "each torque authority is positive");
    rejects(
        [axis](auto& p) {
          p.max_angular_rate_milliradians_per_second[axis] = 0;
        },
        CraftFrameError::invalid_authority, "angular rate bounds are positive");
    rejects(
        [axis](auto& p) {
          p.positive_force_newtons[axis] =
              std::numeric_limits<std::uint32_t>::max();
        },
        CraftFrameError::invalid_authority,
        "positive authority is numerically bounded");
    rejects(
        [axis](auto& p) {
          p.negative_force_newtons[axis] =
              std::numeric_limits<std::uint32_t>::max();
        },
        CraftFrameError::invalid_authority,
        "negative authority is numerically bounded");
    rejects(
        [axis](auto& p) {
          p.torque_newton_metres[axis] =
              std::numeric_limits<std::uint32_t>::max();
        },
        CraftFrameError::invalid_authority,
        "torque authority is numerically bounded");
    rejects(
        [axis](auto& p) {
          p.max_angular_rate_milliradians_per_second[axis] =
              std::numeric_limits<std::uint32_t>::max();
        },
        CraftFrameError::invalid_authority,
        "angular rate is numerically bounded");
  }
  rejects([](auto& p) { p.principal_inertia_kg_m2 = {10, 10, 21}; },
          CraftFrameError::invalid_inertia,
          "impossible inertia triangle rejects");
  rejects([](auto& p) { p.max_surface_gravity_mm_per_second2 = 0; },
          CraftFrameError::invalid_landing_envelope,
          "surface rating needs gravity bound");
  rejects([](auto& p) { p.max_pressure_millibars = 0; },
          CraftFrameError::invalid_environment,
          "atmospheric rating needs pressure bound");
  rejects(
      [](auto& p) {
        p.max_pressure_millibars = std::numeric_limits<std::uint32_t>::max();
      },
      CraftFrameError::invalid_environment, "pressure bound cannot overflow");
  rejects([](auto& p) { p.support_count = 0; },
          CraftFrameError::invalid_support, "landable frame needs supports");
  rejects([](auto& p) { p.support_count = 2; },
          CraftFrameError::invalid_support,
          "two points do not define landing footprint");
  rejects([](auto& p) { p.support_count = 5; },
          CraftFrameError::invalid_support,
          "support count is checked before indexing");
  rejects([](auto& p) { p.supports[0].half_width_mm = 0; },
          CraftFrameError::invalid_support, "zero pad width rejects");
  rejects([](auto& p) { p.supports[0].half_length_mm = 0; },
          CraftFrameError::invalid_support, "zero pad length rejects");
  rejects([](auto& p) { p.supports[0].stroke_mm = 0; },
          CraftFrameError::invalid_support, "zero support stroke rejects");
  rejects([](auto& p) { p.supports[0].rated_load_newtons = 0; },
          CraftFrameError::invalid_support, "zero support load rejects");
  rejects([](auto& p) { p.supports[1].contact_mm = p.supports[0].contact_mm; },
          CraftFrameError::invalid_support, "duplicate contacts reject");
  rejects(
      [](auto& p) {
        for (std::size_t index = 0; index < p.support_count; ++index) {
          p.supports[index].contact_mm[0] = 0;
        }
      },
      CraftFrameError::invalid_support,
      "collinear contacts do not form a support footprint");
  rejects(
      [](auto& p) {
        p.supports[0].contact_mm[0] = std::numeric_limits<std::int32_t>::max();
      },
      CraftFrameError::invalid_support, "unbounded contact position rejects");
  rejects([](auto& p) { p.supports[3].rated_load_newtons = 1; },
          CraftFrameError::invalid_support,
          "inactive support slots must be zero");
  rejects([](auto& p) { p.max_touchdown_vertical_mm_per_second = 0; },
          CraftFrameError::invalid_landing_envelope,
          "vertical touchdown bound is positive");
  rejects([](auto& p) { p.max_touchdown_horizontal_mm_per_second = 0; },
          CraftFrameError::invalid_landing_envelope,
          "horizontal touchdown bound is positive");
  rejects([](auto& p) { p.max_touchdown_angular_milliradians_per_second = 0; },
          CraftFrameError::invalid_landing_envelope,
          "angular touchdown bound is positive");
  rejects([](auto& p) { p.max_slope_millidegrees = 90'000; },
          CraftFrameError::invalid_landing_envelope,
          "vertical surface is not a landing slope");
}

auto additional_boundaries_contract() -> void {
  rejects([](auto& p) { p.operations &= ~flag(CraftOperation::vacuum); },
          CraftFrameError::invalid_operations,
          "orbital and docking require vacuum");
  for (std::size_t axis = 0; axis < 3; ++axis) {
    rejects([axis](auto& p) { p.drag_area_square_mm[axis] = 0; },
            CraftFrameError::invalid_authority,
            "atmospheric reference area is positive");
    rejects(
        [axis](auto& p) {
          p.drag_area_square_mm[axis] =
              std::numeric_limits<std::uint32_t>::max();
        },
        CraftFrameError::invalid_authority, "reference area is bounded");
  }
  rejects(
      [](auto& p) {
        p.max_surface_gravity_mm_per_second2 =
            std::numeric_limits<std::uint32_t>::max();
      },
      CraftFrameError::invalid_environment, "gravity bound cannot overflow");
  rejects([](auto& p) { p.supports[0].contact_mm[1] = p.hull_min_mm[1]; },
          CraftFrameError::invalid_support,
          "contact must sit below stowed hull");
  rejects([](auto& p) { p.supports[0].stroke_mm = 728; },
          CraftFrameError::invalid_support,
          "fully compressed support needs positive hull clearance");
  rejects([](auto& p) { p.supports[0].stroke_mm = 729; },
          CraftFrameError::invalid_support,
          "fully compressed support cannot penetrate hull");
  rejects(
      [](auto& p) {
        p.principal_inertia_kg_m2 = {1000000000, 1000000000, 1000000000};
      },
      CraftFrameError::invalid_inertia,
      "inertia must fit mass within the physical hull envelope");
  rejects(
      [](auto& p) {
        for (std::size_t i = 0; i < p.support_count; ++i) {
          p.supports[i].rated_load_newtons = 1;
        }
      },
      CraftFrameError::invalid_landing_envelope,
      "support load must carry rated weight");
  rejects([](auto& p) { p.positive_force_newtons[1] = 144000; },
          CraftFrameError::invalid_landing_envelope,
          "hover equality is not liftoff authority");
  rejects([](auto& p) { p.center_of_mass_mm[2] = 2700; },
          CraftFrameError::invalid_support,
          "COM on footprint edge is not stable support");
  rejects([](auto& p) { p.center_of_mass_mm[2] = 2701; },
          CraftFrameError::invalid_support,
          "COM outside footprint rejects inside valid hull");

  auto properties = starter_shuttle_frame().properties;
  std::swap(properties.supports[0], properties.supports[2]);
  check(validate_craft_frame_properties(properties).has_value(),
        "either ordered convex support winding is physically valid");
  properties.support_count = 4;
  properties.supports = {{{{-3500, -1328, -6700}, 375, 550, 300, 160000},
                          {{-3500, -1328, 2700}, 375, 550, 300, 160000},
                          {{3500, -1328, 2700}, 375, 550, 300, 160000},
                          {{3500, -1328, -6700}, 375, 550, 300, 160000}}};
  check(validate_craft_frame_properties(properties).has_value(),
        "four-support fixture is valid without changing starter identity");
  std::swap(properties.supports[1], properties.supports[2]);
  check_error(validate_craft_frame_properties(properties),
              CraftFrameError::invalid_support,
              "self-crossing support polygon rejects");
}

auto space_only_contract() -> void {
  auto properties = starter_shuttle_frame().properties;
  properties.operations = flag(CraftOperation::vacuum) |
                          flag(CraftOperation::orbital) |
                          flag(CraftOperation::docking);
  properties.support_count = 0;
  properties.supports = {};
  properties.drag_area_square_mm = {};
  properties.max_surface_gravity_mm_per_second2 = 0;
  properties.max_pressure_millibars = 0;
  properties.max_touchdown_vertical_mm_per_second = 0;
  properties.max_touchdown_horizontal_mm_per_second = 0;
  properties.max_touchdown_angular_milliradians_per_second = 0;
  properties.max_slope_millidegrees = 0;
  check(validate_craft_frame_properties(properties).has_value(),
        "space-only non-landable craft is representable without new semantics");
  check(!supports_operation(properties, CraftOperation::landed) &&
            !supports_operation(properties, CraftOperation::atmosphere),
        "space-only operations do not imply landability or atmosphere");
  const auto recipe = starter_shuttle_frame().recipe;
  const CraftFrameDescriptor substituted{
      recipe, starter_shuttle_frame().diagnostic_name, properties};
  check_error(craft_frame_checksum(substituted),
              CraftFrameError::definition_mismatch,
              "future definition cannot reuse starter identity");

  properties.operations |=
      flag(CraftOperation::atmosphere) | flag(CraftOperation::ascent);
  properties.drag_area_square_mm =
      starter_shuttle_frame().properties.drag_area_square_mm;
  properties.max_pressure_millibars = 2500;
  check(validate_craft_frame_properties(properties).has_value(),
        "non-landable atmospheric craft can ascend back to vacuum");
  check(!supports_operation(properties, CraftOperation::liftoff) &&
            !supports_operation(properties, CraftOperation::terrain_contact),
        "ascent does not implicitly require contact or surface launch");
}

auto recipe_contract() -> void {
  const auto recipe = starter_shuttle_frame().recipe;
  constexpr std::array<std::byte, 16> golden{
      std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0},
      std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0},
      std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
      std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}};
  std::array<std::byte, kCraftFrameRecipeBytes> encoded{};
  check(encode_craft_frame_recipe(recipe, encoded).has_value() &&
            encoded == golden,
        "recipe golden is exact 16-byte little endian format-ID-version");
  const auto decoded = decode_craft_frame_recipe(golden);
  check(decoded && *decoded == recipe, "recipe golden decodes exactly");
  std::array<std::byte, kCraftFrameRecipeBytes + 2> guarded{};
  guarded.fill(std::byte{0xA5});
  check(encode_craft_frame_recipe(recipe, std::span{guarded}.subspan(1, 16))
            .has_value(),
        "recipe writes exactly within caller subspan");
  check(guarded.front() == std::byte{0xA5} && guarded.back() == std::byte{0xA5},
        "recipe preserves both output canaries");
  for (std::size_t size = 0; size <= guarded.size(); ++size) {
    if (size == kCraftFrameRecipeBytes) continue;
    guarded.fill(std::byte{0xA5});
    const auto before = guarded;
    check_error(
        encode_craft_frame_recipe(recipe, std::span{guarded}.first(size)),
        CraftFrameError::invalid_recipe_size,
        "short and oversized encoding spans reject");
    check(guarded == before, "invalid output size leaves all bytes unchanged");
    check_error(decode_craft_frame_recipe(std::span{guarded}.first(size)),
                CraftFrameError::invalid_recipe_size,
                "short and oversized decode spans reject");
  }
  for (const auto bad : {CraftFrameRecipe{CraftFrameId{0}, 1},
                         CraftFrameRecipe{CraftFrameId{2}, 1},
                         CraftFrameRecipe{kStarterShuttleFrameId, 0},
                         CraftFrameRecipe{kStarterShuttleFrameId, 2}}) {
    encoded.fill(std::byte{0xA5});
    const auto before = encoded;
    check(!encode_craft_frame_recipe(bad, encoded),
          "invalid identity/version refuses encoding");
    check(encoded == before,
          "invalid recipe leaves output transactionally unchanged");
  }
  auto malformed = golden;
  malformed[0] = std::byte{0};
  check_error(decode_craft_frame_recipe(malformed),
              CraftFrameError::unsupported_recipe_format,
              "zero recipe format rejects");
  malformed = golden;
  malformed[3] = std::byte{1};
  check_error(decode_craft_frame_recipe(malformed),
              CraftFrameError::unsupported_recipe_format,
              "format consumes all four bytes");
  malformed = golden;
  malformed[11] = std::byte{1};
  check_error(decode_craft_frame_recipe(malformed), CraftFrameError::unknown_id,
              "frame ID consumes all eight bytes");
  malformed = golden;
  malformed[15] = std::byte{1};
  check_error(decode_craft_frame_recipe(malformed),
              CraftFrameError::unsupported_version,
              "descriptor version consumes all four bytes");
}

auto world_independence_contract() -> void {
  constexpr Seed seed{42};
  const auto planet_before = generate_planet_descriptor(seed);
  const auto terrain_before = derive_seed(seed, SeedDomain::terrain);
  const auto mission_before = derive_seed(seed, SeedDomain::mission);
  for (int i = 0; i < 10; ++i) {
    check(resolve_craft_frame(starter_shuttle_frame().recipe).has_value(),
          "repeated frame resolution remains valid");
  }
  check(generate_planet_descriptor(seed) == planet_before &&
            derive_seed(seed, SeedDomain::terrain) == terrain_before &&
            derive_seed(seed, SeedDomain::mission) == mission_before,
        "frame resolution does not consume or alter generated-world streams");
}
} // namespace

auto main() -> int {
  identity_contract();
  invalid_properties_contract();
  additional_boundaries_contract();
  space_only_contract();
  recipe_contract();
  world_independence_contract();
  if (failures != 0) std::cerr << failures << " craft-frame checks failed\n";
  return failures == 0 ? 0 : 1;
}
