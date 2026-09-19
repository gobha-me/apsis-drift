#include "apsis_drift/craft_frame.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace apsis_drift {
namespace {
constexpr auto bit(CraftOperation value) noexcept -> std::uint32_t {
  return static_cast<std::uint32_t>(value);
}
constexpr std::uint32_t all_operations = 255;
constexpr std::int32_t coordinate_limit_mm = 1'000'000;

// Authored physics, not a density/mass inference from the visual mesh. Keep the
// reviewed lab force magnitudes; this descriptor does not replace that model.
constexpr CraftFrameProperties starter_properties{
    .axes = CraftBodyAxes::right_up_back,
    .occupant_capacity = 4,
    .pilot_seats = 1,
    .operations = all_operations,
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
constexpr CraftFrameDescriptor starter{
    {kStarterShuttleFrameId, kStarterShuttleFrameVersion},
    "freedom-shuttle-v1",
    starter_properties};

auto point_valid(const CraftPointMm& point) noexcept -> bool {
  return std::ranges::all_of(point, [](std::int32_t value) {
    return value >= -coordinate_limit_mm && value <= coordinate_limit_mm;
  });
}
auto cross_xz(const CraftPointMm& a, const CraftPointMm& b,
              const CraftPointMm& c) noexcept -> std::int64_t {
  // Validated coordinates bound each product below 4e12, including subtraction.
  const auto dx = std::int64_t{b[0]} - a[0];
  const auto dz = std::int64_t{b[2]} - a[2];
  return dx * (std::int64_t{c[2]} - a[2]) - dz * (std::int64_t{c[0]} - a[0]);
}
auto validate_registered(const CraftFrameDescriptor& frame) noexcept
    -> std::expected<void, CraftFrameError> {
  const auto resolved = resolve_craft_frame(frame.recipe);
  if (!resolved) return std::unexpected{resolved.error()};
  if (frame != *resolved)
    return std::unexpected{CraftFrameError::definition_mismatch};
  return {};
}
struct Hash {
  std::uint64_t value{14695981039346656037ULL};
  auto integer(std::uint64_t input, unsigned bytes) noexcept -> void {
    for (unsigned i = 0; i < bytes; ++i) {
      value ^= input & 255U;
      value *= 1099511628211ULL;
      input >>= 8U;
    }
  }
};
auto put(std::span<std::byte> bytes, std::size_t offset, std::uint64_t value,
         std::size_t width) noexcept -> void {
  for (std::size_t i = 0; i < width; ++i) {
    bytes[offset + i] = static_cast<std::byte>(value & 255U);
    value >>= 8U;
  }
}
auto get(std::span<const std::byte> bytes, std::size_t offset,
         std::size_t width) noexcept -> std::uint64_t {
  std::uint64_t value{};
  for (std::size_t i = 0; i < width; ++i)
    value |= std::uint64_t{std::to_integer<unsigned>(bytes[offset + i])}
             << (8U * i);
  return value;
}
} // namespace

auto supports_operation(const CraftFrameProperties& properties,
                        CraftOperation operation) noexcept -> bool {
  const auto flag = bit(operation);
  return flag != 0 && (flag & (flag - 1)) == 0 &&
         (flag & all_operations) == flag && (properties.operations & flag) != 0;
}

auto validate_craft_frame_properties(const CraftFrameProperties& p) noexcept
    -> std::expected<void, CraftFrameError> {
  if (p.axes != CraftBodyAxes::right_up_back)
    return std::unexpected{CraftFrameError::invalid_axes};
  if (p.occupant_capacity == 0 || p.occupant_capacity > 64 ||
      p.pilot_seats == 0 || p.pilot_seats > p.occupant_capacity)
    return std::unexpected{CraftFrameError::invalid_capacity};
  const auto has = [&p](CraftOperation op) {
    return supports_operation(p, op);
  };
  if (p.operations == 0 || (p.operations & ~all_operations) != 0 ||
      (has(CraftOperation::landed) && !has(CraftOperation::terrain_contact)) ||
      (has(CraftOperation::liftoff) && !has(CraftOperation::landed)) ||
      (has(CraftOperation::ascent) && !has(CraftOperation::vacuum)) ||
      ((has(CraftOperation::orbital) || has(CraftOperation::docking)) &&
       !has(CraftOperation::vacuum)))
    return std::unexpected{CraftFrameError::invalid_operations};
  if (p.dry_mass_kg == 0 || p.dry_mass_kg > 1'000'000)
    return std::unexpected{CraftFrameError::invalid_mass};
  for (auto moment : p.principal_inertia_kg_m2)
    if (moment == 0 || moment > 1'000'000'000'000ULL)
      return std::unexpected{CraftFrameError::invalid_inertia};
  for (std::size_t axis = 0; axis < 3; ++axis)
    if (p.principal_inertia_kg_m2[axis] >
        p.principal_inertia_kg_m2[(axis + 1) % 3] +
            p.principal_inertia_kg_m2[(axis + 2) % 3])
      return std::unexpected{CraftFrameError::invalid_inertia};
  if (!point_valid(p.hull_min_mm) || !point_valid(p.hull_max_mm) ||
      !point_valid(p.center_of_mass_mm))
    return std::unexpected{CraftFrameError::invalid_bounds};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (p.hull_min_mm[axis] >= p.hull_max_mm[axis] ||
        p.center_of_mass_mm[axis] <= p.hull_min_mm[axis] ||
        p.center_of_mass_mm[axis] >= p.hull_max_mm[axis])
      return std::unexpected{CraftFrameError::invalid_bounds};
    for (auto force :
         {p.positive_force_newtons[axis], p.negative_force_newtons[axis],
          p.torque_newton_metres[axis]})
      if (force == 0 || force > 1'000'000'000)
        return std::unexpected{CraftFrameError::invalid_authority};
    if (p.max_angular_rate_milliradians_per_second[axis] == 0 ||
        p.max_angular_rate_milliradians_per_second[axis] > 10000 ||
        p.drag_area_square_mm[axis] > 1'000'000'000 ||
        (has(CraftOperation::atmosphere) && p.drag_area_square_mm[axis] == 0))
      return std::unexpected{CraftFrameError::invalid_authority};
  }
  // All distances are bounded by 2e6 mm and mass by 1e6 kg above:
  // mass * (distance^2 + distance^2) fits in uint64 (at most 8e18).
  std::array<std::uint64_t, 3> reach_mm{};
  for (std::size_t axis = 0; axis < 3; ++axis)
    reach_mm[axis] = static_cast<std::uint64_t>(std::max(
        std::int64_t{p.center_of_mass_mm[axis]} - p.hull_min_mm[axis],
        std::int64_t{p.hull_max_mm[axis]} - p.center_of_mass_mm[axis]));
  for (std::size_t axis = 0; axis < 3; ++axis) {
    const auto a = reach_mm[(axis + 1) % 3];
    const auto b = reach_mm[(axis + 2) % 3];
    if (p.principal_inertia_kg_m2[axis] * 1'000'000 >
        std::uint64_t{p.dry_mass_kg} * (a * a + b * b))
      return std::unexpected{CraftFrameError::invalid_inertia};
  }
  if (p.max_surface_gravity_mm_per_second2 > 100000 ||
      p.max_pressure_millibars > 100000 ||
      (has(CraftOperation::atmosphere) != (p.max_pressure_millibars != 0)))
    return std::unexpected{CraftFrameError::invalid_environment};
  if (p.support_count > p.supports.size())
    return std::unexpected{CraftFrameError::invalid_support};
  for (std::size_t i = p.support_count; i < p.supports.size(); ++i)
    if (p.supports[i] != CraftLandingSupport{})
      return std::unexpected{CraftFrameError::invalid_support};
  if (!has(CraftOperation::terrain_contact)) {
    if (p.support_count != 0 || p.max_touchdown_vertical_mm_per_second != 0 ||
        p.max_touchdown_horizontal_mm_per_second != 0 ||
        p.max_touchdown_angular_milliradians_per_second != 0 ||
        p.max_slope_millidegrees != 0)
      return std::unexpected{CraftFrameError::invalid_landing_envelope};
    return {};
  }
  if (p.support_count < 3)
    return std::unexpected{CraftFrameError::invalid_support};
  if (p.max_surface_gravity_mm_per_second2 == 0 ||
      p.max_touchdown_vertical_mm_per_second == 0 ||
      p.max_touchdown_vertical_mm_per_second > 20000 ||
      p.max_touchdown_horizontal_mm_per_second == 0 ||
      p.max_touchdown_horizontal_mm_per_second > 50000 ||
      p.max_touchdown_angular_milliradians_per_second == 0 ||
      p.max_touchdown_angular_milliradians_per_second > 1000 ||
      p.max_slope_millidegrees == 0 || p.max_slope_millidegrees > 45000)
    return std::unexpected{CraftFrameError::invalid_landing_envelope};
  std::uint64_t total_load{};
  for (std::size_t i = 0; i < p.support_count; ++i) {
    const auto& support = p.supports[i];
    if (!point_valid(support.contact_mm) ||
        support.contact_mm[1] >= p.hull_min_mm[1] ||
        support.half_width_mm == 0 || support.half_width_mm > 10000 ||
        support.half_length_mm == 0 || support.half_length_mm > 10000 ||
        support.stroke_mm == 0 || support.stroke_mm > 10000 ||
        std::int64_t{support.contact_mm[1]} + support.stroke_mm >=
            p.hull_min_mm[1] ||
        support.rated_load_newtons == 0 ||
        support.rated_load_newtons > 1'000'000'000)
      return std::unexpected{CraftFrameError::invalid_support};
    total_load += support.rated_load_newtons;
  }
  // Ordered convex support polygon; the COM projection must be strictly inside.
  // This checks static geometric support only, not touchdown or load sharing.
  std::int64_t winding{};
  for (std::size_t i = 0; i < p.support_count; ++i) {
    const auto& a = p.supports[i].contact_mm;
    const auto& b = p.supports[(i + 1) % p.support_count].contact_mm;
    const auto& c = p.supports[(i + 2) % p.support_count].contact_mm;
    const auto turn = cross_xz(a, b, c);
    const auto com = cross_xz(a, b, p.center_of_mass_mm);
    if (turn == 0 || com == 0 || (turn < 0) != (com < 0) ||
        (winding != 0 && (winding < 0) != (turn < 0)))
      return std::unexpected{CraftFrameError::invalid_support};
    winding = turn;
  }
  const auto weight_millinewtons =
      std::uint64_t{p.dry_mass_kg} * p.max_surface_gravity_mm_per_second2;
  if (total_load * 1000 < weight_millinewtons ||
      (has(CraftOperation::liftoff) &&
       std::uint64_t{p.positive_force_newtons[1]} * 1000 <=
           weight_millinewtons))
    return std::unexpected{CraftFrameError::invalid_landing_envelope};
  return {};
}

auto starter_shuttle_frame() noexcept -> const CraftFrameDescriptor& {
  return starter;
}
auto resolve_craft_frame(CraftFrameRecipe recipe) noexcept
    -> std::expected<CraftFrameDescriptor, CraftFrameError> {
  if (recipe.id != kStarterShuttleFrameId)
    return std::unexpected{CraftFrameError::unknown_id};
  if (recipe.version != kStarterShuttleFrameVersion)
    return std::unexpected{CraftFrameError::unsupported_version};
  const auto valid = validate_craft_frame_properties(starter.properties);
  if (!valid) return std::unexpected{valid.error()};
  return starter;
}

auto craft_frame_checksum(const CraftFrameDescriptor& frame) noexcept
    -> std::expected<std::uint64_t, CraftFrameError> {
  const auto valid = validate_registered(frame);
  if (!valid) return std::unexpected{valid.error()};
  Hash hash;
  for (char c : std::string_view{"apsis-craft-frame-v1"})
    hash.integer(static_cast<unsigned char>(c), 1);
  hash.integer(frame.recipe.id.value, 8);
  hash.integer(frame.recipe.version, 4);
  for (char c : frame.diagnostic_name)
    hash.integer(static_cast<unsigned char>(c), 1);
  hash.integer(0, 1);
  const auto& p = frame.properties;
  hash.integer(static_cast<std::uint8_t>(p.axes), 1);
  hash.integer(p.occupant_capacity, 1);
  hash.integer(p.pilot_seats, 1);
  hash.integer(p.operations, 4);
  hash.integer(p.dry_mass_kg, 4);
  for (auto value : p.principal_inertia_kg_m2)
    hash.integer(value, 8);
  for (const auto& point : {p.hull_min_mm, p.hull_max_mm, p.center_of_mass_mm})
    for (auto value : point)
      hash.integer(static_cast<std::uint32_t>(value), 4);
  for (const auto& values :
       {p.positive_force_newtons, p.negative_force_newtons,
        p.torque_newton_metres, p.max_angular_rate_milliradians_per_second,
        p.drag_area_square_mm})
    for (auto value : values)
      hash.integer(value, 4);
  for (auto value :
       {p.max_surface_gravity_mm_per_second2, p.max_pressure_millibars,
        p.max_touchdown_vertical_mm_per_second,
        p.max_touchdown_horizontal_mm_per_second,
        p.max_touchdown_angular_milliradians_per_second,
        p.max_slope_millidegrees})
    hash.integer(value, 4);
  hash.integer(p.support_count, 1);
  for (const auto& support : p.supports) {
    for (auto value : support.contact_mm)
      hash.integer(static_cast<std::uint32_t>(value), 4);
    for (auto value : {support.half_width_mm, support.half_length_mm,
                       support.stroke_mm, support.rated_load_newtons})
      hash.integer(value, 4);
  }
  return hash.value;
}

auto craft_frame_diagnostic_json(const CraftFrameDescriptor& frame)
    -> std::expected<std::string, CraftFrameError> {
  const auto checksum = craft_frame_checksum(frame);
  if (!checksum) return std::unexpected{checksum.error()};
  const auto& p = frame.properties;
  nlohmann::ordered_json supports = nlohmann::ordered_json::array();
  for (std::size_t i = 0; i < p.support_count; ++i) {
    const auto& s = p.supports[i];
    supports.push_back({{"contact_mm", s.contact_mm},
                        {"half_width_mm", s.half_width_mm},
                        {"half_length_mm", s.half_length_mm},
                        {"stroke_mm", s.stroke_mm},
                        {"rated_load_newtons", s.rated_load_newtons}});
  }
  const nlohmann::ordered_json value{
      {"frame_id", std::to_string(frame.recipe.id.value)},
      {"descriptor_version", frame.recipe.version},
      {"name", frame.diagnostic_name},
      {"axes", "right-up-back"},
      {"occupant_capacity", p.occupant_capacity},
      {"pilot_seats", p.pilot_seats},
      {"operations", p.operations},
      {"dry_mass_kg", p.dry_mass_kg},
      {"principal_inertia_kg_m2", p.principal_inertia_kg_m2},
      {"hull_min_mm", p.hull_min_mm},
      {"hull_max_mm", p.hull_max_mm},
      {"center_of_mass_mm", p.center_of_mass_mm},
      {"positive_force_newtons", p.positive_force_newtons},
      {"negative_force_newtons", p.negative_force_newtons},
      {"torque_newton_metres", p.torque_newton_metres},
      {"max_angular_rate_milliradians_per_second",
       p.max_angular_rate_milliradians_per_second},
      {"drag_area_square_mm", p.drag_area_square_mm},
      {"max_surface_gravity_mm_per_second2",
       p.max_surface_gravity_mm_per_second2},
      {"max_pressure_millibars", p.max_pressure_millibars},
      {"max_touchdown_vertical_mm_per_second",
       p.max_touchdown_vertical_mm_per_second},
      {"max_touchdown_horizontal_mm_per_second",
       p.max_touchdown_horizontal_mm_per_second},
      {"max_touchdown_angular_milliradians_per_second",
       p.max_touchdown_angular_milliradians_per_second},
      {"max_slope_millidegrees", p.max_slope_millidegrees},
      {"supports", std::move(supports)},
      {"checksum", std::to_string(*checksum)}};
  return value.dump();
}

auto encode_craft_frame_recipe(CraftFrameRecipe recipe,
                               std::span<std::byte> output) noexcept
    -> std::expected<void, CraftFrameError> {
  if (output.size() != kCraftFrameRecipeBytes)
    return std::unexpected{CraftFrameError::invalid_recipe_size};
  const auto frame = resolve_craft_frame(recipe);
  if (!frame) return std::unexpected{frame.error()};
  put(output, 0, kCraftFrameRecipeFormat, 4);
  put(output, 4, recipe.id.value, 8);
  put(output, 12, recipe.version, 4);
  return {};
}
auto decode_craft_frame_recipe(std::span<const std::byte> input) noexcept
    -> std::expected<CraftFrameRecipe, CraftFrameError> {
  if (input.size() != kCraftFrameRecipeBytes)
    return std::unexpected{CraftFrameError::invalid_recipe_size};
  if (get(input, 0, 4) != kCraftFrameRecipeFormat)
    return std::unexpected{CraftFrameError::unsupported_recipe_format};
  const CraftFrameRecipe recipe{{get(input, 4, 8)},
                                static_cast<std::uint32_t>(get(input, 12, 4))};
  const auto frame = resolve_craft_frame(recipe);
  if (!frame) return std::unexpected{frame.error()};
  return recipe;
}
} // namespace apsis_drift
