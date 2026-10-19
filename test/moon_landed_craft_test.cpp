#include "apsis_drift/moon_body_dynamics.hpp"
#include "apsis_drift/moon_landed_craft.hpp"
#include "moon_contact_fixture.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace apsis_drift;
using test_fixture::require_moon_contact;
int checks{}, failures{};
auto check(bool value, const char* message) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
auto lifecycle(const test_fixture::MoonContactFixture& f) -> void {
  const RigidBodyWorldContext context{f.moon};
  const auto hash =
      require_moon_contact(rigid_body_state_checksum(context, f.source));
  const auto source_bytes =
      require_moon_contact(encode_rigid_body_state_json(context, f.source));
  const auto gear = prepare_moon_landed_craft(f.moon, f.source, false, hash);
  check(!gear && gear.error() == LandedCraftError::gear_not_deployed,
        "Landing refuses missing gear before contact");
  const auto stale =
      prepare_moon_landed_craft(f.moon, f.source, true, hash ^ 1);
  check(!stale && stale.error() == LandedCraftError::stale_source,
        "Landing refuses stale source checksum");
  check(!prepare_moon_landed_craft(f.moon, f.source, true, hash, 2),
        "Unknown landing terrain recipe refuses");
  auto forged = f.moon;
  ++forged.spin_period_ticks;
  check(!prepare_moon_landed_craft(forged, f.source, true, hash),
        "Forged moon physical source cannot prepare anchor");
  const auto prepared = prepare_moon_landed_craft(f.moon, f.source, true, hash);
  std::cout << "Fixed moon anchor: prepared=" << bool(prepared) << " refusal="
            << (prepared ? -1 : static_cast<int>(prepared.error())) << '\n';
  check(
      prepared.has_value(),
      "Preselected genuine source certifies all pads/hull and prepares anchor");
  if (!prepared) return;
  const auto anchor = *prepared;
  check(anchor.fixed.frame.kind == RigidFrameKind::body_fixed &&
            anchor.fixed.frame.body == f.moon.body &&
            anchor.fixed.linear_velocity_metres_per_second == RigidVector3{} &&
            anchor.fixed.angular_velocity_radians_per_second == RigidVector3{},
        "Anchor owns exact moon fixed frame and zero relative idle velocity");
  const auto snapshot =
      require_moon_contact(MoonContactSnapshot::create(f.moon, f.source, true));
  check(anchor.fixed.position_metres ==
                snapshot.provenance().fixed_query_state.position_metres &&
            anchor.fixed.orientation ==
                snapshot.provenance().fixed_query_state.orientation,
        "Landing preserves supported source position/orientation instead of "
        "clamping to terrain");
  const auto before =
      require_moon_contact(encode_rigid_body_state_json(context, anchor.fixed));
  for (const SimulationTick delta : {0ULL, 1ULL, 1200ULL, 10000000ULL}) {
    const auto tick = anchor.fixed.tick + delta;
    check(validate_moon_landed_craft(f.moon, anchor, tick).has_value(),
          "Anchored support/hull revalidate under actual same-body spin");
    const auto resolved =
        require_moon_contact(resolve_moon_landed_craft(f.moon, anchor, tick));
    auto fixed = anchor.fixed;
    fixed.tick = tick;
    const auto expected = require_moon_contact(
        reframe_rigid_body(context, fixed,
                           {{RigidFrameKind::body_relative_inertial,
                             context.system.id,
                             {},
                             {},
                             f.moon.body},
                            tick},
                           f.moon));
    check(
        require_moon_contact(encode_rigid_body_state_json(context, resolved)) ==
            require_moon_contact(
                encode_rigid_body_state_json(context, expected)),
        "Idle resolution applies actual moon rotation/spin velocity exactly "
        "once");
    const auto released =
        require_moon_contact(release_moon_landed_craft(f.moon, anchor, tick));
    check(
        require_moon_contact(encode_rigid_body_state_json(context, released)) ==
            require_moon_contact(
                encode_rigid_body_state_json(context, resolved)),
        "Liftoff release preserves actual pose/velocity without synthetic "
        "impulse");
    const auto gravity =
        require_moon_contact(evaluate_moon_body_gravity(context, released));
    const long double r =
        std::hypot(released.position_metres.x, released.position_metres.y,
                   released.position_metres.z);
    check(std::abs(
              std::hypot(gravity.acceleration_metres_per_second_squared.x,
                         gravity.acceleration_metres_per_second_squared.y,
                         gravity.acceleration_metres_per_second_squared.z) -
              gravity.gravitational_parameter_metres_cubed_per_second_squared /
                  (r * r)) < 1e-13L,
          "Release uses genuine local gravity even in dry negative relief");
  }
  for (unsigned field = 0; field < 8; ++field) {
    auto bad = anchor;
    if (field == 0)
      ++bad.version;
    else if (field == 1)
      ++bad.terrain_version;
    else if (field == 2)
      ++bad.rotation_version;
    else if (field == 3)
      ++bad.fixed.frame.body->value;
    else if (field == 4)
      bad.fixed.linear_velocity_metres_per_second.x = 0.1;
    else if (field == 5)
      bad.fixed.angular_velocity_radians_per_second.y = 0.1;
    else if (field == 6)
      bad.fixed.position_metres.x = NAN;
    else
      bad.fixed.frame.body->kind = BodyKind::planet;
    check(!resolve_moon_landed_craft(f.moon, bad, bad.fixed.tick) &&
              !validate_moon_landed_craft(f.moon, bad, bad.fixed.tick) &&
              !release_moon_landed_craft(f.moon, bad, bad.fixed.tick),
          "Malformed anchor cannot resolve, hydrate support or release");
  }
  check(!resolve_moon_landed_craft(f.moon, anchor, anchor.fixed.tick - 1) &&
            !resolve_moon_landed_craft(
                f.moon, anchor, std::numeric_limits<SimulationTick>::max() - 1),
        "Anchor clock is monotonic and preserves reserved terminal tick");
  auto other_system =
      require_moon_contact(generate_physical_origin_system(Seed{43}, 2));
  const auto other_catalog =
      require_moon_contact(generate_secondary_body_catalog(other_system));
  const auto other = require_moon_contact(generate_physical_moon_descriptor(
      other_catalog, other_catalog.bodies[0].node.id));
  check(!resolve_moon_landed_craft(other, anchor, anchor.fixed.tick),
        "Foreign genuine moon cannot adopt matching-format anchor");
  check(require_moon_contact(
            encode_rigid_body_state_json(context, anchor.fixed)) == before &&
            require_moon_contact(encode_rigid_body_state_json(
                context, f.source)) == source_bytes,
        "Prepare, idle, validation and release leave original anchor/source "
        "immutable");
}
} // namespace
int main() {
  try {
    lifecycle(test_fixture::make_moon_contact_fixture());
  } catch (const std::exception& error) {
    check(false, error.what());
  }
  std::cout << checks << " moon landed checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
