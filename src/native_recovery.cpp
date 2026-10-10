#include "apsis_drift/native_flight_session.hpp"

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
  auto source = recovery_document();
  if (!source) return std::unexpected{source.error()};
  auto result = record_freedom_loss(*source, cause, craft, tick, checksum);
  if (!result) return std::unexpected{result.error().detail};
  recovery_ = std::move(result->recovery);
  if (recovery_pending()) {
    cancel_port_approach();
    cancel_surface_maneuver();
  }
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
  *this = std::move(*candidate);
  return {};
}
} // namespace apsis_drift
