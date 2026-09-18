#include "apsis_drift/system_rendering.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <ranges>
#include <utility>

namespace apsis_drift {
namespace {

struct Vector3 {
  double x{};
  double y{};
  double z{};
};

struct CameraBasis {
  Vector3 forward;
  Vector3 right;
  Vector3 up;
};

enum class BodyKind : std::uint8_t { star, planet };

struct ProjectedBody {
  BodyKind kind{};
  std::size_t ordinal{};
  PlanetId planet{};
  Rgb8 color{};
  double depth{};
  double screen_x{};
  double screen_y{};
  double physical_radius_pixels{};
  int draw_radius{};
};

[[nodiscard]] auto finite(double value) noexcept -> bool {
  return std::isfinite(value);
}

[[nodiscard]] auto vector(SystemPositionMetres value) noexcept -> Vector3 {
  return {value.x, value.y, value.z};
}

[[nodiscard]] auto vector(SystemVelocityMetresPerSecond value) noexcept
    -> Vector3 {
  return {value.x, value.y, value.z};
}

[[nodiscard]] auto vector(SystemDirection value) noexcept -> Vector3 {
  return {value.x, value.y, value.z};
}

[[nodiscard]] auto subtract(Vector3 left, Vector3 right) noexcept -> Vector3 {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] auto multiply(Vector3 value, double scalar) noexcept -> Vector3 {
  return {value.x * scalar, value.y * scalar, value.z * scalar};
}

[[nodiscard]] auto dot(Vector3 left, Vector3 right) noexcept -> double {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] auto cross(Vector3 left, Vector3 right) noexcept -> Vector3 {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

[[nodiscard]] auto length(Vector3 value) noexcept -> double {
  return std::hypot(value.x, value.y, value.z);
}

[[nodiscard]] auto finite(Vector3 value) noexcept -> bool {
  return finite(value.x) && finite(value.y) && finite(value.z);
}

[[nodiscard]] auto normalized(Vector3 value) noexcept -> Vector3 {
  return multiply(value, 1.0 / length(value));
}

[[nodiscard]] auto camera_basis(const LocalSystemView& view)
    -> std::expected<CameraBasis, LocalSystemRenderError> {
  const auto forward = vector(view.forward);
  const auto up = vector(view.up);
  if (!finite(forward) || !finite(up)) {
    return std::unexpected{LocalSystemRenderError::invalid_view};
  }
  const double forward_length = length(forward);
  const double up_length = length(up);
  if (!finite(forward_length) || !finite(up_length) ||
      forward_length <= 1.0e-12 || up_length <= 1.0e-12) {
    return std::unexpected{LocalSystemRenderError::invalid_camera_basis};
  }
  const auto forward_unit = normalized(forward);
  const auto right = cross(forward_unit, normalized(up));
  const double right_length = length(right);
  if (!finite(right_length) || right_length <= 1.0e-12) {
    return std::unexpected{LocalSystemRenderError::invalid_camera_basis};
  }
  const auto right_unit = normalized(right);
  return CameraBasis{forward_unit, right_unit,
                     normalized(cross(right_unit, forward_unit))};
}

[[nodiscard]] auto valid_settings(
    const LocalSystemRenderSettings& settings) noexcept -> bool {
  return validate_viewport({settings.width, settings.height}) &&
         finite(settings.field_of_view_degrees) &&
         settings.field_of_view_degrees > 1.0 &&
         settings.field_of_view_degrees < 179.0 &&
         finite(settings.near_clip_metres) &&
         finite(settings.far_clip_metres) && settings.near_clip_metres > 0.0 &&
         settings.far_clip_metres > settings.near_clip_metres &&
         finite(settings.handoff_start_radius_pixels) &&
         finite(settings.handoff_complete_radius_pixels) &&
         settings.handoff_start_radius_pixels > 0.0 &&
         settings.handoff_complete_radius_pixels >
             settings.handoff_start_radius_pixels;
}

[[nodiscard]] auto valid_view_numbers(const LocalSystemView& view) noexcept
    -> bool {
  return finite(vector(view.position)) && finite(vector(view.velocity)) &&
         finite(view.time.sub_tick_fraction) &&
         view.time.sub_tick_fraction >= 0.0 &&
         view.time.sub_tick_fraction < 1.0;
}

[[nodiscard]] auto mix64(std::uint64_t value) noexcept -> std::uint64_t {
  value ^= value >> 30U;
  value *= 0xBF58476D1CE4E5B9ULL;
  value ^= value >> 27U;
  value *= 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

[[nodiscard]] auto space_pixel(int x, int y, Seed seed) noexcept
    -> termforge::Pixel {
  auto key = static_cast<std::uint64_t>(static_cast<std::uint32_t>(x));
  key = std::rotl(key, 32) | static_cast<std::uint32_t>(y);
  const auto sample = mix64(key ^ seed.value ^ 0xA0761D6478BD642FULL);
  if ((sample & 0x3FFU) == 0U) {
    const auto value = static_cast<std::uint8_t>(
        150U + static_cast<unsigned>((sample >> 12U) & 0x69U));
    return {value, value,
            static_cast<std::uint8_t>(std::min(255U, value + 12U)), 255};
  }
  return {3, 6, 13, 255};
}

[[nodiscard]] auto channel(double value) noexcept -> std::uint8_t {
  return static_cast<std::uint8_t>(
      std::clamp(static_cast<int>(std::lround(value)), 0, 255));
}

[[nodiscard]] auto blend(termforge::Pixel from, termforge::Pixel to,
                         double amount) noexcept -> termforge::Pixel {
  amount = std::clamp(amount, 0.0, 1.0);
  return {channel(from.r * (1.0 - amount) + to.r * amount),
          channel(from.g * (1.0 - amount) + to.g * amount),
          channel(from.b * (1.0 - amount) + to.b * amount), 255};
}

[[nodiscard]] auto project_body(const LocalSystemRenderSettings& settings,
                                const CameraBasis& basis, Vector3 camera,
                                Vector3 center, double radius_metres,
                                BodyKind kind, std::size_t ordinal,
                                PlanetId planet, Rgb8 color)
    -> std::optional<ProjectedBody> {
  const auto relative = subtract(center, camera);
  const double distance = length(relative);
  const double depth = dot(relative, basis.forward);
  if (!finite(distance) || !finite(depth) || distance <= 0.0 ||
      depth < settings.near_clip_metres || depth > settings.far_clip_metres) {
    return std::nullopt;
  }
  const double tangent =
      std::tan(settings.field_of_view_degrees * std::numbers::pi / 360.0);
  const double focal = static_cast<double>(settings.width) / (2.0 * tangent);
  const double screen_x = static_cast<double>(settings.width - 1) * 0.5 +
                          dot(relative, basis.right) / depth * focal;
  const double screen_y = static_cast<double>(settings.height - 1) * 0.5 -
                          dot(relative, basis.up) / depth * focal;
  const double ratio = std::clamp(radius_metres / distance, 0.0, 1.0);
  const double physical_radius = focal * std::tan(std::asin(ratio));
  if (!finite(screen_x) || !finite(screen_y) || !finite(physical_radius)) {
    return std::nullopt;
  }
  const double minimum = kind == BodyKind::star ? 2.0 : 1.0;
  const double maximum = kind == BodyKind::star ? 96.0 : 32.0;
  const int draw_radius = static_cast<int>(
      std::lround(std::clamp(physical_radius, minimum, maximum)));
  const double visibility_radius =
      std::max(static_cast<double>(draw_radius), physical_radius);
  if (screen_x + visibility_radius < 0.0 ||
      screen_y + visibility_radius < 0.0 ||
      screen_x - visibility_radius >= settings.width ||
      screen_y - visibility_radius >= settings.height) {
    return std::nullopt;
  }
  return ProjectedBody{kind,     ordinal,  planet,          color,      depth,
                       screen_x, screen_y, physical_radius, draw_radius};
}

auto draw_body(const LocalSystemRenderSettings& settings,
               const ProjectedBody& body,
               std::span<termforge::Pixel> destination,
               LocalSystemRenderStats& stats) noexcept -> void {
  const int radius = body.draw_radius;
  if (body.screen_x < -static_cast<double>(radius) ||
      body.screen_x >= static_cast<double>(settings.width + radius) ||
      body.screen_y < -static_cast<double>(radius) ||
      body.screen_y >= static_cast<double>(settings.height + radius)) {
    return;
  }
  const int center_x = static_cast<int>(std::lround(body.screen_x));
  const int center_y = static_cast<int>(std::lround(body.screen_y));
  for (int y = center_y - radius; y <= center_y + radius; ++y) {
    if (y < 0 || y >= settings.height) continue;
    for (int x = center_x - radius; x <= center_x + radius; ++x) {
      if (x < 0 || x >= settings.width) continue;
      const double dx =
          static_cast<double>(x - center_x) / static_cast<double>(radius);
      const double dy =
          static_cast<double>(y - center_y) / static_cast<double>(radius);
      const double radial = dx * dx + dy * dy;
      if (radial > 1.0) continue;
      double shade = 0.52 + 0.48 * std::sqrt(1.0 - radial);
      if (body.kind == BodyKind::star) shade = 0.88 + 0.12 * shade;
      const termforge::Pixel color{
          channel(static_cast<double>(body.color.red) * shade),
          channel(static_cast<double>(body.color.green) * shade),
          channel(static_cast<double>(body.color.blue) * shade), 255};
      destination[static_cast<std::size_t>(y) *
                      static_cast<std::size_t>(settings.width) +
                  static_cast<std::size_t>(x)] = color;
      if (body.kind == BodyKind::star) {
        ++stats.star_pixels;
      } else {
        ++stats.planet_pixels;
      }
    }
  }
}

auto marker_pixel(int x, int y, const LocalSystemRenderSettings& settings,
                  std::span<termforge::Pixel> destination) noexcept -> void {
  if (x < 0 || x >= settings.width || y < 0 || y >= settings.height) return;
  destination[static_cast<std::size_t>(y) *
                  static_cast<std::size_t>(settings.width) +
              static_cast<std::size_t>(x)] = {238, 238, 214, 255};
}

auto draw_selection_marker(const ProjectedBody& body,
                           const LocalSystemRenderSettings& settings,
                           std::span<termforge::Pixel> destination) noexcept
    -> void {
  const double bounded_radius =
      std::clamp(std::max(static_cast<double>(body.draw_radius),
                          body.physical_radius_pixels),
                 2.0, 49.0);
  const int radius = static_cast<int>(std::ceil(bounded_radius)) + 3;
  if (body.screen_x < -static_cast<double>(radius) ||
      body.screen_x >= static_cast<double>(settings.width + radius) ||
      body.screen_y < -static_cast<double>(radius) ||
      body.screen_y >= static_cast<double>(settings.height + radius)) {
    return;
  }
  const int cx = static_cast<int>(std::lround(body.screen_x));
  const int cy = static_cast<int>(std::lround(body.screen_y));
  constexpr int arm{3};
  for (int offset = 0; offset < arm; ++offset) {
    marker_pixel(cx - radius + offset, cy - radius, settings, destination);
    marker_pixel(cx - radius, cy - radius + offset, settings, destination);
    marker_pixel(cx + radius - offset, cy - radius, settings, destination);
    marker_pixel(cx + radius, cy - radius + offset, settings, destination);
    marker_pixel(cx - radius + offset, cy + radius, settings, destination);
    marker_pixel(cx - radius, cy + radius - offset, settings, destination);
    marker_pixel(cx + radius - offset, cy + radius, settings, destination);
    marker_pixel(cx + radius, cy + radius - offset, settings, destination);
  }
}

auto draw_station_edge_cue(const CameraBasis& basis, Vector3 camera,
                           Vector3 station,
                           const LocalSystemRenderSettings& settings,
                           std::span<termforge::Pixel> destination) noexcept
    -> void {
  const auto relative = subtract(station, camera);
  const double distance = length(relative);
  if (!finite(relative) || !finite(distance)) return;

  double horizontal{};
  double vertical{};
  if (distance <= 1.0e-12) {
    vertical = -1.0;
  } else {
    const auto direction = multiply(relative, 1.0 / distance);
    horizontal = dot(direction, basis.right);
    vertical = -dot(direction, basis.up);
    const double depth = dot(direction, basis.forward);
    if (depth <= 0.0) vertical += -depth;
    if (std::hypot(horizontal, vertical) <= 1.0e-12) vertical = -1.0;
  }

  constexpr double margin{4.0};
  const double half_width =
      std::max(1.0, static_cast<double>(settings.width - 1) * 0.5 - margin);
  const double half_height =
      std::max(1.0, static_cast<double>(settings.height - 1) * 0.5 - margin);
  const double scale = std::min(
      std::abs(horizontal) > 1.0e-12 ? half_width / std::abs(horizontal)
                                     : std::numeric_limits<double>::infinity(),
      std::abs(vertical) > 1.0e-12 ? half_height / std::abs(vertical)
                                   : std::numeric_limits<double>::infinity());
  if (!finite(scale)) return;
  const int center_x = std::clamp(
      static_cast<int>(std::lround(
          static_cast<double>(settings.width - 1) * 0.5 + horizontal * scale)),
      0, settings.width - 1);
  const int center_y = std::clamp(
      static_cast<int>(std::lround(
          static_cast<double>(settings.height - 1) * 0.5 + vertical * scale)),
      0, settings.height - 1);
  const auto set = [&](int x, int y) {
    if (x < 0 || x >= settings.width || y < 0 || y >= settings.height) return;
    destination[static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(settings.width) +
                static_cast<std::size_t>(x)] = {126, 214, 210, 255};
  };
  set(center_x, center_y);
  set(center_x - 1, center_y);
  set(center_x + 1, center_y);
  set(center_x, center_y - 1);
  set(center_x, center_y + 1);
  if (std::abs(horizontal) >= std::abs(vertical)) {
    set(center_x + (horizontal < 0.0 ? 2 : -2), center_y);
  } else {
    set(center_x, center_y + (vertical < 0.0 ? 2 : -2));
  }
}

} // namespace

auto resolve_system_navigation(const LocalSystemDescriptor& system,
                               const LocalSystemView& view)
    -> std::expected<SystemNavigationSolution, LocalSystemRenderError> {
  if (!validate_local_system(system)) {
    return std::unexpected{LocalSystemRenderError::invalid_system};
  }
  if (!valid_view_numbers(view)) {
    return std::unexpected{LocalSystemRenderError::invalid_view};
  }
  const auto basis = camera_basis(view);
  if (!basis) return std::unexpected{basis.error()};
  const auto target = find_local_system_planet(system, view.selected_planet);
  if (!target) {
    return std::unexpected{LocalSystemRenderError::unknown_target};
  }
  const auto ephemeris =
      resolve_planet_ephemeris(system, view.selected_planet, view.time);
  if (!ephemeris) {
    return std::unexpected{LocalSystemRenderError::ephemeris_failure};
  }
  const auto relative =
      subtract(vector(ephemeris->position), vector(view.position));
  const double distance = length(relative);
  if (!finite(distance) || distance <= 0.0) {
    return std::unexpected{LocalSystemRenderError::invalid_view};
  }
  const auto direction = multiply(relative, 1.0 / distance);
  const auto relative_velocity =
      subtract(vector(ephemeris->velocity), vector(view.velocity));
  const double closing_speed = -dot(relative_velocity, direction);
  if (!finite(closing_speed)) {
    return std::unexpected{LocalSystemRenderError::invalid_view};
  }
  const double forward = dot(direction, basis->forward);
  const double bearing = std::atan2(dot(direction, basis->right), forward);
  const double elevation =
      std::asin(std::clamp(dot(direction, basis->up), -1.0, 1.0));
  constexpr double motion_epsilon{0.5};
  const auto motion =
      closing_speed > motion_epsilon
          ? SystemTargetMotion::closing
          : (closing_speed < -motion_epsilon ? SystemTargetMotion::opening
                                             : SystemTargetMotion::holding);
  return SystemNavigationSolution{view.selected_planet,
                                  (*target)->descriptor.display_name,
                                  bearing,
                                  elevation,
                                  distance,
                                  closing_speed,
                                  motion,
                                  forward > 0.0};
}

LocalSystemRenderer::LocalSystemRenderer(LocalSystemRenderSettings settings)
    : m_settings(settings),
      m_orbital_renderer(
          {.width = settings.width,
           .height = settings.height,
           .field_of_view_degrees = settings.field_of_view_degrees}),
      m_system_frame(valid_settings(settings)
                         ? static_cast<std::size_t>(settings.width) *
                               static_cast<std::size_t>(settings.height)
                         : 0U),
      m_orbital_frame(m_system_frame.size()) {
}

auto LocalSystemRenderer::render(const LocalSystemDescriptor& system,
                                 const LocalSystemView& view,
                                 std::span<termforge::Pixel> destination)
    -> std::expected<LocalSystemRenderStats, LocalSystemRenderError> {
  if (!valid_settings(m_settings)) {
    return std::unexpected{LocalSystemRenderError::invalid_settings};
  }
  if (destination.size() != m_system_frame.size()) {
    return std::unexpected{LocalSystemRenderError::invalid_framebuffer};
  }
  const auto navigation = resolve_system_navigation(system, view);
  if (!navigation) return std::unexpected{navigation.error()};
  const auto basis = camera_basis(view);
  if (!basis) return std::unexpected{basis.error()};

  for (int y = 0; y < m_settings.height; ++y) {
    for (int x = 0; x < m_settings.width; ++x) {
      m_system_frame[static_cast<std::size_t>(y) *
                         static_cast<std::size_t>(m_settings.width) +
                     static_cast<std::size_t>(x)] =
          space_pixel(x, y, system.seed);
    }
  }

  std::vector<ProjectedBody> bodies;
  bodies.reserve(system.planets.size() + 1U);
  if (auto star = project_body(
          m_settings, *basis, vector(view.position), {},
          static_cast<double>(system.star.radius_kilometres) * 1'000.0,
          BodyKind::star, 0, {}, system.star.color)) {
    bodies.push_back(*star);
  }

  std::optional<ProjectedBody> selected_projection;
  std::optional<PlanetEphemeris> selected_ephemeris;
  for (std::size_t index = 0; index < system.planets.size(); ++index) {
    const auto& planet = system.planets[index];
    const auto ephemeris =
        resolve_planet_ephemeris(system, planet.descriptor.id, view.time);
    if (!ephemeris) {
      return std::unexpected{LocalSystemRenderError::ephemeris_failure};
    }
    const Rgb8 color =
        planet.descriptor.atmosphere_class == AtmosphereClass::airless
            ? planet.descriptor.palette.highland
            : planet.descriptor.palette.atmosphere;
    auto projected = project_body(
        m_settings, *basis, vector(view.position), vector(ephemeris->position),
        static_cast<double>(planet.descriptor.radius.value) * 1'000.0,
        BodyKind::planet, index, planet.descriptor.id, color);
    if (planet.descriptor.id == view.selected_planet) {
      selected_ephemeris = *ephemeris;
      if (projected) selected_projection = *projected;
    }
    if (projected) bodies.push_back(*projected);
  }

  std::ranges::sort(bodies,
                    [](const ProjectedBody& left, const ProjectedBody& right) {
                      if (left.depth != right.depth)
                        return left.depth > right.depth;
                      if (left.kind != right.kind)
                        return left.kind < right.kind;
                      return left.ordinal > right.ordinal;
                    });

  LocalSystemRenderStats stats;
  stats.navigation = *navigation;
  for (const auto& body : bodies) {
    draw_body(m_settings, body, m_system_frame, stats);
    if (body.kind == BodyKind::planet) ++stats.visible_planets;
  }
  stats.selected_visible = selected_projection.has_value();
  if (selected_projection) {
    stats.target_projected_radius_pixels =
        selected_projection->physical_radius_pixels;
  }

  if (selected_projection && selected_ephemeris &&
      selected_projection->physical_radius_pixels >=
          m_settings.handoff_start_radius_pixels) {
    const auto target = find_local_system_planet(system, view.selected_planet);
    if (!target) {
      return std::unexpected{LocalSystemRenderError::unknown_target};
    }
    const auto camera_relative =
        subtract(vector(view.position), vector(selected_ephemeris->position));
    OrbitalCamera camera;
    camera.position = {camera_relative.x, camera_relative.y, camera_relative.z};
    camera.forward = {view.forward.x, view.forward.y, view.forward.z};
    camera.up = {view.up.x, view.up.y, view.up.z};
    const PlanetFixedDirection light{-selected_ephemeris->position.x,
                                     -selected_ephemeris->position.y,
                                     -selected_ephemeris->position.z};
    const auto rendered = m_orbital_renderer.render(
        (*target)->descriptor, camera, light, m_orbital_frame);
    if (!rendered) {
      return std::unexpected{LocalSystemRenderError::orbital_failure};
    }
    stats.orbital_mix =
        std::clamp((selected_projection->physical_radius_pixels -
                    m_settings.handoff_start_radius_pixels) /
                       (m_settings.handoff_complete_radius_pixels -
                        m_settings.handoff_start_radius_pixels),
                   0.0, 1.0);
    for (std::size_t index = 0; index < m_system_frame.size(); ++index) {
      m_system_frame[index] = blend(m_system_frame[index],
                                    m_orbital_frame[index], stats.orbital_mix);
    }
    stats.mode = stats.orbital_mix >= 1.0
                     ? LocalSystemPresentationMode::orbital_target
                     : LocalSystemPresentationMode::target_handoff;
  }

  if (selected_projection) {
    draw_selection_marker(*selected_projection, m_settings, m_system_frame);
  }
  std::ranges::copy(m_system_frame, destination.begin());
  return stats;
}

auto LocalSystemRenderer::render_origin_station(
    const LocalSystemView& view, const OriginStationEphemeris& station,
    std::span<termforge::Pixel> destination)
    -> std::expected<void, LocalSystemRenderError> {
  if (!valid_settings(m_settings)) {
    return std::unexpected{LocalSystemRenderError::invalid_settings};
  }
  if (destination.size() != m_system_frame.size()) {
    return std::unexpected{LocalSystemRenderError::invalid_framebuffer};
  }
  if (!valid_view_numbers(view)) {
    return std::unexpected{LocalSystemRenderError::invalid_view};
  }
  if (!finite(station.position.x) || !finite(station.position.y) ||
      !finite(station.position.z) || !finite(station.velocity.x) ||
      !finite(station.velocity.y) || !finite(station.velocity.z) ||
      !finite(station.host_relative_position.x) ||
      !finite(station.host_relative_position.y) ||
      !finite(station.host_relative_position.z) ||
      !finite(station.host_relative_velocity.x) ||
      !finite(station.host_relative_velocity.y) ||
      !finite(station.host_relative_velocity.z) ||
      !finite(station.phase_radians)) {
    return std::unexpected{LocalSystemRenderError::ephemeris_failure};
  }
  const auto basis = camera_basis(view);
  if (!basis) return std::unexpected{basis.error()};
  const auto projected = project_body(
      m_settings, *basis, vector(view.position), vector(station.position),
      1'000.0, BodyKind::planet, 0, station.host_planet, Rgb8{126, 214, 210});
  if (!projected) {
    draw_station_edge_cue(*basis, vector(view.position),
                          vector(station.position), m_settings, destination);
    return {};
  }
  const int center_x = static_cast<int>(std::lround(projected->screen_x));
  const int center_y = static_cast<int>(std::lround(projected->screen_y));
  const auto set = [&](int x, int y) {
    if (x < 0 || x >= m_settings.width || y < 0 || y >= m_settings.height) {
      return;
    }
    destination[static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(m_settings.width) +
                static_cast<std::size_t>(x)] = {126, 214, 210, 255};
  };
  for (int offset = -8; offset <= 8; ++offset) {
    if (std::abs(offset) >= 4) {
      set(center_x + offset, center_y);
      set(center_x, center_y + offset);
    }
  }
  return {};
}

} // namespace apsis_drift
