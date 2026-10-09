#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
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

// Independent analytic saved configurations, with exact C++ observation
// oracles for the bridge. These do not replace any gameplay starting state.
auto orbital_readout(const std::filesystem::path& path, std::uint64_t seed,
                     std::uint64_t tick) -> bool {
  using namespace apsis_drift;
  const auto system = generate_physical_origin_system(Seed{seed});
  if (!system) return false;
  const auto& planet =
      system->catalog.planets[kOriginHomePlanetOrdinal].descriptor;
  const double radius = planet.radius.value * 1000.0;
  const double r = radius + 500000;
  RigidBodyState initial;
  initial.craft = wayfarer_frame().recipe;
  initial.frame = {RigidFrameKind::planet_relative_inertial,
                   system->catalog.id,
                   planet.id,
                   {}};
  initial.tick = tick;
  initial.position_metres = {r, 0, 0};
  const auto gravity =
      evaluate_central_body_gravity(RigidBodyWorldContext{*system}, initial);
  if (!gravity) return false;
  const double mu =
      gravity->gravitational_parameter_metres_cubed_per_second_squared;
  auto origin = make_freedom_new_game_document(Seed{seed});
  origin.state.tick = tick;
  const auto reference = NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom,
       FreedomFlightSaveDocument{origin, initial, {}},
       planet,
       {}});
  if (!reference) return false;
  const auto air = reference->observe();
  if (!air || air->atmosphere.space_boundary_altitude_metres <= 0) return false;
  std::ofstream output(path);
  if (!output) return false;
  output << std::setprecision(17) << "[\n";
  constexpr std::array names{"stable",      "decaying", "impact",
                             "inbound",     "outbound", "near-parabolic",
                             "bound-no-apo"};
  for (std::size_t i = 0; i < names.size(); ++i) {
    auto state = initial;
    auto expected = OrbitClassification::stable;
    const std::string_view name{names[i]};
    if (name == "decaying" || name == "impact") {
      const double peri =
          name == "impact"
              ? radius - 1000
              : radius + air->atmosphere.space_boundary_altitude_metres * .5;
      const double apo = radius + 1000000;
      state.position_metres.x = apo;
      state.linear_velocity_metres_per_second.z =
          std::sqrt(mu * (2 / apo - 2 / (peri + apo)));
      expected = name == "impact" ? OrbitClassification::impact
                                  : OrbitClassification::decaying;
    } else if (name == "inbound" || name == "outbound") {
      state.linear_velocity_metres_per_second.x =
          (name == "inbound" ? -1 : 1) * std::sqrt(3 * mu / r);
      expected = name == "inbound" ? OrbitClassification::impact
                                   : OrbitClassification::escape;
    } else if (name == "near-parabolic") {
      state.linear_velocity_metres_per_second.z = std::sqrt(2 * mu / r);
      expected = OrbitClassification::escape;
    } else if (name == "bound-no-apo") {
      const double energy = -mu / (8 * kMaximumOrbitalElementRadiusMetres);
      state.linear_velocity_metres_per_second.z =
          std::sqrt(2 * (mu / r + energy));
    } else {
      state.linear_velocity_metres_per_second.z = std::sqrt(mu / r);
    }
    const FreedomFlightSaveDocument document{origin, state, {}};
    const auto session = NativeFreedomFlightSession::open(
        {NativeStartup::Mode::freedom, document, planet, {}});
    if (!session) return false;
    const auto observed = session->observe();
    if (!observed || observed->orbit.classification != expected ||
        observed->orbit.bound != (name == "stable" || name == "decaying" ||
                                  name == "impact" || name == "bound-no-apo") ||
        observed->orbit.near_parabolic != (name == "near-parabolic") ||
        (name == "bound-no-apo" && observed->orbit.apoapsis_radius_metres))
      return false;
    const auto save =
        path.parent_path() / ("orbit-" + std::string{name} + ".json");
    if (!write_freedom_flight_file_atomically(save, document)) return false;
    const std::string_view classification =
        expected == OrbitClassification::stable     ? "stable"
        : expected == OrbitClassification::decaying ? "decaying"
        : expected == OrbitClassification::impact   ? "impact"
                                                    : "escape";
    const auto& o = observed->orbit;
    output << (i == 0 ? "" : ",\n") << "{\"file\":\""
           << save.filename().string() << "\",\"classification\":\""
           << classification << "\",\"bound\":" << (o.bound ? "true" : "false")
           << ",\"near_parabolic\":" << (o.near_parabolic ? "true" : "false")
           << ",\"planet_radius\":" << radius
           << ",\"periapsis_radius\":" << o.periapsis_radius_metres
           << ",\"apoapsis_radius\":";
    if (o.apoapsis_radius_metres)
      output << *o.apoapsis_radius_metres;
    else
      output << "null";
    output << '}';
  }
  output << "\n]\n";
  output.close();
  return !output.fail();
}

} // namespace

namespace {
auto travel_trace(const std::filesystem::path& path, apsis_drift::Seed seed,
                  apsis_drift::SimulationTick tick) -> bool {
  using namespace apsis_drift;
  auto start = native_new_game(seed);
  if (!start) return false;
  auto boarding = NativeFreedomFlightSession::open(std::move(*start));
  if (!boarding) return false;
  for (unsigned t = 0; t < 2960; ++t)
    if (!boarding->advance_walk({0, -1, 0})) return false;
  if (!boarding->begin_boarding()) return false;
  for (unsigned t = 0; t < kGameplayBoardingTicks; ++t)
    if (!boarding->advance_walk({})) return false;
  // This is still a saved space fixture, with actual completed pilot history.
  // Physical departure and return rendezvous belong to the composed trace.
  if (tick < boarding->document().flight.tick)
    tick = boarding->document().flight.tick;
  const auto system = generate_physical_origin_system(seed, 2);
  if (!system) return false;
  const auto& body = system->catalog.planets[0].descriptor;
  FreedomFlightSaveDocument flight;
  flight.origin = make_freedom_new_game_document(seed);
  flight.origin.state.tick = tick;
  flight.model.physical_catalog = 2;
  flight.model.physical_ephemeris = 2;
  flight.flight.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  flight.flight.frame = {RigidFrameKind::planet_relative_inertial,
                         system->catalog.id,
                         body.id,
                         {}};
  flight.flight.tick = tick;
  flight.flight.position_metres = {0, 0, body.radius.value * 10000.0};
  flight.world = FreedomActiveWorldSelection{1, system->catalog.id, body.id};
  auto knowledge = make_freedom_starting_knowledge(seed, 3);
  if (!knowledge) return false;
  FreedomResources resources{1, flight.origin.state.craft, flight.flight.craft,
                             tick};
  FreedomSurfaceSaveDocument surface{flight, {}};
  FreedomResourceSaveDocument fueled{std::move(surface), resources};
  FreedomKnowledgeSaveDocument mapped{std::move(fueled), std::move(*knowledge)};
  FreedomTravelSaveDocument document{std::move(mapped),
                                     {},
                                     NativeStartingAssemblySelection{},
                                     boarding->boarding()};
  auto session = NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom, document, body, {}});
  if (!session || !session->save_as(path)) return false;
  const auto ids = generate_first_intersystem_identities(seed);
  for (unsigned leg = 0; leg < 2; ++leg) {
    if (!session->select_jump(leg == 0 ? ids.target_system
                                       : ids.origin_system) ||
        !session->begin_jump())
      return false;
    const auto checkpoint = [&](SimulationTick offset) {
      return session
          ->save_as(path.string() + ".leg" + std::to_string(leg) + "." +
                    std::to_string(offset) + ".json")
          .has_value();
    };
    if (!checkpoint(0)) return false;
    for (SimulationTick t = 1; t <= kJumpSpoolTicks + kJumpTransitTicks; ++t) {
      if (!session->advance({})) return false;
      if ((t == 1 || t == kJumpSpoolTicks || t == kJumpSpoolTicks + 1 ||
           t == kJumpSpoolTicks + kJumpTransitTicks) &&
          !checkpoint(t))
        return false;
    }
  }
  return true;
}
} // namespace

auto main(int argc, char** argv) -> int {
  using namespace apsis_drift;
  if (argc != 5) {
    std::cerr << "usage: freedom-start-fixture ABSOLUTE_PATH SEED TICK "
                 "freedom|flight|wayfarer-flight|flight-trace|orbital-readout|"
                 "career\n";
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
  if (mode == "travel-trace") {
    return travel_trace(path, Seed{*seed}, *tick) ? 0 : 1;
  } else if (mode == "orbital-readout") {
    return orbital_readout(path, *seed, *tick) ? 0 : 1;
  } else if (mode == "freedom") {
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
        if (n == 60 && !session->set_assistance(false)) return 1;
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
  } else if (mode == "legacy-journey" && *tick == 0) {
    const auto document = make_freedom_journey_new_game_document(Seed{*seed});
    if (!document || !write_freedom_journey_file_atomically(path, *document))
      return 1;
  } else if ((mode == "journey" || mode == "journey-trace") && *tick == 0) {
    auto selected = native_new_game(Seed{*seed});
    if (!selected) {
      std::cerr << selected.error() << '\n';
      return 1;
    }
    auto session = NativeFreedomFlightSession::open(std::move(*selected));
    if (!session) {
      std::cerr << session.error() << '\n';
      return 1;
    }
    if (mode == "journey-trace") {
      for (int n = 0; n < 1920; ++n)
        if (auto walked = session->advance_walk({0, n < 960 ? -1.0 : 1.0, 0});
            !walked) {
          std::cerr << walked.error() << '\n';
          return 1;
        }
    }
    if (auto saved = session->save_as(path); !saved) {
      std::cerr << saved.error() << '\n';
      return 1;
    }
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
