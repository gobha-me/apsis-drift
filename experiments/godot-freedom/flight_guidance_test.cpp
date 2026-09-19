#include "flight_guidance.hpp"

#include <iostream>
#include <limits>

using namespace apsis_drift;
using namespace apsis_drift::flight_lab;

namespace {
auto check(bool condition, const char* message) -> void {
  if (!condition) throw std::runtime_error(message);
}
template <typename F> auto rejects(F function) -> void {
  try {
    function();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error("malformed guidance input accepted");
}
auto fixture(const PlanetDescriptor& planet, double altitude) -> State {
  State result;
  result.position = {planet.radius.value * 1000.0 + altitude, 0, 0};
  result.right = {1, 0, 0};
  result.up = {0, 1, 0};
  result.back = {0, 0, 1};
  return result;
}
auto finite_result(const Guidance& result, GuidanceRequest request) -> void {
  check(result.coast_samples.size() <= request.sample_count,
        "guidance sample count exceeded bound");
  check(finite(result.target_position) && finite(result.target_velocity) &&
            finite(result.delta_velocity) &&
            std::isfinite(result.reference_radius_metres) &&
            std::isfinite(result.reference_periapsis_radius_metres),
        "nonfinite target guidance");
  const auto& orbit = result.orbit;
  for (double value : {orbit.radial_speed, orbit.horizontal_speed,
                       orbit.circular_speed, orbit.periapsis, orbit.apoapsis,
                       orbit.eccentricity, orbit.atmosphere_edge})
    check(std::isfinite(value), "nonfinite orbit guidance");
  double previous = -1;
  for (const auto& sample : result.coast_samples) {
    check(finite(sample.position) && std::isfinite(sample.seconds) &&
              sample.seconds >= 0 &&
              sample.seconds <= request.horizon_seconds &&
              sample.seconds > previous && length(sample.position) <= 1e10,
          "coast sample not finite, monotonic or bounded");
    previous = sample.seconds;
  }
  check(result.central_gravity_vacuum_only, "forecast limitations missing");
}
auto malformed(const PlanetDescriptor& planet, const State& baseline) -> void {
  for (const auto mode :
       {GuidanceMode::none, GuidanceMode::orbit,
        GuidanceMode::return_to_surface, GuidanceMode::escape}) {
    for (double bad : {std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity()}) {
      for (unsigned axis = 0; axis < 3; ++axis) {
        State state = baseline;
        std::array components{&state.position.x, &state.position.y,
                              &state.position.z};
        *components[axis] = bad;
        rejects([&] { (void)flight_guidance(planet, state, {mode}); });
        state = baseline;
        components = {&state.velocity.x, &state.velocity.y, &state.velocity.z};
        *components[axis] = bad;
        rejects([&] { (void)flight_guidance(planet, state, {mode}); });
      }
      rejects([&] { (void)flight_guidance(planet, baseline, {mode, bad}); });
    }
  }
  for (double bad :
       {0.0, -1.0, std::numeric_limits<double>::denorm_min(), 1e-300, .999,
        std::nextafter(1.0, 0.0), std::nextafter(1800.0, 2000.0), 1800.000001})
    rejects([&] {
      (void)flight_guidance(planet, baseline, {GuidanceMode::orbit, bad});
    });
  for (unsigned count : {0U, 1U, 258U, std::numeric_limits<unsigned>::max()})
    rejects([&] {
      (void)flight_guidance(planet, baseline,
                            {GuidanceMode::orbit, 900, count});
    });
  rejects([&] {
    (void)flight_guidance(planet, baseline, {static_cast<GuidanceMode>(999)});
  });
  for (const V position : {V{}, V{999, 0, 0}, V{1e11, 0, 0}}) {
    auto state = baseline;
    state.position = position;
    rejects(
        [&] { (void)flight_guidance(planet, state, {GuidanceMode::orbit}); });
  }
  auto state = baseline;
  state.velocity = {100001, 0, 0};
  rejects([&] { (void)flight_guidance(planet, state, {GuidanceMode::orbit}); });
  const PlanetDescriptor forged{planet.seed,
                                PlanetId{planet.id.value ^ 1},
                                planet.display_name,
                                planet.radius,
                                planet.surface_gravity,
                                planet.atmosphere_class,
                                planet.atmosphere_pressure,
                                planet.terrain_character,
                                planet.water_coverage,
                                planet.palette};
  rejects(
      [&] { (void)flight_guidance(forged, baseline, {GuidanceMode::orbit}); });
}
} // namespace

auto main() -> int {
  try {
    const auto planet = generate_planet_descriptor(Seed{42});
    const double surface = planet.radius.value * 1000.0;
    const double mu =
        9.80665 * planet.surface_gravity.value / 1000.0 * surface * surface;
    State state = fixture(planet, 250000);
    const double radius = length(state.position);
    state.velocity = {0, std::sqrt(mu / radius), 0};
    const auto original = state;
    malformed(planet, state);
    for (unsigned count : {2U, 257U}) {
      const GuidanceRequest request{GuidanceMode::orbit, 1, count};
      const auto shortest = flight_guidance(planet, state, request);
      finite_result(shortest, request);
      check(shortest.coast_samples.size() == count &&
                shortest.coast_samples.back().seconds == 1,
            "minimum horizon must retain distinct finite sample timestamps");
    }
    for (const auto mode :
         {GuidanceMode::none, GuidanceMode::orbit,
          GuidanceMode::return_to_surface, GuidanceMode::escape}) {
      const GuidanceRequest request{mode};
      const auto result = flight_guidance(planet, state, request);
      finite_result(result, request);
      check(state == original, "guidance mutated authoritative state");
      check(result.mode == mode, "guidance mode mismatch");
      if (mode == GuidanceMode::none) {
        check(result.coast_samples.empty() && !result.target_available &&
                  result.termination == GuidanceTermination::disabled,
              "disabled guidance produced a path or target");
        continue;
      }
      check(result.coast_samples.size() == request.sample_count &&
                result.termination == GuidanceTermination::horizon,
            "circular coast unexpectedly truncated");
      for (const auto& sample : result.coast_samples)
        check(std::abs(length(sample.position) - radius) < .01,
              "vacuum circular coast drifts more than one centimetre");
      if (mode == GuidanceMode::orbit) {
        check(result.cue == GuidanceCue::orbit_established &&
                  result.orbit.clear_orbit,
              "clear circular orbit not recognized");
        check(result.target_position == state.position &&
                  result.delta_velocity_available &&
                  length(result.delta_velocity) < 1e-8,
              "circular orbit target invented a burn");
      } else if (mode == GuidanceMode::return_to_surface) {
        check(result.cue == GuidanceCue::return_reference &&
                  result.target_velocity.y < state.velocity.y &&
                  result.reference_periapsis_radius_metres <= radius,
              "return reference fails to lower ideal periapsis");
        auto reference = state;
        reference.velocity = result.target_velocity;
        const auto reference_orbit = orbit_info(planet, reference);
        check(std::abs(reference_orbit.periapsis + surface -
                       result.reference_periapsis_radius_metres) < 1e-5,
              "return reference speed does not match its stated periapsis");
      } else {
        check(result.cue == GuidanceCue::escape_reference &&
                  std::abs(dot(result.target_velocity, result.target_velocity) /
                               2 -
                           mu / radius) < 1e-7,
              "escape reference is not the zero-energy speed threshold");
      }
    }
    // Forecast must not consume thrust settings, input assistance or attitude.
    state.main = 1;
    state.assist = false;
    const auto coast = flight_guidance(planet, state, {GuidanceMode::orbit});
    const auto baseline =
        flight_guidance(planet, original, {GuidanceMode::orbit});
    check(coast.coast_samples.back().position ==
              baseline.coast_samples.back().position,
          "vacuum coast secretly predicted current thrust/assist");
    state = original;
    state.velocity = {0, original.velocity.y * 1.5, 0};
    auto result = flight_guidance(planet, state, {GuidanceMode::escape});
    finite_result(result, {GuidanceMode::escape});
    check(!result.orbit.bound &&
              result.cue == GuidanceCue::escape_energy_reached &&
              result.termination == GuidanceTermination::horizon,
          "unbound coast confused with bound orbital loop");
    state.velocity = {0, std::sqrt(2.0 * mu / radius), 0};
    result = flight_guidance(planet, state, {GuidanceMode::escape});
    finite_result(result, {GuidanceMode::escape});
    check(result.termination == GuidanceTermination::horizon,
          "near-parabolic energy produced singular coast propagation");
    state = original;
    state.angular_model = 2;
    state.orientation = RigidOrientation{};
    const auto rigid_original = state;
    result = flight_guidance(planet, state, {GuidanceMode::orbit, 1800, 257});
    finite_result(result, {GuidanceMode::orbit, 1800, 257});
    check(
        state == rigid_original && result.coast_samples.size() == 257,
        "bounded guidance altered native quaternion state or sample capacity");
    state = original;
    state.velocity = {-1000, 0, 0};
    result = flight_guidance(planet, state, {GuidanceMode::return_to_surface});
    finite_result(result, {GuidanceMode::return_to_surface});
    check(result.radial_degenerate && result.coast_samples.size() < 129 &&
              result.termination != GuidanceTermination::horizon,
          "radial fall should truncate, not invent an orbital plane or land");
    const double boundary = surface + result.orbit.atmosphere_edge;
    check(std::abs(length(result.coast_samples.back().position) - boundary) <
              1e-5,
          "coast truncation overshot environmental boundary");
    state = fixture(planet, 10);
    result = flight_guidance(planet, state, {GuidanceMode::orbit});
    finite_result(result, {GuidanceMode::orbit});
    check(!result.delta_velocity_available && result.delta_velocity == V{} &&
              result.target_position != state.position,
          "different-radius target exposed misleading local delta velocity");
    if (result.orbit.atmosphere_edge > 10)
      check(result.coast_samples.size() == 1 &&
                result.cue == GuidanceCue::climb_above_atmosphere &&
                result.termination == GuidanceTermination::atmosphere,
            "in-atmosphere state received dishonest vacuum forecast");
    if (result.orbit.atmosphere_edge > 10) {
      result =
          flight_guidance(planet, state, {GuidanceMode::return_to_surface});
      check(result.cue == GuidanceCue::atmosphere_model_limit &&
                !result.delta_velocity_available &&
                result.delta_velocity == V{},
            "return through atmosphere was told to climb or use a vacuum burn "
            "delta");
    }
    state = fixture(planet, -1);
    result = flight_guidance(planet, state, {GuidanceMode::orbit});
    check(result.termination == GuidanceTermination::spherical_surface &&
              !result.target_available && result.coast_samples.size() == 1,
          "below reference sphere received a safe flight/landing claim");
    // True radial launch, including projected-nose degeneracy, stays finite.
    state = original;
    state.right = {0, 1, 0};
    state.up = {0, 0, 1};
    state.back = {1, 0, 0};
    state.velocity = {15000, 0, 0};
    result = flight_guidance(planet, state, {GuidanceMode::escape, 1800, 257});
    finite_result(result, {GuidanceMode::escape, 1800, 257});
    check(result.radial_degenerate && !result.orbit.bound &&
              result.target_available,
          "radial unbound state mishandled");
    // Escape energy alone says nothing about whether the forward path is safe.
    state.velocity = {-15000, 0, 0};
    result = flight_guidance(planet, state, {GuidanceMode::escape});
    finite_result(result, {GuidanceMode::escape});
    check(!result.orbit.bound &&
              result.cue == GuidanceCue::escape_energy_reached &&
              (result.termination == GuidanceTermination::atmosphere ||
               result.termination == GuidanceTermination::spherical_surface),
          "inward unbound trajectory must report environmental termination "
          "despite escape energy");
    state = original;
    state.position = {1e10, 0, 0};
    state.velocity = {100000, 0, 0};
    result = flight_guidance(planet, state, {GuidanceMode::escape, 1800, 257});
    finite_result(result, {GuidanceMode::escape, 1800, 257});
    check(result.termination == GuidanceTermination::numerical_limit &&
              result.coast_samples.size() == 1,
          "coast escaping the lab numeric envelope must truncate, not clamp or "
          "overflow");
    bool airless_tested = false;
    for (std::uint64_t seed = 0; seed < 1024 && !airless_tested; ++seed) {
      const auto airless = generate_planet_descriptor(Seed{seed});
      if (airless.atmosphere_pressure.value != 0) continue;
      state = fixture(airless, 1000);
      state.velocity = {-100, 0, 0};
      result =
          flight_guidance(airless, state, {GuidanceMode::return_to_surface});
      finite_result(result, {GuidanceMode::return_to_surface});
      check(result.termination == GuidanceTermination::spherical_surface &&
                std::abs(length(result.coast_samples.back().position) -
                         airless.radius.value * 1000.0) < 1e-5,
            "airless radial path did not stop at reference sphere");
      state = fixture(airless, 0);
      result =
          flight_guidance(airless, state, {GuidanceMode::return_to_surface});
      finite_result(result, {GuidanceMode::return_to_surface});
      check(result.termination == GuidanceTermination::spherical_surface &&
                !result.target_available && !result.delta_velocity_available &&
                result.coast_samples.size() == 1,
            "exact airless surface cannot be classified as clear flight");
      airless_tested = true;
    }
    check(airless_tested, "airless generated fixture missing");
    std::cout << "Read-only orbital guidance: all contracts pass\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
