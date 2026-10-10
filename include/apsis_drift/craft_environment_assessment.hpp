#pragma once

#include "apsis_drift/craft_frame.hpp"
#include "apsis_drift/freedom_knowledge.hpp"

#include <array>

namespace apsis_drift {
inline constexpr std::uint32_t kStarterEnvironmentAssessmentVersion{1};
enum class CraftEnvironmentOperation : std::uint8_t {
  orbital,
  entry,
  surface_flight,
  touchdown,
  ascent
};
enum class CraftEnvironmentRating : std::uint8_t {
  safe,
  marginal,
  unknown,
  insufficient
};
enum class CraftEnvironmentAxis : std::uint8_t {
  shielding_thermal,
  structural,
  propulsion
};
enum class CraftEnvironmentMarginKind : std::uint8_t {
  minimum_temperature,
  maximum_temperature,
  radiation,
  electrical,
  pressure,
  gust,
  ground_motion,
  gravity,
  vertical_thrust,
  support_load
};
struct CraftEnvironmentMargin {
  CraftEnvironmentMarginKind kind{};
  CraftEnvironmentAxis axis{};
  // Native integer units of the corresponding ambient/frame field. Thrust
  // and support load use millinewtons; gravity uses mm/s^2, rounded upward.
  std::int64_t remaining{};
  std::uint64_t reference_capacity{};
  CraftEnvironmentRating rating{};
  friend auto operator==(const CraftEnvironmentMargin&,
                         const CraftEnvironmentMargin&) -> bool = default;
};
inline constexpr std::size_t kCraftEnvironmentMarginCount{10};
struct CraftEnvironmentAssessment {
  std::uint32_t version{kStarterEnvironmentAssessmentVersion};
  PlanetAmbientRecipe environment;
  CraftFrameRecipe craft;
  CraftEnvironmentOperation operation{};
  std::array<CraftEnvironmentMargin, kCraftEnvironmentMarginCount> margins{};
  std::size_t count{};
  bool operation_supported{};
  std::array<CraftEnvironmentRating, 3> axes{};
  CraftEnvironmentRating rating{};
  friend auto operator==(const CraftEnvironmentAssessment&,
                         const CraftEnvironmentAssessment&) -> bool = default;
};
struct KnownCraftEnvironmentAssessment {
  CraftFrameRecipe craft;
  CraftEnvironmentOperation operation{};
  // Missing facts redact the entire dependent margin, including its sign.
  std::array<std::optional<CraftEnvironmentMargin>,
             kCraftEnvironmentMarginCount>
      margins{};
  std::size_t count{};
  bool operation_supported{};
  std::array<CraftEnvironmentRating, 3> axes{};
  CraftEnvironmentRating rating{};
  friend auto operator==(const KnownCraftEnvironmentAssessment&,
                         const KnownCraftEnvironmentAssessment&)
      -> bool = default;
};
enum class CraftEnvironmentError : std::uint8_t {
  unsupported_version,
  unsupported_operation,
  invalid_environment,
  invalid_craft,
  invalid_knowledge,
  owner_mismatch
};

// Pure conservative SURFACE reference envelopes, not instantaneous entry heat,
// live wind, damage, pad clearance or a permission to commit. Orbital queries
// do not apply surface exposure. Resolve actual owners before evaluating.
[[nodiscard]] auto assess_craft_environment(
    const PhysicalLocalSystem&, const PlanetAmbientRecipe&, CraftFrameRecipe,
    CraftEnvironmentOperation,
    std::uint32_t version = kStarterEnvironmentAssessmentVersion)
    -> std::expected<CraftEnvironmentAssessment, CraftEnvironmentError>;
// Authored numerical reference fixtures only; does not certify seed ownership.
[[nodiscard]] auto assess_reference_craft_environment(
    const PlanetAmbientEnvironment&, CraftFrameRecipe,
    CraftEnvironmentOperation,
    std::uint32_t version = kStarterEnvironmentAssessmentVersion)
    -> std::expected<CraftEnvironmentAssessment, CraftEnvironmentError>;
// Independent read-only ledger projection. No caller-supplied knowledge mask.
[[nodiscard]] auto query_known_craft_environment(
    const PhysicalLocalSystem&, const PlanetAmbientRecipe&, CraftFrameRecipe,
    CraftEnvironmentOperation, const FreedomKnowledge&, SimulationTick,
    std::uint32_t version = kStarterEnvironmentAssessmentVersion)
    -> std::expected<KnownCraftEnvironmentAssessment, CraftEnvironmentError>;
[[nodiscard]] auto craft_environment_rating_name(
    CraftEnvironmentRating) noexcept -> std::string_view;
} // namespace apsis_drift
