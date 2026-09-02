#include "apsis_drift/onboarding.hpp"

namespace apsis_drift {

auto initial_onboarding_progress(NewGameOnboardingChoice choice) noexcept
    -> OnboardingProgress {
  switch (choice) {
    case NewGameOnboardingChoice::guided: return {};
    case NewGameOnboardingChoice::skip:
      return {.state = OnboardingState::skipped, .chapter = std::nullopt};
  }
  return {.state = static_cast<OnboardingState>(255), .chapter = std::nullopt};
}

auto validate_onboarding_progress(const OnboardingProgress& progress) noexcept
    -> bool {
  switch (progress.state) {
    case OnboardingState::guided:
      if (!progress.chapter) return false;
      switch (*progress.chapter) {
        case OnboardingChapter::contract_one:
        case OnboardingChapter::contract_two:
        case OnboardingChapter::contract_three: return true;
      }
      return false;
    case OnboardingState::skipped:
    case OnboardingState::completed: return !progress.chapter;
  }
  return false;
}

auto resolve_onboarding_access(const OnboardingProgress& progress) noexcept
    -> std::expected<OnboardingAccess, OnboardingError> {
  if (!validate_onboarding_progress(progress)) {
    return std::unexpected{OnboardingError::invalid_state};
  }
  const bool post_onboarding = progress.state == OnboardingState::skipped ||
                               progress.state == OnboardingState::completed;
  const bool first_jump =
      post_onboarding ||
      (progress.state == OnboardingState::guided &&
       progress.chapter == OnboardingChapter::contract_three);
  return OnboardingAccess{
      .origin_station_known = true,
      .home_planet_known = true,
      .origin_system_chart_known = true,
      .first_jump_solution_available = first_jump,
      .open_exploration_available = post_onboarding,
  };
}

auto advance_onboarding(OnboardingProgress& progress,
                        OnboardingCommand command) noexcept
    -> std::expected<void, OnboardingError> {
  if (!validate_onboarding_progress(progress)) {
    return std::unexpected{OnboardingError::invalid_state};
  }
  if (progress.state != OnboardingState::guided || !progress.chapter) {
    return std::unexpected{OnboardingError::invalid_transition};
  }

  auto next = progress;
  switch (command) {
    case OnboardingCommand::complete_contract_one:
      if (*progress.chapter != OnboardingChapter::contract_one) {
        return std::unexpected{OnboardingError::invalid_transition};
      }
      next.chapter = OnboardingChapter::contract_two;
      break;
    case OnboardingCommand::complete_contract_two:
      if (*progress.chapter != OnboardingChapter::contract_two) {
        return std::unexpected{OnboardingError::invalid_transition};
      }
      next.chapter = OnboardingChapter::contract_three;
      break;
    case OnboardingCommand::complete_contract_three:
      if (*progress.chapter != OnboardingChapter::contract_three) {
        return std::unexpected{OnboardingError::invalid_transition};
      }
      next.state = OnboardingState::completed;
      next.chapter.reset();
      break;
    default: return std::unexpected{OnboardingError::invalid_transition};
  }
  progress = next;
  return {};
}

} // namespace apsis_drift
