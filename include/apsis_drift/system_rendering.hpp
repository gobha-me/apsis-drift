#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "apsis_drift/local_system.hpp"
#include "apsis_drift/orbital.hpp"
#include "apsis_drift/render_profile.hpp"
#include "termforge/core/types.hpp"

namespace apsis_drift {

struct LocalSystemView {
  EphemerisQueryTime time;
  SystemPositionMetres position;
  SystemVelocityMetresPerSecond velocity;
  SystemDirection forward{0.0, 1.0, 0.0};
  SystemDirection up{0.0, 0.0, 1.0};
  PlanetId selected_planet;
};

enum class SystemTargetMotion : std::uint8_t {
  holding,
  closing,
  opening,
};

struct SystemNavigationSolution {
  PlanetId target;
  std::string display_name;
  double bearing_radians{};
  double elevation_radians{};
  double distance_metres{};
  double closing_speed_metres_per_second{};
  SystemTargetMotion motion{SystemTargetMotion::holding};
  bool in_front{};

  friend auto operator==(const SystemNavigationSolution&,
                         const SystemNavigationSolution&) -> bool = default;
};

enum class LocalSystemPresentationMode : std::uint8_t {
  system,
  target_handoff,
  orbital_target,
};

struct LocalSystemRenderSettings {
  int width{kDefaultViewportWidth};
  int height{kDefaultViewportHeight};
  double field_of_view_degrees{60.0};
  double near_clip_metres{1'000.0};
  double far_clip_metres{100'000'000'000.0};
  double handoff_start_radius_pixels{24.0};
  double handoff_complete_radius_pixels{48.0};
};

struct LocalSystemRenderStats {
  LocalSystemPresentationMode mode{LocalSystemPresentationMode::system};
  SystemNavigationSolution navigation;
  std::size_t visible_planets{};
  std::size_t star_pixels{};
  std::size_t planet_pixels{};
  double target_projected_radius_pixels{};
  double orbital_mix{};
  bool selected_visible{};

  friend auto operator==(const LocalSystemRenderStats&,
                         const LocalSystemRenderStats&) -> bool = default;
};

enum class LocalSystemRenderError : std::uint8_t {
  invalid_settings,
  invalid_framebuffer,
  invalid_system,
  invalid_view,
  invalid_camera_basis,
  unknown_target,
  ephemeris_failure,
  orbital_failure,
};

[[nodiscard]] auto resolve_system_navigation(
    const LocalSystemDescriptor& system, const LocalSystemView& view)
    -> std::expected<SystemNavigationSolution, LocalSystemRenderError>;

class LocalSystemRenderer {
 public:
  explicit LocalSystemRenderer(LocalSystemRenderSettings settings = {});

  [[nodiscard]] auto settings() const noexcept
      -> const LocalSystemRenderSettings& {
    return m_settings;
  }

  // A rejected frame leaves destination unchanged. Ephemeris time is an
  // explicit input; rendering cadence never advances generated-world state.
  [[nodiscard]] auto render(const LocalSystemDescriptor& system,
                            const LocalSystemView& view,
                            std::span<termforge::Pixel> destination)
      -> std::expected<LocalSystemRenderStats, LocalSystemRenderError>;

  // Draws presentation-only station geometry at its resolved system-space
  // location without feeding projection or terminal state back into simulation.
  [[nodiscard]] auto render_origin_station(
      const LocalSystemView& view, const OriginStationEphemeris& station,
      std::span<termforge::Pixel> destination)
      -> std::expected<void, LocalSystemRenderError>;

 private:
  LocalSystemRenderSettings m_settings;
  OrbitalRenderer m_orbital_renderer;
  std::vector<termforge::Pixel> m_system_frame;
  std::vector<termforge::Pixel> m_orbital_frame;
};

} // namespace apsis_drift
