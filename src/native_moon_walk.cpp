#include "apsis_drift/moon_surface_walker.hpp"
#include "apsis_drift/native_flight_session.hpp"

#include <utility>

namespace apsis_drift {
auto NativeFreedomFlightSession::begin_moon_surface_walk()
    -> std::expected<void, std::string> {
  if (!moon_ || recovery_pending() || actor_ || surface_walker_ ||
      (travel_ && travel_->phase != FreedomJumpPhase::idle) ||
      !starting_assembly_ || !boarding_ ||
      boarding_->phase != FreedomBoardingPhase::seated || !surface_ ||
      !surface_->moon_landed ||
      surface_maneuver_.kind != NativeSurfaceManeuverKind::off)
    return std::unexpected{
        "Leave the seated Wayfarer only after a supported moon landing"};
  if (!knowledge_ || !resources_)
    return std::unexpected{"Moon walking requires explicit native resources "
                           "and environment ownership"};
  auto terrain = MoonSurfaceWalkTerrain::create(*moon_, *surface_->moon_landed,
                                                document_.flight.tick);
  if (!terrain) return std::unexpected{terrain.error()};
  const auto actor = terrain->entry();
  if (!actor) return std::unexpected{actor.error()};
  auto candidate = *this;
  candidate.surface_walk_selected_ = true;
  candidate.surface_walker_ = *actor;
  candidate.surface_walk_blockage_.reset();
  candidate.gear_clearance_.reset();
  candidate.moon_walk_terrain_ =
      std::make_shared<MoonSurfaceWalkTerrain>(std::move(*terrain));
  if (const auto saved = candidate.surface_walk_document(); !saved)
    return std::unexpected{saved.error()};
  *this = std::move(candidate);
  return {};
}
auto NativeFreedomFlightSession::moon_surface_return_access() const
    -> std::expected<PlanetSurfaceReturnAccess, std::string> {
  if (!moon_ || recovery_pending() || !surface_walker_ || !moon_walk_terrain_ ||
      !surface_ || !surface_->moon_landed ||
      moon_walk_terrain_->anchor() != *surface_->moon_landed)
    return std::unexpected{
        "Return access requires its active landed moon walker"};
  return moon_walk_terrain_->return_access(*surface_walker_);
}
auto NativeFreedomFlightSession::return_from_moon_surface_walk()
    -> std::expected<void, std::string> {
  const auto access = moon_surface_return_access();
  if (!access) return std::unexpected{access.error()};
  if (!access->within_reach)
    return std::unexpected{
        "Move within reach of the Wayfarer hatch ground access"};
  surface_walker_.reset();
  surface_walk_blockage_.reset();
  moon_walk_terrain_.reset();
  return {};
}
auto NativeFreedomFlightSession::advance_moon_surface_walk(
    const OriginWalkControls& controls, SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  if (!moon_ || recovery_pending() || !surface_walker_ || !moon_walk_terrain_ ||
      !surface_ || !surface_->moon_landed ||
      moon_walk_terrain_->anchor() != *surface_->moon_landed ||
      (travel_ && travel_->phase != FreedomJumpPhase::idle))
    return std::unexpected{
        "Moon walking requires its supported actor and shared voyage clock"};
  const auto movement =
      moon_walk_terrain_->advance(*surface_walker_, controls, step);
  if (!movement) return std::unexpected{movement.error()};
  auto candidate = *this;
  // The ordinary constrained craft tick owns pose, ledger and condition time;
  // the supported outside actor supplies the sole movement for that same tick.
  const auto result = candidate.advance_moon_supported_tick({}, step);
  if (!result) return std::unexpected{result.error()};
  candidate.surface_walker_ = movement->actor;
  candidate.surface_walk_blockage_ =
      movement->status == PlanetSurfaceWalkStatus::supported
          ? std::nullopt
          : std::optional{movement->status};
  *this = std::move(candidate);
  return result;
}
} // namespace apsis_drift
