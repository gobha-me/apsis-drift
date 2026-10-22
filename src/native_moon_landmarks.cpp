#include "apsis_drift/moon_surface_walker.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace apsis_drift {
namespace {
auto supported_moon_landmarks(const std::optional<FreedomKnowledge>& knowledge)
    -> bool {
  return knowledge &&
         (knowledge->recipe.version == kFreedomObservedKnowledgeVersion ||
          knowledge->recipe.version == kFreedomTravelKnowledgeVersion ||
          freedom_knowledge_uses_network(knowledge->recipe.version));
}
auto rotated_moon_landmark(RigidOrientation q, MoonFixedPositionMetres v)
    -> RigidVector3 {
  const double n = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
  const double k = 2 * (q.x * v.x + q.y * v.y + q.z * v.z) / n;
  const double s = (q.w * q.w - q.x * q.x - q.y * q.y - q.z * q.z) / n;
  return {s * v.x + k * q.x + 2 * q.w * (q.y * v.z - q.z * v.y) / n,
          s * v.y + k * q.y + 2 * q.w * (q.z * v.x - q.x * v.z) / n,
          s * v.z + k * q.z + 2 * q.w * (q.x * v.y - q.y * v.x) / n};
}
} // namespace
auto NativeFreedomFlightSession::enable_moon_surface_landmarks()
    -> std::expected<void, std::string> {
  if (!moon_ || recovery_pending() || !supported_moon_landmarks(knowledge_))
    return std::unexpected{"Moon exposures require a supported active journey"};
  if (knowledge_->moon_surface_landmarks)
    return std::unexpected{"Moon exposures are already enabled"};
  auto candidate = *this;
  candidate.knowledge_->moon_surface_landmarks =
      FreedomMoonSurfaceLandmarkState{};
  const auto saved = candidate.player_save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return {};
}
auto NativeFreedomFlightSession::moon_surface_landmarks_view() const
    -> std::expected<FreedomMoonSurfaceLandmarksView, std::string> {
  FreedomMoonSurfaceLandmarksView view;
  if (!moon_ || !document_.moon || !supported_moon_landmarks(knowledge_))
    return view;
  view.selected = knowledge_->moon_surface_landmarks.has_value();
  view.can_enable = !view.selected && !recovery_pending();
  if (!view.selected) return view;
  view.inspection_count =
      knowledge_->moon_surface_landmarks->inspections.size();
  const auto body = relative_state();
  if (!body) return std::unexpected{body.error()};
  if (body->frame !=
      RigidCoordinateFrame{RigidFrameKind::body_relative_inertial,
                           system_.catalog.id,
                           {},
                           {},
                           moon_->body})
    return std::unexpected{"Moon exposure observer owner differs"};
  MoonFixedPositionMetres observer;
  bool supported_foot = false;
  if (surface_walker_) {
    if (!moon_walk_terrain_ || !surface_ || !surface_->moon_landed ||
        moon_walk_terrain_->anchor() != *surface_->moon_landed ||
        surface_walker_->frame !=
            RigidCoordinateFrame{RigidFrameKind::body_fixed,
                                 system_.catalog.id,
                                 {},
                                 {},
                                 moon_->body})
      return std::unexpected{"Moon exposure outside actor owner differs"};
    if (auto valid = moon_walk_terrain_->validate(*surface_walker_); !valid)
      return std::unexpected{valid.error()};
    const auto& p = surface_walker_->foot_position_metres;
    observer = {p.x, p.y, p.z};
    supported_foot = true;
  } else {
    const auto fixed = reframe_rigid_body(
        RigidBodyWorldContext{*moon_}, *body,
        {{RigidFrameKind::body_fixed, system_.catalog.id, {}, {}, moon_->body},
         document_.flight.tick},
        *moon_);
    if (!fixed)
      return std::unexpected{"Moon exposure observer transform unavailable"};
    const auto& p = fixed->position_metres;
    observer = {p.x, p.y, p.z};
  }
  const double radius = std::hypot(observer.x, observer.y, observer.z);
  if (!std::isfinite(radius))
    return std::unexpected{"Moon exposure observer is non-finite"};
  if (std::abs(radius - moon_->radius_metres) > 32768) return view;
  if (!moon_surface_landmark_field_ ||
      moon_surface_landmark_field_->owner() != *moon_) {
    auto field = MoonSurfaceLandmarkField::create(
        *moon_, document_.moon->terrain_recipe);
    if (!field) return std::unexpected{"Moon exposure terrain owner refused"};
    moon_surface_landmark_field_ =
        std::make_shared<MoonSurfaceLandmarkField>(std::move(*field));
  }
  const auto sites = moon_surface_landmark_field_->nearby(observer);
  if (!sites) return std::unexpected{"Moon exposure terrain query refused"};
  const auto rotation =
      resolve_physical_moon_rotation(*moon_, document_.flight.tick);
  if (!rotation) return std::unexpected{"Moon exposure rotation unavailable"};
  for (const auto& site : *sites) {
    const auto target =
        rotated_moon_landmark(rotation->fixed_to_system, site.position_metres);
    const RigidVector3 relative{target.x - body->position_metres.x,
                                target.y - body->position_metres.y,
                                target.z - body->position_metres.z};
    const double distance = std::hypot(site.position_metres.x - observer.x,
                                       site.position_metres.y - observer.y,
                                       site.position_metres.z - observer.z);
    if (!std::isfinite(relative.x) || !std::isfinite(relative.y) ||
        !std::isfinite(relative.z) || !std::isfinite(distance))
      return std::unexpected{"Moon exposure projection is non-finite"};
    const bool inspected = std::ranges::any_of(
        knowledge_->moon_surface_landmarks->inspections, [&](const auto& item) {
          return item.target == BodyTarget{system_.catalog.id, moon_->body} &&
                 item.site == site.identity.id;
        });
    view.sites.push_back(
        {site, relative, distance,
         supported_foot && distance <= site.identity.radius_metres + 8,
         supported_foot && distance <= 8 && !recovery_pending(), inspected});
  }
  return view;
}
auto NativeFreedomFlightSession::inspect_moon_surface_landmark(
    SurfaceLandmarkId id) -> std::expected<void, std::string> {
  if (!moon_ || !document_.moon || recovery_pending() ||
      !supported_moon_landmarks(knowledge_) ||
      !knowledge_->moon_surface_landmarks || !surface_walker_ ||
      !moon_walk_terrain_ || !surface_ || !surface_->moon_landed ||
      moon_walk_terrain_->anchor() != *surface_->moon_landed)
    return std::unexpected{
        "Inspecting an exposure requires the supported outside Moon actor"};
  if (auto valid = moon_walk_terrain_->validate(*surface_walker_); !valid)
    return std::unexpected{valid.error()};
  const auto view = moon_surface_landmarks_view();
  if (!view) return std::unexpected{view.error()};
  const auto found = std::ranges::find_if(view->sites, [&](const auto& item) {
    return item.site.identity.id == id;
  });
  if (found == view->sites.end() || !found->can_inspect)
    return std::unexpected{
        "Move within 8 metres of the exposure centre to inspect"};
  if (found->inspected) return {};
  const auto planet_count =
      knowledge_->surface_landmarks
          ? knowledge_->surface_landmarks->inspections.size()
          : 0;
  if (view->inspection_count + planet_count >=
      kMaximumFreedomSurfaceLandmarkInspections)
    return std::unexpected{"The local exposure record is full"};
  auto candidate = *this;
  auto& records = candidate.knowledge_->moon_surface_landmarks->inspections;
  records.push_back({{system_.catalog.id, moon_->body},
                     found->site.identity.cell,
                     id,
                     document_.moon->terrain_recipe,
                     document_.flight.tick});
  std::ranges::sort(records, [](const auto& a, const auto& b) {
    return std::tuple{a.target.system.value, a.target.body.version,
                      a.target.body.kind, a.target.body.value, a.site.value} <
           std::tuple{b.target.system.value, b.target.body.version,
                      b.target.body.kind, b.target.body.value, b.site.value};
  });
  const auto saved = candidate.player_save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return {};
}
} // namespace apsis_drift
