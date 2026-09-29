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

} // namespace apsis_drift
