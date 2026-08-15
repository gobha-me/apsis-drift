#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/benchmark.hpp"
#include "apsis_drift/cockpit.hpp"
#include "apsis_drift/landscape.hpp"
#include "apsis_drift/simulation.hpp"
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
  check(json.find("\"seed\": 42") != std::string::npos,
        "sweep JSON must identify its seed");
  check(json.find("\"frames_per_viewport\": 12") != std::string::npos,
        "sweep JSON must identify its frame count");
  check(json.find("\"checksum\": \"123456789\"") != std::string::npos,
        "sweep JSON must preserve checksums exactly as strings");
  check(json.find("\"target_fps\": 30") != std::string::npos &&
            json.find("\"target_fps\": 60") != std::string::npos,
        "sweep JSON must include every cadence target");

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

[[nodiscard]] auto replay_golden_commands(const Terrain& terrain)
    -> std::expected<FlightState, FlightError> {
  auto initialized = initial_flight_state(terrain);
  if (!initialized) return std::unexpected{initialized.error()};
  auto state = *initialized;
  constexpr std::array commands{
      FlightCommand{0, FlightCommandKind::toggle_autopilot},
      FlightCommand{0, FlightCommandKind::press_forward},
      FlightCommand{18, FlightCommandKind::press_turn_right},
      FlightCommand{36, FlightCommandKind::press_turn_left},
      FlightCommand{48, FlightCommandKind::press_strafe_right},
      FlightCommand{60, FlightCommandKind::release_turn_right},
      FlightCommand{72, FlightCommandKind::release_turn_left},
      FlightCommand{84, FlightCommandKind::press_rise},
      FlightCommand{96, FlightCommandKind::release_strafe_right},
      FlightCommand{108, FlightCommandKind::release_rise},
      FlightCommand{120, FlightCommandKind::press_backward},
      FlightCommand{132, FlightCommandKind::release_forward},
      FlightCommand{144, FlightCommandKind::press_strafe_left},
      FlightCommand{156, FlightCommandKind::press_fall},
      FlightCommand{168, FlightCommandKind::release_backward},
      FlightCommand{180, FlightCommandKind::release_strafe_left},
      FlightCommand{192, FlightCommandKind::release_fall},
      FlightCommand{204, FlightCommandKind::toggle_autopilot},
  };

  std::size_t next_command{};
  while (state.tick < 240) {
    const auto first = next_command;
    while (next_command < commands.size() &&
           commands[next_command].tick == state.tick) {
      ++next_command;
    }
    const std::span tick_commands{commands.data() + first,
                                  next_command - first};
    if (auto advanced =
            advance_flight(terrain, state, tick_commands, kSimulationStep);
        !advanced) {
      return std::unexpected{advanced.error()};
    }
  }
  return state;
}

auto deterministic_command_replay() -> void {
  const auto terrain = Terrain::generate(256, 0xC0FFEEU);
  check(terrain.has_value(), "command replay terrain must generate");
  if (!terrain) return;

  const auto first = replay_golden_commands(*terrain);
  const auto second = replay_golden_commands(*terrain);
  check(first && second, "the golden command stream must replay");
  if (!first || !second) return;

  const auto first_checksum = flight_state_checksum(*first);
  const auto second_checksum = flight_state_checksum(*second);
  constexpr std::uint64_t expected_checksum{209895004964188471ULL};
  if (first_checksum != expected_checksum) {
    std::fprintf(stderr, "golden command checksum: %llu\n",
                 static_cast<unsigned long long>(first_checksum));
  }
  check(first_checksum == second_checksum,
        "replaying a command stream must reproduce its final state");
  check(first_checksum == expected_checksum,
        "the golden command stream checksum must remain stable");
  check(first->tick == 240 && first->mode == FlightMode::autopilot,
        "the golden command stream must reach its expected tick and mode");
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

}  // namespace

auto main() -> int {
  generation_failure_matrix();
  deterministic_generation();
  render_profile_contract();
  viewport_validation_contract();
  cockpit_layout_contract();
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
  if (failures != 0) {
    std::fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
  }
  std::puts("all landscape tests passed");
  return 0;
}
