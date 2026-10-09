#pragma once

#include "apsis_drift/freedom_starting_assembly_save.hpp"
#include "apsis_drift/origin_gameplay_boarding.hpp"

namespace apsis_drift {
inline constexpr std::uint32_t kFreedomBoardingSaveFormatVersion{22};
enum class FreedomBoardingPhase : std::uint8_t {
  station,
  boarding,
  seated,
  disembarking
};
struct FreedomBoardingState {
  FreedomBoardingPhase phase{FreedomBoardingPhase::station};
  OriginWalkerState station_entry;
  SimulationTick started_tick{};
  GameplayBoardingState route;
  friend auto operator==(const FreedomBoardingState&,
                         const FreedomBoardingState&) -> bool = default;
};
// Version19 remains the sole craft/clock/history owner. Assembly is the
// unchanged geometry/rest reference; operating hardware is derived from route
// progress.
struct FreedomBoardingSaveDocument {
  FreedomDockingSaveDocument voyage;
  NativeStartingAssemblySelection starting_assembly;
  FreedomBoardingState boarding;
  std::optional<OriginWalkerState> station_actor;
  friend auto operator==(const FreedomBoardingSaveDocument&,
                         const FreedomBoardingSaveDocument&) -> bool = default;
};
[[nodiscard]] auto validate_freedom_boarding_document(
    const FreedomBoardingSaveDocument&) -> std::expected<void, SaveSchemaError>;
[[nodiscard]] auto encode_freedom_boarding_document_json(
    const FreedomBoardingSaveDocument&)
    -> std::expected<std::string, SaveSchemaError>;
[[nodiscard]] auto decode_freedom_boarding_document_json(std::string_view)
    -> std::expected<FreedomBoardingSaveDocument, SaveSchemaError>;
} // namespace apsis_drift
