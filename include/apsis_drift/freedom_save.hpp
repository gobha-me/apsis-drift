#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/save_schema.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kFreedomSaveFormatVersion{17};

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

  friend auto operator==(const FreedomSaveDocument&, const FreedomSaveDocument&)
      -> bool = default;
};

[[nodiscard]] auto make_freedom_new_game_document(Seed universe_seed)
    -> FreedomSaveDocument;
[[nodiscard]] auto validate_freedom_save_document(
    const FreedomSaveDocument& document)
    -> std::expected<void, SaveSchemaError>;
[[nodiscard]] auto encode_freedom_save_document_json(
    const FreedomSaveDocument& document)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_freedom_save_document_json(std::string_view json_text)
    -> std::expected<FreedomSaveDocument, SaveSchemaError>;

} // namespace apsis_drift
