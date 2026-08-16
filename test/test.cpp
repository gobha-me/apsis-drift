#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cmath>
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

#include "apsis_drift/benchmark.hpp"
#include "apsis_drift/cockpit.hpp"
#include "apsis_drift/coordinates.hpp"
#include "apsis_drift/flight_deck_acceptance.hpp"
#include "apsis_drift/landscape.hpp"
#include "apsis_drift/menu.hpp"
#include "apsis_drift/orbital.hpp"
#include "apsis_drift/origin_station.hpp"
#include "apsis_drift/planet.hpp"
#include "apsis_drift/planetfall_acceptance.hpp"
#include "apsis_drift/planetary_flight.hpp"
#include "apsis_drift/planetary_presentation.hpp"
#include "apsis_drift/save_schema.hpp"
#include "apsis_drift/seed.hpp"
#include "apsis_drift/signal_collection.hpp"
#include "apsis_drift/signal_navigation_acceptance.hpp"
#include "apsis_drift/signal_scanner.hpp"
#include "apsis_drift/simulation.hpp"
#include "apsis_drift/surface_signals.hpp"
#include "apsis_drift/terrain_tiles.hpp"
#include "apsis_drift/title.hpp"
#include "apsis_drift/world_delta_journal.hpp"
#include "capability_floor.hpp"
#include "flight_input.hpp"
#include "signal_input.hpp"
#include "surface_signal_generation.hpp"

namespace {

using namespace apsis_drift;
using termforge::Pixel;
using termforge::Rect;

int failures{};

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

[[nodiscard]] auto read_test_data(std::string_view relative) -> std::string {
  const auto path = std::string{APSIS_DRIFT_SOURCE_DIR} + "/" +
                    std::string{relative};
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

auto origin_station_contract() -> void {
  check(kOriginStationGeneratorVersion == 1,
        "origin-station generator version 1 must remain stable");
  check(kOriginSystemOrdinal == 0 && kOriginStationOrdinal == 0,
        "the version 1 origin path must retain its named ordinals");

  constexpr std::array<std::uint64_t, 3> universe_seeds{
      0, 42, std::numeric_limits<std::uint64_t>::max()};
  constexpr std::array<std::uint64_t, universe_seeds.size()> system_goldens{
      2662095937669570104ULL, 677859337506523986ULL,
      4480404333408418992ULL};
  constexpr std::array<std::uint64_t, universe_seeds.size()> station_goldens{
      7159869865471737051ULL, 14866919373675561773ULL,
      15849284578567890724ULL};
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
  check(system_before ==
                derive_seed(universe, SeedDomain::system,
                            kOriginSystemOrdinal) &&
            planet_before == derive_seed(system_before, SeedDomain::planet, 0) &&
            terrain_before == derive_seed(planet_before, SeedDomain::terrain, 0) &&
            weather_before == derive_seed(planet_before, SeedDomain::weather, 0) &&
            encounter_before ==
                derive_seed(planet_before, SeedDomain::encounter, 0),
        "origin-station derivation must not perturb unrelated world streams");
}

auto origin_onboarding_contract() -> void {
  const auto station = generate_origin_station(Seed{42});
  auto state = initial_origin_onboarding_state(station);
  check(state == OriginOnboardingState{station.id,
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
  unchanged_on_failure(OriginOnboardingCommand::return_to_origin,
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
  unchanged_on_failure(OriginOnboardingCommand::return_to_origin,
                       OriginOnboardingError::invalid_transition);
  check(advance_origin_onboarding(
            state, OriginOnboardingCommand::complete_first_objective)
            .has_value() &&
            state.location == OriginLocation::in_flight &&
            state.first_objective == FirstObjectiveStatus::completed,
        "objective completion must remain in flight until return");
  check(advance_origin_onboarding(
            state, OriginOnboardingCommand::return_to_origin)
            .has_value() &&
            state.location == OriginLocation::docked_at_origin &&
            state.first_objective == FirstObjectiveStatus::completed,
        "return must finish docked at the stable origin station");
  unchanged_on_failure(OriginOnboardingCommand::return_to_origin,
                       OriginOnboardingError::invalid_transition);
  unchanged_on_failure(OriginOnboardingCommand::launch,
                       OriginOnboardingError::invalid_transition);

  OriginOnboardingState malformed{
      station.id, OriginLocation::in_flight, FirstObjectiveStatus::offered};
  const auto malformed_before = malformed;
  const auto malformed_result = advance_origin_onboarding(
      malformed, OriginOnboardingCommand::accept_first_objective);
  check(!malformed_result &&
            malformed_result.error() == OriginOnboardingError::invalid_state &&
            malformed == malformed_before,
        "impossible onboarding combinations must fail without mutation");

  malformed = OriginOnboardingState{
      station.id, static_cast<OriginLocation>(255),
      FirstObjectiveStatus::active};
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

auto save_schema_contract() -> void {
  check(kSaveFormatVersion == 1 && kSaveApplication == "apsis-drift" &&
            kMaximumSaveDocumentBytes == (1U << 20U),
        "save format version 1 identity and byte bound must remain stable");
  const auto fixture = read_test_data("test/data/save-v1-golden.json");
  check(!fixture.empty(), "the version 1 golden save fixture must be readable");

  auto recipe = make_save_recipe(Seed{42});
  const auto target = SurfaceSignalId{0x71d4c959dcd64423ULL};
  SaveDocument expected{
      .recipe = recipe,
      .state =
          SaveMutableState{
              .location = OriginLocation::in_flight,
              .first_objective = FirstObjectiveStatus::active,
              .first_objective_target = target,
              .flight =
                  PlanetaryFlightState{
                      .tick = 1200,
                      .planet = recipe.active_planet,
                      .pose = {{0.25, -0.5, 100'000.0}, 0.75},
                      .velocity = {125.5, -20.25, -5.0},
                      .clearance_metres = 99'000.0,
                      .mode = FlightMode::manual,
                      .controls = {.forward = true, .turn_right = true},
                      .regime = FlightRegime::atmospheric,
                      .last_transition =
                          FlightRegimeTransition{FlightRegime::orbital,
                                                 FlightRegime::atmospheric,
                                                 1000},
                  },
              .discoveries = {{target, 1100}},
              .world_deltas =
                  {{"signal-71d4c959dcd64423",
                    SaveWorldDeltaKind::discovered, 1100}},
          },
  };
  check(recipe.origin_station.value == 0xce51e866ec4e032dULL &&
            recipe.active_planet.value == 0x435b7b7e8ce489e8ULL,
        "save recipes must regenerate the canonical station and planet IDs");
  check(validate_save_document(expected).has_value(),
        "the representative version 1 save must validate");

  const auto decoded = decode_save_document_json(fixture);
  check(decoded && *decoded == expected,
        "the golden save must decode to the complete semantic state");
  const auto encoded = encode_save_document_json(expected);
  check(encoded && *encoded == fixture,
        "the version 1 encoder must reproduce the golden fixture byte-for-byte");
  if (encoded) {
    const auto round_trip = decode_save_document_json(*encoded);
    check(round_trip && *round_trip == expected,
          "save encode/decode must preserve semantic state");
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
      replace_once(fixture, "\"format_version\": 1",
                   "\"format_version\": 2"),
      SaveSchemaErrorCode::unsupported_format_version,
      "future save versions must be rejected explicitly");
  expect_decode_error(
      "{\"application\":\"apsis-drift\",\"format_version\":2}",
      SaveSchemaErrorCode::unsupported_format_version,
      "future formats must be identified before version 1 fields are read");
  expect_decode_error(
      replace_once(fixture, "\"format_version\": 1",
                   "\"format_version\": \"1\""),
      SaveSchemaErrorCode::invalid_type,
      "schema integers with the wrong JSON type must be rejected");
  expect_decode_error(
      replace_once(fixture, "\"application\": \"apsis-drift\"",
                   "\"application\": \"another-game\""),
      SaveSchemaErrorCode::invalid_value,
      "foreign application saves must be rejected");
  expect_decode_error(
      replace_once(fixture, "\"seed_derivation\": 1",
                   "\"seed_derivation\": 2"),
      SaveSchemaErrorCode::incompatible_generator_version,
      "unsupported generator versions must be rejected explicitly");
  expect_decode_error(
      replace_once(fixture, "station-ce51e866ec4e032d",
                   "station-ce51e866ec4e032e"),
      SaveSchemaErrorCode::identity_mismatch,
      "regenerated station identity mismatches must be rejected");
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

  constexpr std::array required_sections{
      "\"application\"", "\"format_version\"", "\"recipe\"",
      "\"state\"",       "\"generator_versions\"",
      "\"first_objective\"", "\"flight\"", "\"discoveries\"",
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
  const auto planet =
      generate_planet_descriptor(Seed{recipe.active_planet.value});
  auto first_cache = TerrainTileCache::create(1);
  check(first_cache.has_value(),
        "world-delta regeneration requires a bounded terrain cache");
  if (!first_cache) return;
  const auto original = generate_surface_signals(planet, *first_cache);
  check(original.has_value(),
        "world-delta regeneration requires a generated signal catalog");
  if (!original) return;

  const auto target = original->signals.front().id;
  SaveDocument saved{
      .recipe = recipe,
      .state =
          SaveMutableState{
              .location = OriginLocation::docked_at_origin,
              .first_objective = FirstObjectiveStatus::completed,
              .first_objective_target = target,
              .flight = std::nullopt,
              .discoveries = {{target, 100}},
              .world_deltas = {{surface_signal_object_key(target),
                                SaveWorldDeltaKind::collected, 120}},
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
                 readout.strength.size() == kInstrumentLineWidth &&
                 readout.cue.size() == kInstrumentLineWidth;
        }),
        "scanner formatting must preserve fixed-width cockpit lines");
  check(empty_readout.cue == "NO SIGNAL" &&
            tracking_readout.cue == "AHEAD >>>" &&
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

  SaveDocument saved{
      .recipe = make_save_recipe(Seed{42}),
      .state =
          SaveMutableState{
              .location = OriginLocation::docked_at_origin,
              .first_objective = FirstObjectiveStatus::completed,
              .first_objective_target = target.id,
              .flight = std::nullopt,
              .discoveries = {{target.id, *state.completion_tick}},
              .world_deltas = std::vector<SaveWorldDelta>(
                  journal.entries().begin(), journal.entries().end()),
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
      const auto restored = advance_signal_collection(
          *catalog, reached, 10'000, *restored_journal, restored_state);
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
  constexpr std::uint64_t expected_flight_checksum{4086686148596456340ULL};
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

  for (const auto [cols, rows] :
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

  for (const auto [cols, rows] :
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
  check(normal.speed == "SPD 005  ",
        "speed must use horizontal velocity magnitude");
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
              readout.alert.size() == kInstrumentLineWidth,
          message);
  };
  check_widths(normal, "every normal instrument line must have fixed width");

  state.mode = FlightMode::manual;
  state.pose.yaw = -1.57079632679489661923F;
  state.pose.altitude = -9999.0F;
  state.clearance = kLowClearanceWarning;
  state.velocity = {999.0F, 0.0F, 9999.0F};
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
            overflow.speed == "SPD ###  ",
        "finite values outside display bounds must use fixed sentinels");
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
  auto descending = *orbital;
  descending.pose.position.altitude_metres =
      bands->atmosphere_enter_altitude_metres - 1.0;
  descending.clearance_metres = descending.pose.position.altitude_metres -
                                environment.surface_elevation_metres;
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
      bands->orbit_enter_altitude_metres + 1.0;
  ascending.clearance_metres = ascending.pose.position.altitude_metres -
                               environment.surface_elevation_metres;
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
      bands->terrain_exit_clearance_metres + 1.0;
  terrain_gap.clearance_metres =
      terrain_gap.pose.position.altitude_metres -
      environment.surface_elevation_metres;
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
  constexpr std::uint64_t expected_checksum{11546696375629488931ULL};
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
  unbounded.velocity.east_metres_per_second = 2'001.0;
  check_rejected(unbounded, environment, {}, kSimulationStep,
                 PlanetaryFlightError::invalid_state,
                 "out-of-regime velocity must be rejected transactionally");
  check_rejected(*initialized,
                 {std::numeric_limits<double>::infinity()}, {},
                 kSimulationStep, PlanetaryFlightError::invalid_environment,
                 "non-finite terrain elevation must be rejected transactionally");
  check_rejected(*initialized, environment, {}, SimulationSeconds{0.0},
                 PlanetaryFlightError::invalid_step,
                 "a zero planetary step must be rejected transactionally");

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
            !parse_benchmark_workload("unknown"),
        "benchmark workloads must parse only their documented names");
  check(workload_identifier(BenchmarkWorkload::landscape) ==
                "voxel-landscape-rgba" &&
            workload_identifier(BenchmarkWorkload::orbital) ==
                "orbital-planet-rgba" &&
            workload_identifier(BenchmarkWorkload::planetary) ==
                "planetary-presentation-rgba",
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
      .total_bytes = 12288,
      .checksum = 123456789,
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
  check(json.find("\"schema_version\": 1") != std::string::npos,
        "sweep JSON must identify its schema version");
  check(json.find("\"workload\": \"voxel-landscape-rgba\"") !=
            std::string::npos,
        "the default sweep report must identify the landscape workload");
  check(json.find("\"seed\": 42") != std::string::npos,
        "sweep JSON must identify its seed");
  check(json.find("\"frames_per_viewport\": 12") != std::string::npos,
        "sweep JSON must identify its frame count");
  check(json.find("\"checksum\": \"123456789\"") != std::string::npos,
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

  auto bad_step = *initialized;
  const auto bad_step_result =
      advance_flight(*terrain, bad_step, {}, SimulationSeconds{0.0});
  check(!bad_step_result && bad_step_result.error() == FlightError::invalid_step,
        "a non-positive simulation step must be rejected");
  check(flight_state_checksum(bad_step) == unchanged,
        "an invalid step must not mutate state");

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

auto orbital_render_failure_matrix() -> void {
  const auto planet = generate_planet_descriptor(Seed{42});
  const OrbitalRenderSettings settings{.width = 160,
                                       .height = 120,
                                       .field_of_view_degrees = 60.0,
                                       .light_direction = {-0.4, -0.6, 0.7}};
  const OrbitalRenderer renderer{settings};
  const auto camera = orbital_camera_for(planet);

  std::vector<Pixel> short_frame(160U * 120U - 1U, {1, 2, 3, 4});
  const auto short_result = renderer.render(planet, camera, short_frame);
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
  check_untouched(invalid_viewport.render(planet, camera, frame),
                  OrbitalRenderError::invalid_viewport,
                  "zero orbital width must be rejected");

  const OrbitalRenderer invalid_fov{{.width = 160,
                                     .height = 120,
                                     .field_of_view_degrees = 180.0}};
  check_untouched(invalid_fov.render(planet, camera, frame),
                  OrbitalRenderError::invalid_field_of_view,
                  "an invalid orbital field of view must be rejected");

  const OrbitalRenderer invalid_stride{{.width = 160,
                                        .height = 120,
                                        .horizontal_sample_stride = 0}};
  check_untouched(invalid_stride.render(planet, camera, frame),
                  OrbitalRenderError::invalid_sample_stride,
                  "an invalid orbital sample stride must be rejected");

  const OrbitalRenderer invalid_light{{
      .width = 160, .height = 120, .light_direction = {0.0, 0.0, 0.0}}};
  check_untouched(invalid_light.render(planet, camera, frame),
                  OrbitalRenderError::invalid_light_direction,
                  "a zero orbital light direction must be rejected");

  const auto invalid_radius = planet_with_radius(planet, 0);
  check_untouched(renderer.render(invalid_radius, camera, frame),
                  OrbitalRenderError::invalid_planet,
                  "an invalid orbital planet radius must be rejected");
  const auto invalid_water = planet_with_water(planet, 10'001);
  check_untouched(renderer.render(invalid_water, camera, frame),
                  OrbitalRenderError::invalid_planet,
                  "invalid orbital water coverage must be rejected");
  const auto invalid_atmosphere =
      planet_with_atmosphere(planet, AtmosphereClass::airless, 1);
  check_untouched(renderer.render(invalid_atmosphere, camera, frame),
                  OrbitalRenderError::invalid_planet,
                  "inconsistent orbital atmosphere data must be rejected");

  auto invalid_camera = camera;
  invalid_camera.position.x = std::numeric_limits<double>::quiet_NaN();
  check_untouched(renderer.render(planet, invalid_camera, frame),
                  OrbitalRenderError::non_finite_camera,
                  "a non-finite orbital camera must be rejected");

  invalid_camera = camera;
  invalid_camera.position = {
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::max()};
  check_untouched(renderer.render(planet, invalid_camera, frame),
                  OrbitalRenderError::non_finite_camera,
                  "an overflowing orbital camera must be rejected");

  invalid_camera = camera;
  invalid_camera.position = {};
  check_untouched(renderer.render(planet, invalid_camera, frame),
                  OrbitalRenderError::camera_inside_planet,
                  "a camera inside the planet must be rejected");

  invalid_camera = camera;
  invalid_camera.forward = {};
  check_untouched(renderer.render(planet, invalid_camera, frame),
                  OrbitalRenderError::invalid_camera_basis,
                  "a zero orbital forward direction must be rejected");

  invalid_camera = camera;
  invalid_camera.up = invalid_camera.forward;
  check_untouched(renderer.render(planet, invalid_camera, frame),
                  OrbitalRenderError::invalid_camera_basis,
                  "a collinear orbital camera basis must be rejected");
}

auto orbital_visibility_contract() -> void {
  const auto generated = generate_planet_descriptor(Seed{42});
  const auto planet = planet_with_atmosphere(
      generated, AtmosphereClass::temperate, 1'000);
  const OrbitalRenderSettings settings{.width = 200,
                                       .height = 150,
                                       .field_of_view_degrees = 60.0,
                                       .light_direction = {-0.4, -0.6, 0.7}};
  const OrbitalRenderer renderer{settings};
  std::vector<Pixel> frame(200U * 150U);

  auto camera = orbital_camera_for(planet);
  const auto visible = renderer.render(planet, camera, frame);
  check(visible && visible->surface_pixels > 0 &&
            visible->atmosphere_pixels > 0,
        "a centered atmospheric planet must render its disc and halo");
  if (visible) {
    check(visible->surface_pixels < frame.size(),
          "a fully visible planet must leave space around its disc");
  }

  camera.forward.x += 1.65 *
                      static_cast<double>(planet.radius.value) * 1'000.0;
  const auto clipped = renderer.render(planet, camera, frame);
  check(clipped && clipped->surface_pixels > 0 && visible &&
            clipped->surface_pixels < visible->surface_pixels,
        "an edge-clipped planet must retain only part of its visible disc");

  camera = orbital_camera_for(planet);
  camera.forward = {0.0, -1.0, 0.0};
  const auto outside = renderer.render(planet, camera, frame);
  check(outside && outside->surface_pixels == 0 &&
            outside->atmosphere_pixels == 0,
        "a planet behind the orbital camera must be outside the view");

  const auto airless =
      planet_with_atmosphere(planet, AtmosphereClass::airless, 0);
  camera = orbital_camera_for(airless);
  const auto without_atmosphere = renderer.render(airless, camera, frame);
  check(without_atmosphere && without_atmosphere->surface_pixels > 0 &&
            without_atmosphere->atmosphere_pixels == 0,
        "an airless planet must render without a halo");
}

auto deterministic_orbital_render() -> void {
  const auto planet = generate_planet_descriptor(Seed{42});
  const OrbitalRenderSettings settings{.width = 160,
                                       .height = 120,
                                       .field_of_view_degrees = 60.0,
                                       .light_direction = {-0.4, -0.6, 0.7}};
  const OrbitalRenderer renderer{settings};
  const auto camera = orbital_camera_for(planet);
  std::vector<Pixel> first(160U * 120U);
  std::vector<Pixel> second(first.size());
  const auto first_result = renderer.render(planet, camera, first);
  const auto second_result = renderer.render(planet, camera, second);
  check(first_result && second_result && first_result == second_result,
        "repeated orbital renders must report identical coverage");
  check(first == second,
        "a fixed planet and orbital camera must render deterministically");
  check(std::ranges::all_of(first,
                            [](Pixel value) { return value.a == 255; }),
        "every orbital pixel must be opaque");

  const auto other_planet = generate_planet_descriptor(Seed{43});
  const auto other_camera = orbital_camera_for(other_planet);
  check(renderer.render(other_planet, other_camera, second) &&
            pixel_checksum(first) != pixel_checksum(second),
        "a different planet descriptor must change the orbital frame");

  auto moved = camera;
  moved.position.x += static_cast<double>(planet.radius.value) * 300.0;
  moved.forward = {-moved.position.x, -moved.position.y, -moved.position.z};
  check(renderer.render(planet, moved, second) &&
            pixel_checksum(first) != pixel_checksum(second),
        "moving the orbital camera must change the rendered frame");

  constexpr int strided_width{161};
  constexpr int strided_height{121};
  const OrbitalRenderer strided({.width = strided_width,
                                 .height = strided_height,
                                 .field_of_view_degrees = 60.0,
                                 .horizontal_sample_stride = 2,
                                 .light_direction = {-0.4, -0.6, 0.7}});
  std::vector<Pixel> strided_frame(
      static_cast<std::size_t>(strided_width * strided_height));
  const auto strided_result =
      strided.render(planet, camera, strided_frame);
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
        "each complete horizontal sample span must receive one centered orbital color");
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
                                    .field_of_view_degrees = 60.0,
                                    .light_direction = {-0.4, -0.6, 0.7}}};
    std::vector<Pixel> first(static_cast<std::size_t>(viewport.width) *
                             static_cast<std::size_t>(viewport.height));
    std::vector<Pixel> second(first.size());
    check(renderer.render(planet, camera, first) &&
              renderer.render(planet, camera, second),
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
      .local_max_distance_metres = 140.0,
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
    check(first->surface_anchor.tile.planet == planet.id &&
              first->total_ms >= first->orbital_render_ms &&
              first->total_ms >= first->local_render_ms &&
              first->total_ms >= first->composite_ms,
          "presentation stats must retain anchor identity and bounded timings");
  }
  constexpr std::array<std::uint64_t, 4> expected_checksums{
      4464057357403076723ULL,
      6162202560146668315ULL,
      3992641663955562031ULL,
      10329579900168594179ULL,
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
                15600629779145530762ULL,
        "the canonical Planetfall path must retain its generated identity and final state");
  check(result->report.final_state.regime == FlightRegime::terrain_flight &&
            result->report.final_state.clearance_metres > 200.0 &&
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
      0, 13'350, 113'071, kPlanetfallAcceptanceTicks};
  constexpr std::array<std::uint64_t, 4> expected_flight_checksums{
      16209989626150487226ULL,
      3828620310919835151ULL,
      6612887580505814172ULL,
      15600629779145530762ULL,
  };
  constexpr std::array<std::uint64_t, 4> expected_frame_checksums{
      17277935430955010903ULL,
      9195127342075088232ULL,
      10695031067588847771ULL,
      9247714629217840819ULL,
  };
  check(result->report.stages.size() == expected_modes.size(),
        "Planetfall must report each presentation stage exactly once");
  if (result->report.stages.size() == expected_modes.size()) {
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
  check(json.find("\"schema_version\": 1") != std::string::npos &&
            json.find("\"scenario\": \"v0.3-planetfall\"") !=
                std::string::npos &&
            json.find("\"final_flight_checksum\": "
                      "\"15600629779145530762\"") != std::string::npos &&
            json.find("\"presentation_mode\": \"terrain-blend\"") !=
                std::string::npos,
        "Planetfall JSON must preserve its versioned scenario and deterministic fields");
}

}  // namespace

auto main() -> int {
  generation_failure_matrix();
  deterministic_generation();
  seed_derivation_contract();
  origin_station_contract();
  origin_onboarding_contract();
  save_schema_contract();
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
  deterministic_planetary_flight_replay();
  planetary_flight_failure_matrix();
  sweep_selection_contract();
  sweep_report_contract();
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
  orbital_render_failure_matrix();
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
