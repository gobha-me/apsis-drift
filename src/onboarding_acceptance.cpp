#include "apsis_drift/onboarding_acceptance.hpp"

#include <algorithm>
#include <cstdio>
#include <format>
#include <optional>
#include <utility>

#include "apsis_drift/intersystem_contract_acceptance.hpp"
#include "apsis_drift/menu.hpp"
#include "apsis_drift/origin_system_contract_acceptance.hpp"
#include "apsis_drift/save_file.hpp"
#include "apsis_drift/signal_run_acceptance.hpp"
#include "apsis_drift/simulation.hpp"

namespace apsis_drift {
namespace {

[[nodiscard]] auto checksum_document(const SaveDocument& document)
    -> std::expected<std::uint64_t, OnboardingAcceptanceError> {
  const auto encoded = encode_save_document_json(document);
  if (!encoded) {
    return std::unexpected{OnboardingAcceptanceError::persistence_failure};
  }
  std::uint64_t hash{1469598103934665603ULL};
  for (const unsigned char byte : *encoded) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

[[nodiscard]] auto history_empty(const SaveDocument& document) noexcept
    -> bool {
  return document.state.discoveries.empty() &&
         document.state.world_deltas.empty() &&
         document.state.origin_system_discoveries.empty() &&
         document.state.origin_system_world_deltas.empty() &&
         document.state.onboarding.state == OnboardingState::skipped &&
         !document.state.onboarding.chapter &&
         document.state.location == OriginLocation::docked_at_origin &&
         !document.state.flight && !document.state.system_flight &&
         !document.state.origin_station_flight &&
         document.state.first_objective == FirstObjectiveStatus::offered &&
         (!document.state.origin_system_contract ||
          document.state.origin_system_contract->phase ==
              OriginSystemContractPhase::offered) &&
         document.state.intersystem_contract &&
         document.state.intersystem_contract->mission_phase ==
             IntersystemMissionPhase::offered;
}

[[nodiscard]] auto session_flow_is_complete() noexcept -> bool {
  SessionController session{false, true};
  if (session.screen() != SessionScreen::title ||
      session.dispatch(MenuCommand::activate).to != SessionScreen::station ||
      session.start_flight().to != SessionScreen::flight ||
      session.dispatch(MenuCommand::escape).to != SessionScreen::paused ||
      session.dispatch(MenuCommand::escape).to != SessionScreen::flight ||
      session.dock_at_station().to != SessionScreen::station) {
    return false;
  }
  return true;
}

[[nodiscard]] auto post_onboarding_idle_is_stable(
    const SaveDocument& completed) -> bool {
  if (!completed.state.intersystem_contract) return false;
  auto idle = completed;
  const auto onboarding = idle.state.onboarding;
  const auto mission_phase = idle.state.intersystem_contract->mission_phase;
  const auto travel_phase = idle.state.intersystem_contract->travel_phase;
  const auto discoveries = idle.state.discoveries;
  const auto deltas = idle.state.world_deltas;
  const auto origin_discoveries = idle.state.origin_system_discoveries;
  const auto origin_deltas = idle.state.origin_system_world_deltas;
  if (!advance_intersystem_time(*idle.state.intersystem_contract,
                                10U * kSimulationHz) ||
      !validate_save_document(idle)) {
    return false;
  }
  return idle.state.onboarding == onboarding &&
         idle.state.intersystem_contract->mission_phase == mission_phase &&
         idle.state.intersystem_contract->travel_phase == travel_phase &&
         idle.state.discoveries == discoveries &&
         idle.state.world_deltas == deltas &&
         idle.state.origin_system_discoveries == origin_discoveries &&
         idle.state.origin_system_world_deltas == origin_deltas;
}

[[nodiscard]] auto total_discoveries(const SaveDocument& document) noexcept
    -> std::size_t {
  return document.state.discoveries.size() +
         document.state.origin_system_discoveries.size();
}

[[nodiscard]] auto total_world_deltas(const SaveDocument& document) noexcept
    -> std::size_t {
  return document.state.world_deltas.size() +
         document.state.origin_system_world_deltas.size();
}

}  // namespace

auto run_onboarding_acceptance(
    RenderConfiguration configuration,
    const std::filesystem::path& checkpoint_path)
    -> std::expected<OnboardingAcceptanceResult, OnboardingAcceptanceError> {
  if (!validate_viewport(configuration.viewport) || checkpoint_path.empty()) {
    return std::unexpected{
        OnboardingAcceptanceError::invalid_configuration};
  }
  auto signal = run_signal_run_acceptance(configuration, checkpoint_path);
  if (!signal) {
    return std::unexpected{OnboardingAcceptanceError::signal_run_failure};
  }

  OnboardingAcceptanceReport report{
      .render_configuration = configuration,
      .free_flight_redock_verified = true,
      .pause_resume_verified = session_flow_is_complete(),
  };
  std::optional<SaveDocument> canonical_final;
  std::vector<termforge::Pixel> canonical_frame;

  for (const auto& scenario : signal->completed_scenarios) {
    if (scenario.measurement.rule_profile !=
        IntersystemRuleProfile::assisted) {
      continue;
    }
    const auto seed = scenario.returned_save.recipe.universe_seed;
    auto origin = run_origin_system_contract_acceptance(
        scenario.returned_save, configuration.viewport.width,
        configuration.viewport.height);
    if (!origin) {
      std::fprintf(stderr, "onboarding seed %llu contract two failed (%u)\n",
                   static_cast<unsigned long long>(seed.value),
                   static_cast<unsigned>(origin.error()));
      return std::unexpected{
          OnboardingAcceptanceError::origin_system_failure};
    }
    const bool canonical = seed == Seed{kSignalRunAcceptanceSeed};
    auto intersystem = run_intersystem_contract_acceptance(
        origin->returned_save, configuration.viewport.width,
        configuration.viewport.height, canonical);
    if (!intersystem) {
      std::fprintf(stderr,
                   "onboarding seed %llu contract three failed (%u)\n",
                   static_cast<unsigned long long>(seed.value),
                   static_cast<unsigned>(intersystem.error()));
      return std::unexpected{OnboardingAcceptanceError::intersystem_failure};
    }

    const auto& completed = intersystem->returned_save;
    const auto access = resolve_onboarding_access(completed.state.onboarding);
    const auto guided_checksum = checksum_document(completed);
    const auto skipped = make_new_game_document(NewGameOptions{
        .universe_seed = seed,
        .penalty_mode = IntersystemRuleProfile::assisted,
        .onboarding = NewGameOnboardingChoice::skip,
    });
    const auto skipped_checksum = checksum_document(skipped);
    const auto skipped_access =
        resolve_onboarding_access(skipped.state.onboarding);
    const auto expected_origin_contract = initial_origin_system_contract(seed);
    if (!guided_checksum || !skipped_checksum) {
      return std::unexpected{
          OnboardingAcceptanceError::persistence_failure};
    }
    const bool identities_match =
        completed.recipe == skipped.recipe &&
        completed.state.intersystem_contract &&
        skipped.state.intersystem_contract &&
        completed.state.intersystem_contract->identities ==
            skipped.state.intersystem_contract->identities &&
        completed.state.origin_system_contract && expected_origin_contract &&
        completed.state.origin_system_contract->binding ==
            expected_origin_contract->binding;
    if (!identities_match) {
      return std::unexpected{OnboardingAcceptanceError::identity_mismatch};
    }
    if (!history_empty(skipped) || !skipped_access ||
        !skipped_access->open_exploration_available) {
      return std::unexpected{
          OnboardingAcceptanceError::skipped_history_failure};
    }
    const bool idle_stable = post_onboarding_idle_is_stable(completed);
    if (!access || !access->open_exploration_available || !idle_stable ||
        completed.state.onboarding.state != OnboardingState::completed ||
        completed.state.onboarding.chapter ||
        !completed.state.intersystem_contract) {
      return std::unexpected{OnboardingAcceptanceError::incomplete_path};
    }

    report.seeds.push_back({
        .seed = seed,
        .origin_station = completed.recipe.origin_station,
        .home_planet = completed.recipe.home_planet,
        .final_tick = completed.state.intersystem_contract->universe_tick,
        .guided_final_checksum = *guided_checksum,
        .skipped_baseline_checksum = *skipped_checksum,
        .guided_discovery_count = total_discoveries(completed),
        .guided_world_delta_count = total_world_deltas(completed),
        .save_checkpoint_count = signal->report.save_checkpoints.size() +
                                 origin->report.checkpoints.size() +
                                 intersystem->report.checkpoints.size(),
        .immutable_identities_match = identities_match,
        .open_exploration_available = true,
        .skipped_history_empty = true,
        .post_onboarding_idle_stable = true,
    });
    report.contract_three_simulation_ms +=
        intersystem->report.simulation_ms;
    report.contract_three_application_render_ms +=
        intersystem->report.application_render_ms;
    if (canonical) {
      report.pilot_recovery_checksum =
          intersystem->report.wrong_side_recovery_checksum;
      report.pilot_recovery_verified =
          report.pilot_recovery_checksum != 0U;
      canonical_final = completed;
      canonical_frame = std::move(intersystem->final_frame);
    }
  }

  std::ranges::sort(report.seeds, [](const auto& left, const auto& right) {
    return left.seed.value < right.seed.value;
  });
  report.guided_new_game_verified = report.seeds.size() == 3U;
  report.skipped_new_game_verified = report.seeds.size() == 3U;
  if (report.seeds.size() != 3U || !canonical_final ||
      canonical_frame.empty() || !report.pause_resume_verified ||
      !report.pilot_recovery_verified || !report.guided_new_game_verified ||
      !report.skipped_new_game_verified) {
    return std::unexpected{OnboardingAcceptanceError::incomplete_path};
  }
  return OnboardingAcceptanceResult{
      .report = std::move(report),
      .returned_save = std::move(*canonical_final),
      .final_frame = std::move(canonical_frame),
  };
}

auto onboarding_acceptance_json(const OnboardingAcceptanceReport& report)
    -> std::string {
  std::string seeds;
  for (std::size_t index = 0; index < report.seeds.size(); ++index) {
    const auto& seed = report.seeds[index];
    seeds += std::format(
        "    {{\"seed\": \"{}\", \"origin_station_id\": \"{}\", "
        "\"home_planet_id\": \"planet-{:016x}\", \"final_tick\": \"{}\", "
        "\"guided_final_checksum\": \"{}\", "
        "\"skipped_baseline_checksum\": \"{}\", "
        "\"guided_discovery_count\": {}, \"guided_world_delta_count\": {}, "
        "\"save_checkpoint_count\": {}, \"immutable_identities_match\": {}, "
        "\"open_exploration_available\": {}, \"skipped_history_empty\": {}, "
        "\"post_onboarding_idle_stable\": {}}}{}\n",
        seed.seed.value, origin_station_id_string(seed.origin_station),
        seed.home_planet.value, seed.final_tick, seed.guided_final_checksum,
        seed.skipped_baseline_checksum, seed.guided_discovery_count,
        seed.guided_world_delta_count, seed.save_checkpoint_count,
        seed.immutable_identities_match ? "true" : "false",
        seed.open_exploration_available ? "true" : "false",
        seed.skipped_history_empty ? "true" : "false",
        seed.post_onboarding_idle_stable ? "true" : "false",
        index + 1U == report.seeds.size() ? "" : ",");
  }
  return std::format(
      "{{\n"
      "  \"schema_version\": 1,\n"
      "  \"scenario\": \"{}\",\n"
      "  \"evidence_scope\": \"application_state_framebuffer_and_encoder\",\n"
      "  \"presentation\": \"{}\",\n"
      "  \"render_profile\": \"{}\",\n"
      "  \"guided_new_game_verified\": {},\n"
      "  \"skipped_new_game_verified\": {},\n"
      "  \"free_flight_redock_verified\": {},\n"
      "  \"pause_resume_verified\": {},\n"
      "  \"pilot_recovery_verified\": {},\n"
      "  \"pilot_recovery_checksum\": \"{}\",\n"
      "  \"presentation_framebuffer_checksum\": \"{}\",\n"
      "  \"encoded_bytes\": \"{}\",\n"
      "  \"encoded_frames\": {},\n"
      "  \"seeds\": [\n{}  ],\n"
      "  \"timings\": {{\"contract_three_simulation_ms\": {:.3f}, "
      "\"contract_three_application_render_ms\": {:.3f}, "
      "\"terminal_proxy\": \"external-live-capture\"}}\n"
      "}}\n",
      kOnboardingAcceptanceScenario, report.presentation,
      profile_name(report.render_configuration),
      report.guided_new_game_verified ? "true" : "false",
      report.skipped_new_game_verified ? "true" : "false",
      report.free_flight_redock_verified ? "true" : "false",
      report.pause_resume_verified ? "true" : "false",
      report.pilot_recovery_verified ? "true" : "false",
      report.pilot_recovery_checksum,
      report.presentation_framebuffer_checksum, report.encoded_bytes,
      report.encoded_frames, seeds, report.contract_three_simulation_ms,
      report.contract_three_application_render_ms);
}

}  // namespace apsis_drift
