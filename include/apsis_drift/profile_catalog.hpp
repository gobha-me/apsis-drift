#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/save_schema.hpp"

namespace apsis_drift {

inline constexpr std::size_t kMaximumLocalProfiles{64};
inline constexpr std::size_t kMaximumProfilePathBytes{4'096};
inline constexpr std::size_t kMaximumProfileDiagnosticBytes{160};

struct ProfileId {
  std::uint64_t value{};

  constexpr auto operator<=>(const ProfileId&) const = default;
};

enum class ProfileLocation : std::uint8_t {
  docked_at_origin,
  origin_system_flight,
  outbound_jump_spooling,
  outbound_jump_committed,
  target_system_flight,
  target_planet_flight,
  return_jump_spooling,
  return_jump_committed,
  origin_system_return,
};

struct ProfileMetadata {
  ProfileId id;
  std::uint64_t save_sequence{};
  std::string application_version;
  std::uint32_t format_version{};
  Seed universe_seed;
  IntersystemRuleProfile penalty_mode{IntersystemRuleProfile::assisted};
  OnboardingState onboarding_state{OnboardingState::guided};
  std::optional<OnboardingChapter> onboarding_chapter;
  ProfileLocation location{ProfileLocation::docked_at_origin};
  SimulationTick tick{};

  friend auto operator==(const ProfileMetadata&, const ProfileMetadata&)
      -> bool = default;
};

enum class ProfileCatalogStatus : std::uint8_t {
  available,
  invalid_header,
  invalid_document,
  unreadable,
};

struct ProfileCatalogEntry {
  std::filesystem::path path;
  std::optional<ProfileMetadata> metadata;
  ProfileCatalogStatus status{ProfileCatalogStatus::invalid_header};
  std::string diagnostic;
  std::string source_bytes;

  [[nodiscard]] auto activatable() const noexcept -> bool {
    return status == ProfileCatalogStatus::available && metadata.has_value();
  }
};

struct ProfileCatalogSnapshot {
  std::filesystem::path directory;
  std::vector<ProfileCatalogEntry> entries;
  std::optional<std::size_t> continue_index;
  bool writable{};
  bool overflow{};
  std::string diagnostic;
};

struct LoadedProfile {
  ProfileMetadata metadata;
  SaveDocument document;
  std::filesystem::path path;
  std::string source_bytes;
};

enum class ProfileCatalogErrorCode : std::uint8_t {
  invalid_path,
  storage_unavailable,
  catalog_overflow,
  catalog_full,
  lock_unavailable,
  duplicate_identity,
  sequence_overflow,
  entropy_failure,
  invalid_profile,
  stale_entry,
  write_failure,
};

struct ProfileCatalogError {
  ProfileCatalogErrorCode code{};
  std::filesystem::path path;
  std::string detail;
};

[[nodiscard]] auto resolve_profile_directory(
    std::optional<std::string> xdg_data_home = std::nullopt,
    std::optional<std::string> home = std::nullopt)
    -> std::expected<std::filesystem::path, ProfileCatalogError>;

[[nodiscard]] auto scan_profile_catalog(
    const std::filesystem::path& directory) -> ProfileCatalogSnapshot;

[[nodiscard]] auto load_catalog_profile(const ProfileCatalogEntry& entry)
    -> std::expected<LoadedProfile, ProfileCatalogError>;

[[nodiscard]] auto create_catalog_profile(
    const std::filesystem::path& directory, const SaveDocument& document)
    -> std::expected<LoadedProfile, ProfileCatalogError>;

[[nodiscard]] auto profile_catalog_error_message(
    const ProfileCatalogError& error) -> std::string;

[[nodiscard]] auto profile_location_name(ProfileLocation location) noexcept
    -> std::string_view;

}  // namespace apsis_drift
