#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <format>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <numbers>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/audio.hpp"
#include "apsis_drift/benchmark.hpp"
#include "apsis_drift/celestial.hpp"
#include "apsis_drift/cockpit.hpp"
#include "apsis_drift/coordinates.hpp"
#include "apsis_drift/flight_deck_acceptance.hpp"
#include "apsis_drift/intersystem_contract.hpp"
#include "apsis_drift/intersystem_contract_acceptance.hpp"
#include "apsis_drift/intersystem_jump.hpp"
#include "apsis_drift/intersystem_jump_acceptance.hpp"
#include "apsis_drift/intersystem_planetfall.hpp"
#include "apsis_drift/intersystem_planetfall_acceptance.hpp"
#include "apsis_drift/intersystem_return_acceptance.hpp"
#include "apsis_drift/landscape.hpp"
#include "apsis_drift/local_system.hpp"
#include "apsis_drift/menu.hpp"
#include "apsis_drift/mission_board.hpp"
#include "apsis_drift/onboarding.hpp"
#include "apsis_drift/onboarding_acceptance.hpp"
#include "apsis_drift/orbital.hpp"
#include "apsis_drift/origin_station.hpp"
#include "apsis_drift/origin_return.hpp"
#include "apsis_drift/origin_system_contract.hpp"
#include "apsis_drift/origin_system_contract_acceptance.hpp"
#include "apsis_drift/planet.hpp"
#include "apsis_drift/planetfall_acceptance.hpp"
#include "apsis_drift/planetary_flight.hpp"
#include "apsis_drift/planetary_presentation.hpp"
#include "apsis_drift/profile_catalog.hpp"
#include "apsis_drift/save_file.hpp"
#include "apsis_drift/save_schema.hpp"
#include "apsis_drift/seed.hpp"
#include "apsis_drift/signal_collection.hpp"
#include "apsis_drift/signal_navigation_acceptance.hpp"
#include "apsis_drift/signal_run.hpp"
#include "apsis_drift/signal_scanner.hpp"
#include "apsis_drift/simulation.hpp"
#include "apsis_drift/surface_signals.hpp"
#include "apsis_drift/system_flight.hpp"
#include "apsis_drift/system_flight_acceptance.hpp"
#include "apsis_drift/system_rendering.hpp"
#include "apsis_drift/terrain_tiles.hpp"
#include "apsis_drift/title.hpp"
#include "apsis_drift/universe_navigation.hpp"
#include "apsis_drift/universe_navigation_acceptance.hpp"
#include "apsis_drift/version.hpp"
#include "apsis_drift/world_delta_journal.hpp"
#include "audio_callback.hpp"
#include "capability_floor.hpp"
#include "flight_input.hpp"
#include "save_file_internal.hpp"
#include "signal_input.hpp"
#include "surface_signal_generation.hpp"

namespace {

using namespace apsis_drift;
using termforge::Pixel;
using termforge::Rect;

int failures{};
int fake_entropy_requests{};

[[nodiscard]] auto fake_seed_entropy() noexcept
    -> std::expected<std::uint64_t, SeedEntropyError> {
  ++fake_entropy_requests;
  return 0x0123456789abcdefULL;
}

auto check(bool condition, const char* message) -> void {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

[[nodiscard]] auto close_enough(float left, float right,
                                float tolerance = 1.0e-5F) -> bool {
  return std::abs(left - right) <= tolerance;
}

[[nodiscard]] auto close_enough(double left, double right,
                                double tolerance = 1.0e-12) -> bool {
  return std::abs(left - right) <= tolerance;
}

[[nodiscard]] auto close_position(PlanetFixedPositionMetres left,
                                  PlanetFixedPositionMetres right,
                                  double tolerance = 1.0e-6) -> bool {
  return close_enough(left.x, right.x, tolerance) &&
         close_enough(left.y, right.y, tolerance) &&
         close_enough(left.z, right.z, tolerance);
}

[[nodiscard]] auto planet_with_radius(const PlanetDescriptor& source,
                                      std::uint32_t radius_km)
    -> PlanetDescriptor {
  return {source.seed,
          source.id,
          source.display_name,
          PlanetRadiusKm{radius_km},
          source.surface_gravity,
          source.atmosphere_class,
          source.atmosphere_pressure,
          source.terrain_character,
          source.water_coverage,
          source.palette};
}

[[nodiscard]] auto planet_with_atmosphere(
    const PlanetDescriptor& source, AtmosphereClass atmosphere_class,
    std::uint16_t pressure_millibars) -> PlanetDescriptor {
  return {source.seed,
          source.id,
          source.display_name,
          source.radius,
          source.surface_gravity,
          atmosphere_class,
          AtmospherePressureMillibars{pressure_millibars},
          source.terrain_character,
          source.water_coverage,
          source.palette};
}

[[nodiscard]] auto planet_with_water(const PlanetDescriptor& source,
                                     std::uint16_t basis_points)
    -> PlanetDescriptor {
  return {source.seed,
          source.id,
          source.display_name,
          source.radius,
          source.surface_gravity,
          source.atmosphere_class,
          source.atmosphere_pressure,
          source.terrain_character,
          WaterCoverageBasisPoints{basis_points},
          source.palette};
}

[[nodiscard]] auto count_pixels(const std::vector<Pixel>& pixels,
                                Pixel target) -> std::size_t {
  return static_cast<std::size_t>(
      std::count(pixels.begin(), pixels.end(), target));
}

[[nodiscard]] auto contained_by(Rect inner, Rect outer) -> bool {
  using i64 = std::int64_t;
  return !inner.empty() && inner.x >= outer.x && inner.y >= outer.y &&
         i64{inner.x} + inner.w <= i64{outer.x} + outer.w &&
         i64{inner.y} + inner.h <= i64{outer.y} + outer.h;
}

[[nodiscard]] auto replace_once(std::string text, std::string_view from,
                                std::string_view to) -> std::string {
  const auto position = text.find(from);
  if (position != std::string::npos) {
    text.replace(position, from.size(), to);
  }
  return text;
}

class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    static std::uint64_t sequence{};
    const auto root = std::filesystem::temp_directory_path();
    do {
      m_path = root / std::format("apsis-drift-test-{}-{}",
                                  static_cast<long long>(::getpid()),
                                  sequence++);
    } while (!std::filesystem::create_directory(m_path));
  }
  TemporaryDirectory(const TemporaryDirectory&) = delete;
  auto operator=(const TemporaryDirectory&) -> TemporaryDirectory& = delete;
  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(m_path, ignored);
  }

  [[nodiscard]] auto path() const -> const std::filesystem::path& {
    return m_path;
  }

 private:
  std::filesystem::path m_path;
};

auto write_test_file(const std::filesystem::path& path,
                     std::string_view contents) -> bool {
  std::ofstream output{path, std::ios::binary};
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  return output.good();
}

[[nodiscard]] auto read_test_file(const std::filesystem::path& path)
    -> std::string {
  std::ifstream input{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

auto generation_failure_matrix() -> void {
  check(!Terrain::generate(0, 1), "zero-sized terrain must be rejected");
  check(!Terrain::generate(16, 1), "terrain below the minimum must be rejected");
  check(!Terrain::generate(300, 1), "non-power-of-two terrain must be rejected");
  check(!Terrain::generate(8192, 1), "oversized terrain must be rejected");
}

auto deterministic_generation() -> void {
  const auto first = Terrain::generate(128, 0x12345678U);
  const auto again = Terrain::generate(128, 0x12345678U);
  const auto other = Terrain::generate(128, 0x87654321U);
  check(first && again && other, "valid terrains must generate");
  if (!first || !again || !other) return;
  check(first->checksum() == again->checksum(),
        "the same seed must generate the same terrain");
  check(first->checksum() != other->checksum(),
        "different seeds should generate different terrain");
  check(first->height_at(-1, -1) == first->height_at(127, 127),
        "terrain lookup must wrap at negative coordinates");
  check(first->height_at(128, 128) == first->height_at(0, 0),
        "terrain lookup must wrap at the positive boundary");
}

auto seed_derivation_contract() -> void {
  constexpr std::array domains{
      SeedDomain::universe, SeedDomain::system,    SeedDomain::planet,
      SeedDomain::terrain,  SeedDomain::weather,   SeedDomain::settlement,
      SeedDomain::encounter,
  };
  constexpr Seed parent{0x0123456789ABCDEFULL};
  constexpr std::array<std::uint64_t, domains.size()> golden{
      4143016152257524795ULL,  5513727441665043320ULL,
      11205738369765721017ULL, 2772304862850006270ULL,
      8464315790950683967ULL,  9835027080358202492ULL,
      15527038008458880189ULL,
  };

  check(kSeedDerivationVersion == 1,
        "seed derivation version 1 must remain stable");
  check(derive_seed(Seed{0}, SeedDomain::universe).value ==
            1291384648262051579ULL,
        "the zero universe identity must retain its golden vector");
  check(derive_seed(Seed{std::numeric_limits<std::uint64_t>::max()},
                    SeedDomain::encounter,
                    std::numeric_limits<std::uint64_t>::max())
            .value == 11366853328773030509ULL,
        "the maximum seed identity must retain its golden vector");
  for (std::size_t index = 0; index < domains.size(); ++index) {
    const auto first = derive_seed(parent, domains[index]);
    const auto again = derive_seed(parent, domains[index]);
    check(first == again,
          "equal seed identities must derive the same child seed");
    check(first.value == golden[index],
          "named seed domains must retain their golden vectors");
  }

  const auto system = derive_seed(Seed{42}, SeedDomain::system, 2);
  const auto planet = derive_seed(system, SeedDomain::planet, 7);
  const auto terrain = derive_seed(planet, SeedDomain::terrain);
  check(system.value == 14659972597280896784ULL &&
            planet.value == 429332262284636838ULL &&
            terrain.value == 15773001243264939156ULL,
        "hierarchical seed derivation must retain its golden path");

  check(derive_seed(parent, SeedDomain::planet, 0) !=
            derive_seed(parent, SeedDomain::planet, 1),
        "seed ordinals must identify distinct siblings");
  check(derive_seed(Seed{0}, SeedDomain::terrain) !=
            derive_seed(Seed{1}, SeedDomain::terrain),
        "seed parents must identify distinct hierarchies");

  const auto weather_before = derive_seed(planet, SeedDomain::weather);
  (void)derive_seed(planet, SeedDomain::encounter, 99);
  const auto weather_after = derive_seed(planet, SeedDomain::weather);
  check(weather_before == weather_after,
        "deriving another stream must not perturb existing streams");

  std::vector<std::uint64_t> smoke;
  smoke.reserve(64 * domains.size() * 128);
  for (std::uint64_t root = 0; root < 64; ++root) {
    for (const auto domain : domains) {
      for (std::uint64_t ordinal = 0; ordinal < 128; ++ordinal) {
        smoke.push_back(derive_seed(Seed{root}, domain, ordinal).value);
      }
    }
  }
  std::ranges::sort(smoke);
  check(std::adjacent_find(smoke.begin(), smoke.end()) == smoke.end(),
        "the bounded seed derivation smoke grid must not collide");
}

auto intersystem_identity_contract() -> void {
  check(kIntersystemContractVersion == 4,
        "intersystem contract version 4 must remain stable");
  check(kFirstTargetSystemOrdinal == 1 && kSystemStarOrdinal == 0 &&
            kFirstMissionOrdinal == 0 && kFirstMissionTargetPlanetOrdinal == 0,
        "the first intersystem identity path must retain its named ordinals");
  check(static_cast<std::uint64_t>(SeedDomain::star) == 8 &&
            static_cast<std::uint64_t>(SeedDomain::orbit) == 9 &&
            static_cast<std::uint64_t>(SeedDomain::mission) == 10 &&
            static_cast<std::uint64_t>(SeedDomain::jump_alignment) == 11 &&
            static_cast<std::uint64_t>(SeedDomain::navigation) == 12,
        "new seed domains must retain their permanent identifiers");
  check(kJumpSpoolTicks == 360 && kJumpTransitTicks == 240,
        "version 1 jump timing must retain its fixed-step durations");

  struct Golden {
    std::uint64_t universe;
    std::uint64_t origin_system;
    std::uint64_t target_system;
    std::uint64_t target_star;
    std::uint64_t target_planet;
    std::uint64_t target_orbit;
    std::uint64_t target_objective;
    std::uint64_t mission;
  };
  constexpr std::array goldens{
      Golden{0, 2662095937669570104ULL, 4894411344637159513ULL,
             5592200058082068984ULL, 14025633564997783731ULL,
             11284210986182746681ULL, 13547108315800561202ULL,
             11120317075475266442ULL},
      Golden{42, 677859337506523986ULL, 2910174744474113395ULL,
             4391435423288202480ULL, 11663323411267002299ULL,
             10083446351388880177ULL, 11040201098989428000ULL,
             15627605413347125595ULL},
      Golden{std::numeric_limits<std::uint64_t>::max(), 4480404333408418992ULL,
             6712719740376008401ULL, 7643486625863325908ULL,
             14915374613842125727ULL, 13335497553964003605ULL,
             8612026277630883453ULL, 5521705905730859352ULL},
  };
  for (const auto& golden : goldens) {
    const auto identities =
        generate_first_intersystem_identities(Seed{golden.universe});
    check(identities ==
              generate_first_intersystem_identities(Seed{golden.universe}),
          "first intersystem identities must reproduce from one universe seed");
    check(identities.origin_system_seed.value == golden.origin_system &&
              identities.origin_system.value == golden.origin_system &&
              identities.target_system_seed.value == golden.target_system &&
              identities.target_system.value == golden.target_system &&
              identities.target_star_seed.value == golden.target_star &&
              identities.target_star.value == golden.target_star &&
              identities.target_planet_seed.value == golden.target_planet &&
              identities.target_planet.value == golden.target_planet &&
              identities.target_orbit_seed.value == golden.target_orbit &&
              identities.target_objective_seed.value ==
                  golden.target_objective &&
              identities.target_objective.value == golden.target_objective &&
              identities.mission_seed.value == golden.mission &&
              identities.mission.value == golden.mission,
          "first intersystem identities must retain their golden vectors");
  }

  check(system_id_string(SystemId{0}) == "system-0000000000000000" &&
            star_id_string(StarId{0xABC}) == "star-0000000000000abc" &&
            mission_id_string(
                MissionId{std::numeric_limits<std::uint64_t>::max()}) ==
                "mission-ffffffffffffffff",
        "intersystem IDs must retain fixed-width canonical encodings");

  constexpr Seed universe{42};
  const auto origin = derive_seed(universe, SeedDomain::system, 0);
  const auto original_planet = derive_seed(origin, SeedDomain::planet, 0);
  const auto terrain = derive_seed(original_planet, SeedDomain::terrain, 0);
  const auto weather = derive_seed(original_planet, SeedDomain::weather, 0);
  const auto settlement = derive_seed(origin, SeedDomain::settlement, 0);
  const auto encounter = derive_seed(original_planet, SeedDomain::encounter, 0);
  (void)generate_first_intersystem_identities(universe);
  check(origin == derive_seed(universe, SeedDomain::system, 0) &&
            original_planet == derive_seed(origin, SeedDomain::planet, 0) &&
            terrain == derive_seed(original_planet, SeedDomain::terrain, 0) &&
            weather == derive_seed(original_planet, SeedDomain::weather, 0) &&
            settlement == derive_seed(origin, SeedDomain::settlement, 0) &&
            encounter == derive_seed(original_planet, SeedDomain::encounter, 0),
        "new intersystem derivations must not perturb existing world streams");
}

auto universe_navigation_contract() -> void {
  check(kUniverseNavigationVersion == 1 &&
            static_cast<std::uint64_t>(SeedDomain::navigation) == 12,
        "universe navigation must retain its version and permanent seed domain");
  check(kMinimumFirstRouteLightSeconds == 172'800U &&
            kMaximumFirstRouteLightSeconds == 345'600U &&
            kLocalSystemBoundaryMetres == 100'000'000'000LL &&
            kDirectCruiseMaximumTimeScale == 65'536U,
        "the bounded first-route scale must retain its documented constants");

  constexpr std::array seeds{
      Seed{0}, Seed{42},
      Seed{std::numeric_limits<std::uint64_t>::max()},
  };
  struct RouteGolden {
    std::uint64_t route_seed;
    UniverseAxisDirection direction;
    std::uint64_t distance_light_seconds;
    std::uint64_t distance_metres;
  };
  constexpr std::array route_goldens{
      RouteGolden{11957133562145355735ULL,
                  UniverseAxisDirection::negative_x, 228'505U,
                  68'504'075'615'290ULL},
      RouteGolden{4490051804352235517ULL,
                  UniverseAxisDirection::negative_z, 321'457U,
                  96'370'384'171'306ULL},
      RouteGolden{12613896438947289695ULL,
                  UniverseAxisDirection::negative_y, 299'680U,
                  89'841'803'813'440ULL},
  };
  for (std::size_t index = 0; index < seeds.size(); ++index) {
    const auto seed = seeds[index];
    const auto identities = generate_first_intersystem_identities(seed);
    const auto origin_before = generate_origin_system(seed);
    const auto target_before =
        generate_local_system(identities.target_system_seed);
    const auto route = generate_first_universe_route(seed);
    check(route == generate_first_universe_route(seed) &&
              validate_first_universe_route(route).has_value() &&
              route.origin == identities.origin_system &&
              route.destination == identities.target_system &&
              route.origin_position == UniversePositionMetres{} &&
              route.distance_light_seconds >=
                  kMinimumFirstRouteLightSeconds &&
              route.distance_light_seconds <=
                  kMaximumFirstRouteLightSeconds &&
              route.distance_metres ==
                  route.distance_light_seconds *
                      static_cast<std::uint64_t>(kMetresPerLightSecond) &&
              route.route_seed.value == route_goldens[index].route_seed &&
              route.direction == route_goldens[index].direction &&
              route.distance_light_seconds ==
                  route_goldens[index].distance_light_seconds &&
              route.distance_metres == route_goldens[index].distance_metres,
          "fixed route seeds must retain their golden coordinates and distances");
    const auto nonzero_axes =
        (route.destination_position.x != 0 ? 1 : 0) +
        (route.destination_position.y != 0 ? 1 : 0) +
        (route.destination_position.z != 0 ? 1 : 0);
    check(nonzero_axes == 1,
          "the bounded first route must use one explicit cardinal axis");
    check(generate_origin_system(seed) == origin_before &&
              generate_local_system(identities.target_system_seed) ==
                  target_before &&
              generate_first_intersystem_identities(seed) == identities,
          "inspecting universe navigation must not perturb existing generated streams");
  }

  const auto route = generate_first_universe_route(Seed{42});
  const auto resolved = resolve_navigation_destination(
      route,
      {.system = route.destination,
       .level = NavigationKnowledgeLevel::resolved},
      route.origin, true, true, true);
  check(resolved && resolved->known && resolved->valid &&
            resolved->authorized && resolved->affordable &&
            resolved->available && resolved->selectable &&
            resolved->disabled_reason == NavigationDisabledReason::none &&
            resolved->position == route.destination_position &&
            resolved->distance_metres == route.distance_metres,
        "a resolved authorized affordable destination must be selectable");

  const auto contact = resolve_navigation_destination(
      route,
      {.system = route.destination,
       .level = NavigationKnowledgeLevel::contact},
      route.origin, true, true, true);
  const auto probable = resolve_navigation_destination(
      route,
      {.system = route.destination,
       .level = NavigationKnowledgeLevel::probable},
      route.origin, true, true, true);
  check(contact && probable && contact->known && probable->known &&
            !contact->valid && !probable->valid && !contact->position &&
            !probable->position &&
            contact->disabled_reason ==
                NavigationDisabledReason::requires_resolved_position &&
            probable->disabled_reason ==
                NavigationDisabledReason::requires_resolved_position,
        "contact and probable knowledge must redact exact route geometry");
  const auto locked = resolve_navigation_destination(
      route,
      {.system = route.destination,
       .level = NavigationKnowledgeLevel::resolved},
      route.origin, false, false, false);
  const auto unaffordable = resolve_navigation_destination(
      route,
      {.system = route.destination,
       .level = NavigationKnowledgeLevel::resolved},
      route.origin, true, false, false);
  const auto in_transit = resolve_navigation_destination(
      route,
      {.system = route.destination,
       .level = NavigationKnowledgeLevel::resolved},
      route.origin, true, true, false);
  const auto current = resolve_navigation_destination(
      route,
      {.system = route.origin, .level = NavigationKnowledgeLevel::visited},
      route.origin, false, false, false);
  check(locked &&
            locked->disabled_reason ==
                NavigationDisabledReason::onboarding_locked &&
            unaffordable &&
            unaffordable->disabled_reason ==
                NavigationDisabledReason::insufficient_endurance &&
            in_transit && in_transit->available && !in_transit->selectable &&
            in_transit->disabled_reason ==
                NavigationDisabledReason::unavailable_during_travel &&
            current && current->disabled_reason ==
                           NavigationDisabledReason::current_system,
        "navigation disabled reasons must follow one stable precedence");

  const OnboardingProgress guided_one{
      .state = OnboardingState::guided,
      .chapter = OnboardingChapter::contract_one,
  };
  const OnboardingProgress guided_three{
      .state = OnboardingState::guided,
      .chapter = OnboardingChapter::contract_three,
  };
  const OnboardingProgress completed{
      .state = OnboardingState::completed, .chapter = std::nullopt};
  const auto first_view = resolve_onboarding_navigation_view(
      route, guided_one, route.origin);
  const auto third_view = resolve_onboarding_navigation_view(
      route, guided_three, route.origin);
  const auto skip_view = resolve_onboarding_navigation_view(
      route, initial_onboarding_progress(NewGameOnboardingChoice::skip),
      route.origin);
  const auto return_view = resolve_onboarding_navigation_view(
      route, completed, route.destination);
  check(first_view && first_view->destinations.size() == 1 &&
            !first_view->destinations.front().selectable && third_view &&
            third_view->destinations.size() == 2 &&
            std::ranges::count_if(
                third_view->destinations,
                [](const auto& row) { return row.selectable; }) == 1 &&
            skip_view && skip_view->destinations == third_view->destinations &&
            return_view && return_view->destinations.size() == 2 &&
            return_view->destinations.front().selectable &&
            return_view->destinations.back().disabled_reason ==
                NavigationDisabledReason::current_system,
        "onboarding must reveal only its bounded navigation baseline");
  check(!resolve_onboarding_navigation_view(
            route, guided_one, route.destination),
        "a pre-jump onboarding chapter must reject an unreachable current system");

  const auto closed_view = resolve_onboarding_navigation_view(
      route, guided_three, route.origin, true, false);
  const auto no_endurance = resolve_onboarding_navigation_view(
      route, guided_three, route.origin, false, true);
  check(closed_view &&
            closed_view->destinations.back().available &&
            !closed_view->destinations.back().selectable && no_endurance &&
            !no_endurance->destinations.back().affordable &&
            no_endurance->destinations.back().disabled_reason ==
                NavigationDisabledReason::insufficient_endurance,
        "selection state and endurance must not change generated knowledge");

  UniverseNavigationSelectionState selection;
  const auto rejected_current = selection;
  check(third_view &&
            !advance_universe_navigation_selection(
                *third_view, selection,
                UniverseNavigationSelectionCommand::select) &&
            selection == rejected_current &&
            advance_universe_navigation_selection(
                *third_view, selection,
                UniverseNavigationSelectionCommand::next) &&
            selection.focused_index == 1U &&
            advance_universe_navigation_selection(
                *third_view, selection,
                UniverseNavigationSelectionCommand::select) &&
            selection.pending_destination == route.destination &&
            advance_universe_navigation_selection(
                *third_view, selection,
                UniverseNavigationSelectionCommand::previous) &&
            selection.focused_index == 0U,
        "universe navigation must focus disabled rows but select only the one available route");
  if (closed_view) {
    UniverseNavigationSelectionState closed_selection{.focused_index = 1U};
    const auto before = closed_selection;
    check(!advance_universe_navigation_selection(
              *closed_view, closed_selection,
              UniverseNavigationSelectionCommand::select) &&
              closed_selection == before,
          "closed travel selection must reject atomically");
  }
  if (return_view) {
    UniverseNavigationSelectionState return_selection;
    check(advance_universe_navigation_selection(
              *return_view, return_selection,
              UniverseNavigationSelectionCommand::select) &&
              return_selection.pending_destination == route.origin,
          "the visited origin must be selectable for the physical return leg");
  }
  UniverseNavigationSelectionState invalid_selection{
      .focused_index = 99U};
  check(third_view &&
            !advance_universe_navigation_selection(
                *third_view, invalid_selection,
                UniverseNavigationSelectionCommand::next),
        "out-of-range navigation focus must reject before mutation");

  const auto direct = make_direct_travel_plan(
      route, route.origin, route.destination, 7,
      static_cast<double>(kDirectCruiseMaximumSpeedMetresPerSecond));
  const auto reverse = make_direct_travel_plan(
      route, route.destination, route.origin, 7,
      static_cast<double>(kDirectCruiseMaximumSpeedMetresPerSecond));
  check(direct && reverse &&
            direct->cruise_distance_metres ==
                route.distance_metres -
                    static_cast<std::uint64_t>(
                        2 * kLocalSystemBoundaryMetres) &&
            direct->cruise_distance_metres == reverse->cruise_distance_metres &&
            direct->departure_position == reverse->arrival_position &&
            direct->arrival_position == reverse->departure_position,
        "direct cruise must preserve one reversible physical boundary handoff");
  if (direct) {
    const auto departure =
        resolve_direct_travel(*direct, direct->departure_tick);
    const auto midpoint_tick =
        direct->departure_tick +
        (direct->arrival_tick - direct->departure_tick) / 2U;
    const auto midpoint = resolve_direct_travel(*direct, midpoint_tick);
    const auto arrival = resolve_direct_travel(*direct, direct->arrival_tick);
    check(departure && !departure->arrived &&
              departure->position == direct->departure_position && midpoint &&
              !midpoint->arrived && arrival && arrival->arrived &&
              arrival->position == direct->arrival_position,
          "analytic direct cruise must resolve departure, progress, and exact arrival");

    auto tick = direct->departure_tick;
    while (tick < direct->arrival_tick) {
      const auto next = advance_direct_travel(
          *direct, tick,
          DirectCruiseTimeScale::sixty_five_thousand_five_hundred_thirty_six);
      check(next && *next > tick,
            "maximum direct-cruise scale must make bounded progress");
      if (!next) break;
      tick = *next;
    }
    check(tick == direct->arrival_tick &&
              resolve_direct_travel(*direct, tick) == arrival,
          "time-scale sampling must clamp to the same authoritative arrival");

    const auto projection = direct_travel_save_projection_json(
        *direct, midpoint_tick,
        DirectCruiseTimeScale::sixty_five_thousand_five_hundred_thirty_six);
    check(projection && projection->size() < 1'024U &&
              projection->find("\"navigation_version\": 1") !=
                  std::string::npos &&
              projection->find(system_id_string(route.destination)) !=
                  std::string::npos,
          "the proposed direct-cruise save projection must remain canonical and bounded");
  }

  auto malformed_route = route;
  malformed_route.distance_light_seconds =
      kMinimumFirstRouteLightSeconds - 1U;
  check(!validate_first_universe_route(malformed_route) &&
            !resolve_navigation_destination(
                malformed_route,
                {.system = route.destination,
                 .level = NavigationKnowledgeLevel::resolved},
                route.origin, true, true, true),
        "malformed generated route data must reject before projection");
  check(!resolve_navigation_destination(
            route,
            {.system = route.destination,
             .level = static_cast<NavigationKnowledgeLevel>(255)},
            route.origin, true, true, true) &&
            !resolve_navigation_destination(
                route,
                {.system = SystemId{1},
                 .level = NavigationKnowledgeLevel::resolved},
                route.origin, true, true, true) &&
            !resolve_onboarding_navigation_view(
                route,
                {.state = OnboardingState::guided, .chapter = std::nullopt},
                route.origin),
        "invalid knowledge, identities, and onboarding combinations must reject");
  check(!make_direct_travel_plan(
            route, route.origin, route.destination, 0,
            std::numeric_limits<double>::quiet_NaN()) &&
            !make_direct_travel_plan(route, route.origin, route.destination, 0,
                                     0.0) &&
            !make_direct_travel_plan(route, route.origin, route.destination, 0,
                                     1.5) &&
            !make_direct_travel_plan(
                route, route.origin, route.destination, 0,
                static_cast<double>(
                    kDirectCruiseMaximumSpeedMetresPerSecond + 1U)) &&
            !make_direct_travel_plan(route, route.origin, route.origin, 0,
                                     1'000'000.0) &&
            !make_direct_travel_plan(
                route, route.origin, route.destination,
                std::numeric_limits<SimulationTick>::max(), 1.0),
        "invalid speeds, routes, and tick overflow must reject before cruise");
  if (direct) {
    check(!resolve_direct_travel(*direct, direct->departure_tick - 1U) &&
              !advance_direct_travel(
                  *direct, direct->departure_tick,
                  static_cast<DirectCruiseTimeScale>(3)),
          "out-of-range cruise ticks and unknown time scales must reject");
    auto corrupt_plan = *direct;
    corrupt_plan.velocity_metres_per_second.x += 1;
    check(!resolve_direct_travel(corrupt_plan, corrupt_plan.departure_tick) &&
              !advance_direct_travel(
                  corrupt_plan, corrupt_plan.departure_tick,
                  DirectCruiseTimeScale::one),
          "corrupt direct-cruise geometry must reject before advancement");
    corrupt_plan = *direct;
    corrupt_plan.departure_position.x =
        std::numeric_limits<std::int64_t>::min();
    check(!resolve_direct_travel(corrupt_plan, corrupt_plan.departure_tick),
          "extreme direct-cruise coordinates must reject without unsafe arithmetic");
  }
}

auto universe_navigation_acceptance_contract() -> void {
  const auto result = run_universe_navigation_acceptance();
  check(result && result->route.universe_seed == Seed{42} &&
            result->visible_rows == 2 && result->selectable_rows == 1 &&
            result->ftl_total_ticks == 600 &&
            result->direct_plan.arrival_tick >
                result->direct_plan.departure_tick &&
            result->maximum_scale_updates > 0 &&
            result->projected_save_bytes < 1'024U &&
            result->direct_arrival_checksum != 0,
        "the universe-navigation acceptance report must prove the bounded route");
  if (!result) return;
  const auto json = universe_navigation_acceptance_json(*result);
  check(json.find("\"schema_version\": 1") != std::string::npos &&
            json.find("\"scenario\": \"v0.4.34-universe-navigation-contract\"") !=
                std::string::npos &&
            json.find("\"resource_cost_units\": \"0\"") !=
                std::string::npos &&
            json.find("separate-live-capture-required-by-contract-three") !=
                std::string::npos,
        "the navigation report must distinguish contract, resource, and transport evidence");
}

auto intersystem_state_contract() -> void {
  auto state = initial_intersystem_contract_state(Seed{42});
  check(
      validate_intersystem_contract_state(state).has_value() &&
          state.universe_tick == 0 &&
          state.mission_phase == IntersystemMissionPhase::offered &&
          state.travel_phase == IntersystemTravelPhase::docked_at_origin &&
          state.rule_profile == IntersystemRuleProfile::assisted &&
          state.current_system == state.identities.origin_system,
      "a first intersystem contract must begin offered at the origin station");

  const auto rejected = [&](IntersystemContractCommand command,
                            IntersystemContractError expected) {
    const auto before = state;
    const auto result =
        advance_intersystem_contract(state, state.universe_tick, command);
    check(!result && result.error() == expected && state == before,
          "a rejected intersystem transition must leave state unchanged");
  };
  const auto accepted = [&](IntersystemContractCommand command) {
    const auto result =
        advance_intersystem_contract(state, state.universe_tick, command);
    check(result.has_value(), "a legal intersystem transition must succeed");
  };
  const auto advance_time = [&](SimulationTick ticks) {
    const auto result = advance_intersystem_time(state, ticks);
    check(result.has_value(), "a valid universe-time advance must succeed");
  };

  rejected(IntersystemContractCommand::launch,
           IntersystemContractError::invalid_transition);
  rejected(IntersystemContractCommand::select_pilot_profile,
           IntersystemContractError::invalid_transition);
  auto advanced_profile = initial_intersystem_contract_state(
      Seed{42}, IntersystemRuleProfile::pilot);
  const auto advanced_before = advanced_profile;
  const auto advanced_change = advance_intersystem_contract(
      advanced_profile, advanced_profile.universe_tick,
      IntersystemContractCommand::select_assisted_profile);
  check(advanced_profile.rule_profile == IntersystemRuleProfile::pilot &&
            intersystem_rule_profile_name(advanced_profile.rule_profile) ==
                "ADVANCED" &&
            !advanced_change && advanced_profile == advanced_before,
        "the New Game penalty mode must remain locked for the career");
  const auto wrong_tick_before = state;
  const auto wrong_tick =
      advance_intersystem_contract(state, state.universe_tick + 1,
                                   IntersystemContractCommand::accept_mission);
  check(
      !wrong_tick &&
          wrong_tick.error() == IntersystemContractError::wrong_command_tick &&
          state == wrong_tick_before,
      "commands must address the exact authoritative universe tick");
  accepted(IntersystemContractCommand::accept_mission);
  check(state.rule_profile == IntersystemRuleProfile::assisted,
        "an accepted contract must retain its career penalty mode");
  rejected(IntersystemContractCommand::accept_mission,
           IntersystemContractError::invalid_transition);
  accepted(IntersystemContractCommand::launch);
  rejected(IntersystemContractCommand::select_pilot_profile,
           IntersystemContractError::invalid_transition);
  accepted(IntersystemContractCommand::begin_outbound_jump);
  const auto target_system =
      generate_local_system(state.identities.target_system_seed);
  const auto origin_system =
      generate_origin_system(state.identities.universe_seed);
  for (SimulationTick tick = 0; tick < kJumpSpoolTicks - 1; ++tick) {
    check(advance_intersystem_jump_tick(state, target_system).has_value(),
          "outbound spooling must advance before its boundary");
  }
  check(state.travel_phase ==
            IntersystemTravelPhase::outbound_jump_spooling &&
            !state.arrival_solution,
        "outbound commitment must not publish before the spool boundary");
  accepted(IntersystemContractCommand::cancel_jump);
  accepted(IntersystemContractCommand::begin_outbound_jump);
  IntersystemJumpAdvance outbound_boundary;
  for (SimulationTick tick = 0; tick < kJumpSpoolTicks; ++tick) {
    const auto advanced = advance_intersystem_jump_tick(state, target_system);
    check(advanced.has_value(), "outbound spooling must reach commitment");
    if (advanced) outbound_boundary = *advanced;
  }
  check(outbound_boundary.committed && state.arrival_solution &&
            state.committed_jump_destination == state.identities.target_system,
        "outbound commitment must atomically bind the target and arrival");
  rejected(IntersystemContractCommand::cancel_jump,
           IntersystemContractError::invalid_transition);
  for (SimulationTick tick = 0; tick < kJumpTransitTicks - 1; ++tick) {
    check(advance_intersystem_jump_tick(state, target_system).has_value(),
          "outbound transit must advance before arrival");
  }
  check(state.travel_phase ==
            IntersystemTravelPhase::outbound_jump_committed,
        "outbound arrival must not publish before the transit boundary");
  const auto outbound_arrival =
      advance_intersystem_jump_tick(state, target_system);
  check(outbound_arrival && outbound_arrival->arrived,
        "outbound arrival must publish at the transit boundary");
  accepted(IntersystemContractCommand::enter_target_planet);
  rejected(IntersystemContractCommand::begin_return_jump,
           IntersystemContractError::invalid_transition);
  accepted(IntersystemContractCommand::complete_objective);
  accepted(IntersystemContractCommand::leave_target_planet);
  accepted(IntersystemContractCommand::begin_return_jump);
  accepted(IntersystemContractCommand::cancel_jump);
  accepted(IntersystemContractCommand::begin_return_jump);
  IntersystemJumpAdvance return_boundary;
  for (SimulationTick tick = 0; tick < kJumpSpoolTicks; ++tick) {
    const auto advanced = advance_intersystem_jump_tick(state, origin_system);
    check(advanced.has_value(), "return spooling must reach commitment");
    if (advanced) return_boundary = *advanced;
  }
  check(return_boundary.committed && state.arrival_solution &&
            state.committed_jump_destination == state.identities.origin_system,
        "return commitment must atomically bind the origin and arrival");
  IntersystemJumpAdvance return_arrival;
  for (SimulationTick tick = 0; tick < kJumpTransitTicks; ++tick) {
    const auto advanced = advance_intersystem_jump_tick(state, origin_system);
    check(advanced.has_value(), "return transit must reach arrival");
    if (advanced) return_arrival = *advanced;
  }
  check(return_arrival.arrived,
        "return arrival must publish at the transit boundary");
  auto approach = initialize_origin_return(state, origin_system);
  check(approach.has_value(),
        "return arrival must initialize the moving-station approach");
  if (!approach)
    return;
  approach->relative_position = {kOriginStationArrivalRadiusMetres, 0.0, 0.0};
  approach->relative_velocity = {};
  approach->forward = {-1.0, 0.0, 0.0};
  check(attempt_origin_docking(state, origin_system, *approach).has_value(),
        "a bounded moving-station rendezvous must permit docking");
  check(state.mission_phase == IntersystemMissionPhase::returned,
        "docking must preserve a distinct returned phase before turn-in");
  accepted(IntersystemContractCommand::turn_in);
  rejected(IntersystemContractCommand::turn_in,
           IntersystemContractError::invalid_transition);
  check(state.mission_phase == IntersystemMissionPhase::turned_in &&
            state.travel_phase == IntersystemTravelPhase::docked_at_origin,
        "the complete contract must finish turned in at the origin station");

  const auto zero_advance_before = state;
  const auto zero_advance = advance_intersystem_time(state, 0);
  check(!zero_advance &&
            zero_advance.error() ==
                IntersystemContractError::invalid_time_advance &&
            state == zero_advance_before,
        "a zero universe-time advance must fail without mutation");

  auto overflow = state;
  overflow.universe_tick = std::numeric_limits<SimulationTick>::max();
  const auto overflow_before = overflow;
  const auto overflow_result = advance_intersystem_time(overflow, 1);
  check(
      !overflow_result &&
          overflow_result.error() == IntersystemContractError::tick_overflow &&
          overflow == overflow_before,
      "universe-time overflow must fail without mutation");

  auto malformed = initial_intersystem_contract_state(Seed{42});
  malformed.identities.target_system.value ^= 1U;
  const auto malformed_before = malformed;
  const auto malformed_result =
      advance_intersystem_contract(malformed, malformed.universe_tick,
                                   IntersystemContractCommand::accept_mission);
  check(
      !malformed_result &&
          malformed_result.error() == IntersystemContractError::invalid_state &&
          malformed == malformed_before,
      "corrupt generated identities must fail without mutation");

  malformed = initial_intersystem_contract_state(Seed{42});
  malformed.travel_phase = static_cast<IntersystemTravelPhase>(255);
  check(!validate_intersystem_contract_state(malformed),
        "unknown travel phases must fail validation");
  malformed = initial_intersystem_contract_state(Seed{42});
  malformed.mission_phase = static_cast<IntersystemMissionPhase>(255);
  check(!validate_intersystem_contract_state(malformed),
        "unknown mission phases must fail validation");
  malformed = initial_intersystem_contract_state(Seed{42});
  malformed.rule_profile = static_cast<IntersystemRuleProfile>(255);
  check(!validate_intersystem_contract_state(malformed),
        "unknown rule profiles must fail validation");
  rejected(static_cast<IntersystemContractCommand>(255),
           IntersystemContractError::invalid_transition);
}

auto intersystem_time_boundary_contract() -> void {
  const auto make_outbound_spool = [] {
    auto state = initial_intersystem_contract_state(Seed{42});
    (void)advance_intersystem_contract(
        state, state.universe_tick,
        IntersystemContractCommand::accept_mission);
    (void)advance_intersystem_contract(
        state, state.universe_tick, IntersystemContractCommand::launch);
    (void)begin_intersystem_jump(state);
    return state;
  };
  const auto reject_advance = [](IntersystemContractState& state,
                                 SimulationTick ticks,
                                 IntersystemContractError error,
                                 const char* message) {
    const auto before = state;
    const auto result = advance_intersystem_time(state, ticks);
    check(!result && result.error() == error && state == before, message);
  };

  auto unrestricted = initial_intersystem_contract_state(Seed{42});
  check(advance_intersystem_time(
            unrestricted, kJumpSpoolTicks + kJumpTransitTicks + 1) &&
            validate_intersystem_contract_state(unrestricted),
        "phases without a pending jump boundary must retain batch time advancement");

  auto invalid = initial_intersystem_contract_state(Seed{42});
  invalid.identities.target_system.value ^= 1U;
  reject_advance(invalid, 1, IntersystemContractError::invalid_state,
                 "raw time advancement must reject invalid input without mutation");

  const auto target = generate_local_system(
      make_outbound_spool().identities.target_system_seed);
  const auto origin = generate_origin_system(Seed{42});

  const auto check_spool_boundary = [&](const IntersystemContractState& spool,
                                        const LocalSystemDescriptor& destination,
                                        const char* prefix) {
    auto before_boundary = spool;
    check(advance_intersystem_time(before_boundary, kJumpSpoolTicks - 1) &&
              validate_intersystem_contract_state(before_boundary),
          prefix);

    auto at_boundary = spool;
    check(advance_intersystem_time(at_boundary, kJumpSpoolTicks) &&
              validate_intersystem_contract_state(at_boundary),
          "raw time advancement must allow an exact spool boundary");

    auto crossed = spool;
    reject_advance(crossed, kJumpSpoolTicks + 1,
                   IntersystemContractError::invalid_time_advance,
                   "raw time advancement must reject crossing a spool boundary");
    reject_advance(before_boundary, 2,
                   IntersystemContractError::invalid_time_advance,
                   "a partial batch must not cross the remaining spool boundary");

    auto large_batch = spool;
    reject_advance(large_batch,
                   kJumpSpoolTicks + kJumpTransitTicks + 1,
                   IntersystemContractError::invalid_time_advance,
                   "a large batch must stop at the next spool boundary");

    auto overflow = spool;
    overflow.universe_tick = std::numeric_limits<SimulationTick>::max();
    overflow.phase_started_tick = overflow.universe_tick;
    check(validate_intersystem_contract_state(overflow).has_value(),
          "the jump overflow fixture must remain a valid spool state");
    reject_advance(overflow, 1, IntersystemContractError::tick_overflow,
                   "jump time overflow must reject without mutation");

    const auto exact_before = at_boundary;
    check(advance_intersystem_jump_tick(at_boundary, destination) ==
                  std::unexpected{IntersystemJumpError::transition_failure} &&
              at_boundary == exact_before,
          "a late jump commitment must reject transactionally");

    auto late = spool;
    late.universe_tick = *late.phase_started_tick + kJumpSpoolTicks + 1;
    check(!validate_intersystem_contract_state(late),
          "a spool state beyond its canonical boundary must be invalid");
  };

  const auto check_transit_boundary = [&reject_advance](
                                          const IntersystemContractState& transit,
                                          const LocalSystemDescriptor& destination) {
    auto before_boundary = transit;
    check(advance_intersystem_time(before_boundary, kJumpTransitTicks - 1) &&
              validate_intersystem_contract_state(before_boundary),
          "raw time advancement must allow the tick before transit arrival");

    auto at_boundary = transit;
    check(advance_intersystem_time(at_boundary, kJumpTransitTicks) &&
              validate_intersystem_contract_state(at_boundary) &&
              at_boundary.arrival_solution &&
              at_boundary.universe_tick ==
                  at_boundary.arrival_solution->arrival_tick,
          "raw time advancement must allow the exact transit boundary");

    auto crossed = transit;
    reject_advance(crossed, kJumpTransitTicks + 1,
                   IntersystemContractError::invalid_time_advance,
                   "raw time advancement must reject crossing a transit boundary");
    reject_advance(before_boundary, 2,
                   IntersystemContractError::invalid_time_advance,
                   "a partial batch must not cross the remaining transit boundary");

    auto large_batch = transit;
    reject_advance(large_batch,
                   kJumpSpoolTicks + kJumpTransitTicks + 1,
                   IntersystemContractError::invalid_time_advance,
                   "a large batch must stop at the next transit boundary");

    const auto exact_before = at_boundary;
    check(advance_intersystem_jump_tick(at_boundary, destination) ==
                  std::unexpected{IntersystemJumpError::transition_failure} &&
              at_boundary == exact_before,
          "a late jump arrival must reject transactionally");

    auto late = transit;
    late.universe_tick = late.arrival_solution->arrival_tick + 1;
    check(!validate_intersystem_contract_state(late),
          "a committed transit beyond arrival must be invalid");
  };

  auto outbound_spool = make_outbound_spool();
  check_spool_boundary(outbound_spool, target,
                       "raw time advancement must allow the tick before outbound commitment");

  auto outbound_transit = outbound_spool;
  for (SimulationTick tick = 0; tick < kJumpSpoolTicks; ++tick) {
    (void)advance_intersystem_jump_tick(outbound_transit, target);
  }
  check(outbound_transit.travel_phase ==
            IntersystemTravelPhase::outbound_jump_committed,
        "the outbound transit fixture must commit canonically");
  check_transit_boundary(outbound_transit, target);

  auto return_spool = outbound_transit;
  for (SimulationTick tick = 0; tick < kJumpTransitTicks; ++tick) {
    (void)advance_intersystem_jump_tick(return_spool, target);
  }
  (void)advance_intersystem_contract(
      return_spool, return_spool.universe_tick,
      IntersystemContractCommand::enter_target_planet);
  (void)advance_intersystem_contract(
      return_spool, return_spool.universe_tick,
      IntersystemContractCommand::complete_objective);
  (void)advance_intersystem_contract(
      return_spool, return_spool.universe_tick,
      IntersystemContractCommand::leave_target_planet);
  (void)begin_intersystem_jump(return_spool);
  check(return_spool.travel_phase ==
            IntersystemTravelPhase::return_jump_spooling,
        "the return spool fixture must begin canonically");
  check_spool_boundary(return_spool, origin,
                       "raw time advancement must allow the tick before return commitment");

  auto return_transit = return_spool;
  for (SimulationTick tick = 0; tick < kJumpSpoolTicks; ++tick) {
    (void)advance_intersystem_jump_tick(return_transit, origin);
  }
  check(return_transit.travel_phase ==
            IntersystemTravelPhase::return_jump_committed,
        "the return transit fixture must commit canonically");
  check_transit_boundary(return_transit, origin);
}

auto intersystem_jump_contract() -> void {
  auto refused = initial_intersystem_contract_state(Seed{42});
  const auto refused_before = refused;
  check(!begin_intersystem_jump(refused) && refused == refused_before,
        "jump must be refused transactionally while docked without an "
        "accepted mission");
  refused.current_system = refused.identities.target_system;
  const auto invalid_system_before = refused;
  check(!begin_intersystem_jump(refused) && refused == invalid_system_before,
        "jump must be refused transactionally from an invalid system state");

  auto state = initial_intersystem_contract_state(Seed{42});
  const auto origin = generate_origin_system(state.identities.universe_seed);
  const auto target =
      generate_local_system(state.identities.target_system_seed);
  const auto persists_exactly = [&target](
                                    const IntersystemContractState& expected) {
    auto document =
        make_new_game_document(Seed{42}, NewGameOnboardingChoice::skip);
    document.state.intersystem_contract = expected;
    if (expected.travel_phase == IntersystemTravelPhase::origin_system_flight ||
        expected.travel_phase ==
            IntersystemTravelPhase::outbound_jump_spooling) {
      auto launch_contract = expected;
      if (expected.travel_phase ==
          IntersystemTravelPhase::outbound_jump_spooling) {
        launch_contract.universe_tick = *expected.phase_started_tick;
        launch_contract.travel_phase =
            IntersystemTravelPhase::origin_system_flight;
        launch_contract.phase_started_tick.reset();
        launch_contract.jump_alignment.reset();
      }
      const auto origin =
          generate_origin_system(expected.identities.universe_seed);
      const auto flight =
          initialize_origin_station_launch(launch_contract, origin);
      if (flight) document.state.origin_station_flight = *flight;
    }
    if (expected.travel_phase == IntersystemTravelPhase::target_system_flight &&
        expected.arrival_solution) {
      const auto flight = initial_system_flight_state(
          target, expected.identities.target_planet,
          *expected.arrival_solution);
      if (flight) document.state.system_flight = *flight;
    }
    const auto encoded = encode_save_document_json(document);
    const auto decoded =
        encoded ? decode_save_document_json(*encoded)
                : std::expected<SaveDocument, SaveSchemaError>{
                      std::unexpected{SaveSchemaError{}}};
    return decoded && decoded->state.intersystem_contract == expected &&
           decoded->state.system_flight == document.state.system_flight;
  };
  check(kIntersystemJumpVersion == 4 &&
            kAssistedTargetArrivalStandoffRadii == 10.0 &&
            kAssistedOriginArrivalStandoffMetres == 40'000.0,
        "Assisted jump version and arrival constants must remain stable");
  check(advance_intersystem_contract(
            state, state.universe_tick,
            IntersystemContractCommand::accept_mission) &&
            advance_intersystem_contract(
                state, state.universe_tick,
                IntersystemContractCommand::launch),
        "an accepted mission must enter origin-system flight");
  const auto ready_before = state;
  check(advance_intersystem_jump_tick(state, target) ==
                std::unexpected{IntersystemJumpError::invalid_phase} &&
            state == ready_before,
        "the transit tick driver must refuse ready state without mutation");
  const auto wrong_selection_before = state;
  check(begin_intersystem_jump(state, state.identities.origin_system) ==
                std::unexpected{
                    IntersystemJumpError::invalid_destination} &&
            state == wrong_selection_before &&
            begin_intersystem_jump(state, state.identities.target_system),
        "outbound spooling must validate the selected destination atomically");

  const auto initial_snapshot = intersystem_jump_snapshot(state);
  check(initial_snapshot && initial_snapshot->phase == "SPOOLING" &&
            initial_snapshot->cancelable && initial_snapshot->progress == 0.0,
        "spooling must expose one semantic cancelable snapshot");
  check(persists_exactly(state),
        "the initial spool boundary must survive save and restore exactly");
  const auto before_wrong_destination = state;
  check(!resolve_intersystem_jump_arrival(state, origin) &&
            advance_intersystem_jump_tick(state, origin) ==
                std::unexpected{IntersystemJumpError::invalid_destination} &&
            state == before_wrong_destination,
        "an outbound jump must reject the wrong destination system");

  for (SimulationTick tick = 0; tick < kJumpSpoolTicks - 1; ++tick) {
    check(advance_intersystem_jump_tick(state, target).has_value(),
          "a valid spool tick must advance");
  }
  check(!state.arrival_solution &&
            state.travel_phase ==
                IntersystemTravelPhase::outbound_jump_spooling,
        "arrival must remain unbound before the exact commitment tick");
  check(persists_exactly(state),
        "the pre-commit spool boundary must survive save and restore exactly");
  check(cancel_intersystem_jump(state) &&
            state.travel_phase == IntersystemTravelPhase::origin_system_flight,
        "spooling must remain cancelable through its final pre-commit tick");
  check(begin_intersystem_jump(state).has_value(),
        "a canceled outbound jump must be restartable");

  IntersystemJumpAdvance boundary;
  for (SimulationTick tick = 0; tick < kJumpSpoolTicks; ++tick) {
    const auto advanced = advance_intersystem_jump_tick(state, target);
    check(advanced.has_value(), "the restarted spool must advance");
    if (advanced) boundary = *advanced;
  }
  check(boundary.committed && !boundary.arrived && state.arrival_solution &&
            state.committed_jump_destination ==
                state.identities.target_system &&
            state.arrival_solution->destination ==
                state.identities.target_system &&
            state.arrival_solution->reference_planet ==
                state.identities.target_planet &&
            state.arrival_solution->arrival_tick ==
                state.universe_tick + kJumpTransitTicks,
        "commitment must atomically bind the target and Assisted arrival");
  check(persists_exactly(state),
        "the commitment boundary must survive save and restore exactly");
  const auto committed_snapshot = intersystem_jump_snapshot(state);
  check(committed_snapshot && committed_snapshot->committed &&
            !committed_snapshot->cancelable &&
            !cancel_intersystem_jump(state),
        "a committed jump must be visible and irreversible");
  std::vector<Pixel> jump_frame(96U * 64U);
  check(committed_snapshot &&
            render_intersystem_jump(*committed_snapshot, 96, 64, jump_frame) &&
            !render_intersystem_jump(*committed_snapshot, 0, 64, jump_frame) &&
            !render_intersystem_jump(*committed_snapshot, 96, 64,
                                     std::span<Pixel>{jump_frame}.first(10)),
        "jump rendering must be deterministic and reject invalid dimensions or buffers");

  auto checkpoint =
      make_new_game_document(Seed{42}, NewGameOnboardingChoice::skip);
  checkpoint.state.intersystem_contract = state;
  const auto encoded_checkpoint = encode_save_document_json(checkpoint);
  const auto decoded_checkpoint =
      encoded_checkpoint ? decode_save_document_json(*encoded_checkpoint)
                         : std::expected<SaveDocument, SaveSchemaError>{
                               std::unexpected{SaveSchemaError{}}};
  check(decoded_checkpoint &&
            decoded_checkpoint->state.intersystem_contract == state,
        "a committed arrival solution must survive a canonical v15 round trip");

  auto missing_arrival = state;
  missing_arrival.arrival_solution.reset();
  auto missing_document =
      make_new_game_document(Seed{42}, NewGameOnboardingChoice::skip);
  missing_document.state.intersystem_contract = missing_arrival;
  const auto missing_encoded = encode_save_document_json(missing_document);
  check(!validate_intersystem_contract_state(missing_arrival) &&
            !missing_encoded &&
            missing_encoded.error().code == SaveSchemaErrorCode::invalid_state &&
            missing_encoded.error().path ==
                "$.state.intersystem_contract.arrival_solution",
        "current committed saves must reject a missing arrival at its exact schema path");
  auto finite_tamper = state;
  finite_tamper.arrival_solution->position.x += 1.0;
  const auto finite_tamper_before = finite_tamper;
  auto tampered_document =
      make_new_game_document(Seed{42}, NewGameOnboardingChoice::skip);
  tampered_document.state.intersystem_contract = finite_tamper;
  const auto tampered_encoded = encode_save_document_json(tampered_document);
  check(validate_intersystem_contract_state(finite_tamper) &&
            !validate_intersystem_arrival_solution(
                finite_tamper, target, *finite_tamper.arrival_solution) &&
            advance_intersystem_jump_tick(finite_tamper, target) ==
                std::unexpected{IntersystemJumpError::invalid_arrival} &&
            finite_tamper == finite_tamper_before && !tampered_encoded &&
            tampered_encoded.error().path ==
                "$.state.intersystem_contract.arrival_solution",
        "a finite altered arrival pose must fail canonical gameplay and save validation transactionally");

  auto altered_velocity = state;
  altered_velocity.arrival_solution->velocity.z += 1.0;
  auto altered_tick = state;
  ++altered_tick.arrival_solution->arrival_tick;
  auto altered_reference = state;
  altered_reference.arrival_solution->reference_planet.reset();
  auto altered_destination = state;
  altered_destination.arrival_solution->destination =
      altered_destination.identities.origin_system;
  check(!validate_intersystem_arrival_solution(
            altered_velocity, target, *altered_velocity.arrival_solution) &&
            !validate_intersystem_contract_state(altered_tick) &&
            !validate_intersystem_contract_state(altered_reference) &&
            !validate_intersystem_contract_state(altered_destination),
        "altered arrival velocity, tick, reference, and destination must be rejected");

  const auto arrival = *state.arrival_solution;
  auto malformed_arrival = state;
  malformed_arrival.arrival_solution->position.x =
      std::numeric_limits<double>::quiet_NaN();
  check(!validate_intersystem_contract_state(malformed_arrival),
        "non-finite committed arrival state must fail before presentation");
  malformed_arrival = state;
  malformed_arrival.arrival_solution->position.x = 1.0e16;
  check(!validate_intersystem_contract_state(malformed_arrival),
        "overflow-prone committed coordinates must fail before presentation");
  const auto body = find_local_system_planet(target, state.identities.target_planet);
  const auto ephemeris = resolve_planet_ephemeris(
      target, state.identities.target_planet,
      {.tick = arrival.arrival_tick, .sub_tick_fraction = 0.0});
  check(body && ephemeris &&
            close_enough(
                std::hypot(arrival.position.x - ephemeris->position.x,
                           arrival.position.y - ephemeris->position.y,
                           arrival.position.z - ephemeris->position.z),
                static_cast<double>((*body)->descriptor.radius.value) *
                    1'000.0 * kAssistedTargetArrivalStandoffRadii,
                1.0e-3) &&
            arrival.velocity == ephemeris->velocity,
        "Assisted arrival must use a ten-radius matched-velocity corridor");

  for (SimulationTick tick = 0; tick < kJumpTransitTicks; ++tick) {
    const auto advanced = advance_intersystem_jump_tick(state, target);
    check(advanced.has_value(), "a committed transit tick must advance");
    if (tick + 1 == kJumpTransitTicks / 2) {
      check(persists_exactly(state),
            "mid-transit state must survive save and restore exactly");
    }
    if (tick + 1 == kJumpTransitTicks) {
      check(advanced && advanced->arrived,
            "arrival must occur on the exact transit boundary");
    }
  }
  check(state.travel_phase == IntersystemTravelPhase::target_system_flight &&
            state.current_system == state.identities.target_system &&
            state.arrival_solution == arrival,
        "target arrival must preserve the committed handoff solution");
  check(persists_exactly(state),
        "the arrival boundary must survive save and restore exactly");

  const auto expected = state;
  const auto replay_at_rate = [&](int frames_per_second) {
    auto replay = initial_intersystem_contract_state(Seed{42});
    (void)advance_intersystem_contract(
        replay, replay.universe_tick,
        IntersystemContractCommand::accept_mission);
    (void)advance_intersystem_contract(
        replay, replay.universe_tick, IntersystemContractCommand::launch);
    (void)begin_intersystem_jump(replay);
    FixedStepClock clock;
    while (replay.travel_phase !=
           IntersystemTravelPhase::target_system_flight) {
      const auto frame = clock.advance(SimulationSeconds{
          1.0 / static_cast<double>(frames_per_second)});
      if (!frame) break;
      for (int step = 0; step < frame->steps &&
                         replay.travel_phase !=
                             IntersystemTravelPhase::target_system_flight;
           ++step) {
        if (!advance_intersystem_jump_tick(replay, target)) return replay;
      }
    }
    return replay;
  };
  const auto at_30 = replay_at_rate(30);
  const auto at_60 = replay_at_rate(60);
  check(at_30 == at_60 &&
            intersystem_arrival_checksum(at_30) ==
                intersystem_arrival_checksum(at_60),
        "render cadence must not affect authoritative jump arrival");

  auto pilot = initial_intersystem_contract_state(
      Seed{42}, IntersystemRuleProfile::pilot);
  check(advance_intersystem_contract(
                pilot, pilot.universe_tick,
                IntersystemContractCommand::accept_mission) &&
            advance_intersystem_contract(
                pilot, pilot.universe_tick,
                IntersystemContractCommand::launch) &&
            begin_intersystem_jump(pilot) && pilot.jump_alignment,
        "Pilot launch must expose deterministic alignment state during spool");
  auto pilot_retry = pilot;
  const auto initial_alignment = *pilot.jump_alignment;
  check(cancel_intersystem_jump(pilot_retry) &&
            begin_intersystem_jump(pilot_retry) &&
            pilot_retry.jump_alignment == initial_alignment,
        "canceling and retrying must not reroll Pilot alignment");
  const auto guidance = resolve_intersystem_jump_guidance(pilot);
  check(guidance && guidance->projected_quality ==
                        IntersystemArrivalQuality::offset &&
            !guidance->correction.empty(),
        "Pilot spool must expose the authoritative error and projected grade");
  const auto pilot_snapshot = intersystem_jump_snapshot(pilot);
  std::vector<Pixel> assisted_spool_frame(96U * 64U);
  std::vector<Pixel> pilot_spool_frame(96U * 64U);
  check(initial_snapshot && pilot_snapshot && pilot_snapshot->alignment &&
            render_intersystem_jump(*initial_snapshot, 96, 64,
                                    assisted_spool_frame) &&
            render_intersystem_jump(*pilot_snapshot, 96, 64,
                                    pilot_spool_frame) &&
            pixel_checksum(assisted_spool_frame) !=
                pixel_checksum(pilot_spool_frame),
        "Pilot spool rendering must add one deterministic alignment reticle");
  if (pilot_snapshot) {
    auto invalid_reticle = *pilot_snapshot;
    invalid_reticle.alignment->heading_error_millidegrees =
        std::numeric_limits<std::int32_t>::min();
    const auto before = pilot_spool_frame;
    check(render_intersystem_jump(invalid_reticle, 96, 64,
                                  pilot_spool_frame) ==
                  std::unexpected{
                      IntersystemJumpError::invalid_framebuffer} &&
              pilot_spool_frame == before,
          "invalid reticle coordinates must reject before touching pixels");
  }
  const auto pilot_before_invalid = pilot;
  const std::array invalid_commands{
      FlightCommand{pilot.universe_tick,
                    FlightCommandKind::press_strafe_left}};
  const std::array wrong_tick_commands{
      FlightCommand{pilot.universe_tick + 1,
                    FlightCommandKind::press_turn_left}};
  check(advance_intersystem_jump_tick(pilot, target, invalid_commands) ==
                std::unexpected{IntersystemJumpError::invalid_command} &&
            pilot == pilot_before_invalid &&
            advance_intersystem_jump_tick(pilot, target,
                                          wrong_tick_commands) ==
                std::unexpected{IntersystemJumpError::wrong_command_tick} &&
            pilot == pilot_before_invalid,
        "Pilot alignment must reject unrelated or mistimed controls atomically");
  const auto heading_before =
      std::abs(pilot.jump_alignment->heading_error_millidegrees);
  const auto velocity_before =
      std::abs(pilot.jump_alignment->velocity_error_basis_points);
  const std::array correction_commands{
      FlightCommand{
          pilot.universe_tick,
          pilot.jump_alignment->heading_error_millidegrees > 0
              ? FlightCommandKind::press_turn_left
              : FlightCommandKind::press_turn_right},
      FlightCommand{
          pilot.universe_tick,
          pilot.jump_alignment->velocity_error_basis_points > 0
              ? FlightCommandKind::press_backward
              : FlightCommandKind::press_forward},
  };
  check(advance_intersystem_jump_tick(pilot, target, correction_commands) &&
            std::abs(pilot.jump_alignment->heading_error_millidegrees) <
                heading_before &&
            std::abs(pilot.jump_alignment->velocity_error_basis_points) <
                velocity_before &&
            persists_exactly(pilot),
        "existing flight controls must improve and persist Pilot alignment");
  auto malformed_alignment = pilot;
  malformed_alignment.jump_alignment->heading_error_millidegrees = 180'001;
  check(!validate_intersystem_contract_state(malformed_alignment),
        "out-of-range Pilot alignment must fail contract validation");

  const auto pilot_commit = [&target](std::int32_t heading,
                                      std::int32_t velocity) {
    auto candidate = initial_intersystem_contract_state(
        Seed{42}, IntersystemRuleProfile::pilot);
    (void)advance_intersystem_contract(
        candidate, candidate.universe_tick,
        IntersystemContractCommand::accept_mission);
    (void)advance_intersystem_contract(
        candidate, candidate.universe_tick,
        IntersystemContractCommand::launch);
    (void)begin_intersystem_jump(candidate);
    candidate.jump_alignment = IntersystemJumpAlignmentState{
        .heading_error_millidegrees = heading,
        .velocity_error_basis_points = velocity,
        .controls = {}};
    for (SimulationTick tick = 0; tick < kJumpSpoolTicks; ++tick) {
      if (!advance_intersystem_jump_tick(candidate, target)) break;
    }
    return candidate;
  };
  const auto aligned_edge = pilot_commit(
      kAlignedHeadingErrorMillidegrees,
      kAlignedVelocityErrorBasisPoints);
  const auto offset_edge = pilot_commit(
      kAlignedHeadingErrorMillidegrees + 1,
      kAlignedVelocityErrorBasisPoints);
  const auto offset_limit = pilot_commit(
      kOffsetHeadingErrorMillidegrees,
      kOffsetVelocityErrorBasisPoints);
  const auto opposed_edge = pilot_commit(
      kOffsetHeadingErrorMillidegrees + 1, 0);
  check(aligned_edge.arrival_solution &&
            aligned_edge.arrival_solution->assessment->quality ==
                IntersystemArrivalQuality::aligned &&
            offset_edge.arrival_solution &&
            offset_edge.arrival_solution->assessment->quality ==
                IntersystemArrivalQuality::offset &&
            offset_limit.arrival_solution &&
            offset_limit.arrival_solution->assessment->quality ==
                IntersystemArrivalQuality::offset &&
            opposed_edge.arrival_solution &&
            opposed_edge.arrival_solution->assessment->quality ==
                IntersystemArrivalQuality::opposed,
        "Pilot grading must include its exact documented threshold edges");
  const auto pilot_replay_at_rate = [&target](int frames_per_second) {
    auto replay = initial_intersystem_contract_state(
        Seed{42}, IntersystemRuleProfile::pilot);
    (void)advance_intersystem_contract(
        replay, replay.universe_tick,
        IntersystemContractCommand::accept_mission);
    (void)advance_intersystem_contract(
        replay, replay.universe_tick, IntersystemContractCommand::launch);
    (void)begin_intersystem_jump(replay);
    FixedStepClock clock;
    while (replay.travel_phase !=
           IntersystemTravelPhase::target_system_flight) {
      const auto frame = clock.advance(SimulationSeconds{
          1.0 / static_cast<double>(frames_per_second)});
      if (!frame) break;
      for (int step = 0; step < frame->steps &&
                         replay.travel_phase !=
                             IntersystemTravelPhase::target_system_flight;
           ++step) {
        std::vector<FlightCommand> commands;
        if (replay.universe_tick == 0) {
          commands = {
              {replay.universe_tick, FlightCommandKind::press_turn_right},
              {replay.universe_tick, FlightCommandKind::press_forward},
          };
        } else if (replay.universe_tick == 60) {
          commands = {
              {replay.universe_tick, FlightCommandKind::release_turn_right},
              {replay.universe_tick, FlightCommandKind::release_forward},
          };
        }
        if (!advance_intersystem_jump_tick(replay, target, commands)) {
          return replay;
        }
      }
    }
    return replay;
  };
  const auto pilot_at_30 = pilot_replay_at_rate(30);
  const auto pilot_at_60 = pilot_replay_at_rate(60);
  check(pilot_at_30 == pilot_at_60 && pilot_at_30.arrival_solution &&
            pilot_at_30.arrival_solution->assessment->quality ==
                IntersystemArrivalQuality::aligned &&
            intersystem_arrival_checksum(pilot_at_30) ==
                intersystem_arrival_checksum(pilot_at_60),
        "render cadence must not affect a tick-addressed Pilot input trace");
  check(persists_exactly(offset_edge),
        "a committed Pilot grade and placement must survive save and restore");
  auto arrived_offset = offset_edge;
  for (SimulationTick tick = 0; tick < kJumpTransitTicks; ++tick) {
    (void)advance_intersystem_jump_tick(arrived_offset, target);
  }
  check(arrived_offset.travel_phase ==
                IntersystemTravelPhase::target_system_flight &&
            persists_exactly(arrived_offset),
        "an arrived Pilot grade and placement must survive save and restore");
  auto dishonest_grade = aligned_edge;
  dishonest_grade.arrival_solution->assessment->quality =
      IntersystemArrivalQuality::opposed;
  check(!validate_intersystem_contract_state(dishonest_grade),
        "a stored Pilot grade must agree with its fixed-point threshold inputs");
  const auto opposed_ephemeris = resolve_planet_ephemeris(
      target, opposed_edge.identities.target_planet,
      {.tick = opposed_edge.arrival_solution->arrival_tick,
       .sub_tick_fraction = 0.0});
  check(opposed_ephemeris &&
            opposed_edge.arrival_solution->destination ==
                opposed_edge.identities.target_system &&
            opposed_edge.arrival_solution->reference_planet ==
                opposed_edge.identities.target_planet &&
            opposed_edge.arrival_solution->position ==
                SystemPositionMetres{-opposed_ephemeris->position.x,
                                     -opposed_ephemeris->position.y,
                                     -opposed_ephemeris->position.z} &&
            opposed_edge.arrival_solution->velocity ==
                SystemVelocityMetresPerSecond{
                    -opposed_ephemeris->velocity.x,
                    -opposed_ephemeris->velocity.y,
                    -opposed_ephemeris->velocity.z},
        "the worst valid grade must bind a recoverable opposite-phase handoff in the correct system");
  const auto opposed_flight = initial_system_flight_state(
      target, opposed_edge.identities.target_planet,
      *opposed_edge.arrival_solution);
  const auto opposed_guidance =
      opposed_flight
          ? resolve_system_flight_guidance(target, *opposed_flight)
          : std::expected<SystemFlightGuidance, SystemFlightError>{
                std::unexpected{SystemFlightError::invalid_arrival}};
  check(opposed_flight && opposed_guidance &&
            !opposed_guidance->orbit_insertion_ready &&
            opposed_guidance->distance_metres > 8'000'000'000.0,
        "an opposed arrival must initialize the existing target-hold sub-light "
        "recovery route");
  const auto opposed_before_recommit = opposed_edge;
  auto opposed_recommit = opposed_edge;
  check(!begin_intersystem_jump(opposed_recommit) &&
            opposed_recommit == opposed_before_recommit,
        "a committed Pilot jump must reject recommit without mutation");

  auto overflow = initial_intersystem_contract_state(Seed{42});
  (void)advance_intersystem_contract(
      overflow, overflow.universe_tick,
      IntersystemContractCommand::accept_mission);
  (void)advance_intersystem_contract(
      overflow, overflow.universe_tick, IntersystemContractCommand::launch);
  overflow.universe_tick = std::numeric_limits<SimulationTick>::max();
  check(begin_intersystem_jump(overflow).has_value(),
        "the overflow fixture must enter spooling");
  const auto overflow_before = overflow;
  check(advance_intersystem_jump_tick(overflow, target) ==
                std::unexpected{IntersystemJumpError::tick_overflow} &&
            overflow == overflow_before,
        "tick overflow must reject atomically");

  auto return_state = expected;
  check(advance_intersystem_contract(
            return_state, return_state.universe_tick,
            IntersystemContractCommand::enter_target_planet) &&
            advance_intersystem_contract(
                return_state, return_state.universe_tick,
                IntersystemContractCommand::complete_objective) &&
            advance_intersystem_contract(
                return_state, return_state.universe_tick,
                IntersystemContractCommand::leave_target_planet) &&
            begin_intersystem_jump(return_state),
        "an objective-complete mission must begin the bounded return route");
  const auto wrong_return_destination = return_state;
  check(advance_intersystem_jump_tick(return_state, target) ==
                std::unexpected{IntersystemJumpError::invalid_destination} &&
            return_state == wrong_return_destination,
        "return spooling must reject the wrong destination without mutation");
  for (SimulationTick tick = 0; tick < kJumpSpoolTicks; ++tick) {
    check(advance_intersystem_jump_tick(return_state, origin).has_value(),
          "the Assisted return route must advance");
  }
  auto altered_return = return_state;
  altered_return.arrival_solution->position.y += 1.0;
  const auto altered_return_before = altered_return;
  check(advance_intersystem_jump_tick(altered_return, origin) ==
                std::unexpected{IntersystemJumpError::invalid_arrival} &&
            altered_return == altered_return_before,
        "a finite altered return arrival must reject without mutation");
  for (SimulationTick tick = 0; tick < kJumpTransitTicks; ++tick) {
    check(advance_intersystem_jump_tick(return_state, origin).has_value(),
          "the Assisted return transit must advance");
  }
  const auto expected_station = resolve_origin_station_ephemeris(
      origin, generate_origin_station(return_state.identities.universe_seed),
      {.tick = return_state.universe_tick, .sub_tick_fraction = 0.0});
  check(return_state.travel_phase ==
                IntersystemTravelPhase::origin_system_return &&
            return_state.current_system ==
                return_state.identities.origin_system &&
            return_state.arrival_solution && expected_station &&
            !return_state.arrival_solution->reference_planet &&
            return_state.arrival_solution->position ==
                SystemPositionMetres{expected_station->position.x +
                                         kAssistedOriginArrivalStandoffMetres,
                                     expected_station->position.y,
                                     expected_station->position.z},
        "the return jump must arrive on the station's tick-resolved corridor");
  auto return_approach = initialize_origin_return(return_state, origin);
  if (return_approach) {
    return_approach->relative_position = {kOriginStationArrivalRadiusMetres,
                                          0.0, 0.0};
    return_approach->relative_velocity = {};
    return_approach->forward = {-1.0, 0.0, 0.0};
  }
  check(return_approach &&
            attempt_origin_docking(return_state, origin, *return_approach) &&
            return_state.travel_phase ==
                IntersystemTravelPhase::docked_at_origin &&
            !return_state.arrival_solution,
        "docking must retire the completed return handoff solution");
}

auto intersystem_jump_acceptance_contract() -> void {
  check(!run_intersystem_jump_acceptance(0, 64) &&
            !run_intersystem_jump_acceptance(
                std::numeric_limits<int>::max(), 1),
        "jump acceptance must reject invalid dimensions before allocation");
  const auto result = run_intersystem_jump_acceptance(96, 64);
  check(result && result->report.destination ==
                      generate_first_intersystem_identities(Seed{42})
                          .target_system &&
            result->report.committed_tick == kJumpSpoolTicks &&
            result->report.arrival_tick ==
                kJumpSpoolTicks + kJumpTransitTicks &&
            result->report.arrival_checksum ==
                14671588990613181972ULL &&
            result->report.assisted_quality ==
                IntersystemArrivalQuality::aligned &&
            result->report.pilot_initial_heading_error_millidegrees ==
                -16'160 &&
            result->report.pilot_initial_velocity_error_basis_points == -473 &&
            result->report.pilot_aligned_checksum ==
                result->report.arrival_checksum &&
            result->report.pilot_offset_checksum ==
                4112027265386174051ULL &&
            result->report.pilot_opposed_checksum ==
                4541203662738406157ULL &&
            result->report.pilot_offset_distance_metres <
                result->report.pilot_opposed_distance_metres &&
            result->report.framebuffer_checksum != 0 &&
            result->transit_frame.size() == 96U * 64U,
        "canonical headless jump acceptance must commit, resume, render, and arrive");
  if (!result) return;
  const auto json = intersystem_jump_acceptance_json(result->report);
  check(json.find("\"scenario\": \"v0.4.15-pilot-ftl-alignment\"") !=
                std::string::npos &&
            json.find("\"schema_version\": 3") != std::string::npos &&
            json.find("\"evidence_scope\": \"application_framebuffer\"") !=
                std::string::npos,
        "jump acceptance JSON must name its renderer-neutral evidence scope");
}

auto system_flight_contract() -> void {
  check(!run_system_flight_acceptance(0, 64) &&
            !run_system_flight_acceptance(
                std::numeric_limits<int>::max(), 1),
        "system-flight acceptance must reject invalid dimensions before allocation");
  auto contract = initial_intersystem_contract_state(Seed{42});
  const auto system =
      generate_local_system(contract.identities.target_system_seed);
  (void)advance_intersystem_contract(
      contract, contract.universe_tick,
      IntersystemContractCommand::accept_mission);
  (void)advance_intersystem_contract(
      contract, contract.universe_tick, IntersystemContractCommand::launch);
  (void)begin_intersystem_jump(contract);
  for (SimulationTick tick = 0;
       tick < kJumpSpoolTicks + kJumpTransitTicks; ++tick) {
    (void)advance_intersystem_jump_tick(contract, system);
  }
  const auto initial = contract.arrival_solution
                           ? initial_system_flight_state(
                                 system, contract.identities.target_planet,
                                 *contract.arrival_solution)
                           : std::expected<SystemFlightState,
                                           SystemFlightError>{
                                 std::unexpected{
                                     SystemFlightError::invalid_arrival}};
  check(kSystemFlightVersion == 1 && initial &&
            initial->tick == contract.universe_tick &&
            validate_system_flight_state(system, *initial).has_value(),
        "the FTL handoff must initialize one valid system-flight state");
  if (!initial) return;
  const auto initial_guidance = resolve_system_flight_guidance(system, *initial);
  const auto initial_instruments = format_flight_instruments(*initial);
  check(initial_guidance &&
            !initial_guidance->inside_approach_boundary &&
            initial_guidance->relative_speed_metres_per_second < 1.0 &&
            initial_guidance->distance_metres > 0.0 &&
            initial_instruments.altitude == "SYS FLT  " &&
            initial_instruments.clearance == "TIME  1x " &&
            initial_instruments.mode == "MODE AUTO" &&
            initial_instruments.alert_state == CockpitAlert::none,
        "arrival guidance must begin at the matched-velocity ten-radius corridor");

  auto compressed = *initial;
  const FlightCommand faster{compressed.tick,
                             FlightCommandKind::increase_time_scale};
  check(advance_system_flight(system, compressed,
                              std::span<const FlightCommand>{&faster, 1}) &&
            compressed.time_scale == SystemTimeScale::four &&
            compressed.tick == initial->tick + 4,
        "time compression must use four bounded authoritative substeps");

  auto coast = *initial;
  coast.mode = FlightMode::manual;
  const auto coast_velocity = coast.velocity;
  const auto coast_position = coast.position;
  check(advance_system_flight(system, coast, {}) &&
            coast.velocity == coast_velocity &&
            coast.position != coast_position,
        "released manual thrust must preserve inertial momentum");

  const auto body = find_local_system_planet(
      system, contract.identities.target_planet);
  const auto ephemeris = resolve_planet_ephemeris(
      system, contract.identities.target_planet,
      {.tick = initial->tick, .sub_tick_fraction = 0.0});
  check(body && ephemeris,
        "the orbit insertion fixture requires the mission planet ephemeris");
  if (!body || !ephemeris) return;
  const double radius =
      static_cast<double>((*body)->descriptor.radius.value) * 1'000.0;

  auto approach = *initial;
  approach.position = {ephemeris->position.x + radius * 5.0,
                       ephemeris->position.y, ephemeris->position.z};
  approach.velocity = ephemeris->velocity;
  approach.time_scale = SystemTimeScale::sixteen;
  check(advance_system_flight(system, approach, {}) &&
            approach.tick == initial->tick + 1 &&
            approach.time_scale == SystemTimeScale::one,
        "the six-radius approach boundary must force one-times simulation");

  const auto crossing_state = [&](SystemTimeScale scale) {
    auto state = *initial;
    state.position = {ephemeris->position.x + radius * 6.0 + 1'000.0,
                      ephemeris->position.y, ephemeris->position.z};
    state.velocity = {ephemeris->velocity.x -
                          kSystemFlightMaximumRelativeSpeed,
                      ephemeris->velocity.y, ephemeris->velocity.z};
    state.forward = {-1.0, 0.0, 0.0};
    state.mode = FlightMode::manual;
    state.time_scale = scale;
    return state;
  };
  for (const auto scale :
       {SystemTimeScale::four, SystemTimeScale::sixteen}) {
    auto reference = crossing_state(SystemTimeScale::one);
    auto compressed_crossing = crossing_state(scale);
    const auto starting_guidance =
        resolve_system_flight_guidance(system, compressed_crossing);
    check(starting_guidance &&
              !starting_guidance->inside_approach_boundary &&
              advance_system_flight(system, reference, {}) &&
              advance_system_flight(system, compressed_crossing, {}) &&
              compressed_crossing == reference &&
              compressed_crossing.tick == initial->tick + 1 &&
              compressed_crossing.time_scale == SystemTimeScale::one &&
              system_flight_state_checksum(compressed_crossing) ==
                  system_flight_state_checksum(reference),
          "compressed flight must stop at the first substep inside the approach boundary");
  }

  auto exact_boundary = *initial;
  exact_boundary.position = {ephemeris->position.x, ephemeris->position.y,
                             ephemeris->position.z + radius * 6.0};
  exact_boundary.velocity = ephemeris->velocity;
  exact_boundary.time_scale = SystemTimeScale::sixteen;
  const auto boundary_guidance =
      resolve_system_flight_guidance(system, exact_boundary);
  check(boundary_guidance && boundary_guidance->distance_metres == radius * 6.0 &&
            boundary_guidance->inside_approach_boundary &&
            advance_system_flight(system, exact_boundary, {}) &&
            exact_boundary.tick == initial->tick + 1 &&
            exact_boundary.time_scale == SystemTimeScale::one,
        "the inclusive approach boundary must retain one-step behavior");

  auto outside = *initial;
  outside.position = {ephemeris->position.x + radius * 7.0,
                      ephemeris->position.y, ephemeris->position.z};
  outside.velocity = ephemeris->velocity;
  outside.mode = FlightMode::manual;
  outside.time_scale = SystemTimeScale::sixteen;
  const auto outside_advance = advance_system_flight(system, outside, {});
  const auto outside_guidance =
      resolve_system_flight_guidance(system, outside);
  check(outside_advance &&
            outside.tick == initial->tick + 16 &&
            outside.time_scale == SystemTimeScale::sixteen &&
            outside_guidance &&
            !outside_guidance->inside_approach_boundary,
        "compressed flight remaining outside must execute its full selected scale");

  auto opening = *initial;
  opening.position = {ephemeris->position.x + radius * 4.0,
                      ephemeris->position.y, ephemeris->position.z};
  opening.velocity = {ephemeris->velocity.x + 10'000.0,
                      ephemeris->velocity.y, ephemeris->velocity.z};
  const auto opening_guidance =
      resolve_system_flight_guidance(system, opening);
  check(opening_guidance && opening_guidance->cue == SystemFlightCue::opening &&
            !opening_guidance->orbit_insertion_ready,
        "overshoot must remain a recoverable opening state");

  auto ready = *initial;
  ready.position = {ephemeris->position.x + radius * 2.5,
                    ephemeris->position.y, ephemeris->position.z};
  ready.velocity = {ephemeris->velocity.x,
                    ephemeris->velocity.y + 1'000.0,
                    ephemeris->velocity.z};
  ready.forward = {-1.0, 0.0, 0.0};
  const auto ready_guidance = resolve_system_flight_guidance(system, ready);
  const auto orbital = insert_system_flight_orbit(system, ready);
  check(ready_guidance && ready_guidance->orbit_insertion_ready && orbital &&
            orbital->planet == ready.target &&
            orbital->regime == FlightRegime::orbital &&
            orbital->tick == ready.tick &&
            orbital->pose.position.altitude_metres > radius,
        "orbit insertion must preserve tick, planet, arrival side, and orbital altitude");
  const auto departed =
      orbital ? depart_planetary_orbit(system, *orbital)
              : std::expected<SystemFlightState, SystemFlightError>{
                    std::unexpected{SystemFlightError::invalid_state}};
  check(departed && departed->tick == ready.tick &&
            departed->system == system.id && departed->target == ready.target &&
            validate_system_flight_state(system, *departed),
        "orbital departure must reverse the planet-fixed handoff without changing identity or tick");
  if (orbital) {
    auto atmospheric = *orbital;
    atmospheric.regime = FlightRegime::atmospheric;
    check(depart_planetary_orbit(system, atmospheric) ==
              std::unexpected{SystemFlightError::planet_departure_refused},
          "planet departure must reject non-orbital craft state");

    auto relabeled_surface_state = *orbital;
    relabeled_surface_state.pose.position.altitude_metres =
        kMinimumFlightClearanceMetres;
    relabeled_surface_state.clearance_metres =
        kMinimumFlightClearanceMetres;
    const auto unchanged = relabeled_surface_state;
    check(!validate_planetary_flight_state(
              (*body)->descriptor, relabeled_surface_state) &&
              depart_planetary_orbit(system, relabeled_surface_state) ==
                  std::unexpected{SystemFlightError::invalid_state} &&
              relabeled_surface_state == unchanged,
          "a near-surface state relabeled as orbital must not depart into system flight");
  }
  auto below_surface = ready;
  below_surface.position.x = ephemeris->position.x + radius * 0.5;
  check(insert_system_flight_orbit(system, below_surface) ==
            std::unexpected{SystemFlightError::orbit_insertion_refused},
        "orbit insertion must reject a craft position below the target surface");

  auto invalid = *initial;
  invalid.position.x = std::numeric_limits<double>::quiet_NaN();
  const auto invalid_before = invalid;
  check(advance_system_flight(system, invalid, {}) ==
                std::unexpected{SystemFlightError::invalid_state} &&
            std::bit_cast<std::uint64_t>(invalid.position.x) ==
                std::bit_cast<std::uint64_t>(invalid_before.position.x),
        "non-finite system flight must reject without partial mutation");
  auto wrong_body = *initial;
  wrong_body.target = PlanetId{wrong_body.target.value ^ 1U};
  check(validate_system_flight_state(system, wrong_body) ==
                std::unexpected{SystemFlightError::unknown_target} &&
            insert_system_flight_orbit(system, wrong_body) ==
                std::unexpected{SystemFlightError::unknown_target},
        "wrong-body system flight must be rejected before transition");
  auto excessive_step = *initial;
  excessive_step.time_scale = static_cast<SystemTimeScale>(32);
  check(advance_system_flight(system, excessive_step, {}) ==
            std::unexpected{SystemFlightError::invalid_state},
        "unsupported time-compression step counts must be rejected");
  auto excessive_coordinate = *initial;
  excessive_coordinate.position.x = 1.0e17;
  check(validate_system_flight_state(system, excessive_coordinate) ==
            std::unexpected{SystemFlightError::invalid_state},
        "overflow-prone system coordinates must be rejected");
  auto overflow = *initial;
  overflow.tick = std::numeric_limits<SimulationTick>::max();
  check(advance_system_flight(system, overflow, {}) ==
            std::unexpected{SystemFlightError::tick_overflow},
        "system-flight tick overflow must reject before integration");
  auto mistimed = *initial;
  const FlightCommand future{mistimed.tick + 1,
                             FlightCommandKind::press_forward};
  check(advance_system_flight(
            system, mistimed,
            std::span<const FlightCommand>{&future, 1}) ==
            std::unexpected{SystemFlightError::wrong_command_tick},
        "system-flight commands must target the authoritative current tick");
  auto held = *initial;
  held.controls.forward = true;
  check(system_flight_state_checksum(held) !=
            system_flight_state_checksum(*initial),
        "system-flight checksums must include held authoritative controls");

  auto saved_contract = contract;
  saved_contract.universe_tick = ready.tick;
  auto document =
      make_new_game_document(Seed{42}, NewGameOnboardingChoice::skip);
  document.state.intersystem_contract = saved_contract;
  document.state.system_flight = ready;
  const auto encoded = encode_save_document_json(document);
  const auto decoded = encoded ? decode_save_document_json(*encoded)
                               : std::expected<SaveDocument, SaveSchemaError>{
                                     std::unexpected{SaveSchemaError{}}};
  check(encoded && decoded && decoded->state.system_flight == ready &&
            decoded->state.intersystem_contract == saved_contract &&
            system_flight_state_checksum(*decoded->state.system_flight) ==
                system_flight_state_checksum(ready),
        "system flight must survive canonical v15 save/resume exactly");
}

auto intersystem_return_contract() -> void {
  check(kOriginStationFlightVersion == 4 &&
            !run_intersystem_return_acceptance(0, 64) &&
            !run_intersystem_return_acceptance(
                std::numeric_limits<int>::max(), 1),
        "return acceptance must reject invalid dimensions before allocation");
  const auto result = run_intersystem_return_acceptance(96, 64);
  check(result && result->report.station ==
                      generate_first_intersystem_identities(Seed{42})
                          .origin_station &&
            result->report.departure_tick < result->report.return_commit_tick &&
            result->report.return_commit_tick <
                result->report.origin_arrival_tick &&
            result->report.origin_arrival_tick < result->report.docking_tick &&
            result->report.departure_checksum != 0 &&
            result->report.origin_arrival_checksum != 0 &&
            result->report.docked_return_checksum != 0 &&
            result->report.discovery_count == 1U &&
            result->report.world_delta_count == 1U &&
            result->report.framebuffer_checksum != 0 &&
            result->final_frame.size() == 96U * 64U,
        "canonical return acceptance must depart, cancel/resume, jump, rendezvous, dock, and turn in");
  if (result) {
    const auto json = intersystem_return_acceptance_json(result->report);
    check(json.find("\"scenario\": \"v0.4.12-intersystem-return\"") !=
                  std::string::npos &&
              json.find("\"schema_version\": 2") != std::string::npos &&
              json.find("\"evidence_scope\": \"application_framebuffer\"") !=
                  std::string::npos,
          "return acceptance JSON must name its renderer-neutral evidence scope");
  }

  auto launch_contract = initial_intersystem_contract_state(
      Seed{42}, IntersystemRuleProfile::pilot);
  const auto launch_command = [&](IntersystemContractCommand value) {
    return advance_intersystem_contract(launch_contract,
                                        launch_contract.universe_tick, value);
  };
  const auto launch_origin =
      generate_origin_system(launch_contract.identities.universe_seed);
  check(launch_command(IntersystemContractCommand::accept_mission) &&
            launch_command(IntersystemContractCommand::launch),
        "the station-flight fixture must enter origin-system flight");
  auto launched =
      initialize_origin_station_launch(launch_contract, launch_origin);
  const auto launch_waypoint = resolve_origin_station_waypoint(
      launch_contract.identities, launch_origin,
      {.tick = launch_contract.universe_tick, .sub_tick_fraction = 0.0});
  const auto launch_pose =
      launched
          ? resolve_origin_station_flight_pose(launch_contract, launch_origin,
                                               *launched)
          : std::expected<OriginStationFlightPose, OriginStationFlightError>{
                std::unexpected{OriginStationFlightError::invalid_state}};
  const auto launch_guidance =
      launched ? resolve_origin_station_flight_guidance(
                     launch_contract, launch_origin, *launched)
               : std::expected<OriginStationFlightGuidance,
                               OriginStationFlightError>{
                     std::unexpected{OriginStationFlightError::invalid_state}};
  check(launched && launch_waypoint && launch_pose && launch_guidance &&
            launched->tick == launch_contract.universe_tick &&
            launched->mode == FlightMode::manual &&
            launched->relative_position ==
                SystemPositionMetres{kOriginStationLaunchStandoffMetres, 0.0,
                                     0.0} &&
            launched->relative_velocity == SystemVelocityMetresPerSecond{} &&
            launch_pose->position.x == launch_waypoint->position.x +
                                           kOriginStationLaunchStandoffMetres &&
            launch_pose->velocity == launch_waypoint->velocity &&
            launch_guidance->arrived && !launch_guidance->in_front &&
            close_enough(std::abs(launch_guidance->bearing_radians),
                         std::numbers::pi) &&
            close_enough(launch_guidance->elevation_radians, 0.0),
        "launch must create a finite deterministic station-relative pose with "
        "matched velocity and camera-relative station direction");
  if (!launched) return;

  auto moving_contract = launch_contract;
  auto moving = *launched;
  const std::array thrust{
      FlightCommand{moving.tick, FlightCommandKind::press_forward}};
  check(advance_origin_station_flight(moving_contract, launch_origin, moving,
                                      thrust) &&
            advance_intersystem_time(moving_contract, 1U) &&
            moving.tick == moving_contract.universe_tick &&
            moving.relative_position.x > kOriginStationLaunchStandoffMetres &&
            moving.relative_velocity.x > 0.0,
        "origin flight must apply authoritative thrust before any jump begins");
  const auto thrust_velocity = moving.relative_velocity;
  const auto thrust_position = moving.relative_position;
  const std::array coast{
      FlightCommand{moving.tick, FlightCommandKind::release_forward}};
  check(advance_origin_station_flight(moving_contract, launch_origin, moving,
                                      coast) &&
            advance_intersystem_time(moving_contract, 1U) &&
            moving.relative_velocity == thrust_velocity &&
            moving.relative_position.x > thrust_position.x,
        "origin flight must coast without hidden drag after thrust release");
  const std::array reverse{
      FlightCommand{moving.tick, FlightCommandKind::press_backward}};
  check(advance_origin_station_flight(moving_contract, launch_origin, moving,
                                      reverse) &&
            advance_intersystem_time(moving_contract, 1U) &&
            moving.relative_velocity.x < thrust_velocity.x,
        "origin flight reverse thrust must brake a forward-moving craft");
  const std::array turn{
      FlightCommand{moving.tick, FlightCommandKind::release_backward},
      FlightCommand{moving.tick, FlightCommandKind::press_turn_left}};
  check(advance_origin_station_flight(moving_contract, launch_origin, moving,
                                      turn) &&
            advance_intersystem_time(moving_contract, 1U) &&
            moving.forward.y > 0.0,
        "origin flight attitude controls must rotate the authoritative craft");
  const auto moving_before_wrong_tick = moving;
  const FlightCommand mistimed{moving.tick + 1U,
                               FlightCommandKind::press_turn_left};
  check(advance_origin_station_flight(
            moving_contract, launch_origin, moving,
            std::span<const FlightCommand>{&mistimed, 1}) ==
                std::unexpected{OriginStationFlightError::wrong_command_tick} &&
            moving == moving_before_wrong_tick,
        "origin flight must reject mistimed controls without mutation");

  auto extreme = *launched;
  extreme.relative_position.x =
      std::nextafter(1.0e15, std::numeric_limits<double>::infinity());
  check(validate_origin_station_flight_state(launch_contract, launch_origin,
                                             extreme) ==
            std::unexpected{OriginStationFlightError::invalid_state},
        "origin flight must reject overflow-prone station-relative distances");
  auto overflow_contract = launch_contract;
  overflow_contract.universe_tick =
      std::numeric_limits<SimulationTick>::max();
  auto overflow_flight = *launched;
  overflow_flight.tick = overflow_contract.universe_tick;
  check(advance_origin_station_flight(overflow_contract, launch_origin,
                                      overflow_flight, {}) ==
            std::unexpected{OriginStationFlightError::tick_overflow},
        "origin flight tick overflow must reject before integration");

  auto redocked_contract = launch_contract;
  check(
      attempt_origin_docking(redocked_contract, launch_origin, *launched) &&
          redocked_contract.travel_phase ==
              IntersystemTravelPhase::docked_at_origin &&
          redocked_contract.mission_phase == IntersystemMissionPhase::accepted,
      "pre-jump docking must return to the accepted station state without "
      "mission progress");
  check(advance_intersystem_contract(redocked_contract,
                                     redocked_contract.universe_tick,
                                     IntersystemContractCommand::launch)
            .has_value(),
        "a redocked contract must be launchable again");
  auto relaunched =
      initialize_origin_station_launch(redocked_contract, launch_origin);
  check(relaunched.has_value(),
        "relaunch must regenerate the tick-addressed station-relative pose");
  if (!relaunched) return;
  relaunched->forward = {0.0, 1.0, 0.0};
  relaunched->relative_velocity = {100.0, 0.0, 0.0};
  auto canonical_spool = redocked_contract;
  auto live_spool = redocked_contract;
  check(begin_intersystem_jump(canonical_spool) &&
            begin_intersystem_jump(live_spool, *relaunched) &&
            canonical_spool.jump_alignment && live_spool.jump_alignment &&
            *canonical_spool.jump_alignment != *live_spool.jump_alignment,
        "Pilot spool must bind the live craft yaw and relative speed into "
        "deterministic alignment");

  auto spool_document =
      make_new_game_document(Seed{42}, NewGameOnboardingChoice::skip);
  spool_document.state.intersystem_contract = live_spool;
  spool_document.state.origin_station_flight = *relaunched;
  const auto spool_json = encode_save_document_json(spool_document);
  const auto spool_round_trip =
      spool_json ? decode_save_document_json(*spool_json)
                 : std::expected<SaveDocument, SaveSchemaError>{
                       std::unexpected{SaveSchemaError{}}};
  check(spool_round_trip &&
            spool_round_trip->state.intersystem_contract == live_spool &&
            spool_round_trip->state.origin_station_flight == relaunched,
        "format 16 must preserve the frozen live craft throughout outbound "
        "spool");
  auto invalid_spool = spool_document;
  ++invalid_spool.state.origin_station_flight->tick;
  check(
      !encode_save_document_json(invalid_spool),
      "outbound spool must reject a craft state not frozen at its start tick");
  for (SimulationTick tick = 0; tick < kSimulationHz / 2U; ++tick) {
    (void)advance_intersystem_jump_tick(
        live_spool,
        generate_local_system(live_spool.identities.target_system_seed));
  }
  auto resumed_flight = *relaunched;
  check(cancel_intersystem_jump(live_spool).has_value(),
        "a live outbound spool must remain cancelable");
  resumed_flight.tick = live_spool.universe_tick;
  resumed_flight.controls = {};
  check(validate_origin_station_flight_state(live_spool, launch_origin,
                                             resumed_flight)
            .has_value(),
        "canceling spool must retime the unchanged relative craft state for "
        "free flight");

  auto contract = initial_intersystem_contract_state(Seed{42});
  const auto offered_before = contract;
  check(
      attempt_origin_docking(contract, generate_origin_system(Seed{42}),
                             OriginStationFlightState{}) ==
              std::unexpected{OriginStationFlightError::invalid_arrival} &&
          contract == offered_before,
      "out-of-order docking must reject without mutating the offered contract");

  const auto command = [&](IntersystemContractCommand value) {
    return advance_intersystem_contract(contract, contract.universe_tick,
                                        value);
  };
  const auto target_system =
      generate_local_system(contract.identities.target_system_seed);
  const auto origin_system =
      generate_origin_system(contract.identities.universe_seed);
  check(command(IntersystemContractCommand::accept_mission) &&
            command(IntersystemContractCommand::launch) &&
            begin_intersystem_jump(contract),
        "origin return validation fixture must launch the contract");
  for (SimulationTick tick = 0;
       tick < kJumpSpoolTicks + kJumpTransitTicks; ++tick) {
    (void)advance_intersystem_jump_tick(contract, target_system);
  }
  check(command(IntersystemContractCommand::enter_target_planet) &&
            command(IntersystemContractCommand::complete_objective) &&
            command(IntersystemContractCommand::leave_target_planet) &&
            begin_intersystem_jump(contract),
        "origin return validation fixture must complete and depart the target");
  for (SimulationTick tick = 0;
       tick < kJumpSpoolTicks + kJumpTransitTicks; ++tick) {
    (void)advance_intersystem_jump_tick(contract, origin_system);
  }
  const auto returning = initialize_origin_return(contract, origin_system);
  check(returning && validate_origin_station_flight_state(
                         contract, origin_system, *returning),
        "origin arrival must initialize a valid station-relative craft state");
  if (!returning)
    return;
  auto outside = *returning;
  outside.relative_position = {kOriginStationArrivalRadiusMetres + 1.0, 0.0,
                               0.0};
  outside.relative_velocity = {};
  outside.forward = {-1.0, 0.0, 0.0};
  const auto outside_guidance =
      resolve_origin_station_flight_guidance(contract, origin_system, outside);
  outside.relative_position.x = kOriginStationArrivalRadiusMetres;
  const auto boundary_guidance =
      resolve_origin_station_flight_guidance(contract, origin_system, outside);
  auto centered = outside;
  centered.relative_position = {};
  const auto centered_guidance =
      resolve_origin_station_flight_guidance(contract, origin_system, centered);
  auto speed_boundary = outside;
  speed_boundary.relative_velocity = {kOriginStationDockingSpeedMetresPerSecond,
                                      0.0, 0.0};
  const auto speed_boundary_guidance = resolve_origin_station_flight_guidance(
      contract, origin_system, speed_boundary);
  auto too_fast = speed_boundary;
  too_fast.relative_velocity.x =
      std::nextafter(kOriginStationDockingSpeedMetresPerSecond,
                     std::numeric_limits<double>::infinity());
  const auto too_fast_guidance =
      resolve_origin_station_flight_guidance(contract, origin_system, too_fast);
  auto docking_contract = contract;
  const auto before_fast_docking = docking_contract;
  check(outside_guidance && !outside_guidance->arrived && boundary_guidance &&
            boundary_guidance->arrived && centered_guidance &&
            centered_guidance->arrived && speed_boundary_guidance &&
            speed_boundary_guidance->arrived && too_fast_guidance &&
            !too_fast_guidance->arrived &&
            attempt_origin_docking(docking_contract, origin_system, too_fast) ==
                std::unexpected{OriginStationFlightError::invalid_arrival} &&
            docking_contract == before_fast_docking,
        "station docking must admit zero range and inclusive 5 km and 25 m/s "
        "boundaries while rejecting excess speed transactionally");

  const auto old_station = resolve_origin_station_waypoint(
      contract.identities, origin_system,
      {.tick = contract.universe_tick, .sub_tick_fraction = 0.0});
  auto later_contract = contract;
  const auto later_ticks =
      generate_origin_station(Seed{42}).orbit.period_ticks / 4U;
  check(advance_intersystem_time(later_contract, later_ticks).has_value(),
        "the stale-waypoint fixture must advance return time");
  const auto current_station = resolve_origin_station_waypoint(
      later_contract.identities, origin_system,
      {.tick = later_contract.universe_tick, .sub_tick_fraction = 0.0});
  auto stale = *returning;
  stale.tick = later_contract.universe_tick;
  if (old_station && current_station) {
    stale.relative_position = {
        old_station->position.x - current_station->position.x,
        old_station->position.y - current_station->position.y,
        old_station->position.z - current_station->position.z};
    stale.relative_velocity = {
        old_station->velocity.x - current_station->velocity.x,
        old_station->velocity.y - current_station->velocity.y,
        old_station->velocity.z - current_station->velocity.z};
  }
  const auto stale_guidance = resolve_origin_station_flight_guidance(
      later_contract, origin_system, stale);
  const auto stale_before = later_contract;
  check(old_station && current_station && stale_guidance &&
            !stale_guidance->within_rendezvous &&
            attempt_origin_docking(later_contract, origin_system, stale) ==
                std::unexpected{OriginStationFlightError::invalid_arrival} &&
            later_contract == stale_before,
        "docking must reject a craft parked at the station's obsolete "
        "system-space waypoint");
  auto invalid = *returning;
  invalid.relative_position.x = std::numeric_limits<double>::quiet_NaN();
  const auto invalid_bits =
      std::bit_cast<std::uint64_t>(invalid.relative_position.x);
  check(
      validate_origin_station_flight_state(contract, origin_system, invalid) ==
              std::unexpected{OriginStationFlightError::invalid_state} &&
          advance_origin_station_flight(contract, origin_system, invalid, {}) ==
              std::unexpected{OriginStationFlightError::invalid_state} &&
          std::bit_cast<std::uint64_t>(invalid.relative_position.x) ==
              invalid_bits,
      "non-finite station return state must reject without partial mutation");
  auto wrong_station = *returning;
  wrong_station.station.value ^= 1U;
  check(validate_origin_station_flight_state(contract, origin_system,
                                             wrong_station) ==
            std::unexpected{OriginStationFlightError::invalid_state},
        "station return state must reject the wrong stable station identity");

  auto document =
      make_new_game_document(Seed{42}, NewGameOnboardingChoice::skip);
  document.state.intersystem_contract = contract;
  document.state.origin_station_flight = *returning;
  document.state.world_deltas = {
      {surface_signal_object_key(contract.identities.target_objective),
       SaveWorldDeltaKind::collected, contract.universe_tick}};
  const auto encoded = encode_save_document_json(document);
  const auto restored = encoded ? decode_save_document_json(*encoded)
                                : std::expected<SaveDocument, SaveSchemaError>{
                                      std::unexpected{SaveSchemaError{}}};
  check(restored && restored->state.origin_station_flight == returning,
        "format 16 must preserve the exact origin-station approach state");
}

auto intersystem_contract_acceptance_contract() -> void {
  check(!run_intersystem_contract_acceptance(0, 64) &&
            !run_intersystem_contract_acceptance(
                std::numeric_limits<int>::max(), 1),
        "contract-loop acceptance must reject invalid dimensions before allocation");
  const auto result = run_intersystem_contract_acceptance(96, 64);
  check(result && result->report.checkpoints.size() == 9U &&
            result->report.final_tick == 36'917U &&
            result->report.final_authoritative_checksum ==
                10'997'290'821'769'536'881ULL &&
            result->report.outbound_selected_system ==
                result->report.target_system &&
            result->report.return_selected_system ==
                generate_first_intersystem_identities(Seed{42})
                    .origin_system &&
            result->report.universe_navigation_rows == 2U &&
            result->report.open_exploration_available &&
            result->report.wrong_side_recovery_checksum != 0U &&
            result->report.target_system_planet_count >= 3U &&
            result->report.target_system_initial_framebuffer_checksum !=
                result->report.target_system_moved_framebuffer_checksum &&
            result->report.discovery_count == 1U &&
            result->report.world_delta_count == 1U &&
            result->report.framebuffer_checksum != 0U &&
            result->final_frame.size() == 96U * 64U,
        "contract-loop acceptance must complete and resume the full deterministic mission");
  if (!result) return;
  check(std::ranges::all_of(
            result->report.checkpoints,
            [&](const auto& checkpoint) {
              return checkpoint.resumed_final_checksum ==
                     result->report.final_authoritative_checksum;
            }),
        "every representative save/resume stage must reach the uninterrupted final checksum");
  const auto json = intersystem_contract_acceptance_json(result->report);
  check(json.find("\"scenario\": \"v0.4.35-first-jump-onboarding\"") !=
                std::string::npos &&
            json.find("\"schema_version\": 3") != std::string::npos &&
            json.find("\"evidence_scope\": \"application_framebuffer\"") !=
                std::string::npos &&
            json.find("\"final_mission_phase\": \"turned_in\"") !=
                std::string::npos &&
            json.find("\"final_onboarding_state\": \"completed\"") !=
                std::string::npos &&
            json.find("\"open_exploration_available\": true") !=
                std::string::npos &&
            json.find("\"terminal_proxy\": \"external-live-capture\"") !=
                std::string::npos,
        "contract-loop JSON must separate authoritative, render, and live presentation evidence");
}

auto origin_system_contract_contract() -> void {
  constexpr Seed seed{42};
  const auto binding = generate_origin_system_contract(seed);
  const auto initial = initial_origin_system_contract(seed);
  const auto system = generate_origin_system(seed);
  check(binding && initial && binding->system == system.id &&
            binding->mission_seed ==
                derive_seed(system.seed, SeedDomain::mission,
                            kOriginSystemContractMissionOrdinal) &&
            binding->contract == MissionId{binding->mission_seed.value} &&
            binding->home_planet == system.planets.front().descriptor.id &&
            binding->target_planet == system.planets[1].descriptor.id &&
            binding->target_planet != binding->home_planet &&
            binding->target_objective.value ==
                derive_seed(system.planets[1].descriptor.seed,
                            SeedDomain::encounter, 0)
                    .value,
        "contract two must bind the first non-home planet and its first signal independently");
  if (!initial) return;
  auto state = *initial;
  const auto unchanged = state;
  check(advance_origin_system_contract(
            state, 10, 9, OriginSystemContractCommand::accept) ==
            std::unexpected{OriginSystemContractError::wrong_command_tick} &&
            state == unchanged,
        "contract-two commands must reject the wrong tick without mutation");
  constexpr std::array commands{
      OriginSystemContractCommand::accept,
      OriginSystemContractCommand::launch,
      OriginSystemContractCommand::begin_outbound_transfer,
      OriginSystemContractCommand::enter_target_planet,
      OriginSystemContractCommand::complete_objective,
      OriginSystemContractCommand::leave_target_planet,
      OriginSystemContractCommand::begin_station_rendezvous,
      OriginSystemContractCommand::dock,
      OriginSystemContractCommand::turn_in,
  };
  for (const auto command : commands) {
    check(advance_origin_system_contract(state, 10, 10, command).has_value(),
          "contract two must accept its bounded ordered transition sequence");
  }
  check(state.phase == OriginSystemContractPhase::turned_in &&
            validate_origin_system_contract(seed, state).has_value(),
        "contract two must finish in a valid explicit turn-in state");
  auto corrupt = state;
  corrupt.binding.target_planet.value ^= 1U;
  check(validate_origin_system_contract(seed, corrupt) ==
            std::unexpected{OriginSystemContractError::invalid_binding},
        "contract two must reject corrupt regenerated identities");

  constexpr std::array route_seeds{Seed{0}, Seed{1}, Seed{42},
                                   Seed{0xffffffffffffffffULL}};
  for (const auto route_seed : route_seeds) {
    const auto route_system = generate_origin_system(route_seed);
    auto route_contract = initial_origin_system_contract(route_seed);
    check(route_contract && route_system.planets.size() > 1U,
          "the tested contract-two seed matrix must generate its bounded route");
    if (!route_contract || route_system.planets.size() <= 1U) continue;
    const double maximum_separation_metres =
        static_cast<double>(route_system.planets[0].orbit.radius_kilometres +
                            route_system.planets[1].orbit.radius_kilometres) *
        1'000.0;
    const double maximum_cruise_ticks =
        maximum_separation_metres /
        kSystemFlightAutopilotMaximumRelativeSpeed *
        static_cast<double>(kSimulationHz);
    check(std::isfinite(maximum_cruise_ticks) &&
              maximum_cruise_ticks < 400'000.0 * 16.0,
          "every tested origin-system target must fit the bounded starter return margin");
    check(advance_origin_system_contract(
              *route_contract, 0, 0,
              OriginSystemContractCommand::accept) &&
              advance_origin_system_contract(
                  *route_contract, 0, 0,
                  OriginSystemContractCommand::launch),
          "each tested route must enter station departure deterministically");
    auto route_station =
        initialize_origin_station_launch(route_seed, 0, route_system);
    const std::array departure_commands{
        FlightCommand{0, FlightCommandKind::press_forward}};
    const bool departed =
        route_station &&
        advance_origin_station_flight(route_seed, 0, route_system,
                                      *route_station, departure_commands);
    const auto route_outbound =
        departed ? initialize_origin_system_outbound_transfer(
                       route_seed, 1, route_system, *route_contract,
                       *route_station)
                 : std::expected<SystemFlightState,
                                 OriginSystemContractError>{
                       std::unexpected{
                           OriginSystemContractError::invalid_flight}};
    check(route_outbound && route_outbound->tick == 1 &&
              route_outbound->target == route_contract->binding.target_planet,
          "each tested seed must preserve a valid physical starting ephemeris");
  }

  auto completed = *initial;
  completed.phase = OriginSystemContractPhase::objective_complete;
  SystemFlightState invalid_departure{
      .system = completed.binding.system,
      .target = completed.binding.target_planet,
      .position = {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0},
  };
  check(initialize_origin_system_return_transfer(
            seed, system, completed, invalid_departure) ==
            std::unexpected{OriginSystemContractError::invalid_flight},
        "contract two must reject non-finite transfer state before mutation");
}

auto origin_system_contract_acceptance_contract() -> void {
  check(!run_origin_system_contract_acceptance(0, 64) &&
            !run_origin_system_contract_acceptance(
                std::numeric_limits<int>::max(), 1),
        "contract-two acceptance must reject invalid dimensions before allocation");
  const auto result = run_origin_system_contract_acceptance(96, 64);
  check(result && result->report.checkpoints.size() == 7U &&
            result->report.binding.home_planet !=
                result->report.binding.target_planet &&
            result->report.target_insertion_tick >
                result->report.outbound_tick &&
            result->report.objective_tick >
                result->report.target_insertion_tick &&
            result->report.final_tick > result->report.rendezvous_tick &&
            result->report.outbound_checksum != 0U &&
            result->report.return_checksum != 0U &&
            result->report.final_station_checksum != 0U &&
            result->report.framebuffer_checksum != 0U &&
            result->report.target_insertion_tick == 2'948'802U &&
            result->report.objective_tick == 3'992'789U &&
            result->report.rendezvous_tick == 7'490'650U &&
            result->report.final_tick == 7'535'733U &&
            result->returned_save.state.onboarding.chapter ==
                OnboardingChapter::contract_three &&
            result->returned_save.state.origin_system_contract &&
            result->returned_save.state.origin_system_contract->phase ==
                OriginSystemContractPhase::turned_in,
        "contract-two acceptance must complete the physical deterministic round trip");
}

auto intersystem_planetfall_contract() -> void {
  const auto identities = generate_first_intersystem_identities(Seed{42});
  const auto system = generate_local_system(identities.target_system_seed);
  const auto body =
      find_local_system_planet(system, identities.target_planet);
  auto cache = TerrainTileCache::create();
  check(body && cache,
        "the intersystem Planetfall fixture must resolve its target terrain");
  if (!body || !cache) return;
  const auto catalog = generate_surface_signals((*body)->descriptor, *cache);
  check(catalog && catalog->signals.front().id ==
                       identities.target_objective,
        "the mission objective must be the target planet's stable first signal");
  if (!catalog) return;
  const auto target_fixed = planet_fixed_from_terrain_address(
      (*body)->descriptor, catalog->signals.front().anchor,
      static_cast<double>(catalog->signals.front().approach_altitude_metres));
  const auto target =
      target_fixed
          ? geodetic_from_planet_fixed((*body)->descriptor, *target_fixed)
          : std::expected<GeodeticPosition, CoordinateError>{
                std::unexpected{CoordinateError::non_finite_input}};
  if (!target) {
    check(false, "the mission target must resolve to a geodetic position");
    return;
  }
  auto flight = initial_planetary_flight_state(
      (*body)->descriptor, *target,
      {static_cast<double>(catalog->signals.front().surface_elevation_metres)},
      0.0, FlightMode::manual);
  if (!flight) {
    check(false, "the target approach must initialize planetary flight");
    return;
  }
  flight->tick = 600;
  auto planetfall = initialize_intersystem_planetfall(
      (*body)->descriptor, identities.target_objective, *flight, {}, *cache);
  check(planetfall && planetfall->scanner.selected ==
                           identities.target_objective &&
            planetfall->navigation.status == SignalScannerStatus::reached,
        "Planetfall must bind the mission target without retargeting it");
  if (!planetfall) return;

  auto pilot_contract = initial_intersystem_contract_state(
      Seed{42}, IntersystemRuleProfile::pilot);
  const auto pilot_command = [&](IntersystemContractCommand command) {
    return advance_intersystem_contract(
        pilot_contract, pilot_contract.universe_tick, command);
  };
  bool pilot_ready =
      pilot_command(IntersystemContractCommand::accept_mission) &&
      pilot_command(IntersystemContractCommand::launch) &&
      begin_intersystem_jump(pilot_contract);
  for (SimulationTick tick = 0;
       pilot_ready && tick < kJumpSpoolTicks + kJumpTransitTicks; ++tick) {
    pilot_ready = advance_intersystem_jump_tick(pilot_contract, system)
                      .has_value();
  }
  pilot_ready =
      pilot_ready &&
      pilot_command(IntersystemContractCommand::enter_target_planet)
          .has_value();
  check(pilot_ready,
        "the thermal persistence fixture must reach Pilot Planetfall");
  auto thermal_document =
      make_new_game_document(Seed{42}, NewGameOnboardingChoice::skip);
  thermal_document.state.intersystem_contract = pilot_contract;
  thermal_document.state.flight = planetfall->flight;
  thermal_document.state.flight->tick = pilot_contract.universe_tick;
  thermal_document.state.flight->thermal = {825'000U, true};
  const auto thermal_json = encode_save_document_json(thermal_document);
  const auto thermal_restored =
      thermal_json
          ? decode_save_document_json(*thermal_json)
          : std::expected<SaveDocument, SaveSchemaError>{
                std::unexpected{SaveSchemaError{}}};
  check(thermal_restored && thermal_restored->state.flight &&
            thermal_restored->state.flight->thermal ==
                PlanetaryThermalState{825'000U, true},
        "save format 16 must preserve an active Pilot thermal abort exactly");
  if (thermal_json) {
    const auto corrupt_thermal = decode_save_document_json(replace_once(
        *thermal_json, "\"load_units\": 825000",
        "\"load_units\": 1000001"));
    check(!corrupt_thermal &&
              corrupt_thermal.error().code ==
                  SaveSchemaErrorCode::invalid_state,
          "out-of-range persisted thermal load must be rejected");
  }

  bool completed{};
  for (SimulationTick tick = 0;
       tick < kSignalCollectionTotalInRangeTicks; ++tick) {
    const auto update = advance_intersystem_planetfall(
        *planetfall, *cache, {}, IntersystemRuleProfile::assisted);
    if (!update) {
      check(false, "Planetfall must advance transactionally");
      return;
    }
    completed = completed || update->objective_completed;
  }
  check(completed &&
            planetfall->collection.status == SignalCollectionStatus::complete &&
            planetfall->journal.entries().size() == 1U &&
            planetfall->journal.entries().front().object_key ==
                surface_signal_object_key(identities.target_objective),
        "collection dwell must emit one bound target delta");

  auto restored_cache = TerrainTileCache::create();
  const auto restored =
      restored_cache
          ? initialize_intersystem_planetfall(
                (*body)->descriptor, identities.target_objective,
                planetfall->flight, planetfall->journal.entries(),
                *restored_cache)
          : std::expected<IntersystemPlanetfallState,
                          IntersystemPlanetfallError>{
                std::unexpected{IntersystemPlanetfallError::terrain_failure}};
  check(restored && restored->flight == planetfall->flight &&
            restored->navigation == planetfall->navigation &&
            restored->collection.status == SignalCollectionStatus::complete,
        "Planetfall must hydrate flight, navigation, and completion exactly");

  auto invalid = flight.value();
  invalid.pose.position.latitude_radians =
      std::numeric_limits<double>::quiet_NaN();
  check(initialize_intersystem_planetfall(
            (*body)->descriptor, identities.target_objective, invalid, {},
            *cache) ==
            std::unexpected{IntersystemPlanetfallError::invalid_planet},
        "non-finite Planetfall state must reject before mutation");
  const std::array invalid_delta{
      SaveWorldDelta{surface_signal_object_key(identities.target_objective),
                     SaveWorldDeltaKind::discovered, flight->tick}};
  check(initialize_intersystem_planetfall(
            (*body)->descriptor, identities.target_objective, *flight,
            invalid_delta, *cache) ==
            std::unexpected{IntersystemPlanetfallError::journal_failure},
        "Planetfall must reject non-terminal mission deltas");
}

auto intersystem_planetfall_acceptance_contract() -> void {
  check(!run_intersystem_planetfall_acceptance(0, 64) &&
            !run_intersystem_planetfall_acceptance(
                std::numeric_limits<int>::max(), 1),
        "intersystem Planetfall acceptance must reject invalid dimensions");
  const auto result = run_intersystem_planetfall_acceptance(96, 64);
  const auto identities = generate_first_intersystem_identities(Seed{42});
  check(result && result->report.planet == identities.target_planet &&
            result->report.target == identities.target_objective &&
            result->report.world_delta_count == 1U &&
            result->report.completion_tick == 1020 &&
            result->report.thermal.universe_seed == Seed{39} &&
            result->report.thermal.planet ==
                PlanetId{0x237709a6a1fd198bULL} &&
            result->report.thermal.nominal_peak_load_units == 374U &&
            result->report.thermal.shallow_peak_load_units == 58'770U &&
            result->report.thermal.manual_correction_peak_load_units ==
                35'130U &&
            result->report.thermal.assisted_peak_load_units ==
                kMaximumThermalLoadUnits &&
            result->report.thermal.forced_abort_tick == 803U &&
            result->report.thermal.recovery_orbit_tick == 11'764U &&
            result->report.thermal.deliberate_reentry_tick == 13'108U &&
            result->report.thermal.resumed_recovery_checksum ==
                12'793'732'928'174'323'102ULL &&
            result->final_frame.size() == 96U * 64U,
        "canonical Planetfall acceptance must enter anywhere, measure thermal envelopes, recover, reenter, save, and render");
  if (result) {
    const auto json = intersystem_planetfall_acceptance_json(result->report);
    check(json.find("\"schema_version\": 3") != std::string::npos &&
              json.find("\"scenario\": \"v0.4.17-pilot-thermal-reentry\"") !=
                  std::string::npos &&
              json.find("\"evidence_scope\": \"application_framebuffer\"") !=
                  std::string::npos,
          "thermal Planetfall JSON must name its renderer-neutral evidence scope");
  }
}

auto mission_board_contract() -> void {
  auto state = initial_intersystem_contract_state(Seed{42});
  const auto offered = mission_board_snapshot(state);
  check(offered && offered->station == "station-ce51e866ec4e032d" &&
            offered->mission == "mission-d8e068532886e95b" &&
            offered->destination_system.find("Ortis // system-") == 0 &&
            offered->objective ==
                "SIGNAL SURVEY // signal-9936ac67f2245d20" &&
            offered->status == "OFFERED" &&
            offered->rule_profile == "ASSISTED" &&
            offered->rule_profile_description ==
                "FORGIVING ENTRY // OPTIMAL FTL ARRIVAL" &&
            !offered->rule_profile_selection_enabled &&
            offered->primary_action == "ACCEPT CONTRACT" &&
            offered->primary_action_enabled && !offered->launch_authorized,
        "the offered mission board must expose one complete semantic contract");

  auto advanced = initial_intersystem_contract_state(
      Seed{42}, IntersystemRuleProfile::pilot);
  const auto accept = advance_intersystem_contract(
      advanced, advanced.universe_tick,
      IntersystemContractCommand::accept_mission);
  const auto accepted = mission_board_snapshot(advanced);
  check(accept && accepted && accepted->status == "ACCEPTED" &&
            accepted->rule_profile == "ADVANCED" &&
            accepted->rule_profile_description ==
                "THERMAL ABORTS // ALIGNMENT-GRADED ARRIVAL" &&
            !accepted->rule_profile_selection_enabled &&
            accepted->primary_action == "LAUNCH" &&
            accepted->primary_action_enabled && accepted->launch_authorized,
        "acceptance must authorize an explicit intersystem launch");

  auto corrupted = advanced;
  corrupted.identities.target_objective.value ^= 1U;
  check(mission_board_snapshot(corrupted) ==
            std::unexpected{MissionBoardError::invalid_contract},
        "the mission board must reject corrupt deterministic references");

  auto document =
      make_new_game_document(Seed{42}, NewGameOnboardingChoice::skip);
  const auto fresh_json = encode_save_document_json(document);
  check(fresh_json &&
            decode_save_document_json(replace_once(
                *fresh_json, "signal-9936ac67f2245d20",
                "signal-9936ac67f2245d21")) ==
                std::unexpected{SaveSchemaError{
                    SaveSchemaErrorCode::identity_mismatch,
                    "$.state.intersystem_contract.identities",
                    "stored first-contract identities do not match deterministic regeneration"}},
        "a corrupt saved mission objective must fail before state commit");
  auto pilot_document = make_new_game_document(NewGameOptions{
      .universe_seed = Seed{42},
      .penalty_mode = IntersystemRuleProfile::pilot,
      .onboarding = NewGameOnboardingChoice::skip,
  });
  const auto pilot_json = encode_save_document_json(pilot_document);
  const auto pilot_round_trip =
      pilot_json ? decode_save_document_json(*pilot_json)
                 : std::expected<SaveDocument, SaveSchemaError>{
                       std::unexpected{SaveSchemaError{}}};
  check(pilot_json &&
            pilot_json->find("\"rule_profile\": \"pilot\"") !=
                std::string::npos &&
            pilot_round_trip &&
            pilot_round_trip->state.intersystem_contract->rule_profile ==
                IntersystemRuleProfile::pilot,
        "format v15 must preserve the authoritative Pilot selection");
  if (pilot_json) {
    const auto invalid_profile = decode_save_document_json(replace_once(
        *pilot_json, "\"rule_profile\": \"pilot\"",
        "\"rule_profile\": \"simulation\""));
    check(!invalid_profile &&
              invalid_profile.error().code ==
                  SaveSchemaErrorCode::invalid_value &&
              invalid_profile.error().path ==
                  "$.state.intersystem_contract.rule_profile",
          "format v15 must reject unknown rule profiles before state commit");
  }
  const auto round_trip = [&](const IntersystemContractState& checkpoint,
                              const char* message) {
    document.state.intersystem_contract = checkpoint;
    document.state.onboarding =
        checkpoint.mission_phase == IntersystemMissionPhase::turned_in
            ? OnboardingProgress{.state = OnboardingState::completed,
                                 .chapter = std::nullopt}
            : OnboardingProgress{
                  .state = OnboardingState::guided,
                  .chapter = OnboardingChapter::contract_three};
    document.state.system_flight.reset();
    document.state.origin_station_flight.reset();
    document.state.flight.reset();
    document.state.world_deltas.clear();
    if (checkpoint.mission_phase ==
            IntersystemMissionPhase::objective_complete ||
        checkpoint.mission_phase == IntersystemMissionPhase::returned ||
        checkpoint.mission_phase == IntersystemMissionPhase::turned_in) {
      document.state.world_deltas.push_back(
          {surface_signal_object_key(
               checkpoint.identities.target_objective),
           SaveWorldDeltaKind::collected, checkpoint.universe_tick});
    }
    const auto system =
        generate_local_system(checkpoint.identities.target_system_seed);
    const auto body = find_local_system_planet(
        system, checkpoint.identities.target_planet);
    if (checkpoint.travel_phase ==
        IntersystemTravelPhase::origin_system_flight) {
      const auto origin =
          generate_origin_system(checkpoint.identities.universe_seed);
      const auto station_flight =
          initialize_origin_station_launch(checkpoint, origin);
      if (station_flight) {
        document.state.origin_station_flight = *station_flight;
      }
    } else if (checkpoint.travel_phase ==
                   IntersystemTravelPhase::target_system_flight &&
               body && checkpoint.arrival_solution) {
      const auto system_flight = initial_system_flight_state(
          system, checkpoint.identities.target_planet,
          *checkpoint.arrival_solution);
      if (system_flight) document.state.system_flight = *system_flight;
    } else if (checkpoint.travel_phase ==
                   IntersystemTravelPhase::target_planet_flight &&
               body) {
      auto flight = initial_planetary_flight_state(
          (*body)->descriptor,
          {.latitude_radians = 0.0,
           .longitude_radians = 0.0,
           .altitude_metres =
               static_cast<double>((*body)->descriptor.radius.value) *
               2'000.0},
          {.surface_elevation_metres = 0.0});
      if (flight) {
        flight->tick = checkpoint.universe_tick;
        document.state.flight = *flight;
      }
    }
    const auto encoded = encode_save_document_json(document);
    if (!encoded) {
      check(false, message);
      return;
    }
    const auto decoded = decode_save_document_json(*encoded);
    check(decoded && decoded->state.intersystem_contract == checkpoint &&
              decoded->state.onboarding == document.state.onboarding,
          message);
  };
  round_trip(initial_intersystem_contract_state(Seed{42}),
             "offered intersystem state must survive a v15 round trip");
  check(advance_intersystem_contract(
            state, state.universe_tick,
            IntersystemContractCommand::accept_mission)
            .has_value(),
        "the released-save fixture must accept its locked Assisted profile");
  round_trip(state, "accepted intersystem state must survive a v15 round trip");

  const auto command = [&](IntersystemContractCommand value) {
    return advance_intersystem_contract(state, state.universe_tick, value);
  };
  check(command(IntersystemContractCommand::launch).has_value(),
        "the released-save fixture must launch with its historical Assisted "
        "profile");
  round_trip(state, "active intersystem state must survive a v15 round trip");
  bool objective_ready = begin_intersystem_jump(state).has_value();
  const auto target_system =
      generate_local_system(state.identities.target_system_seed);
  for (SimulationTick tick = 0;
       objective_ready && tick < kJumpSpoolTicks + kJumpTransitTicks; ++tick) {
    objective_ready =
        advance_intersystem_jump_tick(state, target_system).has_value();
  }
  objective_ready =
      objective_ready &&
      command(IntersystemContractCommand::enter_target_planet).has_value() &&
      command(IntersystemContractCommand::complete_objective).has_value();
  check(objective_ready,
        "the persistence fixture must reach objective completion legally");
  round_trip(
      state,
      "objective-complete intersystem state must survive a v15 round trip");
  auto inconsistent_completion = document;
  inconsistent_completion.state.world_deltas.clear();
  check(!encode_save_document_json(inconsistent_completion),
        "format v15 completion must require its bound collected delta");
  bool returned =
      command(IntersystemContractCommand::leave_target_planet).has_value() &&
      begin_intersystem_jump(state).has_value();
  const auto origin_system =
      generate_origin_system(state.identities.universe_seed);
  for (SimulationTick tick = 0;
       returned && tick < kJumpSpoolTicks + kJumpTransitTicks; ++tick) {
    returned = advance_intersystem_jump_tick(state, origin_system).has_value();
  }
  auto return_approach =
      returned
          ? initialize_origin_return(state, origin_system)
          : std::expected<OriginStationFlightState, OriginStationFlightError>{
                std::unexpected{OriginStationFlightError::invalid_arrival}};
  if (return_approach) {
    return_approach->relative_position = {kOriginStationArrivalRadiusMetres,
                                          0.0, 0.0};
    return_approach->relative_velocity = {};
    return_approach->forward = {-1.0, 0.0, 0.0};
    returned = attempt_origin_docking(state, origin_system, *return_approach)
                   .has_value();
  } else {
    returned = false;
  }
  check(returned, "the persistence fixture must return legally");
  round_trip(state, "returned intersystem state must survive a v15 round trip");
  check(command(IntersystemContractCommand::turn_in).has_value(),
        "the persistence fixture must turn in legally");
  round_trip(state,
             "turned-in intersystem state must survive a v15 round trip");
}

auto local_system_contract() -> void {
  check(kLocalSystemGeneratorVersion == 1 &&
            kAnalyticEphemerisVersion == 1 &&
            kMinimumLocalSystemPlanets == 3 &&
            kMaximumLocalSystemPlanets == 6,
        "local-system version and catalog bounds must remain stable");

  for (const auto seed :
       std::array{Seed{0}, Seed{42},
                  Seed{std::numeric_limits<std::uint64_t>::max()}}) {
    const auto first = generate_local_system(seed);
    const auto again = generate_local_system(seed);
    check(first == again && validate_local_system(first).has_value(),
          "equal system seeds must reproduce one valid catalog");
    check(first.seed == seed && first.id == SystemId{seed.value} &&
              first.planets.size() >= kMinimumLocalSystemPlanets &&
              first.planets.size() <= kMaximumLocalSystemPlanets,
          "a generated local system must retain its identity and bounds");
    for (std::size_t index = 0; index < first.planets.size(); ++index) {
      const auto& planet = first.planets[index];
      const auto expected_seed =
          derive_seed(seed, SeedDomain::planet, index);
      check(planet.descriptor == generate_planet_descriptor(expected_seed) &&
                planet.orbit.planet == planet.descriptor.id &&
                planet.orbit.ordinal == index,
            "each catalog ordinal must retain independent planet identity");
    }
  }

  const auto identities = generate_first_intersystem_identities(Seed{42});
  const auto system = generate_local_system(identities.target_system_seed);
  check(system.id == identities.target_system &&
            system.star.id == identities.target_star &&
            !system.planets.empty() &&
            system.planets.front().descriptor.id == identities.target_planet,
        "the first target catalog must preserve the mission's stable IDs");
  const auto target_before =
      generate_planet_descriptor(identities.target_planet_seed);
  (void)generate_local_system(identities.target_system_seed);
  check(target_before ==
            generate_planet_descriptor(identities.target_planet_seed),
        "catalog generation must not perturb the existing planet stream");

  check(system.planets.size() == 6 &&
            system.star ==
                StarDescriptor{
                    Seed{4'391'435'423'288'202'480ULL},
                    StarId{4'391'435'423'288'202'480ULL}, "Ortis",
                    StarSpectralClass::f, 6'426, 1'276'641,
                    Rgb8{234, 239, 255}},
        "universe seed 42 must retain its target star and catalog size");
  struct OrbitGolden {
    std::uint64_t planet;
    std::uint64_t orbit;
    std::uint64_t radius;
    SimulationTick period;
    std::uint32_t phase;
    std::int32_t inclination;
    std::uint32_t node;
  };
  constexpr std::array orbit_goldens{
      OrbitGolden{11'663'323'411'267'002'299ULL,
                  10'083'446'351'388'880'177ULL, 7'099'452, 2'783'653,
                  2'721'764'640U, -9'833'115, 3'923'635'174U},
      OrbitGolden{9'431'008'004'299'412'890ULL,
                  7'851'130'944'421'290'768ULL, 18'428'603, 6'272'031,
                  1'567'099'797U, -5'965'106, 939'652'002U},
      OrbitGolden{7'198'692'597'331'823'481ULL,
                  14'548'077'165'324'058'995ULL, 30'784'617, 9'894'474,
                  3'114'248'701U, 3'697'998, 3'328'899'740U},
      OrbitGolden{4'966'377'190'364'234'072ULL,
                  12'315'761'758'356'469'586ULL, 41'919'621, 13'232'201,
                  2'274'257'782U, -9'742'142, 441'980'010U},
      OrbitGolden{2'145'840'965'427'808'319ULL, 565'963'905'549'686'197ULL,
                  54'281'536, 16'933'492, 203'468'468U, -9'843'524,
                  2'992'320'609U},
      OrbitGolden{18'360'269'632'169'770'526ULL,
                  16'780'392'572'291'648'404ULL, 64'236'026, 20'542'364,
                  367'035'075U, 7'034'988, 1'949'158'013U},
  };
  for (std::size_t index = 0; index < orbit_goldens.size(); ++index) {
    const auto& observed = system.planets[index];
    const auto& golden = orbit_goldens[index];
    const auto expected_orbit =
        PlanetOrbit{Seed{golden.orbit}, PlanetId{golden.planet},
                    static_cast<std::uint32_t>(index), golden.radius,
                    golden.period, golden.phase, golden.inclination,
                    golden.node};
    check(observed.descriptor.id == PlanetId{golden.planet} &&
              observed.orbit == expected_orbit,
          "universe seed 42 must retain its target orbit catalog");

    const auto start = resolve_planet_ephemeris(
        system, observed.descriptor.id, {0, 0.0});
    const auto repeated = resolve_planet_ephemeris(
        system, observed.descriptor.id, {observed.orbit.period_ticks, 0.0});
    const auto extreme = resolve_planet_ephemeris(
        system, observed.descriptor.id,
        {std::numeric_limits<SimulationTick>::max(), 0.999999});
    const auto radius_metres =
        static_cast<double>(observed.orbit.radius_kilometres) * 1'000.0;
    check(start.has_value() && repeated.has_value() && extreme.has_value() &&
              start->position == repeated->position &&
              start->velocity == repeated->velocity &&
              std::abs(std::hypot(start->position.x, start->position.y,
                                  start->position.z) -
                       radius_metres) <= 1.0,
          "every generated orbit must be bounded, periodic, and overflow-safe");
  }

  const auto target = system.planets.front().descriptor.id;
  const auto epoch = resolve_planet_ephemeris(system, target, {0, 0.0});
  check(epoch.has_value(), "a valid target ephemeris must resolve");
  if (epoch) {
    check(epoch->position ==
                  SystemPositionMetres{-6'748'693'202.0, -2'010'489'231.0,
                                       902'935'114.0} &&
              epoch->velocity ==
                  SystemVelocityMetresPerSecond{572'317.287, -1'822'691.659,
                                                219'165.062} &&
              close_enough(epoch->phase_radians, 3.981718699366, 1.0e-12),
          "universe seed 42 must retain its target ephemeris epoch");
    const auto period = system.planets.front().orbit.period_ticks;
    const auto repeated =
        resolve_planet_ephemeris(system, target, {period, 0.0});
    check(repeated.has_value() && repeated->position == epoch->position &&
              repeated->velocity == epoch->velocity &&
              repeated->cycle_tick == 0,
          "analytic ephemerides must repeat exactly after one period");
    const auto half =
        resolve_planet_ephemeris(system, target, {period / 2, 0.0});
    check(half.has_value() && half->cycle_tick == period / 2 &&
              std::isfinite(half->position.x) &&
              std::isfinite(half->position.y) &&
              std::isfinite(half->position.z),
          "analytic ephemerides must remain finite inside their period");
    const auto maximum = resolve_planet_ephemeris(
        system, target,
        {std::numeric_limits<SimulationTick>::max(), 0.999999});
    check(maximum.has_value(),
          "maximum authoritative ticks must resolve without overflow");
  }

  const auto unknown = resolve_planet_ephemeris(
      system, PlanetId{system.id.value}, {0, 0.0});
  check(!unknown && unknown.error() == LocalSystemError::unknown_planet,
        "unknown planet references must be rejected");
  for (const auto fraction :
       std::array{std::numeric_limits<double>::quiet_NaN(),
                  std::numeric_limits<double>::infinity(), -0.01, 1.0}) {
    const auto invalid =
        resolve_planet_ephemeris(system, target, {0, fraction});
    check(!invalid && invalid.error() == LocalSystemError::non_finite_time,
          "non-finite and out-of-range ephemeris time must be rejected");
  }

  auto malformed_identity = system;
  malformed_identity.id.value ^= 1U;
  check(!validate_local_system(malformed_identity) &&
            validate_local_system(malformed_identity).error() ==
                LocalSystemError::invalid_system,
        "a mismatched system identity must be rejected");
  auto malformed_star = system;
  malformed_star.star.temperature_kelvin = 0;
  check(!validate_local_system(malformed_star) &&
            validate_local_system(malformed_star).error() ==
                LocalSystemError::invalid_star,
        "an invalid star descriptor must be rejected");
  auto malformed_orbit = system;
  malformed_orbit.planets.front().orbit.period_ticks = 0;
  check(!validate_local_system(malformed_orbit) &&
            validate_local_system(malformed_orbit).error() ==
                LocalSystemError::invalid_orbit,
        "an invalid orbital period must be rejected");
  const LocalSystemDescriptor malformed_catalog{
      .seed = system.seed,
      .id = system.id,
      .star = system.star,
      .planets = std::vector<LocalSystemPlanet>{
          system.planets.begin(),
          system.planets.begin() +
              static_cast<std::ptrdiff_t>(kMinimumLocalSystemPlanets - 1)},
  };
  check(!validate_local_system(malformed_catalog) &&
            validate_local_system(malformed_catalog).error() ==
                LocalSystemError::invalid_planet_catalog,
        "an out-of-bounds planet catalog must be rejected");

  const auto diagnostic = local_system_diagnostic_json(system);
  check(diagnostic.has_value() &&
            diagnostic->find("\"generator_version\": 1") !=
                std::string::npos &&
            diagnostic->find(system_id_string(system.id)) !=
                std::string::npos &&
            diagnostic->find("planet-") != std::string::npos,
        "local-system diagnostics must expose compact stable identities");
  if (diagnostic) {
    auto checksum = std::uint64_t{14695981039346656037ULL};
    for (const auto byte : *diagnostic) {
      checksum ^= static_cast<unsigned char>(byte);
      checksum *= 1099511628211ULL;
    }
    check(checksum == 15'119'861'268'817'475'810ULL,
          "local-system diagnostics must retain their golden checksum");
  }
}

[[nodiscard]] auto system_render_fixture_view(
    const LocalSystemDescriptor& system, SimulationTick tick = 0)
    -> LocalSystemView {
  const auto target = resolve_planet_ephemeris(
      system, system.planets.front().descriptor.id, {tick, 0.0});
  if (!target) return {};
  constexpr SystemPositionMetres offset{0.0, -1'000'000'000.0,
                                         120'000'000.0};
  return {
      .time = {tick, 0.0},
      .position = {target->position.x + offset.x,
                   target->position.y + offset.y,
                   target->position.z + offset.z},
      .velocity = target->velocity,
      .forward = {-offset.x, -offset.y, -offset.z},
      .up = {0.0, 0.0, 1.0},
      .selected_planet = target->planet,
  };
}

auto local_system_rendering_contract() -> void {
  const auto identities = generate_first_intersystem_identities(Seed{42});
  const auto system = generate_local_system(identities.target_system_seed);
  auto view = system_render_fixture_view(system);
  const auto target = resolve_planet_ephemeris(
      system, view.selected_planet, view.time);
  check(target.has_value(), "the system-render target fixture must resolve");
  if (!target) return;

  auto closing_view = view;
  closing_view.velocity.y += 100.0;
  const auto closing = resolve_system_navigation(system, closing_view);
  check(closing && closing->target == identities.target_planet &&
            closing->display_name ==
                system.planets.front().descriptor.display_name &&
            closing->distance_metres > 1'000'000.0 &&
            closing->closing_speed_metres_per_second > 90.0 &&
            closing->motion == SystemTargetMotion::closing &&
            closing->in_front,
        "system navigation must derive stable target identity, range, and closing state");
  auto opening_view = view;
  opening_view.velocity.y -= 100.0;
  const auto opening = resolve_system_navigation(system, opening_view);
  check(opening && opening->closing_speed_metres_per_second < -90.0 &&
            opening->motion == SystemTargetMotion::opening,
        "system navigation must distinguish opening motion");
  if (closing) {
    const auto readout = format_system_navigation(*closing);
    check(readout.target.size() == kInstrumentLineWidth &&
              readout.bearing.size() == kInstrumentLineWidth &&
              readout.elevation.size() == kInstrumentLineWidth &&
              readout.distance.size() == kInstrumentLineWidth &&
              readout.motion.size() == kInstrumentLineWidth &&
              readout.arrival.size() == kInstrumentLineWidth &&
              readout.cue.size() == kInstrumentLineWidth,
          "system navigation must remain fixed-width and information-complete");
    const auto guided = format_system_navigation(
        *closing,
        {.target = closing->target,
         .target_radius_metres = 1'000.0,
         .distance_metres = closing->distance_metres,
         .closing_speed_metres_per_second = 1'200.0,
         .relative_speed_metres_per_second = 1'200.0,
         .arrival_estimate_seconds = 65.0,
         .stopping_distance_metres = 14'400.0,
         .cue = SystemFlightCue::brake,
         .inside_approach_boundary = false,
         .orbit_insertion_ready = false});
    check(guided.motion == "CLS +1.2k" &&
              guided.arrival == "ETA 01:05" &&
              guided.cue == "BRAKE NOW",
          "system flight guidance must expose closing, ETA, and braking text");

    SystemFlightState status_state;
    status_state.mode = FlightMode::autopilot;
    status_state.time_scale = SystemTimeScale::sixteen;
    const SystemFlightGuidance blocked_guidance{
        .target = closing->target,
        .target_radius_metres = 1'000.0,
        .distance_metres = 5'000.0,
        .closing_speed_metres_per_second = 500.0,
        .relative_speed_metres_per_second = 8'000.0,
        .arrival_estimate_seconds = 10.0,
        .stopping_distance_metres = 2'500.0,
        .cue = SystemFlightCue::brake,
        .inside_approach_boundary = true,
        .orbit_insertion_ready = false,
    };
    const auto blocked_status =
        format_system_flight_status(status_state, blocked_guidance);
    check(blocked_status.message.contains("AUTO HOLD") &&
              blocked_status.message.contains("16x") &&
              blocked_status.message.contains("BRAKE NOW") &&
              !blocked_status.message.contains("ENTER") &&
              blocked_status.insertion_refusal.contains("RNG 5.0R>3R") &&
              blocked_status.insertion_refusal.contains("REL 8.0k>4k") &&
              blocked_status.insertion_refusal.contains("RAD 500>250m/s"),
          "blocked system flight must hide Enter and name every unsafe insertion threshold");

    auto below_surface_guidance = blocked_guidance;
    below_surface_guidance.distance_metres = 900.0;
    below_surface_guidance.closing_speed_metres_per_second = 0.0;
    below_surface_guidance.relative_speed_metres_per_second = 0.0;
    const auto below_surface_status =
        format_system_flight_status(status_state, below_surface_guidance);
    check(below_surface_status.insertion_refusal.contains("ALT -100<16m") &&
              !below_surface_status.insertion_ready,
          "below-surface system flight must name the minimum-clearance threshold");

    status_state.mode = FlightMode::manual;
    status_state.time_scale = SystemTimeScale::four;
    auto ready_guidance = blocked_guidance;
    ready_guidance.distance_metres = 2'000.0;
    ready_guidance.closing_speed_metres_per_second = 200.0;
    ready_guidance.relative_speed_metres_per_second = 3'000.0;
    ready_guidance.cue = SystemFlightCue::orbit_ready;
    ready_guidance.orbit_insertion_ready = true;
    const auto ready_status =
        format_system_flight_status(status_state, ready_guidance);
    check(ready_status.message.contains("ORBIT RDY") &&
              ready_status.message.contains("ENTER INSERT ORBIT") &&
              ready_status.message.contains("MANUAL") &&
              ready_status.message.contains("4x") &&
              ready_status.insertion_refusal.empty(),
          "ready system flight must expose the actionable Enter cue and active controls");

    auto invalid_guidance = ready_guidance;
    invalid_guidance.target_radius_metres =
        std::numeric_limits<double>::quiet_NaN();
    const auto invalid_status =
        format_system_flight_status(status_state, invalid_guidance);
    check(invalid_status.message == " SYSTEM GUIDANCE INVALID " &&
              invalid_status.insertion_refusal.contains("GUIDANCE INVALID") &&
              !invalid_status.insertion_ready,
          "non-finite system guidance must fail closed before presentation");
    auto scaled_navigation = *closing;
    scaled_navigation.distance_metres = 93'666'774'627.0;
    scaled_navigation.closing_speed_metres_per_second = 1'851'067.0;
    const auto scaled = format_system_navigation(scaled_navigation);
    check(scaled.distance == "RNG 0094G" && scaled.motion == "CLS +1.9M" &&
              scaled.distance.size() == kInstrumentLineWidth &&
              scaled.motion.size() == kInstrumentLineWidth,
          "system-scale range and speed must retain bounded readable units");
  }

  constexpr LocalSystemRenderSettings settings{
      .width = 160,
      .height = 120,
      .field_of_view_degrees = 60.0,
      .near_clip_metres = 1'000.0,
      .far_clip_metres = 100'000'000'000.0,
      .handoff_start_radius_pixels = 24.0,
      .handoff_complete_radius_pixels = 48.0,
  };
  LocalSystemRenderer renderer{settings};
  std::vector<Pixel> first(160U * 120U);
  std::vector<Pixel> repeated(first.size());
  const auto first_result = renderer.render(system, view, first);
  const auto repeated_result = renderer.render(system, view, repeated);
  check(first_result && repeated_result && first_result == repeated_result &&
            first == repeated && first_result->selected_visible &&
            first_result->visible_planets > 0,
        "a fixed system view must render deterministic bodies and selection cues");

  auto later_view = system_render_fixture_view(
      system, system.planets.front().orbit.period_ticks / 4);
  std::vector<Pixel> later(first.size());
  const auto later_result = renderer.render(system, later_view, later);
  check(later_result && pixel_checksum(later) != pixel_checksum(first),
        "planet presentation must move with explicit ephemeris time");

  auto behind_view = view;
  behind_view.forward = {0.0, -1.0, 0.0};
  std::vector<Pixel> behind(first.size());
  const auto behind_result = renderer.render(system, behind_view, behind);
  check(behind_result && !behind_result->selected_visible &&
            !behind_result->navigation.in_front,
        "a selected planet behind the camera must remain navigable but not render as visible");

  const double target_radius = static_cast<double>(
                                   system.planets.front().descriptor.radius.value) *
                               1'000.0;
  auto handoff_view = view;
  handoff_view.position = {target->position.x,
                           target->position.y - target_radius * 4.0,
                           target->position.z};
  handoff_view.forward = {0.0, 1.0, 0.0};
  std::vector<Pixel> handoff(first.size());
  const auto handoff_result =
      renderer.render(system, handoff_view, handoff);
  check(handoff_result &&
            handoff_result->mode ==
                LocalSystemPresentationMode::target_handoff &&
            handoff_result->orbital_mix > 0.0 &&
            handoff_result->orbital_mix < 1.0 &&
            handoff_result->selected_visible,
        "target approach must blend continuously into the existing orbital renderer");

  auto orbital_view = handoff_view;
  orbital_view.position.y = target->position.y - target_radius * 3.0;
  std::vector<Pixel> orbital(first.size());
  const auto orbital_result =
      renderer.render(system, orbital_view, orbital);
  check(orbital_result &&
            orbital_result->mode ==
                LocalSystemPresentationMode::orbital_target &&
            orbital_result->orbital_mix == 1.0 &&
            orbital_result->navigation.target == identities.target_planet,
        "a close target must complete the orbital handoff without changing identity");

  LocalSystemRenderer clipped_renderer{
      {.width = 160, .height = 120, .field_of_view_degrees = 60.0,
       .near_clip_metres = 2'000'000'000.0,
       .far_clip_metres = 3'000'000'000.0}};
  std::vector<Pixel> clipped(first.size());
  const auto clipped_result =
      clipped_renderer.render(system, view, clipped);
  check(clipped_result && !clipped_result->selected_visible &&
            clipped_result->navigation.target == identities.target_planet,
        "near/far clipping must not discard selected-target navigation identity");

  std::vector<Pixel> short_frame(first.size() - 1U, {1, 2, 3, 4});
  const auto short_result = renderer.render(system, view, short_frame);
  check(!short_result &&
            short_result.error() ==
                LocalSystemRenderError::invalid_framebuffer &&
            std::ranges::all_of(short_frame, [](Pixel value) {
              return value == Pixel{1, 2, 3, 4};
            }),
        "invalid system framebuffers must fail without mutation");

  std::vector<Pixel> untouched(first.size(), {5, 6, 7, 8});
  auto invalid_view = view;
  invalid_view.position.x = std::numeric_limits<double>::quiet_NaN();
  const auto invalid_result = renderer.render(system, invalid_view, untouched);
  check(!invalid_result &&
            invalid_result.error() == LocalSystemRenderError::invalid_view &&
            std::ranges::all_of(untouched, [](Pixel value) {
              return value == Pixel{5, 6, 7, 8};
            }),
        "non-finite system views must fail before touching the framebuffer");
  invalid_view = view;
  invalid_view.up = invalid_view.forward;
  check(!renderer.render(system, invalid_view, untouched) &&
            std::ranges::all_of(untouched, [](Pixel value) {
              return value == Pixel{5, 6, 7, 8};
            }),
        "invalid system camera bases must fail transactionally");
  invalid_view = view;
  invalid_view.selected_planet = PlanetId{system.id.value};
  const auto unknown = renderer.render(system, invalid_view, untouched);
  check(!unknown &&
            unknown.error() == LocalSystemRenderError::unknown_target,
        "unknown selected planets must be rejected");

  LocalSystemRenderer invalid_settings{
      {.width = 0, .height = 120}};
  const auto invalid_settings_result =
      invalid_settings.render(system, view, untouched);
  check(!invalid_settings_result &&
            invalid_settings_result.error() ==
                LocalSystemRenderError::invalid_settings &&
            std::ranges::all_of(untouched, [](Pixel value) {
              return value == Pixel{5, 6, 7, 8};
            }),
        "invalid system-render settings must fail transactionally");

  const auto origin_system = generate_origin_system(Seed{42});
  const auto station = generate_origin_station(Seed{42});
  const auto station_epoch = resolve_origin_station_ephemeris(
      origin_system, station, {.tick = 0, .sub_tick_fraction = 0.0});
  const auto station_quarter = resolve_origin_station_ephemeris(
      origin_system, station,
      {.tick = station.orbit.period_ticks / 4U, .sub_tick_fraction = 0.0});
  const auto station_view = [&](const OriginStationEphemeris& ephemeris,
                                SimulationTick tick) {
    const SystemPositionMetres camera{
        ephemeris.position.x - ephemeris.host_relative_position.x,
        ephemeris.position.y - ephemeris.host_relative_position.y -
            50'000'000.0,
        ephemeris.position.z - ephemeris.host_relative_position.z +
            10'000'000.0};
    return LocalSystemView{
        .time = {.tick = tick, .sub_tick_fraction = 0.0},
        .position = camera,
        .velocity = ephemeris.velocity,
        .forward = {-camera.x + ephemeris.position.x -
                        ephemeris.host_relative_position.x,
                    -camera.y + ephemeris.position.y -
                        ephemeris.host_relative_position.y,
                    -camera.z + ephemeris.position.z -
                        ephemeris.host_relative_position.z},
        .up = {0.0, 0.0, 1.0},
        .selected_planet = station.orbit.host_planet,
    };
  };
  if (station_epoch && station_quarter) {
    const auto epoch_view = station_view(*station_epoch, 0);
    const auto quarter_view =
        station_view(*station_quarter, station.orbit.period_ticks / 4U);
    std::vector<Pixel> epoch_frame(first.size());
    std::vector<Pixel> quarter_frame(first.size());
    const auto epoch_background =
        renderer.render(origin_system, epoch_view, epoch_frame);
    const auto epoch_overlay =
        renderer.render_origin_station(epoch_view, *station_epoch, epoch_frame);
    const auto quarter_background =
        renderer.render(origin_system, quarter_view, quarter_frame);
    const auto quarter_overlay = renderer.render_origin_station(
        quarter_view, *station_quarter, quarter_frame);
    check(epoch_background && epoch_overlay && quarter_background &&
              quarter_overlay &&
              pixel_checksum(epoch_frame) != pixel_checksum(quarter_frame),
          "the home planet and station marker must render their tick-resolved "
          "orbital relationship");

    auto behind_view = epoch_view;
    behind_view.forward = {
        behind_view.position.x - station_epoch->position.x,
        behind_view.position.y - station_epoch->position.y,
        behind_view.position.z - station_epoch->position.z,
    };
    std::vector<Pixel> behind_frame(first.size(), {3, 6, 13, 255});
    const auto behind_before = pixel_checksum(behind_frame);
    const auto behind_overlay = renderer.render_origin_station(
        behind_view, *station_epoch, behind_frame);
    check(behind_overlay && pixel_checksum(behind_frame) != behind_before,
          "a station behind the camera must leave a deterministic edge cue");

    std::vector<Pixel> station_short(first.size() - 1U, {9, 8, 7, 6});
    auto non_finite_station = *station_epoch;
    non_finite_station.position.x = std::numeric_limits<double>::quiet_NaN();
    std::vector<Pixel> station_untouched(first.size(), {9, 8, 7, 6});
    auto invalid_station_view = epoch_view;
    invalid_station_view.forward.x =
        std::numeric_limits<double>::quiet_NaN();
    check(
        renderer.render_origin_station(epoch_view, *station_epoch,
                                       station_short) ==
                std::unexpected{LocalSystemRenderError::invalid_framebuffer} &&
            std::ranges::all_of(
                station_short,
                [](Pixel value) { return value == Pixel{9, 8, 7, 6}; }) &&
            renderer.render_origin_station(epoch_view, non_finite_station,
                                           station_untouched) ==
                std::unexpected{LocalSystemRenderError::ephemeris_failure} &&
            renderer.render_origin_station(invalid_station_view,
                                           *station_epoch,
                                           station_untouched) ==
                std::unexpected{LocalSystemRenderError::invalid_view} &&
            std::ranges::all_of(
                station_untouched,
                [](Pixel value) { return value == Pixel{9, 8, 7, 6}; }),
        "station overlays must reject buffer boundaries and non-finite "
        "ephemerides before drawing");
  } else {
    check(false, "the station-render fixture ephemerides must resolve");
  }

  struct ProfileGolden {
    RenderProfile profile;
    std::uint64_t checksum;
  };
  constexpr std::array goldens{
      ProfileGolden{RenderProfile::remote, 2278210513076417452ULL},
      ProfileGolden{RenderProfile::balanced, 12485442992023896220ULL},
      ProfileGolden{RenderProfile::local, 12448240633158274672ULL},
      ProfileGolden{RenderProfile::cinematic, 7286572350764431653ULL},
  };
  for (const auto golden : goldens) {
    const auto viewport = profile_viewport(golden.profile);
    LocalSystemRenderer profile_renderer{
        {.width = viewport.width, .height = viewport.height,
         .field_of_view_degrees = 60.0}};
    std::vector<Pixel> frame(static_cast<std::size_t>(viewport.width) *
                             static_cast<std::size_t>(viewport.height));
    const auto rendered = profile_renderer.render(system, view, frame);
    const auto checksum = pixel_checksum(frame);
    if (rendered && checksum != golden.checksum) {
      std::fprintf(stderr, "%.*s golden system checksum: %llu\n",
                   static_cast<int>(profile_name(golden.profile).size()),
                   profile_name(golden.profile).data(),
                   static_cast<unsigned long long>(checksum));
    }
    check(rendered && checksum == golden.checksum,
          "golden local-system profile checksums must remain stable");
  }
}

auto origin_station_contract() -> void {
  check(kOriginStationGeneratorVersion == 2,
        "origin-station generator version 2 must remain stable");
  check(kOriginSystemOrdinal == 0 && kOriginStationOrdinal == 0,
        "the version 1 origin path must retain its named ordinals");

  constexpr std::array<std::uint64_t, 3> universe_seeds{
      0, 42, std::numeric_limits<std::uint64_t>::max()};
  constexpr std::array<std::uint64_t, universe_seeds.size()> system_goldens{
      2662095937669570104ULL, 677859337506523986ULL, 4480404333408418992ULL};
  constexpr std::array<std::uint64_t, universe_seeds.size()> station_goldens{
      7159869865471737051ULL, 14866919373675561773ULL, 15849284578567890724ULL};
  for (std::size_t index = 0; index < universe_seeds.size(); ++index) {
    const auto station = generate_origin_station(Seed{universe_seeds[index]});
    check(station == generate_origin_station(Seed{universe_seeds[index]}),
          "an origin station must reproduce for the same universe seed");
    if (station.home_system_seed.value != system_goldens[index] ||
        station.station_seed.value != station_goldens[index]) {
      std::fprintf(stderr,
                   "origin station golden %zu: system=%llu station=%llu\n",
                   index,
                   static_cast<unsigned long long>(
                       station.home_system_seed.value),
                   static_cast<unsigned long long>(station.station_seed.value));
    }
    check(station.home_system_seed.value == system_goldens[index] &&
              station.station_seed.value == station_goldens[index] &&
              station.id.value == station_goldens[index],
          "origin stations must retain their version 1 golden identities");
  }
  check(origin_station_id_string(OriginStationId{0}) ==
                "station-0000000000000000" &&
            origin_station_id_string(
                OriginStationId{std::numeric_limits<std::uint64_t>::max()}) ==
                "station-ffffffffffffffff",
        "station IDs must retain their fixed-width canonical encoding");

  constexpr Seed universe{42};
  const auto system_before =
      derive_seed(universe, SeedDomain::system, kOriginSystemOrdinal);
  const auto planet_before = derive_seed(system_before, SeedDomain::planet, 0);
  const auto terrain_before =
      derive_seed(planet_before, SeedDomain::terrain, 0);
  const auto weather_before =
      derive_seed(planet_before, SeedDomain::weather, 0);
  const auto encounter_before =
      derive_seed(planet_before, SeedDomain::encounter, 0);
  (void)generate_origin_station(universe);
  check(
      system_before ==
              derive_seed(universe, SeedDomain::system, kOriginSystemOrdinal) &&
          planet_before == derive_seed(system_before, SeedDomain::planet, 0) &&
          terrain_before ==
              derive_seed(planet_before, SeedDomain::terrain, 0) &&
          weather_before ==
              derive_seed(planet_before, SeedDomain::weather, 0) &&
          encounter_before ==
              derive_seed(planet_before, SeedDomain::encounter, 0),
      "origin-station derivation must not perturb unrelated world streams");

  for (std::uint64_t value = 0; value < 1'024; ++value) {
    const Seed sample{value};
    const auto station = generate_origin_station(sample);
    const auto system = generate_origin_system(sample);
    const auto home =
        find_local_system_planet(system, station.orbit.host_planet);
    check(validate_local_system(system) && home &&
              system.kind == LocalSystemKind::origin_home &&
              system.planets.front().descriptor.id ==
                  station.orbit.host_planet &&
              is_tutorial_safe_home_planet((*home)->descriptor) &&
              station.orbit.radius_kilometres >=
                  static_cast<std::uint64_t>((*home)->descriptor.radius.value) +
                      kOriginStationMinimumAltitudeKilometres &&
              station.orbit.radius_kilometres <=
                  static_cast<std::uint64_t>((*home)->descriptor.radius.value) +
                      kOriginStationMaximumAltitudeKilometres &&
              station.orbit.period_ticks >= kOriginStationMinimumPeriodTicks &&
              station.orbit.period_ticks <= kOriginStationMaximumPeriodTicks,
          "every sampled universe must generate one bounded tutorial home and "
          "station orbit");
    if (!home)
      continue;
    const auto at_epoch = resolve_origin_station_ephemeris(
        system, station, {.tick = 0, .sub_tick_fraction = 0.0});
    const auto one_period = resolve_origin_station_ephemeris(
        system, station,
        {.tick = station.orbit.period_ticks, .sub_tick_fraction = 0.0});
    const auto at_tick_limit = resolve_origin_station_ephemeris(
        system, station,
        {.tick = std::numeric_limits<SimulationTick>::max(),
         .sub_tick_fraction = 0.0});
    check(at_epoch && one_period && at_tick_limit &&
              at_epoch->host_relative_position ==
                  one_period->host_relative_position &&
              at_epoch->host_relative_velocity ==
                  one_period->host_relative_velocity,
          "station ephemerides must repeat exactly and remain finite at tick "
          "extremes");
  }

  const auto station = generate_origin_station(universe);
  const auto origin_system = generate_origin_system(universe);
  auto wrong_kind = origin_system;
  wrong_kind.kind = LocalSystemKind::procedural;
  auto unknown_kind = origin_system;
  unknown_kind.kind = static_cast<LocalSystemKind>(255);
  const auto station_with_orbit = [&](OriginStationOrbit orbit) {
    return OriginStationDescriptor{
        .universe_seed = station.universe_seed,
        .home_system_seed = station.home_system_seed,
        .station_seed = station.station_seed,
        .id = station.id,
        .orbit = orbit,
    };
  };
  auto wrong_host_orbit = station.orbit;
  wrong_host_orbit.host_planet.value ^= 1U;
  auto zero_period_orbit = station.orbit;
  zero_period_orbit.period_ticks = 0;
  auto overflow_period_orbit = station.orbit;
  overflow_period_orbit.period_ticks =
      std::numeric_limits<SimulationTick>::max();
  check(
      !resolve_origin_station_ephemeris(
          wrong_kind, station, {.tick = 0, .sub_tick_fraction = 0.0}) &&
          !validate_local_system(unknown_kind) &&
          !resolve_origin_station_ephemeris(
              origin_system, station_with_orbit(wrong_host_orbit),
              {.tick = 0, .sub_tick_fraction = 0.0}) &&
          !resolve_origin_station_ephemeris(
              origin_system, station_with_orbit(zero_period_orbit),
              {.tick = 0, .sub_tick_fraction = 0.0}) &&
          !resolve_origin_station_ephemeris(
              origin_system, station_with_orbit(overflow_period_orbit),
              {.tick = 0, .sub_tick_fraction = 0.0}) &&
          !resolve_origin_station_ephemeris(
              origin_system, station,
              {.tick = 0,
               .sub_tick_fraction = std::numeric_limits<double>::quiet_NaN()}),
      "station ephemeris resolution must reject wrong systems, hosts, periods, "
      "and non-finite time");

  const auto& safe_home = origin_system.planets.front().descriptor;
  std::vector<LocalSystemPlanet> unsafe_planets;
  unsafe_planets.reserve(origin_system.planets.size());
  unsafe_planets.push_back(
      {PlanetDescriptor{
           .seed = safe_home.seed,
           .id = safe_home.id,
           .display_name = safe_home.display_name,
           .radius = PlanetRadiusKm{kOriginHomeMinimumRadiusKilometres - 1U},
           .surface_gravity = safe_home.surface_gravity,
           .atmosphere_class = safe_home.atmosphere_class,
           .atmosphere_pressure = safe_home.atmosphere_pressure,
           .terrain_character = safe_home.terrain_character,
           .water_coverage = safe_home.water_coverage,
           .palette = safe_home.palette},
       origin_system.planets.front().orbit});
  for (std::size_t index = 1; index < origin_system.planets.size(); ++index) {
    unsafe_planets.push_back(origin_system.planets[index]);
  }
  auto unsafe_system = origin_system;
  unsafe_system.planets = std::move(unsafe_planets);
  check(!validate_local_system(unsafe_system),
        "an impossible tutorial-home safety descriptor must be rejected");
}

auto origin_onboarding_contract() -> void {
  const auto station = generate_origin_station(Seed{42});
  const auto binding = generate_home_signal_contract(Seed{42});
  auto state = initial_origin_onboarding_state(station);
  check(
      state == OriginOnboardingState{station.id, binding.contract,
                                     binding.target,
                                     OriginLocation::docked_at_origin,
                                     FirstObjectiveStatus::offered},
      "a zero-discovery universe must begin docked with one offered objective");

  const auto unchanged_on_failure = [&](OriginOnboardingCommand command,
                                        OriginOnboardingError error) {
    const auto before = state;
    const auto result = advance_origin_onboarding(state, command);
    check(!result && result.error() == error && state == before,
          "a rejected onboarding transition must leave state unchanged");
  };

  unchanged_on_failure(OriginOnboardingCommand::launch,
                       OriginOnboardingError::invalid_transition);
  unchanged_on_failure(OriginOnboardingCommand::complete_first_objective,
                       OriginOnboardingError::invalid_transition);
  unchanged_on_failure(OriginOnboardingCommand::dock_at_origin,
                       OriginOnboardingError::invalid_transition);

  check(advance_origin_onboarding(
            state, OriginOnboardingCommand::accept_first_objective)
            .has_value() &&
            state.location == OriginLocation::docked_at_origin &&
            state.first_objective == FirstObjectiveStatus::active,
        "accepting the bounded offer must arm launch without leaving the station");
  unchanged_on_failure(OriginOnboardingCommand::accept_first_objective,
                       OriginOnboardingError::invalid_transition);
  check(advance_origin_onboarding(state, OriginOnboardingCommand::launch)
            .has_value() &&
            state.location == OriginLocation::in_flight &&
            state.first_objective == FirstObjectiveStatus::active,
        "launch must enter flight with the first objective active");
  unchanged_on_failure(OriginOnboardingCommand::launch,
                       OriginOnboardingError::invalid_transition);
  unchanged_on_failure(OriginOnboardingCommand::dock_at_origin,
                       OriginOnboardingError::invalid_transition);
  check(advance_origin_onboarding(
            state, OriginOnboardingCommand::complete_first_objective)
            .has_value() &&
            state.location == OriginLocation::in_flight &&
            state.first_objective == FirstObjectiveStatus::completed,
        "objective completion must remain in flight until return");
  check(
      advance_origin_onboarding(state, OriginOnboardingCommand::dock_at_origin)
              .has_value() &&
          state.location == OriginLocation::docked_at_origin &&
          state.first_objective == FirstObjectiveStatus::returned,
      "return must finish docked at the stable origin station");
  unchanged_on_failure(OriginOnboardingCommand::dock_at_origin,
                       OriginOnboardingError::invalid_transition);
  unchanged_on_failure(OriginOnboardingCommand::launch,
                       OriginOnboardingError::invalid_transition);
  check(advance_origin_onboarding(state, OriginOnboardingCommand::turn_in)
                .has_value() &&
            state.first_objective == FirstObjectiveStatus::turned_in,
        "turn-in must remain distinct from physical return");
  unchanged_on_failure(OriginOnboardingCommand::turn_in,
                       OriginOnboardingError::invalid_transition);

  OriginOnboardingState malformed{station.id, binding.contract, binding.target,
                                  OriginLocation::in_flight,
                                  FirstObjectiveStatus::offered};
  const auto malformed_before = malformed;
  const auto malformed_result = advance_origin_onboarding(
      malformed, OriginOnboardingCommand::accept_first_objective);
  check(!malformed_result &&
            malformed_result.error() == OriginOnboardingError::invalid_state &&
            malformed == malformed_before,
        "impossible onboarding combinations must fail without mutation");

  malformed = OriginOnboardingState{
      station.id, binding.contract, binding.target,
      static_cast<OriginLocation>(255), FirstObjectiveStatus::active};
  const auto invalid_location_before = malformed;
  const auto invalid_location = advance_origin_onboarding(
      malformed, OriginOnboardingCommand::launch);
  check(!invalid_location &&
            invalid_location.error() == OriginOnboardingError::invalid_state &&
            malformed == invalid_location_before,
        "unknown onboarding locations must fail without mutation");

  state = initial_origin_onboarding_state(station);
  const auto invalid_command_before = state;
  const auto invalid_command = advance_origin_onboarding(
      state, static_cast<OriginOnboardingCommand>(255));
  check(!invalid_command &&
            invalid_command.error() ==
                OriginOnboardingError::invalid_transition &&
            state == invalid_command_before,
        "unknown onboarding commands must fail without mutation");
}

auto career_onboarding_contract() -> void {
  auto guided =
      initial_onboarding_progress(NewGameOnboardingChoice::guided);
  const auto skipped =
      initial_onboarding_progress(NewGameOnboardingChoice::skip);
  check(guided == OnboardingProgress{} &&
            guided.chapter == OnboardingChapter::contract_one &&
            skipped.state == OnboardingState::skipped &&
            !skipped.chapter,
        "Guided must begin at contract one while Skip has no tutorial chapter");

  const auto guided_access = resolve_onboarding_access(guided);
  const auto skipped_access = resolve_onboarding_access(skipped);
  check(guided_access && skipped_access &&
            guided_access->origin_station_known &&
            guided_access->home_planet_known &&
            guided_access->origin_system_chart_known &&
            !guided_access->first_jump_solution_available &&
            !guided_access->open_exploration_available &&
            skipped_access->origin_station_known &&
            skipped_access->home_planet_known &&
            skipped_access->origin_system_chart_known &&
            skipped_access->first_jump_solution_available &&
            skipped_access->open_exploration_available,
        "Guided and Skip must expose their exact bounded starting knowledge");

  const auto unchanged_on_failure = [&](OnboardingCommand command) {
    const auto before = guided;
    const auto result = advance_onboarding(guided, command);
    check(!result && result.error() == OnboardingError::invalid_transition &&
              guided == before,
          "out-of-order onboarding completion must not mutate progress");
  };
  unchanged_on_failure(OnboardingCommand::complete_contract_two);
  unchanged_on_failure(OnboardingCommand::complete_contract_three);
  check(advance_onboarding(guided,
                           OnboardingCommand::complete_contract_one) &&
            guided.chapter == OnboardingChapter::contract_two,
        "contract one completion must advance Guided to contract two");
  unchanged_on_failure(OnboardingCommand::complete_contract_one);
  check(advance_onboarding(guided,
                           OnboardingCommand::complete_contract_two) &&
            guided.chapter == OnboardingChapter::contract_three,
        "contract two completion must advance Guided to contract three");
  const auto contract_three_access = resolve_onboarding_access(guided);
  check(contract_three_access &&
            contract_three_access->first_jump_solution_available &&
            !contract_three_access->open_exploration_available,
        "contract three must expose one first-jump solution before open exploration");
  check(advance_onboarding(guided,
                           OnboardingCommand::complete_contract_three) &&
            guided.state == OnboardingState::completed && !guided.chapter,
        "contract three completion must atomically open exploration");
  const auto completed_access = resolve_onboarding_access(guided);
  check(completed_access && completed_access->open_exploration_available,
        "completed onboarding must retain the post-onboarding access baseline");
  const auto completed_before = guided;
  check(advance_onboarding(guided,
                           OnboardingCommand::complete_contract_three) ==
                std::unexpected{OnboardingError::invalid_transition} &&
            guided == completed_before,
        "completed onboarding must reject repeated completion without mutation");
  auto unknown_command_progress =
      initial_onboarding_progress(NewGameOnboardingChoice::guided);
  const auto unknown_command_before = unknown_command_progress;
  check(advance_onboarding(unknown_command_progress,
                           static_cast<OnboardingCommand>(255)) ==
                std::unexpected{OnboardingError::invalid_transition} &&
            unknown_command_progress == unknown_command_before,
        "unknown onboarding commands must fail without mutation");

  const OnboardingProgress invalid_guided{
      .state = OnboardingState::guided, .chapter = std::nullopt};
  const OnboardingProgress invalid_skipped{
      .state = OnboardingState::skipped,
      .chapter = OnboardingChapter::contract_one};
  const OnboardingProgress invalid_enum{
      .state = static_cast<OnboardingState>(255), .chapter = std::nullopt};
  const OnboardingProgress invalid_chapter{
      .state = OnboardingState::guided,
      .chapter = static_cast<OnboardingChapter>(255)};
  check(!validate_onboarding_progress(invalid_guided) &&
            !validate_onboarding_progress(invalid_skipped) &&
            !validate_onboarding_progress(invalid_enum) &&
            !validate_onboarding_progress(invalid_chapter) &&
            resolve_onboarding_access(invalid_skipped) ==
                std::unexpected{OnboardingError::invalid_state},
        "invalid state/chapter combinations and unknown enums must be rejected");

  const auto guided_document =
      make_new_game_document(Seed{42}, NewGameOnboardingChoice::guided);
  const auto skipped_document =
      make_new_game_document(Seed{42}, NewGameOnboardingChoice::skip);
  check(guided_document.recipe == skipped_document.recipe &&
            guided_document.state.intersystem_contract ==
                skipped_document.state.intersystem_contract &&
            skipped_document.state.onboarding == skipped &&
            skipped_document.state.discoveries.empty() &&
            skipped_document.state.world_deltas.empty() &&
            skipped_document.state.first_objective ==
                FirstObjectiveStatus::offered &&
            skipped_document.state.first_objective_contract.value != 0 &&
            skipped_document.state.first_objective_target.value != 0,
        "Skip must preserve generated truth and add no fabricated history");
  const auto encoded_skip = encode_save_document_json(skipped_document);
  check(encoded_skip && decode_save_document_json(*encoded_skip) ==
                            std::expected<SaveDocument, SaveSchemaError>{
                                skipped_document},
        "Skip must round-trip with a null chapter and unchanged generated truth");

  const auto invalid_choice = make_new_game_document(
      Seed{42}, static_cast<NewGameOnboardingChoice>(255));
  check(!validate_save_document(invalid_choice) &&
            validate_save_document(invalid_choice).error().path ==
                "$.state.onboarding",
        "unknown New Game onboarding choices must produce rejected state");

  auto invalid_document = skipped_document;
  invalid_document.state.onboarding.chapter =
      OnboardingChapter::contract_one;
  check(!validate_save_document(invalid_document) &&
            validate_save_document(invalid_document).error().path ==
                "$.state.onboarding",
        "save validation must reject skipped onboarding with tutorial progress");
  invalid_document = guided_document;
  invalid_document.state.onboarding = {
      .state = OnboardingState::completed, .chapter = std::nullopt};
  check(!validate_save_document(invalid_document) &&
            validate_save_document(invalid_document).error().path ==
                "$.state.onboarding.state",
        "completed onboarding must reject an unfinished authored contract");

  const auto invalid_acceptance = run_onboarding_acceptance(
      {.viewport = {0, 480}}, "unused-onboarding-checkpoint.json");
  check(!invalid_acceptance &&
            invalid_acceptance.error() ==
                OnboardingAcceptanceError::invalid_configuration,
        "the integrated onboarding runner must reject invalid dimensions before work");
  const auto wrong_contract_two =
      run_origin_system_contract_acceptance(guided_document, 1, 1);
  check(!wrong_contract_two &&
            wrong_contract_two.error() ==
                OriginSystemContractAcceptanceError::initialization_failure,
        "contract two acceptance must reject a contract-one starting document");
  const auto wrong_contract_three =
      run_intersystem_contract_acceptance(guided_document, 1, 1, false);
  check(!wrong_contract_three &&
            wrong_contract_three.error() ==
                IntersystemContractAcceptanceError::initialization_failure,
        "contract three acceptance must reject a contract-one starting document");
}

auto save_schema_contract() -> void {
  check(kSaveFormatVersion == 16 && kSaveApplication == "apsis-drift" &&
            kMaximumSaveDocumentBytes == (1U << 20U) &&
            kMaximumSaveApplicationVersionBytes == 64,
        "save format version 16 identity and bounds must remain stable");
  auto recipe = make_save_recipe(Seed{42});
  const auto binding = generate_home_signal_contract(Seed{42});
  const auto target = binding.target;
  const auto target_key = surface_signal_object_key(target);
  SaveDocument expected{
      .recipe = recipe,
      .state =
          SaveMutableState{
              .location = OriginLocation::in_flight,
              .first_objective = FirstObjectiveStatus::active,
              .first_objective_contract = binding.contract,
              .first_objective_target = target,
              .flight =
                  PlanetaryFlightState{
                      .tick = 1200,
                      .planet = recipe.active_planet,
                      .pose = {{0.25, -0.5, 10'000.0}, 0.75},
                      .velocity = {125.5, -20.25, -5.0},
                      .clearance_metres = 9'000.0,
                      .mode = FlightMode::manual,
                      .controls = {.forward = true, .turn_right = true},
                      .regime = FlightRegime::atmospheric,
                      .last_transition =
                          FlightRegimeTransition{FlightRegime::orbital,
                                                 FlightRegime::atmospheric,
                                                 1000},
                      .thermal = {},
                  },
              .system_flight = std::nullopt,
              .origin_station_flight = std::nullopt,
              .discoveries = {{target, 1100}},
              .world_deltas = {{target_key, SaveWorldDeltaKind::discovered,
                                1100}},
              .intersystem_contract = std::nullopt,
          },
  };
  check(recipe.origin_station.value == 0xce51e866ec4e032dULL &&
            recipe.active_planet.value == 0x435b7b7e8ce489e8ULL,
        "save recipes must regenerate the canonical station and planet IDs");
  check(validate_save_document(expected).has_value(),
        "the representative legacy Signal Run save must validate");

  const auto encoded = encode_save_document_json(expected);
  check(encoded &&
            encoded->find("\"format_version\": 16") != std::string::npos &&
            encoded->find(std::format("\"application_version\": \"{}\"",
                                      kApplicationVersion)) !=
                std::string::npos &&
            encoded->find("\"career_kind\": \"legacy_signal_run\"") !=
                std::string::npos &&
            encoded->find("\"onboarding\":") != std::string::npos &&
            encoded->find("\"state\": \"guided\"") != std::string::npos &&
            encoded->find("\"chapter\": \"contract_one\"") != std::string::npos,
        "the current encoder must write canonical format-16 onboarding state "
        "with writer "
        "provenance");
  const std::string fixture = encoded.value_or("{}");
  const auto decoded = decode_save_document_json(fixture);
  check(decoded && *decoded == expected,
        "the canonical save must decode to the complete semantic state");
  if (encoded) {
    const auto round_trip = decode_save_document_json(*encoded);
    check(round_trip && *round_trip == expected &&
              encode_save_document_json(*round_trip) == encoded,
          "save encode/decode/re-encode must preserve canonical format-16 "
          "state exactly");
  }
  for (const auto chapter :
       {OnboardingChapter::contract_one, OnboardingChapter::contract_two,
        OnboardingChapter::contract_three}) {
    auto chapter_document = expected;
    chapter_document.state.onboarding.chapter = chapter;
    const auto chapter_json = encode_save_document_json(chapter_document);
    check(chapter_json && decode_save_document_json(*chapter_json) ==
                              std::expected<SaveDocument, SaveSchemaError>{
                                  chapter_document},
          "every Guided chapter must survive a canonical format-16 round trip");
  }

  auto unknown = fixture;
  unknown.insert(2, "  \"future_optional\": {\"note\": true},\n");
  unknown = replace_once(
      std::move(unknown), "    \"universe_seed\": \"42\",",
      "    \"universe_seed\": \"42\",\n    \"future_recipe_note\": 7,");
  const auto ignored = decode_save_document_json(unknown);
  check(ignored && *ignored == expected &&
            encode_save_document_json(*ignored) == encoded,
        "unknown optional fields must be ignored and discarded on rewrite");

  const auto expect_semantic_decode_error =
      [&](std::string text, SaveSchemaErrorCode code, std::string_view path,
          const char* message) {
        const auto result = decode_save_document_json(text);
        check(!result && result.error().code == code &&
                  result.error().path == path,
              message);
      };
  expect_semantic_decode_error(
      replace_once(fixture,
                   std::format("\"target_signal_id\": \"{}\"", target_key),
                   "\"target_signal_id\": \"signal-0000000000000001\""),
      SaveSchemaErrorCode::identity_mismatch,
      "$.state.first_objective.target_signal_id",
      "decode must reject an objective outside the generated signal catalog");
  expect_semantic_decode_error(
      replace_once(fixture, std::format("\"signal_id\": \"{}\"", target_key),
                   "\"signal_id\": \"signal-0000000000000001\""),
      SaveSchemaErrorCode::identity_mismatch,
      "$.state.discoveries[0].signal_id",
      "decode must reject an indexed discovery outside the generated catalog");
  expect_semantic_decode_error(
      replace_once(fixture, std::format("\"object_key\": \"{}\"", target_key),
                   "\"object_key\": \"signal-0000000000000001\""),
      SaveSchemaErrorCode::identity_mismatch,
      "$.state.world_deltas[0].object_key",
      "decode must reject an indexed delta outside the generated catalog");

  const auto expect_decode_error = [&](std::string text,
                                       SaveSchemaErrorCode code,
                                       const char* message) {
    const auto result = decode_save_document_json(text);
    check(!result && result.error().code == code, message);
  };
  expect_decode_error("{", SaveSchemaErrorCode::malformed_json,
                      "malformed JSON must be rejected");
  expect_decode_error(std::string(kMaximumSaveDocumentBytes + 1U, ' '),
                      SaveSchemaErrorCode::document_too_large,
                      "oversized save input must be rejected before parsing");
  expect_decode_error(
      replace_once(fixture, "  \"application\": \"apsis-drift\",\n",
                   "  \"application\": \"apsis-drift\",\n"
                   "  \"application\": \"apsis-drift\",\n"),
      SaveSchemaErrorCode::duplicate_key,
      "duplicate JSON object keys must be rejected");
  expect_decode_error(
      replace_once(fixture, "\"format_version\": 16", "\"format_version\": 1"),
      SaveSchemaErrorCode::unsupported_alpha_format_version,
      "format-1 alpha saves must be rejected explicitly");
  expect_decode_error(
      replace_once(fixture, "\"format_version\": 16", "\"format_version\": 10"),
      SaveSchemaErrorCode::unsupported_alpha_format_version,
      "format-10 alpha saves must be rejected explicitly");
  expect_decode_error(
      replace_once(fixture, "\"format_version\": 16", "\"format_version\": 11"),
      SaveSchemaErrorCode::unsupported_alpha_format_version,
      "format-11 alpha saves must be rejected explicitly");
  expect_decode_error(
      replace_once(fixture, "\"format_version\": 16", "\"format_version\": 12"),
      SaveSchemaErrorCode::unsupported_alpha_format_version,
      "format-12 alpha saves must be rejected explicitly");
  expect_decode_error(
      replace_once(fixture, "\"format_version\": 16", "\"format_version\": 13"),
      SaveSchemaErrorCode::unsupported_alpha_format_version,
      "format-13 alpha saves must be rejected explicitly");
  expect_decode_error(
      replace_once(fixture, "\"format_version\": 16", "\"format_version\": 14"),
      SaveSchemaErrorCode::unsupported_alpha_format_version,
      "format-14 alpha saves must be rejected explicitly");
  expect_decode_error(
      "{\"application\":\"apsis-drift\",\"format_version\":10}",
      SaveSchemaErrorCode::unsupported_alpha_format_version,
      "old alpha formats must be identified before nested fields are read");
  expect_decode_error(
      replace_once(fixture, "\"format_version\": 16", "\"format_version\": 15"),
      SaveSchemaErrorCode::unsupported_alpha_format_version,
      "format-16 alpha saves must be rejected explicitly");
  expect_decode_error(
      replace_once(fixture, "\"format_version\": 16", "\"format_version\": 17"),
      SaveSchemaErrorCode::unsupported_format_version,
      "future save versions must be rejected explicitly");
  const auto future_with_provenance = decode_save_document_json(replace_once(
      fixture, "\"format_version\": 16", "\"format_version\": 17"));
  check(!future_with_provenance &&
            future_with_provenance.error().detail.find(kApplicationVersion) !=
                std::string::npos,
        "unknown newer saves must identify valid writer provenance");
  expect_decode_error(
      "{\"application\":\"apsis-drift\",\"format_version\":17}",
      SaveSchemaErrorCode::unsupported_format_version,
      "future formats must be identified before current fields are read");
  expect_decode_error(
      replace_once(fixture, "\"format_version\": 16",
                   "\"format_version\": \"16\""),
      SaveSchemaErrorCode::invalid_type,
      "schema integers with the wrong JSON type must be rejected");
  expect_decode_error(replace_once(fixture, "\"application\": \"apsis-drift\"",
                                   "\"application\": \"another-game\""),
                      SaveSchemaErrorCode::invalid_value,
                      "foreign application saves must be rejected");
  expect_decode_error(replace_once(fixture, "\"application_version\"",
                                   "\"missing_application_version\""),
                      SaveSchemaErrorCode::missing_field,
                      "format 16 must require writer-version provenance");
  expect_decode_error(
      replace_once(fixture,
                   std::format("\"application_version\": \"{}\"",
                               kApplicationVersion),
                   "\"application_version\": \"\""),
      SaveSchemaErrorCode::invalid_value,
      "empty writer-version provenance must be rejected");
  expect_decode_error(
      replace_once(fixture,
                   std::format("\"application_version\": \"{}\"",
                               kApplicationVersion),
                   std::format("\"application_version\": \"{}\"",
                               std::string(
                                   kMaximumSaveApplicationVersionBytes + 1U,
                                   'v'))),
      SaveSchemaErrorCode::invalid_value,
      "oversized writer-version provenance must be rejected");
  expect_decode_error(
      replace_once(fixture,
                   std::format("\"application_version\": \"{}\"",
                               kApplicationVersion),
                   "\"application_version\": \"\\u001b\""),
      SaveSchemaErrorCode::invalid_value,
      "control characters in writer-version provenance must be rejected");
  expect_decode_error(
      replace_once(fixture, "\"seed_derivation\": 1",
                   "\"seed_derivation\": 2"),
      SaveSchemaErrorCode::incompatible_generator_version,
      "unsupported generator versions must be rejected explicitly");
  expect_decode_error(replace_once(fixture, "\"origin_home_planet\": 1",
                                   "\"origin_home_planet\": 2"),
                      SaveSchemaErrorCode::incompatible_generator_version,
                      "unsupported tutorial-home generator versions must be "
                      "rejected explicitly");
  expect_decode_error(
      replace_once(fixture, "\"local_sun\": 1", "\"local_sun\": 2"),
      SaveSchemaErrorCode::incompatible_generator_version,
      "unsupported local-sun geometry versions must be rejected explicitly");
  if (encoded) {
    expect_decode_error(
        replace_once(*encoded, "\"local_system\": 1",
                     "\"local_system\": 2"),
        SaveSchemaErrorCode::incompatible_generator_version,
        "unsupported local-system versions must be rejected explicitly");
    expect_decode_error(
        replace_once(*encoded, "\"analytic_ephemeris\": 1",
                     "\"analytic_ephemeris\": 2"),
        SaveSchemaErrorCode::incompatible_generator_version,
        "unsupported ephemeris versions must be rejected explicitly");
    expect_decode_error(
        replace_once(*encoded, "\"intersystem_contract\": 4",
                     "\"intersystem_contract\": 5"),
        SaveSchemaErrorCode::incompatible_generator_version,
        "unsupported first-contract versions must be rejected explicitly");
    expect_decode_error(
        replace_once(*encoded, "\"intersystem_jump\": 4",
                     "\"intersystem_jump\": 5"),
        SaveSchemaErrorCode::incompatible_generator_version,
        "unsupported Assisted-jump versions must be rejected explicitly");
  }
  expect_decode_error(
      replace_once(fixture, "station-ce51e866ec4e032d",
                   "station-ce51e866ec4e032e"),
      SaveSchemaErrorCode::identity_mismatch,
      "regenerated station identity mismatches must be rejected");
  expect_decode_error(
      replace_once(fixture,
                   "\"station_host_planet_id\": \"planet-435b7b7e8ce489e8\"",
                   "\"station_host_planet_id\": \"planet-435b7b7e8ce489e9\""),
      SaveSchemaErrorCode::identity_mismatch,
      "a saved station host outside the regenerated home role must be "
      "rejected");
  expect_decode_error(
      replace_once(fixture,
                   std::format("\"period_ticks\": \"{}\"",
                               recipe.station_orbit.period_ticks),
                   "\"period_ticks\": \"0\""),
      SaveSchemaErrorCode::identity_mismatch,
      "a zero or altered saved station period must be rejected");
  expect_decode_error(
      replace_once(fixture, "planet-435b7b7e8ce489e8",
                   "planet-435B7b7e8ce489e8"),
      SaveSchemaErrorCode::invalid_value,
      "stable identifiers must reject uppercase hexadecimal digits");
  expect_decode_error(
      replace_once(fixture, "\"universe_seed\": \"42\"",
                   "\"universe_seed\": \"042\""),
      SaveSchemaErrorCode::invalid_value,
      "non-canonical unsigned strings must be rejected");
  expect_decode_error(
      replace_once(fixture, "\"latitude_radians\": \"0.25\"",
                   "\"latitude_radians\": \"nan\""),
      SaveSchemaErrorCode::invalid_value,
      "non-finite flight values must be rejected");
  auto impossible_orbit = replace_once(
      fixture, "\"altitude_metres\": \"10000\"",
      "\"altitude_metres\": \"16\"");
  impossible_orbit = replace_once(
      std::move(impossible_orbit), "\"clearance_metres\": \"9000\"",
      "\"clearance_metres\": \"16\"");
  impossible_orbit = replace_once(
      std::move(impossible_orbit), "\"regime\": \"atmospheric\"",
      "\"regime\": \"orbital\"");
  expect_decode_error(
      std::move(impossible_orbit), SaveSchemaErrorCode::invalid_state,
      "save loading must reject a near-surface state relabeled as orbital");
  expect_decode_error(
      replace_once(fixture, "\"location\": \"in_flight\"",
                   "\"location\": \"somewhere\""),
      SaveSchemaErrorCode::invalid_value,
      "unknown state enums must be rejected");
  expect_decode_error(
      replace_once(fixture, "\"kind\": \"discovered\"",
                   "\"kind\": \"future_kind\""),
      SaveSchemaErrorCode::invalid_value,
      "unknown world-delta kinds must be rejected");
  expect_semantic_decode_error(
      replace_once(fixture, "\"state\": \"guided\"",
                   "\"state\": \"future_state\""),
      SaveSchemaErrorCode::invalid_value, "$.state.onboarding.state",
      "unknown onboarding states must be rejected before application");
  expect_semantic_decode_error(
      replace_once(fixture, "\"chapter\": \"contract_one\"",
                   "\"chapter\": \"future_chapter\""),
      SaveSchemaErrorCode::invalid_value, "$.state.onboarding.chapter",
      "unknown onboarding chapters must be rejected before application");
  expect_semantic_decode_error(
      replace_once(fixture, "\"chapter\": \"contract_one\"",
                   "\"chapter\": null"),
      SaveSchemaErrorCode::invalid_state, "$.state.onboarding",
      "Guided onboarding must retain one active authored chapter");
  auto skipped_with_progress = replace_once(
      fixture, "\"state\": \"guided\"", "\"state\": \"skipped\"");
  expect_semantic_decode_error(
      std::move(skipped_with_progress), SaveSchemaErrorCode::invalid_state,
      "$.state.onboarding",
      "Skip must reject a persisted tutorial chapter");
  expect_semantic_decode_error(
      replace_once(fixture, "\"chapter\": \"contract_one\"",
                   "\"chapter\": 1"),
      SaveSchemaErrorCode::invalid_type, "$.state.onboarding.chapter",
      "onboarding chapters must reject the wrong JSON type");

  constexpr std::array required_sections{
      "\"application\"", "\"application_version\"",
      "\"format_version\"", "\"recipe\"", "\"state\"",
      "\"generator_versions\"", "\"local_sun\"",
      "\"onboarding\"", "\"first_objective\"", "\"flight\"", "\"discoveries\"",
      "\"world_deltas\"",
  };
  for (const auto section : required_sections) {
    const auto renamed = replace_once(fixture, section, "\"missing_field\"");
    const auto result = decode_save_document_json(renamed);
    check(!result && result.error().code == SaveSchemaErrorCode::missing_field,
          "every required save section must be present");
  }

  auto invalid = expected;
  invalid.state.location = OriginLocation::docked_at_origin;
  check(encode_save_document_json(invalid) ==
            std::unexpected{SaveSchemaError{
                SaveSchemaErrorCode::invalid_state, "$.state.flight",
                "a docked save cannot contain active planetary flight state"}},
        "invalid state must be rejected before encoding");
  invalid = expected;
  invalid.state.flight->planet.value ^= 1U;
  auto invalid_result = encode_save_document_json(invalid);
  check(!invalid_result && invalid_result.error().code ==
                               SaveSchemaErrorCode::identity_mismatch,
        "flight state must refer to the regenerated active planet");
  invalid = expected;
  invalid.state.flight->velocity.east_metres_per_second = 501.0;
  invalid_result = encode_save_document_json(invalid);
  check(!invalid_result &&
            invalid_result.error().code == SaveSchemaErrorCode::invalid_state,
        "saved velocity must remain inside the active regime bounds");
  invalid = expected;
  invalid.state.flight->pose.position.altitude_metres =
      kMinimumFlightClearanceMetres;
  invalid.state.flight->clearance_metres = kMinimumFlightClearanceMetres;
  invalid.state.flight->regime = FlightRegime::orbital;
  invalid.state.flight->last_transition.reset();
  invalid_result = encode_save_document_json(invalid);
  check(!invalid_result &&
            invalid_result.error().code == SaveSchemaErrorCode::invalid_state,
        "save encoding must reject a near-surface state relabeled as orbital");
  invalid = expected;
  invalid.state.discoveries.push_back(invalid.state.discoveries.front());
  invalid_result = encode_save_document_json(invalid);
  check(!invalid_result &&
            invalid_result.error().code == SaveSchemaErrorCode::invalid_state,
        "duplicate discoveries must be rejected");
  invalid = expected;
  invalid.state.world_deltas.front().object_key = "Bad Key";
  invalid_result = encode_save_document_json(invalid);
  check(!invalid_result &&
            invalid_result.error().code == SaveSchemaErrorCode::invalid_state,
        "invalid world-delta object keys must be rejected");
  invalid = expected;
  invalid.state.discoveries.clear();
  invalid_result = encode_save_document_json(invalid);
  check(!invalid_result &&
            invalid_result.error().path == "$.state.discoveries",
        "an accepted objective must retain its generated target discovery");
  invalid = expected;
  invalid.state.world_deltas.front().kind = SaveWorldDeltaKind::collected;
  invalid_result = encode_save_document_json(invalid);
  check(!invalid_result && invalid_result.error().path ==
                               "$.state.first_objective.status",
        "an active objective must reject a terminal collected delta");
  invalid = expected;
  invalid.state.first_objective = FirstObjectiveStatus::completed;
  invalid_result = encode_save_document_json(invalid);
  check(!invalid_result &&
            invalid_result.error().path == "$.state.world_deltas",
        "a completed objective must require a collected target delta");
  invalid.state.world_deltas.front().kind = SaveWorldDeltaKind::collected;
  check(encode_save_document_json(invalid).has_value(),
        "a completed objective with its collected target must validate");
  invalid = expected;
  invalid.state.discoveries.front().tick = invalid.state.flight->tick + 1U;
  invalid_result = encode_save_document_json(invalid);
  check(!invalid_result && invalid_result.error().path ==
                               "$.state.discoveries[0].tick",
        "an in-flight discovery cannot come from a future tick");
  invalid = expected;
  invalid.state.world_deltas.front().tick = invalid.state.flight->tick + 1U;
  invalid_result = encode_save_document_json(invalid);
  check(!invalid_result && invalid_result.error().path ==
                               "$.state.world_deltas[0].tick",
        "an in-flight world delta cannot come from a future tick");
  invalid = expected;
  invalid.state.world_deltas.front().tick =
      invalid.state.discoveries.front().tick - 1U;
  invalid_result = encode_save_document_json(invalid);
  check(!invalid_result && invalid_result.error().path ==
                               "$.state.world_deltas[0].tick",
        "a signal delta cannot precede discovery of the same signal");
}

auto save_file_contract() -> void {
  const auto zero = make_new_game_document(Seed{0});
  const auto zero_again = make_new_game_document(Seed{0});
  const auto maximum =
      make_new_game_document(Seed{std::numeric_limits<std::uint64_t>::max()});
  const auto skipped =
      make_new_game_document(Seed{0}, NewGameOnboardingChoice::skip);
  check(zero == zero_again && validate_save_document(zero).has_value() &&
            validate_save_document(maximum).has_value() &&
            validate_save_document(skipped).has_value(),
        "new-game profiles must accept the complete unsigned seed range and reproduce deterministically");
  check(zero.state.location == OriginLocation::docked_at_origin &&
            zero.state.first_objective == FirstObjectiveStatus::offered &&
            !zero.state.flight && zero.state.discoveries.empty() &&
            zero.state.world_deltas.empty() &&
            zero.state.onboarding == OnboardingProgress{} &&
            skipped.recipe == zero.recipe &&
            skipped.state.onboarding.state == OnboardingState::skipped &&
            !skipped.state.onboarding.chapter &&
            skipped.state.discoveries.empty() &&
            skipped.state.world_deltas.empty(),
        "a new-game profile must begin docked without mutable generated-world state");

  TemporaryDirectory temporary;
  const auto save_path = temporary.path() / "profile.json";
  const auto save_as_path = temporary.path() / "profile-copy.json";
  check(write_save_file_atomically(save_path, zero).has_value(),
        "a valid new-game profile must save atomically");
  const auto loaded = load_save_file(save_path);
  check(loaded && *loaded == zero,
        "a saved profile must round-trip through the bounded file loader");
  const auto permissions = std::filesystem::status(save_path).permissions();
  check((permissions & (std::filesystem::perms::group_all |
                        std::filesystem::perms::others_all)) ==
            std::filesystem::perms::none,
        "new temporary save files must not grant group or other access");
  if (loaded) {
    check(write_save_file_atomically(save_as_path, *loaded).has_value() &&
              load_save_file(save_as_path) == loaded,
          "an explicitly different destination must support save-as without changing state");
  }

  const auto representative = make_new_game_document(Seed{42});
  const bool representative_written =
      write_save_file_atomically(save_as_path, representative).has_value();
  const auto representative_loaded = load_save_file(save_as_path);
  check(representative_written && representative_loaded &&
            *representative_loaded == representative,
        "atomic replacement must preserve a complete authoritative profile");

  const auto replacement = make_new_game_document(Seed{42});
  const auto interrupted = detail::write_save_file_atomically_for_test(
      save_path, replacement,
      detail::AtomicSaveTestInterruption::before_replace);
  check(!interrupted &&
            interrupted.error().code == SaveFileErrorCode::replace_failed &&
            load_save_file(save_path) == loaded,
        "an interruption before replacement must leave the prior save loadable");
  std::size_t temporary_files{};
  for (const auto& entry :
       std::filesystem::directory_iterator{temporary.path()}) {
    if (entry.path().filename().string().starts_with(".profile.json.tmp.")) {
      ++temporary_files;
    }
  }
  check(temporary_files == 0,
        "failed atomic writes must clean their temporary save files");

  auto invalid_document = replacement;
  invalid_document.recipe.generator_versions.seed_derivation += 1;
  const auto rejected_write =
      write_save_file_atomically(save_path, invalid_document);
  check(!rejected_write &&
            rejected_write.error().code == SaveFileErrorCode::invalid_document &&
            rejected_write.error().schema_error &&
            load_save_file(save_path) == loaded,
        "invalid authoritative state must fail before touching the destination");

  const auto missing_path = temporary.path() / "missing.json";
  const auto missing = load_save_file(missing_path);
  check(!missing && missing.error().code == SaveFileErrorCode::not_found,
        "a missing save must have a distinct actionable file error");
  const auto empty_path = load_save_file({});
  check(!empty_path &&
            empty_path.error().code == SaveFileErrorCode::invalid_path,
        "an empty load path must be rejected before opening a descriptor");
  const auto unavailable = write_save_file_atomically(
      temporary.path() / "missing" / "profile.json", replacement);
  check(!unavailable && unavailable.error().code ==
                            SaveFileErrorCode::temporary_file_failed,
        "a destination in a missing directory must fail without creating directories implicitly");

  const auto malformed_path = temporary.path() / "malformed.json";
  check(write_test_file(malformed_path, "{"),
        "the malformed save fixture must be writable");
  auto live = zero;
  const auto malformed = load_save_file(malformed_path);
  if (malformed) live = *malformed;
  check(!malformed &&
            malformed.error().code == SaveFileErrorCode::invalid_document &&
            malformed.error().schema_error && live == zero,
        "a malformed load must expose schema context without mutating live state");
  if (!malformed) {
    const auto message = save_file_error_message(malformed.error());
    check(message.find(malformed_path.string()) != std::string::npos &&
              message.find("$") != std::string::npos,
          "save diagnostics must include the path and schema location");
  }

  const auto unsupported_path = temporary.path() / "format-10.json";
  constexpr std::string_view unsupported_contents{
      "{\"application\":\"apsis-drift\",\"format_version\":10}\n"};
  check(write_test_file(unsupported_path, unsupported_contents),
        "the unsupported alpha save fixture must be writable");
  live = zero;
  const auto unsupported = load_save_file(unsupported_path);
  if (unsupported) live = *unsupported;
  check(!unsupported &&
            unsupported.error().code == SaveFileErrorCode::invalid_document &&
            unsupported.error().schema_error &&
            unsupported.error().schema_error->code ==
                SaveSchemaErrorCode::unsupported_alpha_format_version &&
            live == zero &&
            read_test_file(unsupported_path) == unsupported_contents,
        "an unsupported alpha save must not replace live state or modify its source file");

  const auto inconsistent_path = temporary.path() / "inconsistent.json";
  const auto signal_document = make_legacy_signal_run_document(Seed{42});
  const auto signal_json = encode_save_document_json(signal_document);
  const auto signal_target =
      surface_signal_object_key(signal_document.state.first_objective_target);
  const auto inconsistent_fixture =
      signal_json
          ? replace_once(
                *signal_json,
                std::format("\"target_signal_id\": \"{}\"", signal_target),
                "\"target_signal_id\": \"signal-0000000000000001\"")
          : std::string{};
  check(write_test_file(inconsistent_path, inconsistent_fixture),
        "the inconsistent semantic save fixture must be writable");
  const auto inconsistent = load_save_file(inconsistent_path);
  check(!inconsistent &&
            inconsistent.error().code == SaveFileErrorCode::invalid_document &&
            inconsistent.error().schema_error &&
            inconsistent.error().schema_error->code ==
                SaveSchemaErrorCode::identity_mismatch &&
            inconsistent.error().schema_error->path ==
                "$.state.first_objective.target_signal_id",
        "file loading must reject inconsistent Signal Run state before commit");
  if (!inconsistent) {
    const auto message = save_file_error_message(inconsistent.error());
    check(message.find(inconsistent_path.string()) != std::string::npos &&
              message.find("$.state.first_objective.target_signal_id") !=
                  std::string::npos,
          "semantic load diagnostics must include the file and indexed schema path");
  }

  const auto boundary_path = temporary.path() / "boundary.json";
  const bool exact_written = write_test_file(
      boundary_path, std::string(kMaximumSaveDocumentBytes, ' '));
  const auto exact_boundary = load_save_file(boundary_path);
  check(exact_written && !exact_boundary &&
            exact_boundary.error().code == SaveFileErrorCode::invalid_document,
        "an exactly maximum-sized invalid document must reach schema validation");
  const bool oversized_written = write_test_file(
      boundary_path, std::string(kMaximumSaveDocumentBytes + 1U, ' '));
  const auto oversized = load_save_file(boundary_path);
  check(oversized_written && !oversized &&
            oversized.error().code == SaveFileErrorCode::document_too_large,
        "a save one byte beyond the format bound must fail before decoding");
}

auto profile_catalog_contract() -> void {
  fake_entropy_requests = 0;
  const auto injected_seed = request_new_game_seed(fake_seed_entropy);
  check(injected_seed && *injected_seed == 0x0123456789abcdefULL &&
            fake_entropy_requests == 1 && !request_new_game_seed(nullptr),
        "New Game entropy must be injectable, called once per request, and fail closed without a source");
  check(parse_new_game_seed("0") == 0U &&
            parse_new_game_seed("18446744073709551615") ==
                std::numeric_limits<std::uint64_t>::max(),
        "New Game seed parsing must accept the complete canonical unsigned range");
  for (const std::string_view invalid :
       {"", "+1", "-1", "00", "01", "18446744073709551616", "1x"}) {
    check(!parse_new_game_seed(invalid),
          "New Game seed parsing must reject non-canonical or overflowing values");
  }

  TemporaryDirectory temporary;
  const auto xdg = resolve_profile_directory(
      temporary.path().string(), std::string{"/unused"});
  check(xdg && *xdg == temporary.path() / "apsis-drift" / "profiles",
        "an absolute XDG data home must own the profile catalog");
  const auto home = resolve_profile_directory(
      std::string{"relative-xdg"}, temporary.path().string());
  check(home && *home == temporary.path() / ".local" / "share" /
                             "apsis-drift" / "profiles",
        "a non-absolute XDG data home must fall back to the absolute HOME path");
  check(!resolve_profile_directory(std::string{}, std::string{"relative-home"}),
        "profile directory resolution must reject a relative HOME fallback");
  if (!xdg) return;

  const NewGameOptions assisted_options{
      .universe_seed = Seed{42},
      .penalty_mode = IntersystemRuleProfile::assisted,
      .onboarding = NewGameOnboardingChoice::guided,
  };
  const auto assisted_document = make_new_game_document(assisted_options);
  const auto assisted = create_catalog_profile(*xdg, assisted_document);
  check(assisted && assisted->metadata.id == ProfileId{1} &&
            assisted->metadata.save_sequence == 1U &&
            assisted->metadata.universe_seed == Seed{42} &&
            assisted->metadata.penalty_mode ==
                IntersystemRuleProfile::assisted &&
            assisted->metadata.onboarding_state == OnboardingState::guided &&
            assisted->metadata.location ==
                ProfileLocation::docked_at_origin &&
            assisted->document == assisted_document,
        "catalog creation must persist an exact Guided Assisted career and its bounded summary");
  if (!assisted) return;

  auto snapshot = scan_profile_catalog(*xdg);
  check(snapshot.writable && !snapshot.overflow &&
            snapshot.entries.size() == 1U &&
            snapshot.continue_index == 0U &&
            snapshot.entries.front().activatable(),
        "a newly created catalog must expose its career as Continue");
  const auto loaded = load_catalog_profile(snapshot.entries.front());
  check(loaded && loaded->document == assisted_document &&
            loaded->metadata == assisted->metadata &&
            loaded->source_bytes == assisted->source_bytes,
        "catalog loading must revalidate and preserve the exact authoritative document");
  const auto scanned_assisted = snapshot.entries.front();
  check(write_test_file(assisted->path, assisted->source_bytes + "\n"),
        "the stale-entry fixture must preserve a valid document with changed bytes");
  const auto stale = load_catalog_profile(scanned_assisted);
  check(!stale && stale.error().code == ProfileCatalogErrorCode::stale_entry,
        "activation must reject a profile whose source bytes changed after cataloging");
  check(write_test_file(assisted->path, assisted->source_bytes),
        "the stale-entry fixture must restore the catalog profile exactly");

  const NewGameOptions advanced_options{
      .universe_seed = Seed{42},
      .penalty_mode = IntersystemRuleProfile::pilot,
      .onboarding = NewGameOnboardingChoice::skip,
  };
  const auto advanced_document = make_new_game_document(advanced_options);
  const auto advanced = create_catalog_profile(*xdg, advanced_document);
  check(advanced && advanced->metadata.id == ProfileId{2} &&
            advanced->metadata.save_sequence == 2U &&
            advanced->metadata.penalty_mode == IntersystemRuleProfile::pilot &&
            advanced->metadata.onboarding_state == OnboardingState::skipped &&
            advanced->document.recipe == assisted_document.recipe &&
            advanced->document.state.onboarding !=
                assisted_document.state.onboarding,
        "the same universe seed must retain generated identity while New Game choices remain career state");
  snapshot = scan_profile_catalog(*xdg);
  check(snapshot.entries.size() == 2U && snapshot.continue_index == 0U &&
            snapshot.entries.front().metadata &&
            snapshot.entries.front().metadata->id == ProfileId{2},
        "Continue must select the newest valid save sequence deterministically");

  const auto symlink_path = *xdg / "profile-0000000000000003.json";
  std::error_code link_error;
  std::filesystem::create_symlink(assisted->path, symlink_path, link_error);
  snapshot = scan_profile_catalog(*xdg);
  check(!link_error && snapshot.entries.size() == 2U,
        "catalog scans must ignore canonical-name symbolic links");

  auto mismatched = assisted->source_bytes;
  mismatched = replace_once(mismatched,
                            "\"penalty_mode\": \"assisted\"",
                            "\"penalty_mode\": \"pilot\"");
  check(write_test_file(assisted->path, mismatched),
        "the invalid profile-header fixture must be writable");
  snapshot = scan_profile_catalog(*xdg);
  const auto invalid = std::ranges::find_if(
      snapshot.entries, [&](const ProfileCatalogEntry& entry) {
        return entry.path == assisted->path;
      });
  check(invalid != snapshot.entries.end() &&
            invalid->status == ProfileCatalogStatus::invalid_header &&
            !invalid->activatable() && snapshot.continue_index == 0U &&
            snapshot.entries[*snapshot.continue_index].metadata->id ==
                ProfileId{2},
        "an inconsistent header must remain visible without displacing the newest usable Continue profile");

  TemporaryDirectory full;
  for (std::uint64_t id = 1; id <= kMaximumLocalProfiles; ++id) {
    check(write_test_file(
              full.path() / std::format("profile-{:016x}.json", id), "{}"),
          "the full-catalog fixture must create each bounded candidate");
  }
  const auto full_snapshot = scan_profile_catalog(full.path());
  const auto refused = create_catalog_profile(full.path(), assisted_document);
  check(full_snapshot.entries.size() == kMaximumLocalProfiles &&
            !full_snapshot.overflow && !refused &&
            refused.error().code == ProfileCatalogErrorCode::catalog_full,
        "a 64-entry profile catalog must refuse a 65th career without overwriting data");
  check(write_test_file(
            full.path() / "profile-0000000000000041.json", "{}"),
        "the overflow catalog fixture must create one extra canonical candidate");
  const auto overflow = scan_profile_catalog(full.path());
  check(overflow.overflow && !overflow.writable && overflow.entries.empty(),
        "more than 64 canonical profile files must fail catalog enumeration closed");
}

auto signal_run_contract() -> void {
  auto cache = TerrainTileCache::create();
  check(cache.has_value(), "Signal Run must create a terrain cache");
  if (!cache) return;

  const auto fresh = make_legacy_signal_run_document(Seed{42});
  auto run = hydrate_signal_run(fresh, *cache);
  check(run.has_value() &&
            run->onboarding.location == OriginLocation::docked_at_origin &&
            run->onboarding.first_objective ==
                FirstObjectiveStatus::offered &&
            !run->scanner.selected && !run->flight,
        "a fresh save must hydrate as the bounded docked offer");
  if (!run) return;

  const auto before_invalid_launch = project_signal_run_save(*run);
  check(!launch_signal_run(*run, *cache) &&
            project_signal_run_save(*run) == before_invalid_launch,
        "launch before briefing acceptance must be rejected transactionally");
  check(accept_signal_run(*run).has_value() && run->scanner.selected &&
            run->discoveries.size() == 1 && run->rendezvous,
        "accepting the briefing must bind and discover exactly one target");
  check(launch_signal_run(*run, *cache).has_value() && run->flight &&
            run->flight->regime == FlightRegime::orbital &&
            run->onboarding.location == OriginLocation::in_flight,
        "launch must create the authoritative orbital craft at rendezvous");

  auto non_finite = *run;
  non_finite.flight->pose.position.latitude_radians =
      std::numeric_limits<double>::quiet_NaN();
  const auto non_finite_checksum =
      planetary_flight_state_checksum(*non_finite.flight);
  check(advance_signal_run(non_finite, *cache, {}) ==
                std::unexpected{SignalRunError::terrain_failure} &&
            non_finite.flight &&
            planetary_flight_state_checksum(*non_finite.flight) ==
                non_finite_checksum &&
            non_finite.onboarding.location == OriginLocation::in_flight,
        "non-finite Signal Run state must be rejected transactionally");

  const auto target = *run->scanner.selected;
  check(run->journal
            .record({surface_signal_object_key(target),
                     SaveWorldDeltaKind::collected, run->flight->tick})
            .has_value(),
        "the completed integration fixture must record its collected delta");
  check(advance_origin_onboarding(
            run->onboarding,
            OriginOnboardingCommand::complete_first_objective)
            .has_value(),
        "the completed integration fixture must advance the objective");
  run->collection = SignalCollectionState{
      .status = SignalCollectionStatus::complete,
      .target = target,
      .consecutive_in_range_ticks = kSignalCollectionTotalInRangeTicks,
      .last_tick = run->flight->tick,
      .completion_tick = run->flight->tick,
  };
  check(advance_signal_run(*run, *cache, {}).has_value() &&
            run->origin_navigation && run->origin_navigation->arrived,
        "a completed craft at the orbital waypoint must resolve return arrival");

  const auto checkpoint = project_signal_run_save(*run);
  check(checkpoint.has_value() && checkpoint->state.flight &&
            checkpoint->state.first_objective ==
                FirstObjectiveStatus::completed &&
            checkpoint->state.discoveries.size() == 1 &&
            checkpoint->state.world_deltas.size() == 1,
        "the in-flight checkpoint must preserve craft, mission, discovery, and delta state");
  if (!checkpoint) return;
  auto resumed_cache = TerrainTileCache::create();
  auto resumed = resumed_cache
                     ? hydrate_signal_run(*checkpoint, *resumed_cache)
                     : std::expected<SignalRunState, SignalRunError>{
                           std::unexpected{SignalRunError::terrain_failure}};
  check(resumed.has_value() && resumed->flight == run->flight &&
            resumed->collection.status == SignalCollectionStatus::complete &&
            resumed->journal.entries().size() == 1,
        "checkpoint hydration must restore terminal collection without duplicate deltas");
  if (!resumed) return;
  check(return_signal_run_to_origin(*resumed).has_value() &&
            resumed->onboarding.location ==
                OriginLocation::docked_at_origin &&
            !resumed->flight,
        "an arrived completed craft must return to the origin station");
  const auto returned = project_signal_run_save(*resumed);
  check(returned.has_value() && !returned->state.flight &&
            returned->state.first_objective == FirstObjectiveStatus::returned &&
            returned->state.world_deltas.size() == 1,
        "the returned save must retain mission and sparse world state");

  auto invalid = *checkpoint;
  invalid.state.first_objective_target = SurfaceSignalId{1};
  check(hydrate_signal_run(invalid, *cache) ==
            std::unexpected{SignalRunError::invalid_document},
        "an unknown generated target must fail at the schema boundary before hydration");
  invalid = *checkpoint;
  invalid.state.discoveries.clear();
  check(hydrate_signal_run(invalid, *cache) ==
            std::unexpected{SignalRunError::invalid_document},
        "an objective missing its discovery must fail at the schema boundary");

  const auto career_document = make_new_game_document(Seed{42});
  auto career_cache = TerrainTileCache::create();
  auto career = career_cache
                    ? hydrate_signal_run(career_document, *career_cache)
                    : std::expected<SignalRunState, SignalRunError>{
                          std::unexpected{SignalRunError::terrain_failure}};
  check(career && accept_signal_run(*career) &&
            launch_signal_run(*career, *career_cache) && career->station_flight,
        "Guided contract one must launch into physical station flight");
  if (!career || !career->station_flight) return;
  const auto guidance_tick = career->station_flight->tick;
  const std::array guidance_commands{
      FlightCommand{guidance_tick, FlightCommandKind::press_turn_left},
      FlightCommand{guidance_tick, FlightCommandKind::press_strafe_right},
      FlightCommand{guidance_tick, FlightCommandKind::press_forward},
      FlightCommand{guidance_tick, FlightCommandKind::release_forward},
      FlightCommand{guidance_tick, FlightCommandKind::press_backward},
      FlightCommand{guidance_tick, FlightCommandKind::release_backward},
  };
  check(advance_signal_run_station_flight(*career, guidance_commands) &&
            career->guidance.attitude_observed &&
            career->guidance.translation_observed &&
            career->guidance.thrust_observed &&
            career->guidance.coast_observed &&
            career->guidance.braking_observed &&
            career->onboarding.first_objective == FirstObjectiveStatus::active,
        "the optional flight check must observe real controls without "
        "advancing the objective");
  auto without_presentation_history = *career;
  without_presentation_history.guidance = {};
  check(project_signal_run_save(*career) ==
            project_signal_run_save(without_presentation_history),
        "contextual guidance observations must not enter saves or "
        "authoritative state");

  auto immediate_redock = *career;
  immediate_redock.station_flight->relative_position = {
      kOriginStationArrivalRadiusMetres, 0.0, 0.0};
  immediate_redock.station_flight->relative_velocity = {};
  const auto immediate_result = interact_signal_run_station(
      immediate_redock, *career_cache);
  SessionController interaction_session{false, true};
  (void)interaction_session.dispatch(MenuCommand::activate);
  (void)interaction_session.start_flight();
  if (immediate_result == SignalRunStationInteraction::redocked) {
    (void)interaction_session.dock_at_station();
  }
  check(immediate_result == SignalRunStationInteraction::redocked &&
            !immediate_redock.station_flight &&
            immediate_redock.onboarding.location ==
                OriginLocation::docked_at_origin &&
            immediate_redock.onboarding.first_objective ==
                FirstObjectiveStatus::active &&
            interaction_session.screen() == SessionScreen::station,
        "Enter at the initial safe boundary must explicitly redock without "
        "requesting process exit");

  auto speed_boundary = *career;
  speed_boundary.station_flight->relative_position = {
      kOriginStationArrivalRadiusMetres, 0.0, 0.0};
  speed_boundary.station_flight->relative_velocity = {
      kOriginStationDockingSpeedMetresPerSecond, 0.0, 0.0};
  check(interact_signal_run_station(speed_boundary, *career_cache) ==
                SignalRunStationInteraction::redocked &&
            !speed_boundary.station_flight,
        "the inclusive 25 m/s boundary must remain dockable");

  auto too_fast = *career;
  too_fast.station_flight->relative_position = {
      kOriginStationArrivalRadiusMetres, 0.0, 0.0};
  too_fast.station_flight->relative_velocity = {
      std::nextafter(kOriginStationDockingSpeedMetresPerSecond,
                     std::numeric_limits<double>::infinity()),
      0.0, 0.0};
  const auto too_fast_before = project_signal_run_save(too_fast);
  const auto too_fast_flight_before = *too_fast.station_flight;
  check(interact_signal_run_station(too_fast, *career_cache) ==
                std::unexpected{
                    SignalRunStationInteractionError::reduce_speed_or_depart} &&
            project_signal_run_save(too_fast) == too_fast_before &&
            too_fast.station_flight == too_fast_flight_before &&
            !too_fast.flight,
        "excess speed inside 5 km must reject transactionally instead of "
        "starting Planetfall");

  auto outside = *career;
  outside.station_flight->relative_position = {
      std::nextafter(kOriginStationArrivalRadiusMetres,
                     std::numeric_limits<double>::infinity()),
      0.0, 0.0};
  outside.station_flight->relative_velocity = {};
  check(interact_signal_run_station(outside, *career_cache) ==
                SignalRunStationInteraction::planetfall_started &&
            outside.flight && !outside.station_flight,
        "the first representable position outside 5 km must start home "
        "Planetfall");

  auto returning_outside = *career;
  returning_outside.onboarding.first_objective =
      FirstObjectiveStatus::completed;
  returning_outside.station_flight->relative_position = {
      std::nextafter(kOriginStationArrivalRadiusMetres,
                     std::numeric_limits<double>::infinity()),
      0.0, 0.0};
  returning_outside.station_flight->relative_velocity = {};
  const auto returning_before = project_signal_run_save(returning_outside);
  const auto returning_flight_before = *returning_outside.station_flight;
  check(interact_signal_run_station(returning_outside, *career_cache) ==
                std::unexpected{
                    SignalRunStationInteractionError::approach_station} &&
            project_signal_run_save(returning_outside) == returning_before &&
            returning_outside.station_flight == returning_flight_before,
        "an incomplete return rendezvous must report approach guidance and "
        "cannot re-enter Planetfall");

  auto returned_home = *career;
  returned_home.onboarding.first_objective = FirstObjectiveStatus::completed;
  returned_home.station_flight->relative_position = {
      kOriginStationArrivalRadiusMetres, 0.0, 0.0};
  returned_home.station_flight->relative_velocity = {};
  check(interact_signal_run_station(returned_home, *career_cache) ==
                SignalRunStationInteraction::objective_returned &&
            !returned_home.station_flight &&
            returned_home.onboarding.location ==
                OriginLocation::docked_at_origin &&
            returned_home.onboarding.first_objective ==
                FirstObjectiveStatus::returned,
        "a completed objective must dock only at the safe inclusive return "
        "boundary");

  auto non_finite_station = *career;
  non_finite_station.station_flight->relative_position.x =
      std::numeric_limits<double>::quiet_NaN();
  const auto non_finite_bits = std::bit_cast<std::uint64_t>(
      non_finite_station.station_flight->relative_position.x);
  check(interact_signal_run_station(non_finite_station, *career_cache) ==
                std::unexpected{
                    SignalRunStationInteractionError::guidance_unavailable} &&
            non_finite_station.station_flight &&
            std::bit_cast<std::uint64_t>(
                non_finite_station.station_flight->relative_position.x) ==
                non_finite_bits,
        "non-finite station interaction state must reject before mutation");
}

auto world_delta_journal_contract() -> void {
  const SurfaceSignalId first_id{0x0123456789abcdefULL};
  const auto first_key = surface_signal_object_key(first_id);
  check(first_key == "signal-0123456789abcdef" &&
            parse_surface_signal_object_key(first_key) == first_id,
        "surface-signal object keys must use the canonical stable ID");
  constexpr std::array invalid_keys{
      std::string_view{},
      std::string_view{"signal-123"},
      std::string_view{"Signal-0123456789abcdef"},
      std::string_view{"signal-0123456789abcdeF"},
      std::string_view{"signal-0123456789abcdeg"},
  };
  for (const auto key : invalid_keys) {
    check(parse_surface_signal_object_key(key) ==
              std::unexpected{WorldDeltaJournalError::invalid_object_key},
          "malformed generated-object keys must be rejected");
  }

  const auto second_key =
      surface_signal_object_key(SurfaceSignalId{0xfedcba9876543210ULL});
  const std::array duplicate_entries{
      SaveWorldDelta{first_key, SaveWorldDeltaKind::discovered, 10},
      SaveWorldDelta{second_key, SaveWorldDeltaKind::discovered, 4},
      SaveWorldDelta{first_key, SaveWorldDeltaKind::completed, 20},
      SaveWorldDelta{first_key, SaveWorldDeltaKind::collected, 20},
      SaveWorldDelta{first_key, SaveWorldDeltaKind::discovered, 15},
  };
  auto journal = WorldDeltaJournal::create(duplicate_entries);
  check(journal.has_value(),
        "a valid sparse world journal must be constructible");
  if (!journal) return;
  check(journal->entries().size() == 2 &&
            journal->entries()[0] == duplicate_entries[1] &&
            journal->entries()[1] == duplicate_entries[3],
        "journal compaction must choose the greatest tick, break equal ticks "
        "by source order, and sort canonically");
  const auto* first_state = journal->state(first_key);
  check(first_state != nullptr &&
            first_state->kind == SaveWorldDeltaKind::collected &&
            first_state->tick == 20,
        "journal lookup must expose the compact current object state");

  const auto before_failure = std::vector<SaveWorldDelta>{
      journal->entries().begin(), journal->entries().end()};
  check(journal->record({"bad key", SaveWorldDeltaKind::removed, 30}) ==
                std::unexpected{WorldDeltaJournalError::invalid_object_key} &&
            std::ranges::equal(journal->entries(), before_failure),
        "invalid journal records must not partially mutate current state");
  check(
      journal->record({first_key, static_cast<SaveWorldDeltaKind>(255), 30}) ==
              std::unexpected{WorldDeltaJournalError::invalid_delta_kind} &&
          std::ranges::equal(journal->entries(), before_failure),
      "future delta kinds must be rejected without mutation");
  check(journal->record({first_key, SaveWorldDeltaKind::removed, 19})
                .has_value() &&
            journal->state(first_key)->kind == SaveWorldDeltaKind::collected,
        "stale journal records must be deterministic idempotent no-ops");

  std::vector<SaveWorldDelta> oversized(kMaximumSaveWorldDeltas + 1U,
                                        duplicate_entries.front());
  check(WorldDeltaJournal::create(oversized) ==
            std::unexpected{WorldDeltaJournalError::journal_capacity_exceeded},
        "raw journal input must respect the version 1 entry bound before "
        "compaction");
}

auto regenerated_world_delta_contract() -> void {
  const auto recipe = make_save_recipe(Seed{42});
  const auto system_seed = derive_seed(recipe.universe_seed, SeedDomain::system,
                                       recipe.origin_system_ordinal);
  const auto planet = generate_origin_home_planet(system_seed);
  auto first_cache = TerrainTileCache::create(1);
  check(first_cache.has_value(),
        "world-delta regeneration requires a bounded terrain cache");
  if (!first_cache) return;
  const auto original = generate_surface_signals(planet, *first_cache);
  check(original.has_value(),
        "world-delta regeneration requires a generated signal catalog");
  if (!original) return;

  const auto binding = generate_home_signal_contract(Seed{42});
  const auto target = binding.target;
  SaveDocument saved{
      .recipe = recipe,
      .state =
          SaveMutableState{
              .location = OriginLocation::docked_at_origin,
              .first_objective = FirstObjectiveStatus::completed,
              .first_objective_contract = binding.contract,
              .first_objective_target = target,
              .flight = std::nullopt,
              .system_flight = std::nullopt,
              .origin_station_flight = std::nullopt,
              .discoveries = {{target, 100}},
              .world_deltas = {{surface_signal_object_key(target),
                                SaveWorldDeltaKind::collected, 120}},
              .intersystem_contract = std::nullopt,
          },
  };
  const auto encoded = encode_save_document_json(saved);
  check(encoded.has_value() &&
            encoded->find("\"terrain_tiles\": 1") != std::string::npos &&
            encoded->find("\"samples\"") == std::string::npos &&
            encoded->find("\"anchor\"") == std::string::npos,
        "world-delta saves must retain generator versions without serializing "
        "terrain data");
  if (!encoded) return;
  const auto decoded = decode_save_document_json(*encoded);
  check(decoded.has_value(),
        "a collected generated object must survive save codec round-trip");
  if (!decoded) return;

  const TerrainTileKey unrelated{planet.id, CubeFace::negative_z, 3, 1, 1};
  check(first_cache->get(planet, unrelated).has_value() &&
            first_cache->size() == 1,
        "the one-entry cache must evict prior generated terrain");
  auto regenerated_cache = TerrainTileCache::create(1);
  check(regenerated_cache.has_value(),
        "regeneration requires a fresh bounded cache");
  if (!regenerated_cache) return;
  const auto regenerated = generate_surface_signals(planet, *regenerated_cache);
  const auto restored_journal =
      WorldDeltaJournal::create(decoded->state.world_deltas);
  check(regenerated.has_value() && restored_journal.has_value(),
        "catalog and sparse state must regenerate independently");
  if (!regenerated || !restored_journal) return;
  const auto projection =
      apply_world_delta_journal(*regenerated, *restored_journal);
  check(projection.has_value(),
        "known sparse deltas must apply after deterministic regeneration");
  if (!projection) return;
  check(projection->signals.front().generated.id == target &&
            projection->signals.front().delta &&
            projection->signals.front().delta->kind ==
                SaveWorldDeltaKind::collected &&
            !projection->signals.front().active,
        "a collected object must remain collected after eviction, "
        "regeneration, and load");
  check(std::ranges::all_of(projection->signals | std::views::drop(1),
                            [](const SurfaceSignalWorldEntry& entry) {
                              return entry.active && !entry.delta;
                            }),
        "unchanged generated objects must remain active without journal bulk");

  const std::array state_deltas{
      SaveWorldDelta{surface_signal_object_key(regenerated->signals[0].id),
                     SaveWorldDeltaKind::collected, 200},
      SaveWorldDelta{surface_signal_object_key(regenerated->signals[1].id),
                     SaveWorldDeltaKind::discovered, 201},
      SaveWorldDelta{surface_signal_object_key(regenerated->signals[2].id),
                     SaveWorldDeltaKind::completed, 202},
      SaveWorldDelta{surface_signal_object_key(regenerated->signals[3].id),
                     SaveWorldDeltaKind::removed, 203},
  };
  const auto state_journal = WorldDeltaJournal::create(state_deltas);
  check(state_journal.has_value(),
        "every version 1 object state must be journalable");
  if (state_journal) {
    const auto state_projection =
        apply_world_delta_journal(*regenerated, *state_journal);
    check(state_projection && !state_projection->signals[0].active &&
              state_projection->signals[1].active &&
              !state_projection->signals[2].active &&
              !state_projection->signals[3].active,
          "discovered objects must remain active while collected, completed, and removed objects are terminal");
  }

  const std::array unknown_delta{SaveWorldDelta{
      "signal-0000000000000000", SaveWorldDeltaKind::removed, 121}};
  const auto unknown_journal = WorldDeltaJournal::create(unknown_delta);
  check(unknown_journal &&
            apply_world_delta_journal(*regenerated, *unknown_journal) ==
                std::unexpected{WorldDeltaJournalError::unknown_object_key},
        "unknown generated-object keys must fail application transactionally");
}

auto surface_signal_contract() -> void {
  check(kSurfaceSignalGeneratorVersion == 1 && kSurfaceSignalCount == 6 &&
            kSurfaceSignalPlacementLod == 12 &&
            kSurfaceSignalPlacementAttempts == 64 &&
            kSurfaceSignalMaximumReliefMetres == 750 &&
            kSurfaceSignalApproachClearanceMetres == 1'000,
        "surface-signal version 1 placement constants must remain stable");

  constexpr Seed parent{0xD15EA5EULL};
  const auto placement =
      derive_surface_signal_seed(parent, SurfaceSignalStream::placement);
  const auto attributes =
      derive_surface_signal_seed(parent, SurfaceSignalStream::attributes);
  check(placement == derive_surface_signal_seed(
                         parent, SurfaceSignalStream::placement) &&
            placement != attributes,
        "surface-signal placement and attributes must use stable independent streams");
  check(surface_signal_id_string(SurfaceSignalId{0}) ==
                "signal-0000000000000000" &&
            surface_signal_id_string(SurfaceSignalId{
                std::numeric_limits<std::uint64_t>::max()}) ==
                "signal-ffffffffffffffff",
        "surface signal IDs must retain their fixed-width canonical encoding");

  const auto planet = generate_planet_descriptor(Seed{42});
  auto cache = TerrainTileCache::create();
  check(cache.has_value(), "surface-signal tests require a terrain cache");
  if (!cache) return;
  const auto catalog = generate_surface_signals(planet, *cache);
  check(catalog.has_value(),
        "the canonical planet must produce a complete surface-signal catalog");
  if (!catalog) return;
  check(catalog->planet == planet.id &&
            catalog->signals.size() == kSurfaceSignalCount,
        "a surface-signal catalog must retain its planet and fixed count");

  struct Golden {
    std::uint64_t id{};
    SurfaceSignalKind kind{};
    CubeFace face{};
    std::uint32_t x{};
    std::uint32_t y{};
    std::int32_t surface{};
    std::int32_t approach{};
    std::uint16_t strength{};
    std::uint16_t reward{};
    std::uint16_t attempt{};
  };
  constexpr std::array<Golden, kSurfaceSignalCount> goldens{
      Golden{10691169904300360855ULL, SurfaceSignalKind::anomaly,
             CubeFace::positive_x, 2019, 1937, 1957, 3031, 8639, 1, 0},
      Golden{8458854497332771446ULL, SurfaceSignalKind::survey,
             CubeFace::negative_x, 1373, 2311, 2984, 4105, 5987, 3, 0},
      Golden{6226539090365182037ULL, SurfaceSignalKind::anomaly,
             CubeFace::positive_y, 1214, 1147, -540, 549, 4730, 1, 0},
      Golden{3994223683397592628ULL, SurfaceSignalKind::recovery,
             CubeFace::negative_y, 1584, 1026, 2862, 3911, 8554, 3, 0},
      Golden{1761908276430003219ULL, SurfaceSignalKind::survey,
             CubeFace::positive_z, 1748, 2172, -692, 428, 7652, 2, 0},
      Golden{17976336943171965426ULL, SurfaceSignalKind::recovery,
             CubeFace::negative_z, 2526, 2201, 3353, 4494, 7602, 1, 0},
  };
  for (std::size_t index = 0; index < goldens.size(); ++index) {
    const auto& signal = catalog->signals[index];
    const auto& golden = goldens[index];
    check(signal.id.value == golden.id && signal.ordinal == index &&
              signal.kind == golden.kind &&
              signal.anchor.tile.face == golden.face &&
              signal.anchor.tile.x == golden.x &&
              signal.anchor.tile.y == golden.y && signal.anchor.u == 0.5 &&
              signal.anchor.v == 0.5 &&
              signal.surface_elevation_metres == golden.surface &&
              signal.approach_altitude_metres == golden.approach &&
              signal.strength_basis_points == golden.strength &&
              signal.reward.discovery_points == golden.reward &&
              signal.placement_attempt == golden.attempt,
          "seed 42 surface signals must retain their version 1 golden catalog");
    for (std::size_t other = index + 1; other < catalog->signals.size();
         ++other) {
      check(signal.id != catalog->signals[other].id,
            "one planet's generated signal identities must remain unique");
    }
  }

  auto warm_cache = TerrainTileCache::create();
  check(warm_cache.has_value(), "cache-order checks require a terrain cache");
  if (warm_cache) {
    const TerrainTileKey unrelated{planet.id, CubeFace::positive_x, 3, 2, 4};
    const auto tile_before = warm_cache->get(planet, unrelated);
    const auto checksum_before = tile_before ? (*tile_before)->checksum() : 0;
    const auto warm_first = generate_surface_signals(planet, *warm_cache);
    const auto warm_second = generate_surface_signals(planet, *warm_cache);
    const auto tile_after = warm_cache->get(planet, unrelated);
    check(warm_first == catalog && warm_second == catalog,
          "terrain-cache residency must not affect surface-signal generation");
    check(tile_before && tile_after &&
              (*tile_after)->checksum() == checksum_before,
          "surface-signal generation must not perturb terrain identity");
  }

  const auto invalid_planet = planet_with_radius(planet, 0);
  auto invalid_cache = TerrainTileCache::create();
  check(invalid_cache &&
            generate_surface_signals(invalid_planet, *invalid_cache) ==
                std::unexpected{SurfaceSignalError::invalid_planet},
        "invalid planets must be rejected before a signal catalog is returned");

  auto exhausted_cache = TerrainTileCache::create();
  check(exhausted_cache &&
            detail::generate_surface_signals_with_limits(
                planet, *exhausted_cache,
                {.attempts = 1, .maximum_relief_metres = 0}) ==
                std::unexpected{SurfaceSignalError::placement_exhausted},
        "a rejected final candidate must fail transactionally without a partial catalog");
}

auto surface_signal_population() -> void {
  constexpr std::array expected_faces{
      CubeFace::positive_x, CubeFace::negative_x, CubeFace::positive_y,
      CubeFace::negative_y, CubeFace::positive_z, CubeFace::negative_z,
  };
  for (std::uint64_t seed = 0; seed < 256; ++seed) {
    const auto planet = generate_planet_descriptor(Seed{seed});
    auto cache = TerrainTileCache::create();
    check(cache.has_value(), "population checks require a terrain cache");
    if (!cache) return;
    const auto first = generate_surface_signals(planet, *cache);
    const auto second = generate_surface_signals(planet, *cache);
    check(first.has_value() && first == second,
          "a multi-seed population must produce stable complete catalogs");
    if (!first) continue;

    for (std::size_t index = 0; index < first->signals.size(); ++index) {
      const auto& signal = first->signals[index];
      check(signal.ordinal == index && signal.anchor.tile.planet == planet.id &&
                signal.anchor.tile.face == expected_faces[index] &&
                signal.anchor.tile.lod == kSurfaceSignalPlacementLod &&
                signal.anchor.tile.x >= 1'024 &&
                signal.anchor.tile.x < 3'072 &&
                signal.anchor.tile.y >= 1'024 &&
                signal.anchor.tile.y < 3'072 && signal.anchor.u == 0.5 &&
                signal.anchor.v == 0.5 &&
                signal.placement_attempt < kSurfaceSignalPlacementAttempts,
            "signals must retain ordered central-face anchors and bounded retries");
      check(signal.strength_basis_points >=
                    kSurfaceSignalMinimumStrengthBasisPoints &&
                signal.strength_basis_points <=
                    kSurfaceSignalMaximumStrengthBasisPoints &&
                signal.reward.discovery_points >=
                    kSurfaceSignalMinimumRewardPoints &&
                signal.reward.discovery_points <=
                    kSurfaceSignalMaximumRewardPoints,
            "generated signal attributes must remain inside their versioned ranges");

      const auto tile = cache->get(planet, signal.anchor.tile);
      check(tile.has_value(), "accepted signal terrain must remain available");
      if (!tile) continue;
      auto minimum = std::numeric_limits<std::int32_t>::max();
      auto maximum = std::numeric_limits<std::int32_t>::min();
      for (const auto y : std::array<std::size_t, 3>{16, 32, 48}) {
        for (const auto x : std::array<std::size_t, 3>{16, 32, 48}) {
          const auto sample = (*tile)->sample_at(x, y);
          check(sample.has_value(), "signal relief samples must be in bounds");
          if (!sample) continue;
          minimum = std::min(minimum, sample->get().elevation_metres);
          maximum = std::max(maximum, sample->get().elevation_metres);
        }
      }
      check(maximum - minimum <= kSurfaceSignalMaximumReliefMetres &&
                signal.approach_altitude_metres ==
                    maximum + kSurfaceSignalApproachClearanceMetres,
            "accepted signals must satisfy relief and approach-clearance rules");
    }

    for (std::size_t left = 0; left < first->signals.size(); ++left) {
      const auto left_position = planet_fixed_from_terrain_address(
          planet, first->signals[left].anchor);
      for (std::size_t right = left + 1; right < first->signals.size();
           ++right) {
        const auto right_position = planet_fixed_from_terrain_address(
            planet, first->signals[right].anchor);
        check(left_position.has_value() && right_position.has_value(),
              "signal anchors must map back to planet-fixed positions");
        if (!left_position || !right_position) continue;
        const auto left_length = std::hypot(
            left_position->x, left_position->y, left_position->z);
        const auto right_length = std::hypot(
            right_position->x, right_position->y, right_position->z);
        const auto cosine =
            (left_position->x * right_position->x +
             left_position->y * right_position->y +
             left_position->z * right_position->z) /
            (left_length * right_length);
        check(cosine <= std::cos(std::numbers::pi_v<double> / 6.0) + 1.0e-12,
              "central-face signal anchors must remain at least 30 degrees apart");
      }
    }
  }
}

auto signal_scanner_contract() -> void {
  const auto planet = generate_planet_descriptor(Seed{42});
  auto cache = TerrainTileCache::create();
  check(cache.has_value(), "signal scanner terrain cache must initialize");
  if (!cache) return;
  const auto catalog = generate_surface_signals(planet, *cache);
  check(catalog.has_value(), "signal scanner catalog must generate");
  if (!catalog) return;

  SignalScannerState selection;
  const auto selected_next = advance_signal_selection(
      *catalog, selection, SignalSelectionCommand::next);
  check(selected_next && selection.selected == catalog->signals[0].id,
        "next must select the first signal from an empty selection");
  for (std::size_t index = 1; index < catalog->signals.size(); ++index) {
    check(advance_signal_selection(*catalog, selection,
                                   SignalSelectionCommand::next)
              .has_value(),
          "next selection must advance through the catalog");
  }
  check(selection.selected == catalog->signals.back().id &&
            advance_signal_selection(*catalog, selection,
                                     SignalSelectionCommand::next) &&
            selection.selected == catalog->signals.front().id,
        "next selection must wrap in catalog order");
  check(advance_signal_selection(*catalog, selection,
                                 SignalSelectionCommand::previous) &&
            selection.selected == catalog->signals.back().id,
        "previous selection must wrap in catalog order");

  SignalScannerState invalid_selection{SurfaceSignalId{1}};
  const auto unchanged = invalid_selection;
  check(advance_signal_selection(*catalog, invalid_selection,
                                 SignalSelectionCommand::next) ==
            std::unexpected{SignalScannerError::invalid_selection} &&
            invalid_selection == unchanged,
        "invalid scanner selection must be rejected transactionally");
  auto malformed = *catalog;
  malformed.signals[1].id = malformed.signals[0].id;
  check(advance_signal_selection(malformed, selection,
                                 SignalSelectionCommand::next) ==
            std::unexpected{SignalScannerError::invalid_catalog},
        "duplicate signal identities must invalidate selection");
  check(advance_signal_selection(
            *catalog, selection,
            static_cast<SignalSelectionCommand>(255)) ==
            std::unexpected{SignalScannerError::invalid_command},
        "unknown signal selection commands must be rejected");

  const auto& target_signal = catalog->signals[0];
  SignalScannerState target_selection{target_signal.id};
  const auto target_fixed = planet_fixed_from_terrain_address(
      planet, target_signal.anchor,
      static_cast<double>(target_signal.approach_altitude_metres));
  check(target_fixed.has_value(), "signal approach point must resolve");
  if (!target_fixed) return;
  const auto target_position =
      geodetic_from_planet_fixed(planet, *target_fixed);
  check(target_position.has_value(), "signal approach point must be geodetic");
  if (!target_position) return;

  PlanetaryFlightState flight{
      .tick = 0,
      .planet = planet.id,
      .pose = {*target_position, 0.0},
      .velocity = {},
      .clearance_metres = kSurfaceSignalApproachClearanceMetres,
      .mode = FlightMode::manual,
      .controls = {},
      .regime = FlightRegime::terrain_flight,
      .last_transition = std::nullopt,
      .thermal = {},
  };
  const auto reached = resolve_signal_navigation(
      planet, *catalog, flight, target_selection);
  check(reached && reached->status == SignalScannerStatus::reached &&
            reached->distance_metres < 1.0e-6 &&
            reached->selected == target_signal.id,
        "the generated approach point must report the target reached");

  flight.pose.position.altitude_metres =
      target_position->altitude_metres + kSignalScannerReachedRadiusMetres;
  const auto reached_boundary = resolve_signal_navigation(
      planet, *catalog, flight, target_selection);
  check(reached_boundary &&
            reached_boundary->status == SignalScannerStatus::reached,
        "the exact reached-radius boundary must remain reached");
  flight.pose.position.altitude_metres += 1.0;
  const auto outside_reached = resolve_signal_navigation(
      planet, *catalog, flight, target_selection);
  check(outside_reached &&
            outside_reached->status == SignalScannerStatus::tracking,
        "one metre beyond the reached radius must resume tracking");
  flight.pose.position.altitude_metres =
      target_position->altitude_metres + kSignalScannerMaximumRangeMetres;
  const auto range_boundary = resolve_signal_navigation(
      planet, *catalog, flight, target_selection);
  check(range_boundary &&
            range_boundary->status == SignalScannerStatus::tracking,
        "the exact maximum-range boundary must remain trackable");
  flight.pose.position.altitude_metres += 1.0;
  const auto outside_range = resolve_signal_navigation(
      planet, *catalog, flight, target_selection);
  check(outside_range &&
            outside_range->status == SignalScannerStatus::out_of_range,
        "one metre beyond maximum range must report out of range");

  const auto target_frame = make_local_tangent_frame(planet, *target_position);
  check(target_frame.has_value(), "signal target tangent frame must resolve");
  if (!target_frame) return;
  const auto west_fixed =
      planet_fixed_from_local(*target_frame, {-5'000.0, 0.0, 0.0});
  const auto west_position =
      west_fixed ? geodetic_from_planet_fixed(planet, *west_fixed)
                 : std::expected<GeodeticPosition, CoordinateError>{
                       std::unexpected{CoordinateError::non_finite_input}};
  check(west_position.has_value(), "signal approach start must resolve");
  if (!west_position) return;
  flight.pose = {*west_position, 0.0};
  const auto tracking = resolve_signal_navigation(
      planet, *catalog, flight, target_selection);
  check(tracking && tracking->status == SignalScannerStatus::tracking &&
            tracking->distance_metres > kSignalScannerReachedRadiusMetres &&
            tracking->distance_metres < 5'100.0 &&
            std::abs(tracking->relative_bearing_radians) < 0.01,
        "a nearby visible target must provide an ahead tracking solution");

  auto occluded_position = *target_position;
  occluded_position.longitude_radians += 0.2;
  if (occluded_position.longitude_radians > std::numbers::pi_v<double>) {
    occluded_position.longitude_radians -=
        2.0 * std::numbers::pi_v<double>;
  }
  flight.pose.position = occluded_position;
  const auto occluded = resolve_signal_navigation(
      planet, *catalog, flight, target_selection);
  check(occluded && occluded->status == SignalScannerStatus::occluded &&
            occluded->distance_metres < kSignalScannerMaximumRangeMetres,
        "a nearby target below the reference-sphere horizon must be occluded");

  auto opposite = *target_position;
  opposite.latitude_radians = -opposite.latitude_radians;
  opposite.longitude_radians += std::numbers::pi_v<double>;
  if (opposite.longitude_radians > std::numbers::pi_v<double>) {
    opposite.longitude_radians -= 2.0 * std::numbers::pi_v<double>;
  }
  flight.pose.position = opposite;
  const auto out_of_range = resolve_signal_navigation(
      planet, *catalog, flight, target_selection);
  check(out_of_range &&
            out_of_range->status == SignalScannerStatus::out_of_range &&
            out_of_range->distance_metres > kSignalScannerMaximumRangeMetres,
        "a target beyond scanner range must report out of range before occlusion");

  flight.pose.position = *target_position;
  const auto no_signal = resolve_signal_navigation(
      planet, *catalog, flight, SignalScannerState{});
  check(no_signal && no_signal->status == SignalScannerStatus::no_signal &&
            !no_signal->selected,
        "an empty selection must produce an explicit no-signal solution");
  auto non_finite = flight;
  non_finite.pose.heading_radians =
      std::numeric_limits<double>::quiet_NaN();
  check(resolve_signal_navigation(planet, *catalog, non_finite,
                                  target_selection) ==
            std::unexpected{SignalScannerError::invalid_flight_state},
        "non-finite scanner flight state must be rejected");
  auto wrong_flight = flight;
  wrong_flight.planet = PlanetId{planet.id.value + 1U};
  check(resolve_signal_navigation(planet, *catalog, wrong_flight,
                                  target_selection) ==
            std::unexpected{SignalScannerError::invalid_flight_state},
        "scanner flight state from another planet must be rejected");
  auto wrong_catalog = *catalog;
  wrong_catalog.planet = PlanetId{planet.id.value + 1U};
  check(resolve_signal_navigation(planet, wrong_catalog, flight,
                                  target_selection) ==
            std::unexpected{SignalScannerError::invalid_planet},
        "scanner catalogs from another planet must be rejected");

  const auto empty_readout = format_signal_scanner(*no_signal);
  const auto tracking_readout = format_signal_scanner(*tracking);
  const auto occluded_readout = format_signal_scanner(*occluded);
  const auto range_readout = format_signal_scanner(*out_of_range);
  const auto reached_readout = format_signal_scanner(*reached);
  const std::array readouts{empty_readout, tracking_readout, occluded_readout,
                            range_readout, reached_readout};
  check(std::ranges::all_of(readouts, [](const auto& readout) {
          return readout.target.size() == kInstrumentLineWidth &&
                 readout.bearing.size() == kInstrumentLineWidth &&
                 readout.distance.size() == kInstrumentLineWidth &&
                 readout.motion.size() == kInstrumentLineWidth &&
                 readout.arrival.size() == kInstrumentLineWidth &&
                 readout.strength.size() == kInstrumentLineWidth &&
                 readout.cue.size() == kInstrumentLineWidth;
        }),
        "scanner formatting must preserve fixed-width cockpit lines");
  check(empty_readout.cue == "NO SIGNAL" &&
            tracking_readout.cue == "THRUST >>" &&
            occluded_readout.cue == "OCCLUDED " &&
            range_readout.cue == "OUT RANGE" &&
            reached_readout.cue == "REACHED! ",
        "scanner states must remain understandable without color");
  const auto disclosed = tracking_readout.target + tracking_readout.bearing +
                         tracking_readout.distance +
                         tracking_readout.strength + tracking_readout.cue;
  check(disclosed.find("survey") == std::string::npos &&
            disclosed.find("recovery") == std::string::npos &&
            disclosed.find("anomaly") == std::string::npos &&
            disclosed.find(surface_signal_id_string(target_signal.id)) ==
                std::string::npos,
        "cockpit scanner output must not reveal undiscovered metadata");

  auto closing_motion = *tracking;
  closing_motion.motion = {
      .closing_speed_metres_per_second = 1'200.0,
      .arrival_estimate_seconds = 65.0,
      .stopping_distance_metres = 720.0,
      .cue = TargetMotionCue::closing,
  };
  auto braking_motion = closing_motion;
  braking_motion.motion.cue = TargetMotionCue::brake;
  auto opening_motion = closing_motion;
  opening_motion.motion.closing_speed_metres_per_second = -350.0;
  opening_motion.motion.arrival_estimate_seconds.reset();
  opening_motion.motion.cue = TargetMotionCue::opening;
  const auto closing_readout = format_signal_scanner(closing_motion);
  const auto braking_readout = format_signal_scanner(braking_motion);
  const auto opening_readout = format_signal_scanner(opening_motion);
  check(closing_readout.motion == "CLS +1.2k" &&
            closing_readout.arrival == "ETA 01:05" &&
            closing_readout.cue == "CLOSING  " &&
            braking_readout.cue == "BRAKE NOW" &&
            opening_readout.motion == "CLS -350 " &&
            opening_readout.arrival == "ETA --:--" &&
            opening_readout.cue == "OPENING! ",
        "scanner motion must expose closing, ETA, braking, and opening text");

  opening_motion.motion.closing_speed_metres_per_second = -0.4;
  check(format_signal_scanner(opening_motion).motion == "CLS -000 ",
        "sub-unit opening speed must preserve its sign when rounded");

  auto right = *tracking;
  right.relative_bearing_radians = 0.5;
  auto left = *tracking;
  left.relative_bearing_radians = -0.5;
  auto overflow = *tracking;
  overflow.distance_metres = 10'000'000.0;
  auto metre_boundary = *tracking;
  metre_boundary.distance_metres = 9'999.4;
  auto kilometre_boundary = *tracking;
  kilometre_boundary.distance_metres = 9'999.5;
  auto bearing_boundary = *tracking;
  bearing_boundary.absolute_bearing_radians =
      359.5 * std::numbers::pi_v<double> / 180.0;
  check(format_signal_scanner(right).cue == "TURN RGHT" &&
            format_signal_scanner(left).cue == "TURN LEFT" &&
            format_signal_scanner(metre_boundary).distance == "DST 9999m" &&
            format_signal_scanner(kilometre_boundary).distance ==
                "DST   10k" &&
            format_signal_scanner(bearing_boundary).bearing == "BRG 000  " &&
            format_signal_scanner(overflow).distance == "DST #### ",
        "scanner formatting must preserve directional and overflow boundaries");

  termforge::KeyEvent tab;
  tab.key = termforge::Key::Tab;
  tab.action = termforge::KeyAction::Press;
  check(detail::signal_selection_command(tab) ==
            SignalSelectionCommand::next,
        "Tab press must select the next signal");
  tab.shift = true;
  check(detail::signal_selection_command(tab) ==
            SignalSelectionCommand::previous,
        "Shift-Tab press must select the previous signal");
  tab.action = termforge::KeyAction::Repeat;
  check(!detail::signal_selection_command(tab),
        "key repeat must not change deterministic signal selection");
  tab.action = termforge::KeyAction::Release;
  check(!detail::signal_selection_command(tab),
        "key release must not change deterministic signal selection");
}

auto signal_collection_contract() -> void {
  check(kSignalCollectionAcquireTicks == 60 &&
            kSignalCollectionScanTicks == 360 &&
            kSignalCollectionTotalInRangeTicks == 420,
        "signal collection timing must remain fixed to the 120 Hz simulation clock");

  const auto planet = generate_planet_descriptor(Seed{42});
  auto cache = TerrainTileCache::create();
  check(cache.has_value(), "signal collection tests require a terrain cache");
  if (!cache) return;
  const auto catalog = generate_surface_signals(planet, *cache);
  check(catalog.has_value(), "signal collection tests require a catalog");
  if (!catalog) return;
  const auto& target = catalog->signals[0];
  const auto& other = catalog->signals[1];
  const auto navigation_for = [](const SurfaceSignal& signal,
                                 SignalScannerStatus status,
                                 double distance) {
    return SignalNavigationSolution{
        .status = status,
        .selected = signal.id,
        .ordinal = signal.ordinal,
        .absolute_bearing_radians = 0.0,
        .relative_bearing_radians = 0.0,
        .distance_metres = distance,
        .motion = {},
        .strength_basis_points = signal.strength_basis_points,
    };
  };
  const auto reached = navigation_for(
      target, SignalScannerStatus::reached,
      kSignalScannerReachedRadiusMetres);
  const auto tracking = navigation_for(
      target, SignalScannerStatus::tracking,
      kSignalScannerReachedRadiusMetres + 1.0);

  auto journal = *WorldDeltaJournal::create();
  SignalCollectionState state;
  const auto initial = advance_signal_collection(
      *catalog, tracking, 0, journal, state);
  check(initial && !initial->delta_emitted &&
            state.status == SignalCollectionStatus::approach &&
            state.target == target.id && state.last_tick == 0,
        "an out-of-radius target must remain in approach state");

  bool emitted_before_completion{};
  bool emitted_on_completion{};
  for (SimulationTick tick = 1;
       tick <= kSignalCollectionTotalInRangeTicks; ++tick) {
    const auto update = advance_signal_collection(
        *catalog, reached, tick, journal, state);
    check(update.has_value(),
          "valid consecutive in-range ticks must advance collection");
    if (!update) break;
    if (tick < kSignalCollectionTotalInRangeTicks) {
      emitted_before_completion |= update->delta_emitted;
    } else {
      emitted_on_completion = update->delta_emitted;
    }
    if (tick == kSignalCollectionAcquireTicks) {
      check(state.status == SignalCollectionStatus::in_range &&
                state.consecutive_in_range_ticks ==
                    kSignalCollectionAcquireTicks,
            "the final acquisition tick must remain in-range");
    }
    if (tick == kSignalCollectionAcquireTicks + 1U) {
      check(state.status == SignalCollectionStatus::scanning,
            "the tick after acquisition must begin scanning");
    }
  }
  const auto target_key = surface_signal_object_key(target.id);
  const auto* collected = journal.state(target_key);
  check(!emitted_before_completion && emitted_on_completion &&
            state.status == SignalCollectionStatus::complete &&
            state.completion_tick == kSignalCollectionTotalInRangeTicks &&
            collected != nullptr &&
            collected->kind == SaveWorldDeltaKind::collected &&
            collected->tick == kSignalCollectionTotalInRangeTicks &&
            journal.entries().size() == 1,
        "the 420th consecutive in-range tick must emit exactly one collection delta");
  const auto repeated = advance_signal_collection(
      *catalog, reached, kSignalCollectionTotalInRangeTicks + 1U, journal,
      state);
  check(repeated && !repeated->delta_emitted &&
            journal.entries().size() == 1,
        "a completed target must remain an idempotent single journal entry");

  auto abort_journal = *WorldDeltaJournal::create();
  SignalCollectionState abort_state;
  for (SimulationTick tick = 0;
       tick <= kSignalCollectionAcquireTicks; ++tick) {
    check(advance_signal_collection(*catalog, reached, tick, abort_journal,
                                    abort_state)
              .has_value(),
          "the abort fixture must reach scanning state");
  }
  check(abort_state.status == SignalCollectionStatus::scanning,
        "the abort fixture must begin scanning");
  const auto left_range = advance_signal_collection(
      *catalog, tracking, kSignalCollectionAcquireTicks + 1U, abort_journal,
      abort_state);
  check(left_range && abort_state.status == SignalCollectionStatus::aborted &&
            abort_state.consecutive_in_range_ticks == 0 &&
            abort_journal.entries().empty(),
        "leaving range must abort and reset partial scan progress");
  const auto reacquired = advance_signal_collection(
      *catalog, reached, kSignalCollectionAcquireTicks + 2U, abort_journal,
      abort_state);
  check(reacquired && abort_state.status == SignalCollectionStatus::in_range &&
            abort_state.consecutive_in_range_ticks == 1,
        "returning after an abort must start a fresh acquisition");

  auto retarget_journal = *WorldDeltaJournal::create();
  SignalCollectionState retarget_state;
  check(advance_signal_collection(*catalog, reached, 10, retarget_journal,
                                  retarget_state)
            .has_value(),
        "retarget fixture must begin acquisition");
  const auto other_reached = navigation_for(
      other, SignalScannerStatus::reached, 100.0);
  const auto retargeted = advance_signal_collection(
      *catalog, other_reached, 11, retarget_journal, retarget_state);
  check(retargeted &&
            retarget_state.status == SignalCollectionStatus::aborted &&
            retarget_state.target == other.id &&
            retarget_state.consecutive_in_range_ticks == 0,
        "changing targets must expose an aborted tick and discard progress");

  auto invalid_journal = *WorldDeltaJournal::create();
  SignalCollectionState invalid_state;
  auto non_finite = reached;
  non_finite.distance_metres = std::numeric_limits<double>::quiet_NaN();
  const auto state_before_invalid = invalid_state;
  const auto invalid = advance_signal_collection(
      *catalog, non_finite, 0, invalid_journal, invalid_state);
  check(!invalid &&
            invalid.error() == SignalCollectionError::invalid_navigation &&
            invalid_state == state_before_invalid &&
            invalid_journal.entries().empty(),
        "non-finite navigation must fail without partial mutation");
  auto unknown_target = reached;
  unknown_target.selected = SurfaceSignalId{0};
  const auto unknown = advance_signal_collection(
      *catalog, unknown_target, 0, invalid_journal, invalid_state);
  check(!unknown &&
            unknown.error() == SignalCollectionError::invalid_catalog &&
            invalid_state == state_before_invalid &&
            invalid_journal.entries().empty(),
        "unknown generated targets must fail without partial mutation");
  SignalCollectionState malformed_state{
      .status = static_cast<SignalCollectionStatus>(255),
      .target = std::nullopt,
      .consecutive_in_range_ticks = 0,
      .last_tick = std::nullopt,
      .completion_tick = std::nullopt,
  };
  const auto malformed_before = malformed_state;
  const auto malformed = advance_signal_collection(
      *catalog, tracking, 0, invalid_journal, malformed_state);
  check(!malformed &&
            malformed.error() == SignalCollectionError::invalid_state &&
            malformed_state == malformed_before,
        "unknown collection states must be rejected transactionally");
  check(advance_signal_collection(*catalog, tracking, 0, invalid_journal,
                                  invalid_state)
            .has_value(),
        "tick validation fixture must accept its first tick");
  const auto before_wrong_tick = invalid_state;
  const auto wrong_tick = advance_signal_collection(
      *catalog, tracking, 2, invalid_journal, invalid_state);
  check(!wrong_tick && wrong_tick.error() == SignalCollectionError::wrong_tick &&
            invalid_state == before_wrong_tick,
        "skipped or duplicate ticks must be rejected transactionally");
  SignalCollectionState overflow_state{
      .status = SignalCollectionStatus::approach,
      .target = target.id,
      .consecutive_in_range_ticks = 0,
      .last_tick = std::numeric_limits<SimulationTick>::max(),
      .completion_tick = std::nullopt,
  };
  const auto overflow_before = overflow_state;
  const auto overflow = advance_signal_collection(
      *catalog, tracking, std::numeric_limits<SimulationTick>::max(),
      invalid_journal, overflow_state);
  check(!overflow && overflow.error() == SignalCollectionError::tick_overflow &&
            overflow_state == overflow_before,
        "the tick boundary must fail before wraparound");

  std::vector<SaveWorldDelta> full_entries;
  full_entries.reserve(kMaximumSaveWorldDeltas);
  for (std::size_t index = 0; index < kMaximumSaveWorldDeltas; ++index) {
    full_entries.push_back(
        {std::format("signal-{:016x}", index + 1U),
         SaveWorldDeltaKind::discovered, 1});
  }
  auto full_journal = WorldDeltaJournal::create(full_entries);
  check(full_journal.has_value(),
        "the exact journal capacity boundary must be constructible");
  if (full_journal) {
    SignalCollectionState full_state{
        .status = SignalCollectionStatus::scanning,
        .target = target.id,
        .consecutive_in_range_ticks =
            kSignalCollectionTotalInRangeTicks - 1U,
        .last_tick = 500,
        .completion_tick = std::nullopt,
    };
    const auto full_state_before = full_state;
    const auto full_entries_before = std::vector<SaveWorldDelta>{
        full_journal->entries().begin(), full_journal->entries().end()};
    const auto full = advance_signal_collection(
        *catalog, reached, 501, *full_journal, full_state);
    check(!full && full.error() == SignalCollectionError::journal_failure &&
              full_state == full_state_before &&
              std::ranges::equal(full_journal->entries(),
                                 full_entries_before),
          "journal capacity failure must not expose a completed scan or partial delta");
  }

  const auto saved_recipe = make_save_recipe(Seed{42});
  const auto saved_system_seed =
      derive_seed(saved_recipe.universe_seed, SeedDomain::system,
                  saved_recipe.origin_system_ordinal);
  const auto saved_planet = generate_origin_home_planet(saved_system_seed);
  auto saved_cache = TerrainTileCache::create();
  check(saved_cache.has_value(),
        "save codec tests require a terrain cache");
  if (!saved_cache) return;
  auto saved_catalog = generate_surface_signals(saved_planet, *saved_cache);
  check(saved_catalog.has_value(),
        "save codec tests require the regenerated Signal Run catalog");
  if (!saved_catalog) return;
  const auto saved_binding = generate_home_signal_contract(Seed{42});
  const auto saved_target_iterator = std::ranges::find_if(
      saved_catalog->signals, [&](const SurfaceSignal& signal) {
        return signal.id == saved_binding.target;
      });
  check(saved_target_iterator != saved_catalog->signals.end(),
        "the saved home contract target must regenerate in its catalog");
  if (saved_target_iterator == saved_catalog->signals.end()) return;
  const auto& saved_target = *saved_target_iterator;
  SaveDocument saved{
      .recipe = saved_recipe,
      .state =
          SaveMutableState{
              .location = OriginLocation::docked_at_origin,
              .first_objective = FirstObjectiveStatus::completed,
              .first_objective_contract = saved_binding.contract,
              .first_objective_target = saved_target.id,
              .flight = std::nullopt,
              .system_flight = std::nullopt,
              .origin_station_flight = std::nullopt,
              .discoveries = {{saved_target.id, *state.completion_tick}},
              .world_deltas = {{surface_signal_object_key(saved_target.id),
                                SaveWorldDeltaKind::collected,
                                *state.completion_tick}},
              .intersystem_contract = std::nullopt,
          },
  };
  const auto encoded = encode_save_document_json(saved);
  const auto decoded =
      encoded ? decode_save_document_json(*encoded)
              : std::expected<SaveDocument, SaveSchemaError>{
                    std::unexpected{SaveSchemaError{}}};
  check(encoded.has_value() && decoded.has_value(),
        "a collected target must survive the version 1 save codec");
  if (decoded) {
    auto restored_journal =
        WorldDeltaJournal::create(decoded->state.world_deltas);
    SignalCollectionState restored_state;
    check(restored_journal.has_value(),
          "a saved collection journal must restore");
    if (restored_journal) {
      const auto saved_reached = navigation_for(
          saved_target, SignalScannerStatus::reached,
          kSignalScannerReachedRadiusMetres);
      const auto restored = advance_signal_collection(
          *saved_catalog, saved_reached, 10'000, *restored_journal,
          restored_state);
      check(restored && !restored->delta_emitted &&
                restored_state.status == SignalCollectionStatus::complete &&
                restored_state.completion_tick == state.completion_tick &&
                restored_journal->entries().size() == 1,
            "reload must recognize terminal state without collecting the unique target twice");
    }
  }

  const std::array readouts{
      format_signal_collection(SignalCollectionState{}),
      format_signal_collection(
          {.status = SignalCollectionStatus::in_range,
           .target = target.id,
           .consecutive_in_range_ticks = 30,
           .last_tick = std::nullopt,
           .completion_tick = std::nullopt}),
      format_signal_collection(
          {.status = SignalCollectionStatus::scanning,
           .target = target.id,
           .consecutive_in_range_ticks =
               kSignalCollectionAcquireTicks + 180U,
           .last_tick = std::nullopt,
           .completion_tick = std::nullopt}),
      format_signal_collection(
          {.status = SignalCollectionStatus::complete,
           .target = target.id,
           .consecutive_in_range_ticks =
               kSignalCollectionTotalInRangeTicks,
           .last_tick = kSignalCollectionTotalInRangeTicks,
           .completion_tick = kSignalCollectionTotalInRangeTicks}),
      format_signal_collection(
          {.status = SignalCollectionStatus::aborted,
           .target = target.id,
           .consecutive_in_range_ticks = 0,
           .last_tick = std::nullopt,
           .completion_tick = std::nullopt}),
  };
  check(std::ranges::all_of(readouts, [](const auto& readout) {
          return readout.cue.size() == kInstrumentLineWidth &&
                 !readout.message.empty();
        }) &&
            readouts[1].cue == "LOCK 050%" &&
            readouts[2].cue == "SCAN 050%" &&
            readouts[3].cue == "COLLECTED" &&
            readouts[4].cue == "SCAN LOST",
        "cockpit collection cues must be fixed-width, textual, and expose progress and outcomes");
}

auto signal_navigation_acceptance_contract() -> void {
  const auto first = replay_signal_navigation_acceptance();
  const auto second = replay_signal_navigation_acceptance();
  check(first.has_value() && second.has_value(),
        "the canonical signal approach must complete");
  if (!first || !second) return;
  const auto first_checksum = planetary_flight_state_checksum(first->flight);
  const auto second_checksum = planetary_flight_state_checksum(second->flight);
  constexpr SimulationTick expected_reached_tick{1'072};
  constexpr SimulationTick expected_completion_tick{1'491};
  constexpr std::uint64_t expected_flight_checksum{17407832030238464473ULL};
  check(first->reached_tick == expected_reached_tick &&
            first->flight.tick == expected_completion_tick &&
            first_checksum == expected_flight_checksum &&
            first->flight.tick == second->flight.tick &&
            first_checksum == second_checksum &&
            first->scanner == second->scanner &&
            first->navigation == second->navigation &&
            first->collection == second->collection &&
            std::ranges::equal(first->journal.entries(),
                               second->journal.entries()),
        "the signal collection path must replay deterministically");
  check(first->navigation.status == SignalScannerStatus::reached &&
            first->navigation.distance_metres <=
                kSignalScannerReachedRadiusMetres &&
            first->scanner.selected == first->catalog.signals[0].id &&
            first->collection.status == SignalCollectionStatus::complete &&
            first->collection.completion_tick == expected_completion_tick &&
            first->command_count == 2 && first->journal.entries().size() == 1 &&
            first->journal.entries().front().kind ==
                SaveWorldDeltaKind::collected,
        "the canonical approach must select, reach, and collect the first signal");

  const auto json = signal_navigation_acceptance_json({
      .target_id = *first->scanner.selected,
      .reached_tick = *first->reached_tick,
      .completion_tick = *first->collection.completion_tick,
      .command_count = first->command_count,
      .world_delta_count = first->journal.entries().size(),
      .final_distance_metres = first->navigation.distance_metres,
      .flight_checksum = first_checksum,
      .render_configuration = {{320, 240}, RenderProfile::remote},
      .presentation = "ansi",
      .framebuffer_checksum = 123,
  });
  check(json.find("\"schema_version\": 2") != std::string::npos &&
            json.find("\"scenario\": \"v0.4-signal-collection\"") !=
                std::string::npos &&
            json.find("\"completion_tick\": 1491") != std::string::npos &&
            json.find("\"final_status\": \"complete\"") !=
                std::string::npos &&
            json.find("\"world_delta_kind\": \"collected\"") !=
                std::string::npos &&
            json.find("\"presentation\": \"ansi\"") !=
                std::string::npos,
        "signal navigation JSON must retain its versioned exact fields");
}

auto planet_descriptor_contract() -> void {
  constexpr std::array streams{
      PlanetDescriptorStream::name,       PlanetDescriptorStream::physical,
      PlanetDescriptorStream::atmosphere, PlanetDescriptorStream::terrain,
      PlanetDescriptorStream::hydrology,  PlanetDescriptorStream::palette,
  };

  check(kPlanetGeneratorVersion == 1,
        "planet generator version 1 must remain stable");
  std::array<std::uint64_t, streams.size()> stream_seeds{};
  for (std::size_t index = 0; index < streams.size(); ++index) {
    const auto first = derive_planet_stream_seed(Seed{42}, streams[index]);
    const auto again = derive_planet_stream_seed(Seed{42}, streams[index]);
    check(first == again,
          "equal planet stream identities must derive the same seed");
    stream_seeds[index] = first.value;
  }
  auto sorted_stream_seeds = stream_seeds;
  std::ranges::sort(sorted_stream_seeds);
  check(std::adjacent_find(sorted_stream_seeds.begin(),
                           sorted_stream_seeds.end()) ==
            sorted_stream_seeds.end(),
        "named planet descriptor streams must remain independent");
  check(stream_seeds ==
            std::array<std::uint64_t, streams.size()>{
                4137554858639612274ULL,
                1905239451672022865ULL,
                18119668118413985072ULL,
                15299131893477559319ULL,
                13066816486509969910ULL,
                10834501079542380501ULL,
            },
        "named planet descriptor streams must retain their golden vectors");

  for (const auto seed :
       std::array{Seed{0}, Seed{42},
                  Seed{std::numeric_limits<std::uint64_t>::max()}}) {
    const auto first = generate_planet_descriptor(seed);
    const auto again = generate_planet_descriptor(seed);
    check(first == again,
          "equal planet seeds must reproduce identical descriptors");
    check(first.seed == seed && first.id == PlanetId{seed.value},
          "a planet descriptor must retain its authoritative identity");
    check(first.radius.value >= PlanetRadiusKm::min &&
              first.radius.value <= PlanetRadiusKm::max,
          "generated planet radius must stay inside its domain");
    check(first.surface_gravity.value >= SurfaceGravityMilliG::min &&
              first.surface_gravity.value <= SurfaceGravityMilliG::max,
          "generated surface gravity must stay inside its domain");
    check(first.atmosphere_pressure.value >= AtmospherePressureMillibars::min &&
              first.atmosphere_pressure.value <=
                  AtmospherePressureMillibars::max,
          "generated atmospheric pressure must stay inside its domain");
    check(first.water_coverage.value >= WaterCoverageBasisPoints::min &&
              first.water_coverage.value <= WaterCoverageBasisPoints::max,
          "generated water coverage must stay inside its domain");
    check(!first.display_name.empty() && first.display_name.size() <= 10 &&
              first.display_name.front() >= 'A' &&
              first.display_name.front() <= 'Z' &&
              std::ranges::all_of(
                  first.display_name.substr(1),
                  [](char value) { return value >= 'a' && value <= 'z'; }),
          "generated planet names must use the bounded ASCII contract");
    check((first.atmosphere_class == AtmosphereClass::airless) ==
              (first.atmosphere_pressure.value == 0),
          "only airless planets may have zero atmospheric pressure");
  }

  constexpr PlanetPalette kAlienPalette{
      PaletteFamily::alien, {100, 78, 153}, {31, 38, 91},   {50, 105, 133},
      {71, 123, 103},       {113, 75, 125}, {211, 179, 221}};
  constexpr PlanetPalette kGlacialPalette{
      PaletteFamily::glacial, {126, 169, 207}, {29, 69, 112},  {76, 139, 172},
      {145, 172, 177},        {189, 207, 208}, {238, 246, 244}};
  check(generate_planet_descriptor(Seed{0}) ==
            PlanetDescriptor{
                Seed{0}, PlanetId{0}, "Soltaon", PlanetRadiusKm{5'096},
                SurfaceGravityMilliG{1'491}, AtmosphereClass::temperate,
                AtmospherePressureMillibars{331}, TerrainCharacter::rugged,
                WaterCoverageBasisPoints{4'478}, kAlienPalette},
        "the zero planet seed must retain its golden descriptor");
  const auto golden = generate_planet_descriptor(Seed{42});
  check(golden ==
            PlanetDescriptor{
                Seed{42}, PlanetId{42}, "Carayx", PlanetRadiusKm{5'499},
                SurfaceGravityMilliG{1'389}, AtmosphereClass::dense,
                AtmospherePressureMillibars{1'561}, TerrainCharacter::volcanic,
                WaterCoverageBasisPoints{2'953}, kAlienPalette},
        "planet seed 42 must retain its golden descriptor");
  check(generate_planet_descriptor(
            Seed{std::numeric_limits<std::uint64_t>::max()}) ==
            PlanetDescriptor{
                Seed{std::numeric_limits<std::uint64_t>::max()},
                PlanetId{std::numeric_limits<std::uint64_t>::max()}, "Nyceune",
                PlanetRadiusKm{6'059}, SurfaceGravityMilliG{1'352},
                AtmosphereClass::dense, AtmospherePressureMillibars{1'869},
                TerrainCharacter::volcanic, WaterCoverageBasisPoints{9'998},
                kGlacialPalette},
        "the maximum planet seed must retain its golden descriptor");

  constexpr std::string_view kGoldenJson = R"json({
  "schema_version": 1,
  "generator_version": 1,
  "planet_seed": "42",
  "planet_id": "planet-000000000000002a",
  "display_name": "Carayx",
  "radius_km": 5499,
  "surface_gravity_milli_g": 1389,
  "atmosphere": {"class": "dense", "pressure_millibars": 1561},
  "terrain_character": "volcanic",
  "water_coverage_basis_points": 2953,
  "palette": {
    "family": "alien",
    "atmosphere": "#644e99",
    "deep_water": "#1f265b",
    "shallow_water": "#326985",
    "lowland": "#477b67",
    "highland": "#714b7d",
    "peak": "#d3b3dd"
  }
}
)json";
  check(planet_descriptor_json(golden) == kGoldenJson,
        "planet diagnostics must retain their version 1 representation");
}

auto planet_descriptor_population() -> void {
  constexpr std::size_t kPopulation{4'096};
  std::array<std::size_t, 4> atmosphere_counts{};
  std::array<std::size_t, 5> terrain_counts{};
  std::array<std::size_t, 5> palette_counts{};
  std::array<std::size_t, 3> water_bands{};
  auto checksum = std::uint64_t{14695981039346656037ULL};

  for (std::uint64_t seed = 0; seed < kPopulation; ++seed) {
    const auto descriptor = generate_planet_descriptor(Seed{seed});
    const auto atmosphere_index =
        static_cast<std::size_t>(descriptor.atmosphere_class);
    const auto terrain_index =
        static_cast<std::size_t>(descriptor.terrain_character);
    const auto palette_index =
        static_cast<std::size_t>(descriptor.palette.family);
    const auto categories_valid = atmosphere_index < atmosphere_counts.size() &&
                                  terrain_index < terrain_counts.size() &&
                                  palette_index < palette_counts.size();
    check(categories_valid,
          "the planet sweep must not generate an invalid category");
    if (!categories_valid)
      continue;
    ++atmosphere_counts[atmosphere_index];
    ++terrain_counts[terrain_index];
    ++palette_counts[palette_index];

    check(descriptor.radius.value >= PlanetRadiusKm::min &&
              descriptor.radius.value <= PlanetRadiusKm::max &&
              descriptor.surface_gravity.value >= SurfaceGravityMilliG::min &&
              descriptor.surface_gravity.value <= SurfaceGravityMilliG::max &&
              descriptor.atmosphere_pressure.value >=
                  AtmospherePressureMillibars::min &&
              descriptor.atmosphere_pressure.value <=
                  AtmospherePressureMillibars::max &&
              descriptor.water_coverage.value >=
                  WaterCoverageBasisPoints::min &&
              descriptor.water_coverage.value <= WaterCoverageBasisPoints::max,
          "the planet sweep must keep every measurement inside its domain");
    const auto pressure = descriptor.atmosphere_pressure.value;
    const auto pressure_matches_class =
        (descriptor.atmosphere_class == AtmosphereClass::airless &&
         pressure == 0) ||
        (descriptor.atmosphere_class == AtmosphereClass::tenuous &&
         pressure >= 1 && pressure <= 249) ||
        (descriptor.atmosphere_class == AtmosphereClass::temperate &&
         pressure >= 250 && pressure <= 1'499) ||
        (descriptor.atmosphere_class == AtmosphereClass::dense &&
         pressure >= 1'500 && pressure <= 2'500);
    check(pressure_matches_class,
          "the planet sweep must keep pressure consistent with its class");
    if (descriptor.water_coverage.value < 3'334) {
      ++water_bands[0];
    } else if (descriptor.water_coverage.value < 6'667) {
      ++water_bands[1];
    } else {
      ++water_bands[2];
    }

    for (const auto byte : planet_descriptor_json(descriptor)) {
      checksum ^= static_cast<std::uint8_t>(byte);
      checksum *= 1099511628211ULL;
    }
  }

  check(std::ranges::all_of(atmosphere_counts,
                            [](std::size_t count) { return count != 0; }),
        "the planet sweep must cover every atmosphere class");
  check(std::ranges::all_of(terrain_counts,
                            [](std::size_t count) { return count != 0; }),
        "the planet sweep must cover every terrain character");
  check(std::ranges::all_of(palette_counts,
                            [](std::size_t count) { return count != 0; }),
        "the planet sweep must cover every palette family");
  check(std::ranges::all_of(water_bands,
                            [](std::size_t count) { return count != 0; }),
        "the planet sweep must cover low, medium, and high water worlds");

  check(checksum == 1927494117462691802ULL,
        "the bounded planet population must retain its aggregate checksum");
  check(atmosphere_counts == std::array<std::size_t, 4>{447, 897, 1'950, 802},
        "the bounded planet population must retain atmosphere counts");
  check(terrain_counts == std::array<std::size_t, 5>{844, 790, 816, 824, 822},
        "the bounded planet population must retain terrain counts");
  check(palette_counts == std::array<std::size_t, 5>{808, 812, 831, 807, 838},
        "the bounded planet population must retain palette counts");
  check(water_bands == std::array<std::size_t, 3>{1'305, 1'413, 1'378},
        "the bounded planet population must retain water-band counts");
}

auto terrain_tile_failure_matrix() -> void {
  const auto planet = generate_planet_descriptor(Seed{42});
  const TerrainTileKey valid_key{planet.id, CubeFace::positive_x, 2, 1, 2};
  const auto valid = generate_terrain_tile(planet, valid_key);
  check(valid.has_value(), "a valid terrain tile key must generate");
  if (valid) {
    check(valid->key() == valid_key &&
              valid->samples().size() == kTerrainTileSampleCount,
          "a generated terrain tile must retain its identity and sample grid");
    check(valid->sample_at(0, 0).has_value() &&
              valid->sample_at(kTerrainTileSamplesPerAxis - 1,
                               kTerrainTileSamplesPerAxis - 1)
                  .has_value(),
          "both inclusive terrain tile sample boundaries must be readable");
    check(valid->sample_at(kTerrainTileSamplesPerAxis, 0) ==
                  std::unexpected{
                      TerrainTileError::invalid_sample_coordinate} &&
              valid->sample_at(0, kTerrainTileSamplesPerAxis) ==
                  std::unexpected{
                      TerrainTileError::invalid_sample_coordinate},
          "terrain tile sample coordinates beyond either axis must fail");
  }

  const TerrainTileKey wrong_planet{
      PlanetId{planet.id.value + 1U}, CubeFace::positive_x, 2, 1, 2};
  const TerrainTileKey invalid_face{
      planet.id, static_cast<CubeFace>(255), 2, 1, 2};
  const TerrainTileKey invalid_lod{
      planet.id, CubeFace::positive_x,
      static_cast<std::uint8_t>(kMaxTerrainLod + 1U), 0, 0};
  const TerrainTileKey invalid_x{planet.id, CubeFace::positive_x, 2, 4, 0};
  const TerrainTileKey invalid_y{planet.id, CubeFace::positive_x, 2, 0, 4};
  const TerrainTileKey overflowing{
      planet.id, CubeFace::positive_x, kMaxTerrainLod,
      std::numeric_limits<std::uint32_t>::max(),
      std::numeric_limits<std::uint32_t>::max()};
  check(generate_terrain_tile(planet, wrong_planet) ==
            std::unexpected{TerrainTileError::wrong_planet},
        "a terrain key from another planet must be rejected");
  check(generate_terrain_tile(planet, invalid_face) ==
            std::unexpected{TerrainTileError::invalid_cube_face},
        "an unknown terrain cube face must be rejected");
  check(generate_terrain_tile(planet, invalid_lod) ==
            std::unexpected{TerrainTileError::invalid_lod},
        "a terrain LOD above the coordinate contract must be rejected");
  check(generate_terrain_tile(planet, invalid_x) ==
                std::unexpected{TerrainTileError::invalid_tile_index} &&
            generate_terrain_tile(planet, invalid_y) ==
                std::unexpected{TerrainTileError::invalid_tile_index} &&
            generate_terrain_tile(planet, overflowing) ==
                std::unexpected{TerrainTileError::invalid_tile_index},
        "out-of-range and overflowing terrain tile indices must be rejected");

  const auto malformed = planet_with_radius(planet, 0);
  check(generate_terrain_tile(malformed, valid_key) ==
            std::unexpected{TerrainTileError::invalid_planet},
        "a malformed planet descriptor must not generate terrain");
  check(TerrainTileCache::create(0) ==
            std::unexpected{TerrainTileError::invalid_cache_capacity},
        "a zero-capacity terrain cache must be rejected");
}

auto deterministic_terrain_tiles() -> void {
  constexpr std::array streams{TerrainGenerationStream::shape,
                               TerrainGenerationStream::detail};
  std::array<std::uint64_t, streams.size()> stream_seeds{};
  for (std::size_t index = 0; index < streams.size(); ++index) {
    stream_seeds[index] =
        derive_terrain_generation_seed(Seed{42}, streams[index]).value;
    check(derive_terrain_generation_seed(Seed{42}, streams[index]) ==
              derive_terrain_generation_seed(Seed{42}, streams[index]),
          "named terrain generation streams must be stable");
  }
  check(stream_seeds[0] != stream_seeds[1],
        "terrain shape and detail streams must remain independent");
  check(stream_seeds ==
            std::array<std::uint64_t, streams.size()>{
                12495169707215482604ULL, 745371854408699215ULL},
        "named terrain streams must retain their golden vectors");

  struct GoldenTile {
    std::uint64_t seed;
    CubeFace face;
    std::uint8_t lod;
    std::uint32_t x;
    std::uint32_t y;
    std::uint64_t checksum;
  };
  constexpr std::array golden_tiles{
      GoldenTile{0, CubeFace::positive_x, 0, 0, 0,
                 9797442332981214159ULL},
      GoldenTile{42, CubeFace::positive_z, 4, 7, 11,
                 743763593216380847ULL},
      GoldenTile{std::numeric_limits<std::uint64_t>::max(),
                 CubeFace::negative_y, kMaxTerrainLod, 65'535, 0,
                 6857593874197516006ULL},
  };
  std::array<std::uint64_t, golden_tiles.size()> observed{};
  for (std::size_t index = 0; index < golden_tiles.size(); ++index) {
    const auto fixture = golden_tiles[index];
    const auto planet = generate_planet_descriptor(Seed{fixture.seed});
    const TerrainTileKey key{planet.id, fixture.face, fixture.lod, fixture.x,
                             fixture.y};
    const auto first = generate_terrain_tile(planet, key);
    const auto second = generate_terrain_tile(planet, key);
    check(first && second && first->samples() == second->samples(),
          "equal planet and tile identities must regenerate every sample");
    if (!first || !second) continue;
    observed[index] = first->checksum();
    if (observed[index] != fixture.checksum) {
      std::fprintf(stderr, "golden terrain tile %zu checksum: %llu\n", index,
                   static_cast<unsigned long long>(observed[index]));
    }
    check(observed[index] == second->checksum(),
          "regenerated terrain tile checksums must agree");
    check(observed[index] == fixture.checksum,
          "terrain tiles must retain their version 1 golden checksums");
  }
  check(observed[0] != observed[1] && observed[1] != observed[2] &&
            observed[0] != observed[2],
        "different planet and tile identities must produce different terrain");

  const auto dry_source = generate_planet_descriptor(Seed{42});
  const auto dry = planet_with_water(dry_source, WaterCoverageBasisPoints::min);
  const auto wet = planet_with_water(dry_source, WaterCoverageBasisPoints::max);
  const TerrainTileKey dry_key{dry.id, CubeFace::positive_x, 0, 0, 0};
  const auto dry_tile = generate_terrain_tile(dry, dry_key);
  const auto wet_tile = generate_terrain_tile(wet, dry_key);
  check(dry_tile && std::ranges::all_of(dry_tile->samples(), [](auto sample) {
          return sample.elevation_metres > 0;
        }),
        "a zero-water descriptor must not generate submerged samples");
  check(wet_tile && std::ranges::all_of(wet_tile->samples(), [](auto sample) {
          return sample.elevation_metres < 0;
        }),
        "a full-water descriptor must not generate exposed samples");
}

auto terrain_tile_seam_contract() -> void {
  const auto planet = generate_planet_descriptor(Seed{42});
  constexpr std::size_t last{kTerrainTileSamplesPerAxis - 1};

  const auto left = generate_terrain_tile(
      planet, {planet.id, CubeFace::positive_x, 2, 1, 2});
  const auto right = generate_terrain_tile(
      planet, {planet.id, CubeFace::positive_x, 2, 2, 2});
  const auto above = generate_terrain_tile(
      planet, {planet.id, CubeFace::positive_x, 2, 1, 3});
  check(left && right && above,
        "same-face terrain seam fixtures must generate");
  if (left && right && above) {
    for (std::size_t sample = 0; sample < kTerrainTileSamplesPerAxis;
         ++sample) {
      check(left->sample_at(last, sample) == right->sample_at(0, sample),
            "horizontal same-face terrain neighbors must share samples");
      check(left->sample_at(sample, last) == above->sample_at(sample, 0),
            "vertical same-face terrain neighbors must share samples");
    }
  }

  std::vector<TerrainTile> faces;
  faces.reserve(6);
  for (std::uint8_t face = 0; face < 6; ++face) {
    auto generated = generate_terrain_tile(
        planet, {planet.id, static_cast<CubeFace>(face), 0, 0, 0});
    check(generated.has_value(), "every cube face terrain fixture must generate");
    if (generated) faces.push_back(std::move(*generated));
  }
  if (faces.size() == 6) {
    const auto edge_coordinate = [last](std::size_t edge,
                                        std::size_t sample) {
      switch (edge) {
        case 0: return std::pair{std::size_t{0}, sample};
        case 1: return std::pair{last, sample};
        case 2: return std::pair{sample, std::size_t{0}};
        default: return std::pair{sample, last};
      }
    };
    for (std::size_t face = 0; face < faces.size(); ++face) {
      for (std::size_t edge = 0; edge < 4; ++edge) {
        for (std::size_t sample = 0; sample <= last; ++sample) {
          const auto [x, y] = edge_coordinate(edge, sample);
          const TerrainTileAddress source_address{
              {planet.id, static_cast<CubeFace>(face), 0, 0, 0},
              static_cast<double>(x) / static_cast<double>(last),
              static_cast<double>(y) / static_cast<double>(last)};
          const auto source_position =
              planet_fixed_from_terrain_address(planet, source_address);
          bool matched{};
          for (std::size_t other_face = 0;
               other_face < faces.size() && !matched; ++other_face) {
            if (other_face == face) continue;
            for (std::size_t other_edge = 0; other_edge < 4 && !matched;
                 ++other_edge) {
              for (std::size_t other_sample = 0; other_sample <= last;
                   ++other_sample) {
                const auto [other_x, other_y] =
                    edge_coordinate(other_edge, other_sample);
                const TerrainTileAddress other_address{
                    {planet.id, static_cast<CubeFace>(other_face), 0, 0, 0},
                    static_cast<double>(other_x) / static_cast<double>(last),
                    static_cast<double>(other_y) /
                        static_cast<double>(last)};
                const auto other_position =
                    planet_fixed_from_terrain_address(planet, other_address);
                if (source_position && other_position &&
                    close_position(*source_position, *other_position)) {
                  check(faces[face].sample_at(x, y) ==
                            faces[other_face].sample_at(other_x, other_y),
                        "cube-face terrain seams and corners must share samples");
                  matched = true;
                  break;
                }
              }
            }
          }
          check(matched,
                "every cube-face edge sample must have an adjacent-face peer");
        }
      }
    }
  }

  const TerrainTileKey parent_key{
      planet.id, CubeFace::negative_z, 2, 1, 2};
  const auto parent = generate_terrain_tile(planet, parent_key);
  std::array<std::expected<TerrainTile, TerrainTileError>, 4> children{
      generate_terrain_tile(
          planet, {planet.id, parent_key.face, 3, 2, 4}),
      generate_terrain_tile(
          planet, {planet.id, parent_key.face, 3, 3, 4}),
      generate_terrain_tile(
          planet, {planet.id, parent_key.face, 3, 2, 5}),
      generate_terrain_tile(
          planet, {planet.id, parent_key.face, 3, 3, 5}),
  };
  check(parent && std::ranges::all_of(children, [](const auto& child) {
          return child.has_value();
        }),
        "cross-LOD terrain seam fixtures must generate");
  if (parent && std::ranges::all_of(children, [](const auto& child) {
        return child.has_value();
      })) {
    constexpr std::size_t half{kTerrainTileIntervalsPerAxis / 2};
    for (std::size_t y = 0; y <= last; ++y) {
      for (std::size_t x = 0; x <= last; ++x) {
        const auto child_x = x < half ? std::size_t{0} : std::size_t{1};
        const auto child_y = y < half ? std::size_t{0} : std::size_t{1};
        const auto local_x = (x - child_x * half) * 2;
        const auto local_y = (y - child_y * half) * 2;
        const auto& child = children[child_y * 2 + child_x].value();
        check(parent->sample_at(x, y) == child.sample_at(local_x, local_y),
              "aligned parent and child LOD samples must be identical");
      }
    }
  }
}

auto terrain_tile_cache_contract() -> void {
  const auto planet = generate_planet_descriptor(Seed{42});
  auto cache = TerrainTileCache::create(2);
  check(cache && cache->capacity() == 2 && cache->size() == 0,
        "a terrain cache must retain its validated capacity");
  if (!cache) return;

  const TerrainTileKey first_key{
      planet.id, CubeFace::positive_x, 2, 0, 0};
  const TerrainTileKey second_key{
      planet.id, CubeFace::positive_x, 2, 1, 0};
  const TerrainTileKey third_key{
      planet.id, CubeFace::positive_x, 2, 2, 0};
  const auto first = cache->get(planet, first_key);
  const auto second = cache->get(planet, second_key);
  const auto first_hit = cache->get(planet, first_key);
  check(first && second && first_hit && *first == *first_hit,
        "a terrain cache hit must return the resident immutable tile");
  check(cache->size() == 2 && cache->contains(first_key) &&
            cache->contains(second_key),
        "terrain cache hits must not change bounded entry count");

  const auto third = cache->get(planet, third_key);
  check(third && cache->size() == 2 && cache->contains(first_key) &&
            cache->contains(third_key) && !cache->contains(second_key),
        "terrain cache insertion must evict the least recently used tile");
  const auto second_again = cache->get(planet, second_key);
  check(second && second_again && *second != *second_again &&
            (*second)->checksum() == (*second_again)->checksum(),
        "an evicted terrain tile must regenerate with the same checksum");
  check(cache->size() == cache->capacity(),
        "terrain cache regeneration must remain inside capacity");

  const auto before_size = cache->size();
  const TerrainTileKey invalid{
      planet.id, CubeFace::positive_x, 2,
      std::numeric_limits<std::uint32_t>::max(), 0};
  check(cache->get(planet, invalid) ==
            std::unexpected{TerrainTileError::invalid_tile_index},
        "a cache miss with an invalid key must return the generator error");
  check(cache->size() == before_size,
        "failed terrain generation must leave cache state unchanged");

  const auto conflicting_planet = planet_with_water(
      planet, static_cast<std::uint16_t>(planet.water_coverage.value + 1U));
  check(cache->get(conflicting_planet, second_key) ==
            std::unexpected{TerrainTileError::invalid_planet},
        "one cached planet identity must reject a conflicting descriptor");
  check(cache->size() == before_size,
        "a conflicting cached descriptor must leave cache state unchanged");

  auto sampling_cache = TerrainTileCache::create(4);
  auto sampler = sampling_cache
                     ? TerrainSurfaceSampler::create(planet, 6,
                                                     *sampling_cache)
                     : std::expected<TerrainSurfaceSampler, TerrainTileError>{
                           std::unexpected{
                               TerrainTileError::invalid_cache_capacity}};
  const auto sample_position =
      planet_fixed_from_geodetic(planet, {0.35, -0.65, 0.0});
  const auto one_shot = sample_position && sampling_cache
                            ? sample_planet_surface(planet, *sample_position, 6,
                                                    *sampling_cache)
                            : std::expected<TerrainSurfaceSample,
                                            TerrainTileError>{std::unexpected{
                                  TerrainTileError::coordinate_failure}};
  const auto prepared = sampler && sample_position
                            ? sampler->sample(*sample_position)
                            : std::expected<TerrainSurfaceSample,
                                            TerrainTileError>{std::unexpected{
                                  TerrainTileError::coordinate_failure}};
  const auto by_direction = sampler && sample_position
                                ? sampler->sample_direction(
                                      {sample_position->x, sample_position->y,
                                       sample_position->z})
                                : std::expected<TerrainSurfaceSample,
                                                TerrainTileError>{
                                      std::unexpected{
                                          TerrainTileError::coordinate_failure}};
  check(one_shot && prepared && by_direction && *one_shot == *prepared &&
            *prepared == *by_direction && sampler->tiles_touched() == 1,
        "a prepared surface sampler must preserve samples while pinning each tile once");
  check(sampler &&
            !sampler->sample_direction(
                {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}) &&
            TerrainSurfaceSampler::create(planet, kMaxTerrainLod + 1,
                                          *sampling_cache) ==
                std::unexpected{TerrainTileError::invalid_lod},
        "prepared surface sampling must reject invalid directions and LODs");
}

auto coordinate_and_lod_contract() -> void {
  constexpr double pi{std::numbers::pi_v<double>};
  constexpr double half_pi{pi / 2.0};
  const auto generated = generate_planet_descriptor(Seed{42});
  const auto planet = planet_with_radius(generated, 5'499);
  constexpr double radius{5'499'000.0};

  const auto prime =
      planet_fixed_from_geodetic(planet, {0.0, 0.0, 0.0});
  const auto east =
      planet_fixed_from_geodetic(planet, {0.0, half_pi, 1'000.0});
  const auto north =
      planet_fixed_from_geodetic(planet, {half_pi, 1.234, 0.0});
  const auto south =
      planet_fixed_from_geodetic(planet, {-half_pi, -2.5, 0.0});
  const auto antimeridian =
      planet_fixed_from_geodetic(planet, {0.0, pi, 0.0});
  check(prime && close_position(*prime, {radius, 0.0, 0.0}),
        "the prime meridian must map to planet-fixed positive x");
  check(east && close_position(*east, {0.0, radius + 1'000.0, 0.0}),
        "east longitude must map to planet-fixed positive y");
  check(north && close_position(*north, {0.0, 0.0, radius}),
        "the north pole must map exactly to planet-fixed positive z");
  check(south && close_position(*south, {0.0, 0.0, -radius}),
        "the south pole must map exactly to planet-fixed negative z");
  check(antimeridian &&
            close_position(*antimeridian, {-radius, 0.0, 0.0}),
        "positive pi must alias the canonical antimeridian");

  if (north && south && antimeridian) {
    const auto north_geodetic =
        geodetic_from_planet_fixed(planet, *north);
    const auto south_geodetic =
        geodetic_from_planet_fixed(planet, *south);
    const auto anti_geodetic =
        geodetic_from_planet_fixed(planet, *antimeridian);
    check(north_geodetic && north_geodetic->longitude_radians == 0.0 &&
              close_enough(north_geodetic->latitude_radians, half_pi),
          "the north pole must have canonical zero longitude");
    check(south_geodetic && south_geodetic->longitude_radians == 0.0 &&
              close_enough(south_geodetic->latitude_radians, -half_pi),
          "the south pole must have canonical zero longitude");
    check(anti_geodetic &&
              close_enough(anti_geodetic->longitude_radians, -pi),
          "the inverse antimeridian must use negative pi");
  }

  for (const auto radius_km :
       std::array<std::uint32_t, 3>{PlanetRadiusKm::min, 5'499,
                                    PlanetRadiusKm::max}) {
    const auto sized_planet = planet_with_radius(generated, radius_km);
    for (const auto geodetic :
         std::array{GeodeticPosition{0.0, 0.0, 0.0},
                    GeodeticPosition{0.61, -2.4, 12'345.0},
                    GeodeticPosition{-0.93, 2.8, -1'000.0},
                    GeodeticPosition{1.2, 7.0, 250'000.0}}) {
      const auto fixed =
          planet_fixed_from_geodetic(sized_planet, geodetic);
      check(fixed.has_value(),
            "valid geodetic samples must map to planet-fixed space");
      if (!fixed) continue;
      const auto round_trip =
          geodetic_from_planet_fixed(sized_planet, *fixed);
      check(round_trip.has_value(),
            "valid planet-fixed samples must map back to geodetic space");
      if (!round_trip) continue;
      const auto canonical_longitude =
          std::remainder(geodetic.longitude_radians, 2.0 * pi);
      check(close_enough(round_trip->latitude_radians,
                         geodetic.latitude_radians) &&
                close_enough(round_trip->longitude_radians,
                             canonical_longitude) &&
                close_enough(round_trip->altitude_metres,
                             geodetic.altitude_metres, 1.0e-6),
            "geodetic round trips must stay inside documented tolerances");
      const auto fixed_again =
          planet_fixed_from_geodetic(sized_planet, *round_trip);
      check(fixed_again && close_position(*fixed, *fixed_again),
            "planet-fixed round trips must stay inside metre tolerance");
    }
  }

  const auto equatorial_frame =
      make_local_tangent_frame(planet, {0.0, 0.0, 100.0});
  const auto polar_frame =
      make_local_tangent_frame(planet, {half_pi, 2.0, 0.0});
  for (const auto* frame :
       std::array<const std::expected<LocalTangentFrame, CoordinateError>*, 2>{
           &equatorial_frame, &polar_frame}) {
    check(frame->has_value(),
          "equatorial and polar local tangent frames must be valid");
    if (!*frame) continue;
    for (const auto local :
         std::array{LocalPositionMetres{},
                    LocalPositionMetres{125.5, -48.25, 2.0},
                    LocalPositionMetres{-20'000.0, 30'000.0, 4'000.0}}) {
      const auto fixed = planet_fixed_from_local(**frame, local);
      check(fixed.has_value(), "valid ENU positions must map to planet space");
      if (!fixed) continue;
      const auto round_trip = local_from_planet_fixed(**frame, *fixed);
      check(round_trip && close_enough(round_trip->east, local.east, 1.0e-6) &&
                close_enough(round_trip->north, local.north, 1.0e-6) &&
                close_enough(round_trip->up, local.up, 1.0e-6),
            "local ENU round trips must stay inside metre tolerance");
    }
  }
  if (polar_frame) {
    check(polar_frame->east == PlanetFixedDirection{0.0, 1.0, 0.0} &&
              polar_frame->north ==
                  PlanetFixedDirection{-1.0, 0.0, 0.0},
          "the north-pole tangent frame must use canonical zero longitude");
  }

  constexpr std::array face_centers{
      std::pair{CubeFace::positive_x,
                PlanetFixedPositionMetres{1.0, 0.0, 0.0}},
      std::pair{CubeFace::negative_x,
                PlanetFixedPositionMetres{-1.0, 0.0, 0.0}},
      std::pair{CubeFace::positive_y,
                PlanetFixedPositionMetres{0.0, 1.0, 0.0}},
      std::pair{CubeFace::negative_y,
                PlanetFixedPositionMetres{0.0, -1.0, 0.0}},
      std::pair{CubeFace::positive_z,
                PlanetFixedPositionMetres{0.0, 0.0, 1.0}},
      std::pair{CubeFace::negative_z,
                PlanetFixedPositionMetres{0.0, 0.0, -1.0}},
  };
  for (const auto& [face, center] : face_centers) {
    const auto address = terrain_address_from_planet_fixed(planet, center, 0);
    check(address && address->tile ==
                         TerrainTileKey{planet.id, face, 0, 0, 0} &&
              address->u == 0.5 && address->v == 0.5,
          "every cube face center must retain its canonical address");
    if (!address) continue;
    const auto inverse =
        planet_fixed_from_terrain_address(planet, *address);
    check(inverse && close_enough(inverse->x / radius, center.x) &&
              close_enough(inverse->y / radius, center.y) &&
              close_enough(inverse->z / radius, center.z),
          "cube face center inverse mappings must preserve direction");
  }

  const auto seam = terrain_address_from_planet_fixed(
      planet, {1.0, 1.0, 0.0}, 0);
  const auto corner = terrain_address_from_planet_fixed(
      planet, {1.0, 1.0, 1.0}, 0);
  check(seam && seam->tile.face == CubeFace::positive_x && seam->u == 1.0 &&
            seam->v == 0.5,
        "an x/y seam tie must choose x and preserve the outer edge");
  check(corner && corner->tile.face == CubeFace::positive_x &&
            corner->u == 1.0 && corner->v == 1.0,
        "a cube corner tie must choose x and preserve both outer edges");

  constexpr std::array seam_directions{
      PlanetFixedPositionMetres{1.0, 1.0, 0.0},
      PlanetFixedPositionMetres{1.0, -1.0, 0.0},
      PlanetFixedPositionMetres{-1.0, 1.0, 0.0},
      PlanetFixedPositionMetres{-1.0, -1.0, 0.0},
      PlanetFixedPositionMetres{1.0, 0.0, 1.0},
      PlanetFixedPositionMetres{1.0, 0.0, -1.0},
      PlanetFixedPositionMetres{-1.0, 0.0, 1.0},
      PlanetFixedPositionMetres{-1.0, 0.0, -1.0},
      PlanetFixedPositionMetres{0.0, 1.0, 1.0},
      PlanetFixedPositionMetres{0.0, 1.0, -1.0},
      PlanetFixedPositionMetres{0.0, -1.0, 1.0},
      PlanetFixedPositionMetres{0.0, -1.0, -1.0},
  };
  for (const auto direction : seam_directions) {
    const auto address =
        terrain_address_from_planet_fixed(planet, direction, 0);
    check(address.has_value(),
          "every physical cube seam must have an address");
    if (!address) continue;
    const auto inverse =
        planet_fixed_from_terrain_address(planet, *address);
    const auto source_length =
        std::hypot(direction.x, direction.y, direction.z);
    check(inverse && close_enough(inverse->x / radius,
                                  direction.x / source_length) &&
              close_enough(inverse->y / radius,
                           direction.y / source_length) &&
              close_enough(inverse->z / radius,
                           direction.z / source_length),
          "every physical cube seam must preserve direction");
  }
  for (const auto x : {-1.0, 1.0}) {
    for (const auto y : {-1.0, 1.0}) {
      for (const auto z : {-1.0, 1.0}) {
        const auto address =
            terrain_address_from_planet_fixed(planet, {x, y, z}, 0);
        check(address &&
                  address->tile.face ==
                      (x < 0.0 ? CubeFace::negative_x
                               : CubeFace::positive_x),
              "every cube corner must follow the x-axis tie rule");
      }
    }
  }

  const auto internal_boundary = terrain_address_from_planet_fixed(
      planet, {1.0, -0.5, 0.0}, 2);
  check(internal_boundary && internal_boundary->tile ==
                                 TerrainTileKey{planet.id,
                                                CubeFace::positive_x, 2, 1, 2} &&
            internal_boundary->u == 0.0 && internal_boundary->v == 0.0,
        "exact internal boundaries must belong to the higher tile index");
  const auto outer_boundary = terrain_address_from_planet_fixed(
      planet, {1.0, 1.0, 0.0}, 2);
  check(outer_boundary && outer_boundary->tile.x == 3 &&
            outer_boundary->u == 1.0,
        "outer face boundaries must remain on the final tile at one");

  if (seam) {
    const TerrainTileAddress adjacent{
        {planet.id, CubeFace::positive_y, 2, 0, 2}, 0.0, 0.0};
    const auto canonical_fixed =
        planet_fixed_from_terrain_address(planet, *seam);
    const auto adjacent_fixed =
        planet_fixed_from_terrain_address(planet, adjacent);
    check(canonical_fixed && adjacent_fixed &&
              close_position(*canonical_fixed, *adjacent_fixed),
          "adjacent cube-face edge addresses must inverse-map identically");
  }

  for (const auto sample :
       std::array{PlanetFixedPositionMetres{1.0, 0.2, -0.4},
                  PlanetFixedPositionMetres{-0.3, -1.0, 0.7},
                  PlanetFixedPositionMetres{0.25, 0.6, 1.0},
                  PlanetFixedPositionMetres{1.0, 1.0, 1.0}}) {
    const auto address =
        terrain_address_from_planet_fixed(planet, sample, 12);
    const auto direction_address = terrain_address_from_planet_direction(
        planet, {sample.x * 17.0, sample.y * 17.0, sample.z * 17.0}, 12);
    check(address.has_value(), "valid directions must produce tile addresses");
    check(direction_address && address && *direction_address == *address,
          "direction addressing must preserve scale-invariant cube coordinates");
    if (!address) continue;
    const auto inverse =
        planet_fixed_from_terrain_address(planet, *address, 2'000.0);
    check(inverse.has_value(), "valid tile addresses must inverse-map");
    if (!inverse) continue;
    const auto sample_length = std::hypot(sample.x, sample.y, sample.z);
    const auto inverse_length = std::hypot(inverse->x, inverse->y, inverse->z);
    check(close_enough(sample.x / sample_length, inverse->x / inverse_length) &&
              close_enough(sample.y / sample_length,
                           inverse->y / inverse_length) &&
              close_enough(sample.z / sample_length,
                           inverse->z / inverse_length),
          "tile address round trips must preserve surface direction");
  }

  auto previous_span = std::numeric_limits<double>::infinity();
  for (std::uint8_t lod = 0; lod <= kMaxTerrainLod; ++lod) {
    const auto span = nominal_terrain_tile_span_metres(planet, lod);
    check(span && *span < previous_span,
          "each terrain LOD must reduce nominal tile span");
    if (!span) continue;
    if (lod != 0) {
      check(close_enough(*span * 2.0, previous_span, 1.0e-6),
            "adjacent terrain LOD spans must differ by exactly two");
    }
    previous_span = *span;
  }
  const auto span_zero = nominal_terrain_tile_span_metres(planet, 0);
  check(span_zero && close_enough(*span_zero, pi * radius / 2.0, 1.0e-6),
        "LOD zero must span one quarter great circle per cube face");
  for (std::uint8_t lod = 0; lod < kMaxTerrainLod; ++lod) {
    const auto span = nominal_terrain_tile_span_metres(planet, lod);
    if (!span) continue;
    const auto threshold = *span / kLodTileSpanMultiplier;
    const auto at_threshold = select_terrain_lod(planet, threshold);
    const auto below_threshold = select_terrain_lod(planet, threshold * 0.999);
    check(at_threshold && *at_threshold == lod,
          "an exact altitude threshold must retain the coarser LOD");
    check(below_threshold && *below_threshold == lod + 1,
          "descending below a threshold must select the next finer LOD");
  }
  check(select_terrain_lod(planet, 0.0) == kMaxTerrainLod,
        "the minimum altitude floor must bound terrain refinement");

  const auto invalid_radius = planet_with_radius(generated, 0);
  const auto quiet_nan = std::numeric_limits<double>::quiet_NaN();
  check(planet_fixed_from_geodetic(invalid_radius, {}) ==
            std::unexpected{CoordinateError::invalid_planet_radius},
        "descriptor radii outside the generated domain must be rejected");
  check(planet_fixed_from_geodetic(planet, {half_pi + 0.01, 0.0, 0.0}) ==
            std::unexpected{CoordinateError::invalid_latitude},
        "latitudes beyond a pole must be rejected");
  check(planet_fixed_from_geodetic(planet, {0.0, 0.0, -radius}) ==
            std::unexpected{CoordinateError::invalid_altitude},
        "the planet center cannot be expressed as geodetic altitude");
  check(geodetic_from_planet_fixed(planet, {}) ==
            std::unexpected{CoordinateError::planet_center},
        "the planet center must not produce arbitrary geodetic angles");
  check(!geodetic_from_planet_fixed(planet, {quiet_nan, 0.0, 0.0}),
        "non-finite planet-fixed positions must be rejected");
  check(!planet_fixed_from_local({}, {}),
        "a malformed local tangent frame must be rejected");
  if (equatorial_frame) {
    auto left_handed = *equatorial_frame;
    left_handed.up = {-left_handed.up.x, -left_handed.up.y,
                      -left_handed.up.z};
    check(!planet_fixed_from_local(left_handed, {}),
          "a left-handed local tangent frame must be rejected");
    check(!planet_fixed_from_local(*equatorial_frame,
                                   {quiet_nan, 0.0, 0.0}),
          "non-finite local positions must be rejected");
  }
  const auto maximum = std::numeric_limits<double>::max();
  check(!geodetic_from_planet_fixed(planet, {maximum, maximum, maximum}),
        "overflowing planet-fixed magnitudes must be rejected");
  check(!terrain_address_from_planet_fixed(planet, {}, 0),
        "the planet center must not produce a terrain address");
  check(!terrain_address_from_planet_fixed(
            planet, {1.0, 0.0, 0.0}, kMaxTerrainLod + 1),
        "terrain addresses above the maximum LOD must be rejected");
  check(terrain_address_from_planet_direction(planet, {}, 0) ==
            std::unexpected{CoordinateError::planet_center} &&
            !terrain_address_from_planet_direction(
                planet, {quiet_nan, 0.0, 0.0}, 0) &&
            !terrain_address_from_planet_direction(
                planet, {1.0, 0.0, 0.0}, kMaxTerrainLod + 1),
        "direction terrain addressing must reject zero, non-finite, and invalid-LOD inputs");

  const TerrainTileAddress invalid_face{
      {planet.id, static_cast<CubeFace>(255), 0, 0, 0}, 0.5, 0.5};
  const TerrainTileAddress invalid_index{
      {planet.id, CubeFace::positive_x, 2, 4, 0}, 0.5, 0.5};
  const TerrainTileAddress invalid_coordinate{
      {planet.id, CubeFace::positive_x, 0, 0, 0}, -0.1, 0.5};
  const TerrainTileAddress wrong_planet{
      {PlanetId{planet.id.value + 1U}, CubeFace::positive_x, 0, 0, 0},
      0.5, 0.5};
  check(planet_fixed_from_terrain_address(planet, invalid_face) ==
            std::unexpected{CoordinateError::invalid_cube_face},
        "unknown cube faces must be rejected");
  check(planet_fixed_from_terrain_address(planet, invalid_index) ==
            std::unexpected{CoordinateError::invalid_tile_index},
        "tile indices outside their LOD must be rejected");
  check(planet_fixed_from_terrain_address(planet, invalid_coordinate) ==
            std::unexpected{CoordinateError::invalid_tile_coordinate},
        "within-tile coordinates outside the unit interval must be rejected");
  check(planet_fixed_from_terrain_address(planet, wrong_planet) ==
            std::unexpected{CoordinateError::wrong_planet},
        "tile addresses from another planet must be rejected");
  check(planet_fixed_from_terrain_address(
            planet, {{planet.id, CubeFace::positive_x, 0, 0, 0}, 0.5, 0.5},
            -radius) == std::unexpected{CoordinateError::invalid_altitude},
        "terrain addresses at the planet center must be rejected");
  check(!planet_fixed_from_terrain_address(
            planet,
            {{planet.id, CubeFace::positive_x, 0, 0, 0}, quiet_nan, 0.5}),
        "non-finite within-tile coordinates must be rejected");
  check(!nominal_terrain_tile_span_metres(planet, kMaxTerrainLod + 1),
        "nominal spans above the maximum LOD must be rejected");
  check(select_terrain_lod(planet, -1.0) ==
            std::unexpected{CoordinateError::invalid_altitude},
        "negative LOD altitudes must be rejected");
  check(select_terrain_lod(planet, quiet_nan) ==
            std::unexpected{CoordinateError::non_finite_input},
        "non-finite LOD altitudes must be rejected");
}

auto render_profile_contract() -> void {
  check(profile_viewport(RenderProfile::remote) == ViewportSize{320, 240},
        "remote profile must remain 320x240");
  check(profile_viewport(RenderProfile::balanced) == ViewportSize{512, 320},
        "balanced profile must remain 512x320");
  check(profile_viewport(RenderProfile::local) == ViewportSize{640, 480},
        "local profile must remain 640x480");
  check(profile_viewport(RenderProfile::cinematic) ==
            ViewportSize{1024, 768},
        "cinematic profile must remain 1024x768");

  check(parse_render_profile("remote") == RenderProfile::remote,
        "remote profile name must parse");
  check(parse_render_profile("balanced") == RenderProfile::balanced,
        "balanced profile name must parse");
  check(parse_render_profile("local") == RenderProfile::local,
        "local profile name must parse");
  check(parse_render_profile("cinematic") == RenderProfile::cinematic,
        "cinematic profile name must parse");
  check(!parse_render_profile("unknown"),
        "unknown profile names must be rejected");

  const auto defaults = default_render_configuration();
  check(defaults.viewport == ViewportSize{640, 480},
        "default viewport must remain 640x480");
  check(profile_name(defaults) == "local",
        "default profile must remain local");
  const auto overridden = resolve_render_configuration(
      RenderProfile::remote, ViewportSize{800, 600});
  check(overridden.viewport == ViewportSize{800, 600},
        "explicit viewport must override a named profile");
  check(profile_name(overridden) == "custom",
        "an explicit viewport must be reported as custom");
}

auto viewport_validation_contract() -> void {
  const auto check_error = [](std::string_view text, ViewportError expected,
                              const char* message) {
    const auto parsed = parse_viewport(text);
    check(!parsed && parsed.error() == expected, message);
  };

  check(parse_viewport("320x240") == ViewportSize{320, 240},
        "a normal viewport must parse");
  check(parse_viewport("800x600") == ViewportSize{800, 600},
        "the high custom viewport must parse");
  check(parse_viewport("1024x768") == ViewportSize{1024, 768},
        "the cinematic viewport must parse");
  check(parse_viewport("4096x1024") == ViewportSize{4096, 1024},
        "the exact pixel budget boundary must parse");

  check_error("", ViewportError::malformed,
              "an empty viewport must be rejected");
  check_error("640", ViewportError::malformed,
              "a viewport without a separator must be rejected");
  check_error("640X480", ViewportError::malformed,
              "the viewport grammar must use lowercase x");
  check_error("640x480x1", ViewportError::malformed,
              "a viewport with multiple separators must be rejected");
  check_error("0x480", ViewportError::non_positive,
              "a zero width must be rejected");
  check_error("640x-1", ViewportError::non_positive,
              "a negative height must be rejected");
  check_error("999999999999999999999999x480",
              ViewportError::numeric_overflow,
              "an overflowing dimension must be rejected");
  check_error("4097x1", ViewportError::dimension_too_large,
              "an overlong axis must be rejected");
  check_error("4096x1025", ViewportError::pixel_budget_exceeded,
              "a viewport above the pixel budget must be rejected");
}

auto cockpit_layout_contract() -> void {
  constexpr ViewportSize viewport{320, 240};
  constexpr termforge::Extent kitty_cell{8, 16};

  for (const auto& [cols, rows] :
       std::array{std::pair{0, 24}, std::pair{-1, 24}, std::pair{80, 0},
                  std::pair{79, 24}, std::pair{80, 23}}) {
    const auto layout =
        compute_cockpit_layout(cols, rows, kitty_cell, viewport);
    check(!layout.supported(),
          "invalid and below-minimum terminals must reject cockpit layout");
    check(layout.viewport.empty(),
          "an unsupported cockpit must not retain a pixel viewport");
  }

  check(!compute_cockpit_layout(80, 24, {0, 16}, viewport).supported(),
        "zero-width cell pixels must reject cockpit layout");
  check(!compute_cockpit_layout(80, 24, {-1, 16}, viewport).supported(),
        "negative cell pixels must reject cockpit layout");
  check(!compute_cockpit_layout(80, 24, kitty_cell, {0, 240}).supported(),
        "an invalid logical viewport must reject cockpit layout");
  check(!compute_cockpit_layout(80, 24, kitty_cell, {-1, 240}).supported(),
        "a negative logical viewport must reject cockpit layout");
  check(!compute_cockpit_layout(65536, 24, kitty_cell, viewport).supported() &&
            !compute_cockpit_layout(80, 24, {65536, 16}, viewport)
                 .supported(),
        "out-of-domain terminal and cell dimensions must reject layout");

  const auto compact =
      compute_cockpit_layout(80, 24, kitty_cell, viewport);
  check(compact.mode == CockpitLayoutMode::compact,
        "the 80x24 target must use compact cockpit layout");
  check(compact.screen == Rect{0, 0, 80, 24},
        "compact layout must retain the full terminal bounds");
  check(compact.left_instruments == Rect{0, 1, 12, 19} &&
            compact.right_instruments == Rect{68, 1, 12, 19},
        "compact layout must reserve symmetric instrument rails");
  check(compact.viewport == Rect{17, 2, 45, 17} &&
            compact.viewport_frame == Rect{16, 1, 47, 19},
        "compact layout must aspect-fit the viewport inside its frame");

  const auto wide = compute_cockpit_layout(120, 40, kitty_cell, viewport);
  check(wide.mode == CockpitLayoutMode::wide,
        "the 120x40 target must use wide cockpit layout");
  check(wide.left_instruments == Rect{0, 1, 18, 35} &&
            wide.right_instruments == Rect{102, 1, 18, 35},
        "wide layout must reserve expanded instrument rails");
  check(wide.viewport == Rect{20, 3, 80, 30} &&
            wide.viewport_frame == Rect{19, 2, 82, 32},
        "wide layout must preserve a framed 4:3 Kitty viewport");

  const auto ansi =
      compute_cockpit_layout(80, 24, {1, 2}, viewport);
  check(ansi.viewport == compact.viewport,
        "ANSI half-block and Kitty cells must share physical aspect layout");
  const auto square_cells =
      compute_cockpit_layout(80, 24, {1, 1}, viewport);
  check(square_cells.viewport == Rect{29, 2, 22, 17},
        "square logical cells must preserve the viewport aspect");

  for (const auto& layout :
       std::array{compact, wide, ansi, square_cells,
                  compute_cockpit_layout(65535, 65535, {65535, 65535},
                                         {4096, 1024})}) {
    check(layout.supported(),
          "valid target and boundary layouts must remain supported");
    check(contained_by(layout.header, layout.screen) &&
              contained_by(layout.left_instruments, layout.screen) &&
              contained_by(layout.viewport_frame, layout.screen) &&
              contained_by(layout.viewport, layout.viewport_frame) &&
              contained_by(layout.right_instruments, layout.screen) &&
              contained_by(layout.messages, layout.screen) &&
              contained_by(layout.status, layout.screen),
          "every cockpit region must remain inside its owner");
    check(layout.left_instruments.intersect(layout.viewport_frame).empty() &&
              layout.viewport_frame
                  .intersect(layout.right_instruments)
                  .empty() &&
              layout.header.intersect(layout.viewport_frame).empty() &&
              layout.messages.intersect(layout.viewport_frame).empty() &&
              layout.status.intersect(layout.viewport_frame).empty(),
          "cockpit chrome regions must not overlap the pixel frame");
    check(layout.viewport.x > layout.viewport_frame.x &&
              layout.viewport.y > layout.viewport_frame.y &&
              layout.viewport.x + layout.viewport.w <
                  layout.viewport_frame.x + layout.viewport_frame.w &&
              layout.viewport.y + layout.viewport.h <
                  layout.viewport_frame.y + layout.viewport_frame.h,
          "the pixel viewport must remain strictly inside the frame border");
  }

  const auto intermediate =
      compute_cockpit_layout(100, 30, kitty_cell, viewport);
  check(intermediate.mode == CockpitLayoutMode::compact,
        "an intermediate terminal must retain compact layout");
  check(intermediate ==
            compute_cockpit_layout(100, 30, kitty_cell, viewport),
        "cockpit layout must be deterministic");
}

auto menu_session_contract() -> void {
  SessionController title;
  check(title.screen() == SessionScreen::title &&
            title.selected() == MenuItem::primary,
        "interactive sessions must begin at Start Flight");
  const auto ignored_escape = title.dispatch(MenuCommand::escape);
  check(!ignored_escape.changed() &&
            title.screen() == SessionScreen::title,
        "Escape on the title screen must not exit");
  (void)title.dispatch(MenuCommand::next);
  check(title.selected() == MenuItem::exit,
        "menu navigation must focus the explicit Exit action");
  (void)title.dispatch(MenuCommand::previous);
  check(title.selected() == MenuItem::primary,
        "reverse navigation must return focus to the primary action");
  const auto started = title.dispatch(MenuCommand::activate);
  check(started.from == SessionScreen::title &&
            started.to == SessionScreen::flight,
        "activating Start Flight must enter flight");

  const auto paused = title.dispatch(MenuCommand::escape);
  check(paused.from == SessionScreen::flight &&
            paused.to == SessionScreen::paused &&
            title.selected() == MenuItem::primary,
        "Escape in flight must pause with Resume focused");
  const auto resumed = title.dispatch(MenuCommand::escape);
  check(resumed.from == SessionScreen::paused &&
            resumed.to == SessionScreen::flight,
        "Escape in the pause menu must resume flight");
  (void)title.dispatch(MenuCommand::escape);
  (void)title.dispatch(MenuCommand::next);
  const auto exited = title.dispatch(MenuCommand::activate);
  check(exited.to == SessionScreen::exit_requested,
        "Exit must require focused activation");
  check(!title.dispatch(MenuCommand::escape).changed(),
        "an exit request must be terminal");

  SessionController headless{true};
  check(headless.screen() == SessionScreen::flight,
        "benchmark and capture sessions must bypass the title screen");

  SessionController docked{false, true};
  const auto entered_station = docked.dispatch(MenuCommand::activate);
  check(entered_station.from == SessionScreen::title &&
            entered_station.to == SessionScreen::station &&
            docked.menu_visible(),
        "a docked profile must continue from title into the station menu");
  check(!docked.dispatch(MenuCommand::activate).changed(),
        "station primary actions must remain application-owned");
  check(docked.start_flight().to == SessionScreen::flight &&
            docked.dock_at_station().to == SessionScreen::station,
        "launch and return must explicitly transition between station and flight");

  for (const auto& [cols, rows] :
       std::array{std::pair{0, 24}, std::pair{-1, 24},
                  std::pair{32, 15}, std::pair{65536, 24}}) {
    check(!compute_menu_layout(cols, rows).supported(),
          "invalid menu dimensions must be rejected");
  }
  const auto compact = compute_menu_layout(80, 24);
  check(compact.supported() && compact.screen == Rect{0, 0, 80, 24},
        "the minimum cockpit terminal must retain a usable menu");
  check(contained_by(compact.art, compact.screen) &&
            contained_by(compact.panel, compact.screen) &&
            contained_by(compact.primary_action, compact.panel) &&
            contained_by(compact.exit_action, compact.panel) &&
            compact.art.intersect(compact.panel).empty(),
        "menu art and actions must remain inside non-overlapping regions");
  check(menu_item_at(compact, compact.primary_action.x,
                     compact.primary_action.y) == MenuItem::primary &&
            menu_item_at(compact,
                         compact.exit_action.x + compact.exit_action.w - 1,
                         compact.exit_action.y) == MenuItem::exit,
        "menu hit testing must include both action boundaries");
  check(!menu_item_at(compact, compact.panel.x, compact.panel.y),
        "menu borders must not activate an action");
  check(compact == compute_menu_layout(80, 24),
        "menu layout must be deterministic");

  for (const auto& [cols, rows] :
       std::array{std::pair{0, 24}, std::pair{80, 0},
                  std::pair{31, 24}, std::pair{80, 23},
                  std::pair{65536, 24}}) {
    check(!compute_title_menu_layout(cols, rows).supported(),
          "invalid title-menu dimensions must be rejected");
  }
  const auto title_layout = compute_title_menu_layout(80, 24);
  check(title_layout.supported() &&
            contained_by(title_layout.art, title_layout.screen) &&
            contained_by(title_layout.panel, title_layout.screen) &&
            title_layout.art.intersect(title_layout.panel).empty() &&
            std::ranges::all_of(title_layout.actions, [&](Rect action) {
              return contained_by(action, title_layout.panel);
            }),
        "the minimum title terminal must retain bounded art and five usable actions");
  for (std::size_t index = 0; index < title_layout.actions.size(); ++index) {
    const auto row = title_layout.actions[index];
    check(title_action_at(title_layout, row.x + row.w - 1, row.y) ==
              static_cast<TitleAction>(index),
          "title-menu hit testing must include every action's right boundary");
  }
  check(!title_action_at(title_layout, title_layout.panel.x,
                         title_layout.panel.y) &&
            title_layout == compute_title_menu_layout(80, 24),
        "title-menu borders must be inert and layout deterministic");
}

auto title_render_contract() -> void {
  constexpr Pixel sentinel{91, 73, 55, 37};
  std::vector<Pixel> invalid(16, sentinel);
  check(!render_title({0, 4}, invalid) &&
            std::all_of(invalid.begin(), invalid.end(),
                        [&](Pixel pixel) { return pixel == sentinel; }),
        "invalid title dimensions must not touch the destination");
  check(!render_title({4, 4}, std::span<Pixel>{invalid}.first(15)) &&
            std::all_of(invalid.begin(), invalid.end(),
                        [&](Pixel pixel) { return pixel == sentinel; }),
        "a mismatched title buffer must remain untouched");
  check(!render_title({8, 8}, std::span<Pixel>{invalid}.first(16)) &&
            std::all_of(invalid.begin(), invalid.end(),
                        [&](Pixel pixel) { return pixel == sentinel; }),
        "a too-small title surface must use the cell fallback safely");

  struct Golden {
    ViewportSize size;
    int scale;
    std::uint64_t checksum;
  };
  constexpr std::array goldens{
      Golden{{320, 240}, 10, 5172959142211273845ULL},
      Golden{{512, 320}, 17, 480885040810389307ULL},
      Golden{{640, 480}, 21, 1502170724445620124ULL},
      Golden{{1024, 768}, 35, 3292241919495927159ULL},
  };
  for (const auto& golden : goldens) {
    const auto count = static_cast<std::size_t>(golden.size.width) *
                       static_cast<std::size_t>(golden.size.height);
    std::vector<Pixel> guarded(count + 2, sentinel);
    auto frame = std::span<Pixel>{guarded}.subspan(1, count);
    const auto result = render_title(golden.size, frame);
    check(result.has_value() && result->scale == golden.scale,
          "title profiles must use the expected integer scale");
    check(guarded.front() == sentinel && guarded.back() == sentinel,
          "title rendering must stay inside its exact destination");
    check(std::all_of(frame.begin(), frame.end(),
                      [](Pixel pixel) { return pixel.a == 255; }),
          "title rendering must produce an opaque framebuffer");
    const auto checksum = pixel_checksum(frame);
    if (checksum != golden.checksum) {
      std::fprintf(stderr, "title %dx%d checksum: %llu\n",
                   golden.size.width, golden.size.height,
                   static_cast<unsigned long long>(checksum));
    }
    check(checksum == golden.checksum,
          "title profile checksum must remain stable");
  }
}

auto flight_instrument_contract() -> void {
  FlightState state;
  state.pose.yaw = 0.35F;
  state.pose.altitude = 135.0F;
  state.clearance = 48.0F;
  state.velocity = {3.0F, 4.0F, 12.0F};
  state.mode = FlightMode::autopilot;

  const auto normal = format_flight_instruments(state);
  check(normal.heading == "HDG 020  ",
        "heading must use rounded normalized degrees");
  check(normal.altitude == "ALT 00135",
        "altitude must use a fixed-width whole-unit field");
  check(normal.clearance == "CLR 048  ",
        "clearance must use a fixed-width whole-unit field");
  check(normal.speed == "SPD 013  ",
        "speed must use total craft velocity magnitude");
  check(normal.mode == "MODE AUTO" &&
            normal.alert_state == CockpitAlert::none,
        "normal autopilot telemetry must not raise an alert");

  const auto check_widths = [](const FlightInstrumentReadout& readout,
                               const char* message) {
    check(readout.heading.size() == kInstrumentLineWidth &&
              readout.altitude.size() == kInstrumentLineWidth &&
              readout.clearance.size() == kInstrumentLineWidth &&
              readout.speed.size() == kInstrumentLineWidth &&
              readout.mode.size() == kInstrumentLineWidth &&
              readout.drive.size() == kInstrumentLineWidth &&
              readout.alert.size() == kInstrumentLineWidth,
          message);
  };
  check_widths(normal, "every normal instrument line must have fixed width");

  state.mode = FlightMode::manual;
  state.pose.yaw = -1.57079632679489661923F;
  state.pose.altitude = -9999.0F;
  state.clearance = kLowClearanceWarning;
  state.velocity = {999.0F, 0.0F, 0.0F};
  const auto boundary = format_flight_instruments(state);
  check(boundary.heading == "HDG 270  " &&
            boundary.altitude == "ALT -9999" &&
            boundary.clearance == "CLR 024  " &&
            boundary.speed == "SPD 999  " &&
            boundary.mode == "MODE MAN ",
        "boundary telemetry must retain fixed-width values");
  check(boundary.alert_state == CockpitAlert::low_clearance &&
            boundary.alert == "! LOW CLR",
        "the exact low-clearance threshold must raise a textual alert");
  check_widths(boundary,
               "every boundary instrument line must have fixed width");

  state.pose.yaw = 359.6F *
                   (3.14159265358979323846F / 180.0F);
  state.pose.altitude = 100000.0F;
  state.clearance = 24.1F;
  state.velocity = {1000.0F, 0.0F, 0.0F};
  const auto overflow = format_flight_instruments(state);
  check(overflow.heading == "HDG 000  ",
        "rounded heading must wrap from 360 to zero");
  check(overflow.altitude == "ALT #####" &&
            overflow.speed == "SPD 1.0k ",
        "kilometre-per-second speed must remain legible in fixed width");
  check(overflow.alert_state == CockpitAlert::none,
        "clearance above the warning threshold must clear the alert");
  check_widths(overflow,
               "every overflow instrument line must have fixed width");

  state.pose.altitude = -9999.5F;
  state.clearance = -0.5F;
  state.velocity = {-0.5F, 0.0F, 0.0F};
  const auto negative_overflow = format_flight_instruments(state);
  check(negative_overflow.altitude == "ALT #####" &&
            negative_overflow.clearance == "CLR ###  " &&
            negative_overflow.speed == "SPD 001  ",
        "round-away negative boundaries must not exceed fixed fields");
  check_widths(negative_overflow,
               "negative overflow lines must retain fixed width");

  std::array<FlightState, 9> invalid_states;
  invalid_states.fill(FlightState{});
  invalid_states[0].pose.yaw = std::numeric_limits<float>::quiet_NaN();
  invalid_states[1].pose.altitude =
      std::numeric_limits<float>::infinity();
  invalid_states[2].clearance = -std::numeric_limits<float>::infinity();
  invalid_states[3].velocity.x =
      std::numeric_limits<float>::quiet_NaN();
  invalid_states[4].velocity.y = std::numeric_limits<float>::infinity();
  invalid_states[5].pose.x = std::numeric_limits<float>::infinity();
  invalid_states[6].pose.y = std::numeric_limits<float>::quiet_NaN();
  invalid_states[7].velocity.vertical =
      -std::numeric_limits<float>::infinity();
  invalid_states[8].mode = static_cast<FlightMode>(255);
  for (const auto& invalid_state : invalid_states) {
    const auto invalid = format_flight_instruments(invalid_state);
    check(invalid.alert_state == CockpitAlert::invalid_telemetry &&
              invalid.alert == "TELEM ERR",
          "non-finite or invalid telemetry must raise a textual error");
    check(invalid.heading == "HDG ---  " &&
              invalid.altitude == "ALT -----" &&
              invalid.clearance == "CLR ---  " &&
              invalid.speed == "SPD ---  ",
          "invalid telemetry must replace numeric fields with dashes");
    check_widths(invalid,
                 "every invalid instrument line must have fixed width");
  }

  const auto terrain = Terrain::generate(128, 42);
  check(terrain.has_value(), "instrument replay terrain must generate");
  if (!terrain) return;
  const auto initialized = initial_flight_state(*terrain);
  check(initialized.has_value(), "instrument replay state must initialize");
  if (!initialized) return;
  auto first = *initialized;
  auto second = *initialized;
  const auto replay = [&](FlightState& replay_state) {
    for (int step = 0; step < 60; ++step) {
      constexpr std::array commands{
          FlightCommand{0, FlightCommandKind::press_forward},
          FlightCommand{0, FlightCommandKind::press_turn_right},
      };
      const std::span tick_commands =
          replay_state.tick == 0 ? std::span{commands}
                                 : std::span<const FlightCommand>{};
      if (!advance_flight(*terrain, replay_state, tick_commands,
                          kSimulationStep)) {
        check(false, "instrument command replay must advance");
        return;
      }
    }
  };
  const auto before = format_flight_instruments(first);
  replay(first);
  replay(second);
  const auto after = format_flight_instruments(first);
  check(after == format_flight_instruments(second),
        "the same command steps must produce identical instrument lines");
  check(after.heading != before.heading && after.speed == "SPD 052  " &&
            after.mode == "MODE MAN ",
        "deterministic command steps must update heading, speed, and mode");
}

auto planetary_flight_regime_contract() -> void {
  const auto generated = generate_planet_descriptor(Seed{42});
  const std::array expected_ceiling{
      std::pair{AtmosphereClass::airless, 20'000.0},
      std::pair{AtmosphereClass::tenuous, 60'000.0},
      std::pair{AtmosphereClass::temperate, 100'000.0},
      std::pair{AtmosphereClass::dense, 160'000.0},
  };
  for (const auto& [atmosphere, ceiling] : expected_ceiling) {
    std::uint16_t pressure{};
    switch (atmosphere) {
      case AtmosphereClass::airless: pressure = 0; break;
      case AtmosphereClass::tenuous: pressure = 100; break;
      case AtmosphereClass::temperate: pressure = 800; break;
      case AtmosphereClass::dense: pressure = 2'000; break;
    }
    const auto planet =
        planet_with_atmosphere(generated, atmosphere, pressure);
    const auto bands = flight_regime_bands(planet);
    check(bands &&
              bands->terrain_enter_clearance_metres == 2'000.0 &&
              bands->terrain_exit_clearance_metres == 2'500.0 &&
              bands->atmosphere_enter_altitude_metres == ceiling &&
              bands->orbit_enter_altitude_metres > ceiling,
          "each atmosphere class must define stable hysteretic bands");
    const auto performance =
        flight_performance(planet, FlightRegime::atmospheric);
    const double expected_vertical_speed =
        std::max(180.0, (ceiling - 2'000.0) / 100.0);
    check(performance &&
              std::abs(performance->maximum_vertical_speed -
                       expected_vertical_speed) < 1.0e-9 &&
              std::abs(performance->vertical_acceleration -
                       expected_vertical_speed / 1.8) < 1.0e-9,
          "atmospheric performance must normalize every approach band to the pacing target");

    const auto fixture = initial_planetary_flight_state(
        planet, {0.0, 0.0, bands->orbit_enter_altitude_metres + 1.0},
        {}, 0.0, FlightMode::manual);
    check(fixture.has_value(),
          "every atmosphere class must provide a regime-validation fixture");
    if (!fixture) continue;
    const auto valid_at = [&](FlightRegime regime, double altitude,
                              double clearance) {
      auto state = *fixture;
      state.pose.position.altitude_metres = altitude;
      state.clearance_metres = clearance;
      state.regime = regime;
      state.last_transition.reset();
      return validate_planetary_flight_state(planet, state).has_value();
    };
    const double atmospheric_clearance =
        bands->terrain_exit_clearance_metres;
    const double overlap_altitude =
        (bands->atmosphere_enter_altitude_metres +
         bands->orbit_enter_altitude_metres) *
        0.5;
    check(!valid_at(FlightRegime::orbital,
                    bands->atmosphere_enter_altitude_metres,
                    atmospheric_clearance) &&
              valid_at(FlightRegime::orbital,
                       bands->atmosphere_enter_altitude_metres + 1.0,
                       atmospheric_clearance) &&
              valid_at(FlightRegime::orbital, overlap_altitude,
                       atmospheric_clearance),
          "orbital validation must honor the exact descent edge and altitude hysteresis overlap");
    check(valid_at(FlightRegime::atmospheric,
                   bands->atmosphere_enter_altitude_metres,
                   atmospheric_clearance) &&
              valid_at(FlightRegime::atmospheric,
                       bands->orbit_enter_altitude_metres - 1.0,
                       atmospheric_clearance) &&
              !valid_at(FlightRegime::atmospheric,
                        bands->orbit_enter_altitude_metres,
                        atmospheric_clearance),
          "atmospheric validation must honor the exact ascent edge and altitude hysteresis overlap");
    check(!valid_at(FlightRegime::atmospheric, overlap_altitude,
                    bands->terrain_enter_clearance_metres) &&
              valid_at(FlightRegime::atmospheric, overlap_altitude,
                       bands->terrain_enter_clearance_metres + 1.0) &&
              valid_at(FlightRegime::atmospheric, overlap_altitude,
                       bands->terrain_exit_clearance_metres),
          "atmospheric validation must honor the exact terrain-entry edge and clearance hysteresis overlap");
    check(valid_at(FlightRegime::terrain_flight, overlap_altitude,
                   bands->terrain_enter_clearance_metres) &&
              valid_at(FlightRegime::terrain_flight, overlap_altitude,
                       bands->terrain_exit_clearance_metres - 1.0) &&
              !valid_at(FlightRegime::terrain_flight, overlap_altitude,
                        bands->terrain_exit_clearance_metres),
          "terrain validation must honor the exact exit edge and clearance hysteresis overlap");
  }

  const auto airless =
      planet_with_atmosphere(generated, AtmosphereClass::airless, 0);
  const auto bands = flight_regime_bands(airless);
  check(bands.has_value(), "the airless approach fixture must be valid");
  if (!bands) return;

  const PlanetaryFlightEnvironment environment{125.0};
  const auto orbital = initial_planetary_flight_state(
      airless, {0.2, -0.4, bands->atmosphere_enter_altitude_metres + 1.0},
      environment, 0.5, FlightMode::manual);
  const auto atmospheric = initial_planetary_flight_state(
      airless, {0.2, -0.4, bands->atmosphere_enter_altitude_metres},
      environment, 0.5, FlightMode::manual);
  const auto terrain = initial_planetary_flight_state(
      airless,
      {0.2, -0.4,
       environment.surface_elevation_metres +
           bands->terrain_enter_clearance_metres},
      environment, 0.5, FlightMode::manual);
  check(orbital && orbital->regime == FlightRegime::orbital &&
            atmospheric &&
            atmospheric->regime == FlightRegime::atmospheric && terrain &&
            terrain->regime == FlightRegime::terrain_flight,
        "initial state must select the exact descending regime boundaries");

  if (!orbital || !atmospheric || !terrain) return;
  constexpr std::array regimes{
      FlightRegime::orbital,
      FlightRegime::atmospheric,
      FlightRegime::terrain_flight,
  };
  for (std::size_t from_index = 0; from_index < regimes.size(); ++from_index) {
    for (std::size_t to_index = 0; to_index < regimes.size(); ++to_index) {
      auto state = to_index == 0 ? *orbital
                   : to_index == 1 ? *atmospheric
                                   : *terrain;
      state.last_transition = FlightRegimeTransition{
          regimes[from_index], regimes[to_index], state.tick};
      const bool adjacent =
          from_index + 1 == to_index || to_index + 1 == from_index;
      check(validate_planetary_flight_state(airless, state).has_value() ==
                adjacent,
            "saved transition metadata must permit only adjacent regime pairs");
    }
  }
  auto unknown_transition = *atmospheric;
  unknown_transition.last_transition = FlightRegimeTransition{
      static_cast<FlightRegime>(255), FlightRegime::atmospheric,
      unknown_transition.tick};
  check(!validate_planetary_flight_state(airless, unknown_transition),
        "saved transition metadata must reject unknown regime values");

  auto descending = *orbital;
  descending.pose.position.altitude_metres =
      bands->atmosphere_enter_altitude_metres + 1.0;
  descending.clearance_metres = descending.pose.position.altitude_metres -
                                environment.surface_elevation_metres;
  descending.velocity.up_metres_per_second = -240.0;
  check(advance_planetary_flight(airless, environment, descending, {},
                                 kSimulationStep) &&
            descending.regime == FlightRegime::atmospheric &&
            descending.last_transition ==
                FlightRegimeTransition{FlightRegime::orbital,
                                       FlightRegime::atmospheric, 1},
        "descending through the approach ceiling must transition once");

  auto approach_gap = descending;
  approach_gap.pose.position.altitude_metres =
      bands->atmosphere_enter_altitude_metres + 1.0;
  approach_gap.clearance_metres =
      approach_gap.pose.position.altitude_metres -
      environment.surface_elevation_metres;
  check(advance_planetary_flight(airless, environment, approach_gap, {},
                                 kSimulationStep) &&
            approach_gap.regime == FlightRegime::atmospheric,
        "the orbital hysteresis gap must retain atmospheric flight");

  auto ascending = approach_gap;
  ascending.pose.position.altitude_metres =
      bands->orbit_enter_altitude_metres - 1.0;
  ascending.clearance_metres = ascending.pose.position.altitude_metres -
                               environment.surface_elevation_metres;
  ascending.velocity.up_metres_per_second =
      flight_performance(airless, FlightRegime::atmospheric)
          ->maximum_vertical_speed;
  ascending.controls.rise = true;
  check(advance_planetary_flight(airless, environment, ascending, {},
                                 kSimulationStep) &&
            ascending.regime == FlightRegime::orbital &&
            ascending.last_transition &&
            ascending.last_transition->from == FlightRegime::atmospheric &&
            ascending.last_transition->to == FlightRegime::orbital,
        "ascending through the orbital boundary must report its transition");

  auto terrain_gap = *terrain;
  terrain_gap.pose.position.altitude_metres =
      environment.surface_elevation_metres +
      bands->terrain_exit_clearance_metres - 1.0;
  terrain_gap.clearance_metres =
      terrain_gap.pose.position.altitude_metres -
      environment.surface_elevation_metres;
  check(advance_planetary_flight(airless, environment, terrain_gap, {},
                                 kSimulationStep) &&
            terrain_gap.regime == FlightRegime::terrain_flight,
        "the terrain hysteresis gap must retain terrain flight");
  terrain_gap.pose.position.altitude_metres =
      environment.surface_elevation_metres +
      bands->terrain_exit_clearance_metres - 0.1;
  terrain_gap.clearance_metres =
      terrain_gap.pose.position.altitude_metres -
      environment.surface_elevation_metres;
  terrain_gap.velocity.up_metres_per_second = 45.0;
  terrain_gap.controls.rise = true;
  check(advance_planetary_flight(airless, environment, terrain_gap, {},
                                 kSimulationStep) &&
            terrain_gap.regime == FlightRegime::atmospheric,
        "ascending through the terrain boundary must enter approach flight");

  const auto quiet = format_flight_regime(*orbital);
  const auto changed = format_flight_regime(descending);
  check(quiet.valid && quiet.regime == "REG ORB  " &&
            quiet.transition == std::string(kInstrumentLineWidth, ' ') &&
            changed.valid && changed.regime == "REG ATM  " &&
            changed.transition == "ORB >ATM ",
        "cockpit regime telemetry must be fixed-width and textual");
  auto invalid_readout_state = descending;
  invalid_readout_state.regime = static_cast<FlightRegime>(255);
  const auto invalid_readout = format_flight_regime(invalid_readout_state);
  check(!invalid_readout.valid && invalid_readout.regime == "REG ---- " &&
            invalid_readout.transition == "TRANS ERR",
        "invalid regime telemetry must remain renderable");

  auto contact = *terrain;
  contact.pose.position.altitude_metres =
      environment.surface_elevation_metres +
      kMinimumFlightClearanceMetres + 0.1;
  contact.clearance_metres = kMinimumFlightClearanceMetres + 0.1;
  contact.velocity.up_metres_per_second = -45.0;
  contact.controls.fall = true;
  check(advance_planetary_flight(airless, environment, contact, {},
                                 kSimulationStep) &&
            contact.clearance_metres == kMinimumFlightClearanceMetres &&
            contact.velocity.up_metres_per_second == 0.0,
        "terrain contact must preserve minimum clearance and cancel descent");

  auto rising_terrain = contact;
  rising_terrain.pose.position.altitude_metres =
      environment.surface_elevation_metres +
      kMinimumFlightClearanceMetres + 0.25;
  rising_terrain.clearance_metres = kMinimumFlightClearanceMetres + 0.25;
  rising_terrain.velocity.up_metres_per_second = -4.0;
  const PlanetaryFlightEnvironment raised_environment{
      environment.surface_elevation_metres + 1.0};
  check(advance_planetary_flight(airless, raised_environment, rising_terrain,
                                 {}, kSimulationStep) &&
            rising_terrain.clearance_metres ==
                kMinimumFlightClearanceMetres &&
            rising_terrain.pose.position.altitude_metres ==
                raised_environment.surface_elevation_metres +
                    kMinimumFlightClearanceMetres &&
            rising_terrain.velocity.up_metres_per_second == 0.0,
        "a newly sampled terrain rise must clamp safely instead of rejecting the flight step");
  check(planetary_flight_error_name(
            PlanetaryFlightError::invalid_environment) ==
            "invalid_environment",
        "planetary flight errors must expose stable typed names");

  auto polar = initial_planetary_flight_state(
      airless, {std::numbers::pi_v<double> / 2.0, 0.0, 1'000.0},
      environment, std::numbers::pi_v<double> - 0.01,
      FlightMode::manual);
  check(polar.has_value(), "an exact-pole flight state must initialize");
  if (polar) {
    constexpr std::array polar_commands{
        FlightCommand{0, FlightCommandKind::press_forward},
        FlightCommand{0, FlightCommandKind::press_strafe_right},
        FlightCommand{0, FlightCommandKind::press_turn_right},
    };
    check(advance_planetary_flight(airless, environment, *polar,
                                   polar_commands, kSimulationStep) &&
              std::isfinite(polar->pose.position.latitude_radians) &&
              std::isfinite(polar->pose.position.longitude_radians) &&
              std::isfinite(polar->pose.heading_radians) &&
              std::hypot(polar->velocity.east_metres_per_second,
                         polar->velocity.north_metres_per_second) <=
                  120.0,
          "pole motion, heading wrap, and diagonal input must remain bounded");
  }
}

auto orbital_motion_feedback_contract() -> void {
  const auto planet = planet_with_atmosphere(
      generate_planet_descriptor(Seed{74}), AtmosphereClass::airless, 0);
  const PlanetaryFlightEnvironment environment{};
  auto initialized = initial_planetary_flight_state(
      planet, {0.1, -0.2, 80'000.0}, environment, 0.0,
      FlightMode::manual);
  check(initialized.has_value(),
        "orbital motion feedback fixture must initialize");
  if (!initialized) return;

  const auto performance = flight_performance(planet, FlightRegime::orbital);
  check(performance && performance->maximum_horizontal_speed == 4'000.0 &&
            performance->maximum_vertical_speed == 2'000.0 &&
            performance->horizontal_acceleration == 1'000.0 &&
            performance->vertical_acceleration == 1'000.0 &&
            !flight_performance(planet, static_cast<FlightRegime>(255)),
        "orbital performance must retain its tuned, validated contract");

  auto coast = *initialized;
  coast.velocity.east_metres_per_second = 100.0;
  check(flight_drive_state(coast) == FlightDriveState::coast,
        "neutral orbital motion must report coast");
  check(advance_planetary_flight(planet, environment, coast, {},
                                 kSimulationStep) &&
            std::abs(coast.velocity.east_metres_per_second - 100.0) <
                1.0e-9,
        "neutral orbital flight must preserve momentum");
  constexpr std::array brake_command{
      FlightCommand{1, FlightCommandKind::press_backward}};
  check(advance_planetary_flight(planet, environment, coast, brake_command,
                                 kSimulationStep) &&
            coast.velocity.east_metres_per_second < 100.0 &&
            flight_drive_state(coast) == FlightDriveState::braking,
        "opposing orbital thrust must decelerate and report braking");

  auto drive = *initialized;
  drive.controls.forward = true;
  check(flight_drive_state(drive) == FlightDriveState::forward,
        "forward thrust must be distinguishable from idle");
  drive.controls = {.backward = true};
  check(flight_drive_state(drive) == FlightDriveState::reverse,
        "reverse thrust from rest must be distinguishable");
  drive.controls = {.strafe_right = true};
  check(flight_drive_state(drive) == FlightDriveState::maneuvering,
        "lateral thrust must report maneuvering");

  auto motion_state = *initialized;
  motion_state.velocity.east_metres_per_second = 100.0;
  const auto closing = resolve_target_relative_motion(
      planet, motion_state, {10'000.0, 0.0, 0.0}, 1'000.0);
  check(closing && closing->cue == TargetMotionCue::closing &&
            std::abs(closing->closing_speed_metres_per_second - 100.0) <
                1.0e-9 &&
            closing->arrival_estimate_seconds &&
            std::abs(*closing->arrival_estimate_seconds - 90.0) < 1.0e-9 &&
            std::abs(closing->stopping_distance_metres - 5.0) < 1.0e-9,
        "target motion must distinguish bearing from closing speed and ETA");
  motion_state.velocity.east_metres_per_second = 4'000.0;
  const auto brake = resolve_target_relative_motion(
      planet, motion_state, {9'000.0, 0.0, 0.0}, 0.0);
  check(brake && brake->cue == TargetMotionCue::brake &&
            brake->stopping_distance_metres == 8'000.0,
        "target motion must warn inside a buffered stopping distance");
  motion_state.velocity.east_metres_per_second = -100.0;
  const auto opening = resolve_target_relative_motion(
      planet, motion_state, {10'000.0, 0.0, 0.0}, 1'000.0);
  const auto arrived = resolve_target_relative_motion(
      planet, motion_state, {500.0, 0.0, 0.0}, 1'000.0);
  check(opening && opening->cue == TargetMotionCue::opening &&
            !opening->arrival_estimate_seconds && arrived &&
            arrived->cue == TargetMotionCue::holding,
        "opening and arrived targets must not expose a misleading ETA");
  check(!resolve_target_relative_motion(
             planet, motion_state,
             {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}, 0.0),
        "non-finite target motion must be rejected");
  check(signal_run_error_name(SignalRunError::flight_failure) ==
            "flight_failure",
        "Signal Run errors must retain their typed diagnostic names");
}

struct PlanetaryReplayFixture {
  PlanetDescriptor planet;
  PlanetaryFlightEnvironment environment;
  PlanetaryFlightState initial;
  PlanetaryFlightState expected;
  std::vector<FlightCommand> commands;
  std::vector<FlightRegimeTransition> transitions;
};

[[nodiscard]] auto make_planetary_replay_fixture()
    -> std::optional<PlanetaryReplayFixture> {
  const auto planet = planet_with_atmosphere(
      generate_planet_descriptor(Seed{0xA5515U}), AtmosphereClass::airless, 0);
  const PlanetaryFlightEnvironment environment{};
  auto initialized = initial_planetary_flight_state(
      planet, {0.15, -0.2, 31'000.0}, environment, 0.3,
      FlightMode::manual);
  if (!initialized) return std::nullopt;

  PlanetaryReplayFixture fixture{planet, environment, *initialized,
                                 *initialized, {}, {}};
  fixture.commands = {
      {0, FlightCommandKind::press_forward},
      {0, FlightCommandKind::press_turn_right},
      {0, FlightCommandKind::press_fall},
  };
  std::size_t next_command{};
  bool ascent_scheduled{};
  for (int step = 0; step < 100'000; ++step) {
    const auto first = next_command;
    while (next_command < fixture.commands.size() &&
           fixture.commands[next_command].tick == fixture.expected.tick) {
      ++next_command;
    }
    const std::span commands{fixture.commands.data() + first,
                             next_command - first};
    const auto advanced = advance_planetary_flight(
        fixture.planet, fixture.environment, fixture.expected, commands,
        kSimulationStep);
    if (!advanced) {
      return std::nullopt;
    }
    if (fixture.expected.last_transition &&
        fixture.expected.last_transition->tick == fixture.expected.tick) {
      fixture.transitions.push_back(*fixture.expected.last_transition);
      if (fixture.expected.regime == FlightRegime::terrain_flight &&
          !ascent_scheduled) {
        ascent_scheduled = true;
        fixture.commands.push_back(
            {fixture.expected.tick, FlightCommandKind::release_fall});
        fixture.commands.push_back(
            {fixture.expected.tick, FlightCommandKind::press_rise});
      } else if (fixture.expected.regime == FlightRegime::orbital &&
                 ascent_scheduled) {
        return fixture;
      }
    }
  }
  return std::nullopt;
}

[[nodiscard]] auto replay_planetary_at_render_rate(
    const PlanetaryReplayFixture& fixture, int render_fps)
    -> std::optional<PlanetaryFlightState> {
  PlanetaryFlightState state = fixture.initial;
  FixedStepClock clock;
  std::size_t next_command{};
  const SimulationSeconds frame_time{1.0 / render_fps};
  while (state.tick < fixture.expected.tick) {
    const auto advance = clock.advance(frame_time);
    if (!advance) return std::nullopt;
    for (int step = 0;
         step < advance->steps && state.tick < fixture.expected.tick; ++step) {
      const auto first = next_command;
      while (next_command < fixture.commands.size() &&
             fixture.commands[next_command].tick == state.tick) {
        ++next_command;
      }
      const std::span commands{fixture.commands.data() + first,
                               next_command - first};
      if (!advance_planetary_flight(fixture.planet, fixture.environment,
                                    state, commands, kSimulationStep)) {
        return std::nullopt;
      }
    }
  }
  return state;
}

auto deterministic_planetary_flight_replay() -> void {
  const auto fixture = make_planetary_replay_fixture();
  check(fixture.has_value(),
        "the planetary descent/ascent fixture must complete");
  if (!fixture) return;

  constexpr std::array expected_transitions{
      std::pair{FlightRegime::orbital, FlightRegime::atmospheric},
      std::pair{FlightRegime::atmospheric, FlightRegime::terrain_flight},
      std::pair{FlightRegime::terrain_flight, FlightRegime::atmospheric},
      std::pair{FlightRegime::atmospheric, FlightRegime::orbital},
  };
  check(fixture->transitions.size() == expected_transitions.size(),
        "the scripted flight must cross all four regime boundaries");
  if (fixture->transitions.size() == expected_transitions.size()) {
    for (std::size_t index = 0; index < expected_transitions.size(); ++index) {
      check(fixture->transitions[index].from ==
                    expected_transitions[index].first &&
                fixture->transitions[index].to ==
                    expected_transitions[index].second,
            "the scripted flight must retain transition order");
      if (index > 0) {
        check(fixture->transitions[index - 1].tick <
                  fixture->transitions[index].tick,
              "regime transitions must occur on distinct ordered ticks");
      }
    }
  }

  const auto at_30 = replay_planetary_at_render_rate(*fixture, 30);
  const auto at_60 = replay_planetary_at_render_rate(*fixture, 60);
  const auto actual_checksum =
      planetary_flight_state_checksum(fixture->expected);
  constexpr std::uint64_t expected_checksum{5033951390750856009ULL};
  if (actual_checksum != expected_checksum) {
    std::fprintf(stderr, "planetary replay checksum: %llu\n",
                 static_cast<unsigned long long>(actual_checksum));
  }
  check(actual_checksum == expected_checksum,
        "the planetary flight replay must retain its golden checksum");
  check(at_30 && at_60 &&
            planetary_flight_state_checksum(*at_30) == expected_checksum &&
            planetary_flight_state_checksum(*at_60) == expected_checksum,
        "render cadence must not alter planetary flight state");
  check(fixture->expected.regime == FlightRegime::orbital &&
            fixture->expected.pose.position.altitude_metres >= 30'000.0 &&
            std::isfinite(fixture->expected.pose.position.latitude_radians) &&
            std::isfinite(fixture->expected.pose.position.longitude_radians) &&
            std::isfinite(fixture->expected.pose.heading_radians),
        "the ascent must return to finite orbital state without losing heading");
}

auto planetary_flight_failure_matrix() -> void {
  const auto planet = planet_with_atmosphere(
      generate_planet_descriptor(Seed{42}), AtmosphereClass::temperate, 800);
  const PlanetaryFlightEnvironment environment{};
  const auto initialized = initial_planetary_flight_state(
      planet, {0.0, 0.0, 120'000.0}, environment, 0.0,
      FlightMode::manual);
  check(initialized.has_value(), "the failure fixture must initialize");
  if (!initialized) return;

  const auto unchanged = planetary_flight_state_checksum(*initialized);
  const auto check_rejected = [&](PlanetaryFlightState state,
                                  PlanetaryFlightEnvironment step_environment,
                                  std::span<const FlightCommand> commands,
                                  SimulationSeconds step,
                                  PlanetaryFlightError error,
                                  const char* message) {
    const auto before = planetary_flight_state_checksum(state);
    const auto result = advance_planetary_flight(
        planet, step_environment, state, commands, step);
    check(!result && result.error() == error &&
              planetary_flight_state_checksum(state) == before,
          message);
  };

  auto invalid = *initialized;
  invalid.pose.position.latitude_radians =
      std::numeric_limits<double>::quiet_NaN();
  check_rejected(invalid, environment, {}, kSimulationStep,
                 PlanetaryFlightError::invalid_state,
                 "non-finite planetary state must be rejected transactionally");
  auto unbounded = *initialized;
  unbounded.velocity.east_metres_per_second = 4'001.0;
  check_rejected(unbounded, environment, {}, kSimulationStep,
                 PlanetaryFlightError::invalid_state,
                 "out-of-regime velocity must be rejected transactionally");
  check_rejected(*initialized,
                 {std::numeric_limits<double>::infinity()}, {},
                 kSimulationStep, PlanetaryFlightError::invalid_environment,
                 "non-finite terrain elevation must be rejected transactionally");
  const std::array invalid_steps{
      SimulationSeconds::zero(),
      SimulationSeconds{-kSimulationStep.count()},
      SimulationSeconds{std::numeric_limits<double>::quiet_NaN()},
      SimulationSeconds{std::numeric_limits<double>::infinity()},
      kSimulationStep / 2.0,
      kSimulationStep * 2.0,
      SimulationSeconds{0.25},
      SimulationSeconds{
          std::nextafter(kSimulationStep.count(), 0.0)},
      SimulationSeconds{std::nextafter(
          kSimulationStep.count(),
          std::numeric_limits<double>::infinity())},
  };
  for (const auto invalid_step : invalid_steps) {
    check_rejected(*initialized, environment, {}, invalid_step,
                   PlanetaryFlightError::invalid_step,
                   "every non-canonical planetary step must be rejected "
                   "transactionally");
  }
  auto canonical = *initialized;
  check(advance_planetary_flight(planet, environment, canonical, {},
                                 kSimulationStep) &&
            canonical.tick == initialized->tick + 1U &&
            planetary_flight_state_checksum(canonical) != unchanged,
        "the canonical planetary step must advance exactly one tick");

  constexpr std::array invalid_command{FlightCommand{
      0, static_cast<FlightCommandKind>(std::numeric_limits<std::uint8_t>::max())}};
  check_rejected(*initialized, environment, invalid_command, kSimulationStep,
                 PlanetaryFlightError::invalid_command,
                 "an unknown planetary command must be rejected transactionally");
  constexpr std::array future_command{
      FlightCommand{1, FlightCommandKind::press_forward}};
  check_rejected(*initialized, environment, future_command, kSimulationStep,
                 PlanetaryFlightError::wrong_command_tick,
                 "a mistimed planetary command must be rejected transactionally");

  auto overflow = *initialized;
  overflow.tick = std::numeric_limits<SimulationTick>::max();
  check_rejected(overflow, environment, {}, kSimulationStep,
                 PlanetaryFlightError::tick_overflow,
                 "planetary tick overflow must be rejected transactionally");
  check(planetary_flight_state_checksum(*initialized) == unchanged,
        "failure tests must not mutate their shared initial state");

  const auto bad_radius = planet_with_radius(planet, 0);
  check(flight_regime_bands(bad_radius) ==
            std::unexpected{PlanetaryFlightError::invalid_planet},
        "invalid planets must not produce flight bands");
  const auto bad_airless =
      planet_with_atmosphere(planet, AtmosphereClass::airless, 1);
  check(flight_regime_bands(bad_airless) ==
            std::unexpected{PlanetaryFlightError::invalid_planet},
        "airless planets with pressure must be rejected");
  check(!initial_planetary_flight_state(
            planet, {0.0, 0.0, 15.0}, environment, 0.0,
            FlightMode::manual),
        "initial state below minimum clearance must be rejected");
}

auto thermal_reentry_contract() -> void {
  const auto dense = planet_with_atmosphere(
      generate_planet_descriptor(Seed{0x7E4A1U}), AtmosphereClass::dense,
      AtmospherePressureMillibars::max);
  const auto temperate = planet_with_atmosphere(
      dense, AtmosphereClass::temperate, 800);
  const PlanetaryFlightEnvironment environment{};
  auto initialized = initial_planetary_flight_state(
      dense, {0.1, -0.2, 40'000.0}, environment, 0.0,
      FlightMode::manual);
  check(initialized.has_value(),
        "the thermal reentry fixture must initialize in atmosphere");
  if (!initialized) return;

  initialized->velocity = {400.0, 0.0, -1'500.0};
  initialized->controls.fall = true;
  const auto dense_assessment =
      resolve_thermal_assessment(dense, *initialized);
  auto slower = *initialized;
  slower.velocity = {250.0, 0.0, -150.0};
  const auto slow_assessment = resolve_thermal_assessment(dense, slower);
  auto temperate_state = *initialized;
  temperate_state.planet = temperate.id;
  const auto temperate_assessment =
      resolve_thermal_assessment(temperate, temperate_state);
  check(dense_assessment && slow_assessment && temperate_assessment &&
            dense_assessment->trend == ThermalTrend::heating &&
            dense_assessment->cue == ThermalCue::slow_and_rise &&
            dense_assessment->load_change_per_second >
                slow_assessment->load_change_per_second &&
            dense_assessment->load_change_per_second >
                temperate_assessment->load_change_per_second,
        "faster, steeper, denser entry must produce stronger thermal loading");

  auto assisted = *initialized;
  for (int tick = 0; tick < 240; ++tick) {
    if (!advance_planetary_flight(dense, environment, assisted, {},
                                  kSimulationStep)) {
      check(false, "Assisted thermal entry must advance");
      return;
    }
  }
  check(assisted.thermal.load_units == kMaximumThermalLoadUnits &&
            !assisted.thermal.abort_latched,
        "Assisted must teach the thermal limit without forcing an abort");

  auto invalid_assisted_latch = assisted;
  invalid_assisted_latch.thermal.abort_latched = true;
  const auto invalid_assisted_before = invalid_assisted_latch;
  check(!advance_planetary_flight(dense, environment,
                                  invalid_assisted_latch, {},
                                  kSimulationStep) &&
            invalid_assisted_latch == invalid_assisted_before,
        "Assisted must reject a Pilot-only abort latch transactionally");

  auto resumed_at_limit = assisted;
  resumed_at_limit.controls.fall = true;
  check(advance_planetary_flight(
            dense, environment, resumed_at_limit, {}, kSimulationStep,
            {.enforce_thermal_abort = true}) &&
            resumed_at_limit.thermal.abort_latched,
        "Pilot must honor an exact-limit resumed state before cooling it");

  auto pilot = *initialized;
  std::optional<SimulationTick> abort_tick;
  for (int tick = 0; tick < 1'000 && !abort_tick; ++tick) {
    if (!advance_planetary_flight(
            dense, environment, pilot, {}, kSimulationStep,
            {.enforce_thermal_abort = true})) {
      check(false, "Pilot thermal entry must advance");
      return;
    }
    if (pilot.thermal.abort_latched) abort_tick = pilot.tick;
  }
  const auto abort_readout = format_thermal_instruments(dense, pilot);
  check(abort_tick && pilot.thermal.load_units == kMaximumThermalLoadUnits &&
            pilot.thermal.abort_latched && abort_readout.valid &&
            abort_readout.load == "HEAT 100%" &&
            abort_readout.trend == "TEMP +   " &&
            abort_readout.limit == "LIM 100% " &&
            abort_readout.flight_path_angle.size() == kInstrumentLineWidth &&
            abort_readout.flight_path_angle.starts_with("FPA -") &&
            abort_readout.cue == "ABRT CLMB",
        "Pilot must latch one textual, information-complete forced skip-out");

  bool recovered{};
  for (int tick = 0; tick < 30'000; ++tick) {
    if (!advance_planetary_flight(
            dense, environment, pilot, {}, kSimulationStep,
            {.enforce_thermal_abort = true})) {
      check(false, "the Pilot skip-out must remain controllable");
      return;
    }
    if (pilot.regime == FlightRegime::orbital &&
        !pilot.thermal.abort_latched) {
      recovered = true;
      break;
    }
  }
  check(recovered && !pilot.controls.fall &&
            pilot.velocity.up_metres_per_second >= 0.0 &&
            pilot.thermal.load_units < kMaximumThermalLoadUnits,
        "the forced skip-out must cool, reach orbit, and require deliberate reentry input");

  const auto airless = planet_with_atmosphere(
      dense, AtmosphereClass::airless, 0);
  auto cooling = initial_planetary_flight_state(
      airless, {0.1, -0.2, 30'000.0}, environment, 0.0,
      FlightMode::manual);
  check(cooling.has_value(), "the airless cooling fixture must initialize");
  if (cooling) {
    cooling->thermal.load_units = 500'000U;
    const auto before = cooling->thermal.load_units;
    for (int tick = 0; tick < 120; ++tick) {
      check(advance_planetary_flight(airless, environment, *cooling, {},
                                     kSimulationStep)
                .has_value(),
            "airless cooling must remain finite");
    }
    check(cooling->thermal.load_units < before,
          "sustained cooling must reduce thermal load predictably");
  }

  auto invalid = *initialized;
  invalid.thermal.load_units = kMaximumThermalLoadUnits + 1U;
  const auto invalid_checksum = planetary_flight_state_checksum(invalid);
  check(!validate_planetary_flight_state(dense, invalid) &&
            !advance_planetary_flight(dense, environment, invalid, {},
                                      kSimulationStep) &&
            planetary_flight_state_checksum(invalid) == invalid_checksum,
        "out-of-range thermal state must be rejected transactionally");
  check(thermal_trend_name(ThermalTrend::heating) == "heating" &&
            thermal_cue_name(ThermalCue::abort_climb) == "abort-climb",
        "thermal enums must expose stable diagnostic names");
}

auto sweep_selection_contract() -> void {
  const auto defaults = default_sweep_viewports();
  check(defaults.size() == 3,
        "the default sweep must include three viewports");
  if (defaults.size() == 3) {
    check(profile_name(defaults[0]) == "remote",
          "the default sweep must begin with remote");
    check(profile_name(defaults[1]) == "balanced",
          "the default sweep must continue with balanced");
    check(profile_name(defaults[2]) == "local",
          "the default sweep must end with local");
  }
  check(default_sweep_fps() == std::vector<std::uint32_t>({30, 60}),
        "the default cadence targets must be 30 and 60 FPS");
  check(parse_benchmark_workload("landscape") ==
                BenchmarkWorkload::landscape &&
            parse_benchmark_workload("orbital") ==
                BenchmarkWorkload::orbital &&
            parse_benchmark_workload("planetary") ==
                BenchmarkWorkload::planetary &&
            parse_benchmark_workload("system") ==
                BenchmarkWorkload::system &&
            !parse_benchmark_workload("unknown"),
        "benchmark workloads must parse only their documented names");
  check(workload_identifier(BenchmarkWorkload::landscape) ==
                "voxel-landscape-rgba" &&
            workload_identifier(BenchmarkWorkload::orbital) ==
                "orbital-planet-rgba" &&
            workload_identifier(BenchmarkWorkload::planetary) ==
                "planetary-presentation-rgba" &&
            workload_identifier(BenchmarkWorkload::system) ==
                "local-system-rgba",
        "benchmark workloads must retain stable report identifiers");

  const auto viewports = parse_sweep_viewports("remote,640x360,cinematic");
  check(viewports && viewports->size() == 3,
        "named and custom sweep viewports must parse together");
  if (viewports && viewports->size() == 3) {
    check(profile_name((*viewports)[0]) == "remote",
          "named sweep viewport identity must be retained");
    check(profile_name((*viewports)[1]) == "custom" &&
              (*viewports)[1].viewport == ViewportSize{640, 360},
          "custom sweep viewport identity and dimensions must be retained");
  }
  check(!parse_sweep_viewports(""),
        "an empty sweep viewport list must be rejected");
  check(!parse_sweep_viewports("remote,,local"),
        "an empty sweep viewport entry must be rejected");
  check(!parse_sweep_viewports("remote,320x240"),
        "duplicate resolved sweep viewports must be rejected");
  check(!parse_sweep_viewports("4097x1"),
        "invalid sweep viewport dimensions must be rejected");

  const auto fps = parse_sweep_fps("24,30,60");
  check(fps && *fps == std::vector<std::uint32_t>({24, 30, 60}),
        "positive sweep FPS targets must retain order");
  check(!parse_sweep_fps(""),
        "an empty sweep FPS list must be rejected");
  check(!parse_sweep_fps("0,30"),
        "a zero sweep FPS target must be rejected");
  check(!parse_sweep_fps("30,nope"),
        "a malformed sweep FPS target must be rejected");
  check(!parse_sweep_fps("30,30"),
        "a duplicate sweep FPS target must be rejected");
  check(!parse_sweep_fps("999999999999999999999"),
        "an overflowing sweep FPS target must be rejected");
}

auto sweep_report_contract() -> void {
  BenchmarkSummary summary{
      .frames = 12,
      .elapsed_seconds = 1.5,
      .achieved_fps = 8.0,
      .render_avg_ms = 3.0,
      .render_p95_ms = 4.0,
      .work_avg_ms = 5.0,
      .work_p95_ms = 6.0,
      .bytes_per_frame = 1024.0,
      .mebibytes_per_second = 1.0,
      .total_bytes = std::numeric_limits<std::uint64_t>::max(),
      .checksum = std::numeric_limits<std::uint64_t>::max() - 1,
      .planetary_presentation = std::nullopt,
  };
  const auto cadence = assess_cadence(summary, 50);
  check(std::abs(cadence.deadline_budget_ms - 20.0) < 0.000001,
        "cadence assessment must derive the frame deadline");
  check(std::abs(cadence.renderer_p95_headroom_ms - 16.0) < 0.000001,
        "cadence assessment must derive renderer headroom");
  check(std::abs(cadence.frame_work_p95_headroom_ms - 14.0) < 0.000001,
        "cadence assessment must derive complete-frame headroom");

  const std::vector measurements{BenchmarkMeasurement{
      resolve_render_configuration(RenderProfile::remote), summary}};
  const std::vector<std::uint32_t> targets{30, 60};
  const auto json = sweep_json(measurements, targets, 42, 12);
  check(json.find("\"schema_version\": 2") != std::string::npos,
        "sweep JSON must identify its schema version");
  check(json.find("\"workload\": \"voxel-landscape-rgba\"") !=
            std::string::npos,
        "the default sweep report must identify the landscape workload");
  check(json.find("\"seed\": 42") != std::string::npos,
        "sweep JSON must identify its seed");
  check(json.find("\"frames_per_viewport\": 12") != std::string::npos,
        "sweep JSON must identify its frame count");
  check(json.find("\"total_bytes\": \"18446744073709551615\"") !=
            std::string::npos,
        "sweep JSON must preserve byte totals exactly as strings");
  check(json.find("\"checksum\": \"18446744073709551614\"") !=
            std::string::npos,
        "sweep JSON must preserve checksums exactly as strings");
  check(json.find("\"target_fps\": 30") != std::string::npos &&
            json.find("\"target_fps\": 60") != std::string::npos,
        "sweep JSON must include every cadence target");
  const auto orbital_json = sweep_json(measurements, targets, 42, 12,
                                       BenchmarkWorkload::orbital);
  check(orbital_json.find("\"workload\": \"orbital-planet-rgba\"") !=
            std::string::npos,
        "an orbital sweep report must identify its renderer workload");
  summary.planetary_presentation = PlanetaryPresentationBenchmarkSummary{
      .orbital_frames = 3,
      .atmospheric_frames = 3,
      .terrain_blend_frames = 3,
      .local_terrain_frames = 3,
      .orbital_render_avg_ms = 2.0,
      .local_render_avg_ms = 3.0,
      .composite_avg_ms = 1.0,
      .total_avg_ms = 6.0,
      .total_p95_ms = 9.0,
      .maximum_tiles_touched = 4,
  };
  const std::vector planetary_measurements{BenchmarkMeasurement{
      resolve_render_configuration(RenderProfile::remote), summary}};
  const auto planetary_json = sweep_json(
      planetary_measurements, targets, 42, 12,
      BenchmarkWorkload::planetary);
  check(planetary_json.find(
            "\"workload\": \"planetary-presentation-rgba\"") !=
                std::string::npos &&
            planetary_json.find("\"terrain_blend\": 3") !=
                std::string::npos &&
            planetary_json.find("\"total_p95_ms\": 9.000000") !=
                std::string::npos,
        "planetary sweep reports must include stage counts and timings");

  const auto table = sweep_table(measurements, targets);
  check(table.find("PROFILE") != std::string::npos &&
            table.find("remote") != std::string::npos,
        "the sweep table must contain a header and profile rows");
}

class CountingAudioSource final : public AudioRenderSource {
 public:
  [[nodiscard]] auto render(std::span<float>) noexcept
      -> std::optional<AudioBufferError> override {
    ++calls;
    return std::nullopt;
  }

  int calls{};
};

class ToneAudioSource final : public AudioRenderSource {
 public:
  [[nodiscard]] auto render(std::span<float> samples) noexcept
      -> std::optional<AudioBufferError> override {
    for (auto& sample : samples) {
      sample = static_cast<float>(m_next++) / 16.0F;
    }
    ++calls;
    return std::nullopt;
  }

  std::uint32_t m_next{};
  int calls{};
};

class RejectingAudioSource final : public AudioRenderSource {
 public:
  [[nodiscard]] auto render(std::span<float>) noexcept
      -> std::optional<AudioBufferError> override {
    ++calls;
    return AudioBufferError::invalid_dimensions;
  }

  int calls{};
};

struct FakeAudioBackendControl {
  bool stopped{};
};

class FakeAudioBackend final : public AudioBackend {
 public:
  FakeAudioBackend(FakeAudioBackendControl& control,
                   bool start_success,
                   AudioBackendFailure start_failure =
                       AudioBackendFailure::none)
      : m_control(control),
        m_start_success(start_success),
        m_start_failure(start_failure) {}

  [[nodiscard]] auto name() const noexcept -> std::string_view override {
    return "fake-device";
  }

  [[nodiscard]] auto state() const noexcept
      -> AudioBackendState override {
    return m_state;
  }

  [[nodiscard]] auto diagnostics() const noexcept
      -> AudioBackendDiagnostics override {
    return {
        .name = name(),
        .state = state(),
        .failure = m_failure,
        .output_device_id = m_start_success
                                ? std::optional<std::uint32_t>{17}
                                : std::nullopt,
        .callback_count = 3,
        .output_underflow_count = 1,
    };
  }

  [[nodiscard]] auto start(AudioFormat format,
                           AudioRenderSource&) noexcept -> bool override {
    m_control.stopped = false;
    if (format != kAudioFormat || !m_start_success) {
      m_failure = m_start_failure;
      m_state = AudioBackendState::failed;
      return false;
    }
    m_failure = AudioBackendFailure::none;
    m_state = AudioBackendState::running;
    return true;
  }

  auto stop() noexcept -> void override {
    m_control.stopped = true;
    m_state = AudioBackendState::stopped;
  }

  auto fail(AudioBackendFailure failure) noexcept -> void {
    m_failure = failure;
    m_state = AudioBackendState::failed;
  }

 private:
  FakeAudioBackendControl& m_control;
  bool m_start_success{};
  AudioBackendFailure m_start_failure{AudioBackendFailure::none};
  AudioBackendState m_state{AudioBackendState::stopped};
  AudioBackendFailure m_failure{AudioBackendFailure::none};
};

struct AudioReplayResult {
  std::uint64_t flight_checksum{};
  std::vector<AudioEvent> events;
  AudioDiagnostics diagnostics;
};

[[nodiscard]] auto replay_audio_event_trace(bool enabled,
                                            bool delayed_consumption)
    -> AudioReplayResult {
  const auto terrain = Terrain::generate(kFlightDeckAcceptanceTerrainSize,
                                         kFlightDeckAcceptanceSeed);
  if (!terrain) return {};
  auto initialized = initial_flight_state(*terrain);
  if (!initialized) return {};

  AudioRuntime audio{enabled ? AudioRuntimeMode::no_device
                             : AudioRuntimeMode::disabled};
  auto state = *initialized;
  const auto schedule = flight_deck_acceptance_commands();
  std::size_t next_command{};
  std::vector<AudioEvent> events;
  for (SimulationTick tick = 0; tick < kFlightDeckAcceptanceTicks; ++tick) {
    const auto first = next_command;
    while (next_command < schedule.size() &&
           schedule[next_command].tick == tick) {
      ++next_command;
    }
    const auto commands = schedule.subspan(first, next_command - first);
    if (!advance_flight(*terrain, state, commands, kSimulationStep)) return {};
    for (const auto& command : commands) {
      const auto emitted = audio.emit(
          command.tick,
          AudioCueId{static_cast<std::uint32_t>(command.kind) + 1U});
      if (enabled && emitted.status != AudioEmitStatus::queued) return {};
      if (!enabled && emitted.status != AudioEmitStatus::disabled) return {};
    }
    if (!delayed_consumption) {
      while (auto event = audio.try_take_event()) events.push_back(*event);
    }
  }
  while (auto event = audio.try_take_event()) events.push_back(*event);
  return {
      .flight_checksum = flight_state_checksum(state),
      .events = std::move(events),
      .diagnostics = audio.diagnostics(),
  };
}

auto audio_contract() -> void {
  static_assert(kAudioSampleRate % kSimulationHz == 0);
  check(kAudioFormat == AudioFormat{} && kAudioFramesPerSimulationTick == 400,
        "audio must use the fixed 48 kHz stereo float contract");
  check(audio_sample_frame({42, 17}) == 16'800,
        "authoritative ticks must map exactly to sample frames");
  const auto overflowing_tick =
      std::numeric_limits<std::uint64_t>::max() /
          kAudioFramesPerSimulationTick +
      1U;
  check(!audio_sample_frame({overflowing_tick, 0}),
        "overflowing audio timestamps must be rejected");

  CountingAudioSource source;
  NoDeviceAudioBackend backend;
  auto invalid_format = kAudioFormat;
  invalid_format.channels = 1;
  check(!backend.start(invalid_format, source) &&
            backend.state() == AudioBackendState::failed &&
            source.calls == 0,
        "the no-device backend must reject invalid formats without rendering");
  check(backend.start(kAudioFormat, source) &&
            backend.state() == AudioBackendState::no_device &&
            source.calls == 0,
        "the no-device backend must start without device or callback work");
  backend.stop();
  check(backend.state() == AudioBackendState::stopped,
        "the no-device backend must stop idempotently");
  check(backend.diagnostics().name == "no-device" &&
            backend.diagnostics().failure == AudioBackendFailure::none,
        "the no-device backend must expose truthful diagnostics");

#if APSIS_DRIFT_TEST_RTAUDIO_ENABLED
  check(rtaudio_backend_compiled(),
        "the enabled build must compile the RtAudio backend");
#else
  check(!rtaudio_backend_compiled() &&
            !make_device_audio_backend(),
        "the disabled build must not compile or construct RtAudio");
#endif

  check(audio_backend_state_name(AudioBackendState::no_device) ==
                "no-device" &&
            audio_backend_failure_name(
                AudioBackendFailure::invalid_selected_device) ==
                "invalid-selected-device",
        "audio diagnostic states and failures must have stable names");

  detail::AudioCallbackBridge callback;
  AudioRuntime callback_silence;
  std::array<float, 8> silence;
  silence.fill(7.0F);
  callback.activate(callback_silence);
  check(callback.render(silence.data(), 4, false) ==
                detail::AudioCallbackAction::continue_stream &&
            std::ranges::all_of(silence,
                                [](float value) { return value == 0.0F; }),
        "the injected callback must receive exact mixer silence");

  ToneAudioSource tone;
  std::array<float, 8> generated{};
  callback.activate(tone);
  check(callback.render(generated.data(), 4, true) ==
                detail::AudioCallbackAction::continue_stream &&
            generated == std::array<float, 8>{0.0F, 0.0625F, 0.125F,
                                               0.1875F, 0.25F, 0.3125F,
                                               0.375F, 0.4375F} &&
            tone.calls == 1 && callback.output_underflow_count() == 1,
        "the injected callback must preserve exact generated frames and count underruns");
  callback.deactivate();
  generated.fill(9.0F);
  check(callback.render(generated.data(), 4, false) ==
                detail::AudioCallbackAction::abort_stream &&
            tone.calls == 1 &&
            std::ranges::all_of(generated,
                                [](float value) { return value == 9.0F; }),
        "callback teardown must reject later work without touching output");

  RejectingAudioSource rejecting;
  callback.activate(rejecting);
  std::array<float, 4> rejected_output{1.0F, 2.0F, 3.0F, 4.0F};
  check(callback.render(rejected_output.data(), 2, false) ==
                detail::AudioCallbackAction::abort_stream &&
            rejecting.calls == 1 &&
            std::ranges::all_of(rejected_output,
                                [](float value) { return value == 0.0F; }) &&
            callback.failure() == AudioBackendFailure::callback_failed,
        "a callback render failure must zero valid output and abort");
  callback.activate(tone);
  check(callback.render(nullptr, 2, false) ==
                detail::AudioCallbackAction::abort_stream &&
            callback.render(generated.data(), 0, false) ==
                detail::AudioCallbackAction::abort_stream &&
            callback.render(generated.data(),
                            kMaximumAudioFramesPerCallback + 1U, false) ==
                detail::AudioCallbackAction::abort_stream,
        "invalid callback buffers must fail closed before mixer access");

  constexpr std::array startup_failures{
      AudioBackendFailure::discovery_failed,
      AudioBackendFailure::no_output_device,
      AudioBackendFailure::invalid_selected_device,
      AudioBackendFailure::open_failed,
      AudioBackendFailure::start_failed,
  };
  for (const auto failure : startup_failures) {
    FakeAudioBackendControl failed_control;
    AudioRuntime failed_device{
        AudioRuntimeMode::automatic,
        std::make_unique<FakeAudioBackend>(failed_control, false, failure)};
    const auto failed_device_diagnostics = failed_device.diagnostics();
    check(failed_control.stopped &&
              failed_device_diagnostics.mode == AudioRuntimeMode::automatic &&
              failed_device_diagnostics.backend_name == "no-device" &&
              failed_device_diagnostics.backend_state ==
                  AudioBackendState::no_device &&
              failed_device_diagnostics.last_backend_failure == failure &&
              failed_device_diagnostics.backend_failure_count == 1 &&
              failed_device_diagnostics.callback_count == 3 &&
              failed_device_diagnostics.output_underflow_count == 1,
          "device startup failure must stop synchronously and retain fallback diagnostics");
  }

  constexpr std::array runtime_failures{
      AudioBackendFailure::callback_failed,
      AudioBackendFailure::device_lost,
  };
  for (const auto failure : runtime_failures) {
    FakeAudioBackendControl loss_control;
    auto loss_backend =
        std::make_unique<FakeAudioBackend>(loss_control, true);
    auto* loss_backend_view = loss_backend.get();
    AudioRuntime runtime_loss{AudioRuntimeMode::automatic,
                              std::move(loss_backend)};
    check(runtime_loss.diagnostics().backend_state ==
                  AudioBackendState::running &&
              runtime_loss.diagnostics().output_device_id == 17,
          "an automatic runtime must expose its selected running device");
    (void)runtime_loss.emit(3, {1});
    loss_backend_view->fail(failure);
    runtime_loss.service();
    const auto loss_diagnostics = runtime_loss.diagnostics();
    check(loss_control.stopped &&
              loss_diagnostics.backend_state ==
                  AudioBackendState::no_device &&
              loss_diagnostics.last_backend_failure == failure &&
              loss_diagnostics.backend_failure_count == 1 &&
              loss_diagnostics.backend_loss_count == 1 &&
              loss_diagnostics.last_reset_reason ==
                  AudioResetReason::backend_loss &&
              loss_diagnostics.queue_depth == 0,
          "runtime callback or device loss must stop, clear, and fall back without blocking simulation");
  }

  FakeAudioBackendControl shutdown_control;
  AudioRuntime shutdown_device{
      AudioRuntimeMode::automatic,
      std::make_unique<FakeAudioBackend>(shutdown_control, true)};
  shutdown_device.shutdown();
  check(shutdown_control.stopped &&
            shutdown_device.diagnostics().last_reset_reason ==
                AudioResetReason::shutdown &&
            shutdown_device.emit(0, {1}).status == AudioEmitStatus::stopped,
        "automatic audio shutdown must synchronously stop its callback backend");

  AudioRuntime invalid_events;
  check(invalid_events.emit(0, {}).status ==
            AudioEmitStatus::rejected_invalid_cue &&
            invalid_events.emit(10, {1}).identity ==
                AudioEventIdentity{10, 0} &&
            invalid_events.emit(10, {2}).identity ==
                AudioEventIdentity{10, 1} &&
            invalid_events.emit(9, {3}).status ==
                AudioEmitStatus::rejected_tick_regression &&
            invalid_events.emit(overflowing_tick, {4}).status ==
                AudioEmitStatus::rejected_timestamp_overflow,
        "invalid audio cues, tick regression, and timestamp overflow must fail closed");
  const auto invalid_diagnostics = invalid_events.diagnostics();
  check(invalid_diagnostics.events_rejected == 3 &&
            invalid_diagnostics.queue_depth == 2,
        "rejected audio events must not mutate the bounded queue");

  AudioRuntime saturated;
  for (std::size_t index = 0; index < kAudioEventQueueCapacity; ++index) {
    const auto emitted = saturated.emit(7, {1});
    check(emitted.status == AudioEmitStatus::queued && emitted.identity &&
              emitted.identity->sequence == index,
          "audio events inside capacity must receive ordered identities");
  }
  const auto dropped = saturated.emit(7, {1});
  check(dropped.status == AudioEmitStatus::dropped_queue_full &&
            dropped.identity &&
            dropped.identity->sequence == kAudioEventQueueCapacity,
        "a full audio queue must drop the newest assigned event");
  for (std::size_t index = 0; index < kAudioEventQueueCapacity; ++index) {
    const auto event = saturated.try_take_event();
    check(event &&
              event->identity == AudioEventIdentity{
                                     7, static_cast<std::uint16_t>(index)},
          "audio queue overflow must preserve existing FIFO order");
  }
  check(!saturated.try_take_event(),
        "draining the audio queue must expose an empty boundary");
  const auto saturated_diagnostics = saturated.diagnostics();
  check(saturated_diagnostics.events_queued == kAudioEventQueueCapacity &&
            saturated_diagnostics.events_dropped == 1 &&
            saturated_diagnostics.maximum_queue_depth ==
                kAudioEventQueueCapacity,
        "audio diagnostics must report bounded overflow truthfully");

  AudioRuntime sequence_limit;
  for (std::uint32_t index = 0;
       index <= std::numeric_limits<std::uint16_t>::max(); ++index) {
    const auto emitted = sequence_limit.emit(1, {1});
    check(emitted.identity && emitted.identity->sequence == index,
          "within-tick audio sequence assignment must remain deterministic");
  }
  check(sequence_limit.emit(1, {1}).status ==
            AudioEmitStatus::rejected_sequence_exhausted,
        "within-tick audio sequence exhaustion must be rejected");
  sequence_limit.reset(AudioResetReason::load);
  const auto after_load = sequence_limit.emit(0, {1});
  check(after_load.identity == AudioEventIdentity{0, 0} &&
            sequence_limit.diagnostics().events_discarded_on_reset ==
                kAudioEventQueueCapacity,
        "load reset must discard stale events and begin a new identity epoch");
  sequence_limit.reset(AudioResetReason::return_to_title);
  const auto after_title = sequence_limit.emit(0, {1});
  const auto title_diagnostics = sequence_limit.diagnostics();
  check(after_title.identity == AudioEventIdentity{0, 0} &&
            title_diagnostics.last_reset_reason ==
                AudioResetReason::return_to_title &&
            title_diagnostics.events_discarded_on_reset ==
                kAudioEventQueueCapacity + 1U,
        "title reset must discard stale events and begin a new identity epoch");

  AudioRuntime buffers;
  (void)buffers.emit(2, {1});
  check(buffers.render({}) == AudioBufferError::invalid_dimensions &&
            buffers.diagnostics().queue_depth == 1,
        "an empty audio callback must be rejected without consumption");
  std::array<float, 3> odd_buffer{1.0F, 2.0F, 3.0F};
  check(buffers.render(odd_buffer) == AudioBufferError::invalid_dimensions &&
            odd_buffer == std::array<float, 3>{1.0F, 2.0F, 3.0F} &&
            buffers.diagnostics().queue_depth == 1,
        "an odd audio buffer must be rejected without mutation or consumption");
  std::vector<float> oversized(
      (kMaximumAudioFramesPerCallback + 1U) * kAudioChannelCount, 4.0F);
  check(buffers.render(oversized) == AudioBufferError::invalid_dimensions &&
            std::ranges::all_of(oversized,
                                [](float value) { return value == 4.0F; }),
        "an oversized audio callback must be rejected without mutation");
  std::array<float, 4> valid_buffer{
      std::numeric_limits<float>::quiet_NaN(), 1.0F, -1.0F, 2.0F};
  check(!buffers.render(valid_buffer) &&
            std::ranges::all_of(valid_buffer,
                                [](float value) { return value == 0.0F; }) &&
            buffers.diagnostics().queue_depth == 0,
        "a valid audio callback must produce finite silence and consume events");

  AudioRuntime no_device;
  (void)no_device.emit(3, {1});
  no_device.service();
  check(no_device.diagnostics().events_dequeued == 1 &&
            no_device.diagnostics().queue_depth == 0,
        "no-device service must discard events without waveform work");
  (void)no_device.emit(4, {1});
  no_device.backend_lost();
  const auto recovered = no_device.diagnostics();
  check(recovered.backend_state == AudioBackendState::no_device &&
            recovered.backend_loss_count == 1 &&
            recovered.last_reset_reason == AudioResetReason::backend_loss &&
            recovered.queue_depth == 0,
        "backend loss must clear stale events and fall back to no-device");
  no_device.shutdown();
  std::array<float, 2> stopped_buffer{5.0F, 6.0F};
  check(no_device.emit(5, {1}).status == AudioEmitStatus::stopped &&
            no_device.render(stopped_buffer) == AudioBufferError::stopped &&
            stopped_buffer == std::array<float, 2>{5.0F, 6.0F},
        "shutdown must reject later playback without touching caller buffers");

  AudioRuntime disabled{AudioRuntimeMode::disabled};
  check(disabled.emit(0, {1}).status == AudioEmitStatus::disabled &&
            disabled.diagnostics().identities_assigned == 0 &&
            disabled.diagnostics().queue_depth == 0,
        "disabled audio must perform no identity or queue work");

  const auto enabled_immediate = replay_audio_event_trace(true, false);
  const auto enabled_delayed = replay_audio_event_trace(true, true);
  const auto disabled_replay = replay_audio_event_trace(false, true);
  check(enabled_immediate.flight_checksum != 0 &&
            enabled_immediate.flight_checksum ==
                enabled_delayed.flight_checksum &&
            enabled_immediate.flight_checksum ==
                disabled_replay.flight_checksum,
        "enabled, delayed, and disabled audio must preserve simulation checksums");
  check(!enabled_immediate.events.empty() &&
            enabled_immediate.events == enabled_delayed.events &&
            disabled_replay.events.empty(),
        "command replay must produce a stable audio trace independent of consumption delay");
}

auto fixed_step_clock_contract() -> void {
  FixedStepClock clock;
  const auto half = kSimulationStep / 2.0;

  const auto first = clock.advance(half);
  check(first && first->steps == 0,
        "a partial simulation step must remain accumulated");
  check(first && std::abs(first->interpolation_alpha - 0.5) < 0.000001,
        "the fixed-step remainder must be presentation-only interpolation");

  const auto negative = clock.advance(SimulationSeconds{-1.0});
  check(!negative && negative.error() ==
                         SimulationTimeError::negative_elapsed,
        "negative elapsed time must be rejected");
  const auto non_finite = clock.advance(SimulationSeconds{
      std::numeric_limits<double>::infinity()});
  check(!non_finite && non_finite.error() ==
                           SimulationTimeError::non_finite_elapsed,
        "non-finite elapsed time must be rejected");

  const auto second = clock.advance(half);
  check(second && second->steps == 1,
        "rejected time must not change the accumulated remainder");
  check(clock.accumulator() == SimulationSeconds::zero(),
        "an exact fixed step must leave no remainder");

  const auto stalled = clock.advance(SimulationSeconds{5.0});
  check(stalled && stalled->steps == kMaxCatchUpSteps,
        "a long stall must have bounded catch-up work");
  check(stalled &&
            std::abs(stalled->dropped.count() -
                     (5.0 - kMaxCatchUp.count())) < 0.000001,
        "a long stall must report discarded elapsed time");
  check(clock.accumulator() == SimulationSeconds::zero(),
        "discarded stall time must not remain as simulation debt");
}

[[nodiscard]] auto simulated_flight_checksum(int render_fps,
                                             int seconds,
                                             int& steps) -> std::uint64_t {
  const auto terrain = Terrain::generate(256, 0xC0FFEEU);
  if (!terrain) return 0;

  auto initialized = initial_flight_state(*terrain);
  if (!initialized) return 0;
  auto state = *initialized;
  FixedStepClock clock;
  const SimulationSeconds frame_time{1.0 / render_fps};
  for (int frame = 0; frame < render_fps * seconds; ++frame) {
    const auto advance = clock.advance(frame_time);
    if (!advance) return 0;
    steps += advance->steps;
    for (int step = 0; step < advance->steps; ++step) {
      if (!advance_flight(*terrain, state, {}, kSimulationStep)) {
        return 0;
      }
    }
  }
  return flight_state_checksum(state);
}

auto deterministic_fixed_step_flight() -> void {
  int steps_at_30{};
  int steps_at_60{};
  const auto state_at_30 = simulated_flight_checksum(30, 2, steps_at_30);
  const auto state_at_60 = simulated_flight_checksum(60, 2, steps_at_60);
  check(steps_at_30 == 240 && steps_at_60 == 240,
        "equal time at 30 and 60 FPS must execute the same fixed steps");
  check(state_at_30 != 0 && state_at_30 == state_at_60,
        "equal time at 30 and 60 FPS must produce identical flight state");
  const auto planet = generate_planet_descriptor(Seed{42});
  const auto sun_at_30 =
      resolve_local_sun(planet, static_cast<SimulationTick>(steps_at_30));
  const auto sun_at_60 =
      resolve_local_sun(planet, static_cast<SimulationTick>(steps_at_60));
  check(sun_at_30 && sun_at_60 && *sun_at_30 == *sun_at_60,
        "render cadence must not change authoritative local-sun geometry");

  const auto terrain = Terrain::generate(128, 42);
  check(terrain.has_value(), "invalid-state flight fixture must generate");
  if (!terrain) return;
  auto initialized = initial_flight_state(*terrain);
  check(initialized.has_value(), "initial flight state must be valid");
  if (!initialized) return;
  auto invalid = *initialized;
  invalid.pose.yaw = std::numeric_limits<float>::quiet_NaN();
  const auto before = flight_state_checksum(invalid);
  check(!advance_flight(*terrain, invalid, {}, kSimulationStep),
        "non-finite flight state must be rejected");
  check(flight_state_checksum(invalid) == before,
        "rejected flight state must remain untouched");
}

auto deterministic_command_replay() -> void {
  const auto commands = flight_deck_acceptance_commands();
  check(commands.size() == 18 && commands.front().tick == 0 &&
            commands.back().tick == 204 &&
            std::ranges::is_sorted(commands, {}, &FlightCommand::tick),
        "the Flight Deck command schedule must remain ordered and complete");

  const auto terrain = Terrain::generate(kFlightDeckAcceptanceTerrainSize,
                                         kFlightDeckAcceptanceSeed);
  check(terrain.has_value(), "command replay terrain must generate");
  if (!terrain) return;

  const auto first = replay_flight_deck_acceptance(*terrain);
  const auto second = replay_flight_deck_acceptance(*terrain);
  check(first && second, "the golden command stream must replay");
  if (!first || !second) return;

  const auto first_checksum = flight_state_checksum(*first);
  const auto second_checksum = flight_state_checksum(*second);
  constexpr std::uint64_t expected_checksum{15302063256845754841ULL};
  if (first_checksum != expected_checksum) {
    std::fprintf(stderr, "golden command checksum: %llu\n",
                 static_cast<unsigned long long>(first_checksum));
  }
  check(first_checksum == second_checksum,
        "replaying a command stream must reproduce its final state");
  check(first_checksum == expected_checksum,
        "the golden command stream checksum must remain stable");
  check(first->tick == kFlightDeckAcceptanceTicks &&
            first->mode == FlightMode::autopilot &&
            first->controls == FlightControls{},
        "the golden command stream must reach its expected tick and mode");

  const auto json = flight_deck_acceptance_json({
      .flight_checksum = first_checksum,
      .framebuffer_checksum = 123456789ULL,
      .render_configuration =
          resolve_render_configuration(RenderProfile::remote),
      .presentation = "ansi",
  });
  check(json.find("\"schema_version\": 1") != std::string::npos &&
            json.find("\"scenario\": \"v0.2-flight-deck\"") !=
                std::string::npos &&
            json.find("\"flight_checksum\": \"") !=
                std::string::npos &&
            json.find("\"framebuffer_checksum\": \"123456789\"") !=
                std::string::npos &&
            json.find("\"presentation\": \"ansi\"") !=
                std::string::npos,
        "the Flight Deck report must retain its versioned exact fields");
}

auto command_edge_contract() -> void {
  const auto terrain = Terrain::generate(128, 42);
  check(terrain.has_value(), "command edge terrain must generate");
  if (!terrain) return;
  const auto initialized = initial_flight_state(*terrain);
  check(initialized.has_value(), "command edge state must initialize");
  if (!initialized) return;

  auto opposed = *initialized;
  constexpr std::array conflict{
      FlightCommand{0, FlightCommandKind::press_forward},
      FlightCommand{0, FlightCommandKind::press_backward},
      FlightCommand{0, FlightCommandKind::press_turn_left},
      FlightCommand{0, FlightCommandKind::press_turn_right},
      FlightCommand{0, FlightCommandKind::press_rise},
      FlightCommand{0, FlightCommandKind::press_fall},
  };
  check(advance_flight(*terrain, opposed, conflict, kSimulationStep)
            .has_value(),
        "opposing commands must be accepted");
  check(opposed.velocity.x == 0.0F && opposed.velocity.y == 0.0F &&
            opposed.velocity.vertical == 0.0F,
        "opposing held controls must produce neutral movement");
  check(opposed.controls.forward && opposed.controls.backward &&
            opposed.controls.turn_left && opposed.controls.turn_right,
        "conflicting held controls must remain explicit in state");

  auto once = *initialized;
  auto twice = *initialized;
  constexpr std::array one_press{
      FlightCommand{0, FlightCommandKind::press_forward}};
  constexpr std::array duplicate_press{
      FlightCommand{0, FlightCommandKind::press_forward},
      FlightCommand{0, FlightCommandKind::press_forward}};
  check(advance_flight(*terrain, once, one_press, kSimulationStep).has_value(),
        "a single press must advance");
  check(advance_flight(*terrain, twice, duplicate_press, kSimulationStep)
            .has_value(),
        "a duplicate press must advance");
  check(flight_state_checksum(once) == flight_state_checksum(twice),
        "duplicate press commands must be idempotent");

  auto toggle_then_press = *initialized;
  auto press_then_toggle = *initialized;
  constexpr std::array toggle_first{
      FlightCommand{0, FlightCommandKind::toggle_autopilot},
      FlightCommand{0, FlightCommandKind::press_forward}};
  constexpr std::array toggle_last{
      FlightCommand{0, FlightCommandKind::press_forward},
      FlightCommand{0, FlightCommandKind::toggle_autopilot}};
  check(advance_flight(*terrain, toggle_then_press, toggle_first,
                       kSimulationStep)
            .has_value() &&
            toggle_then_press.mode == FlightMode::manual &&
            toggle_then_press.controls.forward,
        "a manual press after a toggle must select manual flight");
  check(advance_flight(*terrain, press_then_toggle, toggle_last,
                       kSimulationStep)
            .has_value() &&
            press_then_toggle.mode == FlightMode::autopilot &&
            press_then_toggle.controls == FlightControls{},
        "a toggle after a manual press must select autopilot and clear input");

  const auto unchanged = flight_state_checksum(*initialized);
  auto invalid = *initialized;
  constexpr std::array invalid_kind{FlightCommand{
      0, static_cast<FlightCommandKind>(std::numeric_limits<std::uint8_t>::max())}};
  const auto invalid_result =
      advance_flight(*terrain, invalid, invalid_kind, kSimulationStep);
  check(!invalid_result &&
            invalid_result.error() == FlightError::invalid_command,
        "an unknown command must be rejected");
  check(flight_state_checksum(invalid) == unchanged,
        "an unknown command must not mutate state");

  auto mistimed = *initialized;
  constexpr std::array future{
      FlightCommand{1, FlightCommandKind::press_forward}};
  const auto mistimed_result =
      advance_flight(*terrain, mistimed, future, kSimulationStep);
  check(!mistimed_result &&
            mistimed_result.error() == FlightError::wrong_command_tick,
        "a command for another tick must be rejected");
  check(flight_state_checksum(mistimed) == unchanged,
        "a mistimed command must not mutate state");

  const std::array invalid_steps{
      SimulationSeconds::zero(),
      SimulationSeconds{-kSimulationStep.count()},
      SimulationSeconds{std::numeric_limits<double>::quiet_NaN()},
      SimulationSeconds{std::numeric_limits<double>::infinity()},
      kSimulationStep / 2.0,
      kSimulationStep * 2.0,
      SimulationSeconds{0.25},
      SimulationSeconds{
          std::nextafter(kSimulationStep.count(), 0.0)},
      SimulationSeconds{std::nextafter(
          kSimulationStep.count(),
          std::numeric_limits<double>::infinity())},
  };
  for (const auto invalid_step : invalid_steps) {
    auto rejected = *initialized;
    const auto result = advance_flight(*terrain, rejected, {}, invalid_step);
    check(!result && result.error() == FlightError::invalid_step &&
              flight_state_checksum(rejected) == unchanged,
          "every non-canonical legacy step must be rejected transactionally");
  }
  auto canonical = *initialized;
  check(advance_flight(*terrain, canonical, {}, kSimulationStep) &&
            canonical.tick == initialized->tick + 1U &&
            flight_state_checksum(canonical) != unchanged,
        "the canonical legacy step must advance exactly one tick");

  auto non_finite = *initialized;
  non_finite.velocity.vertical =
      std::numeric_limits<float>::infinity();
  const auto non_finite_checksum = flight_state_checksum(non_finite);
  const auto non_finite_result =
      advance_flight(*terrain, non_finite, {}, kSimulationStep);
  check(!non_finite_result &&
            non_finite_result.error() == FlightError::invalid_state,
        "non-finite velocity must be rejected");
  check(flight_state_checksum(non_finite) == non_finite_checksum,
        "non-finite state rejection must be transactional");

  auto overflow = *initialized;
  overflow.tick = std::numeric_limits<SimulationTick>::max();
  const auto overflow_checksum = flight_state_checksum(overflow);
  const auto overflow_result =
      advance_flight(*terrain, overflow, {}, kSimulationStep);
  check(!overflow_result && overflow_result.error() == FlightError::tick_overflow,
        "the final simulation tick must not wrap");
  check(flight_state_checksum(overflow) == overflow_checksum,
        "tick overflow must not mutate state");
}

[[nodiscard]] auto key_event(termforge::Key key, char32_t ch,
                             termforge::KeyAction action)
    -> termforge::KeyEvent {
  termforge::KeyEvent event;
  event.key = key;
  event.ch = ch;
  event.action = action;
  return event;
}

[[nodiscard]] auto mouse_event(int x, int y, int button, bool pressed)
    -> termforge::MouseEvent {
  termforge::MouseEvent event;
  event.x = x;
  event.y = y;
  event.button = button;
  event.pressed = pressed;
  return event;
}

[[nodiscard]] auto command_kinds_equal(
    const std::vector<FlightCommand>& commands,
    std::initializer_list<FlightCommandKind> expected) -> bool {
  if (commands.size() != expected.size()) return false;
  return std::equal(commands.begin(), commands.end(), expected.begin(),
                    [](const FlightCommand& command,
                       FlightCommandKind kind) {
                      return command.kind == kind;
                    });
}

auto flight_input_mapping_contract() -> void {
  using apsis_drift::detail::FlightInputMapper;
  using termforge::Key;
  using termforge::KeyAction;

  struct Mapping {
    Key key;
    char32_t ch;
    FlightCommandKind press;
    FlightCommandKind release;
  };
  constexpr std::array mappings{
      Mapping{Key::Up, 0, FlightCommandKind::press_forward,
              FlightCommandKind::release_forward},
      Mapping{Key::Down, 0, FlightCommandKind::press_backward,
              FlightCommandKind::release_backward},
      Mapping{Key::Left, 0, FlightCommandKind::press_turn_left,
              FlightCommandKind::release_turn_left},
      Mapping{Key::Right, 0, FlightCommandKind::press_turn_right,
              FlightCommandKind::release_turn_right},
      Mapping{Key::Char, U'W', FlightCommandKind::press_forward,
              FlightCommandKind::release_forward},
      Mapping{Key::Char, U's', FlightCommandKind::press_backward,
              FlightCommandKind::release_backward},
      Mapping{Key::Char, U'A', FlightCommandKind::press_turn_left,
              FlightCommandKind::release_turn_left},
      Mapping{Key::Char, U'd', FlightCommandKind::press_turn_right,
              FlightCommandKind::release_turn_right},
      Mapping{Key::Char, U'Q', FlightCommandKind::press_strafe_left,
              FlightCommandKind::release_strafe_left},
      Mapping{Key::Char, U'e', FlightCommandKind::press_strafe_right,
              FlightCommandKind::release_strafe_right},
      Mapping{Key::Char, U'R', FlightCommandKind::press_rise,
              FlightCommandKind::release_rise},
      Mapping{Key::Char, U'f', FlightCommandKind::press_fall,
              FlightCommandKind::release_fall},
  };

  apsis_drift::detail::FlightInputMapper mapper;
  for (const auto& mapping : mappings) {
    mapper.enqueue(key_event(mapping.key, mapping.ch, KeyAction::Press), 7);
    mapper.enqueue(key_event(mapping.key, mapping.ch, KeyAction::Release), 7);
  }
  mapper.enqueue(key_event(Key::Char, U'w', KeyAction::Repeat), 7);
  mapper.enqueue(key_event(Key::Char, U' ', KeyAction::Press), 7);
  mapper.enqueue(key_event(Key::Char, U' ', KeyAction::Repeat), 7);
  mapper.enqueue(key_event(Key::Char, U'x', KeyAction::Press), 7);
  mapper.enqueue(key_event(Key::Char, U']', KeyAction::Press), 7);
  const auto commands = mapper.take_commands(7);
  check(commands.size() == mappings.size() * 2 + 2,
        "mapping must emit press, release, repeat, and one toggle");
  if (commands.size() == mappings.size() * 2 + 2) {
    for (std::size_t index = 0; index < mappings.size(); ++index) {
      check(commands[index * 2].kind == mappings[index].press &&
                commands[index * 2 + 1].kind ==
                    mappings[index].release,
            "each control must map to its command pair");
    }
    check(commands[commands.size() - 2].kind ==
              FlightCommandKind::press_forward,
          "a repeat must remain an idempotent press");
    check(commands.back().kind == FlightCommandKind::toggle_autopilot,
          "Space must map to one autopilot toggle");
  }

  mapper.enqueue(key_event(Key::Char, U'[', KeyAction::Press), 8, true);
  mapper.enqueue(key_event(Key::Char, U']', KeyAction::Press), 8, true);
  const auto system_commands = mapper.take_commands(8);
  check(command_kinds_equal(
            system_commands,
            {FlightCommandKind::decrease_time_scale,
             FlightCommandKind::increase_time_scale}),
        "time-scale keys must be enabled only for system flight");
}

auto mouse_flight_mapping_contract() -> void {
  using apsis_drift::detail::FlightInputMapper;
  constexpr Rect region{10, 20, 30, 30};

  FlightInputMapper mapper;
  mapper.enqueue(mouse_event(10, 20, 0, true), region, 1);
  check(command_kinds_equal(
            mapper.take_commands(1),
            {FlightCommandKind::press_forward,
             FlightCommandKind::press_turn_left}),
        "a left hold in the upper-left thirds must fly forward and turn left");

  mapper.enqueue(mouse_event(100, 100, 0, false), region, 2);
  check(command_kinds_equal(
            mapper.take_commands(2),
            {FlightCommandKind::release_forward,
             FlightCommandKind::release_turn_left}),
        "a left-button release outside the viewport must neutralize flight");

  mapper.enqueue(mouse_event(39, 49, 2, true), region, 3);
  check(command_kinds_equal(
            mapper.take_commands(3),
            {FlightCommandKind::press_strafe_right,
             FlightCommandKind::press_fall}),
        "a right hold in the lower-right thirds must strafe and descend");

  mapper.enqueue(mouse_event(25, 35, 2, true), region, 4);
  check(command_kinds_equal(
            mapper.take_commands(4),
            {FlightCommandKind::release_strafe_right,
             FlightCommandKind::release_fall}),
        "the center thirds must be neutral on both right-hold axes");

  mapper.enqueue(mouse_event(25, 35, 1, true), region, 5);
  mapper.enqueue(mouse_event(26, 35, 1, true), region, 5);
  check(command_kinds_equal(mapper.take_commands(5),
                            {FlightCommandKind::toggle_autopilot}),
        "a middle-button down edge must toggle once while events repeat");
  mapper.enqueue(mouse_event(100, 100, 1, false), region, 6);
  mapper.enqueue(mouse_event(25, 35, 1, true), region, 6);
  check(command_kinds_equal(mapper.take_commands(6),
                            {FlightCommandKind::toggle_autopilot}),
        "a released middle button must arm the next toggle");

  mapper.enqueue(mouse_event(25, 20, 0, true), region, 7);
  (void)mapper.take_commands(7);
  mapper.enqueue(mouse_event(100, 100, 0, true), region, 8);
  check(command_kinds_equal(mapper.take_commands(8),
                            {FlightCommandKind::release_forward}),
        "an outside pointer event must neutralize mouse input");

  FlightInputMapper invalid;
  invalid.enqueue(mouse_event(0, 0, 0, true), Rect{0, 0, 0, 10}, 1);
  check(invalid.take_commands(1).empty(),
        "an empty active region must ignore mouse flight input");
  constexpr int maximum = std::numeric_limits<int>::max();
  constexpr Rect extreme{maximum - 5, maximum - 5, 4, 4};
  invalid.enqueue(mouse_event(maximum - 2, maximum - 2, 2, true), extreme, 2);
  check(command_kinds_equal(
            invalid.take_commands(2),
            {FlightCommandKind::press_strafe_right,
             FlightCommandKind::press_fall}),
        "extreme valid mouse geometry must map without integer overflow");
}

auto mixed_input_ownership_contract() -> void {
  using apsis_drift::detail::FlightInputMapper;
  using termforge::Key;
  using termforge::KeyAction;
  constexpr Rect region{0, 0, 30, 30};

  FlightInputMapper mapper;
  mapper.enqueue(key_event(Key::Char, U'w', KeyAction::Press), 1);
  check(command_kinds_equal(mapper.take_commands(1),
                            {FlightCommandKind::press_forward}),
        "keyboard must press a control before mouse composition");
  mapper.enqueue(mouse_event(15, 0, 0, true), region, 2);
  check(mapper.take_commands(2).empty(),
        "mouse must not duplicate a same-direction keyboard hold");
  mapper.enqueue(key_event(Key::Char, U'w', KeyAction::Release), 3);
  check(mapper.take_commands(3).empty(),
        "keyboard release must preserve a same-direction mouse hold");
  mapper.enqueue(mouse_event(80, 80, 0, false), region, 4);
  check(command_kinds_equal(mapper.take_commands(4),
                            {FlightCommandKind::release_forward}),
        "the last source release must neutralize the shared control");

  mapper.enqueue(key_event(Key::Char, U'r', KeyAction::Press), 5);
  mapper.enqueue(mouse_event(15, 0, 2, true), region, 5);
  (void)mapper.take_commands(5);
  mapper.neutralize_mouse(6);
  check(mapper.take_commands(6).empty(),
        "pointer loss must preserve keyboard-owned controls");
  mapper.enqueue(key_event(Key::Char, U'r', KeyAction::Release), 7);
  check(command_kinds_equal(mapper.take_commands(7),
                            {FlightCommandKind::release_rise}),
        "keyboard must remain usable after pointer neutralization");

  FlightInputMapper simultaneous;
  simultaneous.enqueue(mouse_event(15, 15, 1, true), region, 9);
  simultaneous.enqueue(key_event(Key::Char, U'w', KeyAction::Press), 9);
  check(command_kinds_equal(
            simultaneous.take_commands(9),
            {FlightCommandKind::toggle_autopilot,
             FlightCommandKind::press_forward}),
        "same-tick pointer toggles must precede manual keyboard commands");

  FlightInputMapper opposing;
  opposing.enqueue(key_event(Key::Char, U'w', KeyAction::Press), 0);
  opposing.enqueue(mouse_event(15, 29, 0, true), region, 0);
  const auto commands = opposing.take_commands(0);
  check(command_kinds_equal(commands,
                            {FlightCommandKind::press_forward,
                             FlightCommandKind::press_backward}),
        "opposing keyboard and mouse directions must remain explicit");

  const auto terrain = Terrain::generate(128, 42);
  check(terrain.has_value(), "mixed-input cancellation terrain must generate");
  if (terrain) {
    auto state = initial_flight_state(*terrain);
    check(state.has_value(), "mixed-input cancellation state must initialize");
    if (state) {
      check(advance_flight(*terrain, *state, commands, kSimulationStep)
                .has_value(),
            "opposing mixed commands must remain a valid simulation step");
      check(close_enough(state->velocity.x, 0.0F) &&
                close_enough(state->velocity.y, 0.0F),
            "opposing mixed inputs must cancel through simulation rules");
    }
  }
}

auto suspended_input_contract() -> void {
  using apsis_drift::detail::FlightInputMapper;
  using termforge::Key;
  using termforge::KeyAction;

  FlightInputMapper mapper;
  mapper.enqueue(key_event(Key::Char, U'w', KeyAction::Press), 4);
  mapper.enqueue(mouse_event(0, 0, 2, true), Rect{0, 0, 30, 30}, 4);

  FlightControls applied;
  applied.forward = true;
  applied.strafe_left = true;
  applied.rise = true;
  mapper.suspend(applied, 4);
  check(command_kinds_equal(
            mapper.take_commands(4),
            {FlightCommandKind::release_forward,
             FlightCommandKind::release_strafe_left,
             FlightCommandKind::release_rise}),
        "menu entry must drop unapplied input and release authoritative holds");
  check(mapper.take_commands(4).empty(),
        "suspension releases must be consumed exactly once");

  mapper.enqueue(key_event(Key::Char, U'e', KeyAction::Press), 5);
  check(command_kinds_equal(mapper.take_commands(5),
                            {FlightCommandKind::press_strafe_right}),
        "keyboard input must work after menu suspension");

  const auto terrain = Terrain::generate(128, 42);
  check(terrain.has_value(), "pause checksum terrain must generate");
  if (!terrain) return;
  auto state = initial_flight_state(*terrain);
  check(state.has_value(), "pause checksum state must initialize");
  if (!state) return;
  state->mode = FlightMode::manual;
  state->controls.forward = true;
  const auto paused_checksum = flight_state_checksum(*state);

  FlightInputMapper paused_mapper;
  paused_mapper.suspend(state->controls, state->tick);
  FixedStepClock clock;
  const auto partial = clock.advance(kSimulationStep / 2.0);
  check(partial && partial->steps == 0,
        "the pause clock test must begin with a partial step");
  clock.reset();
  for (int render = 0; render < 1000; ++render) {
    check(flight_state_checksum(*state) == paused_checksum,
          "paused render cadence must not mutate authoritative flight state");
  }
  check(clock.accumulator() == SimulationSeconds::zero(),
        "menu time must not remain as simulation debt");

  const auto releases = paused_mapper.take_commands(state->tick);
  check(advance_flight(*terrain, *state, releases, kSimulationStep)
            .has_value() &&
            !state->controls.forward,
        "the first resumed tick must neutralize held flight controls");
}

auto mouse_event_coalescing_contract() -> void {
  using apsis_drift::detail::FlightInputMapper;
  constexpr Rect region{0, 0, 30, 30};
  FlightInputMapper mapper;
  for (int event = 0; event < 10000; ++event) {
    const int x = event % 2 == 0 ? 0 : 29;
    const int y = event % 4 < 2 ? 0 : 29;
    mapper.enqueue(mouse_event(x, y, 0, true), region, 11);
  }
  const auto commands = mapper.take_commands(11);
  check(commands.size() <= 8,
        "one tick of pointer changes must produce a constant-size backlog");
  check(mapper.take_commands(11).empty(),
        "coalesced pointer commands must be consumed exactly once");
}

[[nodiscard]] auto replay_equivalent_control_trace(bool use_mouse)
    -> std::uint64_t {
  const auto terrain = Terrain::generate(256, 0xC0FFEEU);
  if (!terrain) return 0;
  auto initialized = initial_flight_state(*terrain);
  if (!initialized) return 0;

  constexpr Rect region{0, 0, 30, 30};
  auto state = *initialized;
  apsis_drift::detail::FlightInputMapper mapper;
  for (SimulationTick tick = 0; tick < 180; ++tick) {
    if (tick == 0) {
      if (use_mouse) {
        mapper.enqueue(mouse_event(15, 15, 1, true), region, tick);
        mapper.enqueue(mouse_event(15, 0, 0, true), region, tick);
      } else {
        mapper.enqueue(key_event(termforge::Key::Char, U' ',
                                 termforge::KeyAction::Press),
                       tick);
        mapper.enqueue(key_event(termforge::Key::Char, U'w',
                                 termforge::KeyAction::Press),
                       tick);
      }
    } else if (tick == 24) {
      if (use_mouse) {
        mapper.enqueue(mouse_event(29, 0, 0, true), region, tick);
      } else {
        mapper.enqueue(key_event(termforge::Key::Right, 0,
                                 termforge::KeyAction::Press),
                       tick);
      }
    } else if (tick == 72) {
      if (use_mouse) {
        mapper.enqueue(mouse_event(15, 0, 0, true), region, tick);
      } else {
        mapper.enqueue(key_event(termforge::Key::Right, 0,
                                 termforge::KeyAction::Release),
                       tick);
      }
    } else if (tick == 96) {
      if (use_mouse) {
        mapper.enqueue(mouse_event(80, 80, 0, false), region, tick);
      } else {
        mapper.enqueue(key_event(termforge::Key::Char, U'w',
                                 termforge::KeyAction::Release),
                       tick);
      }
    }
    const auto commands = mapper.take_commands(tick);
    if (!advance_flight(*terrain, state, commands, kSimulationStep)) return 0;
  }
  return flight_state_checksum(state);
}

auto equivalent_mouse_keyboard_trace_contract() -> void {
  const auto keyboard = replay_equivalent_control_trace(false);
  const auto mouse = replay_equivalent_control_trace(true);
  check(keyboard != 0 && keyboard == mouse,
        "equivalent mouse and keyboard actions must produce one checksum");
}

auto capability_floor_contract() -> void {
  using apsis_drift::detail::DriverChoice;
  using apsis_drift::detail::KeyboardChoice;
  using apsis_drift::detail::flight_deck_requirements;
  using apsis_drift::detail::forced_capabilities;

  const auto requirements = flight_deck_requirements();
  check(requirements.truecolor && requirements.key_repeat &&
            requirements.key_release && !requirements.graphics,
        "the Flight Deck floor must accept Kitty or ANSI truecolor with "
        "repeat/release input");
  check(!forced_capabilities(DriverChoice::automatic,
                             KeyboardChoice::enhanced),
        "automatic mode must preserve normal capability probing");

  const auto kitty =
      forced_capabilities(DriverChoice::kitty, KeyboardChoice::enhanced);
  check(kitty && kitty->kitty_graphics && kitty->truecolor &&
            kitty->kitty_keyboard,
        "forced Kitty must provide truecolor and enhanced input");

  const auto ansi =
      forced_capabilities(DriverChoice::ansi, KeyboardChoice::enhanced);
  check(ansi && !ansi->kitty_graphics && ansi->truecolor &&
            ansi->kitty_keyboard,
        "forced ANSI must combine truecolor with enhanced input");

  const auto missing_truecolor =
      forced_capabilities(DriverChoice::fallback, KeyboardChoice::enhanced);
  check(missing_truecolor && !missing_truecolor->truecolor &&
            missing_truecolor->kitty_keyboard,
        "forced fallback must isolate a missing-truecolor refusal");

  const auto missing_release =
      forced_capabilities(DriverChoice::ansi, KeyboardChoice::press_only);
  check(missing_release && missing_release->truecolor &&
            !missing_release->kitty_keyboard,
        "forced press-only input must isolate a missing-release refusal");
}

struct TimedKeyEvent {
  SimulationTick tick{};
  termforge::KeyEvent event;
};

[[nodiscard]] auto replay_key_trace(int render_fps) -> std::uint64_t {
  const auto terrain = Terrain::generate(256, 0xC0FFEEU);
  if (!terrain) return 0;
  auto initialized = initial_flight_state(*terrain);
  if (!initialized) return 0;

  const std::array trace{
      TimedKeyEvent{0, key_event(termforge::Key::Char, U' ',
                                 termforge::KeyAction::Press)},
      TimedKeyEvent{0, key_event(termforge::Key::Char, U'w',
                                 termforge::KeyAction::Press)},
      TimedKeyEvent{24, key_event(termforge::Key::Char, U'w',
                                  termforge::KeyAction::Repeat)},
      TimedKeyEvent{36, key_event(termforge::Key::Right, 0,
                                  termforge::KeyAction::Press)},
      TimedKeyEvent{72, key_event(termforge::Key::Char, U'w',
                                  termforge::KeyAction::Release)},
      TimedKeyEvent{96, key_event(termforge::Key::Right, 0,
                                  termforge::KeyAction::Release)},
      TimedKeyEvent{120, key_event(termforge::Key::Char, U'r',
                                   termforge::KeyAction::Press)},
      TimedKeyEvent{144, key_event(termforge::Key::Char, U'r',
                                   termforge::KeyAction::Release)},
      TimedKeyEvent{180, key_event(termforge::Key::Char, U' ',
                                   termforge::KeyAction::Press)},
  };

  auto state = *initialized;
  apsis_drift::detail::FlightInputMapper mapper;
  FixedStepClock clock;
  std::size_t next_event{};
  const SimulationSeconds frame_time{1.0 / render_fps};
  for (int frame = 0; frame < render_fps * 2; ++frame) {
    const auto advance = clock.advance(frame_time);
    if (!advance) return 0;
    for (int step = 0; step < advance->steps; ++step) {
      while (next_event < trace.size() && trace[next_event].tick == state.tick) {
        mapper.enqueue(trace[next_event].event, state.tick);
        ++next_event;
      }
      const auto tick_commands = mapper.take_commands(state.tick);
      if (!advance_flight(*terrain, state, tick_commands, kSimulationStep)) {
        return 0;
      }
    }
  }
  return flight_state_checksum(state);
}

auto deterministic_key_trace_contract() -> void {
  const auto at_30 = replay_key_trace(30);
  const auto at_60 = replay_key_trace(60);
  check(at_30 != 0 && at_30 == at_60,
        "normalized press/repeat/release traces must be deterministic across "
        "render cadences");
}

[[nodiscard]] auto replay_mixed_input_trace(int render_fps) -> std::uint64_t {
  const auto terrain = Terrain::generate(256, 0xC0FFEEU);
  if (!terrain) return 0;
  auto initialized = initial_flight_state(*terrain);
  if (!initialized) return 0;

  constexpr Rect region{0, 0, 30, 30};
  auto state = *initialized;
  apsis_drift::detail::FlightInputMapper mapper;
  FixedStepClock clock;
  const SimulationSeconds frame_time{1.0 / render_fps};
  for (int frame = 0; frame < render_fps * 2; ++frame) {
    const auto advance = clock.advance(frame_time);
    if (!advance) return 0;
    for (int step = 0; step < advance->steps; ++step) {
      const auto tick = state.tick;
      if (tick == 0) {
        mapper.enqueue(key_event(termforge::Key::Char, U' ',
                                 termforge::KeyAction::Press),
                       tick);
        mapper.enqueue(mouse_event(15, 0, 0, true), region, tick);
      } else if (tick == 24) {
        mapper.enqueue(mouse_event(29, 0, 0, true), region, tick);
      } else if (tick == 36) {
        mapper.enqueue(key_event(termforge::Key::Right, 0,
                                 termforge::KeyAction::Press),
                       tick);
      } else if (tick == 72) {
        mapper.enqueue(mouse_event(15, 0, 0, true), region, tick);
      } else if (tick == 96) {
        mapper.enqueue(key_event(termforge::Key::Right, 0,
                                 termforge::KeyAction::Release),
                       tick);
      } else if (tick == 120) {
        mapper.enqueue(mouse_event(15, 0, 2, true), region, tick);
      } else if (tick == 144) {
        mapper.neutralize_mouse(tick);
      } else if (tick == 180) {
        mapper.enqueue(mouse_event(15, 15, 1, true), region, tick);
      }
      const auto commands = mapper.take_commands(tick);
      if (!advance_flight(*terrain, state, commands, kSimulationStep)) {
        return 0;
      }
    }
  }
  return flight_state_checksum(state);
}

auto deterministic_mixed_input_trace_contract() -> void {
  const auto at_30 = replay_mixed_input_trace(30);
  const auto at_60 = replay_mixed_input_trace(60);
  check(at_30 != 0 && at_30 == at_60,
        "mixed mouse and keyboard traces must be deterministic across "
        "render cadences");
}

auto camera_derivation_contract() -> void {
  const auto terrain = Terrain::generate(128, 42);
  check(terrain.has_value(), "camera derivation terrain must generate");
  if (!terrain) return;
  const auto initialized = initial_flight_state(*terrain);
  check(initialized.has_value(), "camera derivation state must initialize");
  if (!initialized) return;
  auto state = *initialized;
  state.pose = {.x = 12.5F, .y = 31.25F, .altitude = 98.0F, .yaw = 1.25F};
  const auto camera = derive_camera(state);
  check(camera && camera->x == state.pose.x && camera->y == state.pose.y &&
            camera->height == state.pose.altitude &&
            camera->yaw == state.pose.yaw && camera->pitch == 0.0F,
        "the render camera must derive directly from authoritative pose");

  const auto checksum = flight_state_checksum(state);
  if (camera) {
    auto presentation = *camera;
    presentation.pitch += 0.1F;
    check(presentation.pitch != camera->pitch,
          "camera pitch must remain independently adjustable");
  }
  check(flight_state_checksum(state) == checksum,
        "presentation-only camera changes must not alter flight state");
}

auto render_failure_matrix() -> void {
  const auto terrain = Terrain::generate(128, 42);
  check(terrain.has_value(), "render fixture terrain must generate");
  if (!terrain) return;

  VoxelRenderer renderer{{.width = 160,
                          .height = 120,
                          .field_of_view_degrees = 72.0F,
                          .max_distance = 300.0F,
                          .fog_start = 140.0F}};
  Camera camera;
  std::vector<Pixel> short_buffer(160U * 120U - 1U, {1, 2, 3, 4});
  check(!renderer.render(*terrain, camera, short_buffer),
        "a short framebuffer must be rejected");
  check(std::all_of(short_buffer.begin(), short_buffer.end(),
                    [](Pixel pixel) { return pixel == Pixel{1, 2, 3, 4}; }),
        "a rejected framebuffer must remain untouched");

  VoxelRenderer invalid{{.width = 0, .height = 120}};
  std::vector<Pixel> empty;
  check(!invalid.render(*terrain, camera, empty),
        "invalid renderer dimensions must be rejected");

  std::vector<Pixel> frame(160U * 120U, {5, 6, 7, 8});
  camera.yaw = std::numeric_limits<float>::quiet_NaN();
  check(!renderer.render(*terrain, camera, frame),
        "a non-finite camera must be rejected");
  check(std::all_of(frame.begin(), frame.end(),
                    [](Pixel pixel) { return pixel == Pixel{5, 6, 7, 8}; }),
        "a rejected camera must leave the framebuffer untouched");

  camera.yaw = 0.0F;
  auto invalid_sun_settings = renderer.settings();
  invalid_sun_settings.sun_direction.x =
      std::numeric_limits<float>::infinity();
  VoxelRenderer invalid_sun{invalid_sun_settings};
  check(!invalid_sun.render(*terrain, camera, frame),
        "a non-finite sun direction must be rejected");
  check(std::all_of(frame.begin(), frame.end(),
                    [](Pixel pixel) { return pixel == Pixel{5, 6, 7, 8}; }),
        "a rejected sun direction must leave the framebuffer untouched");

  auto zero_sun_settings = renderer.settings();
  zero_sun_settings.sun_direction = {};
  VoxelRenderer zero_sun{zero_sun_settings};
  check(!zero_sun.render(*terrain, camera, frame),
        "a zero sun direction must be rejected");
  check(std::all_of(frame.begin(), frame.end(),
                    [](Pixel pixel) { return pixel == Pixel{5, 6, 7, 8}; }),
        "a rejected zero sun must leave the framebuffer untouched");
}

auto camera_projection_contract() -> void {
  constexpr float pi{3.14159265358979323846F};
  RenderSettings settings;
  Camera camera;
  camera.x = 0.0F;
  camera.y = 0.0F;
  camera.height = 100.0F;
  camera.yaw = 0.0F;
  camera.pitch = 0.0F;

  const auto forward =
      project_world_direction(camera, {1.0F, 0.0F, 0.0F}, settings);
  check(forward && *forward && close_enough((*forward)->x, 0.0F) &&
            close_enough((*forward)->y, 0.0F),
        "camera-forward direction must project to viewport center");

  const auto right =
      project_world_direction(camera, {1.0F, 0.25F, 0.0F}, settings);
  check(right && *right && (*right)->x > 0.0F &&
            close_enough((*right)->y, 0.0F),
        "a world direction to camera right must project right of center");

  const auto behind =
      project_world_direction(camera, {-1.0F, 0.0F, 0.0F}, settings);
  check(behind && !*behind,
        "a direction behind the camera must not produce a projection");

  const auto outside =
      project_world_direction(camera, {1.0F, 2.0F, 0.0F}, settings);
  check(outside && *outside && (*outside)->x > 1.0F,
        "an off-screen direction must retain an out-of-range coordinate");

  const auto zero = project_world_direction(camera, {}, settings);
  check(!zero && zero.error() == ProjectionError::zero_direction,
        "a zero-length direction must be rejected explicitly");
  const auto non_finite = project_world_direction(
      camera,
      {1.0F, std::numeric_limits<float>::quiet_NaN(), 0.0F}, settings);
  check(!non_finite &&
            non_finite.error() == ProjectionError::non_finite_direction,
        "a non-finite direction must be rejected explicitly");
  auto invalid_settings = settings;
  invalid_settings.field_of_view_degrees = 180.0F;
  const auto invalid_fov =
      project_world_direction(camera, {1.0F, 0.0F, 0.0F}, invalid_settings);
  check(!invalid_fov &&
            invalid_fov.error() == ProjectionError::invalid_field_of_view,
        "an invalid field of view must be rejected explicitly");
  invalid_settings = settings;
  invalid_settings.width = 0;
  const auto invalid_viewport = project_local_horizon(camera, invalid_settings);
  check(!invalid_viewport &&
            invalid_viewport.error() == ProjectionError::invalid_viewport,
        "an invalid projection viewport must be rejected explicitly");

  const auto level_horizon = project_local_horizon(camera, settings);
  const auto sun_before_turn =
      project_world_direction(camera, kLocalSunDirection, settings);
  camera.yaw = pi * 0.1F;
  const auto turned_horizon = project_local_horizon(camera, settings);
  const auto sun_after_turn =
      project_world_direction(camera, kLocalSunDirection, settings);
  check(level_horizon && turned_horizon &&
            close_enough(*level_horizon, *turned_horizon),
        "turning a level camera must not move the local horizon");
  check(sun_before_turn && *sun_before_turn && sun_after_turn &&
            *sun_after_turn &&
            !close_enough((*sun_before_turn)->x, (*sun_after_turn)->x),
        "turning must move the projected world-space sun");

  camera.yaw = 0.35F;
  const auto level_sun =
      project_world_direction(camera, kLocalSunDirection, settings);
  camera.pitch = 0.1F;
  const auto pitched_horizon = project_local_horizon(camera, settings);
  const auto pitched_sun =
      project_world_direction(camera, kLocalSunDirection, settings);
  check(pitched_horizon && level_horizon &&
            *pitched_horizon > *level_horizon,
        "positive pitch must move the local horizon downward");
  check(level_sun && *level_sun && pitched_sun && *pitched_sun &&
            (*pitched_sun)->y < (*level_sun)->y,
        "positive pitch must move a visible world-space sun downward");

  constexpr std::array profiles{
      RenderProfile::remote, RenderProfile::balanced, RenderProfile::local,
      RenderProfile::cinematic};
  std::optional<float> horizon_per_width;
  std::optional<float> sun_vertical_per_aspect;
  for (const auto profile : profiles) {
    const auto viewport = profile_viewport(profile);
    settings.width = viewport.width;
    settings.height = viewport.height;
    const auto horizon = project_local_horizon(camera, settings);
    const auto sun =
        project_world_direction(camera, kLocalSunDirection, settings);
    check(horizon && sun && *sun,
          "every named profile must project the same camera and sun");
    if (!horizon || !sun || !*sun) continue;
    const float centered_horizon =
        *horizon - static_cast<float>(viewport.height - 1) * 0.5F;
    const float normalized_horizon =
        centered_horizon / static_cast<float>(viewport.width);
    const float aspect = static_cast<float>(viewport.width) /
                         static_cast<float>(viewport.height);
    const float normalized_sun = (**sun).y / aspect;
    if (!horizon_per_width) {
      horizon_per_width = normalized_horizon;
      sun_vertical_per_aspect = normalized_sun;
    } else {
      check(close_enough(normalized_horizon, *horizon_per_width),
            "pitch horizon displacement must scale with projection width");
      check(close_enough(normalized_sun, *sun_vertical_per_aspect),
            "sun projection must account for each viewport aspect ratio");
    }
  }
}

auto world_sun_render_contract() -> void {
  constexpr Pixel sun_color{247, 220, 151, 255};
  const auto terrain = Terrain::generate(128, 0xC0FFEEU);
  check(terrain.has_value(), "sun render terrain must generate");
  if (!terrain) return;

  RenderSettings settings{.width = 160,
                          .height = 120,
                          .field_of_view_degrees = 72.0F,
                          .max_distance = 80.0F,
                          .fog_start = 40.0F,
                          .sun_direction = {1.0F, 0.0F, 0.35F}};
  Camera camera;
  camera.yaw = 0.0F;
  camera.pitch = 0.0F;
  camera.height = 300.0F;
  std::vector<Pixel> frame(160U * 120U);
  VoxelRenderer visible{settings};
  check(visible.render(*terrain, camera, frame) &&
            count_pixels(frame, sun_color) > 0,
        "an in-front sun above the local horizon must be visible");

  settings.sun_direction = {-1.0F, 0.0F, 0.35F};
  VoxelRenderer behind{settings};
  check(behind.render(*terrain, camera, frame) &&
            count_pixels(frame, sun_color) == 0,
        "a sun behind the camera must be absent");
  const auto first_lighting = pixel_checksum(frame);

  settings.sun_direction = {-1.0F, 0.4F, 0.35F};
  VoxelRenderer shifted_lighting{settings};
  check(shifted_lighting.render(*terrain, camera, frame) &&
            count_pixels(frame, sun_color) == 0 &&
            pixel_checksum(frame) != first_lighting,
        "terrain lighting must follow the same world-space sun direction");

  settings.sun_direction = {1.0F, 0.0F, -0.1F};
  VoxelRenderer below{settings};
  check(below.render(*terrain, camera, frame) &&
            count_pixels(frame, sun_color) == 0,
        "a sun below the local geometric horizon must be absent");

  settings.sun_direction = {1.0F, 4.0F, 0.35F};
  VoxelRenderer outside{settings};
  check(outside.render(*terrain, camera, frame) &&
            count_pixels(frame, sun_color) == 0,
        "a sun outside the viewport must be absent");

  settings.sun_direction = {1.0F, 0.0F, 0.02F};
  camera.height =
      std::max<float>(terrain->height_at(180, 240), kWaterLevel) + 16.0F;
  settings.max_distance = 300.0F;
  VoxelRenderer occluded{settings};
  check(occluded.render(*terrain, camera, frame) &&
            count_pixels(frame, sun_color) == 0,
        "terrain must occlude a low projected sun");
}

auto deterministic_render() -> void {
  const auto terrain = Terrain::generate(256, 0xC0FFEEU);
  check(terrain.has_value(), "render terrain must generate");
  if (!terrain) return;

  RenderSettings settings{.width = 160,
                          .height = 120,
                          .field_of_view_degrees = 72.0F,
                          .max_distance = 420.0F,
                          .fog_start = 180.0F};
  VoxelRenderer renderer{settings};
  Camera camera;
  camera.height = std::max<float>(terrain->height_at(180, 240), kWaterLevel) +
                  48.0F;
  std::vector<Pixel> first(160U * 120U);
  std::vector<Pixel> second(160U * 120U);
  check(renderer.render(*terrain, camera, first),
        "a correctly sized framebuffer must render");
  check(renderer.render(*terrain, camera, second),
        "the renderer must be reusable");
  check(first == second, "an unchanged camera must render deterministically");
  check(std::all_of(first.begin(), first.end(),
                    [](Pixel pixel) { return pixel.a == 255; }),
        "every rendered pixel must be opaque");

  const auto original = pixel_checksum(first);
  camera.yaw += 0.4F;
  check(renderer.render(*terrain, camera, second),
        "a moved camera must still render");
  check(original != pixel_checksum(second),
        "camera rotation must change the rendered frame");
}

auto golden_profile_renders() -> void {
  const auto terrain = Terrain::generate(128, 0x39C0FFEEU);
  check(terrain.has_value(), "golden profile terrain must generate");
  if (!terrain) return;

  struct GoldenProfile {
    RenderProfile profile;
    std::uint64_t checksum;
  };
  constexpr std::array profiles{
      GoldenProfile{RenderProfile::remote, 2430554823040236521ULL},
      GoldenProfile{RenderProfile::balanced, 17592776064996281288ULL},
      GoldenProfile{RenderProfile::local, 3870257458047887296ULL},
      GoldenProfile{RenderProfile::cinematic, 9168379169038547107ULL},
  };

  Camera camera;
  camera.x = 64.0F;
  camera.y = 64.0F;
  camera.height =
      std::max<float>(terrain->height_at(64, 64), kWaterLevel) + 54.0F;
  camera.yaw = 0.0F;
  camera.pitch = 0.0F;

  for (const auto golden : profiles) {
    const auto viewport = profile_viewport(golden.profile);
    RenderSettings settings;
    settings.width = viewport.width;
    settings.height = viewport.height;
    settings.field_of_view_degrees = 90.0F;
    settings.max_distance = 180.0F;
    settings.fog_start = 90.0F;
    settings.sun_direction = {1.0F, 0.0F, 0.5F};
    VoxelRenderer renderer{settings};
    std::vector<Pixel> first(static_cast<std::size_t>(viewport.width) *
                             static_cast<std::size_t>(viewport.height));
    std::vector<Pixel> second(first.size());
    check(renderer.render(*terrain, camera, first) &&
              renderer.render(*terrain, camera, second),
          "each golden profile camera must render twice");
    const auto checksum = pixel_checksum(first);
    if (checksum != golden.checksum) {
      std::fprintf(stderr, "%.*s golden framebuffer checksum: %llu\n",
                   static_cast<int>(profile_name(golden.profile).size()),
                   profile_name(golden.profile).data(),
                   static_cast<unsigned long long>(checksum));
    }
    check(first == second,
          "identical profile camera and sun state must render identically");
    check(checksum == golden.checksum,
          "golden profile framebuffer checksum must remain stable");
  }
}

auto required_viewport_matrix() -> void {
  const auto terrain = Terrain::generate(128, 0xC0FFEEU);
  check(terrain.has_value(), "viewport render terrain must generate");
  if (!terrain) return;

  constexpr std::array sizes{
      ViewportSize{320, 240}, ViewportSize{512, 320},
      ViewportSize{640, 360}, ViewportSize{640, 480},
      ViewportSize{800, 600}, ViewportSize{1024, 768}};
  for (const auto size : sizes) {
    RenderSettings settings;
    settings.width = size.width;
    settings.height = size.height;
    settings.max_distance = 180.0F;
    settings.fog_start = 90.0F;
    VoxelRenderer renderer{settings};
    Camera camera;
    camera.height =
        std::max<float>(terrain->height_at(180, 240), kWaterLevel) +
        48.0F;
    std::vector<Pixel> frame(static_cast<std::size_t>(size.width) *
                             static_cast<std::size_t>(size.height));
    check(renderer.render(*terrain, camera, frame),
          "every required viewport must render a complete frame");
    check(std::all_of(frame.begin(), frame.end(),
                      [](Pixel pixel) { return pixel.a == 255; }),
          "every required viewport must produce opaque pixels");
  }

  VoxelRenderer over_budget{{.width = 4096, .height = 1025}};
  std::vector<Pixel> empty;
  check(!over_budget.render(*terrain, Camera{}, empty),
        "an over-budget renderer must reject work without a framebuffer");
}

[[nodiscard]] auto orbital_camera_for(const PlanetDescriptor& planet,
                                      double distance_scale = 3.5)
    -> OrbitalCamera {
  const double radius = static_cast<double>(planet.radius.value) * 1'000.0;
  OrbitalCamera camera;
  camera.position = {0.0, -radius * distance_scale, radius * 0.20};
  camera.forward = {-camera.position.x, -camera.position.y,
                    -camera.position.z};
  camera.up = {0.0, 0.0, 1.0};
  return camera;
}

inline constexpr PlanetFixedDirection kOrbitalTestLight{-0.4, -0.6, 0.7};

auto celestial_geometry_contract() -> void {
  check(kLocalSunGeneratorVersion == 1 && kLocalDayTicks == 72'000,
        "local-sun version 1 must retain its ten-minute fixed-step cycle");
  const auto planet = generate_planet_descriptor(Seed{42});
  const auto first = resolve_local_sun(planet, 0);
  const auto again = resolve_local_sun(planet, 0);
  const auto half = resolve_local_sun(planet, kLocalDayTicks / 2);
  const auto full = resolve_local_sun(planet, kLocalDayTicks);
  check(first && again && half && full && *first == *again &&
            first->planet_to_sun == full->planet_to_sun &&
            first->cycle_tick == full->cycle_tick &&
            close_enough(first->planet_to_sun.x, -half->planet_to_sun.x,
                         2.0e-9) &&
            close_enough(first->planet_to_sun.y, -half->planet_to_sun.y,
                         2.0e-9) &&
            first->planet_to_sun.z == half->planet_to_sun.z,
        "local-sun geometry must repeat exactly and cross the opposite meridian at half-cycle");
  if (first) {
    const auto noon = local_solar_elevation(*first, first->planet_to_sun);
    check(noon && close_enough(*noon, 1.0, 1.0e-9),
          "a surface normal facing the sun must resolve local noon");
  }
  const auto other =
      resolve_local_sun(generate_planet_descriptor(Seed{43}), 0);
  check(first && other && first->planet_to_sun != other->planet_to_sun,
        "independent planet seeds must produce different celestial geometry");
  check(resolve_local_sun(planet_with_radius(planet, 0), 0) ==
            std::unexpected{LocalSunError::invalid_planet},
        "invalid generated planet identity must be rejected by the sun model");
  LocalSunGeometry invalid{};
  invalid.planet_to_sun.x = std::numeric_limits<double>::quiet_NaN();
  check(local_solar_elevation(invalid, {1.0, 0.0, 0.0}) ==
            std::unexpected{LocalSunError::invalid_geometry},
        "non-finite celestial geometry must be rejected before presentation");
}

auto orbital_sun_occlusion_contract() -> void {
  const auto planet = generate_planet_descriptor(Seed{42});
  const auto aligned = resolve_local_sun(planet, kLocalDayTicks);
  check(aligned.has_value(), "the orbital sun fixture must resolve");
  if (!aligned) return;
  const double radius = static_cast<double>(planet.radius.value) * 1'000.0;
  OrbitalCamera camera;
  camera.position = {-aligned->planet_to_sun.x * radius * 3.5,
                     -aligned->planet_to_sun.y * radius * 3.5,
                     -aligned->planet_to_sun.z * radius * 3.5};
  camera.forward = aligned->planet_to_sun;
  camera.up = {0.0, 0.0, 1.0};
  const OrbitalRenderer renderer{{.width = 320,
                                  .height = 240,
                                  .field_of_view_degrees = 60.0}};
  std::vector<Pixel> visible_frame(320U * 240U);
  std::vector<Pixel> occluded_frame(visible_frame.size());
  std::vector<Pixel> reemerged_frame(visible_frame.size());
  constexpr SimulationTick limb_offset{5'200};
  const auto visible_sun =
      resolve_local_sun(planet, kLocalDayTicks - limb_offset);
  const auto reemerged_sun =
      resolve_local_sun(planet, kLocalDayTicks + limb_offset);
  const auto visible = visible_sun
                           ? renderer.render(planet, camera,
                                             visible_sun->planet_to_sun,
                                             visible_frame)
                           : std::expected<OrbitalRenderStats,
                                           OrbitalRenderError>{
                                 std::unexpected{
                                     OrbitalRenderError::invalid_light_direction}};
  const auto occluded = renderer.render(
      planet, camera, aligned->planet_to_sun, occluded_frame);
  const auto reemerged = reemerged_sun
                             ? renderer.render(planet, camera,
                                               reemerged_sun->planet_to_sun,
                                               reemerged_frame)
                             : std::expected<OrbitalRenderStats,
                                             OrbitalRenderError>{
                                   std::unexpected{
                                       OrbitalRenderError::invalid_light_direction}};
  check(visible && occluded && reemerged && visible->sun_pixels > 0 &&
            occluded->sun_pixels == 0 && reemerged->sun_pixels > 0 &&
            pixel_checksum(visible_frame) != pixel_checksum(occluded_frame) &&
            pixel_checksum(reemerged_frame) != pixel_checksum(occluded_frame),
        "the deterministic sun must disappear behind the planet and re-emerge on the opposite limb");
}

auto orbital_render_failure_matrix() -> void {
  const auto planet = generate_planet_descriptor(Seed{42});
  const OrbitalRenderSettings settings{.width = 160,
                                       .height = 120,
                                       .field_of_view_degrees = 60.0};
  const OrbitalRenderer renderer{settings};
  const auto camera = orbital_camera_for(planet);

  std::vector<Pixel> short_frame(160U * 120U - 1U, {1, 2, 3, 4});
  const auto short_result =
      renderer.render(planet, camera, kOrbitalTestLight, short_frame);
  check(!short_result &&
            short_result.error() == OrbitalRenderError::invalid_framebuffer,
        "an orbital renderer must reject a short framebuffer");
  check(std::ranges::all_of(short_frame, [](Pixel value) {
          return value == Pixel{1, 2, 3, 4};
        }),
        "a rejected orbital framebuffer must remain untouched");

  std::vector<Pixel> frame(160U * 120U, {5, 6, 7, 8});
  const auto check_untouched = [&frame](const auto& result,
                                      OrbitalRenderError error,
                                      const char* message) {
    check(!result && result.error() == error, message);
    check(std::ranges::all_of(frame, [](Pixel value) {
            return value == Pixel{5, 6, 7, 8};
          }),
          "invalid orbital input must leave the framebuffer untouched");
  };

  const OrbitalRenderer invalid_viewport{{.width = 0, .height = 120}};
  check_untouched(invalid_viewport.render(planet, camera, kOrbitalTestLight,
                                          frame),
                  OrbitalRenderError::invalid_viewport,
                  "zero orbital width must be rejected");

  const OrbitalRenderer invalid_fov{{.width = 160,
                                     .height = 120,
                                     .field_of_view_degrees = 180.0}};
  check_untouched(invalid_fov.render(planet, camera, kOrbitalTestLight, frame),
                  OrbitalRenderError::invalid_field_of_view,
                  "an invalid orbital field of view must be rejected");

  const OrbitalRenderer invalid_stride{{.width = 160,
                                        .height = 120,
                                        .horizontal_sample_stride = 0}};
  check_untouched(
      invalid_stride.render(planet, camera, kOrbitalTestLight, frame),
                  OrbitalRenderError::invalid_sample_stride,
                  "an invalid orbital sample stride must be rejected");

  const OrbitalRenderer invalid_light{{.width = 160, .height = 120}};
  check_untouched(invalid_light.render(planet, camera, {}, frame),
                  OrbitalRenderError::invalid_light_direction,
                  "a zero orbital light direction must be rejected");

  auto tile_cache = TerrainTileCache::create();
  std::vector<std::uint8_t> short_coverage(frame.size() - 1U, 1U);
  check(tile_cache.has_value(),
        "the orbital coverage failure fixture must create its tile cache");
  if (tile_cache) {
    check_untouched(renderer.render_tile_backed(
                        planet, camera, kOrbitalTestLight, 2, *tile_cache,
                        frame, short_coverage),
                    OrbitalRenderError::invalid_framebuffer,
                    "a short orbital coverage buffer must be rejected");
  }

  const auto invalid_radius = planet_with_radius(planet, 0);
  check_untouched(renderer.render(invalid_radius, camera, kOrbitalTestLight,
                                  frame),
                  OrbitalRenderError::invalid_planet,
                  "an invalid orbital planet radius must be rejected");
  const auto invalid_water = planet_with_water(planet, 10'001);
  check_untouched(renderer.render(invalid_water, camera, kOrbitalTestLight,
                                  frame),
                  OrbitalRenderError::invalid_planet,
                  "invalid orbital water coverage must be rejected");
  const auto invalid_atmosphere =
      planet_with_atmosphere(planet, AtmosphereClass::airless, 1);
  check_untouched(renderer.render(invalid_atmosphere, camera,
                                  kOrbitalTestLight, frame),
                  OrbitalRenderError::invalid_planet,
                  "inconsistent orbital atmosphere data must be rejected");

  auto invalid_camera = camera;
  invalid_camera.position.x = std::numeric_limits<double>::quiet_NaN();
  check_untouched(renderer.render(planet, invalid_camera, kOrbitalTestLight,
                                  frame),
                  OrbitalRenderError::non_finite_camera,
                  "a non-finite orbital camera must be rejected");

  invalid_camera = camera;
  invalid_camera.position = {
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::max()};
  check_untouched(renderer.render(planet, invalid_camera, kOrbitalTestLight,
                                  frame),
                  OrbitalRenderError::non_finite_camera,
                  "an overflowing orbital camera must be rejected");

  invalid_camera = camera;
  invalid_camera.position = {};
  check_untouched(renderer.render(planet, invalid_camera, kOrbitalTestLight,
                                  frame),
                  OrbitalRenderError::camera_inside_planet,
                  "a camera inside the planet must be rejected");

  invalid_camera = camera;
  invalid_camera.forward = {};
  check_untouched(renderer.render(planet, invalid_camera, kOrbitalTestLight,
                                  frame),
                  OrbitalRenderError::invalid_camera_basis,
                  "a zero orbital forward direction must be rejected");

  invalid_camera = camera;
  invalid_camera.up = invalid_camera.forward;
  check_untouched(renderer.render(planet, invalid_camera, kOrbitalTestLight,
                                  frame),
                  OrbitalRenderError::invalid_camera_basis,
                  "a collinear orbital camera basis must be rejected");
}

auto orbital_visibility_contract() -> void {
  const auto generated = generate_planet_descriptor(Seed{42});
  const auto planet = planet_with_atmosphere(
      generated, AtmosphereClass::temperate, 1'000);
  const OrbitalRenderSettings settings{.width = 200,
                                       .height = 150,
                                       .field_of_view_degrees = 60.0};
  const OrbitalRenderer renderer{settings};
  std::vector<Pixel> frame(200U * 150U);

  auto camera = orbital_camera_for(planet);
  const auto visible =
      renderer.render(planet, camera, kOrbitalTestLight, frame);
  check(visible && visible->surface_pixels > 0 &&
            visible->atmosphere_pixels > 0,
        "a centered atmospheric planet must render its disc and halo");
  if (visible) {
    check(visible->surface_pixels < frame.size(),
          "a fully visible planet must leave space around its disc");
  }

  camera.forward.x += 1.65 *
                      static_cast<double>(planet.radius.value) * 1'000.0;
  const auto clipped =
      renderer.render(planet, camera, kOrbitalTestLight, frame);
  check(clipped && clipped->surface_pixels > 0 && visible &&
            clipped->surface_pixels < visible->surface_pixels,
        "an edge-clipped planet must retain only part of its visible disc");

  camera = orbital_camera_for(planet);
  camera.forward = {0.0, -1.0, 0.0};
  const auto outside =
      renderer.render(planet, camera, kOrbitalTestLight, frame);
  check(outside && outside->surface_pixels == 0 &&
            outside->atmosphere_pixels == 0,
        "a planet behind the orbital camera must be outside the view");

  const auto airless =
      planet_with_atmosphere(planet, AtmosphereClass::airless, 0);
  camera = orbital_camera_for(airless);
  const auto without_atmosphere =
      renderer.render(airless, camera, kOrbitalTestLight, frame);
  check(without_atmosphere && without_atmosphere->surface_pixels > 0 &&
            without_atmosphere->atmosphere_pixels == 0,
        "an airless planet must render without a halo");
}

auto deterministic_orbital_render() -> void {
  const auto planet = generate_planet_descriptor(Seed{42});
  const OrbitalRenderSettings settings{.width = 160,
                                       .height = 120,
                                       .field_of_view_degrees = 60.0};
  const OrbitalRenderer renderer{settings};
  const auto camera = orbital_camera_for(planet);
  std::vector<Pixel> first(160U * 120U);
  std::vector<Pixel> second(first.size());
  const auto first_result =
      renderer.render(planet, camera, kOrbitalTestLight, first);
  const auto second_result =
      renderer.render(planet, camera, kOrbitalTestLight, second);
  check(first_result && second_result && first_result == second_result,
        "repeated orbital renders must report identical coverage");
  check(first == second,
        "a fixed planet and orbital camera must render deterministically");
  check(std::ranges::all_of(first,
                            [](Pixel value) { return value.a == 255; }),
        "every orbital pixel must be opaque");

  const auto other_planet = generate_planet_descriptor(Seed{43});
  const auto other_camera = orbital_camera_for(other_planet);
  check(renderer.render(other_planet, other_camera, kOrbitalTestLight, second) &&
            pixel_checksum(first) != pixel_checksum(second),
        "a different planet descriptor must change the orbital frame");

  auto moved = camera;
  moved.position.x += static_cast<double>(planet.radius.value) * 300.0;
  moved.forward = {-moved.position.x, -moved.position.y, -moved.position.z};
  check(renderer.render(planet, moved, kOrbitalTestLight, second) &&
            pixel_checksum(first) != pixel_checksum(second),
        "moving the orbital camera must change the rendered frame");

  constexpr int strided_width{161};
  constexpr int strided_height{121};
  const OrbitalRenderer strided({.width = strided_width,
                                 .height = strided_height,
                                 .field_of_view_degrees = 60.0,
                                 .horizontal_sample_stride = 2});
  std::vector<Pixel> strided_frame(
      static_cast<std::size_t>(strided_width * strided_height));
  const auto strided_result =
      strided.render(planet, camera, kOrbitalTestLight, strided_frame);
  check(strided_result &&
            std::ranges::all_of(strided_frame,
                                [](Pixel value) { return value.a == 255; }),
        "strided orbital sampling must fill odd-width framebuffers without crossing their boundary");
  bool pairs_match = true;
  for (int y = 0; y < strided_height && pairs_match; ++y) {
    for (int x = 0; x + 1 < strided_width; x += 2) {
      const auto index = static_cast<std::size_t>(y * strided_width + x);
      if (strided_frame[index] != strided_frame[index + 1]) {
        pairs_match = false;
        break;
      }
    }
  }
  check(pairs_match,
        "each complete horizontal sample span must receive one "
        "centered orbital color");

  auto cache = TerrainTileCache::create();
  std::vector<std::uint8_t> covered(first.size(), 1U);
  std::fill(first.begin(), first.end(), Pixel{9, 8, 7, 6});
  const auto skipped = cache
                           ? renderer.render_tile_backed(
                                 planet, camera, kOrbitalTestLight, 2, *cache,
                                 first, covered)
                           : std::expected<OrbitalRenderStats,
                                           OrbitalRenderError>{
                                 std::unexpected{
                                     OrbitalRenderError::terrain_failure}};
  check(skipped && skipped->surface_pixels == 0 &&
            skipped->terrain_tiles_touched == 0 &&
            std::ranges::all_of(first, [](Pixel value) {
              return value == Pixel{9, 8, 7, 6};
            }),
        "a fully covered orbital fallback must skip tile sampling and leave its unused pixels untouched");
}

auto golden_orbital_profiles() -> void {
  const auto planet = generate_planet_descriptor(Seed{42});
  const auto camera = orbital_camera_for(planet);
  struct GoldenProfile {
    RenderProfile profile;
    std::uint64_t checksum;
  };
  constexpr std::array profiles{
      GoldenProfile{RenderProfile::remote, 11146610085014640820ULL},
      GoldenProfile{RenderProfile::balanced, 3760608313539738156ULL},
      GoldenProfile{RenderProfile::local, 1659061756243897864ULL},
      GoldenProfile{RenderProfile::cinematic, 675305623413012357ULL},
  };

  for (const auto golden : profiles) {
    const auto viewport = profile_viewport(golden.profile);
    const OrbitalRenderer renderer{{.width = viewport.width,
                                    .height = viewport.height,
                                    .field_of_view_degrees = 60.0}};
    std::vector<Pixel> first(static_cast<std::size_t>(viewport.width) *
                             static_cast<std::size_t>(viewport.height));
    std::vector<Pixel> second(first.size());
    check(renderer.render(planet, camera, kOrbitalTestLight, first) &&
              renderer.render(planet, camera, kOrbitalTestLight, second),
          "every named profile must render the orbital fixture");
    const auto checksum = pixel_checksum(first);
    if (checksum != golden.checksum) {
      std::fprintf(stderr, "%.*s golden orbital checksum: %llu\n",
                   static_cast<int>(profile_name(golden.profile).size()),
                   profile_name(golden.profile).data(),
                   static_cast<unsigned long long>(checksum));
    }
    check(first == second,
          "each named orbital profile must render deterministically");
    check(checksum == golden.checksum,
          "golden orbital profile checksums must remain stable");
  }
}

[[nodiscard]] auto presentation_state(const PlanetDescriptor& planet,
                                      double altitude, double surface,
                                      FlightRegime regime)
    -> PlanetaryFlightState {
  return {
      .tick = 7,
      .planet = planet.id,
      .pose = {{0.0, 0.0, altitude}, 0.35},
      .velocity = {80.0, 20.0, -15.0},
      .clearance_metres = altitude - surface,
      .mode = FlightMode::manual,
      .controls = {},
      .regime = regime,
      .last_transition = std::nullopt,
      .thermal = {},
  };
}

auto terrain_surface_sampling_contract() -> void {
  const auto planet = generate_planet_descriptor(Seed{0xA5515U});
  auto cache = TerrainTileCache::create(8);
  check(cache.has_value(), "surface sample cache must initialize");
  if (!cache) return;
  const auto fixed = planet_fixed_from_geodetic(planet, {0.0, 0.0, 0.0});
  check(fixed.has_value(), "surface sample position must resolve");
  if (!fixed) return;

  const auto coarse = sample_planet_surface(planet, *fixed, 2, *cache);
  const auto fine = sample_planet_surface(planet, *fixed, 12, *cache);
  check(coarse && fine,
        "the same canonical surface point must sample at multiple LODs");
  if (coarse && fine) {
    check(coarse->elevation_metres == fine->elevation_metres &&
              coarse->color == fine->color,
          "aligned cross-LOD surface samples must retain terrain identity");
    const auto reconstructed =
        planet_fixed_from_terrain_address(planet, fine->address);
    check(reconstructed && close_position(*fixed, *reconstructed),
          "a sampled surface anchor must reconstruct the same position");
  }
  check(!sample_planet_surface(
             planet,
             {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}, 2,
             *cache),
        "non-finite surface sampling must be rejected");
  check(!sample_planet_surface(planet, *fixed, kMaxTerrainLod + 1, *cache),
        "surface sampling above the maximum LOD must be rejected");
}

auto planetary_presentation_contract() -> void {
  const auto generated = generate_planet_descriptor(Seed{0xA5515U});
  const auto planet = planet_with_atmosphere(
      generated, AtmosphereClass::temperate, 1'000);
  auto cache = TerrainTileCache::create();
  check(cache.has_value(), "presentation surface cache must initialize");
  if (!cache) return;
  const auto fixed = planet_fixed_from_geodetic(planet, {0.0, 0.0, 0.0});
  const auto surface =
      fixed ? sample_planet_surface(planet, *fixed, 12, *cache)
            : std::expected<TerrainSurfaceSample, TerrainTileError>{
                  std::unexpected{TerrainTileError::coordinate_failure}};
  const auto bands = flight_regime_bands(planet);
  check(surface && bands,
        "presentation fixture surface and bands must initialize");
  if (!surface || !bands) return;
  const double ground = std::max(0.0, surface->elevation_metres);

  auto orbital = presentation_state(
      planet, bands->orbit_enter_altitude_metres + 1.0, ground,
      FlightRegime::orbital);
  auto atmosphere_start = presentation_state(
      planet, bands->orbit_enter_altitude_metres, ground,
      FlightRegime::orbital);
  auto atmosphere_full = presentation_state(
      planet, bands->atmosphere_enter_altitude_metres, ground,
      FlightRegime::atmospheric);
  auto terrain_start = presentation_state(
      planet, ground + bands->terrain_exit_clearance_metres, ground,
      FlightRegime::atmospheric);
  auto terrain_full = presentation_state(
      planet, ground + bands->terrain_enter_clearance_metres, ground,
      FlightRegime::terrain_flight);
  const auto orbital_mix = planetary_presentation_mix(planet, orbital);
  const auto atmosphere_start_mix =
      planetary_presentation_mix(planet, atmosphere_start);
  const auto atmosphere_full_mix =
      planetary_presentation_mix(planet, atmosphere_full);
  const auto terrain_start_mix =
      planetary_presentation_mix(planet, terrain_start);
  const auto terrain_full_mix =
      planetary_presentation_mix(planet, terrain_full);
  check(orbital_mix && orbital_mix->atmosphere == 0.0 &&
            orbital_mix->local_terrain == 0.0 && atmosphere_start_mix &&
            atmosphere_start_mix->atmosphere == 0.0 &&
            atmosphere_full_mix && atmosphere_full_mix->atmosphere == 1.0,
        "atmosphere blending must use exact orbital hysteresis endpoints");
  check(terrain_start_mix && terrain_start_mix->local_terrain == 0.0 &&
            terrain_full_mix && terrain_full_mix->local_terrain == 1.0,
        "terrain blending must use exact clearance hysteresis endpoints");

  const auto airless =
      planet_with_atmosphere(planet, AtmosphereClass::airless, 0);
  auto airless_state = atmosphere_full;
  airless_state.planet = airless.id;
  const auto airless_mix = planetary_presentation_mix(airless, airless_state);
  check(airless_mix && airless_mix->atmosphere == 0.0,
        "airless approach must not invent atmospheric color");

  constexpr int width{96};
  constexpr int height{64};
  PlanetaryPresentationRenderer renderer({
      .width = width,
      .height = height,
      .field_of_view_degrees = 60.0,
      .local_near_distance_metres = 140.0,
      .local_max_distance_metres = 100'000.0,
      .local_fog_start_metres = 80.0,
  });
  std::vector<Pixel> frame(static_cast<std::size_t>(width * height));
  std::vector<Pixel> again(frame.size());
  struct Stage {
    PlanetaryFlightState state;
    PlanetaryPresentationMode mode;
  };
  auto blend_state = presentation_state(
      planet,
      ground + (bands->terrain_enter_clearance_metres +
                bands->terrain_exit_clearance_metres) * 0.5,
      ground, FlightRegime::atmospheric);
  const std::array stages{
      Stage{orbital, PlanetaryPresentationMode::orbital},
      Stage{presentation_state(
                planet,
                (bands->orbit_enter_altitude_metres +
                 bands->atmosphere_enter_altitude_metres) * 0.5,
                ground, FlightRegime::atmospheric),
            PlanetaryPresentationMode::atmospheric},
      Stage{blend_state, PlanetaryPresentationMode::terrain_blend},
      Stage{presentation_state(planet, ground + 100.0, ground,
                               FlightRegime::terrain_flight),
            PlanetaryPresentationMode::local_terrain},
  };
  std::array<std::uint64_t, stages.size()> checksums{};
  std::vector<Pixel> moving_orbital_frame;
  std::vector<Pixel> atmospheric_context_frame;
  for (std::size_t index = 0; index < stages.size(); ++index) {
    const double stage_pitch = index == 2 ? -1.25 : index == 3 ? 0.0 : -0.08;
    const auto first = renderer.render(planet, stages[index].state,
                                       {.pitch_radians = stage_pitch}, frame);
    const auto second = renderer.render(planet, stages[index].state,
                                        {.pitch_radians = stage_pitch}, again);
    check(first && second && first->mode == stages[index].mode &&
              second->mode == stages[index].mode && frame == again,
          "every planetary presentation stage must render deterministically");
    if (!first || !second) continue;
    checksums[index] = pixel_checksum(frame);
    if (index == 0) moving_orbital_frame = frame;
    if (index == 1) atmospheric_context_frame = frame;
    check(first->surface_anchor.tile.planet == planet.id &&
              first->total_ms >= first->orbital_render_ms &&
              first->total_ms >= first->local_render_ms &&
              first->total_ms >= first->composite_ms,
          "presentation stats must retain anchor identity and bounded timings");
  }
  constexpr std::array<std::uint64_t, 4> expected_checksums{
      3389802127318038332ULL,
      8750882505373245699ULL,
      17140362428803543361ULL,
      9313099484138567917ULL,
  };
  if (checksums != expected_checksums) {
    std::fprintf(stderr,
                 "planetary presentation golden checksums: %llu %llu %llu %llu\n",
                 static_cast<unsigned long long>(checksums[0]),
                 static_cast<unsigned long long>(checksums[1]),
                 static_cast<unsigned long long>(checksums[2]),
                 static_cast<unsigned long long>(checksums[3]));
  }
  check(checksums == expected_checksums,
        "planetary presentation stages must retain golden frame checksums");
  check(std::ranges::none_of(checksums,
                             [](std::uint64_t value) { return value == 0; }) &&
            std::ranges::adjacent_find(checksums) == checksums.end(),
        "scripted descent stages must produce distinct nonzero frames");

  const std::array handoff_viewports{
      ViewportSize{320, 240}, ViewportSize{640, 480},
      ViewportSize{1024, 320}, ViewportSize{320, 1024}};
  for (const auto viewport : handoff_viewports) {
    PlanetaryPresentationRenderer handoff_renderer({
        .width = viewport.width,
        .height = viewport.height,
    });
    std::vector<Pixel> handoff_frame(
        static_cast<std::size_t>(viewport.width) *
        static_cast<std::size_t>(viewport.height));
    const auto state = presentation_state(
        planet, ground + bands->terrain_enter_clearance_metres, ground,
        FlightRegime::terrain_flight);
    const auto rendered = handoff_renderer.render(
        planet, state, {.pitch_radians = 0.0}, handoff_frame);
    check(rendered && rendered->local_terrain_pixels > 0 &&
              rendered->local_distance_metres > 900.0 &&
              (!rendered->orbital_surface_fallback ||
               rendered->orbital_tiles_touched > 0),
          "minimum, canonical, wide, and tall viewports must establish local terrain while retaining any required spherical coverage");
  }

  PlanetaryPresentationRenderer clearance_renderer;
  std::vector<Pixel> clearance_frame(
      static_cast<std::size_t>(kDefaultViewportWidth) *
      static_cast<std::size_t>(kDefaultViewportHeight));
  struct CoverageCheckpoint {
    double clearance;
    double pitch;
  };
  constexpr std::array coverage_checkpoints{
      CoverageCheckpoint{2'500.0, 0.0},
      CoverageCheckpoint{2'250.0, 0.0},
      CoverageCheckpoint{2'000.0, 0.0},
      CoverageCheckpoint{1'000.0, -0.18},
      CoverageCheckpoint{100.0, -0.35},
  };
  for (const auto checkpoint : coverage_checkpoints) {
    const auto regime = checkpoint.clearance <= 2'000.0
                            ? FlightRegime::terrain_flight
                            : FlightRegime::atmospheric;
    const auto state = presentation_state(
        planet, ground + checkpoint.clearance, ground, regime);
    const auto rendered = clearance_renderer.render(
        planet, state, {.pitch_radians = checkpoint.pitch}, clearance_frame);
    const bool terrain_expected = checkpoint.clearance < 2'500.0;
    check(rendered &&
              (terrain_expected ? rendered->local_terrain_pixels > 0
                                : rendered->local_terrain_pixels == 0) &&
              (!rendered->orbital_surface_fallback ||
               rendered->orbital_tiles_touched > 0) &&
              (terrain_expected || rendered->orbital_tiles_touched > 0),
          "canonical descent clearances must retain continuous spherical or local surface coverage");
  }

  PlanetaryPresentationRenderer bounded_renderer({
      .width = width,
      .height = height,
      .local_near_distance_metres = 140.0,
      .local_max_distance_metres = 140.0,
      .local_fog_start_metres = 80.0,
  });
  std::vector<Pixel> bounded_frame(frame.size());
  const auto bounded = bounded_renderer.render(
      planet, terrain_full, {.pitch_radians = 0.0}, bounded_frame);
  check(bounded && bounded->local_terrain_pixels == 0 &&
            bounded->orbital_surface_fallback &&
            bounded->orbital_tiles_touched > 0,
        "a local pass with zero terrain coverage must retain the spherical planet pass");

  PlanetaryPresentationRenderer airless_renderer({
      .width = width,
      .height = height,
      .field_of_view_degrees = 60.0,
      .local_near_distance_metres = 140.0,
      .local_max_distance_metres = 100'000.0,
      .local_fog_start_metres = 80.0,
  });
  auto airless_atmospheric = stages[1].state;
  airless_atmospheric.planet = airless.id;
  std::vector<Pixel> airless_atmospheric_frame(frame.size());
  const auto airless_render = airless_renderer.render(
      airless, airless_atmospheric, {.pitch_radians = -0.08},
      airless_atmospheric_frame);
  const auto average_row = [](std::span<const Pixel> pixels, int row) {
    std::array<double, 3> average{};
    for (int x = 0; x < width; ++x) {
      const auto value = pixels[static_cast<std::size_t>(row * width + x)];
      average[0] += value.r;
      average[1] += value.g;
      average[2] += value.b;
    }
    for (auto& value : average) value /= width;
    return average;
  };
  const auto zenith = average_row(atmospheric_context_frame, 0);
  const auto horizon = average_row(atmospheric_context_frame, height / 2);
  const double gradient = std::abs(zenith[0] - horizon[0]) +
                          std::abs(zenith[1] - horizon[1]) +
                          std::abs(zenith[2] - horizon[2]);
  check(airless_render &&
            airless_render->mode == PlanetaryPresentationMode::orbital &&
            atmospheric_context_frame != airless_atmospheric_frame &&
            gradient > 8.0,
        "atmospheric flight must add a visible horizon context while airless approaches remain orbital");

  const auto initial_sun = resolve_local_sun(planet, 0);
  check(initial_sun.has_value(), "the local day/night fixture must resolve");
  if (initial_sun) {
    const SimulationTick noon_tick =
        (kLocalDayTicks - initial_sun->cycle_tick) % kLocalDayTicks;
    auto day_state = stages.back().state;
    day_state.tick = noon_tick;
    auto night_state = day_state;
    night_state.tick = noon_tick + kLocalDayTicks / 2;
    std::vector<Pixel> day_frame(frame.size());
    std::vector<Pixel> night_frame(frame.size());
    const auto day_render = renderer.render(
        planet, day_state, {.pitch_radians = 0.0}, day_frame);
    const auto night_render = renderer.render(
        planet, night_state, {.pitch_radians = 0.0}, night_frame);
    const auto luminance = [](std::span<const Pixel> pixels) {
      std::uint64_t total{};
      for (const auto value : pixels) {
        total += static_cast<std::uint64_t>(value.r) + value.g + value.b;
      }
      return total;
    };
    const auto night_stars = std::ranges::count_if(
        std::span{night_frame},
        [](Pixel value) {
          return value.r > 140 && value.r == value.g && value.b >= value.r;
        });
    auto airless_night_state = night_state;
    airless_night_state.planet = airless.id;
    std::vector<Pixel> airless_night_frame(frame.size());
    const auto airless_night = airless_renderer.render(
        airless, airless_night_state, {.pitch_radians = 0.0},
        airless_night_frame);
    const bool coherent_cycle =
        day_render && night_render && airless_night &&
        day_render->local_solar_elevation > 0.85 &&
        night_render->local_solar_elevation < -0.85 &&
        luminance(day_frame) > luminance(night_frame) && night_stars > 0 &&
        airless_night_frame.front() == Pixel{4, 7, 13, 255};
    if (!coherent_cycle) {
      std::fprintf(
          stderr,
          "day/night diagnostics: day=%.6f night=%.6f day_luma=%llu "
          "night_luma=%llu stars=%zu airless_first=(%u,%u,%u)\n",
          day_render ? day_render->local_solar_elevation : 0.0,
          night_render ? night_render->local_solar_elevation : 0.0,
          static_cast<unsigned long long>(luminance(day_frame)),
          static_cast<unsigned long long>(luminance(night_frame)),
          static_cast<std::size_t>(night_stars), airless_night_frame.front().r,
          airless_night_frame.front().g, airless_night_frame.front().b);
    }
    check(coherent_cycle,
          "local terrain, atmospheric sky, night stars, and airless haze must agree on the shared sun direction");
  }

  auto stationary_orbit = orbital;
  stationary_orbit.velocity = {};
  stationary_orbit.controls = {};
  std::vector<Pixel> stationary_frame(frame.size());
  check(renderer.render(planet, stationary_orbit,
                        {.pitch_radians = -0.08}, stationary_frame) &&
            stationary_frame != moving_orbital_frame,
        "orbital velocity must produce a deterministic visual motion cue");
  auto thrust_orbit = stationary_orbit;
  thrust_orbit.controls.forward = true;
  std::vector<Pixel> thrust_frame(frame.size());
  check(renderer.render(planet, thrust_orbit,
                        {.pitch_radians = -0.08}, thrust_frame) &&
            thrust_frame != stationary_frame,
        "the first orbital thrust input must produce an immediate visual response");
  auto later_orbit = orbital;
  later_orbit.tick += 8;
  std::vector<Pixel> later_frame(frame.size());
  check(renderer.render(planet, later_orbit,
                        {.pitch_radians = -0.08}, later_frame) &&
            later_frame != moving_orbital_frame,
        "orbital streak motion must advance only from authoritative tick state");

  std::vector<Pixel> short_frame(frame.size() - 1, {1, 2, 3, 4});
  check(!renderer.render(planet, orbital, {}, short_frame) &&
            std::ranges::all_of(short_frame, [](Pixel value) {
              return value == Pixel{1, 2, 3, 4};
            }),
        "a short planetary framebuffer must be rejected unchanged");

  auto subpixel_blend = terrain_start;
  subpixel_blend.pose.position.altitude_metres -= 0.5;
  subpixel_blend.clearance_metres -= 0.5;
  const auto subpixel_result = renderer.render(
      planet, subpixel_blend, {.pitch_radians = -1.25}, frame);
  check(subpixel_result &&
            subpixel_result->mode ==
                PlanetaryPresentationMode::terrain_blend &&
            subpixel_result->orbital_tiles_touched > 0 &&
            subpixel_result->local_tiles_touched == 0 &&
            subpixel_result->local_render_ms == 0.0,
        "a subpixel terrain contribution must not render an ineffectual local pass");
  auto invalid_state = orbital;
  invalid_state.pose.position.altitude_metres =
      std::numeric_limits<double>::quiet_NaN();
  std::fill(frame.begin(), frame.end(), Pixel{5, 6, 7, 8});
  check(!renderer.render(planet, invalid_state, {}, frame) &&
            std::ranges::all_of(frame, [](Pixel value) {
              return value == Pixel{5, 6, 7, 8};
            }),
        "non-finite planetary state must be rejected unchanged");
  check(!renderer.render(
            planet, orbital,
            {.pitch_radians = std::numeric_limits<double>::infinity()}, frame),
        "non-finite presentation camera must be rejected");
  PlanetaryPresentationRenderer invalid_renderer({
      .width = 0,
      .height = height,
      .terrain_cache_capacity = 0,
  });
  check(!invalid_renderer.render(planet, orbital, {}, {}),
        "invalid presentation settings must be rejected without allocation");
  PlanetaryPresentationRenderer invalid_range_renderer({
      .width = width,
      .height = height,
      .local_near_distance_metres = 200.0,
      .local_max_distance_metres = 100.0,
      .local_fog_start_metres = 80.0,
  });
  check(!invalid_range_renderer.render(planet, orbital, {}, frame),
        "an inverted adaptive terrain range must be rejected");

  const auto instruments = format_flight_instruments(stages.back().state);
  check(instruments.heading.size() == kInstrumentLineWidth &&
            instruments.altitude.size() == kInstrumentLineWidth &&
            instruments.clearance.size() == kInstrumentLineWidth &&
            instruments.speed.size() == kInstrumentLineWidth,
        "planetary telemetry must preserve fixed-width cockpit lines");
}

auto planetfall_acceptance_contract() -> void {
  const auto invalid = run_planetfall_acceptance({{0, 64}, RenderProfile::local});
  check(!invalid &&
            invalid.error() ==
                PlanetfallAcceptanceError::invalid_configuration,
        "Planetfall acceptance must reject invalid viewport dimensions");

  const auto result = run_planetfall_acceptance(
      {{96, 64}, std::nullopt});
  check(result.has_value(),
        "the canonical Planetfall descent must complete");
  if (!result) return;

  check(result->report.planet_id ==
            PlanetId{kPlanetfallAcceptanceSeed} &&
            result->report.planet_name == "Carayx" &&
            result->report.command_count == 4 &&
            result->report.final_state.tick ==
                kPlanetfallAcceptanceTicks &&
            planetary_flight_state_checksum(result->report.final_state) ==
                15251675909814434464ULL,
        "the canonical Planetfall path must retain its generated identity and final state");
  check(result->report.final_state.regime == FlightRegime::terrain_flight &&
            result->report.final_state.clearance_metres > 100.0 &&
            result->report.final_state.clearance_metres < 300.0 &&
            std::hypot(
                result->report.final_state.velocity.east_metres_per_second,
                result->report.final_state.velocity.north_metres_per_second) >
                100.0,
        "Planetfall must finish in a stable low-level forward flyover");

  constexpr std::array expected_modes{
      PlanetaryPresentationMode::orbital,
      PlanetaryPresentationMode::atmospheric,
      PlanetaryPresentationMode::terrain_blend,
      PlanetaryPresentationMode::local_terrain,
  };
  constexpr std::array<SimulationTick, 4> expected_ticks{
      0, 4'080, 15'555, kPlanetfallAcceptanceTicks};
  constexpr std::array<std::uint64_t, 4> expected_flight_checksums{
      10201608541589742394ULL,
      10298811930090958906ULL,
      13755635873101133721ULL,
      15251675909814434464ULL,
  };
  constexpr std::array<std::uint64_t, 4> expected_frame_checksums{
      10946652128119593424ULL,
      16874951085288255351ULL,
      3576507194995956476ULL,
      6089475364341542143ULL,
  };
  check(result->report.stages.size() == expected_modes.size(),
        "Planetfall must report each presentation stage exactly once");
  if (result->report.stages.size() == expected_modes.size()) {
    std::array<std::uint64_t, 4> actual_frames{};
    for (std::size_t index = 0; index < expected_modes.size(); ++index) {
      actual_frames[index] = result->report.stages[index].framebuffer_checksum;
    }
    if (actual_frames != expected_frame_checksums) {
      std::fprintf(stderr,
                   "Planetfall acceptance frame checksums: %llu %llu %llu %llu\n",
                   static_cast<unsigned long long>(actual_frames[0]),
                   static_cast<unsigned long long>(actual_frames[1]),
                   static_cast<unsigned long long>(actual_frames[2]),
                   static_cast<unsigned long long>(actual_frames[3]));
    }
    for (std::size_t index = 0; index < expected_modes.size(); ++index) {
      const auto& stage = result->report.stages[index];
      check(stage.presentation_mode == expected_modes[index] &&
                stage.tick == expected_ticks[index] &&
                stage.flight_checksum == expected_flight_checksums[index] &&
                stage.framebuffer_checksum ==
                    expected_frame_checksums[index] &&
                stage.surface_anchor.tile.planet == result->report.planet_id &&
                std::isfinite(stage.total_avg_ms) &&
                std::isfinite(stage.total_p95_ms) &&
                stage.total_avg_ms >= 0.0 && stage.total_p95_ms >= 0.0,
            "Planetfall stages must retain ordered deterministic identities and finite diagnostics");
    }
  }
  check(result->final_frame.size() == 96U * 64U &&
            pixel_checksum(result->final_frame) ==
                expected_frame_checksums.back(),
        "Planetfall must retain the final local-terrain frame for capture");

  const auto json = planetfall_acceptance_json(result->report);
  check(
      json.find("\"schema_version\": 1") != std::string::npos &&
          json.find("\"scenario\": \"v0.3-planetfall\"") != std::string::npos &&
          json.find("\"final_flight_checksum\": "
                    "\"15251675909814434464\"") != std::string::npos &&
          json.find("\"presentation_mode\": \"terrain-blend\"") !=
              std::string::npos,
      "Planetfall JSON must preserve its versioned scenario and "
      "deterministic fields");
}

}  // namespace

auto main() -> int {
  generation_failure_matrix();
  deterministic_generation();
  seed_derivation_contract();
  intersystem_identity_contract();
  universe_navigation_contract();
  universe_navigation_acceptance_contract();
  intersystem_state_contract();
  intersystem_time_boundary_contract();
  intersystem_jump_contract();
  intersystem_jump_acceptance_contract();
  system_flight_contract();
  intersystem_return_contract();
  intersystem_contract_acceptance_contract();
  origin_system_contract_contract();
  origin_system_contract_acceptance_contract();
  intersystem_planetfall_contract();
  intersystem_planetfall_acceptance_contract();
  mission_board_contract();
  local_system_contract();
  local_system_rendering_contract();
  origin_station_contract();
  origin_onboarding_contract();
  career_onboarding_contract();
  save_schema_contract();
  save_file_contract();
  profile_catalog_contract();
  signal_run_contract();
  world_delta_journal_contract();
  regenerated_world_delta_contract();
  surface_signal_contract();
  surface_signal_population();
  signal_scanner_contract();
  signal_collection_contract();
  signal_navigation_acceptance_contract();
  planet_descriptor_contract();
  planet_descriptor_population();
  terrain_tile_failure_matrix();
  deterministic_terrain_tiles();
  terrain_tile_seam_contract();
  terrain_tile_cache_contract();
  coordinate_and_lod_contract();
  render_profile_contract();
  viewport_validation_contract();
  cockpit_layout_contract();
  menu_session_contract();
  title_render_contract();
  flight_instrument_contract();
  planetary_flight_regime_contract();
  orbital_motion_feedback_contract();
  deterministic_planetary_flight_replay();
  planetary_flight_failure_matrix();
  thermal_reentry_contract();
  sweep_selection_contract();
  sweep_report_contract();
  audio_contract();
  fixed_step_clock_contract();
  deterministic_fixed_step_flight();
  deterministic_command_replay();
  command_edge_contract();
  flight_input_mapping_contract();
  mouse_flight_mapping_contract();
  mixed_input_ownership_contract();
  suspended_input_contract();
  mouse_event_coalescing_contract();
  equivalent_mouse_keyboard_trace_contract();
  capability_floor_contract();
  deterministic_key_trace_contract();
  deterministic_mixed_input_trace_contract();
  camera_derivation_contract();
  camera_projection_contract();
  render_failure_matrix();
  world_sun_render_contract();
  deterministic_render();
  golden_profile_renders();
  required_viewport_matrix();
  celestial_geometry_contract();
  orbital_render_failure_matrix();
  orbital_sun_occlusion_contract();
  orbital_visibility_contract();
  deterministic_orbital_render();
  golden_orbital_profiles();
  terrain_surface_sampling_contract();
  planetary_presentation_contract();
  planetfall_acceptance_contract();
  if (failures != 0) {
    std::fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
  }
  std::puts("all Apsis Drift tests passed");
  return 0;
}
