#pragma once

#include <cstdint>
#include <filesystem>

namespace apsis_drift::test_fixture {
// Real native boarding and paid jumps, with one labelled saved-space boundary.
// Drive0 is ordinary,1 recovery and2 strain. An optional directory retains
// the actual foreign parent, Moon arrival and Home arrival saves.
auto run_foreign_moon_outing(std::uint32_t drive,
                             const std::filesystem::path& directory = {})
    -> unsigned;
} // namespace apsis_drift::test_fixture
