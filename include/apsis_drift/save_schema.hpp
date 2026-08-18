#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/intersystem_contract.hpp"
#include "apsis_drift/origin_station.hpp"
#include "apsis_drift/planetary_flight.hpp"
#include "apsis_drift/surface_signals.hpp"

namespace apsis_drift {

inline constexpr std::string_view kSaveApplication{"apsis-drift"};
inline constexpr std::uint32_t kSaveFormatVersion{3};
inline constexpr std::size_t kMaximumSaveDocumentBytes{1U << 20U};
inline constexpr std::size_t kMaximumSaveDiscoveries{4'096};
inline constexpr std::size_t kMaximumSaveWorldDeltas{16'384};
inline constexpr std::size_t kMaximumSaveObjectKeyBytes{128};

struct SaveGeneratorVersions {
  std::uint32_t seed_derivation{};
  std::uint32_t planet_descriptor{};
  std::uint32_t terrain_tiles{};
  std::uint32_t origin_station{};
  std::uint32_t surface_signals{};
  std::uint32_t local_sun{};
  std::uint32_t local_system{};
  std::uint32_t analytic_ephemeris{};
  std::uint32_t intersystem_contract{};

  friend auto operator==(const SaveGeneratorVersions&,
                         const SaveGeneratorVersions&) -> bool = default;
};

struct SaveRecipe {
  Seed universe_seed;
  std::uint64_t origin_system_ordinal{};
  std::uint64_t active_planet_ordinal{};
  SaveGeneratorVersions generator_versions;
  OriginStationId origin_station;
  PlanetId active_planet;

  friend auto operator==(const SaveRecipe&, const SaveRecipe&)
      -> bool = default;
};

struct SaveDiscovery {
  SurfaceSignalId signal;
  SimulationTick tick{};

  friend auto operator==(const SaveDiscovery&, const SaveDiscovery&)
      -> bool = default;
};

enum class SaveWorldDeltaKind : std::uint8_t {
  discovered,
  collected,
  completed,
  removed,
};

struct SaveWorldDelta {
  std::string object_key;
  SaveWorldDeltaKind kind{};
  SimulationTick tick{};

  friend auto operator==(const SaveWorldDelta&, const SaveWorldDelta&)
      -> bool = default;
};

struct SaveMutableState {
  OriginLocation location{OriginLocation::docked_at_origin};
  FirstObjectiveStatus first_objective{FirstObjectiveStatus::offered};
  SurfaceSignalId first_objective_target;
  std::optional<PlanetaryFlightState> flight;
  std::vector<SaveDiscovery> discoveries;
  std::vector<SaveWorldDelta> world_deltas;
  // Present for the v3 first-contract career. Absence identifies a migrated
  // v1/v2 local Signal Run, which must never be silently retargeted.
  std::optional<IntersystemContractState> intersystem_contract;

  friend auto operator==(const SaveMutableState&, const SaveMutableState&)
      -> bool = default;
};

struct SaveDocument {
  SaveRecipe recipe;
  SaveMutableState state;

  friend auto operator==(const SaveDocument&, const SaveDocument&)
      -> bool = default;
};

enum class SaveSchemaErrorCode : std::uint8_t {
  document_too_large,
  malformed_json,
  duplicate_key,
  missing_field,
  invalid_type,
  invalid_value,
  unsupported_format_version,
  incompatible_generator_version,
  identity_mismatch,
  invalid_state,
};

struct SaveSchemaError {
  SaveSchemaErrorCode code{};
  std::string path;
  std::string detail;

  friend auto operator==(const SaveSchemaError&, const SaveSchemaError&)
      -> bool = default;
};

[[nodiscard]] auto current_save_generator_versions() noexcept
    -> SaveGeneratorVersions;
[[nodiscard]] auto make_save_recipe(Seed universe_seed,
                                    std::uint64_t active_planet_ordinal = 0)
    -> SaveRecipe;

[[nodiscard]] auto validate_save_document(const SaveDocument& document)
    -> std::expected<void, SaveSchemaError>;
[[nodiscard]] auto encode_save_document_json(const SaveDocument& document)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_save_document_json(std::string_view json_text)
    -> std::expected<SaveDocument, SaveSchemaError>;

}  // namespace apsis_drift
