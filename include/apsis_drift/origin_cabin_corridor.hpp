#pragma once

#include "apsis_drift/origin_lower_cockpit_contact.hpp"
#include <cstdint>
#include <limits>
#include <memory>
#include <span>

namespace apsis_drift {
inline constexpr double kCabinCorridorMinimumCenterZMetres{-.35};
inline constexpr double kCabinCorridorMaximumCenterZMetres{3.84};
inline constexpr double kCabinCorridorFloorMetres{kCabinSeamFloorMetres};
inline constexpr OperatingProgress kCabinCorridorHardware{1, 1, 1, 0};
// Reporting-only outward arithmetic bounds: fewer than 64 rounding operations
// on source/center coordinates bounded by 8 m, then the .28 m sole division.
// These never enter obstacle SAT, plane contact or source membership tests.
inline constexpr double kCabinCorridorLengthReportingBoundMetres{
    64 * std::numeric_limits<double>::epsilon() * 8};
inline constexpr double kCabinCorridorAreaFractionReportingBound{
    kCabinCorridorLengthReportingBoundMetres / .28};
enum class CabinCorridorLimitSide : std::int8_t {
  below = -1,
  at = 0,
  above = 1
};
struct CabinCorridorSupportLimit {
  double center_z_metres{};
  CabinCorridorLimitSide side{};
  double sole_area_fraction{}, projected_load_margin_metres{};
};
struct CabinCorridorCertificate {
  double from_center_z_metres{}, to_center_z_metres{};
  // Complete mathematical source-edge events and distinct binary arithmetic
  // events. Neither list is a time-sampling grid.
  std::vector<std::int64_t> source_events_micrometres;
  std::vector<double> critical_centers_z_metres;
  std::vector<CabinCorridorSupportLimit> support_limits;
  double minimum_sole_area_fraction{}, minimum_load_margin_metres{};
  LowerCockpitReservationEvidence swept_clearance;
  bool finite_support{}, boundary_support_compatible{},
      nonpenetrating_crossing{};
  // Fixed-axis, fixed-hardware geometric evidence only. No actor, stair gait,
  // seat occupancy, attached-station, pressure or saved-phase permission.
};
class OriginCabinCorridorGeometry {
 public:
  struct Data;
  OriginCabinCorridorGeometry(const OriginCabinCorridorGeometry&) = default;
  OriginCabinCorridorGeometry(OriginCabinCorridorGeometry&&) noexcept = default;
  auto operator=(const OriginCabinCorridorGeometry&)
      -> OriginCabinCorridorGeometry& = default;
  auto operator=(OriginCabinCorridorGeometry&&) noexcept
      -> OriginCabinCorridorGeometry& = default;
  // Sharing handles preserve the source-name views in these immutable faces.
  // A moved-from handle returns empty/null views and refuses queries.
  [[nodiscard]] auto selected_top_faces() const
      -> std::span<const LowerCockpitFace>;
  [[nodiscard]] auto lower_contact() const -> const OriginLowerCockpitContact*;

 private:
  explicit OriginCabinCorridorGeometry(std::shared_ptr<const Data>);
  std::shared_ptr<const Data> data_;
  friend auto make_origin_cabin_corridor_geometry(
      const OriginLowerCockpitContact&)
      -> std::expected<OriginCabinCorridorGeometry, std::string>;
  friend auto assess_origin_cabin_corridor_proxy_support(
      const OriginCabinCorridorGeometry&, RigidVector3)
      -> std::expected<CabinSeamSupport, std::string>;
  friend auto certify_origin_cabin_corridor_translation(
      const OriginCabinCorridorGeometry&, double, double)
      -> std::expected<CabinCorridorCertificate, std::string>;
};
[[nodiscard]] auto make_origin_cabin_corridor_geometry(
    const OriginLowerCockpitContact&)
    -> std::expected<OriginCabinCorridorGeometry, std::string>;
// Arbitrary bounded finite pose evidence permits wrong-floor/off-center/
// unsupported controls; it never certifies a corridor translation.
[[nodiscard]] auto assess_origin_cabin_corridor_proxy_support(
    const OriginCabinCorridorGeometry&, RigidVector3)
    -> std::expected<CabinSeamSupport, std::string>;
[[nodiscard]] auto assess_origin_cabin_corridor_support(
    const OriginCabinCorridorGeometry&, double center_z_metres)
    -> std::expected<CabinSeamSupport, std::string>;
[[nodiscard]] auto certify_origin_cabin_corridor_translation(
    const OriginCabinCorridorGeometry&, double from_center_z_metres,
    double to_center_z_metres)
    -> std::expected<CabinCorridorCertificate, std::string>;
} // namespace apsis_drift
