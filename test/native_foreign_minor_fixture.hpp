#pragma once

#include <cstdint>
#include <filesystem>

namespace apsis_drift::test_fixture {
// Real native boarding and paid jumps, with one labelled saved-space boundary.
// Drive0 is ordinary,1 recovery and2 strain. An optional directory retains
// the actual foreign entry reference, minor arrival and Home arrival saves.
auto run_foreign_minor_outing(std::uint32_t drive,
                              const std::filesystem::path& directory = {})
    -> unsigned;
[[nodiscard]] auto run_foreign_minor_moon_switch() -> unsigned;
} // namespace apsis_drift::test_fixture
