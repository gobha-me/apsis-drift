#include "apsis_drift/native_startup.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

auto check(bool condition, const char* message) -> void {
  if (!condition) throw std::runtime_error{message};
}

[[nodiscard]] auto contents(const std::filesystem::path& path) -> std::string {
  std::ifstream input{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

} // namespace

auto main() -> int {
  using namespace apsis_drift;
  try {
    const Seed seed{0x1234abcd};
    auto created = native_new_game(seed);
    check(created.has_value(), "explicit New Game was rejected");
    check(created->document == make_new_game_document(seed),
          "New Game changed authoritative saved state");
    check(created->home_planet.id == created->document.recipe.home_planet,
          "New Game selected a different planet");
    check(!created->source_save, "New Game invented a source save");

    const auto directory =
        std::filesystem::temp_directory_path() /
        ("apsis-native-startup-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto save = directory / "career.json";
    check(write_save_file_atomically(save, created->document).has_value(),
          "could not write test save");
    const auto original = contents(save);
    auto continued = native_continue(save);
    check(continued.has_value(), "valid Continue was rejected");
    check(continued->document == created->document,
          "Continue changed authoritative saved state");
    check(continued->source_save == save, "Continue lost selected path");
    check(contents(save) == original, "Continue modified its source save");

    check(!native_continue({}), "empty Continue path was accepted");
    check(!native_continue(directory / "missing.json"),
          "missing Continue save fell back to New Game");
    const auto invalid = directory / "invalid.json";
    {
      std::ofstream output{invalid};
      output << "{broken json\n";
    }
    const auto invalid_original = contents(invalid);
    const auto rejected = native_continue(invalid);
    check(!rejected && !rejected.error().empty(),
          "invalid save was accepted or lacked a diagnostic");
    check(contents(invalid) == invalid_original, "rejected save was modified");
    std::filesystem::remove_all(directory);
    std::cout << "Native startup contract passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
