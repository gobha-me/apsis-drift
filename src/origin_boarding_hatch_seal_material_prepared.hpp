#pragma once
#include "origin_boarding_initial_material_internal.hpp"
namespace apsis_drift::detail {
struct HatchSealSweepConstructorRecord {
  std::array<RigidVector3, 9> local_points;
  OperatingTransform corrected_world;
  double bevel_depth{};
  // Full captured packets are immutable comparison data, never runtime JSON
  // or caller-supplied material permission. Typed points/depth drive the math.
  std::string_view settings_json, spline_json, modifiers_json, properties_json;
};
struct HatchSealMaterialPreparedView {
  const MaterialSourceRecord* source{};
  const MaterialMeshRecord* mesh{};
  const HatchSealSweepConstructorRecord* constructor{};
  std::size_t storage_bytes{};
  std::string_view metadata_sha256, binary_sha256, master_sha256,
      inventory_sha256, lifeboat_sha256, finish_sha256;
};
// Defined only by the pinned build-time generated immutable translation unit.
[[nodiscard]] auto origin_boarding_hatch_seal_material_prepared()
    -> HatchSealMaterialPreparedView;
} // namespace apsis_drift::detail
