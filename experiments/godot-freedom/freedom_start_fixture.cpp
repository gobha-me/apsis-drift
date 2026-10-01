#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>

#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/save_file.hpp"

namespace {

[[nodiscard]] auto decimal(std::string_view text)
    -> std::optional<std::uint64_t> {
  if (text.empty() || text.size() > 20 ||
      (text.size() > 1 && text.front() == '0'))
    return std::nullopt;
  std::uint64_t value{};
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    return std::nullopt;
  return value;
}

} // namespace

auto main(int argc, char** argv) -> int {
  using namespace apsis_drift;
  if (argc != 5) {
    std::cerr << "usage: freedom-start-fixture ABSOLUTE_PATH SEED TICK "
                 "freedom|flight|wayfarer-flight|flight-trace|career\n";
    return 2;
  }
  const auto path = std::filesystem::path{argv[1]};
  const auto seed = decimal(argv[2]);
  const auto tick = decimal(argv[3]);
  const std::string_view mode{argv[4]};
  if (!path.is_absolute() || !seed || !tick ||
      *tick == std::numeric_limits<SimulationTick>::max()) {
    std::cerr << "invalid fixture path, seed or tick\n";
    return 2;
  }
  if (mode == "freedom") {
    auto save = make_freedom_new_game_document(Seed{*seed});
    save.state.tick = *tick;
    if (*tick >= 9) {
      save.state.discoveries.push_back({SurfaceSignalId{77}, 8});
      save.state.world_deltas.push_back(
          {"signal:77", SaveWorldDeltaKind::discovered, 9});
    }
    const auto written = write_freedom_save_file_atomically(path, save);
    if (!written) {
      std::cerr << save_file_error_message(written.error()) << '\n';
      return 1;
    }
  } else if (mode == "flight" || mode == "wayfarer-flight" ||
             mode == "flight-trace") {
    auto origin = make_freedom_new_game_document(Seed{*seed});
    origin.state.tick = *tick;
    const auto system = generate_physical_origin_system(Seed{*seed});
    if (!system) return 1;
    const auto& planet =
        system->catalog.planets[kOriginHomePlanetOrdinal].descriptor;
    RigidBodyState state;
    if (mode != "flight") state.craft = wayfarer_frame().recipe;
    state.tick = *tick;
    state.frame = {RigidFrameKind::planet_relative_inertial,
                   system->catalog.id,
                   planet.id,
                   {}};
    state.position_metres = {planet.radius.value * 1000.0 + 500000, 0, 0};
    const auto gravity =
        evaluate_central_body_gravity(RigidBodyWorldContext{*system}, state);
    if (!gravity) return 1;
    state.linear_velocity_metres_per_second = {
        0,
        std::sqrt(
            gravity->gravitational_parameter_metres_cubed_per_second_squared /
            state.position_metres.x),
        0};
    FreedomFlightSaveDocument document{origin, state, {}};
    if (mode == "flight-trace") {
      auto session = NativeFreedomFlightSession::open(
          {NativeStartup::Mode::freedom, document, planet, {}});
      if (!session) return 1;
      NativeFlightControls controls;
      controls.negative_translation.y = .1;
      controls.negative_translation.z = .25;
      controls.positive_rotation.y = .05;
      for (int n = 0; n < 120; ++n) {
        if (n == 60) session->set_assistance(false);
        if (!session->advance(controls)) return 1;
      }
      document = session->document();
    }
    const auto written = write_freedom_flight_file_atomically(path, document);
    if (!written) {
      std::cerr << save_file_error_message(written.error()) << '\n';
      return 1;
    }
  } else if (mode == "port-approach" || mode == "port-docked" ||
             mode == "port-trace" || mode == "port-far") {
    auto origin = make_freedom_new_game_document(Seed{*seed});
    origin.state.tick = *tick;
    const auto system = generate_physical_origin_system(Seed{*seed});
    const auto station = generate_origin_station(Seed{*seed});
    const auto geometry = origin_station_geometry(station);
    if (!system || !geometry) return 1;
    auto pose =
        resolve_origin_port_pose(*system, station, *geometry, {station.id, 1},
                                 *tick, mode == "port-far" ? 12.0 : .1);
    if (!pose) return 1;
    auto body = pose->planet_relative;
    body.craft = wayfarer_frame().recipe;
    const auto& planet =
        system->catalog.planets[kOriginHomePlanetOrdinal].descriptor;
    FreedomDockingSaveDocument document{{origin, body, {}},
                                        {1, {station.id, 1}, false}};
    auto session = NativeFreedomFlightSession::open(
        {NativeStartup::Mode::freedom, document, planet, {}});
    if (!session) return 1;
    if (mode == "port-docked" || mode == "port-trace") {
      if (!session->capture_port()) return 1;
    }
    if (mode == "port-trace") {
      for (int n = 0; n < 120; ++n)
        if (!session->advance({})) return 1;
      if (!session->release_port()) return 1;
      NativeFlightControls controls;
      controls.negative_translation.y = .25;
      controls.negative_translation.z = .1;
      for (int n = 0; n < 120; ++n)
        if (!session->advance(controls)) return 1;
    }
    if (!session->save_as(path)) return 1;
  } else if (mode == "career" && *tick == 0) {
    const auto written =
        write_save_file_atomically(path, make_new_game_document(Seed{*seed}));
    if (!written) {
      std::cerr << save_file_error_message(written.error()) << '\n';
      return 1;
    }
  } else {
    std::cerr << "invalid fixture mode or career tick\n";
    return 2;
  }
  return 0;
}
