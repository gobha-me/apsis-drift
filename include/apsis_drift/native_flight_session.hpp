#pragma once

#include "apsis_drift/native_craft_binding.hpp"
#include "apsis_drift/native_startup.hpp"

namespace apsis_drift {
struct NativeFlightControls {
  RigidVector3 positive_translation, negative_translation;
  RigidVector3 positive_rotation, negative_rotation;
};
struct NativeFlightObservation {
  AtmosphericFlightSample atmosphere;
  OrbitalTelemetry orbit;
  friend auto operator==(const NativeFlightObservation&,
                         const NativeFlightObservation&) -> bool = default;
};
struct NativeFlightStep {
  OrbitHoldCorrection hold;
  AtmosphericFlightActuation actuation;
  RigidVector3 applied_hold_force_body_newtons;
};

// Application-owned mutable session. Moving/copying it never retains dangling
// descriptor/context pointers; each query makes a transient qualified context.
// Presentation has no mutable access to state, catalog or recipe selections.
class NativeFreedomFlightSession {
 public:
  [[nodiscard]] static auto open(NativeStartup selected)
      -> std::expected<NativeFreedomFlightSession, std::string>;
  [[nodiscard]] auto document() const -> const FreedomFlightSaveDocument& {
    return document_;
  }
  [[nodiscard]] auto system() const -> const PhysicalLocalSystem& {
    return system_;
  }
  [[nodiscard]] auto rotation() const -> const PhysicalPlanetRotationRecipe& {
    return rotation_;
  }
  [[nodiscard]] auto source_save() const
      -> const std::optional<std::filesystem::path>& {
    return source_save_;
  }
  [[nodiscard]] auto observe() const
      -> std::expected<NativeFlightObservation, std::string>;
  auto set_assistance(bool enabled) -> std::expected<void, std::string> {
    if (actor_)
      return std::unexpected{"Board and sit before controlling the craft"};
    document_.model.assistance = enabled;
    return {};
  }
  [[nodiscard]] auto set_hold(OrbitHoldRequest)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto advance(const NativeFlightControls&,
                             SimulationSeconds = kSimulationStep)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto save_as(const std::filesystem::path&) const
      -> std::expected<void, std::string>;
  [[nodiscard]] auto docking() const
      -> const std::optional<FreedomDockingState>& {
    return docking_;
  }
  [[nodiscard]] auto select_port(std::uint32_t ordinal)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto assess_port() const
      -> std::expected<OriginDockAssessment, std::string>;
  [[nodiscard]] auto capture_port() -> std::expected<void, std::string>;
  [[nodiscard]] auto walker() const -> const std::optional<OriginWalkerState>& {
    return actor_;
  }
  [[nodiscard]] auto advance_walk(const OriginWalkControls&,
                                  SimulationSeconds = kSimulationStep)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto boarding() const
      -> const std::optional<FreedomBoardingState>& {
    return boarding_;
  }
  [[nodiscard]] auto boarding_view() const
      -> std::expected<GameplayBoardingView, std::string>;
  [[nodiscard]] auto begin_boarding() -> std::expected<void, std::string>;
  [[nodiscard]] auto begin_disembarking() -> std::expected<void, std::string>;
  [[nodiscard]] auto release_port() -> std::expected<void, std::string>;

  [[nodiscard]] auto starting_assembly() const
      -> const std::optional<NativeStartingAssemblySelection>& {
    return starting_assembly_;
  }
  [[nodiscard]] auto craft_binding() const -> const NativeCraftBinding& {
    return craft_binding_;
  }

 private:
  [[nodiscard]] auto begin_boarding_route(GameplayBoardingDirection)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto boarding_document() const -> FreedomBoardingSaveDocument;
  [[nodiscard]] auto advance_craft_tick(const NativeFlightControls&,
                                        SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  NativeFreedomFlightSession(FreedomFlightSaveDocument document,
                             FreedomFlightHydration hydrated,
                             std::optional<std::filesystem::path> source);
  FreedomFlightSaveDocument document_;
  PhysicalLocalSystem system_;
  PhysicalPlanetRotationRecipe rotation_;
  std::optional<std::filesystem::path> source_save_;
  std::optional<FreedomDockingState> docking_;
  std::optional<OriginWalkerState> actor_;
  std::optional<NativeStartingAssemblySelection> starting_assembly_;
  NativeCraftBinding craft_binding_;
  std::optional<FreedomBoardingState> boarding_;
};
} // namespace apsis_drift
