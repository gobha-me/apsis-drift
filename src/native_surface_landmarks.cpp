#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace apsis_drift {
namespace {
auto rotated_landmark(RigidOrientation q, PlanetFixedPositionMetres v)
    -> RigidVector3 {
  const double n = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  const double k = 2 * (q.x * v.x + q.y * v.y + q.z * v.z) / n;
  const double s = (q.w * q.w - q.x * q.x - q.y * q.y - q.z * q.z) / n;
  return {s * v.x + k * q.x + 2 * q.w * (q.y * v.z - q.z * v.y) / n,
          s * v.y + k * q.y + 2 * q.w * (q.z * v.x - q.x * v.z) / n,
          s * v.z + k * q.z + 2 * q.w * (q.x * v.y - q.y * v.x) / n};
}
auto supports_landmarks(const std::optional<FreedomKnowledge>& knowledge)
    -> bool {
  return knowledge &&
         (knowledge->recipe.version == kFreedomObservedKnowledgeVersion ||
          knowledge->recipe.version == kFreedomTravelKnowledgeVersion ||
          knowledge->recipe.version == kFreedomNetworkKnowledgeVersion);
}
} // namespace

auto NativeFreedomFlightSession::enable_surface_landmarks()
    -> std::expected<void, std::string> {
  if (minor_)
    return std::unexpected{"Minor inspection has no surface landmarks"};
  if (moon_) return enable_moon_surface_landmarks();
  if (recovery_pending() || !supports_landmarks(knowledge_))
    return std::unexpected{
        "Local landmarks require a supported active journey"};
  if (knowledge_->surface_landmarks)
    return std::unexpected{"Local landmarks are already enabled"};
  auto candidate = *this;
  candidate.knowledge_->surface_landmarks = FreedomSurfaceLandmarkState{};
  const auto saved = candidate.save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return {};
}

auto NativeFreedomFlightSession::surface_landmarks_view() const
    -> std::expected<FreedomSurfaceLandmarksView, std::string> {
  FreedomSurfaceLandmarksView view;
  const bool supported = supports_landmarks(knowledge_);
  if (moon_ || minor_)
    return view; // Planetary site history stays retained on its own body.
  view.selected = supported && knowledge_->surface_landmarks.has_value();
  view.can_enable = supported && !view.selected && !recovery_pending();
  if (!view.selected) return view;
  view.inspection_count = knowledge_->surface_landmarks->inspections.size();
  const auto body = relative_state();
  if (!body) return std::unexpected{body.error()};
  const auto planet = rotation_.rotation.planet;
  if (body->frame.kind != RigidFrameKind::planet_relative_inertial ||
      body->frame.system != system_.catalog.id || body->frame.planet != planet)
    return std::unexpected{"Local landmark observer owner differs"};
  const auto rotation =
      resolve_planet_rotation(system_, rotation_, document_.flight.tick);
  if (!rotation) return std::unexpected{"Local landmark rotation unavailable"};
  PlanetFixedPositionMetres observer;
  if (surface_walker_) {
    if (surface_walker_->frame !=
        RigidCoordinateFrame{
            RigidFrameKind::planet_fixed, system_.catalog.id, planet, {}})
      return std::unexpected{"Local landmark foot owner differs"};
    const auto& p = surface_walker_->foot_position_metres;
    observer = {p.x, p.y, p.z};
  } else {
    const auto fixed = reframe_rigid_body(
        RigidBodyWorldContext{system_}, *body,
        {{RigidFrameKind::planet_fixed, system_.catalog.id, planet, {}},
         document_.flight.tick},
        rotation_);
    if (!fixed)
      return std::unexpected{"Local landmark observer transform unavailable"};
    const auto& p = fixed->position_metres;
    observer = {p.x, p.y, p.z};
  }
  const auto physical =
      std::ranges::find_if(system_.catalog.planets, [&](const auto& p) {
        return p.descriptor.id == planet;
      });
  if (physical == system_.catalog.planets.end())
    return std::unexpected{"Local landmark body is unavailable"};
  const double radius =
      static_cast<double>(physical->descriptor.radius.value) * 1000;
  const auto distance_from_centre =
      std::hypot(observer.x, observer.y, observer.z);
  if (!std::isfinite(distance_from_centre))
    return std::unexpected{"Local landmark observer is non-finite"};
  // Normal interplanetary flight has no local sites and needs no terrain work.
  // This shell exceeds the unchanged terrain relief bound, including chord
  // loss.
  if (std::abs(distance_from_centre - radius) > 32768) return view;
  const auto policy = document_.model.terrain_policy;
  if (!surface_landmark_field_ ||
      surface_landmark_system_ != system_.catalog.id ||
      surface_landmark_planet_ != planet ||
      surface_landmark_policy_ != policy) {
    auto field = SurfaceLandmarkField::create(system_, body_id(planet), policy);
    if (!field) return std::unexpected{"Local landmark terrain owner refused"};
    surface_landmark_field_ =
        std::make_shared<SurfaceLandmarkField>(std::move(*field));
    surface_landmark_system_ = system_.catalog.id;
    surface_landmark_planet_ = planet;
    surface_landmark_policy_ = policy;
  }
  const auto sites = surface_landmark_field_->nearby(observer);
  if (!sites) return std::unexpected{"Local landmark terrain query refused"};
  for (const auto& site : *sites) {
    const auto target = rotated_landmark(rotation->geometry.fixed_to_system,
                                         site.position_metres);
    const RigidVector3 relative{target.x - body->position_metres.x,
                                target.y - body->position_metres.y,
                                target.z - body->position_metres.z};
    const double distance = std::hypot(site.position_metres.x - observer.x,
                                       site.position_metres.y - observer.y,
                                       site.position_metres.z - observer.z);
    if (!std::isfinite(relative.x) || !std::isfinite(relative.y) ||
        !std::isfinite(relative.z) || !std::isfinite(distance))
      return std::unexpected{"Local landmark projection is non-finite"};
    const bool inspected = std::ranges::any_of(
        knowledge_->surface_landmarks->inspections, [&](const auto& item) {
          return item.system == system_.catalog.id && item.planet == planet &&
                 item.site == site.identity.id;
        });
    view.sites.push_back(
        {site, relative, distance,
         surface_walker_.has_value() &&
             distance <= site.identity.radius_metres + 8,
         surface_walker_.has_value() && distance <= 8 && !recovery_pending(),
         inspected});
  }
  return view;
}

auto NativeFreedomFlightSession::inspect_surface_landmark(SurfaceLandmarkId id)
    -> std::expected<void, std::string> {
  if (moon_) return inspect_moon_surface_landmark(id);
  if (recovery_pending() || !supports_landmarks(knowledge_) ||
      !knowledge_->surface_landmarks || !surface_walker_ ||
      !surface_walk_terrain_ || !surface_ || !surface_->landed ||
      surface_walk_terrain_->anchor() != *surface_->landed)
    return std::unexpected{
        "Inspecting an exposure requires the supported outside actor"};
  if (auto valid = surface_walk_terrain_->validate(*surface_walker_); !valid)
    return std::unexpected{valid.error()};
  const auto view = surface_landmarks_view();
  if (!view) return std::unexpected{view.error()};
  const auto found = std::ranges::find_if(view->sites, [&](const auto& item) {
    return item.site.identity.id == id;
  });
  if (found == view->sites.end() || !found->can_inspect)
    return std::unexpected{
        "Move within 8 metres of the exposure centre to inspect"};
  if (found->inspected) return {};
  const auto moon_count =
      knowledge_->moon_surface_landmarks
          ? knowledge_->moon_surface_landmarks->inspections.size()
          : 0;
  if (view->inspection_count + moon_count >=
      kMaximumFreedomSurfaceLandmarkInspections)
    return std::unexpected{"The local exposure record is full"};
  auto candidate = *this;
  auto& inspections = candidate.knowledge_->surface_landmarks->inspections;
  inspections.push_back(
      {system_.catalog.id, rotation_.rotation.planet, found->site.identity.cell,
       id, document_.model.terrain_policy, document_.flight.tick});
  std::ranges::sort(inspections, [](const auto& a, const auto& b) {
    return std::tie(a.system.value, a.planet.value, a.site.value) <
           std::tie(b.system.value, b.planet.value, b.site.value);
  });
  const auto saved = candidate.save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return {};
}
} // namespace apsis_drift
