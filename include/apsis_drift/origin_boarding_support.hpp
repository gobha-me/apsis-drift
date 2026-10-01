#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/operating_motion.hpp"

namespace apsis_drift {
inline constexpr std::size_t kBoardingSupportMaximumDocumentBytes{
    std::size_t{2} * std::size_t{1024} * std::size_t{1024}};
// Numerical point-evidence tolerances, never body penetration allowances.
inline constexpr double kBoardingSurfaceToleranceMetres{2e-6};
inline constexpr double kBoardingRodToleranceMetres{1e-9};
inline constexpr double kBoardingSideProjectionMetres{1e-6};

enum class BoardingOwner : std::uint8_t { craft, station };
enum class BoardingBodyCategory : std::uint8_t {
  hand_fingers,
  boot_sole,
  elbow_forearm,
  helmet_head,
  back,
  pelvis_thigh
};
enum class BoardingPredicateEvidence : std::uint8_t {
  undeclared,
  not_evaluated,
  matches,
  mismatch
};
enum class BoardingSideEvidence : std::uint8_t {
  undeclared,
  not_evaluated,
  missing,
  matches,
  mismatch
};
struct BoardingTriangleKey {
  std::uint32_t group{}, triangle{};
  friend auto operator==(const BoardingTriangleKey&, const BoardingTriangleKey&)
      -> bool = default;
};
struct BoardingTriangleRange {
  std::uint32_t start{}, count{};
};
struct BoardingSupportGroup {
  std::string id;
  BoardingOwner owner{};
  std::optional<std::size_t> motion_group;
  std::uint32_t vertex_count{}, triangle_count{}, object_start{},
      object_count{};
};
struct BoardingSupportObject {
  std::uint32_t group{}, triangle_start{}, triangle_count{};
  std::string source_object, semantic_label;
  std::vector<std::uint32_t> candidate_patches;
};
struct BoardingRodPredicate {
  RigidVector3 origin_rest_metres, unit_axis_rest;
  std::array<double, 2> inclusive_interval_metres{};
  double end_margin_metres{};
};
struct BoardingSupportPatch {
  std::string id, role, body_contact;
  std::uint32_t object{}, triangle_count{}, representative_triangle{};
  BoardingBodyCategory body_category{};
  std::vector<BoardingTriangleRange> triangle_ranges;
  std::vector<std::uint32_t> selected_faces;
  double surface_area_square_metres{};
  RigidVector3 representative_barycentric, representative_point_rest_metres;
  std::array<RigidVector3, 2> bounds_rest_metres;
  std::optional<RigidVector3> contact_side_direction_rest;
  std::optional<std::uint32_t> opposite_winding_triangles;
  std::optional<BoardingRodPredicate> rod;
};
struct BoardingSelectedFace {
  BoardingTriangleKey key;
  std::array<RigidVector3, 3> vertices_rest_metres;
};
struct BoardingTriangleAttribution {
  std::uint32_t object{};
  std::vector<std::uint32_t> candidate_patches;
};
struct BoardingCandidateQuery {
  std::uint32_t patch{};
  BoardingTriangleKey triangle;
  BoardingBodyCategory body_category{};
  // Canonical current owner-local frame: craft or station, never global.
  RigidVector3 point_current_metres;
  std::optional<RigidVector3> body_side_probe_current_metres;
};
struct BoardingCandidateSurfaceEvidence {
  std::uint32_t object{}, patch{};
  BoardingOwner owner{};
  RigidVector3 point_rest_metres;
  bool body_category_matches{}, triangle_matches_patch{}, point_on_triangle{};
  BoardingPredicateEvidence rod_interval{BoardingPredicateEvidence::undeclared};
  BoardingSideEvidence declared_side{BoardingSideEvidence::undeclared};
  // No collision permission, body-clearance, reach, posture or support result.
};

// Only source-bound admission constructs this immutable shared handle. Views
// remain valid while any handle sharing this catalog is alive; a moved-from
// handle returns empty views and all its queries refuse.
class OriginBoardingSupport {
 public:
  struct Data;
  OriginBoardingSupport(const OriginBoardingSupport&) = default;
  OriginBoardingSupport(OriginBoardingSupport&&) noexcept = default;
  auto operator=(const OriginBoardingSupport&)
      -> OriginBoardingSupport& = default;
  auto operator=(OriginBoardingSupport&&) noexcept
      -> OriginBoardingSupport& = default;
  [[nodiscard]] auto groups() const -> std::span<const BoardingSupportGroup>;
  [[nodiscard]] auto objects() const -> std::span<const BoardingSupportObject>;
  [[nodiscard]] auto patches() const -> std::span<const BoardingSupportPatch>;
  [[nodiscard]] auto selected_faces() const
      -> std::span<const BoardingSelectedFace>;

 private:
  explicit OriginBoardingSupport(std::shared_ptr<const Data>);
  std::shared_ptr<const Data> data_;
  friend auto decode_origin_boarding_support(std::string_view,
                                             const OperatingMotionRecipe&)
      -> std::expected<OriginBoardingSupport, std::string>;
  friend auto lookup_boarding_triangle(const OriginBoardingSupport&,
                                       BoardingTriangleKey)
      -> std::expected<BoardingTriangleAttribution, std::string>;
  friend auto boarding_support_group_transform(const OriginBoardingSupport&,
                                               const OperatingProgress&,
                                               std::uint32_t)
      -> std::expected<OperatingTransform, std::string>;
};
[[nodiscard]] auto decode_origin_boarding_support(std::string_view,
                                                  const OperatingMotionRecipe&)
    -> std::expected<OriginBoardingSupport, std::string>;
[[nodiscard]] auto lookup_boarding_triangle(const OriginBoardingSupport&,
                                            BoardingTriangleKey)
    -> std::expected<BoardingTriangleAttribution, std::string>;
// Pure full owner-local contact delta from the catalog's qualified recipe;
// includes D1 canonical offset. Source matrices are never applied again.
[[nodiscard]] auto boarding_support_group_transform(
    const OriginBoardingSupport&, const OperatingProgress&, std::uint32_t)
    -> std::expected<OperatingTransform, std::string>;
[[nodiscard]] auto assess_boarding_candidate_surface(
    const OriginBoardingSupport&, const OperatingProgress&,
    const BoardingCandidateQuery&)
    -> std::expected<BoardingCandidateSurfaceEvidence, std::string>;
} // namespace apsis_drift
