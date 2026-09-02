#pragma once

#include <expected>
#include <filesystem>

#include "apsis_drift/save_file.hpp"

namespace apsis_drift::detail {

enum class AtomicSaveTestInterruption : std::uint8_t {
  none,
  before_replace,
};

[[nodiscard]] auto write_save_file_atomically_for_test(
    const std::filesystem::path& path, const SaveDocument& document,
    AtomicSaveTestInterruption interruption)
    -> std::expected<void, SaveFileError>;

} // namespace apsis_drift::detail
