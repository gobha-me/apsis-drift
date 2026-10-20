#include "apsis_drift/physical_minor.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace apsis_drift {
namespace {
using Error = PhysicalMinorError;
static_assert(kPhysicalMinorRecipeVersion == 1 &&
              kSecondaryBodyCatalogVersion == 1 && kBodyIdentityVersion == 1);

auto same_bits(double a, double b) -> bool {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
} // namespace

auto generate_physical_minor_descriptor(const SecondaryBodyCatalog& owner,
                                        BodyId body, std::uint32_t version)
    -> std::expected<PhysicalMinorDescriptor, Error> {
  if (version != kPhysicalMinorRecipeVersion)
    return std::unexpected{Error::unsupported_version};
  if (body.version != kBodyIdentityVersion || body.kind != BodyKind::minor)
    return std::unexpected{Error::invalid_body};
  if (!validate_secondary_body_catalog(owner))
    return std::unexpected{Error::invalid_catalog};
  const auto found = std::ranges::find_if(
      owner.bodies, [body](const SecondaryBodyDescriptor& item) {
        return item.node.id == body;
      });
  if (found == owner.bodies.end()) return std::unexpected{Error::invalid_body};
  // Full catalog validation authenticates this minor's parent, orbit, radius,
  // period, epoch and tint before selecting any physical recipe extension.
  return PhysicalMinorDescriptor{
      .version = version,
      .catalog = owner,
      .body = body,
      .parent = *found->node.parent,
      .radius_metres = found->radius_metres,
      .spin_period_ticks = found->spin_period_ticks,
      .epoch_spin_phase_turns = found->epoch_spin_phase_turns,
      .tint = found->tint,
  };
}

auto validate_physical_minor_descriptor(const PhysicalMinorDescriptor& value)
    -> std::expected<void, Error> {
  const auto expected = generate_physical_minor_descriptor(
      value.catalog, value.body, value.version);
  if (!expected) return std::unexpected{expected.error()};
  if (value != *expected ||
      !same_bits(value.density_kilograms_per_cubic_metre,
                 expected->density_kilograms_per_cubic_metre) ||
      !same_bits(value.gravitational_constant_si,
                 expected->gravitational_constant_si) ||
      !same_bits(value.spin_pole_system.x, expected->spin_pole_system.x) ||
      !same_bits(value.spin_pole_system.y, expected->spin_pole_system.y) ||
      !same_bits(value.spin_pole_system.z, expected->spin_pole_system.z))
    return std::unexpected{Error::descriptor_mismatch};
  return {};
}

auto resolve_physical_minor_properties(const PhysicalMinorDescriptor& value)
    -> std::expected<PhysicalMinorProperties, Error> {
  if (const auto valid = validate_physical_minor_descriptor(value); !valid)
    return std::unexpected{valid.error()};
  // Recipe1 operation order is compatibility data. Do not substitute pow(),
  // a randomized density, a planet mass model or independently rounded GM.
  const double radius = static_cast<double>(value.radius_metres);
  const double radius_squared = radius * radius;
  const double radius_cubed = radius_squared * radius;
  const double volume = (4.0 * std::numbers::pi_v<double> / 3.0) * radius_cubed;
  const double mass = value.density_kilograms_per_cubic_metre * volume;
  const double mu = value.gravitational_constant_si * mass;
  const double gravity = mu / radius_squared;
  if (!std::isfinite(mass) || mass <= 0 || !std::isfinite(mu) || mu <= 0 ||
      !std::isfinite(gravity) || gravity <= 0)
    return std::unexpected{Error::unsafe_arithmetic};
  return PhysicalMinorProperties{value, mass, mu, gravity};
}
} // namespace apsis_drift
