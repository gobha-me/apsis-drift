#include "apsis_drift/craft_frame.hpp"
#include "apsis_drift/freedom_flight_save.hpp"
#include "apsis_drift/station_geometry.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace apsis_drift;
int failures{};
auto check(bool condition, std::string_view message) -> void {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
template <class T, class E> auto require(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Wayfarer fixture refused");
  return *value;
}
auto boundaries() -> void {
  const auto& frame = wayfarer_frame();
  for (const auto recipe :
       {CraftFrameRecipe{kWayfarerFrameId, 0},
        CraftFrameRecipe{kWayfarerFrameId, 2}, CraftFrameRecipe{{3}, 1}}) {
    std::array<std::byte, kCraftFrameRecipeBytes> output{};
    output.fill(std::byte{0xA5});
    const auto before = output;
    check(!resolve_craft_frame(recipe) &&
              !encode_craft_frame_recipe(recipe, output) && output == before,
          "unknown identity/version refuses atomically");
  }
  auto edited = frame.properties;
  ++edited.hull_max_mm[0];
  const CraftFrameDescriptor bad{frame.recipe, frame.diagnostic_name, edited};
  check(!craft_frame_checksum(bad) && !craft_frame_diagnostic_json(bad),
        "caller-edited registered definition refuses");
  std::array<std::byte, kCraftFrameRecipeBytes + 2> guarded{};
  guarded.fill(std::byte{0xA5});
  for (std::size_t size = 0; size < guarded.size(); ++size) {
    if (size == kCraftFrameRecipeBytes) continue;
    const auto before = guarded;
    check(!encode_craft_frame_recipe(frame.recipe,
                                     std::span{guarded}.first(size)) &&
              guarded == before,
          "short/oversized output leaves canaries unchanged");
    check(!decode_craft_frame_recipe(std::span{guarded}.first(size)),
          "short/oversized input refuses");
  }
  check(
      encode_craft_frame_recipe(frame.recipe, std::span{guarded}.subspan(1, 16))
          .has_value(),
      "Wayfarer recipe encodes");
  check(guarded.front() == std::byte{0xA5} &&
            guarded.back() == std::byte{0xA5} &&
            require(decode_craft_frame_recipe(
                std::span{guarded}.subspan(1, 16))) == frame.recipe,
        "Wayfarer recipe roundtrip retains guarded identity");
}
auto geometry_and_ratings() -> void {
  const auto& frame = wayfarer_frame();
  check(frame.recipe == CraftFrameRecipe{{2}, 1} &&
            frame.diagnostic_name == "wayfarer-v1" &&
            require(resolve_craft_frame(frame.recipe)) == frame &&
            validate_craft_frame_properties(frame.properties).has_value(),
        "explicit Wayfarer frame is immutable and physically valid");
  check(require(craft_frame_checksum(frame)) == 15216801238891810296ULL,
        "Wayfarer immutable descriptor checksum golden");
  auto equivalent = frame.properties;
  equivalent.hull_min_mm = starter_shuttle_frame().properties.hull_min_mm;
  equivalent.hull_max_mm = starter_shuttle_frame().properties.hull_max_mm;
  equivalent.supports = starter_shuttle_frame().properties.supports;
  check(
      equivalent == starter_shuttle_frame().properties,
      "authored gameplay ratings are retained independently of mesh geometry");
  check(frame.properties.hull_min_mm == CraftPointMm{-4040, -1259, -5210} &&
            frame.properties.hull_max_mm == CraftPointMm{4040, 2955, 7620},
        "stowed bounds use the source-bound outward millimetre envelope");
  for (const auto& support : frame.properties.supports) {
    if (support == CraftLandingSupport{}) continue;
    check(
        support.contact_mm[1] == -2080 && support.half_width_mm == 320 &&
            support.half_length_mm == 260 && support.stroke_mm == 300 &&
            support.rated_load_newtons == 160000,
        "measured pad geometry and explicit load/stroke ratings retain units");
  }
}
auto port_fit_and_projection() -> void {
  for (const auto seed :
       {Seed{0}, Seed{42}, Seed{std::numeric_limits<std::uint64_t>::max()}}) {
    const auto station = generate_origin_station(seed);
    const auto system = require(generate_physical_origin_system(seed));
    const auto geometry = require(origin_station_geometry(station));
    const RigidBodyWorldContext context{system, &station};
    for (const auto& port : geometry.ports) {
      for (const double separation : {0.0, .15, 6.0, 12.0}) {
        const auto poses = require(resolve_origin_port_pose(
            system, station, geometry, port.id, 25, separation));
        auto state = poses.planet_relative;
        state.craft = wayfarer_frame().recipe;
        const auto encoded =
            require(encode_rigid_body_state_json(context, state));
        check(
            require(decode_rigid_body_state_json(context, encoded)) == state,
            "canonical physical projection preserves explicit Wayfarer recipe");
        auto origin = make_freedom_new_game_document(seed);
        origin.state.tick = state.tick;
        const FreedomFlightSaveDocument document{origin, state, {}};
        const auto save =
            require(encode_freedom_flight_document_json(document));
        check(require(decode_freedom_flight_document_json(save)) == document,
              "flight18 preserves explicitly selected Wayfarer identity");
        const auto& p = wayfarer_frame().properties;
        for (unsigned mask = 0; mask < 8; ++mask) {
          const auto x =
              ((mask & 1U) != 0 ? p.hull_max_mm[0] : p.hull_min_mm[0]) / 1000.0;
          const auto y =
              ((mask & 2U) != 0 ? p.hull_max_mm[1] : p.hull_min_mm[1]) / 1000.0;
          const auto z =
              ((mask & 4U) != 0 ? p.hull_max_mm[2] : p.hull_min_mm[2]) / 1000.0;
          // Independent +/-90-degree yaw oracle; no provider rotation call.
          const auto sign = port.id.ordinal == 1 ? 1.0 : -1.0;
          const RigidVector3 corner{
              poses.station_relative.position_metres.x + sign * z,
              poses.station_relative.position_metres.y + y,
              poses.station_relative.position_metres.z - sign * x};
          const auto& box = port.approach_reservation;
          check(corner.x >= box.minimum_metres.x &&
                    corner.x <= box.maximum_metres.x &&
                    corner.y >= box.minimum_metres.y &&
                    corner.y <= box.maximum_metres.y &&
                    corner.z >= box.minimum_metres.z &&
                    corner.z <= box.maximum_metres.z,
                "entire aligned stowed hull fits D1/D2 withdrawal reservation");
        }
      }
    }
  }
}
} // namespace
auto main() -> int {
  boundaries();
  geometry_and_ratings();
  port_fit_and_projection();
  std::cout << "Wayfarer frame: " << failures << " failures; checksum "
            << require(craft_frame_checksum(wayfarer_frame())) << '\n';
  return failures == 0 ? 0 : 1;
}
