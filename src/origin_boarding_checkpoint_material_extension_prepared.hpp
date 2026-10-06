#pragma once
#include "origin_boarding_initial_material_internal.hpp"
namespace apsis_drift::detail {
struct CheckpointMaterialExtensionSourceRecord {
  std::uint32_t source_index{};
  MaterialSourceRecord identity;
};
struct CheckpointMaterialExtensionFrameRecord {
  std::uint32_t source{}, mesh{};
  std::array<RigidVector3, 32> local_vertices;
  std::array<std::array<std::uint8_t, 4>, 32> polygons;
  OperatingTransform corrected_world;
  double bevel_width{}, bevel_profile{}, bevel_angle_limit{};
  std::uint32_t bevel_segments{};
  bool bevel_show_viewport{}, bevel_show_render{}, normals_show_viewport{},
      normals_show_render{};
  // Complete canonical captured modifier/property packets, immutable strings.
  // Runtime admission compares the provider record and uses typed parameters;
  // it never parses JSON or treats captured metadata as material permission.
  std::string_view modifiers_json, properties_json;
};
struct CheckpointMaterialExtensionPreparedView {
  std::span<const CheckpointMaterialExtensionSourceRecord> sources;
  std::span<const MaterialMeshRecord> meshes;
  const CheckpointMaterialExtensionFrameRecord* frame{};
  std::string_view nose_properties_json;
  std::size_t storage_bytes{};
  std::string_view metadata_sha256, binary_sha256, master_sha256,
      inventory_sha256, history_helper_sha256;
};
// Defined only by the pinned build-time immutable preparation translation unit.
[[nodiscard]] auto origin_boarding_checkpoint_material_extension_prepared()
    -> CheckpointMaterialExtensionPreparedView;
} // namespace apsis_drift::detail
