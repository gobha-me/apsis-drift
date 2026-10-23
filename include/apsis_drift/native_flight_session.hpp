#pragma once

#include "apsis_drift/freedom_assistance_save.hpp"
#include "apsis_drift/freedom_local_jump.hpp"
#include "apsis_drift/freedom_local_observations.hpp"
#include "apsis_drift/moon_surface_landmarks.hpp"
#include "apsis_drift/native_craft_binding.hpp"
#include "apsis_drift/native_startup.hpp"
#include "apsis_drift/origin_station_crew.hpp"
#include <memory>

namespace apsis_drift {
class MoonSurfaceWalkTerrain;
struct NativeFlightControls {
  RigidVector3 positive_translation, negative_translation;
  RigidVector3 positive_rotation, negative_rotation;
};
struct NativeMoonFlightObservation {
  MoonBodyGravity gravity;
  PhysicalMoonRotationGeometry rotation;
  double altitude_metres{}, speed_metres_per_second{};
  double radial_speed_metres_per_second{}, tangential_speed_metres_per_second{};
  friend auto operator==(const NativeMoonFlightObservation&,
                         const NativeMoonFlightObservation&) -> bool = default;
};
struct NativeMinorFlightObservation {
  MinorBodyGravity gravity;
  PhysicalMinorRotationGeometry rotation;
  double altitude_metres{}, speed_metres_per_second{};
  double radial_speed_metres_per_second{}, tangential_speed_metres_per_second{};
  friend auto operator==(const NativeMinorFlightObservation&,
                         const NativeMinorFlightObservation&) -> bool = default;
};
struct NativeFlightObservation {
  AtmosphericFlightSample atmosphere;
  OrbitalTelemetry orbit;
  std::optional<NativeMoonFlightObservation> moon{};
  std::optional<NativeMinorFlightObservation> minor{};
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
  std::optional<MoonBodyActuation> moon{};
  std::optional<MinorBodyActuation> minor{};
  bool minor_standoff_limited{};
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
enum class NativePortDockingPhase : std::uint8_t {
  off,
  aligning,
  closing,
  complete
};
// Opt-in close docking. All fields are transient; attachment still uses the
// unchanged canonical port constraint and existing save schema.
struct NativePortDocking {
  bool active{};
  std::uint32_t remaining_ticks{};
  NativePortDockingPhase phase{NativePortDockingPhase::off};
  std::string note{"Docking off"};
  double alignment_outward_metres{};
  std::optional<OriginDockAssessment> latch_assessment;
  friend auto operator==(const NativePortDocking&, const NativePortDocking&)
      -> bool = default;
};
inline constexpr std::uint32_t kNativePortDockingTicks{21600};
inline constexpr double kNativePortDockingLatchMetres{.001};
inline constexpr double kNativePortDockingLatchRadians{.0001};
inline constexpr double kNativePortDockingLatchSpeed{.001};
inline constexpr double kNativePortDockingLatchAngularSpeed{.0001};
// Optional local Origin return guidance, not a saved navigation route.
struct NativePortRendezvous {
  bool active{};
  std::uint32_t remaining_ticks{};
  std::string note{"Rendezvous off"};
  friend auto operator==(const NativePortRendezvous&,
                         const NativePortRendezvous&) -> bool = default;
};
inline constexpr std::uint32_t kNativePortRendezvousTicks{288000};
enum class NativeSurfaceManeuverKind : std::uint8_t { off, landing, liftoff };
struct NativeSurfaceManeuver {
  NativeSurfaceManeuverKind kind{NativeSurfaceManeuverKind::off};
  std::uint32_t remaining_ticks{};
  std::string note{"Surface aid off"};
  friend auto operator==(const NativeSurfaceManeuver&,
                         const NativeSurfaceManeuver&) -> bool = default;
};

struct FreedomSurfaceMarkerView {
  bool selected{};
  std::optional<FreedomSurfaceMarkerLocation> point;
  bool current_body{};
  // Craft-centred, current owning body's inertial axes, for the native view.
  std::optional<RigidVector3> target_craft_relative_metres;
  // Straight chord from the outside foot, or from the craft while inside.
  std::optional<double> straight_line_distance_metres;
  std::optional<std::string> known_system_name, known_body_name;
};

struct FreedomSurfaceLandmarkView {
  SurfaceLandmarkSite site;
  RigidVector3 target_craft_relative_metres;
  double distance_metres{};
  bool nearby{}, can_inspect{}, inspected{};
};
struct FreedomSurfaceLandmarksView {
  bool selected{}, can_enable{};
  std::size_t inspection_count{};
  std::vector<FreedomSurfaceLandmarkView> sites;
};

struct FreedomMoonSurfaceLandmarkView {
  MoonSurfaceLandmarkSite site;
  RigidVector3 target_craft_relative_metres;
  double distance_metres{};
  bool nearby{}, can_inspect{}, inspected{};
};
struct FreedomMoonSurfaceLandmarksView {
  bool selected{}, can_enable{};
  std::size_t inspection_count{};
  std::vector<FreedomMoonSurfaceLandmarkView> sites;
};

struct NativeOrbitalSurveyView {
  bool selected{}, active{}, can_begin{}, completed{};
  std::uint32_t acquired_ticks{};
  std::string note, body_name;
  std::optional<KnownLanding> result;
};

struct NativeSurveyCommissionView {
  bool supported{}, selected{}, at_origin{}, can_accept{}, can_submit{},
      can_abandon{}, completed{}, expired{};
  std::uint32_t sequence{}, offer_sequence{}, acquired_ticks{};
  std::uint64_t earnings{};
  SimulationTick tick{}, offer_until{}, accepted_tick{}, deadline_tick{};
  SystemId system;
  PlanetId planet;
  std::string body_name, note, last_payment;
};

struct NativeChartedArrivalView {
  bool supported{}, enabled{}, eligible{};
  std::string note;
};

struct NativeSpecimenSaleView {
  bool supported{}, at_origin{}, paid_selected{}, can_sell{};
  std::uint32_t carried_count{}, capacity{4};
  std::uint64_t unit_credits{40}, gross_credits{}, earned_credits{},
      insurance_credits{}, tab_credits{}, net_credits{}, cash_after{};
  SimulationTick tick{};
  std::string token, note, contents;
};
struct NativePaidOperationsView {
  bool supported{}, selected{}, at_origin{}, can_enable{}, can_purchase{},
      can_retainer{};
  SimulationTick tick{}, started_tick{}, berth_until_tick{};
  std::uint64_t cash{}, opening_cash{}, operating_tab{}, recovery_due{};
  std::uint64_t missing_flight_quanta{}, capacity_quanta{}, flight_credits{},
      jump_credits{}, service_credits{};
  std::uint32_t missing_jump_charges{};
  std::uint64_t retainer_cash{}, retainer_tab{}, retainer_subsidy{},
      retainer_berth{}, retainer_total{};
  std::string token, terms, note;
};

struct NativeStarSearchView {
  bool selected{}, active{}, can_begin{};
  std::uint32_t acquired_ticks{};
  std::size_t completed{};
  std::string note;
};

struct NativeDerelictView {
  bool selected{}, known{}, local{}, inspected{}, can_scan{}, can_inspect{};
  std::uint64_t id{};
  std::optional<RigidBodyState> hull;
  double distance_metres{}, relative_speed_metres_per_second{};
  std::string note, history;
};

// Application-owned mutable session. Moving/copying it never retains dangling
// descriptor/context pointers; each query makes a transient qualified context.
// Presentation has no mutable access to state, catalog or recipe selections.
class NativeFreedomFlightSession {
 public:
  [[nodiscard]] auto origin_assistance() const
      -> const std::optional<FreedomAssistanceActor>& {
    return assistance_;
  }
  [[nodiscard]] auto origin_assistance_world() const
      -> const OriginAssistanceWorldContext* {
    return assistance_world_.get();
  }
  [[nodiscard]] auto origin_assistance_propulsion() const
      -> const VacuumActuation& {
    return assistance_propulsion_;
  }
  [[nodiscard]] auto origin_assistance_available() const
      -> std::expected<void, std::string>;
  [[nodiscard]] auto request_origin_assistance()
      -> std::expected<void, std::string>;
  [[nodiscard]] auto cancel_origin_assistance()
      -> std::expected<void, std::string>;
  [[nodiscard]] auto station_crew() const
      -> std::expected<OriginCrewView, std::string>;
  [[nodiscard]] auto station_stellar_lead_view() const
      -> std::expected<NativeStationStellarLeadView, std::string>;
  [[nodiscard]] auto receive_station_stellar_lead(SimulationTick offered_tick)
      -> std::expected<void, std::string>;
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
  [[nodiscard]] auto enable_impact_consequences()
      -> std::expected<void, std::string>;
  [[nodiscard]] auto impact_consequences_available() const
      -> std::expected<void, std::string>;
  [[nodiscard]] auto condition() const -> const std::optional<CraftCondition>& {
    return condition_;
  }
  [[nodiscard]] auto condition_document() const
      -> std::expected<FreedomConditionSaveDocument, std::string>;
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
  // Destination choice only; fuel, space and launch readiness remain separate.
  [[nodiscard]] auto jump_selection_available(SystemId) const
      -> std::expected<void, std::string>;
  [[nodiscard]] auto select_jump(SystemId) -> std::expected<void, std::string>;
  [[nodiscard]] auto planet_jump_selection_available(BodyTarget) const
      -> std::expected<void, std::string>;
  [[nodiscard]] auto select_planet_jump(BodyTarget)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto planet_jump_preview() const
      -> std::expected<FrozenFreedomPlanetJumpArrival, std::string>;
  [[nodiscard]] auto select_local_jump(BodyTarget)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto local_jump_preview() const
      -> std::expected<FrozenLocalBodyJumpArrival, std::string>;

  [[nodiscard]] auto jump_preview() const
      -> std::expected<FreedomJumpPreview, std::string>;
  [[nodiscard]] auto begin_jump() -> std::expected<void, std::string>;
  [[nodiscard]] auto jump_available() const -> std::expected<void, std::string>;
  [[nodiscard]] auto cancel_jump() -> std::expected<void, std::string>;
  [[nodiscard]] auto record_observation(const KnowledgeEvidence&)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto charted_arrival_view() const
      -> std::expected<NativeChartedArrivalView, std::string>;
  [[nodiscard]] auto set_charted_arrivals(bool enabled)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto specimen_sale_view() const
      -> std::expected<NativeSpecimenSaleView, std::string>;
  [[nodiscard]] auto sell_specimens(std::string_view token)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto paid_operations_view() const
      -> std::expected<NativePaidOperationsView, std::string>;
  [[nodiscard]] auto effective_paid_operations_account() const
      -> std::expected<PaidOperationsAccount, std::string>;
  [[nodiscard]] auto enable_paid_operations(std::string_view token)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto purchase_paid_fuel(std::string_view token)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto request_paid_retainer(std::string_view token)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto survey_commission_view() const
      -> std::expected<NativeSurveyCommissionView, std::string>;
  [[nodiscard]] auto accept_survey_commission(SimulationTick offer_until,
                                              std::uint32_t sequence)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto submit_survey_commission(std::uint32_t sequence)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto abandon_survey_commission(std::uint32_t sequence)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto begin_orbital_survey() -> std::expected<void, std::string>;
  [[nodiscard]] auto cancel_orbital_survey()
      -> std::expected<void, std::string>;
  [[nodiscard]] auto orbital_survey_view() const -> NativeOrbitalSurveyView;
  [[nodiscard]] auto begin_star_search() -> std::expected<void, std::string>;
  [[nodiscard]] auto cancel_star_search() -> std::expected<void, std::string>;
  [[nodiscard]] auto star_search_view() const -> NativeStarSearchView;
  [[nodiscard]] auto derelict_view() const -> NativeDerelictView;
  [[nodiscard]] auto scan_derelict() -> std::expected<void, std::string>;
  [[nodiscard]] auto inspect_derelict() -> std::expected<void, std::string>;
  [[nodiscard]] auto replenish_resources() -> std::expected<void, std::string>;
  [[nodiscard]] auto system() const -> const PhysicalLocalSystem& {
    return system_;
  }
  [[nodiscard]] auto moon() const
      -> const std::optional<PhysicalMoonDescriptor>& {
    return moon_;
  }
  [[nodiscard]] auto minor() const
      -> const std::optional<PhysicalMinorDescriptor>& {
    return minor_;
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
  // Qualified same-tick observation for the existing planet-owned providers.
  // The canonical document remains the sole saved physical state.
  [[nodiscard]] auto relative_state() const
      -> std::expected<RigidBodyState, std::string>;
  auto set_assistance(bool enabled) -> std::expected<void, std::string> {
    if (recovery_pending())
      return std::unexpected{
          "Continue the recorded loss before controlling the replacement"};
    if (travel_ && travel_->phase != FreedomJumpPhase::idle)
      return std::unexpected{
          "Cancel the active jump before changing assistance"};
    if (actor_ || surface_walker_)
      return std::unexpected{"Board and sit before controlling the craft"};
    if (document_.model.assistance != enabled) {
      cancel_port_rendezvous();
      cancel_port_docking();
    }
    document_.model.assistance = enabled;
    if (!enabled) cancel_surface_maneuver();
    return {};
  }
  [[nodiscard]] auto set_hold(OrbitHoldRequest)
      -> std::expected<void, std::string>;
  // Explicit player selection from the authoritative current orbit. Assessing
  // availability and selecting the target never advance or reposition the body.
  [[nodiscard]] auto current_orbit_hold_target() const
      -> std::expected<OrbitHoldTarget, std::string>;
  [[nodiscard]] auto hold_current_orbit() -> std::expected<void, std::string>;
  [[nodiscard]] auto advance(const NativeFlightControls&,
                             SimulationSeconds = kSimulationStep)
      -> std::expected<NativeFlightStep, std::string>;
  // Project the currently supported mode through the same typed save owner.
  // Profile metadata and filesystem identity remain outside world state.
  [[nodiscard]] auto save_document() const
      -> std::expected<NativeSaveDocument, std::string>;
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
  [[nodiscard]] auto port_docking_available() const
      -> std::expected<void, std::string>;
  [[nodiscard]] auto begin_port_docking() -> std::expected<void, std::string>;
  auto cancel_port_docking() -> void;
  [[nodiscard]] auto port_docking() const -> const NativePortDocking& {
    return port_docking_;
  }
  [[nodiscard]] auto port_rendezvous_available() const
      -> std::expected<void, std::string>;
  [[nodiscard]] auto begin_port_rendezvous()
      -> std::expected<void, std::string>;
  auto cancel_port_rendezvous() -> void;
  [[nodiscard]] auto port_rendezvous() const -> const NativePortRendezvous& {
    return port_rendezvous_;
  }
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
  // Same preparation/validation as begin_boarding, without installing an
  // actor/route, advancing time or copying world catalogs/geometry/recovery.
  [[nodiscard]] auto boarding_available() const
      -> std::expected<void, std::string>;
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
  // Transient feedback from the last committed ground step, never save data.
  // Supported movement/neutral steps clear it; getters perform no terrain work.
  [[nodiscard]] auto surface_walk_blockage() const
      -> std::optional<PlanetSurfaceWalkStatus> {
    return surface_walker_ && !recovery_pending() ? surface_walk_blockage_
                                                  : std::nullopt;
  }
  [[nodiscard]] auto surface_walk_selected() const -> bool {
    return surface_walk_selected_;
  }
  [[nodiscard]] auto surface_return_access() const
      -> std::expected<PlanetSurfaceReturnAccess, std::string>;
  [[nodiscard]] auto begin_surface_walk() -> std::expected<void, std::string>;
  [[nodiscard]] auto return_from_surface_walk()
      -> std::expected<void, std::string>;
  [[nodiscard]] auto advance_surface_walk(const OriginWalkControls&,
                                          SimulationSeconds = kSimulationStep)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto surface_walk_document() const
      -> std::expected<FreedomSurfaceWalkSaveDocument, std::string>;
  [[nodiscard]] auto mark_surface_marker() -> std::expected<void, std::string>;
  [[nodiscard]] auto replace_surface_marker()
      -> std::expected<void, std::string>;
  [[nodiscard]] auto clear_surface_marker() -> std::expected<void, std::string>;
  [[nodiscard]] auto surface_marker_view() const
      -> std::expected<FreedomSurfaceMarkerView, std::string>;
  [[nodiscard]] auto enable_surface_landmarks()
      -> std::expected<void, std::string>;
  [[nodiscard]] auto inspect_surface_landmark(SurfaceLandmarkId)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto surface_landmarks_view() const
      -> std::expected<FreedomSurfaceLandmarksView, std::string>;
  [[nodiscard]] auto moon_surface_landmarks_view() const
      -> std::expected<FreedomMoonSurfaceLandmarksView, std::string>;
  [[nodiscard]] auto field_sample_collection_available(SurfaceLandmarkId) const
      -> std::expected<void, std::string>;
  [[nodiscard]] auto collect_field_sample(SurfaceLandmarkId)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto discard_field_sample(BodyTarget, SurfaceLandmarkId)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto mark_field_sample_site(BodyTarget, SurfaceLandmarkId,
                                            bool replace)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto surface_document() const -> FreedomSurfaceSaveDocument;
  [[nodiscard]] auto last_gear_clearance() const
      -> std::optional<TerrainPadGapSample>;
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
  [[nodiscard]] auto player_save_document() const
      -> std::expected<NativeSaveDocument, std::string>;
  [[nodiscard]] auto advance_player(const NativeFlightControls&,
                                    SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto advance_player_walk(const OriginWalkControls&,
                                         SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto advance_player_surface_walk(const OriginWalkControls&,
                                                 SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto advance_origin_assistance_tick()
      -> std::expected<void, std::string>;
  [[nodiscard]] auto origin_assistance_port_reserved(OriginPortId) const
      -> bool;
  [[nodiscard]] auto assistance_recipient_supported() const -> bool;
  [[nodiscard]] auto field_sample_collection_source(SurfaceLandmarkId) const
      -> std::expected<FreedomFieldSampleRecord, std::string>;
  [[nodiscard]] auto advance_flight(const NativeFlightControls&,
                                    SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto star_search_conditions() const
      -> std::expected<void, std::string>;
  [[nodiscard]] auto advance_star_search_tick()
      -> std::expected<void, std::string>;
  [[nodiscard]] auto orbital_measurement_conditions() const
      -> std::expected<void, std::string>;
  [[nodiscard]] auto advance_survey_commission_tick()
      -> std::expected<void, std::string>;
  [[nodiscard]] auto paid_operations_origin_available() const -> bool;
  [[nodiscard]] auto paid_operations_token() const -> std::string;
  [[nodiscard]] auto paid_operations_generation() const -> std::uint64_t;
  [[nodiscard]] auto survey_paid_count() const -> std::uint32_t;
  [[nodiscard]] auto paid_income_counts() const -> PaidIncomeCounts;
  [[nodiscard]] auto specimen_sale_token() const -> std::string;
  [[nodiscard]] auto commit_survey_commission_account(SurveyCommissionAccount)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto orbital_survey_conditions() const
      -> std::expected<void, std::string>;
  [[nodiscard]] auto advance_orbital_survey_tick()
      -> std::expected<void, std::string>;
  [[nodiscard]] auto set_moon_landing_gear(bool)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto commit_moon_touchdown(std::uint64_t)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto release_moon_surface() -> std::expected<void, std::string>;
  [[nodiscard]] auto advance_moon_supported_tick(const NativeFlightControls&,
                                                 SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  auto try_moon_surface_contact() -> void;
  [[nodiscard]] auto begin_moon_surface_walk()
      -> std::expected<void, std::string>;
  [[nodiscard]] auto moon_surface_return_access() const
      -> std::expected<PlanetSurfaceReturnAccess, std::string>;
  [[nodiscard]] auto return_from_moon_surface_walk()
      -> std::expected<void, std::string>;
  [[nodiscard]] auto advance_moon_surface_walk(const OriginWalkControls&,
                                               SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto store_moon_surface_marker(bool replace)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto moon_surface_marker_view(FreedomSurfaceMarkerView) const
      -> std::expected<FreedomSurfaceMarkerView, std::string>;
  [[nodiscard]] auto prepare_composed_travel()
      -> std::expected<void, std::string>;
  [[nodiscard]] auto admit_composed_travel() const
      -> std::expected<void, std::string>;
  [[nodiscard]] auto select_foreign_moon_jump(BodyTarget)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto select_foreign_minor_jump(BodyTarget)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto select_composed_local_jump(BodyTarget)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto composed_body_jump_available() const
      -> std::expected<void, std::string>;
  [[nodiscard]] auto begin_composed_body_jump()
      -> std::expected<void, std::string>;
  [[nodiscard]] auto advance_composed_body_jump(const NativeFlightControls&,
                                                SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto local_jump_available() const
      -> std::expected<void, std::string>;
  [[nodiscard]] auto begin_local_jump() -> std::expected<void, std::string>;
  [[nodiscard]] auto advance_local_jump(const NativeFlightControls&,
                                        SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto enable_moon_surface_landmarks()
      -> std::expected<void, std::string>;
  [[nodiscard]] auto inspect_moon_surface_landmark(SurfaceLandmarkId)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto store_surface_marker(bool replace)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto advance_jump(const NativeFlightControls&,
                                  SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto refresh_knowledge(LocalObservationEvent)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto port_approach_controls() const
      -> std::expected<NativeFlightControls, std::string>;
  [[nodiscard]] auto port_docking_conditions(bool initial) const
      -> std::expected<void, std::string>;
  [[nodiscard]] auto port_docking_controls() const
      -> std::expected<NativeFlightControls, std::string>;
  [[nodiscard]] auto advance_port_docking(const NativeFlightControls&,
                                          SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto port_rendezvous_conditions(bool initial) const
      -> std::expected<void, std::string>;
  [[nodiscard]] auto port_rendezvous_controls() const
      -> std::expected<NativeFlightControls, std::string>;
  [[nodiscard]] auto advance_port_rendezvous(const NativeFlightControls&,
                                             SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto begin_boarding_route(GameplayBoardingDirection)
      -> std::expected<void, std::string>;
  [[nodiscard]] auto prepare_boarding_route(GameplayBoardingDirection) const
      -> std::expected<FreedomBoardingState, std::string>;
  [[nodiscard]] auto boarding_document() const -> FreedomBoardingSaveDocument;
  [[nodiscard]] auto advance_craft_tick(const NativeFlightControls&,
                                        SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto observe_minor() const
      -> std::expected<NativeFlightObservation, std::string>;
  [[nodiscard]] auto advance_minor_tick(const NativeFlightControls&,
                                        SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto advance_moon_tick(const NativeFlightControls&,
                                       SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto advance_impact_tick(const NativeFlightControls&,
                                         SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  [[nodiscard]] auto surface_maneuver_controls() const
      -> std::expected<NativeFlightControls, std::string>;
  [[nodiscard]] auto advance_surface(const NativeFlightControls&,
                                     SimulationSeconds)
      -> std::expected<NativeFlightStep, std::string>;
  auto try_surface_contact() -> void;
  auto commit_touchdown_observed(std::uint64_t,
                                 std::optional<TerrainPadGapSample>*)
      -> std::expected<void, std::string>;
  NativeFreedomFlightSession(FreedomFlightSaveDocument document,
                             FreedomFlightHydration hydrated,
                             std::optional<std::filesystem::path> source);
  std::optional<FreedomAssistanceActor> assistance_;
  std::shared_ptr<const OriginAssistanceWorldContext> assistance_world_;
  VacuumActuation assistance_propulsion_{};
  FreedomFlightSaveDocument document_;
  std::optional<FreedomResources> resources_;
  std::optional<FreedomKnowledge> knowledge_;
  std::optional<FreedomTravelState> travel_;
  std::optional<FreedomRecoveryState> recovery_;
  std::optional<CraftCondition> condition_;
  std::optional<CraftCondition> retired_condition_;
  PhysicalLocalSystem system_;
  PhysicalPlanetRotationRecipe rotation_;
  std::optional<PhysicalMoonDescriptor> moon_;
  std::optional<PhysicalMinorDescriptor> minor_;
  std::optional<std::filesystem::path> source_save_;
  std::optional<FreedomDockingState> docking_;
  std::optional<OriginWalkerState> actor_;
  std::optional<NativeStartingAssemblySelection> starting_assembly_;
  NativeCraftBinding craft_binding_;
  std::optional<FreedomBoardingState> boarding_;
  NativePortApproach port_approach_;
  NativePortDocking port_docking_;
  NativePortRendezvous port_rendezvous_;
  NativeFlightControls rendezvous_held_{};
  std::uint32_t rendezvous_held_ticks_{};
  std::optional<FreedomSurfaceState> surface_;
  std::optional<TerrainPadGapSample> gear_clearance_;
  StarterCraftId gear_clearance_craft_{};
  NativeSurfaceManeuver surface_maneuver_;
  bool surface_walk_selected_{};
  std::optional<PlanetSurfaceWalkerState> surface_walker_;
  std::optional<PlanetSurfaceWalkStatus> surface_walk_blockage_;
  std::shared_ptr<PlanetSurfaceWalkTerrain> surface_walk_terrain_;
  std::shared_ptr<MoonSurfaceWalkTerrain> moon_walk_terrain_;
  // Pure derived terrain memoization, never a saved world or actor owner.
  mutable std::shared_ptr<SurfaceLandmarkField> surface_landmark_field_;
  mutable std::shared_ptr<MoonSurfaceLandmarkField>
      moon_surface_landmark_field_;
  mutable SystemId surface_landmark_system_{};
  mutable PlanetId surface_landmark_planet_{};
  mutable std::uint32_t surface_landmark_policy_{};
};
} // namespace apsis_drift
