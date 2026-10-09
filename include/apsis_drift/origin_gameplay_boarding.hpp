#pragma once

#include "apsis_drift/native_craft_binding.hpp"
#include "apsis_drift/station_geometry.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kGameplayBoardingTicksPerSecond{120};
inline constexpr std::uint32_t kGameplayBoardingTicks{1440};
inline constexpr double kGameplayBoardingEntryRadiusMetres{3.5};
enum class GameplayBoardingDirection : std::uint8_t { board, disembark };
enum class GameplayBoardingPhase : std::uint8_t {
  approach,
  ladder,
  cabin,
  seat,
  hardware,
  complete
};
// Route progress belongs to the application's existing tick; there is no
// second simulation clock. The session retains the real entry walker too.
struct GameplayBoardingState {
  GameplayBoardingDirection direction{};
  std::uint32_t elapsed_ticks{};
  RigidVector3 station_entry_eye;
  double station_entry_heading_radians{};
  RigidVector3 craft_station_position;
  RigidOrientation craft_station_orientation;
  friend auto operator==(const GameplayBoardingState&,
                         const GameplayBoardingState&) -> bool = default;
};
struct GameplayBoardingView {
  GameplayBoardingPhase phase{};
  double progress{}, heading_radians{};
  RigidVector3 eye_craft, eye_station, body_center_station;
  double body_radius{.18}, body_height{1.4};
  OperatingProgress hardware;
  OperatingPose operating_pose;
  bool seated{}, complete{};
};
[[nodiscard]] auto begin_origin_gameplay_boarding(const NativeCraftBinding&,
                                                  const OriginPortPose&,
                                                  RigidVector3 entry_eye,
                                                  double entry_heading_radians,
                                                  GameplayBoardingDirection)
    -> std::expected<GameplayBoardingState, std::string>;
// Lightweight save/state validation. Session additionally compares the stored
// craft transform against its freshly resolved attached D1 reference pose.
[[nodiscard]] auto validate_origin_gameplay_boarding(
    const GameplayBoardingState&) -> std::expected<void, std::string>;
[[nodiscard]] auto step_origin_gameplay_boarding(const GameplayBoardingState&,
                                                 std::uint32_t ticks = 1)
    -> std::expected<GameplayBoardingState, std::string>;
[[nodiscard]] auto view_origin_gameplay_boarding(const NativeCraftBinding&,
                                                 const GameplayBoardingState&)
    -> std::expected<GameplayBoardingView, std::string>;
} // namespace apsis_drift
