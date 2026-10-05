#pragma once
#include "apsis_drift/native_craft_binding.hpp"
#include "apsis_drift/origin_boarding_self_model04.hpp"
#include "apsis_drift/origin_cabin_corridor.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace apsis_drift {
inline constexpr std::size_t kBoardingBootSourcePartitionCount{10};
inline constexpr double kBoardingBootPressureRadiusMetres{.020};
inline constexpr double kBoardingBootDiskEdgeMarginMetres{.010};
inline constexpr double kBoardingBootLoadMarginMetres{.010};
struct BoardingBootSourcePartition {
  std::array<LowerCockpitFace, 2> faces;
  // Clockwise X/Z perimeter. The source diagonal is internal, not a gap.
  std::array<RigidVector3, 4> perimeter_metres;
  double plane_metres{};
};
struct BoardingBootSupportRequest {
  BoardingBodyPose pose;
  std::size_t anchor_side{}, anchor_partition{};
  // Stored w is exact; the starboard fraction is the expression 1-w.
  double port_load_fraction{.5};
};
enum class BoardingBootPlaneRelation : std::uint8_t {
  below,
  equal,
  above,
  unresolved
};
enum class BoardingBootSupportStatus : std::uint8_t {
  unloaded,
  supported,
  plane_gap,
  plane_penetration,
  no_finite_contact,
  disk_margin_refused,
  numerical_unresolved
};
struct BoardingBootStanceTranslation {
  // Ty = exact(source plane) - exact(anchor box centerY) + exact(halfY).
  // No rounded translation or translated center supplies authority.
  std::array<double, 3> terms{};
  std::size_t anchor_side{}, anchor_partition{};
  double reporting_metres{};
};
struct BoardingBootPressureWitness {
  std::array<RigidVector3, 2> canonical_boot_centers;
  RigidVector3 canonical_center_of_mass;
  double port_fraction{};
  // Exact expressions: B=w*C0+(1-w)*C1; Pi=Ci+(M-B).
  // They imply w*P0+(1-w)*P1=M, including exact complementary fractions.
};
struct BoardingBootContactPiece {
  LowerCockpitTriangleKey key;
  std::uint32_t object{};
  std::optional<std::uint32_t> evaluated_source_triangle;
  // Rounded reporting polygon/area only. Disk admission uses edge bounds.
  std::vector<RigidVector3> reporting_polygon_metres;
  double reporting_area_square_metres{};
};
struct BoardingBootSupportEvidence {
  BoardingBootSupportStatus status{BoardingBootSupportStatus::unloaded};
  bool loaded{}, pressure_supported{};
  std::optional<std::size_t> source_partition;
  std::array<BoardingBootPlaneRelation, kBoardingBootSourcePartitionCount>
      plane_relations{};
  RigidVector3 reporting_pressure_center_metres;
  // Outward X/Z bounds of the exact pressure expression; Y is reporting only.
  std::array<RigidVector3, 2> pressure_center_bounds_metres;
  double minimum_center_edge_distance_lower_metres{},
      minimum_disk_edge_margin_lower_metres{},
      reporting_sole_origin_bottom_difference_metres{};
  // Exact discrepancy = stored soleY - stored box centerY + stored halfY.
  std::array<double, 3> sole_origin_bottom_difference_terms{};
  BoardingBootPlaneRelation sole_origin_bottom_relation{};
  std::vector<BoardingBootContactPiece> pieces;
};
struct BoardingBootSupportDiagnostic {
  BoardingSelfDiagnostic04 local_self;
  BoardingBootStanceTranslation stance;
  BoardingBootPressureWitness pressure_witness;
  std::array<BoardingBootSupportEvidence, 2> boots;
  // Rounded display points cannot replace the exact uniform placement.
  RigidVector3 reporting_placed_center_of_mass_metres;
  double projected_load_margin_lower_metres{};
  // Placement checks only both boot footprints against the ten eligible tops.
  // This is not all-body or all-obstacle nonpenetration.
  bool placement_nonpenetrating{}, pressure_supported{}, load_supported{},
      checkpoint_supported{};
  static constexpr bool world_qualified{false}, sweep_qualified{false},
      route_qualified{false}, actor_qualified{false}, seat_qualified{false};
};
class OriginBoardingBootSupport {
 public:
  struct Data;
  OriginBoardingBootSupport(const OriginBoardingBootSupport&) = default;
  OriginBoardingBootSupport(OriginBoardingBootSupport&&) noexcept = default;
  auto operator=(const OriginBoardingBootSupport&)
      -> OriginBoardingBootSupport& = default;
  auto operator=(OriginBoardingBootSupport&&) noexcept
      -> OriginBoardingBootSupport& = default;
  [[nodiscard]] auto selected_partitions() const
      -> std::span<const BoardingBootSourcePartition>;
  [[nodiscard]] auto contact() const -> const OriginLowerCockpitContact*;

 private:
  explicit OriginBoardingBootSupport(std::shared_ptr<const Data>);
  std::shared_ptr<const Data> data_;
  friend auto make_origin_boarding_boot_support(const NativeCraftBinding&)
      -> std::expected<OriginBoardingBootSupport, std::string>;
  friend auto assess_origin_boarding_boot_support(
      const OriginBoardingBootSupport&, const BoardingBootSupportRequest&)
      -> std::expected<BoardingBootSupportDiagnostic, std::string>;
};
// Upper9 paired partitions then the registered transition partition at index9.
// Sharing handles own source names and faces; moved-from providers refuse.
[[nodiscard]] auto make_origin_boarding_boot_support(const NativeCraftBinding&)
    -> std::expected<OriginBoardingBootSupport, std::string>;
// Source contact/load and self acceptance are separate checkpoint results.
// No actor mutation, arbitrary hardware, sweep or all-body world predicate.
[[nodiscard]] auto assess_origin_boarding_boot_support(
    const OriginBoardingBootSupport&, const BoardingBootSupportRequest&)
    -> std::expected<BoardingBootSupportDiagnostic, std::string>;

inline constexpr std::uint32_t kBoardingFootSitesVersion{2};
struct BoardingFootSiteScalarBounds {
  double lower{}, upper{};
  bool supported{};
};
struct BoardingFootSiteOffsets {
  // Private numeric controls only; public sites use all-zero offsets.
  RigidVector3 center_metres;
  std::array<double, 2> pressure_xz_metres{};
};
enum class BoardingFootSiteCondition : std::uint8_t {
  unsupported_arithmetic,
  numerical_unresolved,
  incomplete_coverage,
  plane_penetration,
  sole_disk_margin,
  source_disk_margin,
  no_matching_plane
};
struct BoardingFootSiteRefusal {
  std::size_t site{};
  std::optional<std::size_t> partition;
  BoardingFootSiteCondition condition{};
};
struct BoardingFootSiteEdgeEvidence {
  BoardingFootSiteScalarBounds signed_side, edge_length_squared,
      squared_margin_gap;
  bool disk_contained{};
};
struct BoardingFootSitePartitionEvidence {
  bool evaluated{}, footprint_overlap_possible{}, disk_contained{};
  BoardingBootPlaneRelation plane_relation{
      BoardingBootPlaneRelation::unresolved};
  std::array<BoardingFootSiteEdgeEvidence, 4> edges{};
};
struct BoardingFootSiteEvidence {
  // Center = exact sum of terms, never a newly rounded Box center.
  std::array<double, 3> center_x_terms{}, center_y_terms{};
  std::array<double, 2> center_z_terms{};
  // X/Z offset terms are {compiled offset, private offset}.
  std::array<std::array<double, 2>, 2> pressure_offset_terms{};
  RigidVector3 half_size_metres{.06, .05, .14};
  std::array<RigidVector3, 2> center_bounds_metres{}, pressure_bounds_metres{};
  std::array<std::array<RigidVector3, 2>, 4> sole_corner_bounds_metres{};
  std::array<BoardingFootSiteEdgeEvidence, 4> sole_edges{};
  std::array<BoardingFootSitePartitionEvidence,
             kBoardingBootSourcePartitionCount>
      partitions{};
  std::optional<std::size_t> source_partition;
  std::size_t scanned_partitions{};
  bool arithmetic_supported{}, coverage_complete{}, placement_nonpenetrating{},
      sole_disk_contained{}, source_disk_contained{}, eligible{};
  std::optional<BoardingFootSiteRefusal> first_refusal;
};
struct BoardingFootSitesDiagnostic {
  // Owns the source faces/perimeters and their name views throughout result
  // life.
  OriginBoardingBootSupport source;
  std::uint32_t sites_version{kBoardingFootSitesVersion};
  std::array<BoardingFootSiteEvidence, 2> sites{};
  std::array<double, 2> required_radius_margin_terms{
      kBoardingBootPressureRadiusMetres, kBoardingBootDiskEdgeMarginMetres};
  bool arithmetic_supported{}, coverage_complete{}, eligible{};
  std::optional<BoardingFootSiteRefusal> first_refusal;
  static constexpr bool body_qualified{false}, com_qualified{false},
      force_qualified{false}, load_qualified{false}, self_qualified{false},
      world_qualified{false}, crop_qualified{false}, sweep_qualified{false},
      movement_qualified{false}, route_qualified{false}, actor_qualified{false},
      seat_qualified{false}, save_qualified{false},
      first_flight_qualified{false};
};
// Fixed upper/transition sites and pressure points only. No body or load model.
[[nodiscard]] auto assess_origin_boarding_foot_sites(
    const OriginBoardingBootSupport&)
    -> std::expected<BoardingFootSitesDiagnostic, std::string>;
} // namespace apsis_drift
