#pragma once

#include "apsis_drift/freedom_docking_save.hpp"
#include "apsis_drift/intersystem_jump.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomResourceVersion{1};
inline constexpr std::uint64_t kFreedomQuantaPerNewtonSecond{1'560'000};
inline constexpr std::uint64_t kFreedomFlightCapacityQuanta{
    5'400ULL * 360'000ULL * kFreedomQuantaPerNewtonSecond};
inline constexpr std::uint64_t kFreedomFlightReserveQuanta{
    kFreedomFlightCapacityQuanta / 3};
inline constexpr std::uint64_t kFreedomFullMainTickQuanta{4'680'000'000};
inline constexpr std::uint32_t kFreedomJumpCapacity{3};

// The existing starter identity is the vehicle owner, not just a frame type.
// The tick is a consistency witness for the same authoritative flight clock.
struct FreedomResources {
  std::uint32_t version{kFreedomResourceVersion};
  StarterCraftId craft;
  CraftFrameRecipe frame{kWayfarerFrameId, kWayfarerFrameVersion};
  SimulationTick tick{};
  std::uint64_t flight_quanta{kFreedomFlightCapacityQuanta};
  std::uint32_t jump_charges{kFreedomJumpCapacity};
  friend auto operator==(const FreedomResources&, const FreedomResources&)
      -> bool = default;
};
enum class FreedomResourceError : std::uint8_t {
  invalid_state,
  wrong_owner,
  wrong_tick,
  invalid_measurement,
  tick_overflow,
  unavailable_charge,
  jump_refused,
  unsupported_service
};
struct FreedomResourceReading {
  std::uint64_t quantity_quanta{}, capacity_quanta{}, last_tick_quanta{};
  std::uint32_t jump_charges{};
  double fraction{}, main_equivalent_seconds{}, equivalent_newtons{};
  bool reserve{}, operationally_empty{}, propulsion_refused{};
};

[[nodiscard]] auto validate_freedom_resources(const FreedomResources&)
    -> std::expected<void, FreedomResourceError>;
[[nodiscard]] auto validate_freedom_resources(const FreedomResources&,
                                              const FreedomFlightSaveDocument&)
    -> std::expected<void, FreedomResourceError>;
[[nodiscard]] auto freedom_propulsion_tick_quanta(const VacuumActuation&)
    -> std::expected<std::uint64_t, FreedomResourceError>;
// Caller commits this returned candidate together with its physics candidate.
// Unaffordable work must be replaced by passive physics, never a free firing.
[[nodiscard]] auto advance_freedom_resource_tick(const FreedomResources&,
                                                 SimulationTick source_tick,
                                                 std::uint64_t actual_debit)
    -> std::expected<FreedomResources, FreedomResourceError>;
[[nodiscard]] auto freedom_resource_reading(const FreedomResources&,
                                            std::uint64_t last_tick_debit,
                                            bool propulsion_refused)
    -> std::expected<FreedomResourceReading, FreedomResourceError>;
// Supported attached service is the proof, not a caller-supplied proximity bit.
[[nodiscard]] auto replenish_freedom_resources(
    const FreedomResources&, const FreedomDockingSaveDocument&)
    -> std::expected<FreedomResources, FreedomResourceError>;

// Resource-coupled adapters retain the existing jump phase/arrival owner.
// No native route, mission, distance pricing or second jump simulation is
// added.
[[nodiscard]] auto begin_freedom_resource_jump(IntersystemContractState&,
                                               const FreedomResources&)
    -> std::expected<void, FreedomResourceError>;
[[nodiscard]] auto advance_freedom_resource_jump_tick(
    IntersystemContractState&, FreedomResources&, const LocalSystemDescriptor&,
    std::span<const FlightCommand> = {})
    -> std::expected<IntersystemJumpAdvance, FreedomResourceError>;
} // namespace apsis_drift
