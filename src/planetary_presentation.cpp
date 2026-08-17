#include "apsis_drift/planetary_presentation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <vector>

#include "apsis_drift/render_profile.hpp"

namespace apsis_drift {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] auto elapsed_ms(Clock::time_point start,
                              Clock::time_point finish) noexcept -> double {
  return std::chrono::duration<double, std::milli>(finish - start).count();
}

[[nodiscard]] auto optional_cache(std::size_t capacity)
    -> std::optional<TerrainTileCache> {
  auto cache = TerrainTileCache::create(capacity);
  if (!cache) return std::nullopt;
  return std::move(*cache);
}

[[nodiscard]] auto pixel(Rgb8 value) noexcept -> termforge::Pixel {
  return {value.red, value.green, value.blue, 255};
}

inline constexpr std::uint32_t kBlendScale{65'536};

[[nodiscard]] auto blend_weight(double amount) noexcept -> std::uint32_t {
  return static_cast<std::uint32_t>(std::clamp(
      std::lround(std::clamp(amount, 0.0, 1.0) * kBlendScale), 0L,
      static_cast<long>(kBlendScale)));
}

[[nodiscard]] auto blend_channel(std::uint8_t from, std::uint8_t to,
                                 std::uint32_t weight) noexcept
    -> std::uint8_t {
  const auto result =
      static_cast<std::uint32_t>(from) * (kBlendScale - weight) +
      static_cast<std::uint32_t>(to) * weight + kBlendScale / 2;
  return static_cast<std::uint8_t>(result / kBlendScale);
}

[[nodiscard]] auto blend(termforge::Pixel from, termforge::Pixel to,
                         std::uint32_t weight) noexcept -> termforge::Pixel {
  if (weight == 0) return from;
  if (weight == kBlendScale) return to;
  return {blend_channel(from.r, to.r, weight),
          blend_channel(from.g, to.g, weight),
          blend_channel(from.b, to.b, weight), 255};
}

[[nodiscard]] auto can_affect_channel(std::uint32_t weight) noexcept -> bool {
  return weight * 255U >= kBlendScale / 2;
}

[[nodiscard]] auto mix64(std::uint64_t value) noexcept -> std::uint64_t {
  value ^= value >> 30U;
  value *= 0xBF58476D1CE4E5B9ULL;
  value ^= value >> 27U;
  value *= 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

[[nodiscard]] auto dark_space(termforge::Pixel value) noexcept -> bool {
  return value.r < 32 && value.g < 36 && value.b < 48;
}

auto render_atmospheric_context(
    const PlanetaryPresentationSettings& settings,
    const PlanetDescriptor& planet, PlanetaryPresentationCamera camera,
    double atmosphere_mix,
    std::span<termforge::Pixel> destination) noexcept -> void {
  if (planet.atmosphere_class == AtmosphereClass::airless ||
      atmosphere_mix <= 0.0) {
    return;
  }
  const double tangent = std::tan(settings.field_of_view_degrees *
                                  std::numbers::pi / 360.0);
  const double focal = static_cast<double>(settings.width) / (2.0 * tangent);
  const double horizon = static_cast<double>(settings.height - 1) * 0.5 +
                         std::tan(camera.pitch_radians) * focal;
  const double pressure = std::clamp(
      static_cast<double>(planet.atmosphere_pressure.value) /
          static_cast<double>(AtmospherePressureMillibars::max),
      0.0, 1.0);
  const double density = 0.62 + std::sqrt(pressure) * 0.38;
  const auto atmosphere = pixel(planet.palette.atmosphere);
  const termforge::Pixel daylight{196, 208, 218, 255};

  for (int y = 0; y < settings.height; ++y) {
    const double horizon_distance =
        std::abs(static_cast<double>(y) - horizon) /
        std::max(1.0, static_cast<double>(settings.height) * 0.72);
    const double horizon_haze =
        std::pow(1.0 - std::clamp(horizon_distance, 0.0, 1.0), 2.0);
    const auto sky = blend(
        atmosphere, daylight,
        blend_weight(0.16 + horizon_haze * (0.20 + pressure * 0.18)));
    const auto weight = blend_weight(
        atmosphere_mix * density * (0.22 + horizon_haze * 0.38));
    const auto row = static_cast<std::size_t>(y) *
                     static_cast<std::size_t>(settings.width);
    for (int x = 0; x < settings.width; ++x) {
      auto& target = destination[row + static_cast<std::size_t>(x)];
      target = blend(target, sky, weight);
    }
  }
}

auto render_orbital_motion_cues(
    const PlanetaryPresentationSettings& settings,
    const PlanetaryFlightState& state,
    std::span<termforge::Pixel> destination) noexcept -> void {
  if (state.regime != FlightRegime::orbital) return;
  const double speed = std::hypot(
      state.velocity.east_metres_per_second,
      state.velocity.north_metres_per_second,
      state.velocity.up_metres_per_second);
  if (speed > 1.0) {
    constexpr int streak_count{36};
    const int centre_x = settings.width / 2;
    const int centre_y = settings.height / 2;
    const int radius_limit = std::max(settings.width, settings.height);
    const int trail = std::clamp(1 + static_cast<int>(speed / 800.0), 1, 6);
    constexpr double tau{2.0 * std::numbers::pi_v<double>};
    for (int index = 0; index < streak_count; ++index) {
      const auto key = mix64(state.planet.value ^
                             (static_cast<std::uint64_t>(index) *
                              0x9E3779B97F4A7C15ULL));
      const double angle = static_cast<double>(key >> 11U) *
                           (tau / 9'007'199'254'740'992.0);
      const int phase = static_cast<int>(
          (key + state.tick / 2U) % static_cast<std::uint64_t>(radius_limit));
      for (int offset = 0; offset < trail; ++offset) {
        const int radius = std::max(2, phase - offset * 2);
        const int x = centre_x +
                      static_cast<int>(std::lround(std::cos(angle) * radius));
        const int y = centre_y +
                      static_cast<int>(std::lround(std::sin(angle) * radius));
        if (x < 0 || x >= settings.width || y < 0 || y >= settings.height) {
          continue;
        }
        auto& target = destination[static_cast<std::size_t>(y) *
                                       static_cast<std::size_t>(settings.width) +
                                   static_cast<std::size_t>(x)];
        if (!dark_space(target)) continue;
        const auto brightness = static_cast<std::uint8_t>(
            std::clamp(210 - offset * 20, 120, 230));
        target = {brightness, brightness,
                  static_cast<std::uint8_t>(std::min(255, brightness + 18)),
                  255};
      }
    }
  }

  const auto drive = flight_drive_state(state);
  if (!drive || *drive == FlightDriveState::idle ||
      *drive == FlightDriveState::coast) {
    return;
  }
  termforge::Pixel color{126, 214, 210, 255};
  if (*drive == FlightDriveState::reverse ||
      *drive == FlightDriveState::braking) {
    color = {238, 184, 104, 255};
  }
  const int y = std::max(0, settings.height - 8);
  const int centre_x = settings.width / 2;
  for (int x = centre_x - 3; x <= centre_x + 3; ++x) {
    if (x >= 0 && x < settings.width) {
      destination[static_cast<std::size_t>(y) *
                      static_cast<std::size_t>(settings.width) +
                  static_cast<std::size_t>(x)] = color;
    }
  }
}

[[nodiscard]] auto finite_state(const PlanetaryFlightState& state) noexcept
    -> bool {
  const auto valid_regime = [](FlightRegime regime) {
    return regime == FlightRegime::orbital ||
           regime == FlightRegime::atmospheric ||
           regime == FlightRegime::terrain_flight;
  };
  const bool regime_valid = valid_regime(state.regime);
  const bool mode_valid = state.mode == FlightMode::manual ||
                          state.mode == FlightMode::autopilot;
  const bool transition_valid =
      !state.last_transition ||
      (valid_regime(state.last_transition->from) &&
       valid_regime(state.last_transition->to) &&
       state.last_transition->from != state.last_transition->to &&
       state.last_transition->to == state.regime &&
       state.last_transition->tick <= state.tick);
  return regime_valid && mode_valid && transition_valid &&
         std::isfinite(state.pose.position.latitude_radians) &&
         std::isfinite(state.pose.position.longitude_radians) &&
         std::isfinite(state.pose.position.altitude_metres) &&
         std::isfinite(state.pose.heading_radians) &&
         std::isfinite(state.velocity.east_metres_per_second) &&
         std::isfinite(state.velocity.north_metres_per_second) &&
         std::isfinite(state.velocity.up_metres_per_second) &&
         std::isfinite(state.clearance_metres) && state.clearance_metres >= 0.0;
}

[[nodiscard]] auto valid_settings(
    const PlanetaryPresentationSettings& settings) noexcept -> bool {
  return validate_viewport({settings.width, settings.height}) &&
         std::isfinite(settings.field_of_view_degrees) &&
         settings.field_of_view_degrees > 1.0 &&
         settings.field_of_view_degrees < 179.0 &&
         std::isfinite(settings.local_max_distance_metres) &&
         settings.local_max_distance_metres > 1.0 &&
         std::isfinite(settings.local_fog_start_metres) &&
         settings.local_fog_start_metres >= 0.0 &&
         settings.local_fog_start_metres <
             settings.local_max_distance_metres &&
         settings.orbital_terrain_lod <= kMaxTerrainLod &&
         settings.terrain_cache_capacity > 0;
}

[[nodiscard]] auto render_local(
    const PlanetaryPresentationSettings& settings,
    const PlanetDescriptor& planet, const PlanetaryFlightState& state,
    PlanetaryPresentationCamera camera, std::uint8_t lod,
    TerrainTileCache& cache, std::span<termforge::Pixel> destination)
    -> std::expected<std::size_t, PlanetaryPresentationError> {
  const GeodeticPosition surface_origin{
      state.pose.position.latitude_radians,
      state.pose.position.longitude_radians, 0.0};
  const auto frame = make_local_tangent_frame(planet, surface_origin);
  if (!frame) {
    return std::unexpected{PlanetaryPresentationError::coordinate_failure};
  }

  const double tangent = std::tan(settings.field_of_view_degrees *
                                  std::numbers::pi / 360.0);
  const double focal = static_cast<double>(settings.width) / (2.0 * tangent);
  const double horizon =
      static_cast<double>(settings.height - 1) * 0.5 +
      std::tan(camera.pitch_radians) * focal;
  const auto atmosphere = pixel(planet.palette.atmosphere);
  const termforge::Pixel space{4, 7, 13, 255};
  const bool airless = planet.atmosphere_class == AtmosphereClass::airless;
  for (int y = 0; y < settings.height; ++y) {
    const double normalized = settings.height > 1
                                  ? static_cast<double>(y) /
                                        static_cast<double>(settings.height - 1)
                                  : 0.0;
    const double haze = std::clamp(
        normalized /
            std::max(1.0 / static_cast<double>(settings.height),
                     horizon / static_cast<double>(settings.height)),
        0.0, 1.0);
    const auto sky = airless
                         ? space
                         : blend(space, atmosphere,
                                 blend_weight(0.18 + haze * 0.52));
    std::fill_n(destination.begin() +
                    static_cast<std::ptrdiff_t>(y) * settings.width,
                settings.width, sky);
  }

  const double forward_east = std::cos(state.pose.heading_radians);
  const double forward_north = std::sin(state.pose.heading_radians);
  const double right_east = -forward_north;
  const double right_north = forward_east;
  const double eye_altitude = state.pose.position.altitude_metres;
  std::vector<int> occlusion(static_cast<std::size_t>(settings.width),
                             settings.height);
  auto sampler = TerrainSurfaceSampler::create(planet, lod, cache);
  if (!sampler) {
    return std::unexpected{PlanetaryPresentationError::terrain_failure};
  }

  double distance = 1.0;
  while (distance < settings.local_max_distance_metres) {
    const double half_width = distance * tangent;
    double east = forward_east * distance - right_east * half_width;
    double north = forward_north * distance - right_north * half_width;
    const double step_east =
        right_east * (2.0 * half_width / settings.width);
    const double step_north =
        right_north * (2.0 * half_width / settings.width);
    const double fog = std::clamp(
        (distance - settings.local_fog_start_metres) /
            (settings.local_max_distance_metres -
             settings.local_fog_start_metres),
        0.0, 1.0);

    for (int x = 0; x < settings.width; ++x) {
      const auto fixed = planet_fixed_from_local(*frame, {east, north, 0.0});
      if (!fixed) {
        return std::unexpected{PlanetaryPresentationError::coordinate_failure};
      }
      const auto sample = sampler->sample(*fixed);
      if (!sample) {
        return std::unexpected{PlanetaryPresentationError::terrain_failure};
      }
      const double ground = std::max(0.0, sample->elevation_metres);
      const double projected =
          horizon + (eye_altitude - ground) * focal / distance;
      const int top = static_cast<int>(std::clamp(
          projected, 0.0, static_cast<double>(settings.height)));
      const int bottom = occlusion[static_cast<std::size_t>(x)];
      if (top < bottom) {
        auto color = pixel(sample->color);
        color = blend(color, airless ? space : atmosphere,
                      blend_weight(fog * fog * 0.72));
        for (int y = top; y < bottom; ++y) {
          destination[static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(settings.width) +
                      static_cast<std::size_t>(x)] = color;
        }
        occlusion[static_cast<std::size_t>(x)] = top;
      }
      east += step_east;
      north += step_north;
    }
    distance += std::max(0.75, distance * 0.0125);
  }
  return sampler->tiles_touched();
}

}  // namespace

auto planetary_presentation_mode_name(
    PlanetaryPresentationMode mode) noexcept -> std::string_view {
  switch (mode) {
    case PlanetaryPresentationMode::orbital: return "orbital";
    case PlanetaryPresentationMode::atmospheric: return "atmospheric";
    case PlanetaryPresentationMode::terrain_blend: return "terrain-blend";
    case PlanetaryPresentationMode::local_terrain: return "local-terrain";
  }
  return "unknown";
}

auto planetary_presentation_mix(const PlanetDescriptor& planet,
                                const PlanetaryFlightState& state) noexcept
    -> std::expected<PlanetaryPresentationMix, PlanetaryPresentationError> {
  if (state.planet != planet.id || !finite_state(state)) {
    return std::unexpected{PlanetaryPresentationError::invalid_state};
  }
  const auto bands = flight_regime_bands(planet);
  if (!bands) {
    return std::unexpected{PlanetaryPresentationError::invalid_state};
  }
  double atmosphere = std::clamp(
      (bands->orbit_enter_altitude_metres -
       state.pose.position.altitude_metres) /
          (bands->orbit_enter_altitude_metres -
           bands->atmosphere_enter_altitude_metres),
      0.0, 1.0);
  if (planet.atmosphere_class == AtmosphereClass::airless) atmosphere = 0.0;
  const double local = std::clamp(
      (bands->terrain_exit_clearance_metres - state.clearance_metres) /
          (bands->terrain_exit_clearance_metres -
           bands->terrain_enter_clearance_metres),
      0.0, 1.0);
  return PlanetaryPresentationMix{atmosphere, local};
}

PlanetaryPresentationRenderer::PlanetaryPresentationRenderer(
    PlanetaryPresentationSettings settings)
    : m_settings(settings),
      m_cache(optional_cache(settings.terrain_cache_capacity)),
      m_orbital_renderer({.width = settings.width,
                          .height = settings.height,
                          .field_of_view_degrees = settings.field_of_view_degrees,
                          .horizontal_sample_stride =
                              settings.width >= kDefaultViewportWidth ? 2 : 1,
                          .light_direction = settings.light_direction}),
      m_orbital_frame(validate_viewport({settings.width, settings.height})
                          ? static_cast<std::size_t>(settings.width) *
                                static_cast<std::size_t>(settings.height)
                          : 0U),
      m_local_frame(m_orbital_frame.size()) {}

auto PlanetaryPresentationRenderer::render(
    const PlanetDescriptor& planet, const PlanetaryFlightState& state,
    PlanetaryPresentationCamera camera,
    std::span<termforge::Pixel> destination)
    -> std::expected<PlanetaryRenderStats, PlanetaryPresentationError> {
  const auto total_started = Clock::now();
  if (!valid_settings(m_settings)) {
    return std::unexpected{PlanetaryPresentationError::invalid_settings};
  }
  if (!m_cache) {
    return std::unexpected{PlanetaryPresentationError::invalid_settings};
  }
  const auto expected = static_cast<std::size_t>(m_settings.width) *
                        static_cast<std::size_t>(m_settings.height);
  if (destination.size() != expected) {
    return std::unexpected{PlanetaryPresentationError::invalid_framebuffer};
  }
  if (!std::isfinite(camera.pitch_radians) ||
      std::abs(camera.pitch_radians) >= std::numbers::pi / 2.0) {
    return std::unexpected{PlanetaryPresentationError::invalid_camera};
  }
  const auto mix = planetary_presentation_mix(planet, state);
  if (!mix) return std::unexpected{mix.error()};

  const auto selected_lod = select_terrain_lod(
      planet, std::max(0.0, state.pose.position.altitude_metres));
  if (!selected_lod) {
    return std::unexpected{PlanetaryPresentationError::coordinate_failure};
  }
  const auto surface_position = planet_fixed_from_geodetic(
      planet, {state.pose.position.latitude_radians,
               state.pose.position.longitude_radians, 0.0});
  if (!surface_position) {
    return std::unexpected{PlanetaryPresentationError::coordinate_failure};
  }
  const auto anchor =
      sample_planet_surface(planet, *surface_position, *selected_lod, *m_cache);
  if (!anchor) {
    return std::unexpected{PlanetaryPresentationError::terrain_failure};
  }

  PlanetaryRenderStats stats;
  stats.mix = *mix;
  stats.local_terrain_lod = *selected_lod;
  stats.surface_anchor = anchor->address;
  if (mix->local_terrain >= 1.0) {
    stats.mode = PlanetaryPresentationMode::local_terrain;
  } else if (mix->local_terrain > 0.0) {
    stats.mode = PlanetaryPresentationMode::terrain_blend;
  } else if (mix->atmosphere > 0.0) {
    stats.mode = PlanetaryPresentationMode::atmospheric;
  } else {
    stats.mode = PlanetaryPresentationMode::orbital;
  }

  const auto local_weight = blend_weight(mix->local_terrain);
  const auto orbital_weight = kBlendScale - local_weight;
  const bool render_orbital =
      local_weight < kBlendScale && can_affect_channel(orbital_weight);
  const bool render_local_pass =
      local_weight > 0 && can_affect_channel(local_weight);

  if (render_orbital) {
    const auto fixed = planet_fixed_from_geodetic(planet, state.pose.position);
    const auto frame = make_local_tangent_frame(planet, state.pose.position);
    if (!fixed || !frame) {
      return std::unexpected{PlanetaryPresentationError::coordinate_failure};
    }
    const double horizontal = std::cos(camera.pitch_radians);
    const double vertical = std::sin(camera.pitch_radians);
    const double heading_cos = std::cos(state.pose.heading_radians);
    const double heading_sin = std::sin(state.pose.heading_radians);
    const PlanetFixedDirection forward{
        (frame->east.x * heading_cos + frame->north.x * heading_sin) * horizontal +
            frame->up.x * vertical,
        (frame->east.y * heading_cos + frame->north.y * heading_sin) * horizontal +
            frame->up.y * vertical,
        (frame->east.z * heading_cos + frame->north.z * heading_sin) * horizontal +
            frame->up.z * vertical};
    const PlanetFixedDirection up{
        frame->up.x * horizontal -
            (frame->east.x * heading_cos + frame->north.x * heading_sin) * vertical,
        frame->up.y * horizontal -
            (frame->east.y * heading_cos + frame->north.y * heading_sin) * vertical,
        frame->up.z * horizontal -
            (frame->east.z * heading_cos + frame->north.z * heading_sin) * vertical};
    const auto started = Clock::now();
    const auto orbital = m_orbital_renderer.render_tile_backed(
        planet, {*fixed, forward, up}, m_settings.orbital_terrain_lod,
        *m_cache, m_orbital_frame);
    stats.orbital_render_ms = elapsed_ms(started, Clock::now());
    if (!orbital) {
      return std::unexpected{PlanetaryPresentationError::orbital_failure};
    }
    stats.orbital_tiles_touched = orbital->terrain_tiles_touched;
    render_atmospheric_context(m_settings, planet, camera, mix->atmosphere,
                               m_orbital_frame);
    render_orbital_motion_cues(m_settings, state, m_orbital_frame);
  }

  if (render_local_pass) {
    const auto started = Clock::now();
    const auto tiles = render_local(m_settings, planet, state, camera,
                                    *selected_lod, *m_cache, m_local_frame);
    stats.local_render_ms = elapsed_ms(started, Clock::now());
    if (!tiles) return std::unexpected{tiles.error()};
    stats.local_tiles_touched = *tiles;
  }

  const auto composite_started = Clock::now();
  for (std::size_t index = 0; index < expected; ++index) {
    termforge::Pixel result;
    if (!render_orbital) {
      result = m_local_frame[index];
    } else if (!render_local_pass) {
      result = m_orbital_frame[index];
    } else {
      result = blend(m_orbital_frame[index], m_local_frame[index],
                     local_weight);
    }
    destination[index] = result;
  }
  stats.composite_ms = elapsed_ms(composite_started, Clock::now());
  stats.total_ms = elapsed_ms(total_started, Clock::now());
  return stats;
}

}  // namespace apsis_drift
