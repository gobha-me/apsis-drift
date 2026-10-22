#include "native_foreign_minor_fixture.hpp"

#include <exception>
#include <iostream>

int main() {
  try {
    unsigned checks{};
    for (std::uint32_t drive = 0; drive < 3; ++drive)
      checks += apsis_drift::test_fixture::run_foreign_minor_outing(drive);
    checks += apsis_drift::test_fixture::run_foreign_minor_moon_switch();
    std::cout << "Native foreign Moon: " << checks << " checks\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
