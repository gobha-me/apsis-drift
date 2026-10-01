#include "apsis_drift/operating_motion.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "apsis_drift/operating_motion_recipe.hpp"

namespace {
using namespace apsis_drift;
using Json = nlohmann::json;
int failures{};
std::size_t comparisons{};
double maximum_error{};
auto check(bool value, std::string_view label) -> void {
  if (!value) {
    std::cerr << "FAIL: " << label << '\n';
    ++failures;
  }
}
template <class T> auto require(std::expected<T, std::string> value) -> T {
  if (!value) throw std::runtime_error(value.error());
  return *value;
}
auto fixture(std::string_view name) -> Json {
  std::ifstream stream(std::string(APSIS_OPERATING_MOTION_FIXTURE_DIR) + "/" +
                       std::string(name));
  if (!stream)
    throw std::runtime_error("Missing archived operating source fixture");
  return Json::parse(stream);
}
auto coordinate(RigidVector3 value, std::size_t axis) -> double {
  return axis == 0 ? value.x : axis == 1 ? value.y : value.z;
}
auto rows(const Json& value) -> OperatingTransform {
  OperatingTransform result;
  for (std::size_t col = 0; col < 4; ++col)
    result.columns[col] = {value[0][col].get<double>(),
                           value[1][col].get<double>(),
                           value[2][col].get<double>()};
  return result;
}
auto compare(const OperatingTransform& value, const Json& expected,
             std::string_view label) -> void {
  bool passed = true;
  for (std::size_t col = 0; col < 4; ++col)
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const auto error = std::abs(coordinate(value.columns[col], axis) -
                                  expected[axis][col].get<double>());
      maximum_error = std::max(maximum_error, error);
      passed = passed && std::isfinite(error) && error <= 5e-6;
    }
  ++comparisons;
  check(passed, label);
}
auto point(const OperatingTransform& value, RigidVector3 p) -> RigidVector3 {
  const auto& c = value.columns;
  return {((c[0].x * p.x + c[1].x * p.y) + c[2].x * p.z) + c[3].x,
          ((c[0].y * p.x + c[1].y * p.y) + c[2].y * p.z) + c[3].y,
          ((c[0].z * p.x + c[1].z * p.y) + c[2].z * p.z) + c[3].z};
}
auto distance(RigidVector3 a, RigidVector3 b) -> double {
  return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) +
                   (a.z - b.z) * (a.z - b.z));
}
auto source_to_godot(RigidVector3 p) -> RigidVector3 {
  return {p.x, p.z, -p.y};
}
auto source_to_station(RigidVector3 p) -> RigidVector3 {
  return {p.x - .97, p.z, -p.y + .978};
}
auto progress(const Json& values) -> OperatingProgress {
  return {values.value("roof_transfer", 0.0), values.value("inner_door", 0.0),
          values.value("seat_boarding", 0.0),
          values.value("station_closure", 0.0)};
}
auto channel_progress(std::string_view channel, double p) -> OperatingProgress {
  OperatingProgress result;
  if (channel == "roof_transfer")
    result.roof_transfer = p;
  else if (channel == "inner_door")
    result.inner_door = p;
  else if (channel == "seat_boarding")
    result.seat_boarding = p;
  else
    throw std::runtime_error("Unknown source oracle channel");
  return result;
}
auto positive(const OperatingMotionRecipe& recipe) -> void {
  const auto craft = fixture("wayfarer-motion-proposal/source-poses.json");
  check(craft["pose_count"] == 71 && craft["source_poses"].size() == 71,
        "complete original 71 craft oracle poses retained");
  for (const auto& sample : craft["source_poses"]) {
    const auto pose = require(evaluate_operating_motion(
        recipe, channel_progress(sample["channel"].get<std::string>(),
                                 sample["progress"].get<double>())));
    for (std::size_t i = 0; i < kOperatingCraftGroupIds.size(); ++i)
      compare(pose.craft_world_deltas[i],
              sample["source_runtime_delta_rows"]
                    [std::string(kOperatingCraftGroupIds[i])],
              "every craft group agrees with actual corrected source");
  }
  const auto combined =
      fixture("operating-motion-fixtures-01/craft-combined.json");
  check(combined["poses"].size() == 8,
        "all eight independent combined tuples retained");
  for (const auto& sample : combined["poses"]) {
    const auto request = progress(sample["progress"]);
    const auto pose = require(evaluate_operating_motion(recipe, request));
    for (std::size_t i = 0; i < kOperatingCraftGroupIds.size(); ++i)
      compare(pose.craft_world_deltas[i],
              sample["groups"][std::string(kOperatingCraftGroupIds[i])]
                    ["godot_world_delta_rows"],
              "combined ancestor motions agree with actual source");
    (void)require(evaluate_operating_motion(recipe, {1, 1, 1, 1}));
    check(
        require(evaluate_operating_motion(recipe, request)) == pose,
        "evaluation starts from immutable rest regardless of previous request");
  }
  const auto station =
      fixture("operating-motion-fixtures-01/d1-recipe-source-poses.json");
  check(station["source_poses"].size() == 41,
        "all 41 actual D1 source poses retained");
  for (const auto& sample : station["source_poses"]) {
    OperatingProgress request;
    request.station_closure = sample["progress"];
    const auto pose = require(evaluate_operating_motion(recipe, request));
    for (std::size_t i = 0; i < kOperatingStationGroupIds.size(); ++i) {
      const auto& control =
          sample["controls"][std::string(kOperatingStationGroupIds[i])];
      compare(pose.station_node_local[i], control["native_local_godot_rows"],
              "D1 local transforms agree below original imported parent");
      compare(pose.station_contact_deltas[i],
              control["canonical_contact_delta_rows"],
              "D1 world contact deltas agree including station offset");
    }
  }
}
auto pivots(const OperatingMotionRecipe& recipe) -> void {
  const auto craft = fixture("wayfarer-motion-proposal/source-poses.json");
  const auto& negative = craft["rejected_world_delta_interpolation"];
  const auto pose = require(evaluate_operating_motion(recipe, {.425, 0, 0, 0}));
  const auto ladder =
      std::ranges::find_if(recipe.craft_nodes, [](const auto& node) {
        return node.output_group == 6;
      });
  const auto pivot = source_to_godot(ladder->rest_world.columns[3]);
  check(distance(point(pose.craft_world_deltas[6], pivot), pivot) < 1e-8,
        "continuous ladder rotation preserves exact source pivot at rejected "
        "midpoint");
  const auto rejected = rows(negative["rejected_runtime_delta_rows"]);
  const auto drift = distance(point(rejected, pivot), pivot);
  check(
      drift > .048 && drift < .050 &&
          std::abs(drift - negative["source_pivot_displacement_error_m"]
                               .get<double>()) < 1e-6,
      "retained former world-delta interpolation fails pivot by about48.75mm");
  for (const auto p : {.137, .425, .483, .625, .819, 1.0}) {
    const auto result =
        require(evaluate_operating_motion(recipe, {p, 0, 0, 0}));
    for (const auto& node : recipe.craft_nodes)
      if (node.output_group && *node.output_group < 2) {
        const auto hinge = source_to_godot(node.rest_world.columns[3]);
        check(distance(
                  point(result.craft_world_deltas[*node.output_group], hinge),
                  hinge) < 1e-8,
              "roof hinges preserve source pivots at independent intermediate "
              "values");
      }
  }
  const auto gate =
      std::ranges::find_if(recipe.station_nodes, [](const auto& node) {
        return node.output_group == 16;
      });
  const auto gate_pivot = source_to_station(gate->rest_world.columns[3]);
  const auto gate_pose =
      require(evaluate_operating_motion(recipe, {0, 0, 0, 115.0 / 190}));
  check(distance(point(gate_pose.station_contact_deltas[16], gate_pivot),
                 gate_pivot) < 5e-6,
        "D1 canonical contact rotation preserves actual gate hinge");
  // Remove offset conjugation to recreate the specific wrong contact-space
  // recipe.
  const RigidVector3 offset{-.97, 0, .978};
  auto wrong_gate = gate_pose.station_contact_deltas[16];
  auto translated = point(wrong_gate, offset);
  wrong_gate.columns[3] = {translated.x - offset.x, translated.y - offset.y,
                           translated.z - offset.z};
  check(distance(point(wrong_gate, gate_pivot), gate_pivot) > .5,
        "rotation-only conversion without canonical station offset fails gate "
        "pivot");
}
auto invalid(const OperatingMotionRecipe& recipe) -> void {
  check(!decode_operating_motion_recipe(""), "empty document refuses");
  check(!decode_operating_motion_recipe("{}"), "missing schema refuses");
  check(!decode_operating_motion_recipe(
            std::string(kOperatingMotionMaximumDocumentBytes + 1, ' ')),
        "oversize document refuses before parse");
  check(!decode_operating_motion_recipe(std::string(100, '[') +
                                        std::string(100, ']')),
        "deep nesting refuses before parser recursion");
  auto canonical = Json::parse(detail::kOperatingMotionRecipeJson);
  auto refuse = [&](Json bad, std::string_view label) {
    check(!decode_operating_motion_recipe(bad.dump()), label);
  };
  auto bad = canonical;
  bad["extra"] = 0;
  refuse(bad, "unknown top field refuses");
  bad = canonical;
  bad["version"] = true;
  refuse(bad, "boolean version refuses");
  bad = canonical;
  bad["identities"]["contact_sha256"] = "stale";
  refuse(bad, "changed source contact identity refuses");
  bad = canonical;
  bad["craft"]["nodes"][0]["parent"] = 0;
  refuse(bad, "self parent/cycle refuses");
  bad = canonical;
  bad["craft"]["nodes"][0]["parent"] = true;
  refuse(bad, "boolean parent refuses");
  bad = canonical;
  bad["craft"]["nodes"][1]["source_object"] =
      bad["craft"]["nodes"][0]["source_object"];
  refuse(bad, "duplicate source identity refuses");
  bad = canonical;
  bad["craft"]["nodes"][1]["group"] = bad["craft"]["nodes"][0]["group"];
  refuse(bad, "duplicate group refuses");
  bad = canonical;
  bad["station"]["nodes"][1]["group"] = nullptr;
  refuse(bad, "missing station group refuses");
  bad = canonical;
  bad["craft"]["nodes"][0]["rest_world"][0][0] = 2;
  refuse(bad, "scaled/non-rigid rest refuses");
  bad = canonical;
  bad["craft"]["nodes"][0]["rest_world"][0] = Json::array({1, 0});
  refuse(bad, "invalid matrix dimensions refuse");
  bad = canonical;
  bad["craft"]["nodes"][0]["location_metres"][0] = nullptr;
  refuse(bad, "null/non-finite serialization refuses");
  bad = canonical;
  bad["craft"]["tracks"][0]["axis"] = 3;
  refuse(bad, "one-past-axis refuses");
  bad = canonical;
  bad["craft"]["tracks"][0]["knots"][1][0] = 0;
  refuse(bad, "duplicate/nonincreasing progress refuses");
  bad = canonical;
  bad["craft"]["tracks"][0]["knots"][1][0] = .39;
  refuse(bad, "finite altered authored phase refuses");
  bad = canonical;
  bad["station"]["tracks"][0]["knots"].push_back(Json::array({1, 0}));
  refuse(bad, "duplicate endpoint refuses");
  bad = canonical;
  bad["craft"]["tracks"][0]["channel"] = "unknown";
  refuse(bad, "unknown channel refuses");
  bad = canonical;
  bad["craft"]["tracks"][0] = bad["craft"]["tracks"][1];
  refuse(bad, "duplicate scalar target refuses");
  auto duplicate = std::string(detail::kOperatingMotionRecipeJson);
  duplicate.insert(duplicate.find('{') + 1, "\"version\":1,");
  check(!decode_operating_motion_recipe(duplicate),
        "duplicate JSON key refuses");
  auto overflow = std::string(detail::kOperatingMotionRecipeJson);
  const auto first_vector = overflow.find("location_metres");
  const auto first_number = overflow.find('[', first_vector) + 1;
  const auto first_end = overflow.find(',', first_number);
  overflow.replace(first_number, first_end - first_number, "1e10000");
  check(!decode_operating_motion_recipe(overflow),
        "overflowing numeric literal refuses without exposing partial recipe");
  bad = canonical;
  bad["craft"]["nodes"].erase(bad["craft"]["nodes"].end() - 1);
  refuse(bad, "missing source ancestor/node refuses");
  bad = canonical;
  bad["craft"]["tracks"][0]["axis"] = true;
  refuse(bad, "boolean axis refuses");
  bad = canonical;
  bad["craft"]["nodes"][0]["rest_world"][0][0] = -1;
  refuse(bad, "reflected source basis refuses");
  bad = canonical;
  bad["craft"]["tracks"][0]["knots"][1][1] = .5;
  refuse(bad, "finite altered authored travel refuses");
  const auto unchanged = recipe;
  auto changed = recipe;
  changed.craft_nodes[0].location_metres.x =
      std::numeric_limits<double>::quiet_NaN();
  check(!evaluate_operating_motion(changed, {}),
        "typed non-finite recipe refuses transactionally");
  changed = recipe;
  changed.craft_tracks[0].channel = static_cast<OperatingChannel>(255);
  check(!evaluate_operating_motion(changed, {}),
        "unknown typed channel refuses");
  changed = recipe;
  changed.craft_nodes[0].parent = 14;
  check(!evaluate_operating_motion(changed, {}),
        "typed cycle/forward parent refuses before indexing");
  for (const auto value :
       {-1e-9, 1.000000001, std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity()})
    for (int channel = 0; channel < 4; ++channel) {
      OperatingProgress request;
      if (channel == 0) request.roof_transfer = value;
      if (channel == 1) request.inner_door = value;
      if (channel == 2) request.seat_boarding = value;
      if (channel == 3) request.station_closure = value;
      check(!evaluate_operating_motion(recipe, request),
            "each invalid/non-finite progress refuses");
    }
  check(recipe == unchanged, "invalid requests never mutate caller recipe");
  const auto rest = require(evaluate_operating_motion(recipe, {}));
  (void)require(evaluate_operating_motion(recipe, {1, 1, 1, 1}));
  check(require(evaluate_operating_motion(recipe, {})) == rest,
        "returning to rest is bit identical and no mutable mechanism clock "
        "exists");
}
} // namespace

int main() {
  try {
    const auto recipe = require(
        decode_operating_motion_recipe(detail::kOperatingMotionRecipeJson));
    invalid(recipe); // Buffer/type/finite boundaries precede any source visual
                     // proof.
    positive(recipe);
    pivots(recipe);
    std::cout << comparisons
              << " source matrix comparisons; maximum component error "
              << maximum_error << '\n';
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  return failures == 0 ? 0 : 1;
}
