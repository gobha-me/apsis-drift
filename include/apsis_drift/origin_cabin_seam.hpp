#pragma once
#include "apsis_drift/origin_boarding_support.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace apsis_drift {
inline constexpr std::size_t kCabinContactMaximumBytes{
    std::size_t{32} * std::size_t{1024} * std::size_t{1024}};
inline constexpr double kCabinSeamFloorMetres{-100000.0 * 1e-6};
inline constexpr double kCabinSeamMinimumCenterZMetres{3.45};
inline constexpr double kCabinSeamMaximumCenterZMetres{3.84};
inline constexpr OperatingProgress kCabinSeamHardware{1, 1, 1, 0};
enum class CabinProxyPart : std::uint8_t { body, port_boot, starboard_boot };
enum class CabinIntersection : std::uint8_t { boundary, interior };
struct CabinProxyContact {
  BoardingTriangleKey triangle;
  std::uint32_t object{};
  CabinProxyPart part{};
  CabinIntersection intersection{};
};
struct CabinProxyClearance {
  bool coverage_complete{}, interior_clear{};
  std::vector<CabinProxyContact> contacts;
};
struct CabinSupportPiece {
  BoardingTriangleKey triangle;
  double area_square_metres{};
  std::vector<RigidVector3> polygon;
};
struct CabinSoleSupport {
  double area_square_metres{}, area_fraction{};
  std::vector<CabinSupportPiece> pieces;
};
struct CabinSeamSupport {
  std::array<CabinSoleSupport, 2> soles;
  std::vector<RigidVector3> support_hull;
  double projected_load_margin_metres{};
  bool area_sufficient{}, load_supported{};
};
struct CabinSeamCertificate {
  double from_center_z_metres{}, to_center_z_metres{};
  std::vector<double> critical_centers_z_metres;
  double minimum_sole_area_fraction{}, minimum_load_margin_metres{};
  CabinProxyClearance swept_clearance;
  bool finite_support{}, nonpenetrating_crossing{};
  // Craft-local geometric evidence only; no actor action, gait, friction,
  // pressure, attached-station coverage, limb reach or saved-phase authority.
};
// Only bounded admission constructs this immutable handle. Returned metadata
// pointers last while a sharing handle exists. Moved-from queries refuse.
class OriginCabinSeamGeometry {
 public:
  struct Data;
  OriginCabinSeamGeometry(const OriginCabinSeamGeometry&) = default;
  OriginCabinSeamGeometry(OriginCabinSeamGeometry&&) noexcept = default;
  auto operator=(const OriginCabinSeamGeometry&)
      -> OriginCabinSeamGeometry& = default;
  auto operator=(OriginCabinSeamGeometry&&) noexcept
      -> OriginCabinSeamGeometry& = default;
  [[nodiscard]] auto support_catalog() const -> const OriginBoardingSupport*;

 private:
  explicit OriginCabinSeamGeometry(std::shared_ptr<const Data>);
  std::shared_ptr<const Data> data_;
  friend auto decode_origin_cabin_seam_contact(std::string_view,
                                               const OriginBoardingSupport&)
      -> std::expected<OriginCabinSeamGeometry, std::string>;
  friend auto assess_origin_cabin_proxy_clearance(
      const OriginCabinSeamGeometry&, RigidVector3, RigidVector3)
      -> std::expected<CabinProxyClearance, std::string>;
  friend auto assess_origin_cabin_proxy_support(const OriginCabinSeamGeometry&,
                                                RigidVector3)
      -> std::expected<CabinSeamSupport, std::string>;
  friend auto assess_origin_cabin_seam_support(const OriginCabinSeamGeometry&,
                                               double)
      -> std::expected<CabinSeamSupport, std::string>;
  friend auto certify_origin_cabin_seam_translation(
      const OriginCabinSeamGeometry&, double, double)
      -> std::expected<CabinSeamCertificate, std::string>;
};
[[nodiscard]] auto decode_origin_cabin_seam_contact(
    std::string_view, const OriginBoardingSupport&)
    -> std::expected<OriginCabinSeamGeometry, std::string>;
// Fixed proxy, axes and hardware; arbitrary finite endpoint foot poses permit
// source-backed collision controls. Endpoint-union boxes conservatively cover
// the entire straight translation. Static crop and full moving-source coverage
// are independently selected policies, not caller-supplied waivers.
[[nodiscard]] auto assess_origin_cabin_proxy_clearance(
    const OriginCabinSeamGeometry&, RigidVector3, RigidVector3)
    -> std::expected<CabinProxyClearance, std::string>;
[[nodiscard]] auto assess_origin_cabin_proxy_support(
    const OriginCabinSeamGeometry&, RigidVector3)
    -> std::expected<CabinSeamSupport, std::string>;
[[nodiscard]] auto assess_origin_cabin_seam_support(
    const OriginCabinSeamGeometry&, double)
    -> std::expected<CabinSeamSupport, std::string>;
[[nodiscard]] auto certify_origin_cabin_seam_translation(
    const OriginCabinSeamGeometry&, double, double)
    -> std::expected<CabinSeamCertificate, std::string>;
} // namespace apsis_drift
