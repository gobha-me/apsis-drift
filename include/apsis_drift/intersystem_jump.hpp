#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>

#include "termforge/core/types.hpp"
#include "apsis_drift/intersystem_contract.hpp"
#include "apsis_drift/local_system.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kIntersystemJumpVersion{1};
inline constexpr double kAssistedTargetArrivalStandoffRadii{10.0};
inline constexpr double kAssistedOriginArrivalRadiusMetres{80'000'000'000.0};

enum class IntersystemJumpError : std::uint8_t {
  invalid_contract,
  invalid_phase,
  invalid_destination,
  invalid_arrival,
  ephemeris_failure,
  tick_overflow,
  transition_failure,
  invalid_framebuffer,
};

struct IntersystemJumpAdvance {
  bool committed{};
  bool arrived{};
};

struct IntersystemJumpSnapshot {
  std::string phase;
  std::string destination;
  SimulationTick elapsed_ticks{};
  SimulationTick duration_ticks{};
  double progress{};
  bool cancelable{};
  bool committed{};

  friend auto operator==(const IntersystemJumpSnapshot&,
                         const IntersystemJumpSnapshot&) -> bool = default;
};

[[nodiscard]] auto validate_intersystem_arrival_solution(
    const IntersystemContractState& contract,
    const IntersystemArrivalSolution& solution) noexcept
    -> std::expected<void, IntersystemJumpError>;

[[nodiscard]] auto resolve_assisted_jump_arrival(
    const IntersystemContractState& contract,
    const LocalSystemDescriptor& destination)
    -> std::expected<IntersystemArrivalSolution, IntersystemJumpError>;

[[nodiscard]] auto begin_assisted_jump(
    IntersystemContractState& contract) noexcept
    -> std::expected<void, IntersystemJumpError>;

[[nodiscard]] auto cancel_assisted_jump(
    IntersystemContractState& contract) noexcept
    -> std::expected<void, IntersystemJumpError>;

// Advances exactly one authoritative 120 Hz tick. Threshold transitions and
// arrival binding commit atomically; a failure leaves the contract unchanged.
[[nodiscard]] auto advance_assisted_jump_tick(
    IntersystemContractState& contract,
    const LocalSystemDescriptor& destination)
    -> std::expected<IntersystemJumpAdvance, IntersystemJumpError>;

[[nodiscard]] auto intersystem_jump_snapshot(
    const IntersystemContractState& contract)
    -> std::expected<IntersystemJumpSnapshot, IntersystemJumpError>;

[[nodiscard]] auto intersystem_arrival_checksum(
    const IntersystemContractState& contract) noexcept -> std::uint64_t;

[[nodiscard]] auto render_intersystem_jump(
    const IntersystemJumpSnapshot& snapshot, int width, int height,
    std::span<termforge::Pixel> destination)
    -> std::expected<void, IntersystemJumpError>;

}  // namespace apsis_drift
