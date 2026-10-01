#include "apsis_drift/native_startup.hpp"

#include <utility>

namespace apsis_drift {
namespace {

[[nodiscard]] auto select_document(
    SaveDocument document, std::optional<std::filesystem::path> source_save)
    -> std::expected<NativeStartup, std::string> {
  if (const auto valid = validate_save_document(document); !valid)
    return std::unexpected{"Save rejected: " + valid.error().path + ": " +
                           valid.error().detail};

  // Keep docked and origin-system state intact for the future station view.
  // Refuse travel phases whose active world cannot be identified as home;
  // selection alone does not claim those supported states are playable yet.
  if (document.state.intersystem_contract) {
    const auto phase = document.state.intersystem_contract->travel_phase;
    if (phase != IntersystemTravelPhase::docked_at_origin &&
        phase != IntersystemTravelPhase::origin_system_flight &&
        phase != IntersystemTravelPhase::origin_system_return)
      return std::unexpected{"Native presentation cannot open this "
                             "intersystem travel phase yet"};
  }
  if (document.recipe.active_planet_ordinal != kOriginHomePlanetOrdinal ||
      document.recipe.active_planet != document.recipe.home_planet)
    return std::unexpected{"Native presentation cannot open a save on a "
                           "non-home planet yet"};

  const auto system_seed = derive_seed(
      document.recipe.universe_seed, SeedDomain::system, kOriginSystemOrdinal);
  auto home = generate_origin_home_planet(system_seed);
  if (home.id != document.recipe.home_planet)
    return std::unexpected{"Save home planet does not match its C++ recipe"};
  return NativeStartup{NativeStartup::Mode::legacy_career, std::move(document),
                       std::move(home), std::move(source_save)};
}

[[nodiscard]] auto select_document(
    FreedomSaveDocument document,
    std::optional<std::filesystem::path> source_save)
    -> std::expected<NativeStartup, std::string> {
  if (const auto valid = validate_freedom_save_document(document); !valid)
    return std::unexpected{"Freedom save rejected: " + valid.error().path +
                           ": " + valid.error().detail};
  const auto system_seed = derive_seed(
      document.recipe.universe_seed, SeedDomain::system, kOriginSystemOrdinal);
  auto home = generate_origin_home_planet(system_seed);
  if (home.id != document.recipe.home_planet)
    return std::unexpected{"Freedom home planet does not match its C++ recipe"};
  return NativeStartup{NativeStartup::Mode::freedom, std::move(document),
                       std::move(home), std::move(source_save)};
}

} // namespace

namespace {
auto select_document(FreedomFlightSaveDocument document,
                     std::optional<std::filesystem::path> source_save)
    -> std::expected<NativeStartup, std::string> {
  const auto hydrated = hydrate_freedom_flight_document(document);
  if (!hydrated)
    return std::unexpected{
        "Freedom flight save rejected: " + hydrated.error().path + ": " +
        hydrated.error().detail};
  auto home =
      hydrated->system.catalog.planets[kOriginHomePlanetOrdinal].descriptor;
  return NativeStartup{NativeStartup::Mode::freedom, std::move(document),
                       std::move(home), std::move(source_save)};
}
} // namespace

namespace {
auto select_document(FreedomDockingSaveDocument document,
                     std::optional<std::filesystem::path> source_save)
    -> std::expected<NativeStartup, std::string> {
  if (auto valid = validate_freedom_docking_document(document); !valid)
    return std::unexpected{"Freedom docking save rejected: " +
                           valid.error().path + ": " + valid.error().detail};
  auto selected = select_document(document.flight, source_save);
  if (!selected) return std::unexpected{selected.error()};
  selected->document = std::move(document);
  return selected;
}
} // namespace

auto native_new_game(Seed universe_seed)
    -> std::expected<NativeStartup, std::string> {
  return select_document(make_freedom_new_game_document(universe_seed),
                         std::nullopt);
}

auto native_legacy_new_game(Seed universe_seed)
    -> std::expected<NativeStartup, std::string> {
  return select_document(make_new_game_document(universe_seed), std::nullopt);
}

auto native_continue(const std::filesystem::path& save_path)
    -> std::expected<NativeStartup, std::string> {
  if (save_path.empty())
    return std::unexpected{"Continue requires a selected save path"};
  auto loaded = load_native_save_file(save_path);
  if (!loaded) return std::unexpected{save_file_error_message(loaded.error())};
  return std::visit(
      [&](auto& document) {
        return select_document(std::move(document), save_path);
      },
      *loaded);
}

auto prepare_native_freedom_station_start(NativeStartup selected)
    -> std::expected<NativeFreedomStationStart, std::string> {
  if (std::holds_alternative<FreedomFlightSaveDocument>(selected.document))
    return std::unexpected{"The docked native shell cannot present a Freedom "
                           "flight save yet"};
  if (selected.mode != NativeStartup::Mode::freedom ||
      !std::holds_alternative<FreedomSaveDocument>(selected.document))
    return std::unexpected{"Station bootstrap requires a Freedom save"};
  const auto& save = std::get<FreedomSaveDocument>(selected.document);
  if (const auto valid = validate_freedom_save_document(save); !valid)
    return std::unexpected{"Freedom save rejected: " + valid.error().path +
                           ": " + valid.error().detail};
  auto system = generate_physical_origin_system(save.recipe.universe_seed);
  if (!system)
    return std::unexpected{"Physical origin catalog rejected the saved seed"};
  const auto& home =
      system->catalog.planets[kOriginHomePlanetOrdinal].descriptor;
  if (selected.home_planet != home || save.recipe.home_planet != home.id)
    return std::unexpected{"Saved home planet differs from physical origin"};
  auto station = generate_origin_station(save.recipe.universe_seed);
  if (save.state.station != station.id)
    return std::unexpected{"Saved station differs from origin recipe"};
  const EphemerisQueryTime time{save.state.tick, 0.0};
  auto host = resolve_planet_ephemeris(*system, home.id, time);
  if (!host)
    return std::unexpected{"Physical home ephemeris rejected saved clock"};
  auto ephemeris = resolve_origin_station_ephemeris(*system, station, time);
  if (!ephemeris)
    return std::unexpected{"Physical station ephemeris rejected saved clock"};
  return NativeFreedomStationStart{std::move(selected), std::move(*system),
                                   std::move(station), *host, *ephemeris};
}

auto native_save_freedom(const NativeStartup& selected,
                         const std::filesystem::path& destination)
    -> std::expected<void, std::string> {
  const auto& bytes = destination.native();
  if (bytes.empty() || bytes.size() > 4'096 ||
      bytes.find('\0') != std::string::npos || !destination.is_absolute())
    return std::unexpected{"Save As requires a bounded absolute save path"};
  // Revalidate the authoritative recipe/state before any filesystem mutation.
  if (const auto prepared = prepare_native_freedom_station_start(selected);
      !prepared)
    return std::unexpected{prepared.error()};
  const auto& document = std::get<FreedomSaveDocument>(selected.document);
  if (const auto written =
          write_freedom_save_file_atomically(destination, document);
      !written)
    return std::unexpected{save_file_error_message(written.error())};
  return {};
}

} // namespace apsis_drift
