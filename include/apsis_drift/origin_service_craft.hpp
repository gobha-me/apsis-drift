#pragma once

#include "apsis_drift/freedom_resources.hpp"
#include "apsis_drift/moon_landed_craft.hpp"

#include <span>

namespace apsis_drift {
inline constexpr std::uint32_t kOriginServiceCraftVersion{1};
inline constexpr SeedDomain kOriginServiceCraftSeedDomain =
    SeedDomain::origin_service_craft;
inline constexpr std::uint64_t kOriginServiceCargoCapacityQuanta =
    kFreedomFlightCapacityQuanta / 4;
inline constexpr SimulationTick kOriginServiceTransferTicks{1200};

struct OriginServiceCraftId {
  std::uint64_t value{};
  friend auto operator==(const OriginServiceCraftId&,
                         const OriginServiceCraftId&) -> bool = default;
};
struct OriginServiceCraftDescriptor {
  std::uint32_t version{kOriginServiceCraftVersion};
  Seed universe_seed;
  SystemId system;
  OriginServiceCraftId id;
  OriginPortId home_port;
  CraftFrameRecipe frame{kWayfarerFrameId, kWayfarerFrameVersion};
  std::uint64_t flight_capacity_quanta{kFreedomFlightCapacityQuanta};
  std::uint64_t cargo_capacity_quanta{kOriginServiceCargoCapacityQuanta};
  std::uint32_t jump_capacity{kFreedomJumpCapacity};
  double link_range_metres{40.0};
  double max_relative_speed_metres_per_second{0.1};
  std::uint64_t transfer_quanta_per_tick{kOriginServiceCargoCapacityQuanta /
                                         kOriginServiceTransferTicks};
  friend auto operator==(const OriginServiceCraftDescriptor&,
                         const OriginServiceCraftDescriptor&) -> bool = default;
};
struct OriginServiceCraftResources {
  OriginServiceCraftId craft;
  SimulationTick tick{};
  std::uint64_t flight_quanta{}, cargo_quanta{};
  std::uint32_t jump_charges{};
  friend auto operator==(const OriginServiceCraftResources&,
                         const OriginServiceCraftResources&) -> bool = default;
};
struct OriginServiceFuelReceipt {
  StarterCraftId target_craft;
  BodyTarget target_body;
  // Stable landed anchor checksum, NOT the changing same-clock resolved pose.
  std::uint64_t target_anchor_checksum{}, initial_target_quanta{};
  SimulationTick tick{};
  std::uint64_t delivered_quanta{};
  friend auto operator==(const OriginServiceFuelReceipt&,
                         const OriginServiceFuelReceipt&) -> bool = default;
};
struct OriginServiceCraftState {
  OriginServiceCraftDescriptor descriptor;
  RigidBodyState body;
  OriginServiceCraftResources resources;
  bool attached{};
  std::optional<OriginServiceFuelReceipt> delivery{};
  friend auto operator==(const OriginServiceCraftState&,
                         const OriginServiceCraftState&) -> bool = default;
};
struct OriginServiceFuelTransfer {
  OriginServiceCraftState service;
  FreedomResources receiver;
  std::uint64_t transferred_quanta{};
};
enum class OriginServiceCraftError : std::uint8_t {
  unsupported_version,
  invalid_owner,
  invalid_descriptor,
  invalid_resources,
  invalid_state,
  invalid_clock,
  occupied_port,
  unavailable_fuel,
  unavailable_charge,
  invalid_target,
  unsafe_service_geometry,
  stale_transfer
};
[[nodiscard]] auto generate_origin_service_craft_descriptor(
    const PhysicalLocalSystem&,
    std::uint32_t version = kOriginServiceCraftVersion)
    -> std::expected<OriginServiceCraftDescriptor, OriginServiceCraftError>;
[[nodiscard]] auto validate_origin_service_craft_descriptor(
    const PhysicalLocalSystem&, const OriginServiceCraftDescriptor&)
    -> std::expected<void, OriginServiceCraftError>;
// The journey must supply its complete existing attachment roster. Empty is
// useful for isolated source proofs, not a station-wide vacancy certificate.
[[nodiscard]] auto initialize_origin_service_craft(
    const PhysicalLocalSystem&, SimulationTick,
    std::span<const OriginDockConstraint> occupied = {})
    -> std::expected<OriginServiceCraftState, OriginServiceCraftError>;
[[nodiscard]] auto validate_origin_service_craft(
    const PhysicalLocalSystem&, const OriginServiceCraftState&,
    const PhysicalMoonDescriptor* = nullptr)
    -> std::expected<void, OriginServiceCraftError>;
// Hot ledger operations require an already source-authenticated descriptor.
// They check the closed scalar recipe/pools; owner admission is not repeated.
[[nodiscard]] auto advance_origin_service_resources(
    const OriginServiceCraftDescriptor&, const OriginServiceCraftResources&,
    SimulationTick source_tick, std::uint64_t actual_propulsion_debit)
    -> std::expected<OriginServiceCraftResources, OriginServiceCraftError>;
// Route owner pairs this debit with one authentic committed jump receipt.
[[nodiscard]] auto spend_origin_service_jump_charge(
    const OriginServiceCraftDescriptor&, const OriginServiceCraftResources&,
    SimulationTick source_tick)
    -> std::expected<OriginServiceCraftResources, OriginServiceCraftError>;
// One same-tick fuel transaction. No flight/actor tick, teleport, route,
// dispatch authorization, crew admission, refill or second world is supplied.
// Route/session must additionally bind a live seated request and return
// reserve.
[[nodiscard]] auto transfer_origin_service_fuel(
    const PhysicalMoonDescriptor&, const OriginServiceCraftState&,
    const FreedomFlightSaveDocument& recipient, const MoonLandedCraftAnchor&,
    const FreedomResources& receiver)
    -> std::expected<OriginServiceFuelTransfer, OriginServiceCraftError>;
} // namespace apsis_drift
