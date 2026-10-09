#include "apsis_drift/freedom_jump_targeting.hpp"

#include "apsis_drift/intersystem_jump.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>

namespace apsis_drift {
namespace {
using Error = FreedomJumpTargetingError;
using V = RigidVector3;
auto norm(V v) -> double {
  return std::sqrt((v.x * v.x + v.y * v.y) + v.z * v.z);
}
auto finite(V v) -> bool {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
auto difference(SystemPositionMetres a, SystemPositionMetres b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto world(Seed seed, SystemId id)
    -> std::expected<PhysicalLocalSystem, Error> {
  const auto ids = generate_first_intersystem_identities(seed);
  if (id != ids.origin_system && id != ids.target_system)
    return std::unexpected{Error::invalid_owner};
  auto result = id == ids.origin_system
                    ? generate_physical_origin_system(seed, 2)
                    : generate_physical_local_system(ids.target_system_seed, 2);
  if (!result) return std::unexpected{Error::invalid_owner};
  return std::move(*result);
}
auto alignment(const RigidBodyState& source, V leg)
    -> IntersystemArrivalAssessment {
  // Active body-to-frame rotation of forward -Z, in the common inertial axes.
  const auto& q = source.orientation;
  const V forward{-2 * (q.x * q.z + q.w * q.y), -2 * (q.y * q.z - q.w * q.x),
                  2 * (q.x * q.x + q.y * q.y) - 1};
  const double cosine = std::clamp(
      ((forward.x * leg.x + forward.y * leg.y) + forward.z * leg.z) / norm(leg),
      -1.0, 1.0);
  const auto heading = static_cast<std::int32_t>(std::floor(
      std::acos(cosine) * 180000.0 / std::numbers::pi_v<double> + .5));
  // Explicit drift reference, not a virtual keyboard trim or fuel channel.
  const auto velocity = static_cast<std::int32_t>(std::floor(
      std::min(norm(source.linear_velocity_metres_per_second) / 60000.0, 1.0) *
          10000.0 +
      .5));
  const auto quality = heading <= kAlignedHeadingErrorMillidegrees &&
                               velocity <= kAlignedVelocityErrorBasisPoints
                           ? IntersystemArrivalQuality::aligned
                       : heading <= kOffsetHeadingErrorMillidegrees &&
                               velocity <= kOffsetVelocityErrorBasisPoints
                           ? IntersystemArrivalQuality::offset
                           : IntersystemArrivalQuality::opposed;
  return {heading, velocity, quality};
}
auto hash_word(std::uint64_t& hash, std::uint64_t value) -> void {
  for (unsigned byte = 0; byte < 8; ++byte) {
    hash ^= (value >> (byte * 8)) & 255U;
    hash *= 1099511628211ULL;
  }
}
auto sample_seed(const FreedomJumpPreview& p) -> Seed {
  const auto& r = p.request;
  std::uint64_t ordinal{14695981039346656037ULL};
  for (auto value :
       {std::uint64_t{r.version}, r.attempt, r.craft.value,
        r.source.frame.system.value, r.destination.value, r.source.tick,
        std::uint64_t{static_cast<unsigned>(r.profile)},
        std::uint64_t{r.source.craft.version}, r.source.craft.id.value})
    hash_word(ordinal, value);
  // Bind actual geometry too; changing the draft pose before commitment is a
  // different request, never permission to change an already frozen payload.
  for (double value :
       {r.source.position_metres.x, r.source.position_metres.y,
        r.source.position_metres.z, r.source.orientation.w,
        r.source.orientation.x, r.source.orientation.y, r.source.orientation.z,
        r.source.linear_velocity_metres_per_second.x,
        r.source.linear_velocity_metres_per_second.y,
        r.source.linear_velocity_metres_per_second.z,
        r.source.angular_velocity_radians_per_second.x,
        r.source.angular_velocity_radians_per_second.y,
        r.source.angular_velocity_radians_per_second.z})
    hash_word(ordinal, std::bit_cast<std::uint64_t>(value));
  hash_word(ordinal, r.source.frame.planet->value);
  return derive_seed(r.universe_seed, SeedDomain::jump_arrival, ordinal);
}
} // namespace
auto assess_freedom_jump_distance(double metres,
                                  IntersystemArrivalQuality quality)
    -> std::expected<FreedomJumpDistanceAssessment, FreedomJumpTargetingError> {
  if (!std::isfinite(metres) || metres <= 0)
    return std::unexpected{Error::invalid_geometry};
  std::uint32_t multiplier{};
  switch (quality) {
    case IntersystemArrivalQuality::aligned: multiplier = 1; break;
    case IntersystemArrivalQuality::offset: multiplier = 10; break;
    case IntersystemArrivalQuality::opposed: multiplier = 100; break;
    default: return std::unexpected{Error::invalid_profile};
  }
  if (metres > static_cast<double>(kFreedomJumpRatedMetres))
    return FreedomJumpDistanceAssessment{
        FreedomJumpReach::beyond_rated, {}, {}};
  const auto factor = static_cast<std::uint32_t>(std::ceil(
      metres / static_cast<double>(kFreedomJumpRatedMetres) * 1000000.0));
  const auto radius = 1000U + (factor + 999U) / 1000U;
  return FreedomJumpDistanceAssessment{FreedomJumpReach::rated, factor,
                                       radius * multiplier};
}
auto assess_freedom_arrival_volume(const PhysicalLocalSystem& system,
                                   SimulationTick tick,
                                   SystemPositionMetres centre, double radius)
    -> std::expected<FreedomArrivalVolumeAssessment,
                     FreedomJumpTargetingError> {
  const V p{centre.x, centre.y, centre.z};
  if (!validate_local_system(system) || system.generator_version != 2 ||
      system.ephemeris_version != 2)
    return std::unexpected{Error::invalid_owner};
  if (tick == std::numeric_limits<SimulationTick>::max())
    return std::unexpected{Error::tick_overflow};
  if (!finite(p) || norm(p) > kRigidBodyMaximumPositionMetres ||
      !std::isfinite(radius) || radius < 0 || radius > 1000000000.0)
    return std::unexpected{Error::invalid_geometry};
  FreedomArrivalVolumeAssessment result;
  result.intersects_star =
      norm(p) <=
      radius +
          static_cast<double>(system.catalog.star.radius_kilometres) * 1000;
  for (const auto& body : system.catalog.planets) {
    const auto position =
        resolve_planet_ephemeris(system, body.descriptor.id, {tick, 0});
    if (!position) return std::unexpected{Error::invalid_geometry};
    if (norm(difference(centre, position->position)) <=
        radius + body.descriptor.radius.value * 1000.0)
      result.intersecting_planets.push_back(body.descriptor.id);
  }
  return result;
}
auto preview_freedom_jump(const FreedomJumpRequest& r)
    -> std::expected<FreedomJumpPreview, FreedomJumpTargetingError> {
  if (r.version != kFreedomJumpTargetingVersion)
    return std::unexpected{Error::unsupported_policy};
  if (!r.attempt) return std::unexpected{Error::invalid_attempt};
  if (r.profile != IntersystemRuleProfile::assisted &&
      r.profile != IntersystemRuleProfile::pilot)
    return std::unexpected{Error::invalid_profile};
  if (r.craft != make_freedom_new_game_document(r.universe_seed).state.craft)
    return std::unexpected{Error::invalid_owner};
  const auto route = generate_first_universe_route(r.universe_seed);
  const auto current = r.source.frame.system;
  if ((current != route.origin && current != route.destination) ||
      r.destination == current ||
      (r.destination != route.origin && r.destination != route.destination))
    return std::unexpected{Error::invalid_destination};
  if (r.source.tick >
      std::numeric_limits<SimulationTick>::max() - kJumpTransitTicks - 1)
    return std::unexpected{Error::tick_overflow};
  auto from = world(r.universe_seed, current);
  auto to = world(r.universe_seed, r.destination);
  if (!from || !to) return std::unexpected{Error::invalid_owner};
  if (!validate_rigid_body_state(RigidBodyWorldContext{*from}, r.source) ||
      r.source.craft !=
          CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion} ||
      r.source.frame.kind != RigidFrameKind::planet_relative_inertial ||
      norm(r.source.position_metres) >
          static_cast<double>(kLocalSystemBoundaryMetres))
    return std::unexpected{Error::invalid_source};
  const auto source_planet = resolve_planet_ephemeris(
      *from, *r.source.frame.planet, {r.source.tick, 0});
  if (!source_planet) return std::unexpected{Error::invalid_source};
  FreedomJumpPreview p;
  p.request = r;
  p.arrival_tick = r.source.tick + kJumpTransitTicks;
  p.source_position = {source_planet->position.x + r.source.position_metres.x,
                       source_planet->position.y + r.source.position_metres.y,
                       source_planet->position.z + r.source.position_metres.z};
  if (r.destination == route.origin) {
    const auto station = generate_origin_station(r.universe_seed);
    const auto ephemeris =
        resolve_origin_station_ephemeris(*to, station, {p.arrival_tick, 0});
    if (!ephemeris) return std::unexpected{Error::invalid_geometry};
    const V radial{ephemeris->host_relative_position.x,
                   ephemeris->host_relative_position.y,
                   ephemeris->host_relative_position.z};
    const double scale = kAssistedOriginArrivalStandoffMetres / norm(radial);
    p.reference_planet = station.orbit.host_planet;
    p.nominal_arrival = {ephemeris->position.x + radial.x * scale,
                         ephemeris->position.y + radial.y * scale,
                         ephemeris->position.z + radial.z * scale};
    p.arrival_velocity = {
        ephemeris->velocity.x + r.source.linear_velocity_metres_per_second.x,
        ephemeris->velocity.y + r.source.linear_velocity_metres_per_second.y,
        ephemeris->velocity.z + r.source.linear_velocity_metres_per_second.z};
  } else {
    const auto& body = to->catalog.planets.front();
    const auto ephemeris =
        resolve_planet_ephemeris(*to, body.descriptor.id, {p.arrival_tick, 0});
    if (!ephemeris) return std::unexpected{Error::invalid_geometry};
    p.reference_planet = body.descriptor.id;
    p.nominal_arrival = ephemeris->position;
    p.nominal_arrival.z += body.descriptor.radius.value * 1000.0 *
                           kAssistedTargetArrivalStandoffRadii;
    p.arrival_velocity = {
        ephemeris->velocity.x + r.source.linear_velocity_metres_per_second.x,
        ephemeris->velocity.y + r.source.linear_velocity_metres_per_second.y,
        ephemeris->velocity.z + r.source.linear_velocity_metres_per_second.z};
  }
  if (!finite(
          {p.nominal_arrival.x, p.nominal_arrival.y, p.nominal_arrival.z}) ||
      norm({p.nominal_arrival.x, p.nominal_arrival.y, p.nominal_arrival.z}) >
          kRigidBodyMaximumPositionMetres ||
      !finite(
          {p.arrival_velocity.x, p.arrival_velocity.y, p.arrival_velocity.z}) ||
      norm({p.arrival_velocity.x, p.arrival_velocity.y, p.arrival_velocity.z}) >
          kRigidBodyMaximumVelocityMetresPerSecond)
    return std::unexpected{Error::invalid_geometry};
  const auto from_anchor = current == route.origin ? route.origin_position
                                                   : route.destination_position;
  const auto to_anchor = r.destination == route.origin
                             ? route.origin_position
                             : route.destination_position;
  const V leg{static_cast<double>(to_anchor.x - from_anchor.x) +
                  (p.nominal_arrival.x - p.source_position.x),
              static_cast<double>(to_anchor.y - from_anchor.y) +
                  (p.nominal_arrival.y - p.source_position.y),
              static_cast<double>(to_anchor.z - from_anchor.z) +
                  (p.nominal_arrival.z - p.source_position.z)};
  p.distance_metres = norm(leg);
  if (!finite(leg) || p.distance_metres <= 0)
    return std::unexpected{Error::invalid_geometry};
  p.alignment = alignment(r.source, leg);
  const auto grade = r.profile == IntersystemRuleProfile::assisted
                         ? IntersystemArrivalQuality::aligned
                         : p.alignment.quality;
  auto distance = assess_freedom_jump_distance(p.distance_metres, grade);
  if (!distance) return std::unexpected{distance.error()};
  p.distance = *distance;
  if (p.distance.envelope_radius_metres) {
    auto volume =
        assess_freedom_arrival_volume(*to, p.arrival_tick, p.nominal_arrival,
                                      *p.distance.envelope_radius_metres);
    if (!volume) return std::unexpected{volume.error()};
    p.volume = std::move(*volume);
  }
  return p;
}
auto freeze_freedom_jump_arrival(const FreedomJumpRequest& r)
    -> std::expected<FrozenFreedomJumpArrival, FreedomJumpTargetingError> {
  auto p = preview_freedom_jump(r);
  if (!p) return std::unexpected{p.error()};
  if (!p->distance.envelope_radius_metres)
    return std::unexpected{Error::beyond_qualified_policy};
  const auto seed = sample_seed(*p);
  const double scale = *p->distance.envelope_radius_metres / (1.75 * 32768.0);
  const auto component = [&](unsigned shift) {
    return (static_cast<std::int32_t>((seed.value >> shift) & 65535U) - 32768) *
           scale;
  };
  const SystemPositionMetres point{p->nominal_arrival.x + component(0),
                                   p->nominal_arrival.y + component(16),
                                   p->nominal_arrival.z + component(32)};
  auto destination = world(r.universe_seed, r.destination);
  if (!destination) return std::unexpected{destination.error()};
  auto assessment =
      assess_freedom_arrival_volume(*destination, p->arrival_tick, point, 0);
  if (!assessment) return std::unexpected{assessment.error()};
  return FrozenFreedomJumpArrival{std::move(*p), seed, point,
                                  std::move(*assessment)};
}
auto validate_frozen_freedom_jump_arrival(const FrozenFreedomJumpArrival& value)
    -> std::expected<void, FreedomJumpTargetingError> {
  const auto expected = freeze_freedom_jump_arrival(value.preview.request);
  if (!expected || *expected != value)
    return std::unexpected{Error::invalid_frozen_solution};
  return {};
}
} // namespace apsis_drift
