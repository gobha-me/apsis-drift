#include "apsis_drift/native_startup.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

auto check(bool condition, const char* message) -> void {
  if (!condition) throw std::runtime_error{message};
}

[[nodiscard]] auto contents(const std::filesystem::path& path) -> std::string {
  std::ifstream input{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

[[nodiscard]] auto replace_once(std::string text, std::string_view before,
                                std::string_view after) -> std::string {
  const auto position = text.find(before);
  check(position != std::string::npos, "test fixture field was not found");
  text.replace(position, before.size(), after);
  return text;
}

} // namespace

auto main() -> int {
  using namespace apsis_drift;
  try {
    const Seed seed{0x1234abcd};
    auto created = native_new_game(seed);
    check(created.has_value(), "explicit New Game was rejected");
    check(created->mode == NativeStartup::Mode::freedom,
          "New Game did not select Freedom mode");
    check(std::holds_alternative<FreedomSaveDocument>(created->document),
          "New Game selected a career document");
    const auto freedom = std::get<FreedomSaveDocument>(created->document);
    check(freedom == make_freedom_new_game_document(seed),
          "New Game changed authoritative saved state");
    check(freedom.state.craft !=
              make_freedom_new_game_document(Seed{seed.value + 1}).state.craft,
          "starter craft identity is not seeded independently");
    check(created->home_planet.id == freedom.recipe.home_planet,
          "New Game selected a different planet");
    check(!created->source_save, "New Game invented a source save");

    const auto directory =
        std::filesystem::temp_directory_path() /
        ("apsis-native-startup-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto save = directory / "freedom.json";
    check(write_freedom_save_file_atomically(save, freedom).has_value(),
          "could not write test save");
    const auto original = contents(save);
    auto continued = native_continue(save);
    check(continued.has_value(), "valid Continue was rejected");
    check(continued->document == created->document,
          "Continue changed authoritative saved state");
    check(continued->source_save == save, "Continue lost selected path");
    check(contents(save) == original, "Continue modified its source save");

    auto progressed = freedom;
    progressed.state.tick = 25;
    progressed.state.discoveries.push_back({SurfaceSignalId{77}, 8});
    progressed.state.world_deltas.push_back(
        {"signal:77", SaveWorldDeltaKind::discovered, 9});
    check(write_freedom_save_file_atomically(save, progressed).has_value(),
          "could not write progressed Freedom save");
    auto progressed_continue = native_continue(save);
    check(progressed_continue &&
              std::get<FreedomSaveDocument>(progressed_continue->document) ==
                  progressed,
          "Freedom clock or history failed roundtrip");

    const auto encoded = contents(save);
    auto invalid_write = progressed;
    invalid_write.state.tick = std::numeric_limits<SimulationTick>::max();
    check(!write_freedom_save_file_atomically(save, invalid_write) &&
              contents(save) == encoded,
          "invalid Freedom write damaged an existing save");
    const auto rejected_file = directory / "rejected.json";
    const auto reject_unchanged = [&](const std::string& bytes) {
      {
        std::ofstream output{rejected_file, std::ios::binary};
        output << bytes;
      }
      check(!native_continue(rejected_file),
            "invalid Freedom save was accepted");
      check(contents(rejected_file) == bytes,
            "rejected Freedom save was changed");
    };
    reject_unchanged(replace_once(encoded, "\"format_version\": 17",
                                  "\"format_version\": 99"));
    reject_unchanged(
        replace_once(encoded, "\"active_planet_ordinal\": \"0\"",
                     "\"active_planet_ordinal\": \"18446744073709551616\""));
    reject_unchanged(replace_once(encoded, "\"tick\": \"25\"",
                                  "\"tick\": \"18446744073709551615\""));
    reject_unchanged(
        replace_once(encoded,
                     "\"starter_craft_id\": \"" +
                         std::to_string(progressed.state.craft.value) + "\"",
                     "\"starter_craft_id\": \"0\""));
    reject_unchanged(
        replace_once(encoded, "\"mode\": \"freedom\"", "\"mode\": \"career\""));
    reject_unchanged(
        replace_once(encoded, "\"mode\": \"freedom\"",
                     "\"mode\": \"freedom\", \"mode\": \"freedom\""));
    reject_unchanged("{broken json\n");

    auto legacy = native_legacy_new_game(seed);
    check(legacy && legacy->mode == NativeStartup::Mode::legacy_career,
          "named legacy start did not select career mode");
    const auto career = std::get<SaveDocument>(legacy->document);
    check(career == make_new_game_document(seed),
          "legacy career start changed its authoritative state");
    const auto career_path = directory / "career.json";
    check(write_save_file_atomically(career_path, career).has_value(),
          "could not write legacy save");
    auto legacy_continued = native_continue(career_path);
    check(legacy_continued &&
              legacy_continued->mode == NativeStartup::Mode::legacy_career &&
              std::get<SaveDocument>(legacy_continued->document) == career,
          "legacy career Continue changed mode or state");

    auto skip = make_new_game_document(seed, NewGameOnboardingChoice::skip);
    check(skip.state.first_objective == FirstObjectiveStatus::offered &&
              skip.state.intersystem_contract.has_value(),
          "legacy Skip was silently reinterpreted as Freedom");
    const auto skip_path = directory / "skip.json";
    check(write_save_file_atomically(skip_path, skip).has_value(),
          "could not write legacy Skip save");
    const auto continued_skip = native_continue(skip_path);
    check(continued_skip &&
              continued_skip->mode == NativeStartup::Mode::legacy_career &&
              std::get<SaveDocument>(continued_skip->document) == skip,
          "legacy Skip Continue changed its saved meaning");

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
