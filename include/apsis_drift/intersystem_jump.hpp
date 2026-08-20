#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>

#include "termforge/core/types.hpp"
#include "apsis_drift/intersystem_contract.hpp"
#include "apsis_drift/local_system.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kIntersystemJumpVersion{4};
inline constexpr double kAssistedTargetArrivalStandoffRadii{10.0};
inline constexpr double kAssistedOriginArrivalStandoffMetres{40'000.0};
inline constexpr std::int32_t kAlignedHeadingErrorMillidegrees{3'000};
inline constexpr std::int32_t kAlignedVelocityErrorBasisPoints{200};
inline constexpr std::int32_t kOffsetHeadingErrorMillidegrees{45'000};
inline constexpr std::int32_t kOffsetVelocityErrorBasisPoints{2'000};
inline constexpr std::int32_t kHeadingCorrectionMillidegreesPerTick{250};
inline constexpr std::int32_t kVelocityCorrectionBasisPointsPerTick{10};

enum class IntersystemJumpError : std::uint8_t {
  invalid_contract,
  invalid_phase,
  invalid_destination,
  invalid_arrival,
  ephemeris_failure,
  invalid_command,
  wrong_command_tick,
  tick_overflow,
  transition_failure,
  invalid_framebuffer,
};

struct IntersystemJumpAdvance {
  bool committed{};
  bool arrived{};
};

struct IntersystemJumpGuidance {
  std::int32_t heading_error_millidegrees{};
  std::int32_t velocity_error_basis_points{};
  IntersystemArrivalQuality projected_quality{
      IntersystemArrivalQuality::aligned};
  std::string correction;

  friend auto operator==(const IntersystemJumpGuidance&,
                         const IntersystemJumpGuidance&) -> bool = default;
};

struct IntersystemJumpSnapshot {
  std::string phase;
  std::string destination;
  SimulationTick elapsed_ticks{};
  SimulationTick duration_ticks{};
  double progress{};
  bool cancelable{};
  bool committed{};
  IntersystemRuleProfile rule_profile{IntersystemRuleProfile::assisted};
  std::optional<IntersystemJumpGuidance> alignment;
  std::optional<IntersystemArrivalQuality> bound_quality;

  friend auto operator==(const IntersystemJumpSnapshot&,
                         const IntersystemJumpSnapshot&) -> bool = default;
};

struct OriginStationFlightState;

[[nodiscard]] auto validate_intersystem_arrival_solution(
    const IntersystemContractState& contract,
    const LocalSystemDescriptor& destination,
    const IntersystemArrivalSolution& solution)
    -> std::expected<void, IntersystemJumpError>;

[[nodiscard]] auto resolve_intersystem_jump_arrival(
    const IntersystemContractState& contract,
    const LocalSystemDescriptor& destination)
    -> std::expected<IntersystemArrivalSolution, IntersystemJumpError>;

[[nodiscard]] auto begin_intersystem_jump(
    IntersystemContractState& contract) noexcept
    -> std::expected<void, IntersystemJumpError>;

[[nodiscard]] auto begin_intersystem_jump(
    IntersystemContractState& contract,
    const OriginStationFlightState& origin_flight) noexcept
    -> std::expected<void, IntersystemJumpError>;

[[nodiscard]] auto cancel_intersystem_jump(
    IntersystemContractState& contract) noexcept
    -> std::expected<void, IntersystemJumpError>;

// Advances exactly one authoritative 120 Hz tick. Threshold transitions and
// arrival binding commit atomically; a failure leaves the contract unchanged.
[[nodiscard]] auto advance_intersystem_jump_tick(
    IntersystemContractState& contract,
    const LocalSystemDescriptor& destination,
    std::span<const FlightCommand> commands = {})
    -> std::expected<IntersystemJumpAdvance, IntersystemJumpError>;

[[nodiscard]] auto resolve_intersystem_jump_guidance(
    const IntersystemContractState& contract)
    -> std::expected<IntersystemJumpGuidance, IntersystemJumpError>;

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
