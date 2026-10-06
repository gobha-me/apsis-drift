#pragma once
#include "apsis_drift/origin_boarding_intermediate_pause_support.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kBoardingIntermediatePauseEnvelopeVersion{1};
inline constexpr std::size_t kBoardingIntermediatePauseEnvelopeOutputBytes{
    1024},
    kBoardingIntermediatePauseEnvelopeScratchBytes{49152},
    kBoardingIntermediatePauseEnvelopeDefinitionGuards{33};
enum class BoardingIntermediatePauseEnvelopeAxis : std::uint8_t {
  current_contact_positive_z
};
enum class BoardingIntermediatePauseEnvelopeState : std::uint8_t {
  not_run,
  prerequisite_refused,
  capacity,
  unsupported,
  not_refuted,
  necessary_refuted
};
enum class BoardingIntermediatePauseEnvelopeCondition : std::uint8_t {
  none,
  invalid_limits,
  output_capacity,
  invalid_binding,
  unsupported,
  child_unavailable,
  child_prerequisite,
  child_terminal,
  definition_refused,
  sole_capacity,
  source_vertex_capacity,
  minimum_capacity,
  weighted_capacity,
  gap_capacity,
  internal_guard_capacity
};
struct BoardingIntermediatePauseEnvelopeSite {
  std::string_view name;
  std::array<LowerCockpitTriangleKey, 2> keys;
  double plane{};
  BoardingFootSiteScalarBounds sole_max_z, source_max_z, upper_z;
  std::array<bool, 4> source_vertex_evaluated{};
  bool sole_evaluated{}, minimum_evaluated{};
};
struct BoardingIntermediatePauseEnvelopeCounters {
  std::size_t child_calls{};
  BoardingIntermediatePauseCounters pause;
  std::size_t definition_guards{}, sole_records{}, source_vertices{}, minima{},
      weighted_operations{}, gap_operations{};
};
struct BoardingIntermediatePauseEnvelopeRefusal {
  BoardingIntermediatePauseEnvelopeCondition condition{};
  std::optional<std::size_t> side, vertex, operation;
};
struct BoardingIntermediatePauseEnvelopeDiagnostic {
  OriginBoardingIntermediatePauseSupport source;
  std::uint32_t version{kBoardingIntermediatePauseEnvelopeVersion};
  BoardingIntermediatePauseEnvelopeAxis axis{};
  bool reverse{};
  std::array<BoardingIntermediatePauseEnvelopeSite, 2> sites;
  BoardingFootSiteScalarBounds com_z, weighted_upper_z, gap_z;
  BoardingIntermediatePauseEnvelopeCounters work;
  std::array<bool, kBoardingIntermediatePauseEnvelopeDefinitionGuards>
      definition_evaluated{};
  std::array<bool, 3> weighted_evaluated{};
  bool gap_evaluated{}, projection_available{};
  std::optional<BoardingIntermediatePauseEnvelopeRefusal> first_refusal;
  std::optional<BoardingIntermediatePauseRefusal> child_first_refusal;
  BoardingIntermediatePauseCondition child_stop_condition{};
  std::size_t child_output_bytes{}, output_bytes{};
  BoardingIntermediatePauseEnvelopeState state{};
  bool arithmetic_supported{}, envelope_assessed{}, necessary_refuted{};
  explicit BoardingIntermediatePauseEnvelopeDiagnostic(
      const OriginBoardingIntermediatePauseSupport& p)
      : source(p) {}
  static constexpr bool source_qualified{false}, body_qualified{false},
      fixture_qualified{false}, contact_qualified{false}, load_qualified{false},
      support_qualified{false}, self_qualified{false},
      material_qualified{false}, world_qualified{false}, route_qualified{false},
      seat_qualified{false}, actor_qualified{false}, save_qualified{false},
      dynamics_qualified{false}, first_flight_qualified{false};
};
[[nodiscard]] auto assess_origin_boarding_intermediate_pause_envelope(
    const OriginBoardingIntermediatePauseSupport&, bool reverse = false)
    -> std::expected<BoardingIntermediatePauseEnvelopeDiagnostic, std::string>;
} // namespace apsis_drift
