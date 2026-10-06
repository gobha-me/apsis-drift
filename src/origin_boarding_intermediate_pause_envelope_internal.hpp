#pragma once
#include "apsis_drift/origin_boarding_intermediate_pause_envelope.hpp"
#include "origin_boarding_intermediate_pause_support_internal.hpp"
namespace apsis_drift::detail {
struct BoardingIntermediatePauseEnvelopeLimits {
  BoardingIntermediatePauseLimits pause;
  std::size_t sole_records{2}, source_vertices{8}, minima{2},
      weighted_operations{3}, gap_operations{1}, output_bytes{1024};
};
struct BoardingIntermediatePauseEnvelopeMathLimits {
  std::size_t sole_records{2}, source_vertices{8}, minima{2},
      weighted_operations{3}, gap_operations{1};
};
struct BoardingIntermediatePauseEnvelopeMathInput {
  std::array<BoardingFootSiteScalarBounds, 2> sole_max_z;
  std::array<std::array<BoardingFootSiteScalarBounds, 4>, 2> source_vertices_z;
  BoardingFootSiteScalarBounds com_z;
};
struct BoardingIntermediatePauseEnvelopeMath {
  std::array<BoardingIntermediatePauseEnvelopeSite, 2> sites;
  BoardingFootSiteScalarBounds com_z, weighted_upper_z, gap_z;
  BoardingIntermediatePauseEnvelopeCounters work;
  std::array<bool, 3> weighted_evaluated{};
  bool gap_evaluated{};
  std::optional<BoardingIntermediatePauseEnvelopeRefusal> first_refusal;
  BoardingIntermediatePauseEnvelopeState state{};
  bool arithmetic_supported{}, envelope_assessed{}, necessary_refuted{};
  static constexpr bool source_qualified{false}, body_qualified{false},
      fixture_qualified{false}, contact_qualified{false}, load_qualified{false},
      support_qualified{false}, self_qualified{false},
      material_qualified{false}, world_qualified{false}, route_qualified{false},
      seat_qualified{false}, actor_qualified{false}, save_qualified{false},
      dynamics_qualified{false}, first_flight_qualified{false};
};
[[nodiscard]] auto intermediate_pause_envelope_bounded(
    const OriginBoardingIntermediatePauseSupport&, bool reverse,
    BoardingIntermediatePauseEnvelopeLimits = {})
    -> std::expected<BoardingIntermediatePauseEnvelopeDiagnostic, std::string>;
[[nodiscard]] auto intermediate_pause_envelope_math(
    const BoardingIntermediatePauseEnvelopeMathInput&,
    BoardingIntermediatePauseEnvelopeMathLimits = {})
    -> BoardingIntermediatePauseEnvelopeMath;
} // namespace apsis_drift::detail
