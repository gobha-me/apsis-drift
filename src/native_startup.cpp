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

namespace {
auto select_document(FreedomJourneySaveDocument document,
                     std::optional<std::filesystem::path> source_save)
    -> std::expected<NativeStartup, std::string> {
  if (auto valid = validate_freedom_journey_document(document); !valid)
    return std::unexpected{"Freedom journey save rejected: " +
                           valid.error().path + ": " + valid.error().detail};
  auto selected = select_document(document.voyage, source_save);
  if (!selected) return std::unexpected{selected.error()};
  selected->document = std::move(document);
  return selected;
}
} // namespace

namespace {
auto select_document(FreedomStartingAssemblySaveDocument document,
                     std::optional<std::filesystem::path> source_save)
    -> std::expected<NativeStartup, std::string> {
  if (auto v = validate_freedom_starting_assembly_document(document); !v)
    return std::unexpected{"Starting assembly rejected: " + v.error().detail};
  auto selected = select_document(document.journey, source_save);
  if (!selected) return std::unexpected{selected.error()};
  selected->document = std::move(document);
  return selected;
}
auto select_document(FreedomBoardingSaveDocument document,
                     std::optional<std::filesystem::path> source_save)
    -> std::expected<NativeStartup, std::string> {
  if (auto v = validate_freedom_boarding_document(document); !v)
    return std::unexpected{"Boarding save rejected: " + v.error().detail};
  auto selected = select_document(document.voyage, source_save);
  if (!selected) return std::unexpected{selected.error()};
  selected->document = std::move(document);
  return selected;
}
} // namespace
namespace {
auto select_document(FreedomSurfaceSaveDocument document,
                     std::optional<std::filesystem::path> source_save)
    -> std::expected<NativeStartup, std::string> {
  if (auto valid = validate_freedom_surface_document(document); !valid)
    return std::unexpected{"Surface save rejected: " + valid.error().detail};
  auto selected = std::visit(
      [&](const auto& base) { return select_document(base, source_save); },
      document.base);
  if (!selected) return std::unexpected{selected.error()};
  selected->document = std::move(document);
  return selected;
}
} // namespace
namespace {
auto select_document(FreedomResourceSaveDocument d,
                     std::optional<std::filesystem::path> source)
    -> std::expected<NativeStartup, std::string> {
  if (auto v = validate_freedom_resource_document(d); !v)
    return std::unexpected{"Resource save rejected: " + v.error().detail};
  auto selected = select_document(d.voyage, source);
  if (!selected) return std::unexpected{selected.error()};
  selected->document = std::move(d);
  return selected;
}
} // namespace
namespace {
auto select_document(FreedomKnowledgeSaveDocument d,
                     std::optional<std::filesystem::path> source)
    -> std::expected<NativeStartup, std::string> {
  if (auto v = validate_freedom_knowledge_document(d); !v)
    return std::unexpected{v.error().detail};
  auto selected = select_document(d.voyage, source);
  if (!selected) return std::unexpected{selected.error()};
  selected->document = std::move(d);
  return selected;
}
} // namespace
auto native_new_game(Seed universe_seed)
    -> std::expected<NativeStartup, std::string> {
  auto document =
      make_freedom_starting_assembly_new_game_document(universe_seed);
  if (!document)
    return std::unexpected{"New Game rejected: " + document.error().detail};
  const auto& flight = document->journey.voyage.flight;
  FreedomResources resources{1, flight.origin.state.craft, flight.flight.craft,
                             flight.flight.tick};
  auto knowledge = make_freedom_starting_knowledge(
      universe_seed, kFreedomObservedKnowledgeVersion);
  if (!knowledge) return std::unexpected{"Starting chart rejected"};
  return select_document(
      FreedomKnowledgeSaveDocument{{{std::move(*document), {}}, resources},
                                   std::move(*knowledge)},
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
  if (selected.mode != NativeStartup::Mode::freedom)
    return std::unexpected{"Station bootstrap requires a Freedom save"};
  if (const auto* k =
          std::get_if<FreedomKnowledgeSaveDocument>(&selected.document)) {
    if (auto v = validate_freedom_knowledge_document(*k); !v)
      return std::unexpected{v.error().detail};
    auto inner = selected;
    inner.document = k->voyage;
    auto prepared = prepare_native_freedom_station_start(std::move(inner));
    if (!prepared) return std::unexpected{prepared.error()};
    prepared->selected.document = std::move(selected.document);
    return prepared;
  }
  if (const auto* r =
          std::get_if<FreedomResourceSaveDocument>(&selected.document)) {
    if (auto v = validate_freedom_resource_document(*r); !v)
      return std::unexpected{v.error().detail};
    auto inner = selected;
    inner.document =
        std::visit([](const auto& base) -> NativeSaveDocument { return base; },
                   r->voyage.base);
    auto prepared = prepare_native_freedom_station_start(std::move(inner));
    if (!prepared) return std::unexpected{prepared.error()};
    prepared->selected.document = std::move(selected.document);
    return prepared;
  }
  const FreedomSaveDocument* origin{};
  std::uint32_t physical_catalog = kPhysicalLocalSystemGeneratorVersion;
  if (const auto* boarding =
          std::get_if<FreedomBoardingSaveDocument>(&selected.document)) {
    if (auto v = validate_freedom_boarding_document(*boarding); !v)
      return std::unexpected{v.error().detail};
    if (boarding->boarding.phase == FreedomBoardingPhase::seated)
      return std::unexpected{"Seated voyage uses the flight presentation"};
    origin = &boarding->voyage.flight.origin;
    physical_catalog = boarding->voyage.flight.model.physical_catalog;
  } else if (const auto* assembly =
                 std::get_if<FreedomStartingAssemblySaveDocument>(
                     &selected.document)) {
    if (auto v = validate_freedom_starting_assembly_document(*assembly); !v)
      return std::unexpected{v.error().detail};
    origin = &assembly->journey.voyage.flight.origin;
    physical_catalog = assembly->journey.voyage.flight.model.physical_catalog;
  } else if (const auto* journey =
                 std::get_if<FreedomJourneySaveDocument>(&selected.document)) {
    if (auto valid = validate_freedom_journey_document(*journey); !valid)
      return std::unexpected{"Journey station bootstrap refused: " +
                             valid.error().detail};
    origin = &journey->voyage.flight.origin;
    physical_catalog = journey->voyage.flight.model.physical_catalog;
  } else
    origin = std::get_if<FreedomSaveDocument>(&selected.document);
  if (!origin)
    return std::unexpected{
        "Station bootstrap requires a supported station selection"};
  const auto& save = *origin;
  if (const auto valid = validate_freedom_save_document(save); !valid)
    return std::unexpected{"Freedom save rejected: " + valid.error().path +
                           ": " + valid.error().detail};
  auto system = generate_physical_origin_system(save.recipe.universe_seed,
                                                physical_catalog);
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
  if (selected.mode != NativeStartup::Mode::freedom)
    return std::unexpected{"Freedom Save As requires Freedom mode"};
  const auto& bytes = destination.native();
  if (bytes.empty() || bytes.size() > 4'096 ||
      bytes.find('\0') != std::string::npos || !destination.is_absolute())
    return std::unexpected{"Save As requires a bounded absolute save path"};
  if (const auto* k =
          std::get_if<FreedomKnowledgeSaveDocument>(&selected.document)) {
    auto canonical = select_document(*k, selected.source_save);
    if (!canonical || canonical->home_planet != selected.home_planet)
      return std::unexpected{
          "Knowledge Save As requires its selected physical home"};
    const auto written =
        write_freedom_knowledge_file_atomically(destination, *k);
    if (!written)
      return std::unexpected{save_file_error_message(written.error())};
    return {};
  }
  if (const auto* r =
          std::get_if<FreedomResourceSaveDocument>(&selected.document)) {
    const auto canonical = select_document(*r, selected.source_save);
    if (!canonical || canonical->home_planet != selected.home_planet)
      return std::unexpected{
          "Resource Save As requires its selected physical home"};
    const auto written =
        write_freedom_resource_file_atomically(destination, *r);
    if (!written)
      return std::unexpected{save_file_error_message(written.error())};
    return {};
  }
  if (const auto* surface =
          std::get_if<FreedomSurfaceSaveDocument>(&selected.document)) {
    const auto written =
        write_freedom_surface_file_atomically(destination, *surface);
    if (!written)
      return std::unexpected{save_file_error_message(written.error())};
    return {};
  }
  if (const auto* boarding =
          std::get_if<FreedomBoardingSaveDocument>(&selected.document)) {
    const auto written =
        write_freedom_boarding_file_atomically(destination, *boarding);
    if (!written)
      return std::unexpected{save_file_error_message(written.error())};
    return {};
  }
  // Revalidate the authoritative recipe/state before any filesystem mutation.
  if (const auto prepared = prepare_native_freedom_station_start(selected);
      !prepared)
    return std::unexpected{prepared.error()};
  const auto written =
      std::holds_alternative<FreedomStartingAssemblySaveDocument>(
          selected.document)
          ? write_freedom_starting_assembly_file_atomically(
                destination, std::get<FreedomStartingAssemblySaveDocument>(
                                 selected.document))
      : std::holds_alternative<FreedomJourneySaveDocument>(selected.document)
          ? write_freedom_journey_file_atomically(
                destination,
                std::get<FreedomJourneySaveDocument>(selected.document))
          : write_freedom_save_file_atomically(
                destination, std::get<FreedomSaveDocument>(selected.document));
  if (!written)
    return std::unexpected{save_file_error_message(written.error())};
  return {};
}

} // namespace apsis_drift
