#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/save_schema.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kFreedomSaveFormatVersion{17};
inline constexpr std::uint32_t kFreedomCraftLineageSaveFormatVersion{28};
inline constexpr std::uint32_t kFreedomCraftLineageVersion{1};
inline constexpr std::uint32_t kFreedomNetworkSaveFormatVersion{49};
inline constexpr std::uint32_t kFreedomNetworkSelectionVersion{1};
inline constexpr std::uint32_t kFreedomDerelictSaveFormatVersion{60};
inline constexpr std::uint32_t kFreedomDerelictSelectionVersion{1};
struct FreedomDerelictSelection {
  std::uint32_t version{kFreedomDerelictSelectionVersion};
  friend auto operator==(const FreedomDerelictSelection&,
                         const FreedomDerelictSelection&) -> bool = default;
};
// Explicit opt-in descriptor; the bounded catalog is regenerated from the seed.
struct FreedomNetworkSelection {
  std::uint32_t version{kFreedomNetworkSelectionVersion};
  friend auto operator==(const FreedomNetworkSelection&,
                         const FreedomNetworkSelection&) -> bool = default;
};
struct FreedomCraftLineage {
  std::uint32_t version{kFreedomCraftLineageVersion};
  std::uint64_t generation{};
  friend auto operator==(const FreedomCraftLineage&, const FreedomCraftLineage&)
      -> bool = default;
};

struct StarterCraftId {
  std::uint64_t value{};

  friend auto operator==(const StarterCraftId&, const StarterCraftId&)
      -> bool = default;
};

struct FreedomStationState {
  OriginStationId station;
  StarterCraftId craft;
  SimulationTick tick{};
  std::vector<SaveDiscovery> discoveries;
  std::vector<SaveWorldDelta> world_deltas;

  friend auto operator==(const FreedomStationState&, const FreedomStationState&)
      -> bool = default;
};

struct FreedomSaveDocument {
  SaveRecipe recipe;
  FreedomStationState state;
  std::optional<FreedomCraftLineage> lineage{};
  std::optional<FreedomNetworkSelection> network{};
  std::optional<FreedomDerelictSelection> derelicts{};

  friend auto operator==(const FreedomSaveDocument&, const FreedomSaveDocument&)
      -> bool = default;
};

// Generation zero is the unchanged legacy starter. Later generations use a
// nonzero identity permutation without consuming or changing world RNG streams.
[[nodiscard]] auto derive_freedom_craft_identity(Seed, std::uint64_t generation)
    -> std::expected<StarterCraftId, SaveSchemaError>;

[[nodiscard]] auto make_freedom_new_game_document(Seed universe_seed)
    -> FreedomSaveDocument;
[[nodiscard]] auto make_freedom_network_new_game_document(Seed universe_seed)
    -> std::expected<FreedomSaveDocument, SaveSchemaError>;
[[nodiscard]] auto validate_freedom_save_document(
    const FreedomSaveDocument& document)
    -> std::expected<void, SaveSchemaError>;
[[nodiscard]] auto encode_freedom_save_document_json(
    const FreedomSaveDocument& document)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_freedom_save_document_json(std::string_view json_text)
    -> std::expected<FreedomSaveDocument, SaveSchemaError>;

} // namespace apsis_drift
