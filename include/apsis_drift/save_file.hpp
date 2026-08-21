#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

#include "apsis_drift/save_schema.hpp"

namespace apsis_drift {

struct NewGameOptions {
  Seed universe_seed;
  IntersystemRuleProfile penalty_mode{IntersystemRuleProfile::assisted};
  NewGameOnboardingChoice onboarding{NewGameOnboardingChoice::guided};

  friend auto operator==(const NewGameOptions&, const NewGameOptions&)
      -> bool = default;
};

enum class SaveFileErrorCode : std::uint8_t {
  invalid_path,
  not_found,
  open_failed,
  read_failed,
  document_too_large,
  invalid_document,
  temporary_file_failed,
  write_failed,
  sync_failed,
  replace_failed,
  directory_sync_failed,
};

struct SaveFileError {
  SaveFileErrorCode code{};
  std::filesystem::path path;
  std::string detail;
  std::optional<SaveSchemaError> schema_error;

  friend auto operator==(const SaveFileError&, const SaveFileError&)
      -> bool = default;
};

[[nodiscard]] auto make_new_game_document(
    Seed universe_seed,
    NewGameOnboardingChoice onboarding = NewGameOnboardingChoice::guided)
    -> SaveDocument;
[[nodiscard]] auto make_new_game_document(const NewGameOptions& options)
    -> SaveDocument;
[[nodiscard]] auto make_legacy_signal_run_document(Seed universe_seed)
    -> SaveDocument;

[[nodiscard]] auto load_save_file(const std::filesystem::path& path)
    -> std::expected<SaveDocument, SaveFileError>;

[[nodiscard]] auto write_save_file_atomically(
    const std::filesystem::path& path, const SaveDocument& document)
    -> std::expected<void, SaveFileError>;

[[nodiscard]] auto save_file_error_message(const SaveFileError& error)
    -> std::string;

}  // namespace apsis_drift
