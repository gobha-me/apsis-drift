#include "apsis_drift/godot/saved_flight.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "apsis_drift/universe_navigation.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <numbers>
#include <source_location>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
using V = RigidVector3;
int checks{}, failures{};
auto check(bool value, std::string_view label) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
template <class T, class E>
auto required(std::expected<T, E> value,
              std::source_location location = std::source_location::current())
    -> T {
  if (!value) {
    std::string message =
        "fixture failure at line " + std::to_string(location.line());
    if constexpr (std::is_same_v<E, std::string>)
      message += " " + value.error();
    else if constexpr (requires { value.error().detail; })
      message += " " + value.error().detail;
    throw std::runtime_error(message);
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto read(const std::filesystem::path& path) -> std::string {
  std::ifstream file{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{file}, {}};
}
auto write(const std::filesystem::path& path, std::string_view bytes) -> void {
  std::ofstream file{path, std::ios::binary};
  file << bytes;
  if (!file) throw std::runtime_error("fixture output failed");
}
auto same_scalar(double a, double b) -> bool {
  return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}
auto same_vector(V a, V b) -> bool {
  return same_scalar(a.x, b.x) && same_scalar(a.y, b.y) &&
         same_scalar(a.z, b.z);
}
auto close(double actual, double expected, double offset) -> bool {
  return std::abs(actual - expected) <=
         4 * std::numeric_limits<double>::epsilon() *
             std::max({1.0, std::abs(expected), std::abs(offset)});
}
struct Fixture {
  Seed seed;
  PhysicalLocalSystem owner;
  PlanetDescriptor planet;
  RigidBodyWorldContext context;
  FreedomFlightSaveDocument canonical;
  RigidBodyState source;
  FreedomFlightSaveDocument oracle;
  Json input;
  Fixture(Seed selected_seed = Seed{42}, bool neighbor = false,
          std::uint32_t policy = 2)
      : seed(selected_seed),
        owner(required(neighbor
                           ? generate_physical_local_system(
                                 generate_first_intersystem_identities(seed)
                                     .target_system_seed,
                                 2)
                           : generate_physical_origin_system(seed, 2))),
        planet(owner.catalog.planets[neighbor ? 0 : kOriginHomePlanetOrdinal]
                   .descriptor),
        context(owner) {
    canonical.origin = make_freedom_new_game_document(seed);
    canonical.origin.state.tick = 25;
    canonical.origin.state.discoveries.push_back({{77}, 8});
    canonical.origin.state.world_deltas.push_back(
        {"signal:77", SaveWorldDeltaKind::discovered, 9});
    canonical.model.physical_catalog = 2;
    canonical.model.physical_ephemeris = 2;
    canonical.model.terrain_policy = policy;
    canonical.model.assistance = false;
    canonical.world =
        FreedomActiveWorldSelection{1, owner.catalog.id, planet.id};
    auto& body = canonical.flight;
    body.tick = 25;
    body.craft = {kWayfarerFrameId, 1};
    body.frame = {RigidFrameKind::planet_relative_inertial,
                  owner.catalog.id,
                  planet.id,
                  {}};
    body.position_metres = {planet.radius.value * 1000.0 + 500000, 130, 75};
    body.orientation = {.8, .4, -.2, .4};
    body.linear_velocity_metres_per_second = {15, -20, -100};
    body.angular_velocity_radians_per_second = {.02, -.03, .04};
    source = required(reframe_rigid_body(
        context, body,
        {{RigidFrameKind::system_inertial, owner.catalog.id, {}, {}},
         body.tick}));
    // Independent oracle: subtract final authoritative ephemeris exactly once.
    // Never call the reverse handoff or import decoder to construct the oracle.
    const auto ephemeris =
        required(resolve_planet_ephemeris(owner, planet.id, {source.tick, 0}));
    oracle = canonical;
    oracle.flight = source;
    oracle.flight.frame = body.frame;
    oracle.flight.position_metres = {
        source.position_metres.x - ephemeris.position.x,
        source.position_metres.y - ephemeris.position.y,
        source.position_metres.z - ephemeris.position.z};
    oracle.flight.linear_velocity_metres_per_second = {
        source.linear_velocity_metres_per_second.x - ephemeris.velocity.x,
        source.linear_velocity_metres_per_second.y - ephemeris.velocity.y,
        source.linear_velocity_metres_per_second.z - ephemeris.velocity.z};
    oracle.flight =
        required(canonicalize_rigid_body_state(context, oracle.flight));
    input =
        Json::parse(required(encode_freedom_flight_document_json(canonical)));
    input["format_version"] = kFreedomSystemFlightImportFormatVersion;
    input["state"]["location"] = "system_flight";
    input["flight_model"]["terrain_policy"] = policy;
    input["flight"] =
        Json::parse(required(encode_rigid_body_state_json(context, source)));
  }
  auto bytes() const -> std::string { return input.dump(2) + "\n"; }
  auto malformed() const -> Json {
    auto bad = input;
    bad["world_owner"]["planet_id"] = "0";
    return bad;
  }
};
auto invalid(const Fixture& f) -> void {
  const auto before = f.bytes();
  const auto refused = [&](const Json& bad, std::string_view label) {
    check(!decode_freedom_system_flight_import_json(bad.dump()) &&
              !decode_native_save_document_json(bad.dump()),
          label);
  };
  for (auto key :
       {"application", "application_version", "format_version", "mode",
        "recipe", "state", "flight", "flight_model", "world_owner"}) {
    auto bad = f.input;
    bad.erase(key);
    refused(bad, "incomplete import cannot invent a full-flight owner");
  }
  auto bad = f.input;
  bad["extra"] = true;
  refused(bad, "unknown fields rejected");
  bad = f.input;
  bad["craft_lineage"] = {{"version", 1}, {"generation", "2"}};
  refused(bad, "bare import cannot invent a recovery owner");
  bad = f.input;
  bad["state"]["location"] = "planetary_flight";
  refused(bad, "system source location must be explicit");
  bad = f.input;
  bad["state"]["tick"] = "24";
  refused(bad, "history/source clocks must match before transform");
  bad = f.input;
  bad["flight"]["tick"] = "18446744073709551614";
  bad["state"]["tick"] = "18446744073709551614";
  refused(bad, "unadvanceable source clock rejected");
  bad = f.input;
  bad["world_owner"]["system_id"] = "0";
  refused(bad, "foreign world rejected before source conversion");
  refused(f.malformed(), "unknown selected force body rejected");
  bad = f.input;
  bad["world_owner"]["version"] = 99;
  refused(bad, "unsupported world recipe rejected");
  bad = f.input;
  bad["recipe"]["universe_seed"] = "43";
  refused(bad, "altered source universe cannot select another catalog");
  for (auto key :
       {"physical_catalog", "physical_ephemeris", "rotation_owner",
        "rotation_generator", "central_body", "atmosphere", "terrain_policy"}) {
    bad = f.input;
    bad["flight_model"][key] = 99;
    refused(bad, "unsupported force/terrain tuple rejected");
  }
  bad = f.input;
  bad["flight_model"].erase("terrain_policy");
  refused(bad, "import cannot infer terrain policy");
  bad = f.input;
  bad["flight_model"]["orbit_hold"]["version"] = 99;
  refused(bad, "unsupported hold policy rejected");
  bad = f.input;
  bad["flight_model"]["orbit_hold"]["target"] = {
      {"planet_id", "0"}, {"radius_metres", 1e8}, {"plane_normal", {0, 0, 1}}};
  refused(bad, "hold target must bind force body before transform");
  for (const auto& normal : {Json::array({0, 0, 0}), Json::array({1e300, 0, 0}),
                             Json::array({0, 1})}) {
    bad = f.input;
    bad["flight_model"]["orbit_hold"]["target"] = {
        {"planet_id", std::to_string(f.planet.id.value)},
        {"radius_metres", f.planet.radius.value * 1000.0 + 1e6},
        {"plane_normal", normal}};
    refused(bad, "invalid hold normal/buffer rejected");
  }
  for (auto field :
       {"position_metres", "linear_velocity_metres_per_second",
        "angular_velocity_radians_per_second", "orientation_wxyz"}) {
    for (auto value : {"nan", "inf", "1e309"}) {
      bad = f.input;
      bad["flight"][field][0] = value;
      refused(bad, "nonfinite rigid source rejected");
    }
    bad = f.input;
    bad["flight"][field].push_back("0");
    refused(bad, "oversized rigid buffers rejected");
  }
  bad = f.input;
  bad["flight"]["orientation_wxyz"] = {"0", "0", "0", "0"};
  refused(bad, "degenerate orientation rejected");
  bad = f.input;
  bad["flight"]["owner"]["system_seed"] = "0";
  refused(bad, "forged rigid owner rejected");
  auto extreme = f.source;
  const auto ephemeris = required(
      resolve_planet_ephemeris(f.owner, f.planet.id, {f.source.tick, 0}));
  extreme.position_metres.x =
      std::copysign(kRigidBodyMaximumPositionMetres, -ephemeris.position.x);
  bad = f.input;
  bad["flight"] =
      Json::parse(required(encode_rigid_body_state_json(f.context, extreme)));
  const auto overflow = decode_freedom_system_flight_import_json(bad.dump());
  check(ephemeris.position.x != 0 && !overflow &&
            overflow.error().detail ==
                "system import same-tick conversion is unsafe or unsupported",
        "legal source exceeding destination bounds fails actual conversion");
  bad = f.input;
  bad["flight"]["craft"]["id"] = "1";
  refused(bad, "unsupported craft cannot inherit Wayfarer controls");
  bad = f.input;
  bad["flight"] = Json::parse(
      required(encode_rigid_body_state_json(f.context, f.canonical.flight)));
  refused(bad, "planet-relative rigid-v3 cannot masquerade as system source");
  bad = f.input;
  bad["flight"]["frame"]["kind"] = "planet_fixed";
  bad["flight"]["frame"]["planet"] = std::to_string(f.planet.id.value);
  refused(bad, "rotating source is not silently interpreted");
  const std::string duplicate =
      "{\"format_version\":35," + f.input.dump().substr(1);
  const auto dup = decode_freedom_system_flight_import_json(duplicate);
  check(!dup && dup.error().code == SaveSchemaErrorCode::duplicate_key,
        "duplicate source fields rejected");
  const auto over = decode_freedom_system_flight_import_json(
      std::string(kMaximumSaveDocumentBytes + 1, ' '));
  check(!over && over.error().code == SaveSchemaErrorCode::document_too_large,
        "import buffer bounded before parse");
  check(!decode_freedom_system_flight_import_json(std::string(65, '[') +
                                                  std::string(65, ']')),
        "excessive nesting rejected");
  const auto old = decode_freedom_flight_document_json(before);
  check(!old &&
            old.error().code == SaveSchemaErrorCode::unsupported_format_version,
        "historical flight decoder explicitly still refuses35");
  for (auto version : {18U, 26U, 31U}) {
    bad = f.input;
    bad["format_version"] = version;
    refused(bad, "old formats cannot reinterpret import fields/frame");
  }
  const FreedomSurfaceSaveDocument surface{f.canonical, {false, {}}};
  auto wrapped =
      Json::parse(required(encode_freedom_surface_document_json(surface)));
  wrapped["base"] = f.input;
  check(!decode_freedom_surface_document_json(wrapped.dump()) &&
            !decode_native_save_document_json(wrapped.dump()),
        "nested surface codec cannot smuggle top-level import35");
  check(f.bytes() == before,
        "all malformed import queries leave complete source untouched");
}
auto rated_rise(const Fixture& f) -> void {
  const auto properties =
      required(resolve_craft_frame(f.canonical.flight.craft)).properties;
  const double radius = f.planet.radius.value * 1000.0;
  const double surface_g = f.planet.surface_gravity.value * 9.80665 / 1000.0;
  std::array<double, 2> upright{};
  std::size_t altitude_index{};
  for (const double altitude : {500000.0, 1000000.0}) {
    for (const double angle :
         {-std::numbers::pi / 2, -std::numbers::pi / 6, std::numbers::pi / 2}) {
      auto document = f.canonical;
      document.flight.position_metres = {radius + altitude, 0, 0};
      document.flight.orientation = {std::cos(angle / 2), 0, 0,
                                     std::sin(angle / 2)};
      auto session = required(NativeFreedomFlightSession::open(
          required(native_select_save_document(NativeSaveDocument{document}))));
      const auto before = required(
          encode_native_save_document_json(required(session.save_document())));
      const auto view = godot_spike::project_saved_flight(session);
      const auto margin = godot_spike::rated_rise_margin(session, view);
      const auto& body = session.document().flight;
      const auto& q = body.orientation;
      // Independent Hamilton q * (0,+Y) * conjugate(q), projected onto the
      // actual saved radial position. Do not reuse the tangent-axis helper.
      const double n = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
      const V up{2 * (q.x * q.y - q.w * q.z) / n,
                 (q.w * q.w - q.x * q.x + q.y * q.y - q.z * q.z) / n,
                 2 * (q.y * q.z + q.w * q.x) / n};
      const auto p = body.position_metres;
      const double distance = std::hypot(p.x, p.y, p.z);
      const double radial_up =
          (up.x * p.x + up.y * p.y + up.z * p.z) / distance;
      const double thrust =
          static_cast<double>(properties.positive_force_newtons[1]) /
          properties.dry_mass_kg;
      const double gravity =
          surface_g * (radius / distance) * (radius / distance);
      const double expected = thrust * radial_up - gravity;
      check(margin && std::abs(*margin - expected) < 1e-11,
            "rated RISE follows actual saved attitude and local gravity");
      check(
          std::abs(radial_up + std::sin(angle)) < 1e-12,
          "upright tilted and inverted fixtures have independent radial signs");
      if (angle == -std::numbers::pi / 2 && margin)
        upright[altitude_index] = *margin;
      for (const double bad :
           {std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(), -1.01, 1.01}) {
        auto malformed = view;
        malformed.body_axes[1].up = bad;
        check(!godot_spike::rated_rise_margin(session, malformed),
              "malformed optional RISE projection refuses without changing "
              "flight");
      }
      for (int repeat = 0; repeat < 3; ++repeat)
        check(godot_spike::rated_rise_margin(session, view) == margin,
              "repeated rated RISE reads are stable");
      check(required(encode_native_save_document_json(
                required(session.save_document()))) == before,
            "valid and refused RISE reads preserve complete saved state and "
            "tick");
    }
    ++altitude_index;
  }
  check(upright[1] > upright[0],
        "same upright craft gains RISE margin as inverse-square gravity falls");
}
auto accepted(const Fixture& f, const std::filesystem::path& directory,
              bool export_fixture = false) -> void {
  const auto bytes = f.bytes();
  const auto imported =
      required(decode_freedom_system_flight_import_json(bytes));
  const auto dispatched = required(decode_native_save_document_json(bytes));
  check(
      std::holds_alternative<FreedomFlightSaveDocument>(dispatched) &&
          std::get<FreedomFlightSaveDocument>(dispatched) == f.oracle &&
          imported == f.oracle,
      "top-level import equals independent final-ephemeris subtraction oracle");
  check(imported.origin == f.canonical.origin &&
            imported.model == f.canonical.model &&
            imported.world == f.canonical.world,
        "import preserves full history model explicit force body and terrain");
  check(imported.flight.orientation == f.source.orientation &&
            same_vector(imported.flight.angular_velocity_radians_per_second,
                        f.source.angular_velocity_radians_per_second) &&
            imported.flight.tick == f.source.tick,
        "system-aligned translation preserves full attitude body spin and "
        "clock exactly");
  const auto ephemeris = required(
      resolve_planet_ephemeris(f.owner, f.planet.id, {f.source.tick, 0}));
  const auto p = imported.flight.position_metres,
             oldp = f.canonical.flight.position_metres;
  const auto v = imported.flight.linear_velocity_metres_per_second,
             oldv = f.canonical.flight.linear_velocity_metres_per_second;
  check(close(p.x, oldp.x, ephemeris.position.x) &&
            close(p.y, oldp.y, ephemeris.position.y) &&
            close(p.z, oldp.z, ephemeris.position.z) &&
            close(v.x, oldv.x, ephemeris.velocity.x) &&
            close(v.y, oldv.y, ephemeris.velocity.y) &&
            close(v.z, oldv.z, ephemeris.velocity.z),
        "physical source roundtrip uses existing scale-qualified cancellation "
        "tolerance");
  const auto encoded = required(encode_freedom_flight_document_json(imported));
  check(Json::parse(encoded)["format_version"] ==
                (f.canonical.model.terrain_policy == 2 ? 31 : 26) &&
            required(decode_freedom_flight_document_json(encoded)) == imported,
        "ordinary Save As stays in canonical historical format26/31");
  check(required(decode_freedom_flight_document_json(required(
            encode_freedom_flight_document_json(f.canonical)))) == f.canonical,
        "historical canonical bytes retain normal hydration");
  auto live = required(NativeFreedomFlightSession::open(
      required(native_select_save_document(dispatched))));
  auto oracle = required(NativeFreedomFlightSession::open(
      required(native_select_save_document(NativeSaveDocument{f.oracle}))));
  const auto a = godot_spike::project_saved_flight(live),
             b = godot_spike::project_saved_flight(oracle);
  check(a.body_axes == b.body_axes && a.system_axes == b.system_axes &&
            a.pose == b.pose && a.rotation == b.rotation &&
            a.station_available == b.station_available &&
            a.station_position == b.station_position,
        "actual native body tangent star and station projection match "
        "independent canonical owner");
  // Exercise physical commands, rather than proving only parser roundtrips.
  const NativeFlightControls controls{
      {.2, .1, 0}, {0, 0, .4}, {.1, 0, .2}, {0, .1, 0}};
  required(live.advance(controls));
  required(oracle.advance(controls));
  check(live.document() == oracle.document() &&
            required(live.observe()) == required(oracle.observe()),
        "body-axis controls produce exact real 120 Hz step from converted "
        "oracle");
  auto continued = required(NativeFreedomFlightSession::open(required(
      native_select_save_document(required(decode_native_save_document_json(
          required(encode_native_save_document_json(
              required(live.save_document())))))))));
  required(live.advance(controls));
  required(continued.advance(controls));
  check(live.document() == continued.document() &&
            required(live.observe()) == required(continued.observe()),
        "Save Continue resumes controlled physical step exactly after import");
  if (!export_fixture) return;
  std::filesystem::create_directories(directory);
  const auto source_path = directory / "system-import.json";
  write(source_path, bytes);
  write(directory / "malformed-import.json", f.malformed().dump(2) + "\n");
  write(directory / "canonical-before.json",
        required(encode_freedom_flight_document_json(f.oracle)));
  auto file_session = required(
      NativeFreedomFlightSession::open(required(native_continue(source_path))));
  required(file_session.save_as(directory / "import-save-as.json"));
  check(read(source_path) == bytes &&
            read(directory / "import-save-as.json") ==
                read(directory / "canonical-before.json"),
        "real Continue and atomic Save As preserve original system-frame "
        "source file");
  auto neutral_oracle = required(NativeFreedomFlightSession::open(
      required(native_select_save_document(NativeSaveDocument{f.oracle}))));
  required(file_session.advance({}));
  required(neutral_oracle.advance({}));
  write(
      directory / "canonical-after.json",
      required(encode_freedom_flight_document_json(neutral_oracle.document())));
  check(file_session.document() == neutral_oracle.document() &&
            read(source_path) == bytes,
        "native file Continue neutral tick matches independent exported "
        "physical oracle");
}
} // namespace
int main(int argc, char** argv) {
  if (argc != 1 && !(argc == 3 && std::string_view{argv[1]} == "--fixtures"))
    return 2;
  try {
    const Fixture primary;
    invalid(primary);
    rated_rise(primary);
    for (const auto seed :
         {Seed{0}, Seed{42}, Seed{std::numeric_limits<std::uint64_t>::max()}})
      for (bool neighbor : {false, true})
        for (std::uint32_t policy : {1U, 2U})
          accepted(Fixture{seed, neighbor, policy}, {}, false);
    if (argc == 3) accepted(primary, std::filesystem::absolute(argv[2]), true);
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << error.what() << '\n';
  }
  std::cout << checks << " system flight import checks, " << failures
            << " failures\n";
  return failures ? 1 : 0;
}
