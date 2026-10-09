#pragma once

#include "apsis_drift/freedom_navigation.hpp"
#include "apsis_drift/planet_ambient.hpp"

#include <variant>

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomKnowledgeVersion{1};
inline constexpr std::uint32_t kFreedomObservedKnowledgeVersion{2};
inline constexpr std::uint32_t kFreedomStartingChartVersion{1};
inline constexpr std::size_t kMaximumFreedomKnowledgeFacts{128};
inline constexpr std::size_t kMaximumFreedomKnowledgeTransitions{4};

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
  friend auto operator==(const FreedomKnowledgeRecipe&,
                         const FreedomKnowledgeRecipe&) -> bool = default;
};
struct FreedomKnowledge {
  FreedomKnowledgeRecipe recipe;
  std::vector<KnowledgeEntry> entries;
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
  oversized_ledger
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
[[nodiscard]] auto query_freedom_knowledge(const FreedomKnowledge&,
                                           KnowledgeSubject, KnowledgeFact,
                                           SimulationTick now)
    -> std::expected<std::optional<KnowledgeReading>, FreedomKnowledgeError>;
[[nodiscard]] auto known_freedom_subjects(const FreedomKnowledge&,
                                          SimulationTick now)
    -> std::expected<std::vector<KnowledgeSubject>, FreedomKnowledgeError>;
[[nodiscard]] auto resolve_freedom_knowledge_chart(const FreedomKnowledge&,
                                                   SystemId current,
                                                   const FreedomResources&,
                                                   bool selection_open = true)
    -> std::expected<UniverseNavigationView, FreedomKnowledgeError>;
} // namespace apsis_drift
