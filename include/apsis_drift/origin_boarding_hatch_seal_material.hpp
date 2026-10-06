#pragma once
#include "apsis_drift/origin_boarding_initial_material.hpp"
namespace apsis_drift {
namespace detail {
struct BoardingHatchSealMaterialAccess;
}
inline constexpr std::uint32_t kBoardingHatchSealMaterialVersion{1};
inline constexpr std::size_t kBoardingHatchSealMaterialMaximumSourceBytes{
    65536};
struct BoardingHatchSealMaterialSummary {
  std::uint32_t version{kBoardingHatchSealMaterialVersion};
  std::size_t source_bytes{}, segments{};
  std::uint64_t base_guards{}, coordinate_guards{}, bound_guards{},
      index_guards{}, strip_guards{}, raw_inclusions{}, quantized_inclusions{};
  double raw_radius{}, game_radius{};
  bool bindings_complete{}, arithmetic_supported{}, constructors_complete{};
};
class OriginBoardingHatchSealMaterial {
 public:
  struct Data;
  OriginBoardingHatchSealMaterial(const OriginBoardingHatchSealMaterial&) =
      default;
  OriginBoardingHatchSealMaterial(OriginBoardingHatchSealMaterial&&) noexcept =
      default;
  auto operator=(const OriginBoardingHatchSealMaterial&)
      -> OriginBoardingHatchSealMaterial& = default;
  auto operator=(OriginBoardingHatchSealMaterial&&) noexcept
      -> OriginBoardingHatchSealMaterial& = default;
  [[nodiscard]] auto summary() const -> const BoardingHatchSealMaterialSummary*;

 private:
  explicit OriginBoardingHatchSealMaterial(std::shared_ptr<const Data>);
  std::shared_ptr<const Data> data_;
  friend struct detail::BoardingHatchSealMaterialAccess;
};
[[nodiscard]] auto make_origin_boarding_hatch_seal_material(
    const NativeCraftBinding&, const OriginBoardingInitialMaterial&)
    -> std::expected<OriginBoardingHatchSealMaterial, std::string>;
} // namespace apsis_drift
