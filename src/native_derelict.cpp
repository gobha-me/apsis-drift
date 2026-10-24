#include "apsis_drift/freedom_derelict.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/save_file.hpp"

#include <cmath>
#include <limits>

namespace apsis_drift {
namespace {
auto separation(RigidVector3 a, RigidVector3 b) -> double {
  return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}
auto observation(const RigidBodyState& body, StarterCraftId craft)
    -> FreedomDerelictObservation {
  return {craft, body.tick, body.position_metres,
          body.linear_velocity_metres_per_second};
}
} // namespace

auto observe_derelict_relative_motion(const RigidBodyState& player,
                                      const RigidBodyState& hull)
    -> std::optional<DerelictRelativeMotion> {
  const auto& frame = player.frame;
  if (frame.kind != RigidFrameKind::planet_relative_inertial ||
      frame.system.value == 0 || !frame.planet || frame.planet->value == 0 ||
      frame.station || frame.body || frame != hull.frame ||
      player.tick != hull.tick ||
      player.tick == std::numeric_limits<SimulationTick>::max())
    return std::nullopt;
  const auto bounded = [](RigidVector3 vector, double limit) {
    return std::isfinite(vector.x) && std::isfinite(vector.y) &&
           std::isfinite(vector.z) && std::abs(vector.x) <= limit &&
           std::abs(vector.y) <= limit && std::abs(vector.z) <= limit;
  };
  for (const auto* body : {&player, &hull}) {
    if (!bounded(body->position_metres, kRigidBodyMaximumPositionMetres) ||
        !bounded(body->linear_velocity_metres_per_second,
                 kRigidBodyMaximumVelocityMetresPerSecond))
      return std::nullopt;
  }
  const RigidVector3 offset{hull.position_metres.x - player.position_metres.x,
                            hull.position_metres.y - player.position_metres.y,
                            hull.position_metres.z - player.position_metres.z};
  const auto range = std::hypot(offset.x, offset.y, offset.z);
  if (range == 0) return std::nullopt;
  const RigidVector3 direction{offset.x / range, offset.y / range,
                               offset.z / range};
  const RigidVector3 velocity{hull.linear_velocity_metres_per_second.x -
                                  player.linear_velocity_metres_per_second.x,
                              hull.linear_velocity_metres_per_second.y -
                                  player.linear_velocity_metres_per_second.y,
                              hull.linear_velocity_metres_per_second.z -
                                  player.linear_velocity_metres_per_second.z};
  const auto radial = direction.x * velocity.x + direction.y * velocity.y +
                      direction.z * velocity.z;
  return DerelictRelativeMotion{-radial,
                                std::hypot(velocity.x - radial * direction.x,
                                           velocity.y - radial * direction.y,
                                           velocity.z - radial * direction.z)};
}

auto NativeFreedomFlightSession::derelict_view() const -> NativeDerelictView {
  NativeDerelictView view;
  view.selected = document_.origin.derelicts && knowledge_ &&
                  knowledge_->derelicts.has_value();
  if (!view.selected) {
    view.note = "This journey has no derelict population selected";
    return view;
  }
  const auto& records = *knowledge_->derelicts;
  view.known = records.contact.has_value();
  view.inspected = records.inspection.has_value();
  if (view.inspected)
    view.history =
        "Wayfarer hull: no active power or thrust. Weathered service "
        "markings remain; the registry plate is unreadable. "
        "Exterior observation only; ownership and crew fate unknown.";
  if (view.known) {
    const auto descriptor =
        generate_freedom_derelict(document_.origin.recipe.universe_seed);
    if (!descriptor) {
      view.note = "Recorded contact is unavailable";
      return view;
    }
    view.id = descriptor->id;
  }
  view.note = "Scan local space during ordinary flight above the atmosphere";
  if (recovery_pending() || moon_ || minor_ || actor_ || surface_walker_ ||
      (docking_ && docking_->attached) || (surface_ && surface_->landed) ||
      (travel_ && travel_->phase != FreedomJumpPhase::idle))
    return view;
  const auto body = relative_state();
  const auto observed = observe();
  if (!body || !observed ||
      !std::isfinite(observed->atmosphere.altitude_metres) ||
      observed->atmosphere.altitude_metres <=
          observed->atmosphere.space_boundary_altitude_metres)
    return view;
  view.can_scan = !view.known;
  view.note = "Ready to scan local signals";
  const auto descriptor =
      generate_freedom_derelict(document_.origin.recipe.universe_seed);
  if (!descriptor || body->frame.system != descriptor->system ||
      body->frame.planet != descriptor->planet)
    return view;
  const auto hull = resolve_freedom_derelict(*descriptor, body->tick);
  if (!hull || !view.known) return view;
  view.local = true;
  view.hull = *hull;
  view.motion = observe_derelict_relative_motion(*body, *hull);
  view.distance_metres =
      separation(body->position_metres, hull->position_metres);
  view.relative_speed_metres_per_second =
      separation(body->linear_velocity_metres_per_second,
                 hull->linear_velocity_metres_per_second);
  view.can_inspect =
      view.distance_metres >= kFreedomDerelictInspectionMinimumMetres &&
      view.distance_metres <= kFreedomDerelictInspectionMaximumMetres &&
      view.relative_speed_metres_per_second <=
          kFreedomDerelictInspectionMaximumSpeedMetresPerSecond;
  view.note = "Unpowered Wayfarer contact: inspect from 100–500 m, matching "
              "relative speed to 20 m/s or less. Exterior only; hull collision "
              "and boarding are not implemented.";
  return view;
}

auto NativeFreedomFlightSession::scan_derelict()
    -> std::expected<void, std::string> {
  const auto view = derelict_view();
  if (!view.can_scan) return std::unexpected{view.note};
  const auto body = relative_state();
  const auto descriptor =
      generate_freedom_derelict(document_.origin.recipe.universe_seed);
  if (!body || !descriptor || body->frame.system != descriptor->system ||
      body->frame.planet != descriptor->planet)
    return std::unexpected{"No local derelict signal acquired"};
  const auto hull = resolve_freedom_derelict(*descriptor, body->tick);
  if (!hull || separation(body->position_metres, hull->position_metres) >
                   kFreedomDerelictContactRangeMetres)
    return std::unexpected{"No local derelict signal acquired"};
  auto candidate = std::make_unique<NativeFreedomFlightSession>(*this);
  candidate->knowledge_->derelicts->contact =
      observation(*body, document_.origin.state.craft);
  const auto saved = candidate->player_save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (const auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(*candidate);
  return {};
}

auto NativeFreedomFlightSession::inspect_derelict()
    -> std::expected<void, std::string> {
  const auto view = derelict_view();
  if (!view.can_inspect) return std::unexpected{view.note};
  if (view.inspected) return {};
  const auto body = relative_state();
  if (!body) return std::unexpected{body.error()};
  auto candidate = std::make_unique<NativeFreedomFlightSession>(*this);
  candidate->knowledge_->derelicts->inspection =
      observation(*body, document_.origin.state.craft);
  const auto saved = candidate->player_save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (const auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(*candidate);
  return {};
}
} // namespace apsis_drift
