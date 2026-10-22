#pragma once

#include <filesystem>

namespace apsis_drift {
class NativeFreedomFlightSession;
namespace test_fixture {
// Separate supported-contact placement from a genuine foreign arrival. The
// supplied voyage remains unchanged; this does not certify a flown descent.
auto run_foreign_moon_ground(const NativeFreedomFlightSession&,
                             const std::filesystem::path& output) -> unsigned;
} // namespace test_fixture
} // namespace apsis_drift
