#pragma once

#include <cstdint>
#include <optional>

#include "termforge/core/types.hpp"

namespace apsis_drift {

enum class SessionScreen : std::uint8_t {
  title,
  station,
  flight,
  paused,
  exit_requested,
};

enum class MenuItem : std::uint8_t { primary, exit };

enum class MenuCommand : std::uint8_t {
  previous,
  next,
  activate,
  escape,
};

struct SessionTransition {
  SessionScreen from{SessionScreen::title};
  SessionScreen to{SessionScreen::title};

  [[nodiscard]] constexpr auto changed() const noexcept -> bool {
    return from != to;
  }
};

class SessionController {
 public:
  explicit SessionController(bool start_in_flight = false,
                             bool docked_profile = false) noexcept;

  [[nodiscard]] auto screen() const noexcept -> SessionScreen {
    return m_screen;
  }
  [[nodiscard]] auto selected() const noexcept -> MenuItem {
    return m_selected;
  }
  [[nodiscard]] auto menu_visible() const noexcept -> bool {
    return m_screen == SessionScreen::title ||
           m_screen == SessionScreen::station ||
           m_screen == SessionScreen::paused;
  }

  [[nodiscard]] auto start_flight() noexcept -> SessionTransition;
  [[nodiscard]] auto dock_at_station() noexcept -> SessionTransition;

  auto select(MenuItem item) noexcept -> void;
  [[nodiscard]] auto dispatch(MenuCommand command) noexcept
      -> SessionTransition;

 private:
  SessionScreen m_screen{SessionScreen::title};
  SessionScreen m_title_destination{SessionScreen::flight};
  MenuItem m_selected{MenuItem::primary};
};

struct MenuLayout {
  termforge::Rect screen{};
  termforge::Rect art{};
  termforge::Rect panel{};
  termforge::Rect heading{};
  termforge::Rect primary_action{};
  termforge::Rect exit_action{};
  termforge::Rect hint{};

  [[nodiscard]] constexpr auto supported() const noexcept -> bool {
    return !panel.empty() && !primary_action.empty() &&
           !exit_action.empty();
  }

  constexpr auto operator==(const MenuLayout&) const noexcept
      -> bool = default;
};

[[nodiscard]] auto compute_menu_layout(int cols, int rows) noexcept
    -> MenuLayout;

[[nodiscard]] auto menu_item_at(const MenuLayout& layout, int x,
                                int y) noexcept
    -> std::optional<MenuItem>;

}  // namespace apsis_drift
