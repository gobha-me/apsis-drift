#include "apsis_drift/native_flight_session.hpp"

namespace apsis_drift {
auto NativeFreedomFlightSession::surface_walk_document() const
    -> std::expected<FreedomSurfaceWalkSaveDocument, std::string> {
  auto voyage = recovery_document();
  if (!voyage) return std::unexpected{voyage.error()};
  FreedomSurfaceWalkSaveDocument result{std::move(*voyage), 1, surface_walker_};
  if (auto valid = validate_freedom_surface_walk_document(result); !valid)
    return std::unexpected{valid.error().detail};
  return result;
}
auto NativeFreedomFlightSession::begin_surface_walk()
    -> std::expected<void, std::string> {
  if (recovery_pending() ||
      (travel_ && travel_->phase != FreedomJumpPhase::idle) || actor_ ||
      surface_walker_ || !starting_assembly_ || !boarding_ ||
      boarding_->phase != FreedomBoardingPhase::seated || !surface_ ||
      !surface_->landed ||
      surface_maneuver_.kind != NativeSurfaceManeuverKind::off)
    return std::unexpected{
        "Leave the seated Wayfarer only after a supported stable landing"};
  if (!knowledge_ || !resources_)
    return std::unexpected{"Surface walking requires explicit native resources "
                           "and environment ownership"};
  auto terrain = PlanetSurfaceWalkTerrain::create(
      system_, rotation_, *surface_->landed, document_.flight.tick);
  if (!terrain) return std::unexpected{terrain.error()};
  const auto actor = terrain->entry();
  if (!actor) return std::unexpected{actor.error()};
  auto candidate = *this;
  candidate.surface_walk_selected_ = true;
  candidate.surface_walker_ = *actor;
  candidate.surface_walk_terrain_ =
      std::make_shared<PlanetSurfaceWalkTerrain>(std::move(*terrain));
  if (auto saved = candidate.surface_walk_document(); !saved)
    return std::unexpected{saved.error()};
  *this = std::move(candidate);
  return {};
}
auto NativeFreedomFlightSession::return_from_surface_walk()
    -> std::expected<void, std::string> {
  if (recovery_pending() || !surface_walker_ || !surface_walk_terrain_)
    return std::unexpected{"Return requires an active surface actor"};
  const auto near = surface_walk_terrain_->near_entry(*surface_walker_);
  if (!near) return std::unexpected{near.error()};
  if (!*near)
    return std::unexpected{
        "Move within reach of the Wayfarer hatch ground access"};
  surface_walker_.reset();
  surface_walk_terrain_.reset();
  return {};
}
auto NativeFreedomFlightSession::advance_surface_walk(
    const OriginWalkControls& controls, SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  if (recovery_pending() || !surface_walker_ || !surface_walk_terrain_ ||
      !surface_ || !surface_->landed ||
      surface_walk_terrain_->anchor() != *surface_->landed)
    return std::unexpected{
        "Surface walking requires its unchanged landed craft"};
  const auto movement =
      surface_walk_terrain_->advance(*surface_walker_, controls, step);
  if (!movement) return std::unexpected{movement.error()};
  auto candidate = *this;
  const auto flight = candidate.advance_craft_tick({}, step);
  if (!flight) return std::unexpected{flight.error()};
  candidate.surface_walker_ = movement->actor;
  *this = std::move(candidate);
  return *flight;
}
} // namespace apsis_drift
