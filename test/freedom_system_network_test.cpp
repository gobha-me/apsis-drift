#include "apsis_drift/freedom_system_network.hpp"

#include "apsis_drift/freedom_jump_targeting.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
using namespace apsis_drift;
int checks{}, failures{};
std::uint64_t fingerprint{14695981039346656037ULL};
auto check(bool value, std::string_view message) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <typename T, typename E> auto need(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Fixed network fixture refused");
  return std::move(*value);
}
auto hash(std::uint64_t value) -> void {
  for (unsigned i = 0; i < 8; ++i) {
    fingerprint ^= (value >> (8 * i)) & 255U;
    fingerprint *= 1099511628211ULL;
  }
}
auto absolute(std::int64_t x) -> std::uint64_t {
  return static_cast<std::uint64_t>(x < 0 ? -x : x);
}
// Independent exact distance oracle: base2^16 schoolbook multiplication and
// carry propagation, unlike the producer's two-word 32-bit split. Eight digits
// cover the registered <2^94 sum with ample headroom. No root approximation.
using Digits = std::array<std::uint32_t, 8>;
auto sum_squares(std::array<std::uint64_t, 3> values) -> Digits {
  std::array<std::uint64_t, 8> columns{};
  for (const auto value : values) {
    check(value < (std::uint64_t{1} << 47U),
          "Oracle input fits frozen47-bit bound");
    for (unsigned i = 0; i < 3; ++i)
      for (unsigned j = 0; j < 3; ++j)
        columns[i + j] +=
            ((value >> (16 * i)) & 65535U) * ((value >> (16 * j)) & 65535U);
  }
  Digits result{};
  for (std::size_t i = 0; i < columns.size(); ++i) {
    result[i] = static_cast<std::uint32_t>(columns[i] & 65535U);
    if (i + 1 < columns.size()) columns[i + 1] += columns[i] >> 16U;
  }
  check((columns.back() >> 16U) == 0,
        "Independent oracle has no discarded carry");
  return result;
}
auto less(const Digits& a, const Digits& b) -> bool {
  for (std::size_t i = a.size(); i > 0; --i) {
    if (a[i - 1] != b[i - 1]) return a[i - 1] < b[i - 1];
  }
  return false;
}
auto geometry(Seed seed, bool print_routes) -> void {
  const auto old_ids = generate_first_intersystem_identities(seed);
  const auto old_route = generate_first_universe_route(seed);
  const auto network = need(generate_freedom_system_network(seed));
  // An independent copy detects mutation by refused operations.
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Snapshot.
  const auto original = network;
  check(validate_freedom_system_network(network).has_value() &&
            network == need(generate_freedom_system_network(seed)),
        "Reproducible canonical network authenticates its whole recipe");
  check(network.version == 1 && network.universe_seed == seed &&
            network.network_seed ==
                derive_seed(seed, SeedDomain::freedom_system_network, 1),
        "Network has independent named/versioned derivation");
  check(
      network.nodes[0].system == old_ids.origin_system &&
          network.nodes[0].system_seed == old_ids.origin_system_seed &&
          network.nodes[0].position == old_route.origin_position &&
          network.nodes[1].system == old_ids.target_system &&
          network.nodes[1].system_seed == old_ids.target_system_seed &&
          network.nodes[1].position == old_route.destination_position,
      "Original pair identity, system seeds and exact anchors stay unchanged");
  std::array<std::int64_t, 3> old{old_route.destination_position.x,
                                  old_route.destination_position.y,
                                  old_route.destination_position.z};
  const auto axis = static_cast<std::size_t>(
      std::ranges::find_if(old, [](auto x) { return x != 0; }) - old.begin());
  check(axis < 3, "Original pair has one genuine cardinal displacement");
  if (axis >= 3) return;
  const auto perpendicular = (axis + 1) % 3;
  const auto h =
      static_cast<std::int64_t>(old_route.distance_light_seconds / 2);
  const auto q =
      static_cast<std::int64_t>(old_route.distance_light_seconds / 4);
  for (std::size_t i = 0; i < network.nodes.size(); ++i) {
    const auto& node = network.nodes[i];
    check(node.ordinal == i && node.system.value != 0 &&
              node.system.value == node.system_seed.value &&
              need(find_freedom_system_network_node(network, node.system)) ==
                  node,
          "Stable ordinal/id/seed and owned node lookup");
    if (i >= 2) {
      check(node.system_seed ==
                derive_seed(network.network_seed, SeedDomain::system, i),
            "Added identity has its own canonical system-seed stream ordinal");
      std::array<std::int64_t, 3> expected{};
      expected[axis] = (old[axis] < 0 ? -h : h) * kMetresPerLightSecond;
      expected[perpendicular] = (i == 2 ? q : -q) * kMetresPerLightSecond;
      check(
          node.position ==
              UniversePositionMetres{expected[0], expected[1], expected[2]},
          "Independent axis-index construction matches exact LS-grid anchors");
    }
    for (std::size_t j = 0; j < i; ++j)
      check(node.system != network.nodes[j].system &&
                node.position != network.nodes[j].position,
            "Generated nodes have distinct nonzero IDs and distinct anchors");
    hash(node.ordinal);
    hash(node.system_seed.value);
    hash(node.system.value);
    hash(static_cast<std::uint64_t>(node.position.x));
    hash(static_cast<std::uint64_t>(node.position.y));
    hash(static_cast<std::uint64_t>(node.position.z));
    check(!resolve_freedom_system_network_route(network, node.system,
                                                node.system),
          "Same-system route refuses");
    for (std::size_t j = i + 1; j < network.nodes.size(); ++j) {
      const auto& other = network.nodes[j];
      const auto route = need(resolve_freedom_system_network_route(
          network, node.system, other.system));
      const auto reverse = need(resolve_freedom_system_network_route(
          network, other.system, node.system));
      const UniversePositionMetres delta{other.position.x - node.position.x,
                                         other.position.y - node.position.y,
                                         other.position.z - node.position.z};
      check(
          route.source == node.system && route.destination == other.system &&
              route.displacement == delta &&
              reverse.displacement ==
                  UniversePositionMetres{-delta.x, -delta.y, -delta.z} &&
              route.distance_metres == reverse.distance_metres &&
              route.squared_distance_light_seconds ==
                  reverse.squared_distance_light_seconds,
          "All6 actual Euclidean links retain directed geometry and symmetry");
      const std::array metre{absolute(delta.x), absolute(delta.y),
                             absolute(delta.z)};
      std::uint64_t square_ls{};
      for (const auto component : metre) {
        check(component % kMetresPerLightSecond == 0,
              "Every displacement retains exact integer-lightsecond units");
        const auto ls = component / kMetresPerLightSecond;
        square_ls += ls * ls;
      }
      check(route.squared_distance_light_seconds == square_ls &&
                square_ls > 0 &&
                square_ls <= old_route.distance_light_seconds *
                                 old_route.distance_light_seconds,
            "Exact squared distances are nonzero and no link exceeds legacy D");
      check(route.distance_metres > 2 * static_cast<std::uint64_t>(
                                            kLocalSystemBoundaryMetres) &&
                route.distance_metres <= old_route.distance_metres &&
                route.distance_metres <= kFreedomJumpRatedMetres,
            "All links are distinct local systems and within current starter "
            "rating");
      const auto squared = sum_squares(metre);
      const auto upper = sum_squares({route.distance_metres, 0, 0});
      const auto lower = sum_squares({route.distance_metres - 1, 0, 0});
      check(
          !less(upper, squared) && less(lower, squared),
          "Independent integer oracle certifies exact Euclidean metre ceiling");
      if (i == 0 && j == 1)
        check(route.distance_metres == old_route.distance_metres &&
                  square_ls == old_route.distance_light_seconds *
                                   old_route.distance_light_seconds,
              "Original cardinal pair retains exact metre distance");
      hash(route.squared_distance_light_seconds);
      hash(route.distance_metres);
      if (print_routes)
        std::cout << "ROUTE seed=" << seed.value << " from=" << i << " to=" << j
                  << " dx=" << delta.x << " dy=" << delta.y << " dz=" << delta.z
                  << " square_ls=" << square_ls
                  << " ceil_metres=" << route.distance_metres << '\n';
    }
  }
  check(network == original &&
            old_ids == generate_first_intersystem_identities(seed) &&
            old_route == generate_first_universe_route(seed),
        "Network route reads and independent stream reads do not mutate old "
        "truth");
}
auto ownership_and_refusals() -> void {
  const Seed seed{42};
  const auto network = need(generate_freedom_system_network(seed));
  const auto home = need(generate_physical_origin_system(seed, 2));
  const auto neighbor = need(generate_physical_local_system(
      generate_first_intersystem_identities(seed).target_system_seed, 2));
  for (const auto& node : network.nodes) {
    const auto system =
        need(resolve_freedom_network_system(network, node.system));
    const auto direct =
        node.ordinal == 0
            ? need(generate_physical_origin_system(seed, 2))
            : need(generate_physical_local_system(node.system_seed, 2));
    check(system == direct && validate_local_system(system).has_value() &&
              system.generator_version == 2 &&
              system.catalog.id == node.system &&
              system.origin_universe_seed ==
                  (node.ordinal == 0 ? std::optional{seed} : std::nullopt),
          "Each typed physical owner is rederived with correct Origin "
          "provenance");
    check(!resolve_freedom_network_system(network, node.system, 0) &&
              !resolve_freedom_network_system(
                  network, node.system,
                  std::numeric_limits<std::uint32_t>::max()),
          "Unsupported physical recipe refuses rather than replacing an owner");
  }
  check(home == need(resolve_freedom_network_system(network,
                                                    network.nodes[0].system)) &&
            neighbor == need(resolve_freedom_network_system(
                            network, network.nodes[1].system)),
        "Original Home/neighbor physical catalogs are exact, without carrier "
        "conversion");
  check(!generate_freedom_system_network(seed, 0) &&
            !generate_freedom_system_network(seed, 4) &&
            !generate_freedom_system_network(
                seed, std::numeric_limits<std::uint32_t>::max()),
        "Unsupported topology versions refuse before any adoption");
  for (unsigned damage = 0; damage < 10; ++damage) {
    auto bad = network;
    switch (damage) {
      case 0: bad.version = 2; break;
      case 1: ++bad.universe_seed.value; break;
      case 2: ++bad.network_seed.value; break;
      case 3: bad.nodes[2].ordinal = 3; break;
      case 4: bad.nodes[2].system = bad.nodes[1].system; break;
      case 5: bad.nodes[2].system = SystemId{0}; break;
      case 6: ++bad.nodes[2].system_seed.value; break;
      case 7: ++bad.nodes[2].position.x; break;
      case 8: bad.nodes[3].position = bad.nodes[0].position; break;
      case 9:
        bad.nodes[3].position.y = std::numeric_limits<std::int64_t>::min();
        break;
    }
    const auto frozen = bad;
    check(!validate_freedom_system_network(bad) &&
              !find_freedom_system_network_node(bad, network.nodes[0].system) &&
              !resolve_freedom_network_system(bad, network.nodes[0].system) &&
              !resolve_freedom_system_network_route(
                  bad, network.nodes[0].system, network.nodes[1].system) &&
              bad == frozen,
          "Full forged topology refuses even lookup of an untouched known node "
          "atomically");
  }
  const auto foreign =
      need(generate_freedom_system_network(Seed{43})).nodes[2].system;
  for (const auto id : {SystemId{0}, foreign})
    check(
        !find_freedom_system_network_node(network, id) &&
            !resolve_freedom_network_system(network, id) &&
            !resolve_freedom_system_network_route(network, id,
                                                  network.nodes[0].system) &&
            !resolve_freedom_system_network_route(network,
                                                  network.nodes[0].system, id),
        "Foreign/zero system cannot borrow a valid network or produce a route");
  // Owned results remain valid when the caller's topology storage goes away.
  auto temporary = network;
  const auto owned = need(
      find_freedom_system_network_node(temporary, temporary.nodes[2].system));
  temporary = {};
  check(owned == network.nodes[2],
        "Small lookup result owns its full node value");
  const auto legacy = generate_first_universe_route(seed);
  check(legacy.route_seed.value == 4490051804352235517ULL &&
            legacy.origin.value == 0x09683d79dbc20b52ULL &&
            legacy.destination.value == 0x28630482e6b15573ULL &&
            legacy.direction == UniverseAxisDirection::negative_z &&
            legacy.distance_light_seconds == 321457 &&
            legacy.distance_metres == 96370384171306ULL &&
            legacy.destination_position ==
                UniversePositionMetres{0, 0, -96370384171306LL},
        "Existing seed42 identity/geometry golden is not replaced by network "
        "recipe");
}
auto uncharted_geometry(Seed seed) -> void {
  const auto prior = need(generate_freedom_system_network(seed));
  const auto network = need(generate_freedom_system_network(
      seed, kFreedomUnchartedSystemNetworkVersion));
  // An independent copy detects mutation by refused operations.
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Snapshot.
  const auto original = network;
  const auto route = generate_first_universe_route(seed);
  const bool sizes =
      network.nodes.size() == kFreedomUnchartedSystemNetworkSize &&
      prior.nodes.size() == kFreedomSystemNetworkSize;
  check(sizes,
        "Explicit recipe2 has six nodes and default recipe1 still has four");
  if (!sizes) return;
  check(network.version == 2 && network.universe_seed == seed &&
            network.network_seed ==
                derive_seed(seed, SeedDomain::freedom_uncharted_systems, 2) &&
            network == need(generate_freedom_system_network(seed, 2)) &&
            validate_freedom_system_network(network).has_value(),
        "Six-node recipe has reproducible independently named versioned truth");
  check(
      std::equal(prior.nodes.begin(), prior.nodes.end(), network.nodes.begin()),
      "All original four IDs, seeds, ordinals and anchors remain exact prefix");
  const std::array<std::int64_t, 3> old{route.destination_position.x,
                                        route.destination_position.y,
                                        route.destination_position.z};
  const auto axis = static_cast<std::size_t>(
      std::ranges::find_if(old, [](auto x) { return x != 0; }) - old.begin());
  check(axis < 3, "Six-node source retains genuine old cardinal route");
  if (axis >= 3) return;
  const auto unused = (axis + 2) % 3;
  const auto h = static_cast<std::int64_t>(route.distance_light_seconds / 2);
  const auto q = static_cast<std::int64_t>(route.distance_light_seconds / 4);
  std::size_t links{};
  for (std::size_t i = 0; i < network.nodes.size(); ++i) {
    const auto& node = network.nodes[i];
    check(node.ordinal == i && node.system_seed.value != 0 &&
              node.system.value == node.system_seed.value &&
              need(find_freedom_system_network_node(network, node.system)) ==
                  node,
          "All six canonical owned nodes resolve without identity aliases");
    if (i >= kFreedomSystemNetworkSize) {
      std::array<std::int64_t, 3> expected{};
      expected[axis] = (old[axis] < 0 ? -h : h) * kMetresPerLightSecond;
      expected[unused] = (i == 4 ? q : -q) * kMetresPerLightSecond;
      check(node.system_seed ==
                    derive_seed(network.network_seed, SeedDomain::system, i) &&
                node.position == UniversePositionMetres{expected[0],
                                                        expected[1],
                                                        expected[2]},
            "Independent axis-index oracle locates new4/5 in the third plane");
    }
    for (std::size_t j = 0; j < i; ++j)
      check(node.system != network.nodes[j].system &&
                node.system_seed != network.nodes[j].system_seed &&
                node.position != network.nodes[j].position,
            "All six IDs, seeds and positions are distinct");
    check(!resolve_freedom_system_network_route(network, node.system,
                                                node.system),
          "Six-node route still refuses same-system travel");
    for (std::size_t j = i + 1; j < network.nodes.size(); ++j) {
      ++links;
      const auto& other = network.nodes[j];
      const auto actual = need(resolve_freedom_system_network_route(
          network, node.system, other.system));
      const auto reverse = need(resolve_freedom_system_network_route(
          network, other.system, node.system));
      const UniversePositionMetres delta{other.position.x - node.position.x,
                                         other.position.y - node.position.y,
                                         other.position.z - node.position.z};
      const std::array metres{absolute(delta.x), absolute(delta.y),
                              absolute(delta.z)};
      std::uint64_t square_ls{};
      for (const auto component : metres) {
        check(component % kMetresPerLightSecond == 0,
              "Six-node displacements stay on the exact integer LS grid");
        const auto ls = component / kMetresPerLightSecond;
        square_ls += ls * ls;
      }
      check(actual.source == node.system &&
                actual.destination == other.system &&
                actual.displacement == delta &&
                reverse.displacement ==
                    UniversePositionMetres{-delta.x, -delta.y, -delta.z} &&
                reverse.distance_metres == actual.distance_metres &&
                reverse.squared_distance_light_seconds == square_ls &&
                actual.squared_distance_light_seconds == square_ls &&
                square_ls > 0 &&
                square_ls <=
                    route.distance_light_seconds * route.distance_light_seconds,
            "All15 links have independent exact displacement/square and "
            "symmetry");
      check(actual.distance_metres > 2 * static_cast<std::uint64_t>(
                                             kLocalSystemBoundaryMetres) &&
                actual.distance_metres <= route.distance_metres &&
                actual.distance_metres <= kFreedomJumpRatedMetres,
            "All15 links retain separated systems and starter-rated distance");
      const auto target = sum_squares(metres);
      check(
          !less(sum_squares({actual.distance_metres, 0, 0}), target) &&
              less(sum_squares({actual.distance_metres - 1, 0, 0}), target),
          "Independent schoolbook oracle certifies each integer metre ceiling");
      if (j < kFreedomSystemNetworkSize)
        check(actual == need(resolve_freedom_system_network_route(
                            prior, node.system, other.system)),
              "All original six complete route records remain exact under "
              "recipe2");
      if (seed == Seed{42})
        std::cout << "UNCHARTED_ROUTE seed=" << seed.value << " from=" << i
                  << " to=" << j << " dx=" << delta.x << " dy=" << delta.y
                  << " dz=" << delta.z << " square_ls=" << square_ls
                  << " ceil_metres=" << actual.distance_metres << '\n';
    }
  }
  check(
      links == 15 && network == original &&
          prior == need(generate_freedom_system_network(seed)),
      "Whole15-link reads preserve both selected and default immutable truth");
}
auto uncharted_ownership_and_refusals() -> void {
  const auto network = need(generate_freedom_system_network(Seed{42}, 2));
  for (const auto& node : network.nodes) {
    const auto physical =
        need(resolve_freedom_network_system(network, node.system));
    const auto direct =
        node.ordinal == 0
            ? need(generate_physical_origin_system(Seed{42}, 2))
            : need(generate_physical_local_system(node.system_seed, 2));
    check(
        physical == direct && validate_local_system(physical).has_value() &&
            physical.catalog.id == node.system &&
            physical.generator_version == 2 &&
            physical.origin_universe_seed ==
                (node.ordinal == 0 ? std::optional{Seed{42}} : std::nullopt),
        "Each of six genuine physical catalogs binds the actual seeded owner");
    check(!resolve_freedom_network_system(network, node.system, 0),
          "Six-node selection never authorizes an unsupported physical recipe");
  }
  check(freedom_system_network_version_supported(1) &&
            freedom_system_network_version_supported(2) &&
            !freedom_system_network_version_supported(0) &&
            freedom_system_network_version_supported(3) &&
            !freedom_system_network_version_supported(4) &&
            !freedom_system_network_version_supported(
                std::numeric_limits<std::uint32_t>::max()),
        "Only explicit topology versions1/2/3 are supported");
  for (unsigned damage = 0; damage < 14; ++damage) {
    auto bad = network;
    switch (damage) {
      case 0: bad.nodes.pop_back(); break;
      case 1: bad.nodes.push_back(bad.nodes.back()); break;
      case 2: bad.nodes.clear(); break;
      case 3: bad.nodes.resize(64, bad.nodes.back()); break;
      case 4: bad.version = 1; break;
      case 5: bad.version = 3; break;
      case 6: ++bad.network_seed.value; break;
      case 7: bad.nodes[4].system = bad.nodes[0].system; break;
      case 8: bad.nodes[4].system = SystemId{0}; break;
      case 9: ++bad.nodes[5].system_seed.value; break;
      case 10: bad.nodes[4].ordinal = 5; break;
      case 11: bad.nodes[4].position = bad.nodes[0].position; break;
      case 12:
        bad.nodes[4].position.x = std::numeric_limits<std::int64_t>::min();
        break;
      case 13:
        bad.nodes[5].position.z = std::numeric_limits<std::int64_t>::max();
        break;
    }
    const auto frozen = bad;
    check(!validate_freedom_system_network(bad) &&
              !find_freedom_system_network_node(bad, network.nodes[0].system) &&
              !resolve_freedom_network_system(bad, network.nodes[0].system) &&
              !resolve_freedom_system_network_route(
                  bad, network.nodes[0].system, network.nodes[1].system) &&
              bad == frozen,
          "Malformed vector size/recipe/new node refuses before old-node "
          "lookup or arithmetic");
  }
  const auto prior = need(generate_freedom_system_network(Seed{42}));
  check(!find_freedom_system_network_node(prior, network.nodes[4].system) &&
            !resolve_freedom_system_network_route(prior, prior.nodes[0].system,
                                                  network.nodes[4].system),
        "Old four-node topology cannot silently acquire a new system");
  const auto foreign =
      need(generate_freedom_system_network(Seed{43}, 2)).nodes[4].system;
  for (const auto id : {SystemId{0}, foreign})
    check(!find_freedom_system_network_node(network, id) &&
              !resolve_freedom_network_system(network, id) &&
              !resolve_freedom_system_network_route(network, id,
                                                    network.nodes[0].system) &&
              !resolve_freedom_system_network_route(
                  network, network.nodes[0].system, id),
          "Six-node consumers refuse zero and foreign system ownership");
}

auto outer_geometry(Seed seed) -> void {
  const auto prior = need(generate_freedom_system_network(seed, 2));
  const auto network = need(generate_freedom_system_network(seed, 3));
  const auto route = generate_first_universe_route(seed);
  check(network.nodes.size() == 8 && prior.nodes.size() == 6,
        "Outer recipe appends exactly two nodes to the original six");
  if (network.nodes.size() != 8 || prior.nodes.size() != 6) return;
  check(network.version == kFreedomOuterSystemNetworkVersion &&
            network.universe_seed == seed &&
            network.network_seed ==
                derive_seed(seed, SeedDomain::freedom_outer_systems, 3) &&
            network.network_seed != prior.network_seed &&
            network == need(generate_freedom_system_network(seed, 3)) &&
            validate_freedom_system_network(network).has_value(),
        "Outer generation is explicit, independently seeded and canonical");
  check(
      std::equal(prior.nodes.begin(), prior.nodes.end(), network.nodes.begin()),
      "Outer recipe retains all six complete old node records exactly");
  const std::array<std::int64_t, 3> old{route.destination_position.x,
                                        route.destination_position.y,
                                        route.destination_position.z};
  const auto axis = static_cast<std::size_t>(
      std::ranges::find_if(old, [](auto value) { return value != 0; }) -
      old.begin());
  check(axis < 3, "Outer source has a genuine signed cardinal route");
  if (axis >= 3) return;
  const auto perpendicular = (axis + 2) % 3;
  const auto h = route.distance_light_seconds / 2;
  const auto q = route.distance_light_seconds / 4;
  const auto reach_square = h * h;
  std::size_t links{};
  for (std::size_t i = 0; i < network.nodes.size(); ++i) {
    const auto& node = network.nodes[i];
    check(node.ordinal == i && node.system.value != 0 &&
              node.system.value == node.system_seed.value &&
              need(find_freedom_system_network_node(network, node.system)) ==
                  node,
          "All eight canonical identities retain owned lookup");
    if (i >= 6) {
      std::array<std::int64_t, 3> expected{};
      const auto along =
          static_cast<std::int64_t>(h + q) * kMetresPerLightSecond;
      const auto side = static_cast<std::int64_t>(h) * kMetresPerLightSecond;
      expected[axis] = old[axis] < 0 ? -along : along;
      expected[perpendicular] = i == 6 ? side : -side;
      check(node.system_seed ==
                    derive_seed(network.network_seed, SeedDomain::system, i) &&
                node.position == UniversePositionMetres{expected[0],
                                                        expected[1],
                                                        expected[2]},
            "Independent axis-index oracle authenticates the outer anchor and "
            "seed");
    }
    check(!resolve_freedom_system_network_route(network, node.system,
                                                node.system),
          "Outer recipe still refuses same-system travel");
    for (std::size_t j = i + 1; j < network.nodes.size(); ++j) {
      ++links;
      const auto& other = network.nodes[j];
      check(node.system != other.system &&
                node.system_seed != other.system_seed &&
                node.position != other.position,
            "Outer IDs, seeds and anchors have no aliases");
      const auto actual = need(resolve_freedom_system_network_route(
          network, node.system, other.system));
      const auto reverse = need(resolve_freedom_system_network_route(
          network, other.system, node.system));
      const UniversePositionMetres delta{other.position.x - node.position.x,
                                         other.position.y - node.position.y,
                                         other.position.z - node.position.z};
      const std::array metres{absolute(delta.x), absolute(delta.y),
                              absolute(delta.z)};
      std::uint64_t squared_ls{};
      for (const auto component : metres) {
        check(component % kMetresPerLightSecond == 0,
              "Outer displacement stays on integer light-seconds");
        const auto ls = component / kMetresPerLightSecond;
        squared_ls += ls * ls;
      }
      check(actual.displacement == delta &&
                reverse.displacement ==
                    UniversePositionMetres{-delta.x, -delta.y, -delta.z} &&
                actual.squared_distance_light_seconds == squared_ls &&
                reverse.squared_distance_light_seconds == squared_ls &&
                actual.distance_metres == reverse.distance_metres &&
                squared_ls > 0 &&
                squared_ls <= route.distance_light_seconds *
                                  route.distance_light_seconds &&
                actual.distance_metres <= route.distance_metres &&
                actual.distance_metres <= kFreedomJumpRatedMetres &&
                actual.distance_metres >
                    2 * static_cast<std::uint64_t>(kLocalSystemBoundaryMetres),
            "All28 outer anchor links are exact, separated, symmetric and "
            "within the original drive bound");
      const auto target = sum_squares(metres);
      check(!less(sum_squares({actual.distance_metres, 0, 0}), target) &&
                less(sum_squares({actual.distance_metres - 1, 0, 0}), target),
            "Independent schoolbook oracle certifies the outer integer metre "
            "ceiling");
      if (j < 6)
        check(actual == need(resolve_freedom_system_network_route(
                            prior, node.system, other.system)),
              "All15 old complete route records remain exact under the outer "
              "recipe");
    }
  }
  check(links == 28 && prior == need(generate_freedom_system_network(seed, 2)),
        "Outer route reads preserve all original six-node truth");
  const auto distance_square = [&](std::size_t from, std::size_t to) {
    return need(resolve_freedom_system_network_route(network,
                                                     network.nodes[from].system,
                                                     network.nodes[to].system))
        .squared_distance_light_seconds;
  };
  for (std::size_t outer = 6; outer < 8; ++outer) {
    for (std::size_t known = 0; known < 4; ++known)
      check(distance_square(known, outer) > reach_square,
            "No originally charted system can acquire an outer contact at the "
            "proposed reach");
    const auto same_side = outer - 2;
    const auto opposite_side = same_side == 4 ? 5U : 4U;
    check(distance_square(same_side, outer) <= reach_square &&
              distance_square(opposite_side, outer) > reach_square,
          "Each outer contact is geometrically in reach only from its matching "
          "hidden frontier");
  }
  for (const auto waypoint : {2U, 3U})
    for (const auto hidden : {4U, 5U})
      check(distance_square(waypoint, hidden) <= reach_square &&
                distance_square(0, hidden) > reach_square,
            "Charted waypoints support the discovery chain without a Home "
            "acquisition shortcut");
}
auto outer_ownership_and_refusals() -> void {
  const auto network = need(generate_freedom_system_network(Seed{42}, 3));
  const auto prior = need(generate_freedom_system_network(Seed{42}, 2));
  for (const auto& node : network.nodes) {
    const auto physical =
        need(resolve_freedom_network_system(network, node.system, 2));
    check(
        physical.catalog.id == node.system && physical.generator_version == 2 &&
            validate_local_system(physical).has_value(),
        "Outer lookup resolves actual authoritative generated body ownership");
    if (node.ordinal < 6)
      check(physical ==
                need(resolve_freedom_network_system(prior, node.system, 2)),
            "All six original physical catalogs are unchanged, not just anchor "
            "labels");
    else
      check(physical ==
                need(generate_physical_local_system(node.system_seed, 2)),
            "Both new physical catalogs use their independently derived system "
            "seeds");
    check(!resolve_freedom_network_system(network, node.system, 0),
          "Outer selection cannot authorize an unsupported physical recipe");
  }
  for (unsigned damage = 0; damage < 12; ++damage) {
    auto bad = network;
    switch (damage) {
      case 0: bad.version = 0; break;
      case 1: bad.version = 4; break;
      case 2: bad.nodes.resize(6); break;
      case 3: bad.nodes.push_back(bad.nodes.back()); break;
      case 4: ++bad.network_seed.value; break;
      case 5: bad.nodes[6].ordinal = 7; break;
      case 6: bad.nodes[6].system = SystemId{0}; break;
      case 7: ++bad.nodes[6].system_seed.value; break;
      case 8: bad.nodes[6].position = bad.nodes[0].position; break;
      case 9: bad.nodes[7].system = bad.nodes[6].system; break;
      case 10:
        bad.nodes[6].position.x = std::numeric_limits<std::int64_t>::min();
        break;
      case 11:
        bad.nodes[7].position.z = std::numeric_limits<std::int64_t>::max();
        break;
    }
    const auto frozen = bad;
    check(!validate_freedom_system_network(bad) &&
              !find_freedom_system_network_node(bad, network.nodes[0].system) &&
              !resolve_freedom_network_system(bad, network.nodes[0].system) &&
              !resolve_freedom_system_network_route(
                  bad, network.nodes[0].system, network.nodes[1].system) &&
              bad == frozen,
          "Malformed outer context refuses before old-node lookup or "
          "coordinate arithmetic");
  }
  const auto foreign =
      need(generate_freedom_system_network(Seed{43}, 3)).nodes[6].system;
  for (const auto id : {SystemId{0}, foreign})
    check(!find_freedom_system_network_node(network, id) &&
              !resolve_freedom_network_system(network, id) &&
              !resolve_freedom_system_network_route(
                  network, network.nodes[0].system, id),
          "Outer consumers refuse zero or foreign owner IDs");
  check(!find_freedom_system_network_node(prior, network.nodes[6].system) &&
            !resolve_freedom_system_network_route(prior, prior.nodes[0].system,
                                                  network.nodes[6].system),
        "The six-node recipe cannot silently expand into an outer system");
}
auto outer_axis_and_floor_coverage() -> void {
  std::array<bool, 24> covered{};
  for (std::uint64_t value = 0;
       value < 4096 && !std::ranges::all_of(
                           covered, [](bool v) { return v; });
       ++value) {
    const auto route = generate_first_universe_route(Seed{value});
    const auto tag = static_cast<std::size_t>(route.direction) * 4 +
                     route.distance_light_seconds % 4;
    check(
        tag < covered.size(),
        "Outer coverage indexes only a supported signed axis and floor class");
    if (tag >= covered.size()) return;
    if (!covered[tag]) {
      covered[tag] = true;
      outer_geometry(Seed{value});
    }
  }
  check(std::ranges::all_of(covered, [](bool v) { return v; }),
        "Outer geometry covers all six signed directions and all four "
        "integer-floor classes");
}

} // namespace
int main() {
  try {
    ownership_and_refusals();
    for (const auto value : std::array<std::uint64_t, 8>{
             0, 1, 4, 7, 42, 123, 2048,
             std::numeric_limits<std::uint64_t>::max()})
      geometry(Seed{value}, true);
    for (std::uint64_t value = 100; value < 164; ++value)
      geometry(Seed{value}, false);
    uncharted_ownership_and_refusals();
    outer_ownership_and_refusals();
    outer_axis_and_floor_coverage();
    for (const auto value : {std::uint64_t{0}, std::uint64_t{42},
                             std::numeric_limits<std::uint64_t>::max()})
      outer_geometry(Seed{value});
    for (const auto value : std::array<std::uint64_t, 8>{
             0, 1, 4, 7, 42, 123, 2048,
             std::numeric_limits<std::uint64_t>::max()})
      uncharted_geometry(Seed{value});
    for (std::uint64_t value = 100; value < 164; ++value)
      uncharted_geometry(Seed{value});
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << error.what() << '\n';
  }
  check(fingerprint == 7122939247517196130ULL,
        "Network1 fixed-seed recipe retains its independently checked golden");
  std::cout << checks << " system-network checks, " << failures
            << " failures; first network fingerprint " << fingerprint << '\n';
  return failures ? 1 : 0;
}
