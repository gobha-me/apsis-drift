#include "apsis_drift/native_flight_session.hpp"

#include <memory>

namespace apsis_drift {
auto NativeFreedomFlightSession::recovery_document() const
    -> std::expected<FreedomRecoverySaveDocument, std::string> {
  if (!resources_ || !knowledge_ || !starting_assembly_)
    return std::unexpected{"Recovery requires the selected native starter, "
                           "resources and knowledge"};
  FreedomRecoveryVoyage voyage = FreedomKnowledgeSaveDocument{
      {surface_document(), *resources_}, *knowledge_};
  if (travel_) {
    auto document = travel_document();
    if (!document) return std::unexpected{document.error()};
    voyage = std::move(*document);
  }
  FreedomRecoveryState state;
  state.checkpoint =
      FreedomSafeStationCheckpoint{1, {document_.origin.state.station, 1}, 0};
  if (recovery_) state = *recovery_;
  FreedomRecoverySaveDocument result{std::move(voyage), std::move(state)};
  if (auto v = validate_freedom_recovery_document(result); !v)
    return std::unexpected{v.error().detail};
  return result;
}
auto NativeFreedomFlightSession::record_loss(FreedomLossCause cause,
                                             StarterCraftId craft,
                                             SimulationTick tick,
                                             std::uint64_t checksum)
    -> std::expected<void, std::string> {
  // Retire unfinished retrieval before freezing its immutable loss source.
  // A refused loss discards this candidate and preserves the original activity.
  auto candidate = std::make_unique<NativeFreedomFlightSession>(*this);
  candidate->cancel_derelict_recovery_unfinished();
  auto source = candidate->recovery_document();
  if (!source) return std::unexpected{source.error()};
  auto result = record_freedom_loss(*source, cause, craft, tick, checksum);
  if (!result) return std::unexpected{result.error().detail};
  candidate->recovery_ = std::move(result->recovery);
  if (candidate->recovery_pending()) {
    candidate->gear_clearance_.reset();
    if (candidate->condition_) {
      candidate->condition_->tick = candidate->document_.flight.tick;
      candidate->retired_condition_ = candidate->condition_;
    }
    candidate->cancel_port_approach();
    candidate->cancel_surface_maneuver();
  }
  if (candidate->condition_) {
    auto selected = candidate->condition_document();
    if (!selected) return std::unexpected{selected.error()};
    if (auto encoded = encode_freedom_condition_document_json(*selected);
        !encoded)
      return std::unexpected{encoded.error().detail};
  }
  *this = std::move(*candidate);
  return {};
}
auto NativeFreedomFlightSession::complete_recovery()
    -> std::expected<void, std::string> {
  if (!recovery_) return std::unexpected{"No recorded loss to continue"};
  auto source = recovery_document();
  if (!source) return std::unexpected{source.error()};
  if (!recovery_->pending) {
    if (recovery_->latest) return {};
    return std::unexpected{"No recorded loss to continue"};
  }
  auto replacement = complete_freedom_recovery(*source);
  if (!replacement) return std::unexpected{replacement.error().detail};
  const auto& flight = recovery_flight(replacement->voyage);
  auto hydrated = hydrate_freedom_flight_document(flight);
  if (!hydrated) return std::unexpected{hydrated.error().detail};
  auto home =
      find_local_system_planet(hydrated->system, *flight.flight.frame.planet);
  if (!home) return std::unexpected{"Recovery home is unavailable"};
  auto candidate = open({NativeStartup::Mode::freedom, std::move(*replacement),
                         (*home)->descriptor, source_save_});
  if (!candidate) return std::unexpected{candidate.error()};
  // Player replacement never replaces or restocks the independent responder.
  candidate->assistance_ = assistance_;
  candidate->assistance_world_ = assistance_world_;
  candidate->assistance_propulsion_ = assistance_propulsion_;
  candidate->surface_walk_selected_ = surface_walk_selected_;
  if (condition_) {
    auto pristine = make_craft_condition(candidate->document_);
    if (!pristine) return std::unexpected{pristine.error()};
    candidate->condition_ = *pristine;
    auto retired = *condition_;
    retired.tick = document_.flight.tick;
    candidate->retired_condition_ = retired;
    if (auto valid = candidate->condition_document(); !valid)
      return std::unexpected{valid.error()};
    else if (auto encoded = encode_freedom_condition_document_json(*valid);
             !encoded)
      return std::unexpected{encoded.error().detail};
  }
  *this = std::move(*candidate);
  return {};
}
} // namespace apsis_drift
