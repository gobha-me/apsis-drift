#pragma once

#include "apsis_drift/origin_boarding_source_endpoint_load.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace apsis_drift {
namespace detail {
struct BoardingInitialMaterialAccess;
struct BoardingInitialMaterialLimits;
} // namespace detail
inline constexpr std::uint32_t kBoardingInitialMaterialVersion{1};
inline constexpr std::size_t kBoardingInitialMaterialOriginalObjects{1746};
inline constexpr std::size_t kBoardingInitialMaterialEffectiveObjects{1751};
inline constexpr std::size_t kBoardingInitialMaterialMaximumSourceBytes{
    std::size_t{32} * std::size_t{1024} * std::size_t{1024}};
enum class BoardingInitialMaterialRelation : std::uint8_t {
  unknown,
  shell_sheet,
  service_enclosure,
  support_enclosure,
  stowed
};
enum class BoardingInitialMaterialCondition : std::uint8_t {
  none,
  invalid_source,
  invalid_binding,
  child_prerequisite,
  unsupported_arithmetic,
  source_capacity,
  constructor_capacity,
  constructor_relation,
  envelope_capacity,
  triangle_capacity,
  pair_capacity,
  axis_capacity,
  missing_relation,
  enclosure_unresolved,
  sheet_unresolved,
  source_face_identity
};
struct BoardingInitialMaterialRefusal {
  BoardingInitialMaterialCondition condition{};
  std::optional<std::size_t> part, source;
  std::optional<std::uint32_t> triangle;
  std::string_view source_object;
};
struct BoardingInitialMaterialSourceSummary {
  std::size_t original_objects{}, effective_objects{}, fixed_objects{},
      moving_objects{}, removed_objects{}, replacement_objects{},
      absent_crop_objects{}, source_bytes{};
  std::uint64_t ring_inclusions{}, support_plane_inclusions{};
  bool bindings_complete{}, constructors_complete{};
};
class OriginBoardingInitialMaterial {
 public:
  struct Data;
  OriginBoardingInitialMaterial(const OriginBoardingInitialMaterial&) = default;
  OriginBoardingInitialMaterial(OriginBoardingInitialMaterial&&) noexcept =
      default;
  auto operator=(const OriginBoardingInitialMaterial&)
      -> OriginBoardingInitialMaterial& = default;
  auto operator=(OriginBoardingInitialMaterial&&) noexcept
      -> OriginBoardingInitialMaterial& = default;
  [[nodiscard]] auto summary() const
      -> const BoardingInitialMaterialSourceSummary*;
  [[nodiscard]] auto source_name(std::size_t) const -> std::string_view;

 private:
  explicit OriginBoardingInitialMaterial(std::shared_ptr<const Data>);
  std::shared_ptr<const Data> data_;
  friend struct detail::BoardingInitialMaterialAccess;
  friend auto make_origin_boarding_initial_material(const NativeCraftBinding&)
      -> std::expected<OriginBoardingInitialMaterial, std::string>;
};
struct BoardingInitialMaterialPartEvidence {
  BoardingBodyPartId id{};
  BoardingPlantedLegPointBounds witness, full_reservation;
  std::size_t envelopes_examined{}, envelope_exclusions{},
      relation_exclusions{};
  bool original_witness_identity{}, complete{};
};
struct BoardingInitialMaterialPayload {
  std::uint32_t material_version{kBoardingInitialMaterialVersion};
  BoardingInitialMaterialSourceSummary source;
  std::array<BoardingInitialMaterialPartEvidence, kBoardingBodyPartCount>
      parts{};
  std::uint64_t envelope_pairs{}, enclosure_pairs{}, triangle_visits{},
      collapsed_triangles{}, boundary_pairs{}, axes_examined{};
  bool bindings_complete{}, source_complete{}, arithmetic_supported{},
      initial_material_exclusion{};
  std::optional<BoardingInitialMaterialRefusal> first_refusal;
};
struct BoardingInitialMaterialDiagnostic {
  BoardingSourceEndpointLoadDiagnostic initial;
  OriginBoardingInitialMaterial source;
  BoardingInitialMaterialPayload material;
  static constexpr bool dynamics_qualified{false}, strength_qualified{false},
      continuous_qualified{false}, sweep_qualified{false},
      route_qualified{false}, actor_qualified{false}, seat_qualified{false},
      save_qualified{false}, first_flight_qualified{false};
};
[[nodiscard]] auto make_origin_boarding_initial_material(
    const NativeCraftBinding&)
    -> std::expected<OriginBoardingInitialMaterial, std::string>;
[[nodiscard]] auto assess_origin_boarding_initial_material(
    const OriginBoardingBootSupport&, const OriginBoardingInitialMaterial&)
    -> std::expected<BoardingInitialMaterialDiagnostic, std::string>;
} // namespace apsis_drift
