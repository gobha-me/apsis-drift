#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "apsis_drift/physical_local_system.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kBodyIdentityVersion{1};
inline constexpr std::uint32_t kBodyHierarchyVersion{1};
inline constexpr std::size_t kMaximumHierarchyBodies{64};
inline constexpr std::size_t kMaximumBodyHierarchyDepth{8};

// A kind tag keeps existing star/planet words intact, even if their words
// coincide. Identity is system-scoped; it is never inferred from a display
// name.
enum class BodyKind : std::uint8_t { star, planet, moon, minor };
struct BodyId {
  std::uint32_t version{kBodyIdentityVersion};
  BodyKind kind{};
  std::uint64_t value{};
  friend auto operator<=>(const BodyId&, const BodyId&) = default;
};
[[nodiscard]] auto body_id(StarId) noexcept -> BodyId;
[[nodiscard]] auto body_id(PlanetId) noexcept -> BodyId;

struct BodyTarget {
  SystemId system;
  BodyId body;
  friend auto operator==(const BodyTarget&, const BodyTarget&)
      -> bool = default;
};

// Circular vectors are expressed in common, nonrotating system axes and
// translated by the parent. Parent spin is a separate surface-frame provider.
struct CircularBodyOrbit {
  std::uint32_t version{kBodyHierarchyVersion};
  std::uint32_t ephemeris_version{kAnalyticEphemerisVersion};
  std::uint64_t radius_kilometres{};
  SimulationTick period_ticks{};
  std::uint32_t epoch_phase_turns{};
  std::int32_t inclination_microdegrees{};
  std::uint32_t ascending_node_turns{};
  friend auto operator==(const CircularBodyOrbit&, const CircularBodyOrbit&)
      -> bool = default;
};
struct HierarchyBody {
  BodyId id;
  std::optional<BodyId> parent;
  std::optional<CircularBodyOrbit> orbit;
  friend auto operator==(const HierarchyBody&, const HierarchyBody&)
      -> bool = default;
};
struct BodyHierarchy {
  std::uint32_t version{kBodyHierarchyVersion};
  SystemId system;
  std::vector<HierarchyBody> bodies;
  // Compatibility projections retain the complete actual catalog owner.
  // Absence denotes declared analytic geometry, not an admitted native world.
  std::optional<std::variant<LocalSystemDescriptor, PhysicalLocalSystem>>
      catalog;
};
struct BodyEphemeris {
  BodyId body;
  std::optional<BodyId> parent;
  SystemPositionMetres relative_position;
  SystemVelocityMetresPerSecond relative_velocity;
  SystemPositionMetres position;
  SystemVelocityMetresPerSecond velocity;
  SimulationTick cycle_tick{};
  double phase_radians{};
  friend auto operator==(const BodyEphemeris&, const BodyEphemeris&)
      -> bool = default;
};
enum class BodyEphemerisError : std::uint8_t {
  unsupported_version,
  invalid_identity,
  invalid_catalog,
  oversized_hierarchy,
  invalid_root,
  duplicate_identity,
  missing_parent,
  cycle,
  excessive_depth,
  invalid_orbit,
  invalid_time,
  unsafe_arithmetic,
  invalid_target,
  invalid_encoding
};

[[nodiscard]] auto make_body_hierarchy(const LocalSystemDescriptor&)
    -> std::expected<BodyHierarchy, BodyEphemerisError>;
[[nodiscard]] auto make_body_hierarchy(const PhysicalLocalSystem&)
    -> std::expected<BodyHierarchy, BodyEphemerisError>;
[[nodiscard]] auto validate_body_hierarchy(const BodyHierarchy&)
    -> std::expected<void, BodyEphemerisError>;
// Results are sorted by full BodyId, independent of declaration order.
[[nodiscard]] auto resolve_body_ephemerides(const BodyHierarchy&,
                                            EphemerisQueryTime)
    -> std::expected<std::vector<BodyEphemeris>, BodyEphemerisError>;
[[nodiscard]] auto validate_body_target(const BodyHierarchy&, BodyTarget)
    -> std::expected<void, BodyEphemerisError>;
[[nodiscard]] auto planet_id(BodyId)
    -> std::expected<PlanetId, BodyEphemerisError>;

// Explicit future save/navigation component; existing save formats and
// PlanetId target encodings are unchanged. No generated catalog is serialized.
[[nodiscard]] auto encode_body_target(BodyTarget)
    -> std::expected<std::string, BodyEphemerisError>;
[[nodiscard]] auto decode_body_target(std::string_view)
    -> std::expected<BodyTarget, BodyEphemerisError>;

} // namespace apsis_drift
