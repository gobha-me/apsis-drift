#pragma once

#include "apsis_drift/freedom_local_observations.hpp"
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
  std::uint64_t fuel_debit_quanta{};
  bool propulsion_refused{};
  bool jump_committed{}, jump_arrived{};
};

// Transient player command, never part of the save/world recipe. The aid only
// translates an already aligned craft near a selected port using real thrust.
struct NativePortApproach {
  bool active{};
  std::uint32_t remaining_ticks{};
  std::string note{"Approach aid off"};
  friend auto operator==(const NativePortApproach&, const NativePortApproach&)
      -> bool = default;
};
inline constexpr std::uint32_t kNativePortApproachTicks{7200};
enum class NativeSurfaceManeuverKind : std::uint8_t { off, landing, liftoff };
struct NativeSurfaceManeuver {
  NativeSurfaceManeuverKind kind{NativeSurfaceManeuverKind::off};
  std::uint32_t remaining_ticks{};
  std::string note{"Surface aid off"};
  friend auto operator==(const NativeSurfaceManeuver&,
                         const NativeSurfaceManeuver&) -> bool = default;
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
  [[nodiscard]] auto resources() const
      -> const std::optional<FreedomResources>& {
    return resources_;
  }
  [[nodiscard]] auto knowledge() const
      -> const std::optional<FreedomKnowledge>& {
    return knowledge_;
  }
  [[nodiscard]] auto recovery_pending() const -> bool {
    return recovery_ && recovery_->pending;
  }
  [[nodiscard]] auto recovery_document() const
      -> std::expected<FreedomRecoverySaveDocument, std::string>;
  [[nodiscard]] auto record_loss(FreedomLossCause, StarterCraftId,
                                 SimulationTick, std::uint64_t source_checksum)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto complete_recovery() -> std::expected<void, std::string>;
  [[nodiscard]] auto travel() const
      -> const std::optional<FreedomTravelState>& {
    return travel_;
  }
  [[nodiscard]] auto travel_document() const
      -> std::expected<FreedomTravelSaveDocument, std::string>;
  [[nodiscard]] auto select_jump(SystemId) -> std::expected<void, std::string>;
  [[nodiscard]] auto jump_preview() const
      -> std::expected<FreedomJumpPreview, std::string>;
  [[nodiscard]] auto begin_jump() -> std::expected<void, std::string>;
  [[nodiscard]] auto jump_available() const -> std::expected<void, std::string>;
  [[nodiscard]] auto cancel_jump() -> std::expected<void, std::string>;
  [[nodiscard]] auto record_observation(const KnowledgeEvidence&)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto replenish_resources() -> std::expected<void, std::string>;
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
    if (recovery_pending())
      return std::unexpected{
          "Continue the recorded loss before controlling the replacement"};
    if (travel_ && travel_->phase != FreedomJumpPhase::idle)
      return std::unexpected{
          "Cancel the active jump before changing assistance"};
    if (actor_ || surface_walker_)
      return std::unexpected{"Board and sit before controlling the craft"};
    document_.model.assistance = enabled;
    if (!enabled) cancel_surface_maneuver();
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
  [[nodiscard]] auto begin_port_approach() -> std::expected<void, std::string>;
  auto cancel_port_approach() -> void;
  [[nodiscard]] auto port_approach() const -> const NativePortApproach& {
    return port_approach_;
  }
  [[nodiscard]] auto port_approach_available() const
      -> std::expected<void, std::string>;
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
  [[nodiscard]] auto surface() const
      -> const std::optional<FreedomSurfaceState>& {
    return surface_;
  }
  [[nodiscard]] auto surface_walker() const
      -> const std::optional<PlanetSurfaceWalkerState>& {
    return surface_walker_;
  }
  [[nodiscard]] auto surface_walk_selected() const -> bool {
    return surface_walk_selected_;
  }
  [[nodiscard]] auto begin_surface_walk() -> std::expected<void, std::string>;
  [[nodiscard]] auto return_from_surface_walk()
      -> std::expected<void, std::string>;
  [[nodiscard]] auto advance_surface_walk(const OriginWalkControls&,
                                          SimulationSeconds = kSimulationStep)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto surface_walk_document() const
      -> std::expected<FreedomSurfaceWalkSaveDocument, std::string>;
  [[nodiscard]] auto surface_document() const -> FreedomSurfaceSaveDocument;
  [[nodiscard]] auto set_landing_gear(bool deployed)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto commit_touchdown(std::uint64_t expected_source_checksum)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto release_surface() -> std::expected<void, std::string>;
  [[nodiscard]] auto request_landing() -> std::expected<void, std::string>;
  auto cancel_surface_maneuver() -> void;
  [[nodiscard]] auto surface_maneuver() const -> const NativeSurfaceManeuver& {
    return surface_maneuver_;
  }

 private:
  [[nodiscard]] auto advance_jump(const NativeFlightControls&,
                                  SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto refresh_knowledge(LocalObservationEvent)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto port_approach_controls() const
      -> std::expected<NativeFlightControls, std::string>;
  [[nodiscard]] auto begin_boarding_route(GameplayBoardingDirection)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto boarding_document() const -> FreedomBoardingSaveDocument;
  [[nodiscard]] auto advance_craft_tick(const NativeFlightControls&,
                                        SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto surface_maneuver_controls() const
      -> std::expected<NativeFlightControls, std::string>;
  [[nodiscard]] auto advance_surface(const NativeFlightControls&,
                                     SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  auto try_surface_contact() -> void;
  NativeFreedomFlightSession(FreedomFlightSaveDocument document,
                             FreedomFlightHydration hydrated,
                             std::optional<std::filesystem::path> source);
  FreedomFlightSaveDocument document_;
  std::optional<FreedomResources> resources_;
  std::optional<FreedomKnowledge> knowledge_;
  std::optional<FreedomTravelState> travel_;
  std::optional<FreedomRecoveryState> recovery_;
  PhysicalLocalSystem system_;
  PhysicalPlanetRotationRecipe rotation_;
  std::optional<std::filesystem::path> source_save_;
  std::optional<FreedomDockingState> docking_;
  std::optional<OriginWalkerState> actor_;
  std::optional<NativeStartingAssemblySelection> starting_assembly_;
  NativeCraftBinding craft_binding_;
  std::optional<FreedomBoardingState> boarding_;
  NativePortApproach port_approach_;
  std::optional<FreedomSurfaceState> surface_;
  NativeSurfaceManeuver surface_maneuver_;
  bool surface_walk_selected_{};
  std::optional<PlanetSurfaceWalkerState> surface_walker_;
  std::shared_ptr<PlanetSurfaceWalkTerrain> surface_walk_terrain_;
};
} // namespace apsis_drift
