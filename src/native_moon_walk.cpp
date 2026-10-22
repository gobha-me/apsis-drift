#include "apsis_drift/moon_surface_walker.hpp"
#include "apsis_drift/native_flight_session.hpp"

#include <cmath>
#include <utility>

namespace apsis_drift {
namespace {
auto moon_marker_rotated(RigidOrientation q, MoonFixedPositionMetres v)
    -> RigidVector3 {
  const double n = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  const double k = 2 * (q.x * v.x + q.y * v.y + q.z * v.z) / n;
  const double s = (q.w * q.w - q.x * q.x - q.y * q.y - q.z * q.z) / n;
  return {s * v.x + k * q.x + 2 * q.w * (q.y * v.z - q.z * v.y) / n,
          s * v.y + k * q.y + 2 * q.w * (q.z * v.x - q.x * v.z) / n,
          s * v.z + k * q.z + 2 * q.w * (q.x * v.y - q.y * v.x) / n};
}
} // namespace

auto NativeFreedomFlightSession::store_moon_surface_marker(bool replace)
    -> std::expected<void, std::string> {
  if (!moon_ || recovery_pending() || !knowledge_ || !surface_walker_ ||
      !moon_walk_terrain_ || !surface_ || !surface_->moon_landed ||
      moon_walk_terrain_->anchor() != *surface_->moon_landed)
    return std::unexpected{
        "Recording a marker requires the supported outside moon actor"};
  const bool present = knowledge_->surface_marker &&
                       knowledge_->surface_marker->point.has_value();
  if (present != replace)
    return std::unexpected{
        replace ? "No surface marker to replace"
                : "Use Replace marker to change the saved point"};
  if (auto valid = moon_walk_terrain_->validate(*surface_walker_); !valid)
    return std::unexpected{valid.error()};
  const auto& actor = *surface_walker_;
  if (actor.frame !=
      RigidCoordinateFrame{
          RigidFrameKind::body_fixed, system_.catalog.id, {}, {}, moon_->body})
    return std::unexpected{"Surface marker moon actor owner is unavailable"};
  auto candidate = *this;
  candidate.knowledge_->surface_marker = FreedomSurfaceMarkerState{
      kFreedomMoonSurfaceMarkerVersion,
      FreedomMoonSurfaceMarkerPoint{actor.frame.system,
                                    *actor.frame.body,
                                    {actor.foot_position_metres.x,
                                     actor.foot_position_metres.y,
                                     actor.foot_position_metres.z},
                                    document_.flight.tick}};
  const auto saved = candidate.player_save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return {};
}

auto NativeFreedomFlightSession::moon_surface_marker_view(
    FreedomSurfaceMarkerView view) const
    -> std::expected<FreedomSurfaceMarkerView, std::string> {
  const auto& point = std::get<FreedomMoonSurfaceMarkerPoint>(*view.point);
  // The ledger currently has no Moon identity facts. Retain an unresolved body
  // name even though the physical owner is authenticated for navigation.
  const auto known = query_freedom_knowledge(
      *knowledge_,
      {KnowledgeSubjectKind::system, point.system, point.system.value},
      KnowledgeFact::identity_type, document_.flight.tick);
  if (!known) return std::unexpected{"Surface marker identity query refused"};
  if (*known)
    if (const auto* identity = std::get_if<KnownIdentity>(&(*known)->value))
      view.known_system_name = identity->name;
  if (!moon_ || point.system != system_.catalog.id || point.body != moon_->body)
    return view;
  const auto body = relative_state();
  if (!body) return view;
  if (body->frame !=
      RigidCoordinateFrame{RigidFrameKind::body_relative_inertial,
                           point.system,
                           {},
                           {},
                           point.body})
    return view;
  const auto rotation =
      resolve_physical_moon_rotation(*moon_, document_.flight.tick);
  if (!rotation)
    return std::unexpected{"Surface marker moon rotation is unavailable"};
  const auto target =
      moon_marker_rotated(rotation->fixed_to_system, point.position);
  const RigidVector3 relative{target.x - body->position_metres.x,
                              target.y - body->position_metres.y,
                              target.z - body->position_metres.z};
  double distance = std::hypot(relative.x, relative.y, relative.z);
  if (surface_walker_) {
    if (!moon_walk_terrain_ || !surface_ || !surface_->moon_landed ||
        moon_walk_terrain_->anchor() != *surface_->moon_landed ||
        surface_walker_->frame !=
            RigidCoordinateFrame{
                RigidFrameKind::body_fixed, point.system, {}, {}, point.body})
      return std::unexpected{"Surface marker moon observer owner differs"};
    if (const auto valid = moon_walk_terrain_->validate(*surface_walker_);
        !valid)
      return std::unexpected{valid.error()};
    const auto& foot = surface_walker_->foot_position_metres;
    distance = std::hypot(point.position.x - foot.x, point.position.y - foot.y,
                          point.position.z - foot.z);
  }
  if (!std::isfinite(relative.x) || !std::isfinite(relative.y) ||
      !std::isfinite(relative.z) || !std::isfinite(distance))
    return std::unexpected{"Surface marker moon bearing is unavailable"};
  view.current_body = true;
  view.target_craft_relative_metres = relative;
  view.straight_line_distance_metres = distance;
  return view;
}

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
