#pragma once

#include "apsis_drift/native_flight_session.hpp"

#include <array>
#include <expected>
#include <optional>
#include <string>

namespace apsis_drift {
struct NativePlanetTrackingTarget {
  PlanetId planet;
  std::string name;
};
struct NativePlanetTrackingTargets {
  SystemId system;
  SimulationTick tick{};
  std::array<NativePlanetTrackingTarget, kMaximumLocalSystemPlanets> targets{};
  std::size_t count{};
};
struct NativePlanetTrackingReading {
  bool selected{}, available{};
  SystemId system;
  PlanetId planet, reference_planet;
  StarterCraftId craft;
  CraftFrameRecipe craft_frame{};
  RigidCoordinateFrame world_frame;
  SimulationTick tick{};
  std::string name;
  RigidVector3 position_metres;
  double range_metres{};
  std::string note{"Planet HUD tracking is off."};
};
// View-local state only. A new SavedFlightWorld/Continue starts empty. List and
// selection perform complete knowledge admission; read uses that admitted
// snapshot and the session's existing current physical owner, never a new
// world.
class NativePlanetTracking {
 public:
  [[nodiscard]] auto targets(const NativeFreedomFlightSession&) const
      -> std::expected<NativePlanetTrackingTargets, std::string>;
  [[nodiscard]] auto select(const NativeFreedomFlightSession&, BodyTarget,
                            SimulationTick offered_tick)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto read(const NativeFreedomFlightSession&)
      -> NativePlanetTrackingReading;
  auto clear() noexcept -> void { admission_.reset(); }

 private:
  struct Admission {
    FreedomKnowledgeRecipe recipe;
    BodyTarget target;
    PlanetId reference;
    StarterCraftId craft;
    CraftFrameRecipe craft_frame;
    RigidCoordinateFrame world_frame;
    SimulationTick tick{}, last_read_tick{};
    std::string name;
  };
  std::optional<Admission> admission_;
};
} // namespace apsis_drift
