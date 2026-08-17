#include "apsis_drift/planetfall_acceptance.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <span>
#include <utility>

#include "apsis_drift/landscape.hpp"
#include "apsis_drift/seed.hpp"
#include "apsis_drift/terrain_tiles.hpp"

namespace apsis_drift {
namespace {

inline constexpr std::uint8_t kFlightTerrainLod{12};
inline constexpr double kDescentStartAboveOrbitMetres{50'000.0};
inline constexpr double kStartLatitudeRadians{0.625};
inline constexpr double kStartLongitudeRadians{-std::numbers::pi / 4.0};

constexpr std::array kCommands{
    FlightCommand{0, FlightCommandKind::press_forward},
    FlightCommand{0, FlightCommandKind::press_turn_right},
    FlightCommand{0, FlightCommandKind::press_fall},
    FlightCommand{20'379, FlightCommandKind::release_fall},
};

struct CanonicalSurface {
  double latitude_radians{};
  double longitude_radians{};
  double elevation_metres{};
};

struct Replay {
  PlanetDescriptor planet;
  PlanetaryFlightState final_state;
  std::vector<FlightCommand> commands;
  std::array<PlanetaryFlightState, 4> checkpoints;
};

[[nodiscard]] auto canonical_surface(const PlanetDescriptor& planet,
                                     TerrainTileCache& cache)
    -> std::expected<CanonicalSurface, PlanetfallAcceptanceError> {
  const auto fixed = planet_fixed_from_geodetic(
      planet,
      {kStartLatitudeRadians, kStartLongitudeRadians, 0.0});
  if (!fixed) {
    return std::unexpected{PlanetfallAcceptanceError::terrain_failure};
  }
  const auto sample =
      sample_planet_surface(planet, *fixed, kFlightTerrainLod, cache);
  if (!sample) {
    return std::unexpected{PlanetfallAcceptanceError::terrain_failure};
  }
  return CanonicalSurface{kStartLatitudeRadians, kStartLongitudeRadians,
                          std::max(0.0, sample->elevation_metres)};
}

[[nodiscard]] auto surface_environment(const PlanetDescriptor& planet,
                                       const PlanetaryFlightState& state,
                                       TerrainTileCache& cache)
    -> std::expected<PlanetaryFlightEnvironment,
                     PlanetfallAcceptanceError> {
  const auto fixed = planet_fixed_from_geodetic(
      planet, {state.pose.position.latitude_radians,
               state.pose.position.longitude_radians, 0.0});
  if (!fixed) {
    return std::unexpected{PlanetfallAcceptanceError::terrain_failure};
  }
  const auto sample =
      sample_planet_surface(planet, *fixed, kFlightTerrainLod, cache);
  if (!sample) {
    return std::unexpected{PlanetfallAcceptanceError::terrain_failure};
  }
  return PlanetaryFlightEnvironment{
      std::max(0.0, sample->elevation_metres)};
}

[[nodiscard]] auto replay_descent()
    -> std::expected<Replay, PlanetfallAcceptanceError> {
  const auto planet =
      generate_planet_descriptor(Seed{kPlanetfallAcceptanceSeed});
  auto cache = TerrainTileCache::create();
  if (!cache) {
    return std::unexpected{PlanetfallAcceptanceError::terrain_failure};
  }
  const auto surface = canonical_surface(planet, *cache);
  const auto bands = flight_regime_bands(planet);
  if (!surface || !bands) {
    return std::unexpected{PlanetfallAcceptanceError::terrain_failure};
  }
  const double start_altitude =
      bands->orbit_enter_altitude_metres + kDescentStartAboveOrbitMetres;
  const auto initial = initial_planetary_flight_state(
      planet,
      {surface->latitude_radians, surface->longitude_radians, start_altitude},
      {surface->elevation_metres}, 0.35, FlightMode::manual);
  if (!initial) {
    return std::unexpected{PlanetfallAcceptanceError::flight_failure};
  }

  Replay replay{.planet = planet,
                .final_state = *initial,
                .commands = {},
                .checkpoints = {}};
  replay.commands.assign(kCommands.begin(), kCommands.end());
  replay.checkpoints[0] = *initial;
  bool atmospheric_recorded{};
  bool blend_recorded{};
  std::size_t next_command{};

  while (replay.final_state.tick < kPlanetfallAcceptanceTicks) {
    const auto first = next_command;
    while (next_command < replay.commands.size() &&
           replay.commands[next_command].tick == replay.final_state.tick) {
      ++next_command;
    }
    const std::span commands{replay.commands.data() + first,
                             next_command - first};
    const auto environment =
        surface_environment(planet, replay.final_state, *cache);
    if (!environment) return std::unexpected{environment.error()};
    if (!advance_planetary_flight(planet, *environment,
                                  replay.final_state, commands,
                                  kSimulationStep)) {
      return std::unexpected{PlanetfallAcceptanceError::flight_failure};
    }

    if (!atmospheric_recorded &&
        replay.final_state.regime == FlightRegime::atmospheric) {
      replay.checkpoints[1] = replay.final_state;
      atmospheric_recorded = true;
    }
    const auto mix =
        planetary_presentation_mix(planet, replay.final_state);
    if (!mix) {
      return std::unexpected{PlanetfallAcceptanceError::presentation_failure};
    }
    if (!blend_recorded && mix->local_terrain > 0.0 &&
        mix->local_terrain < 1.0) {
      replay.checkpoints[2] = replay.final_state;
      blend_recorded = true;
    }
  }
  replay.checkpoints[3] = replay.final_state;

  if (!atmospheric_recorded || !blend_recorded ||
      replay.checkpoints[3].tick != kPlanetfallAcceptanceTicks ||
      replay.final_state.regime != FlightRegime::terrain_flight) {
    return std::unexpected{PlanetfallAcceptanceError::incomplete_path};
  }
  return replay;
}

[[nodiscard]] auto percentile95(std::vector<double> values) -> double {
  std::ranges::sort(values);
  if (values.empty()) return 0.0;
  const auto index = std::min(
      values.size() - 1,
      static_cast<std::size_t>(
          std::ceil(static_cast<double>(values.size()) * 0.95)) -
          1);
  return values[index];
}

[[nodiscard]] auto cube_face_name(CubeFace face) noexcept
    -> std::string_view {
  switch (face) {
    case CubeFace::positive_x: return "+x";
    case CubeFace::negative_x: return "-x";
    case CubeFace::positive_y: return "+y";
    case CubeFace::negative_y: return "-y";
    case CubeFace::positive_z: return "+z";
    case CubeFace::negative_z: return "-z";
  }
  return "unknown";
}

}  // namespace

auto run_planetfall_acceptance(RenderConfiguration configuration)
    -> std::expected<PlanetfallAcceptanceResult,
                     PlanetfallAcceptanceError> {
  if (!validate_viewport(configuration.viewport)) {
    return std::unexpected{
        PlanetfallAcceptanceError::invalid_configuration};
  }
  const auto replay = replay_descent();
  if (!replay) return std::unexpected{replay.error()};

  PlanetaryPresentationSettings settings;
  settings.width = configuration.viewport.width;
  settings.height = configuration.viewport.height;
  PlanetaryPresentationRenderer renderer{settings};
  const auto pixel_count =
      static_cast<std::size_t>(configuration.viewport.width) *
      static_cast<std::size_t>(configuration.viewport.height);
  std::vector<termforge::Pixel> frame(pixel_count);
  PlanetfallAcceptanceResult result{
      .report = {.render_configuration = configuration,
                 .planet_id = replay->planet.id,
                 .planet_name = replay->planet.display_name,
                 .final_state = replay->final_state,
                 .command_count = replay->commands.size(),
                 .stages = {}},
      .final_frame = {}};
  result.report.stages.reserve(replay->checkpoints.size());

  for (std::size_t index = 0; index < replay->checkpoints.size(); ++index) {
    const auto& state = replay->checkpoints[index];
    const double pitch = index == 2 ? -1.25 : index == 3 ? -0.35 : -0.08;
    double orbital_total{};
    double local_total{};
    double composite_total{};
    double total{};
    std::vector<double> total_samples;
    total_samples.reserve(kPlanetfallAcceptanceFramesPerStage);
    PlanetaryRenderStats last_stats;
    for (std::size_t frame_index = 0;
         frame_index < kPlanetfallAcceptanceFramesPerStage; ++frame_index) {
      const auto rendered = renderer.render(
          replay->planet, state, {.pitch_radians = pitch}, frame);
      if (!rendered) {
        return std::unexpected{
            PlanetfallAcceptanceError::presentation_failure};
      }
      last_stats = *rendered;
      orbital_total += rendered->orbital_render_ms;
      local_total += rendered->local_render_ms;
      composite_total += rendered->composite_ms;
      total += rendered->total_ms;
      total_samples.push_back(rendered->total_ms);
    }
    const double count =
        static_cast<double>(kPlanetfallAcceptanceFramesPerStage);
    result.report.stages.push_back({
        .presentation_mode = last_stats.mode,
        .flight_regime = state.regime,
        .tick = state.tick,
        .position = state.pose.position,
        .clearance_metres = state.clearance_metres,
        .flight_checksum = planetary_flight_state_checksum(state),
        .framebuffer_checksum = pixel_checksum(frame),
        .surface_anchor = last_stats.surface_anchor,
        .orbital_tiles_touched = last_stats.orbital_tiles_touched,
        .local_tiles_touched = last_stats.local_tiles_touched,
        .orbital_render_avg_ms = orbital_total / count,
        .local_render_avg_ms = local_total / count,
        .composite_avg_ms = composite_total / count,
        .total_avg_ms = total / count,
        .total_p95_ms = percentile95(std::move(total_samples)),
    });
  }
  result.final_frame = std::move(frame);
  return result;
}

auto planetfall_acceptance_json(const PlanetfallAcceptanceReport& report)
    -> std::string {
  std::string stages;
  for (std::size_t index = 0; index < report.stages.size(); ++index) {
    const auto& stage = report.stages[index];
    stages += std::format(
        "    {{\n"
        "      \"presentation_mode\": \"{}\",\n"
        "      \"flight_regime\": \"{}\",\n"
        "      \"tick\": {},\n"
        "      \"position\": {{\"latitude_radians\": {:.12f}, "
        "\"longitude_radians\": {:.12f}, \"altitude_metres\": {:.6f}, "
        "\"clearance_metres\": {:.6f}}},\n"
        "      \"flight_checksum\": \"{}\",\n"
        "      \"framebuffer_checksum\": \"{}\",\n"
        "      \"surface_anchor\": {{\"face\": \"{}\", \"lod\": {}, "
        "\"x\": {}, \"y\": {}}},\n"
        "      \"orbital_tiles_touched\": {},\n"
        "      \"local_tiles_touched\": {},\n"
        "      \"orbital_render_avg_ms\": {:.6f},\n"
        "      \"local_render_avg_ms\": {:.6f},\n"
        "      \"composite_avg_ms\": {:.6f},\n"
        "      \"total_avg_ms\": {:.6f},\n"
        "      \"total_p95_ms\": {:.6f}\n"
        "    }}{}\n",
        planetary_presentation_mode_name(stage.presentation_mode),
        flight_regime_name(stage.flight_regime), stage.tick,
        stage.position.latitude_radians,
        stage.position.longitude_radians,
        stage.position.altitude_metres, stage.clearance_metres,
        stage.flight_checksum, stage.framebuffer_checksum,
        cube_face_name(stage.surface_anchor.tile.face),
        stage.surface_anchor.tile.lod, stage.surface_anchor.tile.x,
        stage.surface_anchor.tile.y, stage.orbital_tiles_touched,
        stage.local_tiles_touched, stage.orbital_render_avg_ms,
        stage.local_render_avg_ms, stage.composite_avg_ms,
        stage.total_avg_ms, stage.total_p95_ms,
        index + 1 == report.stages.size() ? "" : ",");
  }

  return std::format(
      "{{\n"
      "  \"schema_version\": 1,\n"
      "  \"scenario\": \"{}\",\n"
      "  \"seed\": {},\n"
      "  \"planet_generator_version\": {},\n"
      "  \"terrain_generator_version\": {},\n"
      "  \"planet_id\": \"planet-{:016x}\",\n"
      "  \"planet_name\": \"{}\",\n"
      "  \"simulation_hz\": {},\n"
      "  \"command_count\": {},\n"
      "  \"final_tick\": {},\n"
      "  \"final_flight_checksum\": \"{}\",\n"
      "  \"final_state\": {{\"latitude_radians\": {:.12f}, "
      "\"longitude_radians\": {:.12f}, \"altitude_metres\": {:.6f}, "
      "\"clearance_metres\": {:.6f}, \"heading_radians\": {:.12f}, "
      "\"regime\": \"{}\"}},\n"
      "  \"render_profile\": \"{}\",\n"
      "  \"viewport_width\": {},\n"
      "  \"viewport_height\": {},\n"
      "  \"frames_per_stage\": {},\n"
      "  \"stages\": [\n"
      "{}"
      "  ]\n"
      "}}\n",
      kPlanetfallAcceptanceScenario, kPlanetfallAcceptanceSeed,
      kPlanetGeneratorVersion, kTerrainTileGeneratorVersion,
      report.planet_id.value, report.planet_name, kSimulationHz,
      report.command_count, report.final_state.tick,
      planetary_flight_state_checksum(report.final_state),
      report.final_state.pose.position.latitude_radians,
      report.final_state.pose.position.longitude_radians,
      report.final_state.pose.position.altitude_metres,
      report.final_state.clearance_metres,
      report.final_state.pose.heading_radians,
      flight_regime_name(report.final_state.regime),
      profile_name(report.render_configuration),
      report.render_configuration.viewport.width,
      report.render_configuration.viewport.height,
      kPlanetfallAcceptanceFramesPerStage, stages);
}

}  // namespace apsis_drift
