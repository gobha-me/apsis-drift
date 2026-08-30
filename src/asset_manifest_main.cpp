#include <cstdio>
#include <filesystem>
#include <string_view>

#include "asset_manifest.hpp"

auto main(int argc, char** argv) -> int {
  if (argc != 4 || std::string_view{argv[1]} != "--root") {
    std::fprintf(stderr,
                 "usage: apsis-drift-asset-validator --root REPOSITORY "
                 "MANIFEST\n");
    return 2;
  }
  const auto diagnostics = apsis_drift::asset_provenance::validate_manifest_file(
      std::filesystem::path{argv[3]}, std::filesystem::path{argv[2]});
  for (const auto& diagnostic : diagnostics) {
    std::fprintf(stderr, "%s:%s: %s\n", argv[3], diagnostic.path.c_str(),
                 diagnostic.detail.c_str());
  }
  if (!diagnostics.empty()) return 1;
  std::printf("validated %s\n", argv[3]);
  return 0;
}
