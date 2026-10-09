#pragma once

#include "apsis_drift/godot/contact_patch.hpp"
#include "apsis_drift/native_flight_session.hpp"

namespace apsis_drift::godot_spike {
// A distinct geometry experiment for saved terrain; NOT physical recipe
// adoption, save data or permission to populate TouchdownObservations.
inline constexpr ContactSurfaceRecipe kExperimentalSavedContactSurface{
    2, 1, 8, 0, 13, 32, 1};

struct SavedContactProvenance {
  PhysicalLocalSystem owner;
  PlanetDescriptor planet;
  PhysicalPlanetRotationRecipe rotation;
  ContactSurfaceRecipe recipe;
  RigidBodyState original_state, fixed_query_state;
  std::uint64_t original_state_checksum{};
  friend auto operator==(const SavedContactProvenance&,
                         const SavedContactProvenance&) -> bool = default;
};
struct SavedContactPadOutcome {
  std::uint8_t support_index{};
  // Present once a candidate was constructed, including later patch refusal.
  std::optional<ContactTriangle> triangle;
  // Every active slot has an outcome. Unused slots are entirely zero/empty.
  std::optional<std::expected<ContactPatch, ContactPatchError>> geometry;
  friend auto operator==(const SavedContactPadOutcome&,
                         const SavedContactPadOutcome&) -> bool = default;
};
struct SavedContactGeometryBatch {
  SavedContactProvenance provenance;
  std::uint8_t support_count{};
  std::array<SavedContactPadOutcome, 4> supports;
  friend auto operator==(const SavedContactGeometryBatch&,
                         const SavedContactGeometryBatch&) -> bool = default;
};
enum class SavedContactError : std::uint8_t {
  unsupported_recipe,
  invalid_physical_owner,
  invalid_state,
  unsupported_frame,
  invalid_craft,
  invalid_rotation,
  reframe_failed,
  invalid_cache_capacity
};

// Read-only same-tick snapshot, owning its descriptor/state/cache by value.
// Factory validation performs no terrain sampling. query() derives all nominal
// pads independently of gear; at most three vertex samples per active support.
// Boundary/conditioning failures retain the indexed foot, never imply unsafe
// material or readiness. No first surface, bearing, hull/sweep or landing
// claim.
class SavedContactGeometry {
 public:
  [[nodiscard]] static auto create(
      const NativeFreedomFlightSession&,
      ContactSurfaceRecipe = kExperimentalSavedContactSurface,
      std::size_t cache_capacity = kDefaultTerrainTileCacheCapacity)
      -> std::expected<SavedContactGeometry, SavedContactError>;
  // The same validated C++ owner/state seam also serves save validation without
  // recursively opening a live session. It never infers a physical owner from
  // bare numeric IDs or accepts an unvalidated caller-built descriptor.
  [[nodiscard]] static auto create(
      const PhysicalLocalSystem&, const PhysicalPlanetRotationRecipe&,
      const RigidBodyState&,
      ContactSurfaceRecipe = kExperimentalSavedContactSurface,
      std::size_t cache_capacity = kDefaultTerrainTileCacheCapacity)
      -> std::expected<SavedContactGeometry, SavedContactError>;
  [[nodiscard]] auto query() -> SavedContactGeometryBatch;
  [[nodiscard]] auto provenance() const -> const SavedContactProvenance& {
    return provenance_;
  }
  [[nodiscard]] auto cache_size() const -> std::size_t { return cache_.size(); }

 private:
  SavedContactGeometry(SavedContactProvenance provenance,
                       TerrainTileCache cache)
      : provenance_(std::move(provenance)), cache_(std::move(cache)) {}
  SavedContactProvenance provenance_;
  TerrainTileCache cache_;
};
} // namespace apsis_drift::godot_spike
