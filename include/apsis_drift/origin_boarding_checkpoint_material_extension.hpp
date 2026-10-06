#pragma once
#include "apsis_drift/origin_boarding_initial_material.hpp"
namespace apsis_drift {
namespace detail {
struct BoardingCheckpointMaterialExtensionAccess;
}
inline constexpr std::uint32_t kBoardingCheckpointMaterialExtensionVersion{1};
inline constexpr std::uint32_t kBoardingCheckpointMaterialUnion02Version{2};
inline constexpr std::size_t
    kBoardingCheckpointMaterialExtensionMaximumSourceBytes{256 *
                                                           std::size_t{1024}};
struct BoardingCheckpointMaterialExtensionSummary {
  std::uint32_t extension_version{kBoardingCheckpointMaterialExtensionVersion};
  std::size_t source_bytes{}, sources{}, vertices{}, triangles{}, prisms{};
  std::uint64_t base_guards{}, vertex_guards{}, index_guards{},
      raw_plane_guards{}, quantized_plane_guards{};
  bool bindings_complete{}, arithmetic_supported{}, constructors_complete{};
};
class OriginBoardingCheckpointMaterialExtension {
 public:
  struct Data;
  OriginBoardingCheckpointMaterialExtension(
      const OriginBoardingCheckpointMaterialExtension&) = default;
  OriginBoardingCheckpointMaterialExtension(
      OriginBoardingCheckpointMaterialExtension&&) noexcept = default;
  auto operator=(const OriginBoardingCheckpointMaterialExtension&)
      -> OriginBoardingCheckpointMaterialExtension& = default;
  auto operator=(OriginBoardingCheckpointMaterialExtension&&) noexcept
      -> OriginBoardingCheckpointMaterialExtension& = default;
  [[nodiscard]] auto summary() const
      -> const BoardingCheckpointMaterialExtensionSummary*;

 private:
  explicit OriginBoardingCheckpointMaterialExtension(
      std::shared_ptr<const Data>);
  std::shared_ptr<const Data> data_;
  friend struct detail::BoardingCheckpointMaterialExtensionAccess;
};
[[nodiscard]] auto make_origin_boarding_checkpoint_material_extension(
    const NativeCraftBinding&, const OriginBoardingInitialMaterial&)
    -> std::expected<OriginBoardingCheckpointMaterialExtension, std::string>;
[[nodiscard]] auto make_origin_boarding_checkpoint_material_extension_union02(
    const NativeCraftBinding&, const OriginBoardingInitialMaterial&)
    -> std::expected<OriginBoardingCheckpointMaterialExtension, std::string>;
} // namespace apsis_drift
