#pragma once
#include "apsis_drift/origin_boarding_hatch_seal_material.hpp"
#include "origin_boarding_initial_material_internal.hpp"
namespace apsis_drift::detail {
struct HatchSealMaterialPreparedView;
enum class BoardingHatchSealMaterialCondition : std::uint8_t {
  none,
  invalid_limits,
  invalid_binding,
  source_capacity,
  source_identity,
  unsupported_arithmetic,
  base_capacity,
  coordinate_capacity,
  bound_capacity,
  index_capacity,
  strip_capacity,
  inclusion_capacity,
  segment_capacity,
  constructor_settings,
  source_bounds,
  ring_topology,
  raw_containment,
  quantized_containment
};
struct BoardingHatchSealMaterialLimits {
  std::size_t source_bytes{kBoardingHatchSealMaterialMaximumSourceBytes},
      segments{8};
  std::uint64_t base_guards{4096}, coordinate_guards{540}, bound_guards{540},
      index_guards{480}, strip_guards{160}, inclusions{960};
};
struct BoardingHatchSealMaterialConstructorMath {
  BoardingHatchSealMaterialSummary work;
  BoardingHatchSealMaterialCondition condition{};
  std::optional<std::size_t> source, triangle, strip, vertex, axis;
  bool quantized{}, complete{};
  static constexpr bool source_qualified{false}, material_qualified{false},
      body_qualified{false}, world_qualified{false}, actor_qualified{false};
};
struct BoardingHatchSealCapsule {
  BoardingPlantedLegPointBounds first, second;
  double radius{};
};
struct BoardingHatchSealMaterialAccess {
  [[nodiscard]] static auto data(const OriginBoardingHatchSealMaterial&)
      -> const OriginBoardingHatchSealMaterial::Data*;
  [[nodiscard]] static auto valid(const OriginBoardingHatchSealMaterial&,
                                  const OriginBoardingInitialMaterial&) -> bool;
  [[nodiscard]] static auto make(
      const NativeCraftBinding&, const OriginBoardingInitialMaterial&,
      BoardingHatchSealMaterialLimits,
      BoardingHatchSealMaterialConstructorMath* evidence = nullptr)
      -> std::expected<OriginBoardingHatchSealMaterial, std::string>;
  [[nodiscard]] static auto encloser_count(
      const OriginBoardingHatchSealMaterial&,
      const OriginBoardingInitialMaterial&, std::size_t source) -> std::size_t;
  [[nodiscard]] static auto capsule(const OriginBoardingHatchSealMaterial&,
                                    const OriginBoardingInitialMaterial&,
                                    std::size_t source, std::size_t ordinal)
      -> const BoardingHatchSealCapsule*;
};
[[nodiscard]] auto hatch_seal_material_constructor_math(
    const HatchSealMaterialPreparedView&, BoardingHatchSealMaterialLimits = {})
    -> std::expected<BoardingHatchSealMaterialConstructorMath, std::string>;
[[nodiscard]] auto hatch_seal_material_capsule_inclusion_math(
    BoardingPlantedLegPointBounds point, BoardingPlantedLegPointBounds first,
    BoardingPlantedLegPointBounds second, double radius)
    -> std::expected<MaterialConstructorMathEvidence, std::string>;
} // namespace apsis_drift::detail
