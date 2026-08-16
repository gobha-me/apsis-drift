#include "apsis_drift/menu.hpp"

#include <algorithm>

namespace apsis_drift {
namespace {

inline constexpr int kMinimumMenuCols{32};
inline constexpr int kMinimumMenuRows{16};
inline constexpr int kMaximumTerminalAxis{65535};
inline constexpr int kPanelWidth{36};
inline constexpr int kPanelHeight{8};

}  // namespace

SessionController::SessionController(bool start_in_flight,
                                     bool docked_profile) noexcept
    : m_screen(start_in_flight ? SessionScreen::flight
                               : SessionScreen::title),
      m_title_destination(docked_profile ? SessionScreen::station
                                         : SessionScreen::flight) {}

auto SessionController::start_flight() noexcept -> SessionTransition {
  const auto before = m_screen;
  if (m_screen == SessionScreen::station) m_screen = SessionScreen::flight;
  return {before, m_screen};
}

auto SessionController::dock_at_station() noexcept -> SessionTransition {
  const auto before = m_screen;
  if (m_screen == SessionScreen::flight) {
    m_screen = SessionScreen::station;
    m_selected = MenuItem::primary;
  }
  return {before, m_screen};
}

auto SessionController::select(MenuItem item) noexcept -> void {
  if (menu_visible()) m_selected = item;
}

auto SessionController::dispatch(MenuCommand command) noexcept
    -> SessionTransition {
  const SessionScreen before = m_screen;
  if (m_screen == SessionScreen::exit_requested) return {before, m_screen};

  if (command == MenuCommand::escape) {
    if (m_screen == SessionScreen::flight) {
      m_screen = SessionScreen::paused;
      m_selected = MenuItem::primary;
    } else if (m_screen == SessionScreen::paused) {
      m_screen = SessionScreen::flight;
    }
    return {before, m_screen};
  }

  if (!menu_visible()) return {before, m_screen};
  if (command == MenuCommand::previous || command == MenuCommand::next) {
    m_selected = m_selected == MenuItem::primary ? MenuItem::exit
                                                 : MenuItem::primary;
  } else if (command == MenuCommand::activate) {
    if (m_selected == MenuItem::exit) {
      m_screen = SessionScreen::exit_requested;
    } else if (m_screen == SessionScreen::title) {
      m_screen = m_title_destination;
    } else if (m_screen == SessionScreen::paused) {
      m_screen = SessionScreen::flight;
    }
  }
  return {before, m_screen};
}

auto compute_menu_layout(int cols, int rows) noexcept -> MenuLayout {
  MenuLayout layout;
  if (cols <= 0 || rows <= 0 || cols > kMaximumTerminalAxis ||
      rows > kMaximumTerminalAxis) {
    return layout;
  }
  layout.screen = {0, 0, cols, rows};
  if (cols < kMinimumMenuCols || rows < kMinimumMenuRows) return layout;

  const int panel_width = std::min(kPanelWidth, cols - 4);
  const int panel_x = (cols - panel_width) / 2;
  const int panel_y = rows - kPanelHeight - 1;
  layout.panel = {panel_x, panel_y, panel_width, kPanelHeight};
  layout.heading = {panel_x + 2, panel_y + 1, panel_width - 4, 1};
  layout.primary_action = {panel_x + 2, panel_y + 3, panel_width - 4, 1};
  layout.exit_action = {panel_x + 2, panel_y + 5, panel_width - 4, 1};
  layout.hint = {panel_x + 2, panel_y + 7, panel_width - 4, 1};

  const int art_height = std::max(0, panel_y - 2);
  layout.art = {2, 1, cols - 4, art_height};
  return layout;
}

auto menu_item_at(const MenuLayout& layout, int x, int y) noexcept
    -> std::optional<MenuItem> {
  if (!layout.supported()) return std::nullopt;
  if (layout.primary_action.contains(x, y)) return MenuItem::primary;
  if (layout.exit_action.contains(x, y)) return MenuItem::exit;
  return std::nullopt;
}

}  // namespace apsis_drift
