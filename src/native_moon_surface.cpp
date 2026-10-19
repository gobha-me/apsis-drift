#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace apsis_drift {
auto NativeFreedomFlightSession::set_moon_landing_gear(bool deployed)
    -> std::expected<void, std::string> {
  if (!moon_ || !document_.moon || recovery_pending() || actor_ ||
      surface_walker_ || (docking_ && docking_->attached) ||
      (travel_ && travel_->phase != FreedomJumpPhase::idle))
    return std::unexpected{"Moon gear requires its free seated craft"};
  if (surface_ && surface_->moon_landed && !deployed)
    return std::unexpected{"Lift off before stowing moon landing gear"};
  auto candidate = *this;
  if (!candidate.surface_) candidate.surface_ = FreedomSurfaceState{};
  candidate.surface_->gear_deployed = deployed;
  candidate.gear_clearance_.reset();
  candidate.cancel_port_approach();
  candidate.cancel_surface_maneuver();
  if (const auto valid =
          validate_freedom_surface_document(candidate.surface_document());
      !valid)
    return std::unexpected{valid.error().detail};
  *this = std::move(candidate);
  return {};
}

auto NativeFreedomFlightSession::commit_moon_touchdown(std::uint64_t checksum)
    -> std::expected<void, std::string> {
  if (!moon_ || !document_.moon || recovery_pending() || actor_ ||
      surface_walker_ || (docking_ && docking_->attached) || !surface_ ||
      !surface_->gear_deployed || surface_->landed || surface_->moon_landed ||
      (travel_ && travel_->phase != FreedomJumpPhase::idle))
    return std::unexpected{
        "Moon touchdown requires deployed gear and its free seated craft"};
  if (condition_ && !condition_allows_landing(*condition_))
    return std::unexpected{
        "Landing structure is damaged; safe touchdown unavailable"};
  const auto anchor = prepare_moon_landed_craft(
      *moon_, document_.flight, true, checksum, document_.moon->terrain_recipe);
  if (!anchor)
    return std::unexpected{
        "Moon touchdown refused by terrain, hull or motion checks"};
  const auto pose =
      resolve_moon_landed_craft(*moon_, *anchor, document_.flight.tick);
  if (!pose) return std::unexpected{"Moon touchdown pose refused"};
  auto candidate = *this;
  candidate.document_.flight = *pose;
  candidate.surface_->moon_landed = *anchor;
  candidate.gear_clearance_.reset();
  candidate.cancel_port_approach();
  candidate.cancel_surface_maneuver();
  candidate.surface_maneuver_.note = "Landed on certified dry moon terrain";
  if (const auto valid =
          validate_freedom_surface_document(candidate.surface_document());
      !valid)
    return std::unexpected{valid.error().detail};
  // Existing planetary knowledge cannot record a moon as its parent. The
  // retained ledger stays intact; typed lunar survey facts are a later scope.
  *this = std::move(candidate);
  return {};
}

auto NativeFreedomFlightSession::release_moon_surface()
    -> std::expected<void, std::string> {
  if (!moon_ || !surface_ || !surface_->moon_landed || recovery_pending() ||
      actor_ || surface_walker_ || (docking_ && docking_->attached) ||
      (travel_ && travel_->phase != FreedomJumpPhase::idle))
    return std::unexpected{"Moon liftoff requires its landed seated craft"};
  const auto pose = release_moon_landed_craft(*moon_, *surface_->moon_landed,
                                              document_.flight.tick);
  if (!pose)
    return std::unexpected{"Moon liftoff terrain or thrust authority refused"};
  auto candidate = *this;
  candidate.document_.flight = *pose;
  candidate.surface_->moon_landed.reset();
  candidate.gear_clearance_.reset();
  candidate.cancel_surface_maneuver();
  candidate.surface_maneuver_.note =
      "Moon liftoff released; apply up thrusters";
  if (const auto valid =
          validate_freedom_surface_document(candidate.surface_document());
      !valid)
    return std::unexpected{valid.error().detail};
  *this = std::move(candidate);
  return {};
}

auto NativeFreedomFlightSession::advance_moon_supported_tick(
    const NativeFlightControls& controls, SimulationSeconds step)
    -> std::expected<NativeFlightStep, std::string> {
  if (!moon_ || !surface_ || !surface_->moon_landed ||
      !std::isfinite(step.count()) || step != kSimulationStep ||
      document_.flight.tick >= std::numeric_limits<SimulationTick>::max() - 2)
    return std::unexpected{"Invalid moon constrained clock or owner"};
  if (controls.positive_translation != RigidVector3{} ||
      controls.negative_translation != RigidVector3{} ||
      controls.positive_rotation != RigidVector3{} ||
      controls.negative_rotation != RigidVector3{})
    return std::unexpected{"Select Liftoff before firing propulsion"};
  const auto pose = resolve_moon_landed_craft(*moon_, *surface_->moon_landed,
                                              document_.flight.tick + 1);
  if (!pose) return std::unexpected{"Moon constrained pose advance refused"};
  auto candidate = *this;
  candidate.document_.flight = *pose;
  candidate.document_.origin.state.tick = pose->tick;
  if (resources_) {
    if (!validate_freedom_resources(*resources_, document_))
      return std::unexpected{
          "Moon constrained resource owner or clock refused"};
    const auto next =
        advance_freedom_resource_tick(*resources_, document_.flight.tick, 0);
    if (!next)
      return std::unexpected{"Moon constrained resource clock refused"};
    candidate.resources_ = *next;
  }
  if (candidate.condition_) candidate.condition_->tick = pose->tick;
  if (const auto valid =
          hydrate_freedom_moon_flight_document(candidate.document_);
      !valid)
    return std::unexpected{valid.error().detail};
  *this = std::move(candidate);
  return NativeFlightStep{};
}

auto NativeFreedomFlightSession::try_moon_surface_contact() -> void {
  if (!moon_ || !surface_ || !surface_->gear_deployed ||
      surface_->moon_landed || actor_ || surface_walker_)
    return;
  const auto fixed = reframe_rigid_body(
      RigidBodyWorldContext{*moon_}, document_.flight,
      {{RigidFrameKind::body_fixed, system_.catalog.id, {}, {}, moon_->body},
       document_.flight.tick},
      *moon_);
  if (!fixed) return;
  const auto& p = fixed->position_metres;
  const auto& v = fixed->linear_velocity_metres_per_second;
  // Releasing support never supplies an impulse. Actual outward thrust must
  // be able to leave the gear stroke without auto-capturing on every tick.
  if ((p.x * v.x + p.y * v.y) + p.z * v.z > 0) return;
  const auto checksum = rigid_body_state_checksum(RigidBodyWorldContext{*moon_},
                                                  document_.flight);
  if (checksum) (void)commit_moon_touchdown(*checksum);
}
} // namespace apsis_drift
