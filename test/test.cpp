#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <numbers>
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
#include "apsis_drift/planet.hpp"
#include "apsis_drift/seed.hpp"
#include "apsis_drift/simulation.hpp"
#include "apsis_drift/terrain_tiles.hpp"
#include "apsis_drift/title.hpp"
#include "capability_floor.hpp"
#include "flight_input.hpp"

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
    check(address.has_value(), "valid directions must produce tile addresses");
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
            !parse_benchmark_workload("unknown"),
        "benchmark workloads must parse only their documented names");
  check(workload_identifier(BenchmarkWorkload::landscape) ==
                "voxel-landscape-rgba" &&
            workload_identifier(BenchmarkWorkload::orbital) ==
                "orbital-planet-rgba",
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

}  // namespace

auto main() -> int {
  generation_failure_matrix();
  deterministic_generation();
  seed_derivation_contract();
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
  if (failures != 0) {
    std::fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
  }
  std::puts("all Apsis Drift tests passed");
  return 0;
}
