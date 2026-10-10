#pragma once

#include <expected>
#include <filesystem>

#include "apsis_drift/save_file.hpp"

namespace apsis_drift::detail {

// The catalog supplies a validated header plus the unchanged typed save body.
[[nodiscard]] auto write_encoded_save_atomically(const std::filesystem::path&,
                                                 std::string_view)
    -> std::expected<void, SaveFileError>;

enum class AtomicSaveTestInterruption : std::uint8_t {
  none,
  before_replace,
};

[[nodiscard]] auto write_save_file_atomically_for_test(
    const std::filesystem::path& path, const SaveDocument& document,
    AtomicSaveTestInterruption interruption)
    -> std::expected<void, SaveFileError>;

} // namespace apsis_drift::detail
