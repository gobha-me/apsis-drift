#include "apsis_drift/freedom_system_network.hpp"

#include <algorithm>
#include <array>

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
    -> std::array<UniversePositionMetres, 3> {
  switch (direction) {
    case UniverseAxisDirection::positive_x:
      return {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    case UniverseAxisDirection::negative_x:
      return {{{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    case UniverseAxisDirection::positive_y:
      return {{{0, 1, 0}, {0, 0, 1}, {1, 0, 0}}};
    case UniverseAxisDirection::negative_y:
      return {{{0, -1, 0}, {0, 0, 1}, {1, 0, 0}}};
    case UniverseAxisDirection::positive_z:
      return {{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}};
    case UniverseAxisDirection::negative_z:
      return {{{0, 0, -1}, {1, 0, 0}, {0, 1, 0}}};
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
  if (!freedom_system_network_version_supported(version))
    return std::unexpected{Error::unsupported_version};
  const auto legacy = generate_first_intersystem_identities(universe_seed);
  const auto route = generate_first_universe_route(universe_seed);
  if (!validate_first_universe_route(route))
    return std::unexpected{Error::invalid_geometry};
  const auto original_seed =
      derive_seed(universe_seed, SeedDomain::freedom_system_network,
                  kFreedomSystemNetworkVersion);
  const auto uncharted_seed =
      derive_seed(universe_seed, SeedDomain::freedom_uncharted_systems,
                  kFreedomUnchartedSystemNetworkVersion);
  const auto seed =
      version == kFreedomSystemNetworkVersion ? original_seed
      : version == kFreedomUnchartedSystemNetworkVersion
          ? uncharted_seed
          : derive_seed(universe_seed, SeedDomain::freedom_outer_systems,
                        kFreedomOuterSystemNetworkVersion);
  FreedomSystemNetwork result{version, universe_seed, seed, {}};
  result.nodes.resize(version == kFreedomSystemNetworkVersion
                          ? kFreedomSystemNetworkSize
                      : version == kFreedomUnchartedSystemNetworkVersion
                          ? kFreedomUnchartedSystemNetworkSize
                          : kFreedomOuterSystemNetworkSize);
  result.nodes[0] = {0, legacy.origin_system_seed, legacy.origin_system,
                     route.origin_position};
  result.nodes[1] = {1, legacy.target_system_seed, legacy.target_system,
                     route.destination_position};
  const auto frame = axes(route.direction);
  const auto h = static_cast<std::int64_t>(route.distance_light_seconds / 2);
  const auto q = static_cast<std::int64_t>(route.distance_light_seconds / 4);
  for (std::uint32_t ordinal = 2; ordinal < result.nodes.size(); ++ordinal) {
    const auto system_seed = derive_seed(
        ordinal < kFreedomSystemNetworkSize            ? original_seed
        : ordinal < kFreedomUnchartedSystemNetworkSize ? uncharted_seed
                                                       : seed,
        SeedDomain::system, ordinal);
    const std::int64_t side = ordinal % 2 == 0 ? 1 : -1;
    const auto perpendicular =
        frame[ordinal < kFreedomSystemNetworkSize ? 1 : 2];
    const auto along = ordinal < kFreedomUnchartedSystemNetworkSize ? h : h + q;
    const auto across = ordinal < kFreedomUnchartedSystemNetworkSize ? q : h;
    const UniversePositionMetres point{
        (along * frame[0].x + side * across * perpendicular.x) *
            kMetresPerLightSecond,
        (along * frame[0].y + side * across * perpendicular.y) *
            kMetresPerLightSecond,
        (along * frame[0].z + side * across * perpendicular.z) *
            kMetresPerLightSecond};
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
  // H-A/H-B/N-A/N-B <D, A-B<=D/2, and H-N==D. Recipe2's remaining-axis
  // nodes have the same h/+/-q offsets: their mutual links are2q, and links
  // across the two perpendicular axes are sqrt(2)*q. Thus all15 remain<=D.
  // Recipe3 appends C+/C- at (h+q)e +/-h*r. H-C squared is at most
  // (3D/4)^2+(D/2)^2 <D^2; N-C is bounded by (D-h)^2+h^2 <=D^2.
  // Links to the old perpendicular-axis nodes have squares2q^2+h^2 or
  // q^2+(h+/-q)^2, each <=D^2; C+-C- is2h<=D. These bounds use the
  // integer floors h and q, so all28 links retain the old ceil bound.
  // The minimum D>=172800ls also separates all nodes beyond two local systems.
  return result;
}
auto validate_freedom_system_network(const FreedomSystemNetwork& network)
    -> std::expected<void, Error> {
  if (!freedom_system_network_version_supported(network.version))
    return std::unexpected{Error::unsupported_version};
  const auto size = network.version == kFreedomSystemNetworkVersion
                        ? kFreedomSystemNetworkSize
                    : network.version == kFreedomUnchartedSystemNetworkVersion
                        ? kFreedomUnchartedSystemNetworkSize
                        : kFreedomOuterSystemNetworkSize;
  if (network.nodes.size() != size)
    return std::unexpected{Error::invalid_network};
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
