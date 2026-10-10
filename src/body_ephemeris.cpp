#include "apsis_drift/body_ephemeris.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <type_traits>

#include <nlohmann/json.hpp>

#include "local_system_internal.hpp"

namespace apsis_drift {
namespace {
using Error = BodyEphemerisError;
using Json = nlohmann::json;
constexpr std::array<std::string_view, 4> names{"star", "planet", "moon",
                                                "minor"};

auto valid_id(BodyId id) -> bool {
  return id.version == kBodyIdentityVersion &&
         static_cast<unsigned>(id.kind) < names.size();
}
auto projected(const LocalSystemDescriptor& catalog, std::uint32_t ephemeris)
    -> std::vector<HierarchyBody> {
  const auto star = body_id(catalog.star.id);
  std::vector<HierarchyBody> result{{star, {}, {}}};
  for (const auto& planet : catalog.planets) {
    const auto& orbit = planet.orbit;
    result.push_back(
        {body_id(planet.descriptor.id), star,
         CircularBodyOrbit{
             kBodyHierarchyVersion, ephemeris, orbit.radius_kilometres,
             orbit.period_ticks, orbit.epoch_phase_turns,
             orbit.inclination_microdegrees, orbit.ascending_node_turns}});
  }
  std::ranges::sort(result, {}, &HierarchyBody::id);
  return result;
}
struct Prepared {
  std::vector<HierarchyBody> bodies;
  std::vector<std::optional<std::size_t>> parents;
  std::vector<std::size_t> depths;
};
auto prepare(const BodyHierarchy& hierarchy) -> std::expected<Prepared, Error> {
  if (hierarchy.version != kBodyHierarchyVersion)
    return std::unexpected{Error::unsupported_version};
  if (hierarchy.bodies.empty() ||
      hierarchy.bodies.size() > kMaximumHierarchyBodies)
    return std::unexpected{Error::oversized_hierarchy};
  Prepared result{hierarchy.bodies, {}, {}};
  std::ranges::sort(result.bodies, {}, &HierarchyBody::id);
  const auto count = result.bodies.size();
  result.parents.resize(count);
  result.depths.resize(count);
  std::size_t roots{};
  for (std::size_t i = 0; i < count; ++i) {
    const auto& node = result.bodies[i];
    if (!valid_id(node.id) || (node.parent && !valid_id(*node.parent)))
      return std::unexpected{Error::invalid_identity};
    if (i && result.bodies[i - 1].id == node.id)
      return std::unexpected{Error::duplicate_identity};
    if (!node.parent) {
      if (node.id.kind != BodyKind::star || node.orbit)
        return std::unexpected{Error::invalid_root};
      ++roots;
      continue;
    }
    if (node.id.kind == BodyKind::star || !node.orbit)
      return std::unexpected{Error::invalid_root};
    if (*node.parent == node.id) return std::unexpected{Error::cycle};
    const auto parent = std::ranges::lower_bound(result.bodies, *node.parent,
                                                 {}, &HierarchyBody::id);
    if (parent == result.bodies.end() || parent->id != *node.parent)
      return std::unexpected{Error::missing_parent};
    result.parents[i] =
        static_cast<std::size_t>(parent - result.bodies.begin());
    const auto& orbit = *node.orbit;
    if (orbit.version != kBodyHierarchyVersion ||
        (orbit.ephemeris_version != kAnalyticEphemerisVersion &&
         orbit.ephemeris_version != kContinuousAnalyticEphemerisVersion))
      return std::unexpected{Error::unsupported_version};
    if (!orbit.radius_kilometres || !orbit.period_ticks ||
        orbit.period_ticks > (std::uint64_t{1} << 53) - 1 ||
        orbit.inclination_microdegrees < -180'000'000 ||
        orbit.inclination_microdegrees > 180'000'000)
      return std::unexpected{Error::invalid_orbit};
  }
  if (roots != 1) return std::unexpected{Error::invalid_root};
  for (std::size_t i = 0; i < count; ++i) {
    std::array<bool, kMaximumHierarchyBodies> seen{};
    std::optional<std::size_t> current{i};
    std::size_t depth{};
    while (current) {
      if (seen[*current]) return std::unexpected{Error::cycle};
      seen[*current] = true;
      ++depth;
      current = result.parents[*current];
    }
    if (depth > kMaximumBodyHierarchyDepth)
      return std::unexpected{Error::excessive_depth};
    result.depths[i] = depth;
  }
  if (hierarchy.catalog) {
    const bool matches = std::visit(
        [&](const auto& source) {
          if (!validate_local_system(source)) return false;
          if constexpr (std::is_same_v<std::decay_t<decltype(source)>,
                                       LocalSystemDescriptor>)
            return hierarchy.system == source.id &&
                   result.bodies ==
                       projected(source, kAnalyticEphemerisVersion);
          else
            return hierarchy.system == source.catalog.id &&
                   result.bodies ==
                       projected(source.catalog, source.ephemeris_version);
        },
        *hierarchy.catalog);
    if (!matches) return std::unexpected{Error::invalid_catalog};
  }
  return result;
}
auto decimal(const Json& value) -> std::optional<std::uint64_t> {
  if (!value.is_string()) return {};
  const auto& text = value.get_ref<const std::string&>();
  if (text.empty() || text.size() > 20 ||
      (text.size() > 1 && text.front() == '0'))
    return {};
  std::uint64_t result{};
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    return {};
  return result;
}
auto finite(SystemPositionMetres value) -> bool {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}
auto finite(SystemVelocityMetresPerSecond value) -> bool {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}
} // namespace

auto body_id(StarId id) noexcept -> BodyId {
  return {kBodyIdentityVersion, BodyKind::star, id.value};
}
auto body_id(PlanetId id) noexcept -> BodyId {
  return {kBodyIdentityVersion, BodyKind::planet, id.value};
}
auto planet_id(BodyId id) -> std::expected<PlanetId, Error> {
  if (!valid_id(id) || id.kind != BodyKind::planet)
    return std::unexpected{Error::invalid_identity};
  return PlanetId{id.value};
}
auto make_body_hierarchy(const LocalSystemDescriptor& source)
    -> std::expected<BodyHierarchy, Error> {
  if (!validate_local_system(source))
    return std::unexpected{Error::invalid_catalog};
  return BodyHierarchy{kBodyHierarchyVersion, source.id,
                       projected(source, kAnalyticEphemerisVersion), source};
}
auto make_body_hierarchy(const PhysicalLocalSystem& source)
    -> std::expected<BodyHierarchy, Error> {
  if (!validate_local_system(source))
    return std::unexpected{Error::invalid_catalog};
  return BodyHierarchy{kBodyHierarchyVersion, source.catalog.id,
                       projected(source.catalog, source.ephemeris_version),
                       source};
}
auto validate_body_hierarchy(const BodyHierarchy& hierarchy)
    -> std::expected<void, Error> {
  const auto result = prepare(hierarchy);
  if (!result) return std::unexpected{result.error()};
  return {};
}
auto resolve_body_ephemerides(const BodyHierarchy& hierarchy,
                              EphemerisQueryTime time)
    -> std::expected<std::vector<BodyEphemeris>, Error> {
  if (!std::isfinite(time.sub_tick_fraction) || time.sub_tick_fraction < 0 ||
      time.sub_tick_fraction >= 1)
    return std::unexpected{Error::invalid_time};
  // Preserve the physical owner's reserved tick; legacy/declared pure
  // queries retain their existing full-width modulo domain.
  if (time.tick == std::numeric_limits<SimulationTick>::max() &&
      hierarchy.catalog &&
      std::holds_alternative<PhysicalLocalSystem>(*hierarchy.catalog))
    return std::unexpected{Error::invalid_time};
  const auto prepared = prepare(hierarchy);
  if (!prepared) return std::unexpected{prepared.error()};
  std::vector<BodyEphemeris> result(prepared->bodies.size());
  // Parent depth, then canonical identity; no recursive resolution or map
  // order.
  for (std::size_t depth = 1; depth <= kMaximumBodyHierarchyDepth; ++depth) {
    for (std::size_t i = 0; i < prepared->bodies.size(); ++i) {
      if (prepared->depths[i] != depth) continue;
      const auto& node = prepared->bodies[i];
      auto& out = result[i];
      out.body = node.id;
      out.parent = node.parent;
      if (!node.orbit) continue;
      const auto& orbit = *node.orbit;
      // An internal geometry carrier only; no moon/minor identity is converted
      // to PlanetId and no descriptor is admitted through this kernel.
      PlanetOrbit carrier{};
      carrier.radius_kilometres = orbit.radius_kilometres;
      carrier.period_ticks = orbit.period_ticks;
      carrier.epoch_phase_turns = orbit.epoch_phase_turns;
      carrier.inclination_microdegrees = orbit.inclination_microdegrees;
      carrier.ascending_node_turns = orbit.ascending_node_turns;
      const auto relative = detail::resolve_validated_circular_orbit(
          carrier, time,
          orbit.ephemeris_version == kContinuousAnalyticEphemerisVersion);
      if (!relative) return std::unexpected{Error::unsafe_arithmetic};
      out.relative_position = out.position = relative->position;
      out.relative_velocity = out.velocity = relative->velocity;
      out.cycle_tick = relative->cycle_tick;
      out.phase_radians = relative->phase_radians;
      const auto parent = *prepared->parents[i];
      // A direct root child must preserve legacy signed-zero bits as well.
      if (prepared->depths[parent] > 1) {
        const auto& base = result[parent];
        out.position = {base.position.x + out.position.x,
                        base.position.y + out.position.y,
                        base.position.z + out.position.z};
        out.velocity = {base.velocity.x + out.velocity.x,
                        base.velocity.y + out.velocity.y,
                        base.velocity.z + out.velocity.z};
      }
      if (!finite(out.position) || !finite(out.velocity))
        return std::unexpected{Error::unsafe_arithmetic};
    }
  }
  return result;
}
auto validate_body_target(const BodyHierarchy& hierarchy, BodyTarget target)
    -> std::expected<void, Error> {
  const auto prepared = prepare(hierarchy);
  if (!prepared) return std::unexpected{prepared.error()};
  if (target.system != hierarchy.system || !valid_id(target.body) ||
      std::ranges::find(prepared->bodies, target.body, &HierarchyBody::id) ==
          prepared->bodies.end())
    return std::unexpected{Error::invalid_target};
  return {};
}
auto encode_body_target(BodyTarget target)
    -> std::expected<std::string, Error> {
  if (!valid_id(target.body)) return std::unexpected{Error::invalid_identity};
  return Json{{"version", target.body.version},
              {"system", std::to_string(target.system.value)},
              {"kind", names[static_cast<std::size_t>(target.body.kind)]},
              {"identity", std::to_string(target.body.value)}}
      .dump();
}
auto decode_body_target(std::string_view encoded)
    -> std::expected<BodyTarget, Error> {
  if (encoded.empty() || encoded.size() > 256)
    return std::unexpected{Error::invalid_encoding};
  bool invalid{};
  std::set<std::string> keys;
  const auto document = Json::parse(
      encoded.begin(), encoded.end(),
      [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 1) invalid = true;
        if (event == Json::parse_event_t::key &&
            !keys.insert(value.get<std::string>()).second)
          invalid = true;
        return true;
      },
      false);
  if (invalid || !document.is_object() || document.size() != 4 ||
      !document.contains("version") ||
      !document["version"].is_number_integer() ||
      document["version"] != kBodyIdentityVersion ||
      !document.contains("system") || !document.contains("identity") ||
      !document.contains("kind") || !document["kind"].is_string())
    return std::unexpected{Error::invalid_encoding};
  const auto system = decimal(document["system"]);
  const auto identity = decimal(document["identity"]);
  const auto kind =
      std::ranges::find(names, document["kind"].get_ref<const std::string&>());
  if (!system || !identity || kind == names.end())
    return std::unexpected{Error::invalid_encoding};
  return BodyTarget{SystemId{*system},
                    {kBodyIdentityVersion,
                     static_cast<BodyKind>(kind - names.begin()), *identity}};
}

} // namespace apsis_drift
