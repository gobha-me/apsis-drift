#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

#include "apsis_drift/origin_station.hpp"
#include "apsis_drift/save_file.hpp"

namespace apsis_drift {

// An application-owned selection. Presentation may read this state but cannot
// replace its recipe or mutable contents with a newly generated fixture.
struct NativeStartup {
  enum class Mode { freedom, legacy_career } mode;
  NativeSaveDocument document;
  PlanetDescriptor home_planet;
  std::optional<std::filesystem::path> source_save;
};

[[nodiscard]] auto native_new_game(Seed universe_seed)
    -> std::expected<NativeStartup, std::string>;
[[nodiscard]] auto native_legacy_new_game(Seed universe_seed)
    -> std::expected<NativeStartup, std::string>;

[[nodiscard]] auto native_continue(const std::filesystem::path& save_path)
    -> std::expected<NativeStartup, std::string>;

} // namespace apsis_drift
