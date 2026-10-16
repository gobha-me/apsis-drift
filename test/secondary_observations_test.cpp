#include "apsis_drift/secondary_observations.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
auto check(bool value, const char* message) -> void {
  if (!value) throw std::runtime_error{message};
}
} // namespace
auto main() -> int {
  using namespace apsis_drift;
  try {
    const auto owner = generate_physical_origin_system(Seed{42}, 2);
    check(owner.has_value(), "owner generation failed");
    const auto catalog = generate_secondary_body_catalog(*owner);
    check(catalog.has_value(), "secondary catalog generation failed");
    const auto hierarchy = make_body_hierarchy(*catalog);
    const auto bodies = resolve_body_ephemerides(*hierarchy, {12345, 0});
    check(bodies.has_value(), "actual body clock failed");
    const auto moon_id = catalog->bodies[0].node.id;
    const auto& moon =
        *std::ranges::find(*bodies, moon_id, &BodyEphemeris::body);
    const auto find_moon =
        [&](const auto& items) -> const SecondaryBodyObservation& {
      return *std::ranges::find(items, moon_id, &SecondaryBodyObservation::id);
    };
    const SystemPositionMetres observer{moon.position.x, moon.position.y,
                                        moon.position.z + 500'000'000};
    const auto observed =
        observe_secondary_bodies(*catalog, observer, {12345, 0});
    check(observed.has_value(), "valid optical query refused");
    const auto& actual = find_moon(*observed);
    check(actual.status == SecondaryObservationStatus::visible &&
              actual.direction == RigidVector3{0, 0, -1} &&
              actual.angular_radius ==
                  std::asin(catalog->bodies[0].radius_metres / 500'000'000.0) &&
              observed->front().distance_metres >=
                  observed->back().distance_metres,
          "optical query enlarged size or lost direction/order");
    check(
        std::abs(std::hypot(actual.light_direction.x, actual.light_direction.y,
                            actual.light_direction.z) -
                 1) < 1e-14 &&
            std::isfinite(actual.spin_radians) &&
            std::abs(actual.spin_radians) <= std::numbers::pi,
        "light/spin projection is invalid");
    check(observe_secondary_bodies(*catalog, observer, {12345, 0}) == observed,
          "read-only observation changed its deterministic result");
    const auto inside =
        observe_secondary_bodies(*catalog, moon.position, {12345, 0});
    check(inside &&
              find_moon(*inside).status ==
                  SecondaryObservationStatus::close_unavailable &&
              find_moon(*inside).angular_radius == 0,
          "inside-body query implied a drawable surface");
    const auto distant =
        observe_secondary_bodies(*catalog, {1e14, 1e14, 1e14}, {12345, 0});
    check(distant && find_moon(*distant).status ==
                         SecondaryObservationStatus::unresolved,
          "unresolved body became an enlarged physical silhouette");
    const auto parent_id = *catalog->bodies[0].node.parent;
    const auto& parent =
        *std::ranges::find(*bodies, parent_id, &BodyEphemeris::body);
    const SystemPositionMetres behind{parent.position.x * 2 - moon.position.x,
                                      parent.position.y * 2 - moon.position.y,
                                      parent.position.z * 2 - moon.position.z};
    const auto occluded =
        observe_secondary_bodies(*catalog, behind, {12345, 0});
    check(occluded && find_moon(*occluded).status ==
                          SecondaryObservationStatus::occluded,
          "parent planet did not occlude its actual moon");
    for (const double bad : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(), 1e16})
      check(!observe_secondary_bodies(*catalog, {bad, 0, 0}, {12345, 0}),
            "invalid observer arithmetic accepted");
    check(!observe_secondary_bodies(
              *catalog, observer,
              {std::numeric_limits<SimulationTick>::max(), 0}) &&
              !observe_secondary_bodies(
                  *catalog, observer,
                  {12345, std::numeric_limits<double>::quiet_NaN()}),
          "invalid clock accepted");
    auto forged = *catalog;
    ++forged.bodies[0].radius_metres;
    check(!observe_secondary_bodies(forged, observer, {12345, 0}),
          "forged optical geometry accepted");
    std::cout << "secondary observations contract passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
