#include "apsis_drift/freedom_system_network.hpp"

#include <algorithm>

namespace apsis_drift {
namespace {
using Error = FreedomSystemNetworkError;
constexpr auto kMaximumDistance =
    kMaximumFirstRouteLightSeconds *
    static_cast<std::uint64_t>(kMetresPerLightSecond);
static_assert(kMaximumDistance < (std::uint64_t{1} << 47U));
// Only this recipe's <=47-bit metre coordinates enter these two-word squares.
// This is a bounded integer-root implementation, not a general wide-arithmetic
// API. No compiler extension, floating-point root or new dependency is used.
struct Square {
  std::uint64_t high{}, low{};
};
auto square(std::uint64_t x) -> Square {
  const auto a = x >> 32U;
  const auto b = x & 0xffffffffULL;
  const auto p = b * b;
  const auto middle = 2 * a * b;
  const auto low = p + (middle << 32U);
  return {a * a + (middle >> 32U) + (low < p ? 1U : 0U), low};
}
auto add(Square a, Square b) -> Square {
  const auto low = a.low + b.low;
  return {a.high + b.high + (low < a.low ? 1U : 0U), low};
}
auto less(Square a, Square b) -> bool {
  return a.high < b.high || (a.high == b.high && a.low < b.low);
}
auto magnitude(std::int64_t value) -> std::uint64_t {
  // Authenticated generated differences are <=kMaximumDistance, never
  // INT64_MIN.
  return static_cast<std::uint64_t>(value < 0 ? -value : value);
}
auto ceiling_distance(UniversePositionMetres d, std::uint64_t bound)
    -> std::uint64_t {
  const auto target = add(add(square(magnitude(d.x)), square(magnitude(d.y))),
                          square(magnitude(d.z)));
  std::uint64_t lower{}, upper{bound};
  // At most47 iterations: every complete link in this recipe is <=old Home-N.
  while (lower < upper) {
    const auto middle = lower + (upper - lower) / 2;
    if (less(square(middle), target))
      lower = middle + 1;
    else
      upper = middle;
  }
  return lower;
}
auto axes(UniverseAxisDirection direction)
    -> std::array<UniversePositionMetres, 2> {
  switch (direction) {
    case UniverseAxisDirection::positive_x: return {{{1, 0, 0}, {0, 1, 0}}};
    case UniverseAxisDirection::negative_x: return {{{-1, 0, 0}, {0, 1, 0}}};
    case UniverseAxisDirection::positive_y: return {{{0, 1, 0}, {0, 0, 1}}};
    case UniverseAxisDirection::negative_y: return {{{0, -1, 0}, {0, 0, 1}}};
    case UniverseAxisDirection::positive_z: return {{{0, 0, 1}, {1, 0, 0}}};
    case UniverseAxisDirection::negative_z: return {{{0, 0, -1}, {1, 0, 0}}};
  }
  return {};
}
auto lookup(const FreedomSystemNetwork& network, SystemId id)
    -> const FreedomSystemNetworkNode* {
  const auto found =
      std::ranges::find(network.nodes, id, &FreedomSystemNetworkNode::system);
  return found == network.nodes.end() ? nullptr : &*found;
}
} // namespace

auto generate_freedom_system_network(Seed universe_seed, std::uint32_t version)
    -> std::expected<FreedomSystemNetwork, Error> {
  if (version != kFreedomSystemNetworkVersion)
    return std::unexpected{Error::unsupported_version};
  const auto legacy = generate_first_intersystem_identities(universe_seed);
  const auto route = generate_first_universe_route(universe_seed);
  if (!validate_first_universe_route(route))
    return std::unexpected{Error::invalid_geometry};
  const auto seed =
      derive_seed(universe_seed, SeedDomain::freedom_system_network, version);
  FreedomSystemNetwork result{version, universe_seed, seed, {}};
  result.nodes[0] = {0, legacy.origin_system_seed, legacy.origin_system,
                     route.origin_position};
  result.nodes[1] = {1, legacy.target_system_seed, legacy.target_system,
                     route.destination_position};
  const auto frame = axes(route.direction);
  const auto h = static_cast<std::int64_t>(route.distance_light_seconds / 2);
  const auto q = static_cast<std::int64_t>(route.distance_light_seconds / 4);
  for (std::uint32_t ordinal = 2; ordinal < 4; ++ordinal) {
    const auto system_seed = derive_seed(seed, SeedDomain::system, ordinal);
    const std::int64_t side = ordinal == 2 ? 1 : -1;
    const UniversePositionMetres point{
        (h * frame[0].x + side * q * frame[1].x) * kMetresPerLightSecond,
        (h * frame[0].y + side * q * frame[1].y) * kMetresPerLightSecond,
        (h * frame[0].z + side * q * frame[1].z) * kMetresPerLightSecond};
    result.nodes[ordinal] = {ordinal, system_seed, SystemId{system_seed.value},
                             point};
  }
  // There is no retry/re-roll that could change seeded identity semantics.
  for (std::size_t i = 0; i < result.nodes.size(); ++i) {
    if (!result.nodes[i].system.value || !result.nodes[i].system_seed.value)
      return std::unexpected{Error::invalid_system};
    for (std::size_t j = 0; j < i; ++j)
      if (result.nodes[i].system == result.nodes[j].system ||
          result.nodes[i].position == result.nodes[j].position)
        return std::unexpected{Error::invalid_network};
  }
  // All coordinates are exact integer light-seconds. h<=D/2 and q<=D/4:
  // H-A/H-B/N-A/N-B <D, A-B<=D/2, and H-N==D. The minimum D>=172800ls
  // also puts every link far outside two local-system boundaries.
  return result;
}
auto validate_freedom_system_network(const FreedomSystemNetwork& network)
    -> std::expected<void, Error> {
  const auto expected =
      generate_freedom_system_network(network.universe_seed, network.version);
  if (!expected) return std::unexpected{expected.error()};
  if (network != *expected) return std::unexpected{Error::invalid_network};
  return {};
}
auto find_freedom_system_network_node(const FreedomSystemNetwork& network,
                                      SystemId id)
    -> std::expected<FreedomSystemNetworkNode, Error> {
  if (const auto valid = validate_freedom_system_network(network); !valid)
    return std::unexpected{valid.error()};
  const auto* node = lookup(network, id);
  if (!node) return std::unexpected{Error::invalid_system};
  return *node;
}
auto resolve_freedom_network_system(const FreedomSystemNetwork& network,
                                    SystemId id, std::uint32_t physical_version)
    -> std::expected<PhysicalLocalSystem, Error> {
  const auto node = find_freedom_system_network_node(network, id);
  if (!node) return std::unexpected{node.error()};
  const auto system =
      node->ordinal == 0
          ? generate_physical_origin_system(network.universe_seed,
                                            physical_version)
          : generate_physical_local_system(node->system_seed, physical_version);
  if (!system || system->catalog.id != node->system)
    return std::unexpected{Error::physical_system_refused};
  return *system;
}
auto resolve_freedom_system_network_route(const FreedomSystemNetwork& network,
                                          SystemId source, SystemId destination)
    -> std::expected<FreedomSystemNetworkRoute, Error> {
  if (const auto valid = validate_freedom_system_network(network); !valid)
    return std::unexpected{valid.error()};
  const auto* from = lookup(network, source);
  const auto* to = lookup(network, destination);
  if (!from || !to || source == destination)
    return std::unexpected{Error::invalid_system};
  const UniversePositionMetres d{to->position.x - from->position.x,
                                 to->position.y - from->position.y,
                                 to->position.z - from->position.z};
  const auto x = magnitude(d.x / kMetresPerLightSecond);
  const auto y = magnitude(d.y / kMetresPerLightSecond);
  const auto z = magnitude(d.z / kMetresPerLightSecond);
  const auto squared_ls = (x * x + y * y) + z * z;
  const auto bound =
      generate_first_universe_route(network.universe_seed).distance_metres;
  return FreedomSystemNetworkRoute{source, destination, d, squared_ls,
                                   ceiling_distance(d, bound)};
}
} // namespace apsis_drift
