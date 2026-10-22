#pragma once
#include "apsis_drift/freedom_jump_targeting.hpp"
#include "apsis_drift/freedom_resources.hpp"
#include "apsis_drift/moon_landed_craft.hpp"
#include "apsis_drift/moon_terrain_impact.hpp"
#include "apsis_drift/origin_service_craft.hpp"

namespace apsis_drift {
// Purpose-specific service owner; no player-save carrier.
inline constexpr double kOriginServiceArrivalHeightMetres{500};
inline constexpr double kOriginServiceHoverHeightMetres{30};
struct OriginServiceBeacon {
  std::uint32_t version{1};
  StarterCraftId target;
  std::optional<FreedomCraftLineage> lineage;
  MoonLandedCraftAnchor anchor;
  friend auto operator==(const OriginServiceBeacon&, const OriginServiceBeacon&)
      -> bool = default;
};
struct OriginServiceRouteArrival {
  std::uint32_t version{1};
  OriginServiceCraftId craft;
  RigidBodyState source;
  std::uint64_t source_checksum{};
  SimulationTick arrival_tick{};
  RigidBodyState body;
  OriginServiceCraftDescriptor descriptor;
  std::optional<OriginServiceBeacon> beacon;
  friend auto operator==(const OriginServiceRouteArrival&,
                         const OriginServiceRouteArrival&) -> bool = default;
};
struct OriginServiceRouteStep {
  VacuumActuation propulsion;
  std::uint64_t fuel_debit_quanta{};
  double target_distance_metres{}, relative_speed_metres_per_second{};
  bool link_ready{};
};
[[nodiscard]] auto validate_origin_service_beacon(
    const OriginServiceCraftDescriptor&, const PhysicalMoonDescriptor&,
    const OriginServiceBeacon&, SimulationTick)
    -> std::expected<void, std::string>;
[[nodiscard]] auto freeze_origin_service_moon_arrival(
    const PhysicalLocalSystem&, const OriginServiceCraftState&,
    const PhysicalMoonDescriptor&, const OriginServiceBeacon&)
    -> std::expected<OriginServiceRouteArrival, std::string>;
[[nodiscard]] auto advance_origin_service_moon_route(
    const PhysicalLocalSystem&, const PhysicalMoonDescriptor&,
    const OriginServiceBeacon&, OriginServiceCraftState&,
    double goal_height_metres)
    -> std::expected<OriginServiceRouteStep, std::string>;
[[nodiscard]] auto freeze_origin_service_home_arrival(
    const PhysicalLocalSystem&, const OriginServiceCraftState&,
    const PhysicalMoonDescriptor&)
    -> std::expected<OriginServiceRouteArrival, std::string>;
[[nodiscard]] auto advance_origin_service_home_route(
    const PhysicalLocalSystem&, OriginServiceCraftState&,
    double outward_standoff_metres = 0)
    -> std::expected<OriginServiceRouteStep, std::string>;
} // namespace apsis_drift
