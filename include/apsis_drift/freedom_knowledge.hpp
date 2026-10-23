#pragma once

#include "apsis_drift/freedom_derelict.hpp"
#include "apsis_drift/freedom_navigation.hpp"
#include "apsis_drift/moon_terrain.hpp"
#include "apsis_drift/planet_ambient.hpp"
#include "apsis_drift/surface_landmarks.hpp"
#include "apsis_drift/survey_commissions.hpp"

#include <variant>

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomKnowledgeVersion{1};
inline constexpr std::uint32_t kFreedomObservedKnowledgeVersion{2};
inline constexpr std::uint32_t kFreedomTravelKnowledgeVersion{3};
inline constexpr std::uint32_t kFreedomNetworkKnowledgeVersion{4};
inline constexpr std::uint32_t kFreedomPlanetaryKnowledgeVersion{5};
inline constexpr std::uint32_t kFreedomUnchartedKnowledgeVersion{6};
inline constexpr std::uint32_t kFreedomUnchartedStartingChartVersion{4};
inline constexpr std::uint32_t kFreedomStarSearchVersion{1};
inline constexpr std::uint32_t kFreedomStarSearchTicks{1200};
inline constexpr std::uint32_t kFreedomStartingChartVersion{1};
inline constexpr std::uint32_t kFreedomPlanetaryStartingChartVersion{3};
[[nodiscard]] constexpr auto freedom_knowledge_uses_network(
    std::uint32_t version) -> bool {
  return version == kFreedomNetworkKnowledgeVersion ||
         version == kFreedomPlanetaryKnowledgeVersion ||
         version == kFreedomUnchartedKnowledgeVersion;
}
[[nodiscard]] constexpr auto freedom_knowledge_has_planetary_chart(
    std::uint32_t version) -> bool {
  return version == kFreedomPlanetaryKnowledgeVersion ||
         version == kFreedomUnchartedKnowledgeVersion;
}
[[nodiscard]] constexpr auto freedom_knowledge_network_version(
    std::uint32_t version) -> std::uint32_t {
  return version == kFreedomUnchartedKnowledgeVersion ? 2U : 1U;
}
[[nodiscard]] constexpr auto freedom_knowledge_chart_version(
    std::uint32_t version) -> std::uint32_t {
  return version == kFreedomUnchartedKnowledgeVersion   ? 4U
         : version == kFreedomPlanetaryKnowledgeVersion ? 3U
                                                        : 2U;
}
inline constexpr std::uint32_t kFreedomSurfaceMarkerVersion{1};
inline constexpr std::uint32_t kFreedomMoonSurfaceMarkerVersion{2};
inline constexpr std::uint32_t kFreedomNetworkMoonSurfaceMarkerVersion{3};
inline constexpr std::uint32_t kFreedomSurfaceLandmarkVersion{1};
inline constexpr std::uint32_t kFreedomMoonSurfaceLandmarkVersion{1};
inline constexpr std::uint32_t kFreedomNetworkMoonSurfaceLandmarkVersion{2};
inline constexpr std::size_t kMaximumFreedomSurfaceLandmarkInspections{64};
inline constexpr std::size_t kMaximumFreedomKnowledgeFacts{128};
inline constexpr std::size_t kMaximumFreedomNetworkKnowledgeFacts{256};
inline constexpr std::size_t kMaximumFreedomUnchartedKnowledgeFacts{384};
inline constexpr std::size_t kMaximumFreedomKnowledgeTransitions{4};
inline constexpr std::uint32_t kFreedomOrbitalSurveyVersion{1};
inline constexpr std::uint32_t kFreedomOrbitalSurveyTicks{3600};
inline constexpr std::size_t kMaximumFreedomOrbitalSurveys{16};
inline constexpr std::uint32_t kFreedomFieldSamplesVersion{1};
inline constexpr std::uint32_t kFreedomNetworkFieldSamplesVersion{2};
inline constexpr std::size_t kFreedomFieldSampleCapacity{4};

// Preserve the existing typed identities. Future BodyId hierarchy adds an
// explicit subject kind/version; it never silently reinterprets PlanetId.
enum class KnowledgeSubjectKind : std::uint8_t { system, planet, station };
struct KnowledgeSubject {
  KnowledgeSubjectKind kind{};
  SystemId system;
  std::uint64_t identity{};
  friend auto operator==(const KnowledgeSubject&, const KnowledgeSubject&)
      -> bool = default;
};
enum class KnowledgeFact : std::uint8_t {
  identity_type,
  location,
  physical,
  atmosphere,
  hazards,
  landing,
  station_ports,
  presence
};
enum class KnowledgeSource : std::uint8_t {
  starting_chart,
  local_ship,
  physical_arrival
};
struct KnowledgeTransition {
  NavigationKnowledgeLevel level{NavigationKnowledgeLevel::contact};
  KnowledgeSource source{KnowledgeSource::local_ship};
  std::uint64_t source_id{};
  SimulationTick tick{};
  StarterCraftId flight;
  friend auto operator==(const KnowledgeTransition&, const KnowledgeTransition&)
      -> bool = default;
};
struct KnowledgeEntry {
  KnowledgeSubject subject;
  KnowledgeFact fact{};
  // Ordered strict confidence advances; duplicate readings add no history.
  std::vector<KnowledgeTransition> transitions;
  friend auto operator==(const KnowledgeEntry&, const KnowledgeEntry&)
      -> bool = default;
};
struct FreedomKnowledgeRecipe {
  std::uint32_t version{kFreedomKnowledgeVersion};
  Seed universe_seed;
  std::uint32_t chart{kFreedomStartingChartVersion};
  std::uint32_t topology{kUniverseNavigationVersion};
  std::uint32_t physical_catalog{
      kContinuousPhysicalLocalSystemGeneratorVersion};
  std::uint32_t ephemeris{kContinuousAnalyticEphemerisVersion};
  std::uint32_t ambient{kPlanetAmbientGeneratorVersion};
  // Version 1 stays unselected; version 2 explicitly selects local sensors.
  std::uint32_t observation_policy{};
  // Version 3 registers the same seeded neighbor's planets. Earlier domains
  // stay origin-only; registration itself grants no observations. Version4
  // selects the explicit four-system domain2/chart2/topology2. Version5
  // additionally grants ordinary-planet identity/location through chart3.
  // Version6/chart4 selects network2: the same four charted systems and two
  // initially unknown systems whose anchors require actual stellar acquisition.
  std::uint32_t world_domain{};
  // Explicit recovery selection bounds historical craft provenance to the
  // admitted generations. Absence retains the exact starter-only contract.
  std::optional<FreedomCraftLineage> craft_lineage{};
  friend auto operator==(const FreedomKnowledgeRecipe&,
                         const FreedomKnowledgeRecipe&) -> bool = default;
};
// Personal navigation metadata, separate from generated truth and sensor facts.
// Each point stays fixed to its typed body; craft replacement/travel cannot
// move it. Keep the original planetary type and version1 encoding unchanged.
struct FreedomSurfaceMarkerPoint {
  SystemId system;
  PlanetId planet;
  PlanetFixedPositionMetres position;
  SimulationTick tick{};
  friend auto operator==(const FreedomSurfaceMarkerPoint&,
                         const FreedomSurfaceMarkerPoint&) -> bool = default;
};
struct FreedomMoonSurfaceMarkerPoint {
  SystemId system;
  BodyId body;
  MoonFixedPositionMetres position;
  SimulationTick tick{};
  friend auto operator==(const FreedomMoonSurfaceMarkerPoint&,
                         const FreedomMoonSurfaceMarkerPoint&)
      -> bool = default;
};
using FreedomSurfaceMarkerLocation =
    std::variant<FreedomSurfaceMarkerPoint, FreedomMoonSurfaceMarkerPoint>;
struct FreedomSurfaceMarkerState {
  std::uint32_t version{kFreedomSurfaceMarkerVersion};
  std::optional<FreedomSurfaceMarkerLocation> point;
  friend auto operator==(const FreedomSurfaceMarkerState&,
                         const FreedomSurfaceMarkerState&) -> bool = default;
};
// Personal inspection history, independent of craft lineage and sensor facts.
// The seeded identity is authenticated without replaying contact geometry.
struct FreedomSurfaceLandmarkInspection {
  SystemId system;
  PlanetId planet;
  SurfaceLandmarkCell cell;
  SurfaceLandmarkId site;
  std::uint32_t terrain_policy{};
  SimulationTick tick{};
  friend auto operator==(const FreedomSurfaceLandmarkInspection&,
                         const FreedomSurfaceLandmarkInspection&)
      -> bool = default;
};
struct FreedomSurfaceLandmarkState {
  std::uint32_t version{kFreedomSurfaceLandmarkVersion};
  std::vector<FreedomSurfaceLandmarkInspection> inspections;
  friend auto operator==(const FreedomSurfaceLandmarkState&,
                         const FreedomSurfaceLandmarkState&) -> bool = default;
};
// Optional Moon history uses its genuine typed target and independent recipe.
// Absence stays absent in historical and default journey encodings.
struct FreedomMoonSurfaceLandmarkInspection {
  BodyTarget target;
  SurfaceLandmarkCell cell;
  SurfaceLandmarkId site;
  std::uint32_t terrain_recipe{kMoonTerrainRecipeVersion};
  SimulationTick tick{};
  friend auto operator==(const FreedomMoonSurfaceLandmarkInspection&,
                         const FreedomMoonSurfaceLandmarkInspection&)
      -> bool = default;
};
struct FreedomMoonSurfaceLandmarkState {
  std::uint32_t version{kFreedomMoonSurfaceLandmarkVersion};
  std::vector<FreedomMoonSurfaceLandmarkInspection> inspections;
  friend auto operator==(const FreedomMoonSurfaceLandmarkState&,
                         const FreedomMoonSurfaceLandmarkState&)
      -> bool = default;
};
struct FreedomOrbitalSurveyAcquisition {
  SystemId system;
  PlanetId planet;
  StarterCraftId craft;
  SimulationTick started_tick{};
  std::uint32_t acquired_ticks{};
  friend auto operator==(const FreedomOrbitalSurveyAcquisition&,
                         const FreedomOrbitalSurveyAcquisition&)
      -> bool = default;
};
struct FreedomOrbitalSurveyRecord {
  SystemId system;
  PlanetId planet;
  StarterCraftId craft;
  SimulationTick started_tick{};
  SimulationTick completed_tick{};
  friend auto operator==(const FreedomOrbitalSurveyRecord&,
                         const FreedomOrbitalSurveyRecord&) -> bool = default;
};
struct FreedomOrbitalSurveyState {
  std::uint32_t version{kFreedomOrbitalSurveyVersion};
  std::optional<FreedomOrbitalSurveyAcquisition> active;
  std::vector<FreedomOrbitalSurveyRecord> completed;
  friend auto operator==(const FreedomOrbitalSurveyState&,
                         const FreedomOrbitalSurveyState&) -> bool = default;
};
// Physical pouch contents and first-collection notes are separate. Source
// authentication is not a composition assay or a navigation fact.
struct FreedomFieldSampleRecord {
  BodyTarget target;
  SurfaceLandmarkCell cell;
  SurfaceLandmarkId site;
  std::uint32_t terrain_recipe{};
  SurfaceLandmarkKind kind{};
  StarterCraftId craft;
  SimulationTick tick{};
  friend auto operator==(const FreedomFieldSampleRecord&,
                         const FreedomFieldSampleRecord&) -> bool = default;
};
struct FreedomFieldSampleState {
  std::uint32_t version{kFreedomFieldSamplesVersion};
  StarterCraftId carrier;
  std::vector<FreedomFieldSampleRecord> carried;
  std::vector<FreedomFieldSampleRecord> journal;
  friend auto operator==(const FreedomFieldSampleState&,
                         const FreedomFieldSampleState&) -> bool = default;
};
// Private acquisition target stays in the authoritative ledger; the native
// progress view reveals no target identity or coordinates until completion.
struct FreedomStarSearchAcquisition {
  SystemId source, target;
  StarterCraftId craft;
  SimulationTick started_tick{}, last_tick{};
  std::uint32_t acquired_ticks{};
  friend auto operator==(const FreedomStarSearchAcquisition&,
                         const FreedomStarSearchAcquisition&) -> bool = default;
};
struct FreedomStarSearchRecord {
  SystemId source, target;
  StarterCraftId craft;
  SimulationTick started_tick{}, completed_tick{};
  friend auto operator==(const FreedomStarSearchRecord&,
                         const FreedomStarSearchRecord&) -> bool = default;
};
struct FreedomStarSearchState {
  std::uint32_t version{kFreedomStarSearchVersion};
  std::optional<FreedomStarSearchAcquisition> active;
  // Acquisition order, at most two records; never a generated catalog dump.
  std::vector<FreedomStarSearchRecord> completed;
  friend auto operator==(const FreedomStarSearchState&,
                         const FreedomStarSearchState&) -> bool = default;
};
inline constexpr std::uint32_t kFreedomStationStellarLeadVersion{1};
// One immutable personal note. The private target is not a navigation fact or
// part of the public attendant offer; search/presence evidence owns its status.
struct FreedomStationStellarLead {
  std::uint32_t version{kFreedomStationStellarLeadVersion};
  OriginStationId station;
  std::uint32_t attendant{1};
  StarterCraftId craft;
  SimulationTick received_tick{};
  SystemId target;
  friend auto operator==(const FreedomStationStellarLead&,
                         const FreedomStationStellarLead&) -> bool = default;
};
enum class FreedomStationStellarLeadStatus : std::uint8_t {
  absent,
  unverified,
  sensor_fix_acquired,
  visited
};
struct FreedomKnowledge {
  FreedomKnowledgeRecipe recipe;
  std::vector<KnowledgeEntry> entries;
  // Absence preserves historical knowledge JSON. A selected empty marker
  // retains its explicit version after the pilot clears the point.
  std::optional<FreedomSurfaceMarkerState> surface_marker{};
  std::optional<FreedomSurfaceLandmarkState> surface_landmarks{};
  std::optional<FreedomMoonSurfaceLandmarkState> moon_surface_landmarks{};
  // Explicit player-selected acquisition; absence preserves historical bytes.
  std::optional<FreedomOrbitalSurveyState> orbital_surveys{};
  std::optional<FreedomFieldSampleState> field_samples{};
  std::optional<FreedomStarSearchState> star_search{};
  std::optional<FreedomDerelictState> derelicts{};
  std::optional<FreedomStationStellarLead> station_stellar_lead{};
  std::optional<SurveyCommissionAccount> survey_commissions{};
  friend auto operator==(const FreedomKnowledge&, const FreedomKnowledge&)
      -> bool = default;
};
struct KnowledgeEvidence {
  KnowledgeSubject subject;
  KnowledgeFact fact{};
  KnowledgeTransition transition;
};
enum class FreedomKnowledgeError : std::uint8_t {
  unsupported_recipe,
  invalid_subject,
  invalid_fact,
  invalid_source,
  invalid_transition,
  invalid_tick,
  invalid_ledger,
  oversized_ledger,
  invalid_surface_marker,
  invalid_surface_landmarks,
  invalid_orbital_surveys,
  invalid_field_samples,
  invalid_star_search,
  invalid_derelicts,
  invalid_station_stellar_lead,
  invalid_survey_commissions
};

struct KnownIdentity {
  std::string name;
  friend auto operator==(const KnownIdentity&, const KnownIdentity&)
      -> bool = default;
};
struct KnownOrbit {
  std::uint64_t radius_kilometres{};
  SimulationTick period_ticks{};
  std::uint32_t epoch_phase_turns{};
  std::int32_t inclination_microdegrees{};
  std::uint32_t ascending_node_turns{};
  std::optional<PlanetId> host_planet;
  friend auto operator==(const KnownOrbit&, const KnownOrbit&)
      -> bool = default;
};
struct KnownPhysical {
  std::uint32_t radius_kilometres{};
  std::uint16_t gravity_milli_g{};
  friend auto operator==(const KnownPhysical&, const KnownPhysical&)
      -> bool = default;
};
struct KnownAtmosphere {
  AtmosphereClass kind{};
  std::uint16_t pressure_millibars{};
  friend auto operator==(const KnownAtmosphere&, const KnownAtmosphere&)
      -> bool = default;
};
// No ambient recipe, planet seed, gravity or atmosphere travels through a
// hazard reading. Those are separately known facts or private generated truth.
struct KnownHazards {
  std::uint32_t min_temperature_mk{}, max_temperature_mk{};
  std::uint32_t radiation_nsv_per_hour{}, electric_field_v_per_m{};
  std::uint32_t gust_mm_per_second{}, ground_acceleration_mm_per_second2{};
  AtmosphericVisibilityKind visibility{};
  std::uint16_t maximum_obscuration_basis_points{};
  friend auto operator==(const KnownHazards&, const KnownHazards&)
      -> bool = default;
};
struct KnownLanding {
  TerrainCharacter character{};
  std::uint16_t water_basis_points{};
  friend auto operator==(const KnownLanding&, const KnownLanding&)
      -> bool = default;
};
struct KnownStationPorts {
  std::uint32_t count{};
  friend auto operator==(const KnownStationPorts&, const KnownStationPorts&)
      -> bool = default;
};
struct KnownPresence {
  SimulationTick arrival_tick{};
  friend auto operator==(const KnownPresence&, const KnownPresence&)
      -> bool = default;
};
using KnowledgeValue =
    std::variant<std::monostate, KnownIdentity, UniversePositionMetres,
                 KnownOrbit, KnownPhysical, KnownAtmosphere, KnownHazards,
                 KnownLanding, KnownStationPorts, KnownPresence>;
struct KnowledgeReading {
  KnowledgeSubject subject;
  KnowledgeFact fact{};
  KnowledgeTransition provenance;
  // CONTACT/PROBABLE have no exact generated value.
  KnowledgeValue value;
  friend auto operator==(const KnowledgeReading&, const KnowledgeReading&)
      -> bool = default;
};

struct FreedomKnownPlanetRow {
  BodyTarget target;
  std::string name;
  bool current{}, visited{};
  friend auto operator==(const FreedomKnownPlanetRow&,
                         const FreedomKnownPlanetRow&) -> bool = default;
};

[[nodiscard]] auto make_freedom_starting_knowledge(
    Seed, std::uint32_t version = kFreedomKnowledgeVersion)
    -> std::expected<FreedomKnowledge, FreedomKnowledgeError>;
[[nodiscard]] auto validate_freedom_knowledge(const FreedomKnowledge&,
                                              SimulationTick now)
    -> std::expected<void, FreedomKnowledgeError>;
// Local application observations only. #189 owns actual equipment/scan and
// arrival triggers. Presentation has no writer; no probe/mission sources yet.
[[nodiscard]] auto apply_freedom_knowledge(const FreedomKnowledge&,
                                           const KnowledgeEvidence&,
                                           SimulationTick now)
    -> std::expected<FreedomKnowledge, FreedomKnowledgeError>;
// Preview is read-only. Receive requires the exact freshly generated offer;
// native admission additionally owns the actual Origin attendant interaction.
[[nodiscard]] auto offer_freedom_station_stellar_lead(const FreedomKnowledge&,
                                                      StarterCraftId,
                                                      SimulationTick)
    -> std::expected<FreedomStationStellarLead, FreedomKnowledgeError>;
[[nodiscard]] auto receive_freedom_station_stellar_lead(
    const FreedomKnowledge&, const FreedomStationStellarLead&, SimulationTick)
    -> std::expected<FreedomKnowledge, FreedomKnowledgeError>;
[[nodiscard]] auto freedom_station_stellar_lead_status(const FreedomKnowledge&,
                                                       SimulationTick)
    -> std::expected<FreedomStationStellarLeadStatus, FreedomKnowledgeError>;
[[nodiscard]] auto begin_freedom_star_search(const FreedomKnowledge&, SystemId,
                                             StarterCraftId, SimulationTick)
    -> std::expected<FreedomKnowledge, FreedomKnowledgeError>;
[[nodiscard]] auto advance_freedom_star_search(const FreedomKnowledge&,
                                               SystemId, StarterCraftId,
                                               SimulationTick, bool qualifying)
    -> std::expected<FreedomKnowledge, FreedomKnowledgeError>;
[[nodiscard]] auto query_freedom_knowledge(const FreedomKnowledge&,
                                           KnowledgeSubject, KnowledgeFact,
                                           SimulationTick now)
    -> std::expected<std::optional<KnowledgeReading>, FreedomKnowledgeError>;
[[nodiscard]] auto known_freedom_subjects(const FreedomKnowledge&,
                                          SimulationTick now)
    -> std::expected<std::vector<KnowledgeSubject>, FreedomKnowledgeError>;
// Selected chart3 only. Enumerate resolved identity/location evidence first;
// authentic catalog entries then resolve those admitted IDs, never grant them.
[[nodiscard]] auto resolve_freedom_known_planets(const FreedomKnowledge&,
                                                 BodyTarget current,
                                                 SimulationTick now)
    -> std::expected<std::vector<FreedomKnownPlanetRow>, FreedomKnowledgeError>;
[[nodiscard]] auto resolve_freedom_knowledge_chart(
    const FreedomKnowledge&, SystemId current, const FreedomResources&,
    bool selection_open = true,
    const FreedomSaveDocument* instance_owner = nullptr)
    -> std::expected<UniverseNavigationView, FreedomKnowledgeError>;
} // namespace apsis_drift
