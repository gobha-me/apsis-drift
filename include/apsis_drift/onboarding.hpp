#pragma once

#include <cstdint>
#include <expected>
#include <optional>

namespace apsis_drift {

enum class OnboardingState : std::uint8_t {
  guided,
  skipped,
  completed,
};

enum class OnboardingChapter : std::uint8_t {
  contract_one,
  contract_two,
  contract_three,
};

struct OnboardingProgress {
  OnboardingState state{OnboardingState::guided};
  std::optional<OnboardingChapter> chapter{OnboardingChapter::contract_one};

  friend auto operator==(const OnboardingProgress&, const OnboardingProgress&)
      -> bool = default;
};

enum class NewGameOnboardingChoice : std::uint8_t {
  guided,
  skip,
};

enum class OnboardingCommand : std::uint8_t {
  complete_contract_one,
  complete_contract_two,
  complete_contract_three,
};

enum class OnboardingError : std::uint8_t {
  invalid_state,
  invalid_transition,
};

struct OnboardingAccess {
  bool origin_station_known{true};
  bool home_planet_known{true};
  bool origin_system_chart_known{true};
  bool first_jump_solution_available{};
  bool open_exploration_available{};

  friend auto operator==(const OnboardingAccess&, const OnboardingAccess&)
      -> bool = default;
};

[[nodiscard]] auto initial_onboarding_progress(
    NewGameOnboardingChoice choice) noexcept -> OnboardingProgress;

[[nodiscard]] auto validate_onboarding_progress(
    const OnboardingProgress& progress) noexcept -> bool;

[[nodiscard]] auto resolve_onboarding_access(
    const OnboardingProgress& progress) noexcept
    -> std::expected<OnboardingAccess, OnboardingError>;

[[nodiscard]] auto advance_onboarding(OnboardingProgress& progress,
                                      OnboardingCommand command) noexcept
    -> std::expected<void, OnboardingError>;

} // namespace apsis_drift
