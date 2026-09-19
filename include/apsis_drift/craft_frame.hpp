#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace apsis_drift {

struct CraftFrameId {
  std::uint64_t value{};
  friend auto operator==(const CraftFrameId&, const CraftFrameId&)
      -> bool = default;
};
inline constexpr CraftFrameId kStarterShuttleFrameId{1};
inline constexpr std::uint32_t kStarterShuttleFrameVersion{1};
inline constexpr std::uint32_t kCraftFrameRecipeFormat{1};
inline constexpr std::size_t kCraftFrameRecipeBytes{16};

struct CraftFrameRecipe {
  CraftFrameId id;
  std::uint32_t version{};
  friend auto operator==(const CraftFrameRecipe&, const CraftFrameRecipe&)
      -> bool = default;
};

// Fixed-width physical recipe data; no floating-point inputs or generated
// state. Positive axes are right, up, back; the nose and main thrust point
// along -Z.
using CraftPointMm = std::array<std::int32_t, 3>;
enum class CraftBodyAxes : std::uint8_t { right_up_back = 1 };
enum class CraftOperation : std::uint32_t {
  vacuum = 1U << 0,
  atmosphere = 1U << 1,
  terrain_contact = 1U << 2,
  landed = 1U << 3,
  liftoff = 1U << 4,
  ascent = 1U << 5,
  orbital = 1U << 6,
  docking = 1U << 7,
};
struct CraftLandingSupport {
  CraftPointMm contact_mm{};
  // Stroke is vertical compression; full stroke must leave hull clearance.
  std::uint32_t half_width_mm{}, half_length_mm{}, stroke_mm{};
  std::uint32_t rated_load_newtons{};
  friend auto operator==(const CraftLandingSupport&, const CraftLandingSupport&)
      -> bool = default;
};
struct CraftFrameProperties {
  CraftBodyAxes axes{CraftBodyAxes::right_up_back};
  std::uint8_t occupant_capacity{}, pilot_seats{};
  std::uint32_t operations{};
  std::uint32_t dry_mass_kg{};
  std::array<std::uint64_t, 3> principal_inertia_kg_m2{};
  CraftPointMm hull_min_mm{}, hull_max_mm{}, center_of_mass_mm{};
  std::array<std::uint32_t, 3> positive_force_newtons{},
      negative_force_newtons{};
  std::array<std::uint32_t, 3> torque_newton_metres{};
  std::array<std::uint32_t, 3> max_angular_rate_milliradians_per_second{};
  // Effective Cd*A, not literal visual surface area, one value per body axis.
  std::array<std::uint32_t, 3> drag_area_square_mm{};
  std::uint32_t max_surface_gravity_mm_per_second2{}, max_pressure_millibars{};
  std::uint32_t max_touchdown_vertical_mm_per_second{},
      max_touchdown_horizontal_mm_per_second{};
  std::uint32_t max_touchdown_angular_milliradians_per_second{},
      max_slope_millidegrees{};
  std::uint8_t support_count{};
  std::array<CraftLandingSupport, 4> supports{};
  friend auto operator==(const CraftFrameProperties&,
                         const CraftFrameProperties&) -> bool = default;
};

struct CraftFrameDescriptor {
  const CraftFrameRecipe recipe;
  const std::string_view diagnostic_name;
  const CraftFrameProperties properties;
  friend auto operator==(const CraftFrameDescriptor&,
                         const CraftFrameDescriptor&) -> bool = default;
};
enum class CraftFrameError : std::uint8_t {
  unknown_id,
  unsupported_version,
  invalid_axes,
  invalid_capacity,
  invalid_operations,
  invalid_mass,
  invalid_inertia,
  invalid_bounds,
  invalid_authority,
  invalid_environment,
  invalid_support,
  invalid_landing_envelope,
  definition_mismatch,
  invalid_recipe_size,
  unsupported_recipe_format,
};

[[nodiscard]] auto supports_operation(const CraftFrameProperties&,
                                      CraftOperation) noexcept -> bool;
// Validates physical definitions, including future space-only fixtures; this is
// not a registry and does not admit unknown identities into saved state.
[[nodiscard]] auto validate_craft_frame_properties(
    const CraftFrameProperties&) noexcept
    -> std::expected<void, CraftFrameError>;
[[nodiscard]] auto starter_shuttle_frame() noexcept
    -> const CraftFrameDescriptor&;
[[nodiscard]] auto resolve_craft_frame(CraftFrameRecipe) noexcept
    -> std::expected<CraftFrameDescriptor, CraftFrameError>;
[[nodiscard]] auto craft_frame_checksum(const CraftFrameDescriptor&) noexcept
    -> std::expected<std::uint64_t, CraftFrameError>;
[[nodiscard]] auto craft_frame_diagnostic_json(const CraftFrameDescriptor&)
    -> std::expected<std::string, CraftFrameError>;

// Standalone native recipe projection, NOT an alteration to legacy SaveRecipe.
// Little endian: format u32, frame ID u64, descriptor version u32. Exact size;
// validate before writing, so refusal leaves every output byte unchanged.
[[nodiscard]] auto encode_craft_frame_recipe(CraftFrameRecipe,
                                             std::span<std::byte>) noexcept
    -> std::expected<void, CraftFrameError>;
[[nodiscard]] auto decode_craft_frame_recipe(
    std::span<const std::byte>) noexcept
    -> std::expected<CraftFrameRecipe, CraftFrameError>;

} // namespace apsis_drift
