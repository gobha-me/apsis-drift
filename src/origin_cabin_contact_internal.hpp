#pragma once
// Purpose-specific implementation shared by the seam and lower cockpit
// providers. Coverage authority stays with those immutable providers.
#include "apsis_drift/origin_cabin_seam.hpp"
#include <optional>
#include <span>
namespace apsis_drift::detail {
struct CabinContactBox {
  RigidVector3 low, high;
};
struct CabinContactObstacle {
  BoardingTriangleKey key;
  std::uint32_t object{};
  std::array<RigidVector3, 3> points;
  CabinContactBox bounds;
};
struct CabinContactAccess {
  static auto obstacles(const OriginCabinSeamGeometry&)
      -> std::span<const CabinContactObstacle>;
  static auto reservations(RigidVector3) -> std::array<CabinContactBox, 3>;
  static auto intersection(const CabinContactObstacle&, CabinContactBox)
      -> std::optional<CabinIntersection>;
  static auto bounds(const std::array<RigidVector3, 3>&) -> CabinContactBox;
  static auto unite(CabinContactBox, CabinContactBox) -> CabinContactBox;
  static auto finite(RigidVector3) -> bool;
  // Actual planar source-polygon helpers; axes are X or Z only.
  static auto clip_floor_polygon(std::vector<RigidVector3>, std::size_t, double,
                                 bool) -> std::vector<RigidVector3>;
  static auto floor_polygon_area(const std::vector<RigidVector3>&) -> double;
  static auto floor_support_hull(std::vector<RigidVector3>)
      -> std::vector<RigidVector3>;
};
} // namespace apsis_drift::detail
