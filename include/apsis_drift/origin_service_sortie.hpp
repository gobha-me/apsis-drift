#pragma once
#include "apsis_drift/origin_service_route.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kOriginServiceSortieVersion{1};
inline constexpr SimulationTick kOriginServiceSortiePhaseTicks{36000};
inline constexpr SimulationTick kOriginServiceSortieSpoolTicks{360};
// Service recipe only: actual outward travel precedes either charged jump.
inline constexpr double kOriginServiceWithdrawalMetres{150};
enum class OriginServiceSortiePhase : std::uint8_t {
  requested,
  departing,
  outward_spool,
  outward_transit,
  approaching,
  delivering,
  withdrawing,
  return_spool,
  return_transit,
  returning,
  completed,
  canceled
};
struct OriginServiceSortie {
  std::uint32_t version{kOriginServiceSortieVersion};
  OriginServiceCraftState service;
  OriginServiceBeacon beacon;
  OriginServiceSortiePhase phase{OriginServiceSortiePhase::requested};
  // Terminal states preserve their event clock and have deadline_tick0.
  SimulationTick start_tick{}, phase_start_tick{}, deadline_tick{};
  bool return_requested{};
  std::optional<OriginServiceRouteArrival> pending{};
  std::optional<OriginServiceCraftResources> commitment_resources{};
  friend auto operator==(const OriginServiceSortie&, const OriginServiceSortie&)
      -> bool = default;
};
// Only a currently supported actual recipient is passed by the outer journey.
// An absent snapshot means leave/abort; it cannot create a new target/anchor.
struct OriginServiceSortieRecipient {
  const FreedomFlightSaveDocument& flight;
  const MoonLandedCraftAnchor& anchor;
  const FreedomResources& resources;
};
struct OriginServiceSortieStep {
  OriginServiceSortie sortie;
  std::optional<FreedomResources> receiver{};
  std::uint64_t transferred_quanta{}, propulsion_quanta{};
  VacuumActuation propulsion{};
};
[[nodiscard]] auto begin_origin_service_sortie(
    const PhysicalLocalSystem&, const PhysicalMoonDescriptor&,
    const OriginServiceCraftState&, const OriginServiceBeacon&,
    const OriginServiceSortieRecipient&)
    -> std::expected<OriginServiceSortie, std::string>;
[[nodiscard]] auto validate_origin_service_sortie(const PhysicalLocalSystem&,
                                                  const PhysicalMoonDescriptor&,
                                                  const OriginServiceSortie&)
    -> std::expected<void, std::string>;
[[nodiscard]] auto cancel_origin_service_sortie(const PhysicalLocalSystem&,
                                                const PhysicalMoonDescriptor&,
                                                const OriginServiceSortie&)
    -> std::expected<OriginServiceSortie, std::string>;
// One outer world tick, no nested player session. Failure leaves both inputs
// unchanged. Caller commits returned NPC and optional fuel credit atomically.
// Occupants are the other same-clock actual station attachment constraints.
[[nodiscard]] auto advance_origin_service_sortie(
    const PhysicalLocalSystem&, const PhysicalMoonDescriptor&,
    const OriginServiceSortie&, SimulationTick world_tick,
    const OriginServiceSortieRecipient* = nullptr,
    std::span<const OriginDockConstraint> occupants = {})
    -> std::expected<OriginServiceSortieStep, std::string>;
} // namespace apsis_drift
