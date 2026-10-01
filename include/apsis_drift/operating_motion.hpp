#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/rigid_body.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kOperatingMotionRecipeVersion{1};
inline constexpr std::size_t kOperatingMotionMaximumDocumentBytes{
    std::size_t{128} * std::size_t{1024}};
inline constexpr std::array<std::string_view, 13> kOperatingCraftGroupIds{
    "roof_port",
    "roof_starboard",
    "inner_port_outer",
    "inner_port_inner",
    "inner_starboard_outer",
    "inner_starboard_inner",
    "ladder_base",
    "ladder_upper",
    "seat_carriage",
    "seat_swivel",
    "seat_lift",
    "seat_entry_arm",
    "seat_lock"};
inline constexpr std::array<std::string_view, 17> kOperatingStationGroupIds{
    "station_d1_00", "station_d1_01", "station_d1_02", "station_d1_03",
    "station_d1_04", "station_d1_05", "station_d1_06", "station_d1_07",
    "station_d1_08", "station_d1_09", "station_d1_10", "station_d1_11",
    "station_d1_12", "station_d1_13", "station_d1_14", "station_d1_15",
    "station_d1_16"};

// Metres, active affine transform, four columns: basis X/Y/Z and translation.
// This source-specific boundary does not introduce a generic physics engine.
struct OperatingTransform {
  std::array<RigidVector3, 4> columns{RigidVector3{1, 0, 0},
                                      RigidVector3{0, 1, 0},
                                      RigidVector3{0, 0, 1}, RigidVector3{}};
  friend auto operator==(const OperatingTransform&, const OperatingTransform&)
      -> bool = default;
};
struct OperatingProgress {
  double roof_transfer{}, inner_door{}, seat_boarding{}, station_closure{};
  friend auto operator==(const OperatingProgress&, const OperatingProgress&)
      -> bool = default;
};
struct OperatingPose {
  // Complete world-rest deltas applied once to flat world-baked craft groups.
  std::array<OperatingTransform, 13> craft_world_deltas;
  // Assigned below the original imported D1 parent hierarchy.
  std::array<OperatingTransform, 17> station_node_local;
  // Complete rest deltas in canonical station coordinates, including offset.
  std::array<OperatingTransform, 17> station_contact_deltas;
  friend auto operator==(const OperatingPose&, const OperatingPose&)
      -> bool = default;
};

enum class OperatingChannel : std::uint8_t {
  roof_transfer,
  inner_door,
  seat_boarding,
  station_closure
};
enum class OperatingProperty : std::uint8_t { location, rotation_euler };
struct OperatingMotionKnot {
  double progress{}, value{};
  friend auto operator==(const OperatingMotionKnot&, const OperatingMotionKnot&)
      -> bool = default;
};
struct OperatingMotionTrack {
  OperatingChannel channel{};
  std::size_t node{}, axis{};
  OperatingProperty property{};
  std::vector<OperatingMotionKnot> knots;
  friend auto operator==(const OperatingMotionTrack&,
                         const OperatingMotionTrack&) -> bool = default;
};
struct OperatingMotionNode {
  std::string source_object;
  std::optional<std::size_t> parent;
  std::optional<std::size_t> output_group;
  RigidVector3 location_metres, euler_xyz_radians;
  OperatingTransform rest_world;
  friend auto operator==(const OperatingMotionNode&, const OperatingMotionNode&)
      -> bool = default;
};
struct OperatingMotionRecipe {
  std::uint32_t version{kOperatingMotionRecipeVersion};
  std::string craft_source_sha256, station_source_sha256, closure_source_sha256;
  std::string craft_model_sha256, station_model_sha256;
  std::string contact_sha256, station_closure_sha256;
  std::array<OperatingMotionNode, 15> craft_nodes;
  std::array<OperatingMotionNode, 19> station_nodes;
  std::vector<OperatingMotionTrack> craft_tracks, station_tracks;
  friend auto operator==(const OperatingMotionRecipe&,
                         const OperatingMotionRecipe&) -> bool = default;
};

[[nodiscard]] auto decode_operating_motion_recipe(std::string_view)
    -> std::expected<OperatingMotionRecipe, std::string>;
[[nodiscard]] auto validate_operating_motion_recipe(
    const OperatingMotionRecipe&) -> std::expected<void, std::string>;
// Pure source-local evaluation. No simulation clock, actor state or save
// change. Every request starts from immutable rest and composes each ancestor
// once.
[[nodiscard]] auto evaluate_operating_motion(const OperatingMotionRecipe&,
                                             const OperatingProgress&)
    -> std::expected<OperatingPose, std::string>;
} // namespace apsis_drift
