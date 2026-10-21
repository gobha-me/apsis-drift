#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>

#include "apsis_drift/physical_local_system.hpp"
#include "apsis_drift/universe_navigation.hpp"

namespace apsis_drift {
// Explicit new selection only. Legacy pair generation and saves stay version1.
inline constexpr std::uint32_t kFreedomSystemNetworkVersion{1};
inline constexpr std::size_t kFreedomSystemNetworkSize{4};

struct FreedomSystemNetworkNode {
  std::uint32_t ordinal{};
  Seed system_seed;
  SystemId system;
  UniversePositionMetres position;
  friend auto operator==(const FreedomSystemNetworkNode&,
                         const FreedomSystemNetworkNode&) -> bool = default;
};
struct FreedomSystemNetwork {
  std::uint32_t version{kFreedomSystemNetworkVersion};
  Seed universe_seed;
  Seed network_seed;
  // Stable Home, original neighbor, added2, added3 order; no mutable discovery.
  std::array<FreedomSystemNetworkNode, kFreedomSystemNetworkSize> nodes;
  friend auto operator==(const FreedomSystemNetwork&,
                         const FreedomSystemNetwork&) -> bool = default;
};
struct FreedomSystemNetworkRoute {
  SystemId source;
  SystemId destination;
  UniversePositionMetres displacement;
  // Exact Euclidean square in integer light-seconds; endpoints use that grid.
  std::uint64_t squared_distance_light_seconds{};
  // Integer CEILING of the actual Euclidean metre distance; error is <1m.
  // Non-cardinal links are not represented as an authored FirstUniverseRoute.
  std::uint64_t distance_metres{};
  friend auto operator==(const FreedomSystemNetworkRoute&,
                         const FreedomSystemNetworkRoute&) -> bool = default;
};
enum class FreedomSystemNetworkError : std::uint8_t {
  unsupported_version,
  invalid_network,
  invalid_system,
  invalid_geometry,
  physical_system_refused,
};

[[nodiscard]] auto generate_freedom_system_network(
    Seed universe_seed, std::uint32_t version = kFreedomSystemNetworkVersion)
    -> std::expected<FreedomSystemNetwork, FreedomSystemNetworkError>;
[[nodiscard]] auto validate_freedom_system_network(const FreedomSystemNetwork&)
    -> std::expected<void, FreedomSystemNetworkError>;
// Small owned results avoid returning pointers into caller-owned mutable data.
[[nodiscard]] auto find_freedom_system_network_node(const FreedomSystemNetwork&,
                                                    SystemId)
    -> std::expected<FreedomSystemNetworkNode, FreedomSystemNetworkError>;
[[nodiscard]] auto resolve_freedom_network_system(
    const FreedomSystemNetwork&, SystemId,
    std::uint32_t physical_version =
        kContinuousPhysicalLocalSystemGeneratorVersion)
    -> std::expected<PhysicalLocalSystem, FreedomSystemNetworkError>;
[[nodiscard]] auto resolve_freedom_system_network_route(
    const FreedomSystemNetwork&, SystemId source, SystemId destination)
    -> std::expected<FreedomSystemNetworkRoute, FreedomSystemNetworkError>;
} // namespace apsis_drift
