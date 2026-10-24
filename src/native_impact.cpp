#include "apsis_drift/native_flight_session.hpp"

#include <type_traits>
#include <utility>

namespace apsis_drift {
auto NativeFreedomFlightSession::condition_document() const
    -> std::expected<FreedomConditionSaveDocument, std::string> {
  if (!condition_)
    return std::unexpected{"Terrain impact consequences are not selected"};
  // Project the existing sole voyage owner, without recursively wrapping it.
  auto plain = *this;
  plain.condition_.reset();
  const auto projected = plain.player_save_document();
  if (!projected) return std::unexpected{projected.error()};
  auto voyage = std::visit(
      [](const auto& value)
          -> std::expected<FreedomConditionVoyage, std::string> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, FreedomKnowledgeSaveDocument> ||
                      std::is_same_v<T, FreedomTravelSaveDocument> ||
                      std::is_same_v<T, FreedomRecoverySaveDocument> ||
                      std::is_same_v<T, FreedomSurfaceWalkSaveDocument>)
          return FreedomConditionVoyage{value};
        else
          return std::unexpected{
              "Impact consequences require the complete native voyage"};
      },
      *projected);
  if (!voyage) return std::unexpected{voyage.error()};
  auto condition = *condition_;
  // Walking, attached clocks and jump transit share the physical voyage clock;
  // they cannot independently generate impact events or clear contact gates.
  condition.tick = document_.flight.tick;
  FreedomConditionSaveDocument result{1, std::move(*voyage), condition,
                                      retired_condition_};
  if (auto valid = validate_freedom_condition_document(result); !valid)
    return std::unexpected{valid.error().detail};
  return result;
}

auto NativeFreedomFlightSession::impact_consequences_available() const
    -> std::expected<void, std::string> {
  if (minor_)
    return std::unexpected{"Minor inspection has no terrain-impact mode"};
  if (document_.flight.frame.kind == RigidFrameKind::system_inertial) {
    if (document_.model.frame_policy != kFreedomReleaseSystemFramePolicy)
      return std::unexpected{"Unsupported system-flight impact selection"};
    auto candidate = *this;
    const auto relative = relative_freedom_flight_state(document_, system_);
    if (!relative)
      return std::unexpected{"Impact frame assessment refused: " +
                             relative.error().detail};
    candidate.document_.flight = *relative;
    return candidate.impact_consequences_available();
  }
  if (condition_)
    return std::unexpected{"Terrain impacts are already selected"};
  if (recovery_pending() ||
      (travel_ && travel_->phase != FreedomJumpPhase::idle))
    return std::unexpected{
        "Select terrain impacts outside pending recovery or active jumps"};
  if (!starting_assembly_ || !resources_ || !knowledge_ ||
      document_.model.physical_catalog != 2 ||
      document_.model.physical_ephemeris != 2 ||
      document_.flight.craft !=
          CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion})
    return std::unexpected{
        "Terrain impacts require the supported native Wayfarer voyage"};
  return {};
}

auto NativeFreedomFlightSession::enable_impact_consequences()
    -> std::expected<void, std::string> {
  if (minor_)
    return std::unexpected{"Minor inspection has no terrain-impact mode"};
  if (condition_) return {};
  if (document_.flight.frame.kind == RigidFrameKind::system_inertial) {
    if (document_.model.frame_policy != kFreedomReleaseSystemFramePolicy)
      return std::unexpected{"Unsupported system-flight impact selection"};
    auto candidate = *this;
    const auto relative = relative_freedom_flight_state(document_, system_);
    if (!relative)
      return std::unexpected{"Impact frame handoff refused: " +
                             relative.error().detail};
    candidate.document_.flight = *relative;
    if (auto enabled = candidate.enable_impact_consequences(); !enabled)
      return enabled;
    const auto saved = candidate.player_save_document();
    if (!saved) return std::unexpected{saved.error()};
    if (auto encoded = encode_native_save_document_json(*saved); !encoded)
      return std::unexpected{encoded.error().detail};
    *this = std::move(candidate);
    return {};
  }
  if (auto available = impact_consequences_available(); !available)
    return available;
  auto condition = make_craft_condition(document_);
  if (!condition) return std::unexpected{condition.error()};
  auto candidate = *this;
  candidate.condition_ = *condition;
  if (auto selected = candidate.condition_document(); !selected)
    return std::unexpected{selected.error()};
  else if (auto encoded = encode_freedom_condition_document_json(*selected);
           !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return {};
}

auto NativeFreedomFlightSession::advance_impact_tick(
    const NativeFlightControls& controls, SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  // Stage the ordinary authoritative physics, gross fuel transaction and
  // knowledge together. Nothing below can modify the live session on refusal.
  auto candidate = *this;
  candidate.condition_.reset();
  auto result = candidate.advance_craft_tick(controls, step);
  if (!result) return std::unexpected{result.error()};
  auto source_condition = *condition_;
  source_condition.tick = document_.flight.tick;
  candidate.condition_ = source_condition;

  // Existing whole-pad touchdown assessment has priority over the prototype
  // collision response. A gentle qualified landing never becomes hull damage.
  if (candidate.surface_ && candidate.surface_->gear_deployed &&
      candidate.surface_maneuver_.kind != NativeSurfaceManeuverKind::liftoff)
    candidate.try_surface_contact();
  if (candidate.surface_ && candidate.surface_->landed) {
    candidate.condition_->tick = candidate.document_.flight.tick;
  } else {
    const auto sweep = assess_terrain_impact(
        system_, rotation_, document_, candidate.document_,
        surface_ && surface_->gear_deployed, document_.model.terrain_policy);
    if (!sweep) return std::unexpected{sweep.error()};
    if (sweep->classification == TerrainImpactClass::unknown)
      return std::unexpected{
          "Experimental impact coverage is unresolved; flight tick retained"};
    const auto reduction = reduce_craft_condition(source_condition, document_,
                                                  candidate.document_, *sweep);
    if (!reduction) return std::unexpected{reduction.error()};
    candidate.condition_ = reduction->condition;
    if (sweep->witness && sweep->witness->closing_speed_metres_per_second > 0) {
      auto response = respond_terrain_impact(
          system_, rotation_, document_, candidate.document_, *sweep->witness);
      if (!response) return std::unexpected{response.error()};
      candidate.document_.flight = *response;
      candidate.document_.model.hold = {};
      candidate.cancel_port_approach();
      candidate.cancel_surface_maneuver();
      candidate.surface_maneuver_.note =
          reduction->catastrophic
              ? "Terrain impact: craft lost; recovery awaits explicit Continue"
          : reduction->event_applied
              ? "Terrain impact: localized craft damage recorded"
              : "Terrain contact: inward motion stopped";
    }
    if (reduction->catastrophic) {
      // Frozen retired knowledge must not be changed by post-impact
      // acquisition.
      candidate.cancel_derelict_recovery_unfinished();
      auto recovery = candidate.recovery_document();
      if (!recovery) return std::unexpected{recovery.error()};
      const auto checksum = freedom_recovery_source_checksum(recovery->voyage);
      if (!checksum) return std::unexpected{checksum.error().detail};
      if (auto lost =
              candidate.record_loss(FreedomLossCause::irrecoverable_destruction,
                                    candidate.document_.origin.state.craft,
                                    candidate.document_.flight.tick, *checksum);
          !lost)
        return std::unexpected{lost.error()};
      // The bounded loss history and its condition remain authenticated
      // together through pending loss and the subsequent replacement.
      candidate.retired_condition_ = candidate.condition_;
    }
  }
  const auto observed = candidate.observe();
  if (!observed) return std::unexpected{observed.error()};
  result->actuation.after = observed->atmosphere;
  result->actuation.observation_after = observed->orbit;
  if (auto saved = candidate.condition_document(); !saved)
    return std::unexpected{saved.error()};
  else if (auto encoded = encode_freedom_condition_document_json(*saved);
           !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return result;
}
} // namespace apsis_drift
