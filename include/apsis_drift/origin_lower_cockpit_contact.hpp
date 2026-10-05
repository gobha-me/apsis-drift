#pragma once
#include "apsis_drift/origin_cabin_seam.hpp"
#include "apsis_drift/origin_stowed_contact_partition.hpp"
#include <optional>
#include <span>
namespace apsis_drift {
inline constexpr std::size_t kLowerCockpitMaximumDocumentBytes{
    std::size_t{2} * std::size_t{1024} * std::size_t{1024}};
inline constexpr std::size_t kLowerCockpitMaximumPolicyBytes{std::size_t{16} *
                                                             std::size_t{1024}};
inline constexpr std::size_t kStowedCockpitMaximumDocumentBytes{
    std::size_t{128} * std::size_t{1024}};
inline constexpr std::size_t kStowedCockpitMaximumFrameBytes{std::size_t{2} *
                                                             std::size_t{1024}};
enum class LowerCockpitContactBuffer : std::uint8_t {
  original,
  halo,
  replacement
};
struct LowerCockpitTriangleKey {
  LowerCockpitContactBuffer buffer{};
  // Original keys use their craft group; halo and replacement use group zero
  // in their separate namespaces. None is an evaluated source-object face ID.
  std::uint32_t group{}, triangle{};
  friend auto operator==(const LowerCockpitTriangleKey&,
                         const LowerCockpitTriangleKey&) -> bool = default;
};
struct LowerCockpitSourceObject {
  std::string source_object;
  std::uint32_t triangle_start{}, triangle_count{},
      source_evaluated_triangles{};
  std::optional<std::uint32_t> original_object;
  std::vector<std::uint32_t> evaluated_source_triangles;
};
struct LowerCockpitFace {
  LowerCockpitTriangleKey key;
  std::uint32_t object{};
  std::string_view source_object;
  // The original contact catalog lacks evaluated source-object face indices.
  std::optional<std::uint32_t> evaluated_source_triangle;
  std::array<RigidVector3, 3> points_current_metres;
  RigidVector3 unit_normal_current;
};
struct LowerCockpitReservationContact {
  LowerCockpitTriangleKey key;
  std::uint32_t object{};
  std::optional<std::uint32_t> evaluated_source_triangle;
  CabinProxyPart part{};
  CabinIntersection intersection{};
};
struct LowerCockpitReservationEvidence {
  bool coverage_complete{}, interior_clear{};
  std::vector<LowerCockpitReservationContact> contacts;
  // Fixed #355 technical reservations only. No actor, gait, boot support,
  // limb reach, occupied seat, mechanism sweep or attached-station permission.
};
struct LowerCockpitSurfaceEvidence {
  LowerCockpitFace face;
  RigidVector3 barycentric;
  double signed_plane_distance_metres{};
  bool point_on_face{};
};
// Sharing handles keep returned object spans and face source-name views alive.
// A moved-from handle returns empty views and refuses every query.
class OriginLowerCockpitContact {
 public:
  struct Data;
  OriginLowerCockpitContact(const OriginLowerCockpitContact&) = default;
  OriginLowerCockpitContact(OriginLowerCockpitContact&&) noexcept = default;
  auto operator=(const OriginLowerCockpitContact&)
      -> OriginLowerCockpitContact& = default;
  auto operator=(OriginLowerCockpitContact&&) noexcept
      -> OriginLowerCockpitContact& = default;
  [[nodiscard]] auto objects() const
      -> std::span<const LowerCockpitSourceObject>;
  // objects() remains the additive halo inventory. Replacements are separate.
  [[nodiscard]] auto replacement_objects() const
      -> std::span<const LowerCockpitSourceObject>;
  [[nodiscard]] auto stowed_partition() const
      -> const OriginStowedContactPartition*;
  // Original-source provenance; effective collision uses this handle's queries.
  [[nodiscard]] auto original_geometry() const
      -> const OriginCabinSeamGeometry*;

 private:
  explicit OriginLowerCockpitContact(std::shared_ptr<const Data>);
  std::shared_ptr<const Data> data_;
  friend auto decode_origin_lower_cockpit_contact(
      std::string_view, std::string_view, const OriginCabinSeamGeometry&)
      -> std::expected<OriginLowerCockpitContact, std::string>;
  friend auto make_origin_stowed_lower_cockpit_contact(
      const OriginLowerCockpitContact&, std::string_view, std::string_view)
      -> std::expected<OriginLowerCockpitContact, std::string>;
  friend auto lookup_lower_cockpit_face(const OriginLowerCockpitContact&,
                                        LowerCockpitTriangleKey)
      -> std::expected<LowerCockpitFace, std::string>;
  friend auto assess_lower_cockpit_reservations(
      const OriginLowerCockpitContact&, RigidVector3, RigidVector3)
      -> std::expected<LowerCockpitReservationEvidence, std::string>;
};
[[nodiscard]] auto decode_origin_lower_cockpit_contact(
    std::string_view halo, std::string_view policy,
    const OriginCabinSeamGeometry&)
    -> std::expected<OriginLowerCockpitContact, std::string>;
// Fixed static selection at kCabinSeamHardware. Direct REST points receive the
// complete seat-lift delta once. No actor or arbitrary-progress authority.
[[nodiscard]] auto make_origin_stowed_lower_cockpit_contact(
    const OriginLowerCockpitContact& base, std::string_view replacement_contact,
    std::string_view frame)
    -> std::expected<OriginLowerCockpitContact, std::string>;
[[nodiscard]] auto lookup_lower_cockpit_face(const OriginLowerCockpitContact&,
                                             LowerCockpitTriangleKey)
    -> std::expected<LowerCockpitFace, std::string>;
// Complete endpoint-union boxes conservatively contain the straight fixed-axis
// translation. Strict declared domain comparisons never use retained extrema.
[[nodiscard]] auto assess_lower_cockpit_reservations(
    const OriginLowerCockpitContact&, RigidVector3 from_foot,
    RigidVector3 to_foot)
    -> std::expected<LowerCockpitReservationEvidence, std::string>;
// Two-micrometre numerical point evidence, never a penetration allowance.
[[nodiscard]] auto assess_lower_cockpit_surface_point(
    const OriginLowerCockpitContact&, LowerCockpitTriangleKey,
    RigidVector3 point_current_metres)
    -> std::expected<LowerCockpitSurfaceEvidence, std::string>;
} // namespace apsis_drift
