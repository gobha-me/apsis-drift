#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "termforge/core/types.hpp"
#include "apsis_drift/intersystem_contract.hpp"

namespace apsis_drift {

inline constexpr std::uint64_t kIntersystemJumpAcceptanceSeed{42};
inline constexpr std::size_t kMaximumIntersystemJumpAcceptancePixels{
    4096U * 4096U};

enum class IntersystemJumpAcceptanceError : std::uint8_t {
  invalid_configuration,
  transition_failure,
  persistence_failure,
  render_failure,
};

struct IntersystemJumpAcceptanceReport {
  SystemId destination;
  PlanetId reference_planet;
  SimulationTick committed_tick{};
  SimulationTick arrival_tick{};
  std::uint64_t arrival_checksum{};
  std::uint64_t framebuffer_checksum{};
  int width{};
  int height{};
};

struct IntersystemJumpAcceptanceResult {
  IntersystemJumpAcceptanceReport report;
  std::vector<termforge::Pixel> transit_frame;
};

[[nodiscard]] auto run_intersystem_jump_acceptance(int width, int height)
    -> std::expected<IntersystemJumpAcceptanceResult,
                     IntersystemJumpAcceptanceError>;

[[nodiscard]] auto intersystem_jump_acceptance_json(
    const IntersystemJumpAcceptanceReport& report,
    std::string_view presentation) -> std::string;

}  // namespace apsis_drift
