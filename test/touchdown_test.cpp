#include "apsis_drift/touchdown.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
using namespace apsis_drift;
int failures{};
auto check(bool condition, std::string_view message) -> void {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}
auto near(double actual, double expected, double tolerance,
          std::string_view message) -> void {
  check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
        message);
}
constexpr auto bit(TouchdownMargin margin) -> std::uint32_t {
  return static_cast<std::uint32_t>(margin);
}
auto has(const TouchdownAssessment& value, TouchdownMargin margin) -> bool {
  return (value.failed_margins & bit(margin)) != 0;
}
auto same(double a, double b) -> bool {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
auto same(RigidVector3 a, RigidVector3 b) -> bool {
  return same(a.x, b.x) && same(a.y, b.y) && same(a.z, b.z);
}
auto same(const RigidBodyState& a, const RigidBodyState& b) -> bool {
  return a.craft == b.craft && a.frame == b.frame && a.tick == b.tick &&
         same(a.position_metres, b.position_metres) &&
         same(a.linear_velocity_metres_per_second,
              b.linear_velocity_metres_per_second) &&
         same(a.angular_velocity_radians_per_second,
              b.angular_velocity_radians_per_second) &&
         same(a.orientation.w, b.orientation.w) &&
         same(a.orientation.x, b.orientation.x) &&
         same(a.orientation.y, b.orientation.y) &&
         same(a.orientation.z, b.orientation.z);
}
auto same(const TouchdownObservations& a, const TouchdownObservations& b)
    -> bool {
  if (a.planet != b.planet || a.tick != b.tick ||
      a.support_count != b.support_count || a.gear_deployed != b.gear_deployed)
    return false;
  for (std::size_t i = 0; i < a.supports.size(); ++i) {
    const auto& x = a.supports[i];
    const auto& y = b.supports[i];
    if (x.support_index != y.support_index || x.surface != y.surface ||
        x.footprint_supported != y.footprint_supported ||
        !same(x.minimum_gap_metres, y.minimum_gap_metres) ||
        !same(x.maximum_gap_metres, y.maximum_gap_metres) ||
        !same(x.outward_normal, y.outward_normal))
      return false;
  }
  return true;
}
auto same(const TouchdownAssessment& a, const TouchdownAssessment& b) -> bool {
  if (a.classification != b.classification ||
      a.failed_margins != b.failed_margins ||
      a.support_count != b.support_count)
    return false;
  for (std::size_t i = 0; i < a.supports.size(); ++i) {
    const auto& x = a.supports[i];
    const auto& y = b.supports[i];
    if (x.failed_margins != y.failed_margins ||
        !same(x.normal_velocity_metres_per_second,
              y.normal_velocity_metres_per_second) ||
        !same(x.tangential_speed_metres_per_second,
              y.tangential_speed_metres_per_second) ||
        !same(x.compression_capacity_metres, y.compression_capacity_metres))
      return false;
  }
  return true;
}
struct Fixture {
  LocalSystemDescriptor system{generate_origin_system(Seed{42})};
  RigidBodyWorldContext context{system};
  auto state() const -> RigidBodyState {
    RigidBodyState result;
    result.frame = {RigidFrameKind::planet_fixed, system.id,
                    system.planets.front().descriptor.id, std::nullopt};
    result.tick = 17;
    result.position_metres = {
        0, system.planets.front().descriptor.radius.value * 1000.0 + 1.328, 0};
    return result;
  }
  auto observations() const -> TouchdownObservations {
    TouchdownObservations result;
    result.planet = *state().frame.planet;
    result.tick = state().tick;
    result.support_count = 3;
    result.gear_deployed = true;
    for (std::uint8_t i = 0; i < result.support_count; ++i)
      result.supports[i] = {i, 0, 0, {0, 1, 0}, TouchdownSurface::solid, true};
    return result;
  }
};

auto boundaries(const Fixture& f) -> void {
  const auto reject = [&](RigidBodyState state,
                          TouchdownObservations observations) {
    const auto before = state;
    const auto original = observations;
    check(!assess_touchdown_envelope(f.context, state, observations),
          "malformed touchdown input rejected");
    check(same(state, before) && same(observations, original),
          "invalid touchdown input remains bitwise unchanged");
  };
  auto state = f.state();
  auto observations = f.observations();
  state.frame.kind = RigidFrameKind::system_inertial;
  state.frame.planet.reset();
  reject(state, observations);
  state = f.state();
  state.frame.planet.reset();
  reject(state, observations);
  state = f.state();
  state.frame.system.value ^= 1;
  reject(state, observations);
  state = f.state();
  state.frame.planet = PlanetId{0};
  reject(state, observations);
  state = f.state();
  state.craft.id.value = 0;
  reject(state, observations);
  state = f.state();
  state.craft.version = 2;
  reject(state, observations);
  state = f.state();
  state.position_metres = {};
  reject(state, observations);
  state = f.state();
  state.position_metres.x = 1e16;
  reject(state, observations);
  state = f.state();
  state.linear_velocity_metres_per_second.x = 1e10;
  reject(state, observations);
  state = f.state();
  state.angular_velocity_radians_per_second.x = 100.001;
  reject(state, observations);
  state = f.state();
  state.tick = std::numeric_limits<SimulationTick>::max();
  observations.tick = state.tick;
  reject(state, observations);
  observations = f.observations();
  for (auto q :
       {RigidOrientation{0, 0, 0, 0}, RigidOrientation{-1, 0, 0, 0},
        RigidOrientation{1, -0.0, 0, 0}, RigidOrientation{2, 0, 0, 0}}) {
    state = f.state();
    state.orientation = q;
    reject(state, observations);
  }
  for (double bad : {NAN, INFINITY, -INFINITY}) {
    state = f.state();
    state.position_metres.y = bad;
    reject(state, observations);
    state = f.state();
    state.linear_velocity_metres_per_second.x = bad;
    reject(state, observations);
    state = f.state();
    state.angular_velocity_radians_per_second.z = bad;
    reject(state, observations);
    state = f.state();
    state.orientation.w = bad;
    reject(state, observations);
    auto invalid = observations;
    invalid.supports[1].minimum_gap_metres = bad;
    reject(f.state(), invalid);
    invalid = observations;
    invalid.supports[1].maximum_gap_metres = bad;
    reject(f.state(), invalid);
    for (int axis = 0; axis < 3; ++axis) {
      invalid = observations;
      const std::array components{&invalid.supports[1].outward_normal.x,
                                  &invalid.supports[1].outward_normal.y,
                                  &invalid.supports[1].outward_normal.z};
      *components[static_cast<std::size_t>(axis)] = bad;
      reject(f.state(), invalid);
    }
  }
  observations = f.observations();
  observations.planet.value ^= 1;
  reject(f.state(), observations);
  observations = f.observations();
  ++observations.tick;
  reject(f.state(), observations);
  for (unsigned count : {0U, 1U, 2U, 4U, 5U, 255U}) {
    observations = f.observations();
    observations.support_count = static_cast<std::uint8_t>(count);
    reject(f.state(), observations);
  }
  for (unsigned index : {0U, 2U, 3U, 255U}) {
    observations = f.observations();
    observations.supports[1].support_index = static_cast<std::uint8_t>(index);
    reject(f.state(), observations);
  }
  observations = f.observations();
  std::swap(observations.supports[0], observations.supports[2]);
  reject(f.state(), observations);
  observations = f.observations();
  observations.supports[3] = observations.supports[0];
  reject(f.state(), observations);
  observations = f.observations();
  observations.supports[1].minimum_gap_metres = .01;
  reject(f.state(), observations);
  observations = f.observations();
  observations.supports[1].maximum_gap_metres = 1e16;
  reject(f.state(), observations);
  for (RigidVector3 normal : {RigidVector3{}, RigidVector3{0, 2, 0},
                              RigidVector3{0, -1, 0}, RigidVector3{1, 0, 0}}) {
    observations = f.observations();
    observations.supports[1].outward_normal = normal;
    reject(f.state(), observations);
  }
  for (unsigned material : {0U, 4U, 255U}) {
    observations = f.observations();
    observations.supports[1].surface = static_cast<TouchdownSurface>(material);
    reject(f.state(), observations);
  }
}

auto baseline_and_purity(const Fixture& f) -> void {
  const auto state = f.state();
  const auto observations = f.observations();
  const auto result = assess_touchdown_envelope(f.context, state, observations);
  check(result && result->classification == TouchdownClass::pad_contact_ready &&
            result->failed_margins == 0 && result->support_count == 3,
        "stationary level solid fully supported deployed pads are ready");
  if (!result) return;
  for (std::size_t i = 0; i < result->support_count; ++i) {
    near(result->supports[i].normal_velocity_metres_per_second, 0, 0,
         "resting pad has zero normal speed");
    near(result->supports[i].tangential_speed_metres_per_second, 0, 0,
         "resting pad has zero tangent speed");
    near(result->supports[i].compression_capacity_metres, .3, 0,
         "level pad retains authored 300mm stroke");
  }
  for (int repeat = 0; repeat < 100; ++repeat) {
    const auto again =
        assess_touchdown_envelope(f.context, state, observations);
    check(again && same(*result, *again),
          "repeated assessment reproduces every output bit");
    check(same(state, f.state()) && same(observations, f.observations()),
          "pure assessment cannot move craft, advance ticks or alter "
          "observations");
  }
  for (double gap : {std::numeric_limits<double>::denorm_min(), .001, 16.0}) {
    auto clear = observations;
    for (std::size_t i = 0; i < clear.support_count; ++i)
      clear.supports[i].minimum_gap_metres =
          clear.supports[i].maximum_gap_metres = gap;
    const auto classified = assess_touchdown_envelope(f.context, state, clear);
    check(classified &&
              classified->classification == TouchdownClass::pads_clear,
          "strictly positive pad gaps never fabricate contact or landing");
  }
  auto airborne = observations;
  airborne.gear_deployed = false;
  for (std::size_t i = 0; i < airborne.support_count; ++i)
    airborne.supports[i].minimum_gap_metres =
        airborne.supports[i].maximum_gap_metres = 16;
  const auto clear = assess_touchdown_envelope(f.context, state, airborne);
  check(clear && clear->classification == TouchdownClass::pads_clear &&
            clear->failed_margins == (bit(TouchdownMargin::gear_not_deployed) |
                                      bit(TouchdownMargin::separated_support)),
        "clear classification retains complete unmet readiness margins without "
        "fabricating contact");
  if (clear)
    for (std::size_t i = 0; i < clear->support_count; ++i)
      check(clear->supports[i].failed_margins ==
                bit(TouchdownMargin::separated_support),
            "clear pad keeps local separation margin, not unrelated global "
            "gear flag");
}

auto environment_contract(const Fixture& f) -> void {
  // Current registered frame covers the entire generated environment range.
  // Do not forge an impossible world merely to hit future-proof failure bits.
  std::array<bool, 4> found{};
  for (std::uint64_t seed = 0;
       seed < 65536 && found != std::array{true, true, true, true}; ++seed) {
    const auto system = generate_local_system(Seed{seed});
    const RigidBodyWorldContext context{system};
    for (const auto& body : system.planets) {
      const auto& planet = body.descriptor;
      const std::array edges{
          planet.surface_gravity.value == SurfaceGravityMilliG::min,
          planet.surface_gravity.value == SurfaceGravityMilliG::max,
          planet.atmosphere_pressure.value == AtmospherePressureMillibars::min,
          planet.atmosphere_pressure.value == AtmospherePressureMillibars::max};
      for (std::size_t edge = 0; edge < edges.size(); ++edge) {
        if (!edges[edge] || found[edge]) continue;
        auto state = f.state();
        state.frame.system = system.id;
        state.frame.planet = planet.id;
        state.position_metres = {0, planet.radius.value * 1000.0 + 1.328, 0};
        auto observations = f.observations();
        observations.planet = planet.id;
        const auto result =
            assess_touchdown_envelope(context, state, observations);
        check(result &&
                  result->classification == TouchdownClass::pad_contact_ready &&
                  result->failed_margins == 0,
              "generated min/max gravity and pressure remain inside starter "
              "environment/load ratings");
        found[edge] = true;
        std::cout << "Touchdown environment edge " << edge << " system seed "
                  << seed << " planet " << planet.id.value << '\n';
      }
    }
  }
  check(found == std::array{true, true, true, true},
        "bounded generated population covers all environment extrema");
  for (bool excessive_gravity : {false, true}) {
    LocalSystemDescriptor changed{
        f.system.seed, f.system.id, f.system.kind, f.system.star, {}};
    for (const auto& body : f.system.planets) {
      const auto& p = body.descriptor;
      const PlanetDescriptor forged{
          p.seed,
          p.id,
          p.display_name,
          p.radius,
          excessive_gravity ? SurfaceGravityMilliG{3000} : p.surface_gravity,
          p.atmosphere_class,
          excessive_gravity ? p.atmosphere_pressure
                            : AtmospherePressureMillibars{3000},
          p.terrain_character,
          p.water_coverage,
          p.palette};
      changed.planets.push_back({forged, body.orbit});
    }
    const RigidBodyWorldContext context{changed};
    check(!assess_touchdown_envelope(context, f.state(), f.observations()),
          "forged high-gravity/high-pressure descriptor cannot bypass "
          "authoritative world identity");
  }
}

auto scalar_edges(const Fixture& f) -> void {
  const auto observations = f.observations();
  for (const double velocity :
       {-2.0, std::nextafter(-2.0, 0.0), std::nextafter(-2.0, -3.0), 0.0,
        std::numeric_limits<double>::denorm_min()}) {
    auto state = f.state();
    state.linear_velocity_metres_per_second.y = velocity;
    const auto result =
        assess_touchdown_envelope(f.context, state, observations);
    check(result.has_value(), "finite vertical boundary state accepted");
    if (!result) continue;
    check(has(*result, TouchdownMargin::descent_speed_exceeded) ==
              (velocity < -2),
          "2m/s descending limit inclusive; adjacent excess refused");
    check(has(*result, TouchdownMargin::separating_velocity) == (velocity > 0),
          "stationary contact allowed; any positive separating speed refused");
  }
  for (double speed :
       {3.0, std::nextafter(3.0, 0.0), std::nextafter(3.0, 4.0)}) {
    auto state = f.state();
    state.linear_velocity_metres_per_second.x = speed;
    const auto result =
        assess_touchdown_envelope(f.context, state, observations);
    check(result && has(*result, TouchdownMargin::tangential_speed_exceeded) ==
                        (speed > 3),
          "3m/s tangent limit inclusive with nextafter boundary");
  }
  for (double speed : {.1, std::nextafter(.1, 0.0), std::nextafter(.1, 1.0)}) {
    auto state = f.state();
    state.angular_velocity_radians_per_second.y = speed;
    const auto result =
        assess_touchdown_envelope(f.context, state, observations);
    check(result && has(*result, TouchdownMargin::angular_speed_exceeded) ==
                        (speed > .1),
          "0.1rad/s angular norm limit inclusive with nextafter boundary");
  }
  for (double compression :
       {.3, std::nextafter(.3, 0.0), std::nextafter(.3, 1.0)}) {
    auto compressed = observations;
    compressed.supports[1].minimum_gap_metres = -compression;
    const auto result =
        assess_touchdown_envelope(f.context, f.state(), compressed);
    check(result && has(*result, TouchdownMargin::compression_exceeded) ==
                        (compression > .3),
          "300mm stroke inclusive; whole-pad deepest penetration controls");
  }
  for (double gap : {0.0, std::numeric_limits<double>::denorm_min()}) {
    auto partial = observations;
    partial.supports[1].maximum_gap_metres = gap;
    const auto result =
        assess_touchdown_envelope(f.context, f.state(), partial);
    check(
        result && has(*result, TouchdownMargin::separated_support) == (gap > 0),
        "touching pad corner cannot hide another unsupported separated corner");
  }
}

auto geometry_and_material(const Fixture& f) -> void {
  for (auto material :
       {TouchdownSurface::water, TouchdownSurface::unsupported}) {
    auto observations = f.observations();
    observations.supports[1].surface = material;
    const auto result =
        assess_touchdown_envelope(f.context, f.state(), observations);
    check(result &&
              result->classification == TouchdownClass::pad_contact_unsafe &&
              result->failed_margins ==
                  bit(TouchdownMargin::unsupported_material),
          "water/unsupported material is semantic unsafe contact, not "
          "malformed input");
  }
  auto observations = f.observations();
  observations.supports[2].footprint_supported = false;
  auto result = assess_touchdown_envelope(f.context, f.state(), observations);
  check(result && result->failed_margins ==
                      bit(TouchdownMargin::unsupported_footprint),
        "ridge/ledge missing whole-footprint support named independently of "
        "average height");
  observations = f.observations();
  observations.gear_deployed = false;
  result = assess_touchdown_envelope(f.context, f.state(), observations);
  check(result &&
            result->classification == TouchdownClass::pad_contact_unsafe &&
            has(*result, TouchdownMargin::gear_not_deployed),
        "undeployed rated support does not count as ready");
  auto state = f.state();
  state.orientation = {0, 1, 0, 0};
  result = assess_touchdown_envelope(f.context, state, f.observations());
  check(result &&
            result->classification == TouchdownClass::pad_contact_unsafe &&
            has(*result, TouchdownMargin::not_upright),
        "inverted craft cannot be accepted from pad observations alone");
  state = f.state();
  state.orientation = {.5, .5, .5, .5};
  result = assess_touchdown_envelope(f.context, state, f.observations());
  check(result && has(*result, TouchdownMargin::not_upright),
        "exactly sideways body-up projection is excluded, not accepted as "
        "upright");
  constexpr double cosine_limit = .9781476007338057;
  // Both an exact power-of-two fixture and the original planet-radius-plus-
  // gear fixture preserve axis-aligned unit radial. The latter regresses an
  // inverse-length multiply that shifted radial +Y one ULP below one.
  for (double radius : {0x1p23, f.state().position_metres.y}) {
    state = f.state();
    state.position_metres = {0, radius, 0};
    for (double cosine : {cosine_limit, std::nextafter(cosine_limit, 1.0),
                          std::nextafter(cosine_limit, 0.0)}) {
      observations = f.observations();
      observations.supports[0].outward_normal = {std::sqrt(1 - cosine * cosine),
                                                 cosine, 0};
      const auto normal = observations.supports[0].outward_normal;
      check((normal.x * normal.x + normal.y * normal.y) + normal.z * normal.z ==
                1,
            "slope boundary fixture has exact unit norm before provider "
            "normalization");
      result = assess_touchdown_envelope(f.context, state, observations);
      check(
          result && has(*result, TouchdownMargin::slope_exceeded) ==
                        (cosine < cosine_limit),
          "12degree slope uses inclusive dot threshold without acos ambiguity");
      if (result)
        near(result->supports[0].compression_capacity_metres, .3 * cosine,
             1e-15,
             "gear stroke projects along individual pad normal, not planet "
             "radial");
    }
  }
}

auto assessment_hash(const TouchdownAssessment& value) -> std::uint64_t {
  std::uint64_t hash = 14695981039346656037ULL;
  const auto add = [&](std::uint64_t integer, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) {
      hash ^= (integer >> (i * 8U)) & 255U;
      hash *= 1099511628211ULL;
    }
  };
  for (const unsigned char character :
       std::string_view{"apsis-touchdown-supplied-v1"})
    add(character, 1);
  add(kTouchdownEnvelopeVersion, 4);
  add(static_cast<std::uint8_t>(value.classification), 1);
  add(value.failed_margins, 4);
  add(value.support_count, 1);
  for (const auto& pad : value.supports) {
    add(pad.failed_margins, 4);
    add(std::bit_cast<std::uint64_t>(pad.normal_velocity_metres_per_second), 8);
    add(std::bit_cast<std::uint64_t>(pad.tangential_speed_metres_per_second),
        8);
    add(std::bit_cast<std::uint64_t>(pad.compression_capacity_metres), 8);
  }
  return hash;
}

auto exact_assessment_contract(const Fixture& f) -> void {
  static_assert(kTouchdownEnvelopeVersion == 1);
  // Level result independently constructed from the analytic zero-speed,
  // 300mm-stroke record; other records observed identically with GCC/Clang
  // after the separate analytic geometry and boundary contracts above.
  constexpr std::array<std::uint64_t, 4> golden{
      12330518051017978207ULL, 43715311420859247ULL, 3324266018647820293ULL,
      31461596898727366ULL};
  for (unsigned fixture = 0; fixture < 4; ++fixture) {
    auto state = f.state();
    auto observations = f.observations();
    if (fixture == 1) {
      state.linear_velocity_metres_per_second = {.5, -.2, .1};
      for (std::size_t i = 0; i < observations.support_count; ++i)
        observations.supports[i].outward_normal = {std::sqrt(1 - .98 * .98),
                                                   .98, 0};
    } else if (fixture == 2) {
      state.orientation = {.5, .5, .5, .5};
      state.position_metres = {0, 0, state.position_metres.y};
      state.linear_velocity_metres_per_second = {0, 0, -1.9};
      state.angular_velocity_radians_per_second = {-.1, 0, 0};
      for (std::size_t i = 0; i < observations.support_count; ++i)
        observations.supports[i].outward_normal = {0, 0, 1};
    } else if (fixture == 3) {
      state.linear_velocity_metres_per_second = {4, -3, 0};
      state.angular_velocity_radians_per_second = {0, .11, 0};
      observations.gear_deployed = false;
      observations.supports[0].surface = TouchdownSurface::water;
      observations.supports[1].footprint_supported = false;
      observations.supports[1].minimum_gap_metres = -.4;
      observations.supports[2].maximum_gap_metres = .1;
    }
    const auto assessed =
        assess_touchdown_envelope(f.context, state, observations);
    check(assessed.has_value(), "fixed exact-assessment fixture evaluates");
    if (!assessed) continue;
    check(
        assessment_hash(*assessed) == golden[fixture],
        "version-one exact assessment matches analytic/cross-compiler golden");
    std::cout << "Touchdown exact fixture " << fixture << " hash "
              << assessment_hash(*assessed) << '\n';
  }
}

auto pad_velocity_and_combined_margins(const Fixture& f) -> void {
  auto state = f.state();
  state.linear_velocity_metres_per_second = {0, -1.9, 0};
  state.angular_velocity_radians_per_second = {-.1, 0, 0};
  auto result = assess_touchdown_envelope(f.context, state, f.observations());
  check(
      result && has(*result, TouchdownMargin::descent_speed_exceeded) &&
          !has(*result, TouchdownMargin::angular_speed_exceeded),
      "safe COM descent and allowed spin can still overspeed nose-pad contact");
  if (result) {
    near(result->supports[0].normal_velocity_metres_per_second, -2.57, 1e-14,
         "nose pad normal speed includes omega cross 6.7metre lever arm");
    near(result->supports[1].normal_velocity_metres_per_second, -1.63, 1e-14,
         "rear pad has distinct opposite pitch-induced normal velocity");
  }
  // Rotate the complete fixture X->Y, Y->Z, Z->X. Body omega is unchanged,
  // but world-space point velocities must follow the authoritative quaternion.
  const auto original = result;
  state.orientation = {.5, .5, .5, .5};
  state.position_metres = {0, 0, state.position_metres.y};
  state.linear_velocity_metres_per_second = {0, 0, -1.9};
  auto rotated = f.observations();
  for (std::size_t i = 0; i < rotated.support_count; ++i)
    rotated.supports[i].outward_normal = {0, 0, 1};
  result = assess_touchdown_envelope(f.context, state, rotated);
  check(result && original &&
            result->classification == original->classification &&
            result->failed_margins == original->failed_margins,
        "rotating craft, motion and ground together preserves semantic "
        "assessment");
  if (result && original)
    for (std::size_t i = 0; i < result->support_count; ++i) {
      near(result->supports[i].normal_velocity_metres_per_second,
           original->supports[i].normal_velocity_metres_per_second, 1e-14,
           "body angular velocity transforms into owning-frame pad normal "
           "speed");
      near(result->supports[i].tangential_speed_metres_per_second,
           original->supports[i].tangential_speed_metres_per_second, 1e-14,
           "quaternion transform preserves physical pad tangent speed");
    }
  state = f.state();
  state.linear_velocity_metres_per_second = {-2.9, 0, 0};
  state.angular_velocity_radians_per_second = {0, .1, 0};
  result = assess_touchdown_envelope(f.context, state, f.observations());
  check(result && has(*result, TouchdownMargin::tangential_speed_exceeded),
        "yaw pad speed can exceed horizontal envelope despite safe COM speed");
  if (result)
    near(result->supports[0].tangential_speed_metres_per_second, 3.57, 1e-14,
         "nose pad tangential speed includes yaw lever arm");
  state = f.state();
  state.linear_velocity_metres_per_second = {4, -3, 0};
  state.angular_velocity_radians_per_second = {0, .11, 0};
  auto observations = f.observations();
  observations.gear_deployed = false;
  observations.supports[0].surface = TouchdownSurface::water;
  observations.supports[1].footprint_supported = false;
  observations.supports[1].minimum_gap_metres = -.4;
  observations.supports[2].maximum_gap_metres = .1;
  result = assess_touchdown_envelope(f.context, state, observations);
  constexpr auto expected = bit(TouchdownMargin::gear_not_deployed) |
                            bit(TouchdownMargin::unsupported_material) |
                            bit(TouchdownMargin::unsupported_footprint) |
                            bit(TouchdownMargin::compression_exceeded) |
                            bit(TouchdownMargin::separated_support) |
                            bit(TouchdownMargin::descent_speed_exceeded) |
                            bit(TouchdownMargin::tangential_speed_exceeded) |
                            bit(TouchdownMargin::angular_speed_exceeded);
  check(result &&
            result->classification == TouchdownClass::pad_contact_unsafe &&
            result->failed_margins == expected,
        "all simultaneous unmet margins accumulate without first-failure short "
        "circuit");
}

auto physical_owner_contract() -> void {
  const auto origin = generate_physical_origin_system(Seed{42});
  check(origin.has_value(), "genuine physical origin generated");
  if (!origin) return;
  const auto procedural = generate_physical_local_system(origin->catalog.seed);
  check(procedural.has_value(), "genuine same-ID procedural owner generated");
  if (!procedural) return;
  check(origin->catalog.id == procedural->catalog.id &&
            origin->catalog.planets.front().descriptor.id ==
                procedural->catalog.planets.front().descriptor.id &&
            origin->catalog.planets.front().descriptor !=
                procedural->catalog.planets.front().descriptor,
        "same numeric IDs do not substitute origin/procedural descriptors");
  const auto station = generate_origin_station(Seed{42});
  for (const auto* owner : {&*origin, &*procedural}) {
    const auto before_owner = *owner;
    const auto& planet = owner->catalog.planets.front();
    const RigidBodyWorldContext context{*owner};
    const auto selected =
        find_local_system_planet(*owner, planet.descriptor.id);
    check(selected && *selected == &planet &&
              !validate_local_system(owner->catalog) &&
              !find_local_system_planet(owner->catalog, planet.descriptor.id),
          "physical owner resolves its own descriptor; legacy catalog refuses");
    for (const auto recipe :
         {CraftFrameRecipe{kStarterShuttleFrameId, kStarterShuttleFrameVersion},
          CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion}}) {
      const auto frame = resolve_craft_frame(recipe);
      check(frame.has_value(), "registered terrain-contact craft resolves");
      if (!frame) continue;
      const auto& craft = frame->properties;
      RigidBodyState state;
      state.craft = recipe;
      state.frame = {RigidFrameKind::planet_fixed, owner->catalog.id,
                     planet.descriptor.id, std::nullopt};
      state.tick = 17;
      state.position_metres = {
          0,
          planet.descriptor.radius.value * 1000.0 -
              static_cast<double>(craft.supports[0].contact_mm[1]) / 1000.0,
          0};
      TouchdownObservations observations;
      observations.planet = planet.descriptor.id;
      observations.tick = state.tick;
      observations.support_count = craft.support_count;
      observations.gear_deployed = true;
      for (std::uint8_t i = 0; i < observations.support_count; ++i)
        observations.supports[i] = {
            i, 0, 0, {0, 1, 0}, TouchdownSurface::solid, true};
      const auto before_state = state;
      const auto before_observations = observations;
      const auto bytes = encode_rigid_body_state_json(context, state);
      const auto checksum = rigid_body_state_checksum(context, state);
      const auto result =
          assess_touchdown_envelope(context, state, observations);
      const double gravity =
          planet.descriptor.surface_gravity.value * 9.80665 / 1000.0;
      std::uint32_t expected{};
      if (gravity > craft.max_surface_gravity_mm_per_second2 / 1000.0)
        expected |= bit(TouchdownMargin::gravity_rating_exceeded);
      if (planet.descriptor.atmosphere_pressure.value >
          craft.max_pressure_millibars)
        expected |= bit(TouchdownMargin::pressure_rating_exceeded);
      for (std::size_t i = 0; i < craft.support_count; ++i)
        if (craft.dry_mass_kg * gravity > craft.supports[i].rated_load_newtons)
          expected |= bit(TouchdownMargin::static_support_load_exceeded);
      check(result && result->failed_margins == expected && expected == 0 &&
                result->classification == TouchdownClass::pad_contact_ready &&
                result->support_count == craft.support_count,
            "both physical owners/crafts retain actual environment/load rules");
      if (result)
        for (std::size_t i = 0; i < craft.support_count; ++i) {
          near(result->supports[i].normal_velocity_metres_per_second, 0, 0,
               "physical level pad normal speed is zero");
          near(result->supports[i].tangential_speed_metres_per_second, 0, 0,
               "physical level pad tangential speed is zero");
          near(result->supports[i].compression_capacity_metres, .3, 0,
               "physical craft retains authored stroke");
        }
      auto moving = state;
      moving.linear_velocity_metres_per_second.y = -1.9;
      const double pitch = recipe.id == kStarterShuttleFrameId ? -.1 : .1;
      moving.angular_velocity_radians_per_second.x = pitch;
      const auto spun =
          assess_touchdown_envelope(context, moving, observations);
      check(spun && has(*spun, TouchdownMargin::descent_speed_exceeded),
            "both physical craft geometries retain pad overspeed at safe COM");
      if (spun)
        for (std::size_t i = 0; i < craft.support_count; ++i) {
          const double offset =
              static_cast<double>(craft.supports[i].contact_mm[2] -
                                  craft.center_of_mass_mm[2]) /
              1000.0;
          near(spun->supports[i].normal_velocity_metres_per_second,
               -1.9 - pitch * offset, 1e-14,
               "independent body-X cross lever-arm normal velocity");
        }
      const auto reject = [&](const RigidBodyWorldContext& selected_context,
                              RigidBodyState invalid_state,
                              TouchdownObservations invalid_observations,
                              TouchdownError error) {
        const auto prior_state = invalid_state;
        const auto prior_observations = invalid_observations;
        const auto rejected = assess_touchdown_envelope(
            selected_context, invalid_state, invalid_observations);
        check(!rejected && rejected.error() == error,
              "physical invalid input preserves existing error/order");
        check(same(invalid_state, prior_state) &&
                  same(invalid_observations, prior_observations),
              "physical refusal leaves every state/observation bit unchanged");
      };
      for (unsigned field = 0; field < 4; ++field) {
        auto forged = *owner;
        if (field == 0) ++forged.generator_version;
        if (field == 1) ++forged.source_catalog_generator;
        if (field == 2) ++forged.ephemeris_version;
        if (field == 3) forged.origin_universe_seed = Seed{43};
        auto bad_observations = observations;
        bad_observations.support_count = 0;
        reject(RigidBodyWorldContext{forged}, state, bad_observations,
               TouchdownError::invalid_state);
      }
      auto wrong_state = state;
      wrong_state.frame.kind = RigidFrameKind::system_inertial;
      wrong_state.frame.planet.reset();
      auto wrong_observations = observations;
      ++wrong_observations.tick;
      reject(context, wrong_state, wrong_observations,
             TouchdownError::unsupported_frame);
      wrong_observations.support_count = 0;
      reject(context, state, wrong_observations,
             TouchdownError::observation_identity_mismatch);
      wrong_state = state;
      wrong_state.frame.system.value ^= 1;
      reject(context, wrong_state, observations, TouchdownError::invalid_state);
      wrong_state = state;
      wrong_state.frame.planet = PlanetId{0};
      reject(context, wrong_state, observations, TouchdownError::invalid_state);
      wrong_observations = observations;
      wrong_observations.planet = PlanetId{0};
      reject(context, state, wrong_observations,
             TouchdownError::observation_identity_mismatch);
      for (double bad : {NAN, INFINITY, -INFINITY}) {
        wrong_state = state;
        wrong_state.position_metres.y = bad;
        reject(context, wrong_state, observations,
               TouchdownError::invalid_state);
        wrong_observations = observations;
        wrong_observations.supports[1].minimum_gap_metres = bad;
        reject(context, state, wrong_observations,
               TouchdownError::invalid_observation);
        wrong_observations = observations;
        wrong_observations.supports[1].outward_normal.z = bad;
        reject(context, state, wrong_observations,
               TouchdownError::invalid_normal);
      }
      wrong_observations = observations;
      wrong_observations.support_count = 4;
      reject(context, state, wrong_observations,
             TouchdownError::invalid_support_count);
      wrong_observations = observations;
      wrong_observations.supports[1].support_index = 0;
      reject(context, state, wrong_observations,
             TouchdownError::invalid_support_order);
      wrong_observations = observations;
      wrong_observations.supports[3] = observations.supports[0];
      reject(context, state, wrong_observations,
             TouchdownError::invalid_observation);
      wrong_observations = observations;
      wrong_observations.supports[1].outward_normal = {0, -1, 0};
      reject(context, state, wrong_observations,
             TouchdownError::invalid_normal);
      wrong_observations = observations;
      wrong_observations.gear_deployed = false;
      wrong_observations.supports[0].surface = TouchdownSurface::water;
      wrong_observations.supports[1].footprint_supported = false;
      wrong_observations.supports[2].minimum_gap_metres =
          -std::nextafter(.3, 1.0);
      const auto unsafe =
          assess_touchdown_envelope(context, state, wrong_observations);
      check(
          unsafe &&
              unsafe->classification == TouchdownClass::pad_contact_unsafe &&
              unsafe->failed_margins ==
                  (bit(TouchdownMargin::gear_not_deployed) |
                   bit(TouchdownMargin::unsupported_material) |
                   bit(TouchdownMargin::unsupported_footprint) |
                   bit(TouchdownMargin::compression_exceeded)),
          "physical owner lookup grants no gear/material/footprint permission");
      if (owner == &*origin) {
        const PhysicalLocalSystem substituted{
            origin->generator_version,
            origin->source_catalog_generator,
            origin->ephemeris_version,
            origin->origin_universe_seed,
            procedural->catalog,
            origin->stellar_mass_millisolar,
            origin->stellar_gm_km3_per_second2};
        reject(RigidBodyWorldContext{substituted}, state, observations,
               TouchdownError::invalid_state);
        reject(RigidBodyWorldContext{*procedural, &station}, state,
               observations, TouchdownError::invalid_state);
        check(
            bytes && !decode_rigid_body_state_json(
                         RigidBodyWorldContext{*procedural}, *bytes),
            "same-ID procedural owner cannot hydrate origin-owned projection");
      }
      check(same(state, before_state) &&
                same(observations, before_observations) &&
                *owner == before_owner && bytes && checksum &&
                encode_rigid_body_state_json(context, state) == bytes &&
                rigid_body_state_checksum(context, state) == checksum,
            "physical assessment preserves owner, input bits and projection");
    }
  }
}
} // namespace

auto main() -> int {
  const Fixture fixture;
  boundaries(fixture);
  baseline_and_purity(fixture);
  environment_contract(fixture);
  scalar_edges(fixture);
  geometry_and_material(fixture);
  pad_velocity_and_combined_margins(fixture);
  exact_assessment_contract(fixture);
  physical_owner_contract();
  std::cout << "Touchdown supplied-pad envelope: " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
