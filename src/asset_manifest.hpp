#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace apsis_drift::asset_provenance {

inline constexpr std::size_t kMaximumManifestBytes{1U << 20U};
inline constexpr std::size_t kMaximumAssetRecords{1'024U};
inline constexpr std::size_t kMaximumManifestDepth{32U};
inline constexpr std::size_t kMaximumManifestStringBytes{16U << 10U};
inline constexpr std::size_t kMaximumManifestCollectionEntries{256U};

struct Diagnostic {
  std::string path;
  std::string detail;

  friend auto operator==(const Diagnostic&, const Diagnostic&)
      -> bool = default;
};

using Diagnostics = std::vector<Diagnostic>;

[[nodiscard]] auto validate_manifest_json(
    std::string_view json_text, const std::filesystem::path& repository_root)
    -> Diagnostics;

[[nodiscard]] auto validate_manifest_file(
    const std::filesystem::path& manifest_path,
    const std::filesystem::path& repository_root) -> Diagnostics;

} // namespace apsis_drift::asset_provenance
