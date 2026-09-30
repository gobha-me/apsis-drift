#include "apsis_drift/native_startup.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
int failures{};
auto check(bool ok, std::string_view message) -> void {
  if (!ok) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
template <class T, class E> auto required(std::expected<T, E> x) -> T {
  if (!x)
    throw std::runtime_error("required Freedom flight save fixture failed");
  return *x;
}
auto contents(const std::filesystem::path& path) -> std::string {
  std::ifstream in{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{in}, {}};
}
auto put(const std::filesystem::path& path, std::string_view bytes) -> void {
  std::ofstream out{path, std::ios::binary};
  out << bytes;
}
auto scalars(RigidBodyState& s) -> std::array<double*, 13> {
  return {&s.position_metres.x,
          &s.position_metres.y,
          &s.position_metres.z,
          &s.orientation.w,
          &s.orientation.x,
          &s.orientation.y,
          &s.orientation.z,
          &s.linear_velocity_metres_per_second.x,
          &s.linear_velocity_metres_per_second.y,
          &s.linear_velocity_metres_per_second.z,
          &s.angular_velocity_radians_per_second.x,
          &s.angular_velocity_radians_per_second.y,
          &s.angular_velocity_radians_per_second.z};
}
auto bits(RigidBodyState a, RigidBodyState b) -> bool {
  auto x = scalars(a), y = scalars(b);
  for (std::size_t n = 0; n < x.size(); ++n)
    if (std::bit_cast<std::uint64_t>(*x[n]) !=
        std::bit_cast<std::uint64_t>(*y[n]))
      return false;
  return a.tick == b.tick && a.frame == b.frame && a.craft == b.craft;
}
auto document(Seed seed, double altitude = 1000) -> FreedomFlightSaveDocument {
  auto origin = make_freedom_new_game_document(seed);
  origin.state.tick = 25;
  origin.state.discoveries.push_back({{77}, 8});
  origin.state.world_deltas.push_back(
      {"signal:77", SaveWorldDeltaKind::discovered, 9});
  auto system = required(generate_physical_origin_system(seed));
  const auto& planet =
      system.catalog.planets[kOriginHomePlanetOrdinal].descriptor;
  RigidBodyState state;
  state.tick = 25;
  state.frame = {RigidFrameKind::planet_relative_inertial,
                 system.catalog.id,
                 planet.id,
                 {}};
  state.position_metres = {planet.radius.value * 1000.0 + altitude, 0, 0};
  state.linear_velocity_metres_per_second = {0, 0, -100};
  return {origin, state, {}};
}
auto invalid() -> void {
  const auto valid = document(Seed{42});
  const auto text = required(encode_freedom_flight_document_json(valid));
  const auto root = Json::parse(text);
  const auto refuses = [&](const Json& value) {
    check(!decode_freedom_flight_document_json(value.dump()),
          "malformed format18 refuses without fallback");
  };
  for (auto key : {"application", "application_version", "format_version",
                   "mode", "recipe", "state", "flight", "flight_model"}) {
    auto bad = root;
    bad.erase(key);
    refuses(bad);
  }
  auto bad = root;
  bad["extra"] = true;
  refuses(bad);
  bad = root;
  bad["mode"] = "career";
  refuses(bad);
  bad = root;
  bad["state"]["location"] = "docked_at_origin";
  refuses(bad);
  bad = root;
  bad["state"]["tick"] = "24";
  refuses(bad);
  bad = root;
  bad["recipe"]["universe_seed"] = "43";
  refuses(bad);
  bad = root;
  bad["state"]["starter_craft_id"] = "0";
  refuses(bad);
  for (auto field : {"physical_catalog", "physical_ephemeris", "rotation_owner",
                     "rotation_generator", "central_body", "atmosphere"})
    for (const auto& value :
         {Json(99), Json(-1), Json(1.0), Json("1"), Json(nullptr)}) {
      bad = root;
      bad["flight_model"][field] = value;
      refuses(bad);
    }
  bad = root;
  bad["flight_model"]["orbit_hold"]["version"] = 99;
  refuses(bad);
  bad = root;
  bad["flight_model"]["assistance"] = 1;
  refuses(bad);
  bad = root;
  bad["flight_model"]["orbit_hold"]["target"] = {
      {"planet_id", "0"}, {"radius_metres", 1e7}, {"plane_normal", {0, 0, 1}}};
  refuses(bad);
  auto hold = valid;
  hold.model.hold.target =
      OrbitHoldTarget{valid.origin.recipe.home_planet,
                      valid.flight.position_metres.x + 500000,
                      {0, 0, 1}};
  const auto held =
      Json::parse(required(encode_freedom_flight_document_json(hold)));
  for (auto radius : {-1., 0., 1e30}) {
    bad = held;
    bad["flight_model"]["orbit_hold"]["target"]["radius_metres"] = radius;
    refuses(bad);
  }
  for (const auto& normal :
       {Json{0, 0, 2}, Json{0, 0}, Json{0, 0, -0.0}, Json{"x", 0, 1}}) {
    bad = held;
    bad["flight_model"]["orbit_hold"]["target"]["plane_normal"] = normal;
    refuses(bad);
  }
  for (std::string_view key : {"format_version", "target", "planet_id"}) {
    const std::string marker = '"' + std::string{key} + "\":";
    const auto& source = key == "format_version" ? text : held.dump();
    const auto where = source.find(marker);
    check(where != std::string::npos, "duplicate-key fixture found field");
    if (where != std::string::npos) {
      auto duplicate = source;
      duplicate.insert(where,
                       marker + (key == "planet_id" ? "\"0\"," : "null,"));
      check(!decode_freedom_flight_document_json(duplicate),
            "duplicate outer/nested fields refuse");
    }
  }
  check(!decode_freedom_flight_document_json("{broken") &&
            !decode_freedom_flight_document_json(
                std::string(kMaximumSaveDocumentBytes + 1, 'x')),
        "malformed and oversized documents refuse");
  for (auto name : {"position_metres", "orientation_wxyz",
                    "linear_velocity_metres_per_second",
                    "angular_velocity_radians_per_second"}) {
    const auto count = root["flight"][name].size();
    for (std::size_t n = 0; n < count; ++n) {
      for (const auto& value : {"nan", "inf", "1e999"}) {
        bad = root;
        bad["flight"][name][n] = value;
        refuses(bad);
      }
    }
  }
  for (auto field : {"version", "generator", "source_catalog_generator",
                     "ephemeris_version"}) {
    bad = root;
    bad["flight"]["owner"][field] = 99;
    refuses(bad);
  }
  bad = root;
  bad["flight"]["owner"]["origin_universe_seed"] = "43";
  refuses(bad);
  bad = root;
  bad["flight"]["owner"]["catalog_kind"] = "procedural";
  refuses(bad);
  bad = root;
  bad["flight"]["owner"]["family"] = "legacy";
  refuses(bad);
  bad = root;
  bad["flight"]["tick"] = "24";
  refuses(bad);
  bad = root;
  bad["flight"]["version"] = 2;
  refuses(bad);
  bad = root;
  bad["flight"]["frame"]["kind"] = "system_inertial";
  refuses(bad);
  bad = root;
  bad["flight"]["frame"]["planet"] = "0";
  refuses(bad);
  bad = root;
  bad["flight"]["craft"]["version"] = 99;
  refuses(bad);
  bad = root;
  bad["flight"]["position_metres"][0] = "1000000000000001";
  refuses(bad);
  bad = root;
  bad["flight"]["orientation_wxyz"][1] = "-0";
  refuses(bad);
  for (std::size_t n = 0; n < 13; ++n)
    for (double value : {std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity()}) {
      auto d = valid;
      *scalars(d.flight)[n] = value;
      check(!encode_freedom_flight_document_json(d),
            "non-finite state cannot enter format18");
    }
  auto d = valid;
  d.flight.frame.kind = RigidFrameKind::system_inertial;
  check(!hydrate_freedom_flight_document(d),
        "non-home flight domain refuses rather than inferring a body");
  d = valid;
  d.flight.tick = std::numeric_limits<SimulationTick>::max() - 1;
  d.origin.state.tick = d.flight.tick;
  check(!hydrate_freedom_flight_document(d),
        "non-advancing flight clock refuses");
  d = valid;
  d.flight.orientation = {1, 0, -0., 0};
  check(!encode_freedom_flight_document_json(d),
        "noncanonical signed zero is not sanitized on save");
  d = valid;
  d.model.hold.target =
      OrbitHoldTarget{valid.origin.recipe.home_planet, 1e7, {0, -0., 1}};
  check(!encode_freedom_flight_document_json(d),
        "noncanonical persisted hold target refuses");
  const auto station =
      required(encode_freedom_save_document_json(valid.origin));
  check(!decode_freedom_flight_document_json(station) &&
            !decode_freedom_save_document_json(text),
        "format17 and format18 are distinct, never relabeled by codecs");
}
auto continuation(Seed seed, unsigned mode,
                  const std::filesystem::path& directory) -> void {
  auto a = document(seed, mode == 0 ? 1000 : 500000), b = a;
  auto world = required(hydrate_freedom_flight_document(a));
  RigidBodyWorldContext context{world.system};
  if (mode == 2) {
    const auto g = required(evaluate_central_body_gravity(context, a.flight));
    const double radius = a.flight.position_metres.x;
    a.flight.linear_velocity_metres_per_second = {
        0,
        std::sqrt(g.gravitational_parameter_metres_cubed_per_second_squared /
                  radius),
        0};
    a.model.hold.target =
        OrbitHoldTarget{a.origin.recipe.home_planet, radius, {0, 0, 1}};
    a.flight.position_metres.x += 1000;
    b = a;
  }
  for (unsigned n = 0; n < 360; ++n) {
    VacuumIntent intent;
    intent.assistance = (n / 97) % 2 == 0;
    if (n > 100 && n < 150) intent.negative_translation.z = .125;
    if (n > 210) intent.positive_rotation.y = .1;
    a.model.assistance = intent.assistance;
    b.model.assistance = intent.assistance;
    if (n == 35 || n == 160 || n == 300) {
      const auto path = directory / (std::to_string(seed.value) + "-" +
                                     std::to_string(mode) + ".json");
      check(write_freedom_flight_file_atomically(path, b).has_value(),
            "atomic flight save succeeds");
      const auto bytes = contents(path);
      auto selected = required(native_continue(path));
      check(selected.mode == NativeStartup::Mode::freedom &&
                selected.source_save == path &&
                std::holds_alternative<FreedomFlightSaveDocument>(
                    selected.document),
            "native C++ Continue selects saved flight rather than docked/study "
            "state");
      b = std::get<FreedomFlightSaveDocument>(selected.document);
      check(b == a && bits(b.flight, a.flight) && contents(path) == bytes,
            "full identity/history/model and exact state retained by file "
            "Continue");
      check(!prepare_native_freedom_station_start(selected),
            "unsupported docked consumer explicitly refuses genuine saved "
            "flight");
      world = required(hydrate_freedom_flight_document(b));
    }
    if (mode == 2) {
      const auto edge = world.atmosphere.space_boundary_altitude_metres;
      auto x = required(advance_orbit_hold_dynamics(
          context, a.flight, intent, world.rotation, {1, edge}, a.model.hold,
          a.model.central));
      auto y = required(advance_orbit_hold_dynamics(
          context, b.flight, intent, world.rotation, {1, edge}, b.model.hold,
          b.model.central));
      check(x.status == y.status &&
                x.observation_after == y.observation_after &&
                x.actuation.propulsion.world_linear_impulse_newton_seconds ==
                    y.actuation.propulsion.world_linear_impulse_newton_seconds,
            "saved hold target, mode and body continue exact force/observation "
            "trace");
    } else if (mode == 1) {
      auto x = required(advance_central_body_dynamics(context, a.flight, intent,
                                                      a.model.central));
      auto y = required(advance_central_body_dynamics(context, b.flight, intent,
                                                      b.model.central));
      check(x.gravity_impulse_newton_seconds ==
                    y.gravity_impulse_newton_seconds &&
                x.propulsion.world_linear_impulse_newton_seconds ==
                    y.propulsion.world_linear_impulse_newton_seconds,
            "saved central provider continues exact impulses");
    } else {
      auto x = required(
          advance_atmospheric_flight(context, a.flight, intent, world.rotation,
                                     a.model.atmosphere, a.model.central));
      auto y = required(
          advance_atmospheric_flight(context, b.flight, intent, world.rotation,
                                     b.model.atmosphere, b.model.central));
      check(x.after == y.after && x.observation_after == y.observation_after &&
                x.aerodynamic_linear_impulse_newton_seconds ==
                    y.aerodynamic_linear_impulse_newton_seconds,
            "saved atmosphere/rotation versions continue exact environmental "
            "trace");
    }
    a.origin.state.tick = a.flight.tick;
    b.origin.state.tick = b.flight.tick;
    check(a == b && bits(a.flight, b.flight),
          "interrupted full format18 state matches uninterrupted trace");
  }
  std::cout << "flight-save " << seed.value << " mode " << mode << " checksum "
            << required(rigid_body_state_checksum(context, a.flight)) << '\n';
}
auto files(const std::filesystem::path& directory) -> void {
  const auto path = directory / "existing.json";
  auto d = document(Seed{42});
  check(write_freedom_flight_file_atomically(path, d).has_value(),
        "initial flight file writes");
  const auto original = contents(path);
  d.model.atmosphere.version = 99;
  check(!write_freedom_flight_file_atomically(path, d) &&
            contents(path) == original,
        "refused flight save preserves existing destination bytes");
  const auto size =
      std::distance(std::filesystem::directory_iterator(directory),
                    std::filesystem::directory_iterator{});
  check(size == 1, "refusal left no temporary file");
  check(!native_continue(directory / "missing.json"),
        "missing flight file does not fall back");
  put(directory / "bad.json", "{broken");
  check(!native_continue(directory / "bad.json") &&
            contents(directory / "bad.json") == "{broken",
        "corrupt flight source is refused without rewriting");
  const auto station = make_freedom_new_game_document(Seed{42});
  const auto station_path = directory / "station17.json";
  check(write_freedom_save_file_atomically(station_path, station).has_value() &&
            std::holds_alternative<FreedomSaveDocument>(
                required(load_native_save_file(station_path))),
        "existing format17 still loads as docked Freedom");
  const auto legacy_path = directory / "career16.json";
  check(
      write_save_file_atomically(legacy_path, make_new_game_document(Seed{42}))
              .has_value() &&
          std::holds_alternative<SaveDocument>(
              required(load_native_save_file(legacy_path))),
      "existing format16 still loads as career, unchanged");
  check(!write_freedom_flight_file_atomically({}, document(Seed{42})) &&
            !write_freedom_flight_file_atomically(
                directory / "missing" / "x.json", document(Seed{42})),
        "invalid/missing destination paths refuse");
}
} // namespace
int main() {
  try {
    invalid();
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("apsis-flight-save-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    files(directory);
    for (Seed seed :
         {Seed{0}, Seed{42}, Seed{std::numeric_limits<std::uint64_t>::max()}})
      for (unsigned mode = 0; mode < 3; ++mode)
        continuation(seed, mode, directory);
    std::filesystem::remove_all(directory);
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 2;
  }
  return failures ? 1 : 0;
}
