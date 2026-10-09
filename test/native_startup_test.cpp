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
    check(
        std::holds_alternative<FreedomKnowledgeSaveDocument>(created->document),
        "New Game selected a career document");
    const auto journey =
        std::get<FreedomStartingAssemblySaveDocument>(
            std::get<FreedomKnowledgeSaveDocument>(created->document)
                .voyage.voyage.base)
            .journey;
    const auto freedom = journey.voyage.flight.origin;
    check(freedom == make_freedom_new_game_document(seed),
          "New Game changed authoritative saved state");
    check(freedom.state.craft !=
              make_freedom_new_game_document(Seed{seed.value + 1}).state.craft,
          "starter craft identity is not seeded independently");
    check(created->home_planet.id == freedom.recipe.home_planet,
          "New Game selected a different planet");
    check(!created->source_save, "New Game invented a source save");
    const auto created_station = prepare_native_freedom_station_start(*created);
    check(created_station.has_value(),
          "New Game could not resolve its physical station");
    check(created_station->selected.document == created->document &&
              created_station->ephemeris.station == freedom.state.station &&
              created_station->ephemeris.host_planet ==
                  freedom.recipe.home_planet &&
              created_station->host.planet == freedom.recipe.home_planet &&
              created_station->ephemeris.cycle_tick == 0,
          "New Game station bootstrap changed saved identity or clock");

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
    check(std::get<FreedomSaveDocument>(continued->document) == freedom,
          "Continue changed authoritative saved state");
    check(continued->source_save == save, "Continue lost selected path");
    check(contents(save) == original, "Continue modified its source save");
    const auto continued_station =
        prepare_native_freedom_station_start(*continued);
    const auto legacy_system = generate_physical_origin_system(seed);
    check(legacy_system.has_value(), "historical physical catalog refused");
    const auto legacy_station = resolve_origin_station_ephemeris(
        *legacy_system, generate_origin_station(seed), {0});
    check(continued_station && legacy_station &&
              continued_station->ephemeris == *legacy_station &&
              continued_station->system == *legacy_system &&
              continued_station->system.catalog ==
                  created_station->system.catalog &&
              created_station->system.ephemeris_version == 2 &&
              continued_station->selected.source_save == save,
          "historical17 Continue changed its original motion recipe or station "
          "identity");

    const auto copy_path = directory / "native-copy.json";
    check(native_save_freedom(*created, copy_path).has_value() &&
              load_native_save_file(copy_path) ==
                  std::expected<NativeSaveDocument, SaveFileError>{
                      created->document} &&
              !created->source_save && contents(save) == original,
          "native New Game Save As changed state or failed to persist");
    const auto saved_new_game = native_continue(copy_path);
    check(saved_new_game.has_value(), "native Save As failed selection reload");
    const auto saved_station =
        prepare_native_freedom_station_start(*saved_new_game);
    check(saved_station &&
              saved_station->ephemeris == created_station->ephemeris,
          "native Save As failed physical station reload");

    auto progressed = freedom;
    progressed.state.tick = 25;
    progressed.state.discoveries.push_back({SurfaceSignalId{77}, 8});
    progressed.state.world_deltas.push_back(
        {"signal:77", SaveWorldDeltaKind::discovered, 9});
    check(write_freedom_save_file_atomically(save, progressed).has_value(),
          "could not write progressed Freedom save");
    const auto progressed_bytes = contents(save);
    auto progressed_continue = native_continue(save);
    check(progressed_continue &&
              std::get<FreedomSaveDocument>(progressed_continue->document) ==
                  progressed,
          "Freedom clock or history failed roundtrip");
    const auto progressed_station =
        prepare_native_freedom_station_start(*progressed_continue);
    check(progressed_station &&
              progressed_station->selected.document ==
                  progressed_continue->document &&
              progressed_station->ephemeris.position !=
                  created_station->ephemeris.position &&
              progressed_station->ephemeris.cycle_tick == 25 &&
              progressed_station->host.cycle_tick !=
                  created_station->host.cycle_tick &&
              contents(save) == progressed_bytes,
          "Continue discarded the saved clock or mutable history");

    const auto encoded = contents(save);
    const auto unicode_path = directory / "native-copy-é.json";
    check(native_save_freedom(*progressed_continue, unicode_path).has_value(),
          "native Save As failed UTF-8 destination");
    const auto unicode_continue = native_continue(unicode_path);
    check(unicode_continue &&
              unicode_continue->document == progressed_continue->document &&
              contents(save) == encoded &&
              progressed_continue->source_save == save,
          "native Save As lost UTF-8 path, history, clock or selected source");
    check(native_save_freedom(*progressed_continue, copy_path).has_value(),
          "explicit native replacement failed");
    const auto copied_continue = native_continue(copy_path);
    check(copied_continue &&
              copied_continue->document == progressed_continue->document,
          "explicit native replacement did not preserve progressed state");
    const auto copied_bytes = contents(copy_path);
    const auto refuse_save = [&](const NativeStartup& selection,
                                 const std::filesystem::path& destination) {
      const auto refused = native_save_freedom(selection, destination);
      check(!refused && !refused.error().empty() &&
                contents(copy_path) == copied_bytes &&
                contents(save) == encoded,
            "native save refusal changed a source or lacked a diagnostic");
    };
    for (const auto& destination :
         {std::filesystem::path{}, std::filesystem::path{"relative.json"},
          directory / "missing-parent" / "save.json", directory,
          std::filesystem::path{"/"},
          std::filesystem::path{"/" + std::string(4'096, 'x')},
          std::filesystem::path{copy_path.string() + std::string(1, '\0') +
                                "suffix"}})
      refuse_save(*progressed_continue, destination);
    auto mismatched_selection = *progressed_continue;
    mismatched_selection.mode = NativeStartup::Mode::legacy_career;
    refuse_save(mismatched_selection, copy_path);
    auto invalid_selection = *progressed_continue;
    std::get<FreedomSaveDocument>(invalid_selection.document).state.tick =
        std::numeric_limits<SimulationTick>::max();
    refuse_save(invalid_selection, copy_path);
    for (const auto& entry : std::filesystem::directory_iterator{directory})
      check(entry.path().filename().string().find(".tmp.") == std::string::npos,
            "failed native Save As left a temporary save behind");
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
    check(!prepare_native_freedom_station_start(*legacy_continued),
          "legacy career save was accepted as a Freedom station start");
    refuse_save(*legacy_continued, copy_path);
    NativeStartup forged_home{created->mode, created->document,
                              generate_planet_descriptor(Seed{99}),
                              created->source_save};
    refuse_save(forged_home, copy_path);
    check(!prepare_native_freedom_station_start(std::move(forged_home)),
          "forged home descriptor was accepted by station bootstrap");

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
