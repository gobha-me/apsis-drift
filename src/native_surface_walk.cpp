#include "apsis_drift/native_flight_session.hpp"

#include <cmath>

namespace apsis_drift {
namespace {
auto marker_rotated(RigidOrientation q, PlanetFixedPositionMetres v)
    -> RigidVector3 {
  const double n = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  const double k = 2 * (q.x * v.x + q.y * v.y + q.z * v.z) / n;
  const double s = (q.w * q.w - q.x * q.x - q.y * q.y - q.z * q.z) / n;
  return {s * v.x + k * q.x + 2 * q.w * (q.y * v.z - q.z * v.y) / n,
          s * v.y + k * q.y + 2 * q.w * (q.z * v.x - q.x * v.z) / n,
          s * v.z + k * q.z + 2 * q.w * (q.x * v.y - q.y * v.x) / n};
}
} // namespace

auto NativeFreedomFlightSession::mark_surface_marker()
    -> std::expected<void, std::string> {
  return store_surface_marker(false);
}
auto NativeFreedomFlightSession::replace_surface_marker()
    -> std::expected<void, std::string> {
  return store_surface_marker(true);
}
auto NativeFreedomFlightSession::store_surface_marker(bool replace)
    -> std::expected<void, std::string> {
  if (moon_) return store_moon_surface_marker(replace);
  if (recovery_pending() || !knowledge_ || !surface_walker_ ||
      !surface_walk_terrain_ || !surface_ || !surface_->landed ||
      surface_walk_terrain_->anchor() != *surface_->landed)
    return std::unexpected{
        "Recording a marker requires the supported outside actor"};
  const bool present = knowledge_->surface_marker &&
                       knowledge_->surface_marker->point.has_value();
  if (present != replace)
    return std::unexpected{
        replace ? "No surface marker to replace"
                : "Use Replace marker to change the saved point"};
  if (auto valid = surface_walk_terrain_->validate(*surface_walker_); !valid)
    return std::unexpected{valid.error()};
  const auto& actor = *surface_walker_;
  if (actor.frame.kind != RigidFrameKind::planet_fixed ||
      actor.frame.system != system_.catalog.id || !actor.frame.planet ||
      actor.frame.station)
    return std::unexpected{"Surface marker actor owner is unavailable"};
  auto candidate = *this;
  candidate.knowledge_->surface_marker = FreedomSurfaceMarkerState{
      kFreedomSurfaceMarkerVersion,
      FreedomSurfaceMarkerPoint{actor.frame.system,
                                *actor.frame.planet,
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
auto NativeFreedomFlightSession::clear_surface_marker()
    -> std::expected<void, std::string> {
  if (recovery_pending())
    return std::unexpected{
        "Continue the recorded loss before clearing the marker"};
  if (!knowledge_ || !knowledge_->surface_marker ||
      !knowledge_->surface_marker->point)
    return std::unexpected{"No surface marker to clear"};
  auto candidate = *this;
  candidate.knowledge_->surface_marker->point.reset();
  const auto saved = candidate.player_save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return {};
}
auto NativeFreedomFlightSession::surface_marker_view() const
    -> std::expected<FreedomSurfaceMarkerView, std::string> {
  FreedomSurfaceMarkerView view;
  if (!knowledge_ || !knowledge_->surface_marker) return view;
  const auto now = document_.flight.tick;
  if (!validate_freedom_knowledge(*knowledge_, now))
    return std::unexpected{"Surface marker knowledge owner is invalid"};
  view.selected = true;
  view.point = knowledge_->surface_marker->point;
  if (!view.point) return view;
  if (std::holds_alternative<FreedomMoonSurfaceMarkerPoint>(*view.point))
    return moon_surface_marker_view(std::move(view));
  const auto& point = std::get<FreedomSurfaceMarkerPoint>(*view.point);
  const auto name = [&](KnowledgeSubject subject)
      -> std::expected<std::optional<std::string>, std::string> {
    const auto known = query_freedom_knowledge(
        *knowledge_, subject, KnowledgeFact::identity_type, now);
    if (!known) return std::unexpected{"Surface marker identity query refused"};
    if (*known)
      if (const auto* identity = std::get_if<KnownIdentity>(&(*known)->value))
        return identity->name;
    return std::optional<std::string>{};
  };
  auto system_name =
      name({KnowledgeSubjectKind::system, point.system, point.system.value});
  auto body_name =
      name({KnowledgeSubjectKind::planet, point.system, point.planet.value});
  if (!system_name) return std::unexpected{system_name.error()};
  if (!body_name) return std::unexpected{body_name.error()};
  view.known_system_name = std::move(*system_name);
  view.known_body_name = std::move(*body_name);
  if (point.system != system_.catalog.id ||
      point.planet != rotation_.rotation.planet)
    return view;
  const auto body = relative_state();
  if (!body) return view;
  if (body->frame.kind != RigidFrameKind::planet_relative_inertial ||
      body->frame.system != point.system || body->frame.planet != point.planet)
    return view;
  const auto rotation = resolve_planet_rotation(system_, rotation_, now);
  if (!rotation) return view;
  const auto target =
      marker_rotated(rotation->geometry.fixed_to_system, point.position);
  const RigidVector3 relative{target.x - body->position_metres.x,
                              target.y - body->position_metres.y,
                              target.z - body->position_metres.z};
  double distance = std::hypot(relative.x, relative.y, relative.z);
  if (surface_walker_) {
    if (surface_walker_->frame !=
        RigidCoordinateFrame{
            RigidFrameKind::planet_fixed, point.system, point.planet, {}})
      return std::unexpected{"Surface marker observer owner differs"};
    const auto& foot = surface_walker_->foot_position_metres;
    distance = std::hypot(point.position.x - foot.x, point.position.y - foot.y,
                          point.position.z - foot.z);
  }
  if (!std::isfinite(relative.x) || !std::isfinite(relative.y) ||
      !std::isfinite(relative.z) || !std::isfinite(distance))
    return std::unexpected{"Surface marker bearing is unavailable"};
  view.current_body = true;
  view.target_craft_relative_metres = relative;
  view.straight_line_distance_metres = distance;
  return view;
}

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
  if (moon_) return begin_moon_surface_walk();
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
  candidate.gear_clearance_.reset();
  candidate.surface_walker_ = *actor;
  candidate.surface_walk_blockage_.reset();
  candidate.surface_walk_terrain_ =
      std::make_shared<PlanetSurfaceWalkTerrain>(std::move(*terrain));
  if (auto saved = candidate.surface_walk_document(); !saved)
    return std::unexpected{saved.error()};
  *this = std::move(candidate);
  return {};
}
auto NativeFreedomFlightSession::surface_return_access() const
    -> std::expected<PlanetSurfaceReturnAccess, std::string> {
  if (moon_) return moon_surface_return_access();
  if (recovery_pending() || !surface_walker_ || !surface_walk_terrain_ ||
      !surface_ || !surface_->landed ||
      surface_walk_terrain_->anchor() != *surface_->landed)
    return std::unexpected{"Return access requires its active landed walker"};
  return surface_walk_terrain_->return_access(*surface_walker_);
}
auto NativeFreedomFlightSession::return_from_surface_walk()
    -> std::expected<void, std::string> {
  if (moon_) return return_from_moon_surface_walk();
  if (recovery_pending() || !surface_walker_ || !surface_walk_terrain_)
    return std::unexpected{"Return requires an active surface actor"};
  const auto near = surface_walk_terrain_->near_entry(*surface_walker_);
  if (!near) return std::unexpected{near.error()};
  if (!*near)
    return std::unexpected{
        "Move within reach of the Wayfarer hatch ground access"};
  surface_walker_.reset();
  surface_walk_blockage_.reset();
  surface_walk_terrain_.reset();
  return {};
}
auto NativeFreedomFlightSession::advance_player_surface_walk(
    const OriginWalkControls& controls, SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  if (moon_) return advance_moon_surface_walk(controls, step);
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
  candidate.surface_walk_blockage_ = movement->status;
  if (movement->status == PlanetSurfaceWalkStatus::supported)
    candidate.surface_walk_blockage_.reset();
  *this = std::move(candidate);
  return *flight;
}
} // namespace apsis_drift
