#pragma once
#include "apsis_drift/origin_boarding_initial_material.hpp"
namespace apsis_drift {
namespace detail {
struct BoardingSeparationRingMaterialAccess;
}
inline constexpr std::uint32_t kBoardingSeparationRingMaterialVersion{1};
inline constexpr std::size_t kBoardingSeparationRingMaterialMaximumSourceBytes{
    65536};
struct BoardingSeparationRingMaterialSummary {
  std::uint32_t version{kBoardingSeparationRingMaterialVersion};
  std::size_t source_bytes{}, segments{};
  std::uint64_t base_guards{}, coordinate_guards{}, bound_guards{},
      index_guards{}, strip_guards{}, raw_inclusions{}, quantized_inclusions{};
  double raw_radius{}, game_radius{};
  bool bindings_complete{}, arithmetic_supported{}, constructors_complete{};
};
class OriginBoardingSeparationRingMaterial {
 public:
  struct Data;
  OriginBoardingSeparationRingMaterial(
      const OriginBoardingSeparationRingMaterial&) = default;
  OriginBoardingSeparationRingMaterial(
      OriginBoardingSeparationRingMaterial&&) noexcept = default;
  auto operator=(const OriginBoardingSeparationRingMaterial&)
      -> OriginBoardingSeparationRingMaterial& = default;
  auto operator=(OriginBoardingSeparationRingMaterial&&) noexcept
      -> OriginBoardingSeparationRingMaterial& = default;
  [[nodiscard]] auto summary() const
      -> const BoardingSeparationRingMaterialSummary*;

 private:
  explicit OriginBoardingSeparationRingMaterial(std::shared_ptr<const Data>);
  std::shared_ptr<const Data> data_;
  friend struct detail::BoardingSeparationRingMaterialAccess;
};
[[nodiscard]] auto make_origin_boarding_separation_ring_material(
    const NativeCraftBinding&, const OriginBoardingInitialMaterial&)
    -> std::expected<OriginBoardingSeparationRingMaterial, std::string>;
} // namespace apsis_drift
