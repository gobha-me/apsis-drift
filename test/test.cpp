#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/benchmark.hpp"
#include "apsis_drift/landscape.hpp"
#include "apsis_drift/simulation.hpp"
#include "flight_input.hpp"

namespace {

using namespace apsis_drift;
using termforge::Pixel;

int failures{};

auto check(bool condition, const char* message) -> void {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
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

  FlightInputMapper enhanced;
  enhanced.set_enhanced_keyboard(true);
  for (const auto& mapping : mappings) {
    check(enhanced.enqueue(key_event(mapping.key, mapping.ch,
                                     KeyAction::Press),
                           7)
              .has_value(),
          "enhanced key press must enqueue");
    check(enhanced.enqueue(key_event(mapping.key, mapping.ch,
                                     KeyAction::Release),
                           7)
              .has_value(),
          "enhanced key release must enqueue");
  }
  check(enhanced.enqueue(key_event(Key::Char, U'w', KeyAction::Repeat), 7)
            .has_value(),
        "enhanced key repeat must enqueue");
  check(enhanced.enqueue(key_event(Key::Char, U' ', KeyAction::Press), 7)
            .has_value(),
        "Space press must enqueue");
  check(enhanced.enqueue(key_event(Key::Char, U' ', KeyAction::Repeat), 7)
            .has_value(),
        "Space repeat must be ignored safely");
  check(enhanced.enqueue(key_event(Key::Char, U'x', KeyAction::Press), 7)
            .has_value(),
        "an unrelated key must be ignored safely");
  const auto enhanced_commands = enhanced.take_commands(7);
  check(enhanced_commands.size() == mappings.size() * 2 + 2,
        "enhanced mapping must emit press, release, repeat, and one toggle");
  if (enhanced_commands.size() == mappings.size() * 2 + 2) {
    for (std::size_t index = 0; index < mappings.size(); ++index) {
      check(enhanced_commands[index * 2].kind == mappings[index].press &&
                enhanced_commands[index * 2 + 1].kind ==
                    mappings[index].release,
            "each enhanced control must map to its command pair");
    }
    check(enhanced_commands[enhanced_commands.size() - 2].kind ==
              FlightCommandKind::press_forward,
          "an enhanced repeat must remain an idempotent press");
    check(enhanced_commands.back().kind ==
              FlightCommandKind::toggle_autopilot,
          "Space must map to one autopilot toggle");
  }

  FlightInputMapper fallback;
  fallback.set_enhanced_keyboard(false);
  check(fallback.enqueue(key_event(Key::Char, U'w', KeyAction::Press), 10)
            .has_value(),
        "fallback press must enqueue a pulse");
  check(fallback.take_commands(10) ==
            std::vector{FlightCommand{10,
                                      FlightCommandKind::press_forward}},
        "fallback pulse must begin at the current tick");
  check(fallback.enqueue(key_event(Key::Char, U'w', KeyAction::Press), 15)
            .has_value(),
        "a repeated fallback press must extend the pulse");
  check(fallback.take_commands(20).empty(),
        "an extended fallback pulse must remove its earlier release");
  check(fallback.take_commands(25) ==
            std::vector{FlightCommand{25,
                                      FlightCommandKind::release_forward}},
        "an extended fallback pulse must release after ten ticks");
  check(fallback.enqueue(key_event(Key::Char, U'w', KeyAction::Release), 30)
            .has_value() &&
            fallback.take_commands(30).empty(),
        "fallback release events must not create extra commands");
  check(!fallback.enqueue(
             key_event(Key::Char, U'w', KeyAction::Press),
             std::numeric_limits<SimulationTick>::max())
              .has_value(),
        "a fallback pulse must reject a release-tick overflow");
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
            camera->yaw == state.pose.yaw,
        "the render camera must derive directly from authoritative pose");

  const auto checksum = flight_state_checksum(state);
  if (camera) {
    auto presentation = *camera;
    presentation.horizon += 20.0F;
    check(presentation.horizon != camera->horizon,
          "camera horizon must remain independently adjustable");
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
                          .vertical_scale = 96.0F,
                          .fog_start = 140.0F}};
  Camera camera;
  camera.horizon = 52.0F;
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
}

auto deterministic_render() -> void {
  const auto terrain = Terrain::generate(256, 0xC0FFEEU);
  check(terrain.has_value(), "render terrain must generate");
  if (!terrain) return;

  RenderSettings settings{.width = 160,
                          .height = 120,
                          .field_of_view_degrees = 72.0F,
                          .max_distance = 420.0F,
                          .vertical_scale = 112.0F,
                          .fog_start = 180.0F};
  VoxelRenderer renderer{settings};
  Camera camera;
  camera.horizon = 52.0F;
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
    settings.vertical_scale =
        255.0F * static_cast<float>(size.height) /
        static_cast<float>(kFrameHeight);
    VoxelRenderer renderer{settings};
    Camera camera;
    camera.horizon =
        205.0F * static_cast<float>(size.height) /
        static_cast<float>(kFrameHeight);
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
  sweep_selection_contract();
  sweep_report_contract();
  fixed_step_clock_contract();
  deterministic_fixed_step_flight();
  deterministic_command_replay();
  command_edge_contract();
  flight_input_mapping_contract();
  camera_derivation_contract();
  render_failure_matrix();
  deterministic_render();
  required_viewport_matrix();
  if (failures != 0) {
    std::fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
  }
  std::puts("all landscape tests passed");
  return 0;
}
