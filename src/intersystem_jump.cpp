#include "apsis_drift/intersystem_jump.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>

namespace apsis_drift {
namespace {

[[nodiscard]] auto finite(SystemPositionMetres value) noexcept -> bool {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] auto finite(SystemVelocityMetresPerSecond value) noexcept
    -> bool {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] auto elapsed(const IntersystemContractState& contract) noexcept
    -> SimulationTick {
  return contract.phase_started_tick &&
                 *contract.phase_started_tick <= contract.universe_tick
             ? contract.universe_tick - *contract.phase_started_tick
             : 0;
}

auto hash_word(std::uint64_t& hash, std::uint64_t value) noexcept -> void {
  constexpr std::uint64_t prime{1099511628211ULL};
  for (int byte = 0; byte < 8; ++byte) {
    hash ^= (value >> (byte * 8)) & 0xFFU;
    hash *= prime;
  }
}

[[nodiscard]] auto assessment_for(std::int32_t heading,
                                  std::int32_t velocity) noexcept
    -> IntersystemArrivalAssessment {
  const auto absolute_heading = std::abs(heading);
  const auto absolute_velocity = std::abs(velocity);
  const auto quality =
      absolute_heading <= kAlignedHeadingErrorMillidegrees &&
              absolute_velocity <= kAlignedVelocityErrorBasisPoints
          ? IntersystemArrivalQuality::aligned
          : absolute_heading <= kOffsetHeadingErrorMillidegrees &&
                    absolute_velocity <= kOffsetVelocityErrorBasisPoints
                ? IntersystemArrivalQuality::offset
                : IntersystemArrivalQuality::opposed;
  return {.heading_error_millidegrees = heading,
          .velocity_error_basis_points = velocity,
          .quality = quality};
}

[[nodiscard]] auto valid_alignment_command(FlightCommandKind kind) noexcept
    -> bool {
  switch (kind) {
    case FlightCommandKind::press_forward:
    case FlightCommandKind::release_forward:
    case FlightCommandKind::press_backward:
    case FlightCommandKind::release_backward:
    case FlightCommandKind::press_turn_left:
    case FlightCommandKind::release_turn_left:
    case FlightCommandKind::press_turn_right:
    case FlightCommandKind::release_turn_right: return true;
    case FlightCommandKind::press_strafe_left:
    case FlightCommandKind::release_strafe_left:
    case FlightCommandKind::press_strafe_right:
    case FlightCommandKind::release_strafe_right:
    case FlightCommandKind::press_rise:
    case FlightCommandKind::release_rise:
    case FlightCommandKind::press_fall:
    case FlightCommandKind::release_fall:
    case FlightCommandKind::toggle_autopilot:
    case FlightCommandKind::decrease_time_scale:
    case FlightCommandKind::increase_time_scale: return false;
  }
  return false;
}

auto apply_alignment_command(IntersystemJumpAlignmentState& alignment,
                             FlightCommandKind kind) noexcept -> void {
  switch (kind) {
    case FlightCommandKind::press_forward: alignment.controls.forward = true; break;
    case FlightCommandKind::release_forward: alignment.controls.forward = false; break;
    case FlightCommandKind::press_backward: alignment.controls.backward = true; break;
    case FlightCommandKind::release_backward: alignment.controls.backward = false; break;
    case FlightCommandKind::press_turn_left: alignment.controls.turn_left = true; break;
    case FlightCommandKind::release_turn_left: alignment.controls.turn_left = false; break;
    case FlightCommandKind::press_turn_right: alignment.controls.turn_right = true; break;
    case FlightCommandKind::release_turn_right: alignment.controls.turn_right = false; break;
    default: break;
  }
}

auto integrate_alignment(IntersystemJumpAlignmentState& alignment) noexcept
    -> void {
  const auto heading_intent =
      static_cast<std::int32_t>(alignment.controls.turn_right) -
      static_cast<std::int32_t>(alignment.controls.turn_left);
  const auto velocity_intent =
      static_cast<std::int32_t>(alignment.controls.forward) -
      static_cast<std::int32_t>(alignment.controls.backward);
  alignment.heading_error_millidegrees = std::clamp(
      alignment.heading_error_millidegrees +
          heading_intent * kHeadingCorrectionMillidegreesPerTick,
      -180'000, 180'000);
  alignment.velocity_error_basis_points = std::clamp(
      alignment.velocity_error_basis_points +
          velocity_intent * kVelocityCorrectionBasisPointsPerTick,
      -10'000, 10'000);
}

struct Vector3 {
  double x{};
  double y{};
  double z{};
};

[[nodiscard]] auto vector(SystemPositionMetres value) noexcept -> Vector3 {
  return {value.x, value.y, value.z};
}

[[nodiscard]] auto vector(SystemVelocityMetresPerSecond value) noexcept
    -> Vector3 {
  return {value.x, value.y, value.z};
}

[[nodiscard]] auto magnitude(Vector3 value) noexcept -> double {
  return std::hypot(value.x, value.y, value.z);
}

[[nodiscard]] auto normalized(Vector3 value) noexcept
    -> std::optional<Vector3> {
  const auto length = magnitude(value);
  if (!std::isfinite(length) || length <= 0.0) return std::nullopt;
  return Vector3{value.x / length, value.y / length, value.z / length};
}

[[nodiscard]] auto cross(Vector3 lhs, Vector3 rhs) noexcept -> Vector3 {
  return {lhs.y * rhs.z - lhs.z * rhs.y,
          lhs.z * rhs.x - lhs.x * rhs.z,
          lhs.x * rhs.y - lhs.y * rhs.x};
}

[[nodiscard]] auto rotate(Vector3 value, Vector3 axis,
                          double angle) noexcept -> Vector3 {
  const auto cosine = std::cos(angle);
  const auto sine = std::sin(angle);
  const auto dot = value.x * axis.x + value.y * axis.y + value.z * axis.z;
  const auto perpendicular = cross(axis, value);
  return {value.x * cosine + perpendicular.x * sine +
              axis.x * dot * (1.0 - cosine),
          value.y * cosine + perpendicular.y * sine +
              axis.y * dot * (1.0 - cosine),
          value.z * cosine + perpendicular.z * sine +
              axis.z * dot * (1.0 - cosine)};
}

[[nodiscard]] auto resolve_canonical_arrival(
    const IntersystemContractState& contract,
    const LocalSystemDescriptor& destination, SimulationTick arrival_tick,
    std::optional<IntersystemArrivalAssessment> assessment)
    -> std::expected<IntersystemArrivalSolution, IntersystemJumpError> {
  const bool outbound = destination.id == contract.identities.target_system;
  const bool returning = destination.id == contract.identities.origin_system;
  if ((!outbound && !returning) || !validate_local_system(destination) ||
      (outbound && !assessment) || (returning && assessment)) {
    return std::unexpected{IntersystemJumpError::invalid_destination};
  }

  IntersystemArrivalSolution solution{
      .destination = destination.id,
      .reference_planet = std::nullopt,
      .arrival_tick = arrival_tick,
      .position = {0.0, -kAssistedOriginArrivalRadiusMetres, 0.0},
      .velocity = {},
      .assessment = std::move(assessment),
  };
  if (outbound) {
    const auto body = find_local_system_planet(
        destination, contract.identities.target_planet);
    if (!body) {
      return std::unexpected{IntersystemJumpError::invalid_destination};
    }
    const auto ephemeris = resolve_planet_ephemeris(
        destination, contract.identities.target_planet,
        {.tick = arrival_tick, .sub_tick_fraction = 0.0});
    if (!ephemeris) {
      return std::unexpected{IntersystemJumpError::ephemeris_failure};
    }
    const auto tangent = normalized(vector(ephemeris->velocity));
    const auto radial = normalized(vector(ephemeris->position));
    const auto normal = radial && tangent
                            ? normalized(cross(*radial, *tangent))
                            : std::nullopt;
    if (!tangent || !radial || !normal) {
      return std::unexpected{IntersystemJumpError::invalid_arrival};
    }
    solution.reference_planet = contract.identities.target_planet;
    const auto& bound_assessment = *solution.assessment;
    if (bound_assessment.quality == IntersystemArrivalQuality::opposed) {
      solution.position = {-ephemeris->position.x, -ephemeris->position.y,
                           -ephemeris->position.z};
      solution.velocity = {-ephemeris->velocity.x, -ephemeris->velocity.y,
                           -ephemeris->velocity.z};
    } else {
      const double radius_metres =
          static_cast<double>((*body)->descriptor.radius.value) * 1'000.0;
      double standoff_radii = kAssistedTargetArrivalStandoffRadii;
      double angle{};
      if (bound_assessment.quality == IntersystemArrivalQuality::offset) {
        const double heading_severity =
            static_cast<double>(std::abs(
                bound_assessment.heading_error_millidegrees)) /
            static_cast<double>(kOffsetHeadingErrorMillidegrees);
        const double velocity_severity =
            static_cast<double>(std::abs(
                bound_assessment.velocity_error_basis_points)) /
            static_cast<double>(kOffsetVelocityErrorBasisPoints);
        const double severity = std::clamp(
            std::max(heading_severity, velocity_severity), 0.0, 1.0);
        standoff_radii += 90.0 * severity;
        angle = static_cast<double>(
                    bound_assessment.heading_error_millidegrees) *
                std::numbers::pi_v<double> / 180'000.0;
      }
      const Vector3 trailing{-tangent->x, -tangent->y, -tangent->z};
      const auto approach = rotate(trailing, *normal, angle);
      const double standoff = radius_metres * standoff_radii;
      solution.position = {
          ephemeris->position.x + approach.x * standoff,
          ephemeris->position.y + approach.y * standoff,
          ephemeris->position.z + approach.z * standoff,
      };
      const double velocity_scale =
          1.0 + static_cast<double>(
                    bound_assessment.velocity_error_basis_points) /
                    10'000.0;
      solution.velocity = {
          ephemeris->velocity.x * velocity_scale,
          ephemeris->velocity.y * velocity_scale,
          ephemeris->velocity.z * velocity_scale,
      };
    }
  }
  if (!finite(solution.position) || !finite(solution.velocity)) {
    return std::unexpected{IntersystemJumpError::invalid_arrival};
  }
  return solution;
}

}  // namespace

auto validate_intersystem_arrival_solution(
    const IntersystemContractState& contract,
    const LocalSystemDescriptor& destination,
    const IntersystemArrivalSolution& solution)
    -> std::expected<void, IntersystemJumpError> {
  if (!validate_intersystem_contract_state(contract)) {
    return std::unexpected{IntersystemJumpError::invalid_contract};
  }
  const bool outbound =
      contract.travel_phase ==
          IntersystemTravelPhase::outbound_jump_committed ||
      contract.travel_phase == IntersystemTravelPhase::target_system_flight ||
      contract.travel_phase == IntersystemTravelPhase::target_planet_flight ||
      contract.travel_phase == IntersystemTravelPhase::return_jump_spooling;
  const bool returning =
      contract.travel_phase == IntersystemTravelPhase::return_jump_committed ||
      contract.travel_phase == IntersystemTravelPhase::origin_system_return;
  const auto expected_destination =
      outbound ? contract.identities.target_system
               : returning ? contract.identities.origin_system : SystemId{};
  if ((!outbound && !returning) || destination.id != expected_destination ||
      solution.destination != expected_destination) {
    return std::unexpected{IntersystemJumpError::invalid_arrival};
  }

  std::optional<IntersystemArrivalAssessment> assessment;
  if (outbound) {
    if (!solution.assessment ||
        assessment_for(solution.assessment->heading_error_millidegrees,
                       solution.assessment->velocity_error_basis_points) !=
            *solution.assessment) {
      return std::unexpected{IntersystemJumpError::invalid_arrival};
    }
    assessment = solution.assessment;
  } else if (solution.assessment) {
    return std::unexpected{IntersystemJumpError::invalid_arrival};
  }
  const auto expected = resolve_canonical_arrival(
      contract, destination, solution.arrival_tick, std::move(assessment));
  if (!expected || *expected != solution) {
    return std::unexpected{IntersystemJumpError::invalid_arrival};
  }
  return {};
}

auto resolve_intersystem_jump_arrival(
    const IntersystemContractState& contract,
    const LocalSystemDescriptor& destination)
    -> std::expected<IntersystemArrivalSolution, IntersystemJumpError> {
  if (!validate_intersystem_contract_state(contract)) {
    return std::unexpected{IntersystemJumpError::invalid_contract};
  }
  const bool outbound = contract.travel_phase ==
                        IntersystemTravelPhase::outbound_jump_spooling;
  const bool returning = contract.travel_phase ==
                         IntersystemTravelPhase::return_jump_spooling;
  if (!outbound && !returning) {
    return std::unexpected{IntersystemJumpError::invalid_phase};
  }
  const auto expected_destination =
      outbound ? contract.identities.target_system
               : contract.identities.origin_system;
  if (destination.id != expected_destination ||
      !validate_local_system(destination)) {
    return std::unexpected{IntersystemJumpError::invalid_destination};
  }
  if (contract.universe_tick >
      std::numeric_limits<SimulationTick>::max() - kJumpTransitTicks) {
    return std::unexpected{IntersystemJumpError::tick_overflow};
  }
  const auto arrival_tick = contract.universe_tick + kJumpTransitTicks;
  const auto assessment =
      outbound
          ? std::optional{contract.rule_profile == IntersystemRuleProfile::pilot
                              ? assessment_for(
                                    contract.jump_alignment
                                        ->heading_error_millidegrees,
                                    contract.jump_alignment
                                        ->velocity_error_basis_points)
                              : IntersystemArrivalAssessment{
                                    .quality =
                                        IntersystemArrivalQuality::aligned}}
          : std::nullopt;
  return resolve_canonical_arrival(contract, destination, arrival_tick,
                                   assessment);
}

auto begin_intersystem_jump(IntersystemContractState& contract) noexcept
    -> std::expected<void, IntersystemJumpError> {
  const auto command =
      contract.travel_phase == IntersystemTravelPhase::origin_system_flight
          ? IntersystemContractCommand::begin_outbound_jump
          : contract.travel_phase ==
                    IntersystemTravelPhase::target_system_flight &&
                contract.mission_phase ==
                    IntersystemMissionPhase::objective_complete
            ? IntersystemContractCommand::begin_return_jump
            : static_cast<IntersystemContractCommand>(255);
  auto next = contract;
  if (!advance_intersystem_contract(next, next.universe_tick, command)) {
    return std::unexpected{IntersystemJumpError::transition_failure};
  }
  contract = std::move(next);
  return {};
}

auto cancel_intersystem_jump(IntersystemContractState& contract) noexcept
    -> std::expected<void, IntersystemJumpError> {
  auto next = contract;
  if (!advance_intersystem_contract(next, next.universe_tick,
                                    IntersystemContractCommand::cancel_jump)) {
    return std::unexpected{IntersystemJumpError::transition_failure};
  }
  contract = std::move(next);
  return {};
}

auto advance_intersystem_jump_tick(
    IntersystemContractState& contract,
    const LocalSystemDescriptor& destination,
    std::span<const FlightCommand> commands)
    -> std::expected<IntersystemJumpAdvance, IntersystemJumpError> {
  if (!validate_intersystem_contract_state(contract)) {
    return std::unexpected{IntersystemJumpError::invalid_contract};
  }
  const bool outbound =
      contract.travel_phase ==
          IntersystemTravelPhase::outbound_jump_spooling ||
      contract.travel_phase ==
          IntersystemTravelPhase::outbound_jump_committed;
  const bool returning =
      contract.travel_phase == IntersystemTravelPhase::return_jump_spooling ||
      contract.travel_phase == IntersystemTravelPhase::return_jump_committed;
  if (!outbound && !returning) {
    return std::unexpected{IntersystemJumpError::invalid_phase};
  }
  const auto expected_destination =
      outbound ? contract.identities.target_system
               : contract.identities.origin_system;
  if (destination.id != expected_destination) {
    return std::unexpected{IntersystemJumpError::invalid_destination};
  }
  if (contract.travel_phase == IntersystemTravelPhase::return_jump_spooling) {
    const auto target =
        generate_local_system(contract.identities.target_system_seed);
    if (!contract.arrival_solution ||
        !validate_intersystem_arrival_solution(
            contract, target, *contract.arrival_solution)) {
      return std::unexpected{IntersystemJumpError::invalid_arrival};
    }
  }
  const bool committed_before =
      contract.travel_phase ==
          IntersystemTravelPhase::outbound_jump_committed ||
      contract.travel_phase == IntersystemTravelPhase::return_jump_committed;
  if (committed_before &&
      (!contract.arrival_solution ||
       !validate_intersystem_arrival_solution(
           contract, destination, *contract.arrival_solution))) {
    return std::unexpected{IntersystemJumpError::invalid_arrival};
  }
  auto next = contract;
  const bool spooling_before =
      next.travel_phase == IntersystemTravelPhase::outbound_jump_spooling ||
      next.travel_phase == IntersystemTravelPhase::return_jump_spooling;
  if (!commands.empty() &&
      (!spooling_before || !next.jump_alignment)) {
    return std::unexpected{IntersystemJumpError::invalid_command};
  }
  for (const auto& command : commands) {
    if (!valid_alignment_command(command.kind)) {
      return std::unexpected{IntersystemJumpError::invalid_command};
    }
    if (command.tick != contract.universe_tick) {
      return std::unexpected{IntersystemJumpError::wrong_command_tick};
    }
  }
  if (next.jump_alignment) {
    for (const auto& command : commands) {
      apply_alignment_command(*next.jump_alignment, command.kind);
    }
    integrate_alignment(*next.jump_alignment);
  }
  if (!advance_intersystem_time(next, 1)) {
    return std::unexpected{IntersystemJumpError::tick_overflow};
  }
  IntersystemJumpAdvance result;
  const bool spooling =
      next.travel_phase == IntersystemTravelPhase::outbound_jump_spooling ||
      next.travel_phase == IntersystemTravelPhase::return_jump_spooling;
  if (spooling && elapsed(next) >= kJumpSpoolTicks) {
    auto solution = resolve_intersystem_jump_arrival(next, destination);
    if (!solution) return std::unexpected{solution.error()};
    const bool outbound_commit =
        next.travel_phase == IntersystemTravelPhase::outbound_jump_spooling;
    next.travel_phase =
        outbound_commit ? IntersystemTravelPhase::outbound_jump_committed
                        : IntersystemTravelPhase::return_jump_committed;
    next.committed_jump_destination =
        outbound_commit ? next.identities.target_system
                        : next.identities.origin_system;
    next.phase_started_tick = next.universe_tick;
    next.jump_alignment.reset();
    next.arrival_solution = std::move(*solution);
    if (!validate_intersystem_contract_state(next) ||
        !validate_intersystem_arrival_solution(
            next, destination, *next.arrival_solution)) {
      return std::unexpected{IntersystemJumpError::transition_failure};
    }
    result.committed = true;
  } else {
    const bool committed =
        next.travel_phase ==
            IntersystemTravelPhase::outbound_jump_committed ||
        next.travel_phase == IntersystemTravelPhase::return_jump_committed;
    if (committed && elapsed(next) >= kJumpTransitTicks) {
      if (!next.arrival_solution ||
          next.arrival_solution->arrival_tick != next.universe_tick) {
        return std::unexpected{IntersystemJumpError::invalid_arrival};
      }
      const bool outbound_arrival =
          next.travel_phase ==
          IntersystemTravelPhase::outbound_jump_committed;
      next.travel_phase =
          outbound_arrival ? IntersystemTravelPhase::target_system_flight
                           : IntersystemTravelPhase::origin_system_return;
      next.current_system = outbound_arrival ? next.identities.target_system
                                             : next.identities.origin_system;
      next.committed_jump_destination.reset();
      next.phase_started_tick.reset();
      if (!validate_intersystem_contract_state(next) ||
          !validate_intersystem_arrival_solution(
              next, destination, *next.arrival_solution)) {
        return std::unexpected{IntersystemJumpError::transition_failure};
      }
      result.arrived = true;
    }
  }
  contract = std::move(next);
  return result;
}

auto resolve_intersystem_jump_guidance(
    const IntersystemContractState& contract)
    -> std::expected<IntersystemJumpGuidance, IntersystemJumpError> {
  if (!validate_intersystem_contract_state(contract) ||
      contract.travel_phase !=
          IntersystemTravelPhase::outbound_jump_spooling ||
      contract.rule_profile != IntersystemRuleProfile::pilot ||
      !contract.jump_alignment) {
    return std::unexpected{IntersystemJumpError::invalid_phase};
  }
  const auto assessment = assessment_for(
      contract.jump_alignment->heading_error_millidegrees,
      contract.jump_alignment->velocity_error_basis_points);
  std::string correction{"HOLD ALIGNMENT"};
  if (assessment.heading_error_millidegrees >
      kAlignedHeadingErrorMillidegrees) {
    correction = "A / LEFT";
  } else if (assessment.heading_error_millidegrees <
             -kAlignedHeadingErrorMillidegrees) {
    correction = "D / RIGHT";
  } else if (assessment.velocity_error_basis_points >
             kAlignedVelocityErrorBasisPoints) {
    correction = "S / SLOW";
  } else if (assessment.velocity_error_basis_points <
             -kAlignedVelocityErrorBasisPoints) {
    correction = "W / FAST";
  }
  return IntersystemJumpGuidance{
      .heading_error_millidegrees =
          assessment.heading_error_millidegrees,
      .velocity_error_basis_points =
          assessment.velocity_error_basis_points,
      .projected_quality = assessment.quality,
      .correction = std::move(correction),
  };
}

auto intersystem_jump_snapshot(const IntersystemContractState& contract)
    -> std::expected<IntersystemJumpSnapshot, IntersystemJumpError> {
  if (!validate_intersystem_contract_state(contract) ||
      !contract.phase_started_tick) {
    return std::unexpected{IntersystemJumpError::invalid_contract};
  }
  const bool spooling =
      contract.travel_phase ==
          IntersystemTravelPhase::outbound_jump_spooling ||
      contract.travel_phase == IntersystemTravelPhase::return_jump_spooling;
  const bool committed =
      contract.travel_phase ==
          IntersystemTravelPhase::outbound_jump_committed ||
      contract.travel_phase == IntersystemTravelPhase::return_jump_committed;
  if (!spooling && !committed) {
    return std::unexpected{IntersystemJumpError::invalid_phase};
  }
  const auto duration = spooling ? kJumpSpoolTicks : kJumpTransitTicks;
  const auto ticks = std::min(elapsed(contract), duration);
  const auto destination =
      contract.travel_phase ==
                  IntersystemTravelPhase::outbound_jump_spooling ||
              contract.travel_phase ==
                  IntersystemTravelPhase::outbound_jump_committed
          ? contract.identities.target_system
          : contract.identities.origin_system;
  std::optional<IntersystemJumpGuidance> alignment;
  if (spooling && contract.rule_profile == IntersystemRuleProfile::pilot) {
    const auto guidance = resolve_intersystem_jump_guidance(contract);
    if (!guidance) {
      return std::unexpected{IntersystemJumpError::invalid_contract};
    }
    alignment = *guidance;
  }
  std::optional<IntersystemArrivalQuality> bound_quality;
  if (contract.arrival_solution && contract.arrival_solution->assessment) {
    bound_quality = contract.arrival_solution->assessment->quality;
  }
  return IntersystemJumpSnapshot{
      .phase = spooling ? "SPOOLING" : "TRANSIT COMMITTED",
      .destination = system_id_string(destination),
      .elapsed_ticks = ticks,
      .duration_ticks = duration,
      .progress = static_cast<double>(ticks) / static_cast<double>(duration),
      .cancelable = spooling,
      .committed = committed,
      .rule_profile = contract.rule_profile,
      .alignment = std::move(alignment),
      .bound_quality = bound_quality,
  };
}

auto intersystem_arrival_checksum(
    const IntersystemContractState& contract) noexcept -> std::uint64_t {
  constexpr std::uint64_t offset{14695981039346656037ULL};
  std::uint64_t hash = offset;
  hash_word(hash, contract.universe_tick);
  hash_word(hash, contract.current_system.value);
  hash_word(hash, static_cast<std::uint64_t>(contract.travel_phase));
  if (!contract.arrival_solution) return hash;
  const auto& arrival = *contract.arrival_solution;
  hash_word(hash, arrival.destination.value);
  hash_word(hash, arrival.reference_planet
                      ? arrival.reference_planet->value
                      : std::numeric_limits<std::uint64_t>::max());
  hash_word(hash, arrival.arrival_tick);
  hash_word(hash, std::bit_cast<std::uint64_t>(arrival.position.x));
  hash_word(hash, std::bit_cast<std::uint64_t>(arrival.position.y));
  hash_word(hash, std::bit_cast<std::uint64_t>(arrival.position.z));
  hash_word(hash, std::bit_cast<std::uint64_t>(arrival.velocity.x));
  hash_word(hash, std::bit_cast<std::uint64_t>(arrival.velocity.y));
  hash_word(hash, std::bit_cast<std::uint64_t>(arrival.velocity.z));
  hash_word(hash, arrival.assessment.has_value() ? 1U : 0U);
  if (arrival.assessment) {
    hash_word(hash, static_cast<std::uint64_t>(
                        static_cast<std::int64_t>(
                            arrival.assessment->heading_error_millidegrees)));
    hash_word(hash, static_cast<std::uint64_t>(
                        static_cast<std::int64_t>(
                            arrival.assessment->velocity_error_basis_points)));
    hash_word(hash, static_cast<std::uint8_t>(arrival.assessment->quality));
  }
  return hash;
}

auto render_intersystem_jump(const IntersystemJumpSnapshot& snapshot,
                             int width, int height,
                             std::span<termforge::Pixel> destination)
    -> std::expected<void, IntersystemJumpError> {
  if (width <= 0 || height <= 0 ||
      static_cast<std::size_t>(width) >
          std::numeric_limits<std::size_t>::max() /
              static_cast<std::size_t>(height) ||
      destination.size() != static_cast<std::size_t>(width) *
                                static_cast<std::size_t>(height) ||
      !std::isfinite(snapshot.progress) || snapshot.progress < 0.0 ||
      snapshot.progress > 1.0 || snapshot.duration_ticks == 0 ||
      snapshot.elapsed_ticks > snapshot.duration_ticks ||
      (snapshot.alignment &&
       (snapshot.alignment->heading_error_millidegrees < -180'000 ||
        snapshot.alignment->heading_error_millidegrees > 180'000 ||
        snapshot.alignment->velocity_error_basis_points < -10'000 ||
        snapshot.alignment->velocity_error_basis_points > 10'000))) {
    return std::unexpected{IntersystemJumpError::invalid_framebuffer};
  }
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x);
      const std::uint64_t noise =
          (static_cast<std::uint64_t>(x + 1) * 0x9e3779b185ebca87ULL) ^
          (static_cast<std::uint64_t>(y + 1) * 0xc2b2ae3d27d4eb4fULL) ^
          (snapshot.elapsed_ticks * 0x165667b19e3779f9ULL);
      const double dx = static_cast<double>(x) -
                        static_cast<double>(width - 1) * 0.5;
      const double dy = static_cast<double>(y) -
                        static_cast<double>(height - 1) * 0.5;
      const double radius = std::hypot(dx, dy) /
                            static_cast<double>(std::max(width, height));
      const bool star =
          (noise & 0x7ffU) < (snapshot.committed ? 18U : 5U);
      const double flare =
          snapshot.committed ? std::max(0.0, 1.0 - radius * 5.0) : 0.0;
      destination[index] = {
          static_cast<std::uint8_t>(std::clamp(
              8.0 + flare * 150.0 + (star ? 120.0 : 0.0), 0.0, 255.0)),
          static_cast<std::uint8_t>(std::clamp(
              15.0 + flare * 190.0 + (star ? 150.0 : 0.0), 0.0, 255.0)),
          static_cast<std::uint8_t>(std::clamp(
              28.0 + snapshot.progress * 55.0 + flare * 210.0 +
                  (star ? 210.0 : 0.0),
              0.0, 255.0)),
          255};
    }
  }
  if (snapshot.alignment) {
    const double horizontal = std::clamp(
        static_cast<double>(snapshot.alignment->heading_error_millidegrees) /
            static_cast<double>(kOffsetHeadingErrorMillidegrees),
        -1.0, 1.0);
    const double vertical = std::clamp(
        static_cast<double>(snapshot.alignment->velocity_error_basis_points) /
            static_cast<double>(kOffsetVelocityErrorBasisPoints),
        -1.0, 1.0);
    const int center_x = width / 2;
    const int center_y = height / 2;
    const int marker_x = std::clamp(
        center_x + static_cast<int>(std::round(horizontal * width * 0.35)),
        0, width - 1);
    const int marker_y = std::clamp(
        center_y + static_cast<int>(std::round(vertical * height * 0.35)),
        0, height - 1);
    const auto paint = [&](int x, int y, termforge::Pixel pixel) {
      if (x < 0 || y < 0 || x >= width || y >= height) return;
      destination[static_cast<std::size_t>(y) *
                      static_cast<std::size_t>(width) +
                  static_cast<std::size_t>(x)] = pixel;
    };
    const termforge::Pixel guide{90, 210, 220, 255};
    const termforge::Pixel marker{245, 180, 80, 255};
    for (int delta = -5; delta <= 5; ++delta) {
      paint(center_x + delta, center_y, guide);
      paint(center_x, center_y + delta, guide);
    }
    for (int delta = -3; delta <= 3; ++delta) {
      paint(marker_x + delta, marker_y, marker);
      paint(marker_x, marker_y + delta, marker);
    }
  }
  return {};
}

}  // namespace apsis_drift
