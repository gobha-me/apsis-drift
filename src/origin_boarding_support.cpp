#include "apsis_drift/origin_boarding_support.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

#include <nlohmann/json.hpp>

#include "apsis_drift/boarding_support_data.hpp"

namespace apsis_drift {
struct OriginBoardingSupport::Data {
  OperatingMotionRecipe motion;
  std::vector<BoardingSupportGroup> groups;
  std::vector<BoardingSupportObject> objects;
  std::vector<BoardingSupportPatch> patches;
  std::vector<BoardingSelectedFace> faces;
};
namespace {
using Json = nlohmann::json;
using Data = OriginBoardingSupport::Data;
constexpr std::array<std::string_view, 10> identity_keys{
    "support_sha256",
    "contact_sha256",
    "operating_motion_sha256",
    "craft_model_sha256",
    "station_clearance_model_sha256",
    "station_motion_model_sha256",
    "craft_source_sha256",
    "station_source_sha256",
    "closure_source_sha256",
    "operating_package_sha256"};
constexpr std::array<std::string_view, 10> identity_values{
    "2d84bf607ac136e8c847a5e7cd7be5529d9a28a89f01f444b520a0950cdaa5d2",
    "109e3f140f6865612118b2712021a71c0732200f6adc14ac2d4a1ce1d749657a",
    "afa1eb3d81deab1b5ac00a53d1650fa9bb222bcf8ea9c376efa166b93eda0298",
    "a9a8104a0ea8b5c22e4149861a76b5ab3911a9f77ba871b446c08bf9ed56621c",
    "a02d4b2a14f4459f6b5109768fa102aa214e611d3aa3faf28e573cd557e3a29e",
    "79c8303ebe362a619f58a931cf97102cf63cd7bdd36f46a86152368107e0be80",
    "87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677",
    "6a3d4cf56af54b8b4d1cc1e344f32651609022280f1c3fb0a9109bf86dfa4fb6",
    "6a12e1e6846be4de6c89ce0c65b154567a8a07818d37bf574de2a319109319b4",
    "9a3d2632b3231dfd0a418049a0e80fa6534feaebd3ffa944c2c9635755a5f957"};
constexpr std::array<std::string_view, 6> roles{"hand_grasp", "boot_support",
                                                "elbow_rest", "head_rest",
                                                "back_rest",  "seat_pan"};
constexpr std::array<std::string_view, 6> bodies{
    "hand/fingers", "boot sole", "elbow/forearm",
    "helmet/head",  "back",      "pelvis/thigh"};
auto fail(std::string_view message) -> void {
  throw std::runtime_error(std::string(message));
}
auto keys(const Json& value, std::initializer_list<std::string_view> names)
    -> bool {
  return value.is_object() && value.size() == names.size() &&
         std::ranges::all_of(names, [&](std::string_view name) {
           return value.contains(std::string(name));
         });
}
auto array_size(const Json& value, std::size_t size) -> void {
  if (!value.is_array() || value.size() != size)
    fail("Boarding array dimensions");
}
auto unsigned_number(const Json& value, std::uint32_t maximum)
    -> std::uint32_t {
  if (!value.is_number_unsigned() || value.get<std::uint64_t>() > maximum)
    fail("Boarding integer type/bound");
  return value.get<std::uint32_t>();
}
auto scalar(const Json& value, double bound = 128) -> double {
  if (!value.is_number()) fail("Boarding scalar type");
  const auto result = value.get<double>();
  if (!std::isfinite(result) || std::abs(result) > bound)
    fail("Boarding scalar finite/bound");
  return result;
}
auto text(const Json& value, std::size_t maximum = 256) -> std::string {
  if (!value.is_string()) fail("Boarding string type");
  auto result = value.get<std::string>();
  if (result.empty() || result.size() > maximum) fail("Boarding string length");
  return result;
}
auto vector(const Json& value) -> RigidVector3 {
  array_size(value, 3);
  return {scalar(value[0]), scalar(value[1]), scalar(value[2])};
}
auto add(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto subtract(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(RigidVector3 a, double b) -> RigidVector3 {
  return {a.x * b, a.y * b, a.z * b};
}
auto dot(RigidVector3 a, RigidVector3 b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto cross(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto norm(RigidVector3 a) -> double {
  return std::sqrt(dot(a, a));
}
auto finite(RigidVector3 value) -> bool {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z) && std::abs(value.x) <= 128 &&
         std::abs(value.y) <= 128 && std::abs(value.z) <= 128;
}
auto near(RigidVector3 a, RigidVector3 b, double tolerance = 2e-12) -> bool {
  return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance &&
         std::abs(a.z - b.z) <= tolerance;
}
auto unit(RigidVector3 value) -> bool {
  return std::abs(dot(value, value) - 1) <= 1e-9;
}
auto inverse_point(const OperatingTransform& transform, RigidVector3 point)
    -> RigidVector3 {
  const auto local = subtract(point, transform.columns[3]);
  return {dot(transform.columns[0], local), dot(transform.columns[1], local),
          dot(transform.columns[2], local)};
}
auto on_triangle(const BoardingSelectedFace& face, RigidVector3 point) -> bool {
  const auto& v = face.vertices_rest_metres;
  const auto normal = cross(subtract(v[1], v[0]), subtract(v[2], v[0]));
  const auto length = norm(normal);
  if (!(length > 0)) return false;
  const auto direction = scale(normal, 1 / length);
  if (std::abs(dot(subtract(point, v[0]), direction)) >
      kBoardingSurfaceToleranceMetres)
    return false;
  for (std::size_t i = 0; i < 3; ++i) {
    const auto edge = subtract(v[(i + 1) % 3], v[i]);
    if (dot(cross(edge, subtract(point, v[i])), direction) / norm(edge) <
        -kBoardingSurfaceToleranceMetres)
      return false;
  }
  return true;
}
auto parse_json(std::string_view input) -> Json {
  if (input.empty() || input.size() > kBoardingSupportMaximumDocumentBytes)
    fail("Boarding document byte limit");
  std::size_t depth{};
  bool quoted{}, escaped{};
  for (const auto c : input) {
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
    } else if (c == '"')
      quoted = true;
    else if (c == '[' || c == '{') {
      if (++depth > 16) fail("Boarding document nesting limit");
    } else if (c == ']' || c == '}') {
      if (depth == 0) fail("Boarding document unbalanced nesting");
      --depth;
    }
  }
  bool duplicate{};
  std::vector<std::set<std::string>> seen;
  auto callback = [&](int, Json::parse_event_t event, Json& value) {
    if (event == Json::parse_event_t::object_start)
      seen.emplace_back();
    else if (event == Json::parse_event_t::object_end)
      seen.pop_back();
    else if (event == Json::parse_event_t::key && !seen.empty())
      duplicate =
          !seen.back().insert(value.get<std::string>()).second || duplicate;
    return true;
  };
  auto result = Json::parse(input, callback);
  if (duplicate) fail("Boarding duplicate object key");
  return result;
}
auto range(const Json& value, std::uint32_t bound) -> BoardingTriangleRange {
  array_size(value, 2);
  BoardingTriangleRange result{unsigned_number(value[0], bound),
                               unsigned_number(value[1], bound)};
  if (result.count == 0 || result.start >= bound ||
      result.count > bound - result.start)
    fail("Boarding range bound/overflow");
  return result;
}
auto structural(const Json& root) -> std::shared_ptr<Data> {
  if (!keys(root, {"schema", "version", "identities", "quantization_metres",
                   "crop_bounds_metres", "counts", "groups", "objects",
                   "vertices", "faces", "patches"}) ||
      root["schema"] != "apsis.boarding-support/1" ||
      unsigned_number(root["version"], 1) != 1 ||
      scalar(root["quantization_metres"]) != 1e-6)
    fail("Boarding root schema/version/quantization");
  const auto& identity = root["identities"];
  if (!identity.is_object() || identity.size() != identity_keys.size())
    fail("Boarding identity roster");
  for (std::size_t i = 0; i < identity_keys.size(); ++i)
    if (!identity.contains(std::string(identity_keys[i])) ||
        identity[std::string(identity_keys[i])] != identity_values[i])
      fail("Boarding source/contact/model/motion identity");
  const auto& crop = root["crop_bounds_metres"];
  if (!keys(crop, {"craft", "station"})) fail("Boarding crop schema");
  for (const auto owner : {"craft", "station"}) {
    array_size(crop[owner], 2);
    const auto lo = vector(crop[owner][0]), hi = vector(crop[owner][1]);
    if (lo.x >= hi.x || lo.y >= hi.y || lo.z >= hi.z)
      fail("Boarding crop bounds");
  }
  const auto& counts = root["counts"];
  if (!keys(counts, {"groups", "objects", "triangles", "vertices", "patches",
                     "selected_face_references", "selected_faces",
                     "selected_vertices"}))
    fail("Boarding count schema");
  const std::array<std::pair<std::string_view, std::uint32_t>, 8> totals{
      {{"groups", 32},
       {"objects", 1087},
       {"triangles", 475212},
       {"vertices", 253078},
       {"patches", 42},
       {"selected_face_references", 1474},
       {"selected_faces", 1322},
       {"selected_vertices", 1217}}};
  for (const auto& [key, count] : totals)
    if (unsigned_number(counts[std::string(key)], count) != count)
      fail("Boarding count roster");
  array_size(root["groups"], 32);
  array_size(root["objects"], 1087);
  array_size(root["vertices"], 1217);
  array_size(root["faces"], 1322);
  array_size(root["patches"], 42);
  auto result = std::make_shared<Data>();
  result->groups.reserve(32);
  result->objects.reserve(1087);
  result->faces.reserve(1322);
  result->patches.reserve(42);
  std::set<std::string> group_ids;
  std::array<bool, 13> craft_motion{};
  std::array<bool, 17> station_motion{};
  std::uint32_t object_next{}, triangle_total{}, vertex_total{};
  bool craft_fixed{}, station_fixed{};
  for (const auto& group : root["groups"]) {
    if (!keys(group, {"id", "owner", "motion_group", "vertex_count",
                      "triangle_count", "object_start", "object_count"}))
      fail("Boarding group schema");
    BoardingSupportGroup value;
    value.id = text(group["id"], 64);
    const auto owner = text(group["owner"], 8);
    if (owner != "craft" && owner != "station") fail("Boarding group owner");
    value.owner =
        owner == "craft" ? BoardingOwner::craft : BoardingOwner::station;
    if (!group_ids.insert(value.id).second) fail("Boarding duplicate group");
    if (group["motion_group"].is_null()) {
      if (value.id != owner + "_fixed") fail("Boarding fixed group identity");
      if (value.owner == BoardingOwner::craft)
        craft_fixed = true;
      else
        station_fixed = true;
    } else {
      const auto motion = text(group["motion_group"], 64);
      std::string expected_group;
      expected_group.reserve(owner.size() + 1 + motion.size());
      expected_group.append(owner);
      expected_group.push_back('_');
      expected_group.append(motion);
      if (value.id != expected_group) fail("Boarding motion group relation");
      if (value.owner == BoardingOwner::craft) {
        const auto found = std::ranges::find(kOperatingCraftGroupIds, motion);
        if (found == kOperatingCraftGroupIds.end())
          fail("Boarding unknown craft motion");
        const auto index =
            static_cast<std::size_t>(found - kOperatingCraftGroupIds.begin());
        if (craft_motion[index]) fail("Boarding duplicate craft motion");
        craft_motion[index] = true;
        value.motion_group = index;
      } else {
        const auto found = std::ranges::find(kOperatingStationGroupIds, motion);
        if (found == kOperatingStationGroupIds.end())
          fail("Boarding unknown station motion");
        const auto index =
            static_cast<std::size_t>(found - kOperatingStationGroupIds.begin());
        if (station_motion[index]) fail("Boarding duplicate station motion");
        station_motion[index] = true;
        value.motion_group = index;
      }
    }
    value.vertex_count = unsigned_number(group["vertex_count"], 253078);
    value.triangle_count = unsigned_number(group["triangle_count"], 475212);
    value.object_start = unsigned_number(group["object_start"], 1087);
    value.object_count = unsigned_number(group["object_count"], 1087);
    if (!value.vertex_count || !value.triangle_count || !value.object_count ||
        value.object_start != object_next ||
        value.object_count > 1087 - object_next ||
        value.triangle_count > 475212 - triangle_total ||
        value.vertex_count > 253078 - vertex_total)
      fail("Boarding group partition/count");
    object_next += value.object_count;
    triangle_total += value.triangle_count;
    vertex_total += value.vertex_count;
    result->groups.push_back(std::move(value));
  }
  if (object_next != 1087 || triangle_total != 475212 ||
      vertex_total != 253078 || !craft_fixed || !station_fixed ||
      !std::ranges::all_of(craft_motion, [](bool x) { return x; }) ||
      !std::ranges::all_of(station_motion, [](bool x) { return x; }))
    fail("Boarding incomplete group roster");
  std::set<std::pair<std::uint32_t, std::string>> object_ids;
  for (std::size_t g = 0; g < result->groups.size(); ++g) {
    const auto& group = result->groups[g];
    std::uint32_t next{};
    for (std::uint32_t i = group.object_start;
         i < group.object_start + group.object_count; ++i) {
      const auto& object = root["objects"][i];
      if (!keys(object,
                {"group", "source_object", "triangle_start", "triangle_count",
                 "semantic_label", "default_collision"}) ||
          object["default_collision"] != "obstacle")
        fail("Boarding object schema/obstacle policy");
      BoardingSupportObject value;
      value.group = unsigned_number(object["group"], 31);
      value.source_object = text(object["source_object"]);
      value.semantic_label = text(object["semantic_label"], 64);
      value.triangle_start =
          unsigned_number(object["triangle_start"], group.triangle_count);
      value.triangle_count =
          unsigned_number(object["triangle_count"], group.triangle_count);
      if (value.group != g || value.triangle_start != next ||
          !value.triangle_count ||
          value.triangle_count > group.triangle_count - next ||
          !object_ids.emplace(value.group, value.source_object).second)
        fail("Boarding object range/ownership partition");
      next += value.triangle_count;
      result->objects.push_back(std::move(value));
    }
    if (next != group.triangle_count) fail("Boarding object partition gap");
  }
  struct Vertex {
    std::uint32_t group{}, source{};
    RigidVector3 position;
  };
  std::vector<Vertex> vertices;
  vertices.reserve(1217);
  std::optional<std::pair<std::uint32_t, std::uint32_t>> previous_vertex;
  for (const auto& vertex : root["vertices"]) {
    if (!keys(vertex, {"group", "source_vertex", "position_micrometres"}))
      fail("Boarding vertex schema");
    Vertex value;
    value.group = unsigned_number(vertex["group"], 31);
    value.source = unsigned_number(
        vertex["source_vertex"], result->groups[value.group].vertex_count - 1);
    const auto key = std::pair{value.group, value.source};
    if (previous_vertex && key <= *previous_vertex)
      fail("Boarding vertex identity/order");
    previous_vertex = key;
    array_size(vertex["position_micrometres"], 3);
    std::array<double, 3> coordinates{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      const auto& component = vertex["position_micrometres"][axis];
      if (!component.is_number_integer())
        fail("Boarding quantized integer type");
      if (component.is_number_unsigned() &&
          component.get<std::uint64_t>() > 128000000)
        fail("Boarding quantized integer bound");
      const auto number = component.get<std::int64_t>();
      if (number < -128000000 || number > 128000000)
        fail("Boarding quantized integer bound");
      coordinates[axis] = static_cast<double>(number) * 1e-6;
    }
    value.position = {coordinates[0], coordinates[1], coordinates[2]};
    vertices.push_back(value);
  }
  std::array<bool, 1217> vertex_used{};
  std::optional<std::pair<std::uint32_t, std::uint32_t>> previous_face;
  for (const auto& face : root["faces"]) {
    if (!keys(face, {"group", "source_triangle", "vertices"}))
      fail("Boarding face schema");
    BoardingSelectedFace value;
    value.key.group = unsigned_number(face["group"], 31);
    value.key.triangle =
        unsigned_number(face["source_triangle"],
                        result->groups[value.key.group].triangle_count - 1);
    const auto key = std::pair{value.key.group, value.key.triangle};
    if (previous_face && key <= *previous_face)
      fail("Boarding face identity/order");
    previous_face = key;
    array_size(face["vertices"], 3);
    std::array<std::uint32_t, 3> indices{};
    for (std::size_t i = 0; i < 3; ++i) {
      indices[i] = unsigned_number(face["vertices"][i], 1216);
      const auto& vertex = vertices[indices[i]];
      if (vertex.group != value.key.group)
        fail("Boarding face cross-group vertex");
      value.vertices_rest_metres[i] = vertex.position;
      vertex_used[indices[i]] = true;
    }
    if (indices[0] == indices[1] || indices[0] == indices[2] ||
        indices[1] == indices[2] ||
        !(norm(cross(subtract(value.vertices_rest_metres[1],
                              value.vertices_rest_metres[0]),
                     subtract(value.vertices_rest_metres[2],
                              value.vertices_rest_metres[0]))) > 0))
      fail("Boarding degenerate face");
    result->faces.push_back(value);
  }
  if (!std::ranges::all_of(vertex_used, [](bool x) { return x; }))
    fail("Boarding unreferenced selected vertex");
  std::array<bool, 1322> face_used{};
  std::set<std::string> patch_ids;
  std::uint32_t reference_count{}, reversed_count{}, grasp_count{},
      boot_count{}, rod_count{};
  std::optional<std::uint32_t> previous_object;
  for (const auto& patch : root["patches"]) {
    if (!keys(patch,
              {"id", "object", "role", "body_contact", "triangle_ranges",
               "triangle_count", "selected_faces", "surface_area_m2",
               "representative_triangle", "representative_barycentric",
               "representative_contact_point_rest_m", "bounds_rest_m",
               "contact_side_direction_rest", "opposite_winding_triangles",
               "required_point_predicate"}))
      fail("Boarding patch schema");
    BoardingSupportPatch value;
    value.id = text(patch["id"], 384);
    value.object = unsigned_number(patch["object"], 1086);
    if (!patch_ids.insert(value.id).second ||
        (previous_object && value.object < *previous_object))
      fail("Boarding patch identity/order");
    previous_object = value.object;
    auto& object = result->objects[value.object];
    const auto& group = result->groups[object.group];
    value.role = text(patch["role"], 32);
    value.body_contact = text(patch["body_contact"], 32);
    const auto role = std::ranges::find(roles, value.role);
    if (role == roles.end()) fail("Boarding patch role");
    const auto category = static_cast<std::size_t>(role - roles.begin());
    value.body_category = static_cast<BoardingBodyCategory>(category);
    if (value.body_contact != bodies[category] ||
        value.id != group.id + "/" + object.source_object + "/" + value.role)
      fail("Boarding patch body/identity relation");
    if (!patch["triangle_ranges"].is_array() ||
        patch["triangle_ranges"].empty() ||
        patch["triangle_ranges"].size() > 76)
      fail("Boarding patch range dimensions");
    std::uint32_t selected_count{}, next = object.triangle_start;
    for (const auto& raw : patch["triangle_ranges"]) {
      const auto interval = range(raw, group.triangle_count);
      if (interval.start < next || interval.start < object.triangle_start ||
          interval.start >= object.triangle_start + object.triangle_count ||
          interval.count >
              object.triangle_start + object.triangle_count - interval.start)
        fail("Boarding patch range/owner");
      next = interval.start + interval.count;
      selected_count += interval.count;
      value.triangle_ranges.push_back(interval);
    }
    value.triangle_count = unsigned_number(patch["triangle_count"], 1322);
    if (value.triangle_count != selected_count)
      fail("Boarding patch triangle count");
    array_size(patch["selected_faces"], selected_count);
    value.selected_faces.reserve(selected_count);
    std::size_t cursor{};
    for (const auto interval : value.triangle_ranges)
      for (std::uint32_t triangle = interval.start;
           triangle < interval.start + interval.count; ++triangle) {
        const auto index =
            unsigned_number(patch["selected_faces"][cursor++], 1321);
        if (result->faces[index].key !=
            BoardingTriangleKey{object.group, triangle})
          fail("Boarding patch face/range relation");
        face_used[index] = true;
        value.selected_faces.push_back(index);
      }
    reference_count += selected_count;
    value.surface_area_square_metres = scalar(patch["surface_area_m2"]);
    if (value.surface_area_square_metres <= 0)
      fail("Boarding patch area positive");
    value.representative_triangle = unsigned_number(
        patch["representative_triangle"], group.triangle_count - 1);
    value.representative_barycentric =
        vector(patch["representative_barycentric"]);
    const auto b = value.representative_barycentric;
    if (b.x < 0 || b.y < 0 || b.z < 0 || b.x > 1 || b.y > 1 || b.z > 1 ||
        std::abs((b.x + b.y) + b.z - 1) > 1e-12)
      fail("Boarding barycentric domain");
    value.representative_point_rest_metres =
        vector(patch["representative_contact_point_rest_m"]);
    array_size(patch["bounds_rest_m"], 2);
    value.bounds_rest_metres = {vector(patch["bounds_rest_m"][0]),
                                vector(patch["bounds_rest_m"][1])};
    if (!patch["contact_side_direction_rest"].is_null()) {
      value.contact_side_direction_rest =
          vector(patch["contact_side_direction_rest"]);
      if (!unit(*value.contact_side_direction_rest))
        fail("Boarding side unit direction");
      value.opposite_winding_triangles =
          unsigned_number(patch["opposite_winding_triangles"], selected_count);
    } else if (!patch["opposite_winding_triangles"].is_null())
      fail("Boarding undeclared side winding");
    if ((category == 0) != (!value.contact_side_direction_rest))
      fail("Boarding role side declaration");
    if (category == 0) ++grasp_count;
    if (category == 1) ++boot_count;
    if (!patch["required_point_predicate"].is_null()) {
      const auto& rod = patch["required_point_predicate"];
      if (!keys(rod, {"type", "origin_rest_m", "unit_axis_rest",
                      "inclusive_interval_m", "end_margin_m"}) ||
          rod["type"] != "rest_axis_interval")
        fail("Boarding rod schema");
      BoardingRodPredicate predicate;
      predicate.origin_rest_metres = vector(rod["origin_rest_m"]);
      predicate.unit_axis_rest = vector(rod["unit_axis_rest"]);
      array_size(rod["inclusive_interval_m"], 2);
      predicate.inclusive_interval_metres = {
          scalar(rod["inclusive_interval_m"][0]),
          scalar(rod["inclusive_interval_m"][1])};
      predicate.end_margin_metres = scalar(rod["end_margin_m"]);
      if (!unit(predicate.unit_axis_rest) ||
          predicate.inclusive_interval_metres[0] >=
              predicate.inclusive_interval_metres[1] ||
          predicate.end_margin_metres != .03)
        fail("Boarding rod axis/interval/margin");
      value.rod = predicate;
      ++rod_count;
    }
    if ((category < 2) != value.rod.has_value())
      fail("Boarding role rod declaration");
    double area{};
    std::uint32_t reversed{};
    bool representative_found{};
    RigidVector3 lo{128, 128, 128}, hi{-128, -128, -128};
    for (const auto index : value.selected_faces) {
      const auto& face = result->faces[index];
      const auto& v = face.vertices_rest_metres;
      const auto normal = cross(subtract(v[1], v[0]), subtract(v[2], v[0]));
      area += norm(normal) * .5;
      if (value.contact_side_direction_rest &&
          dot(normal, *value.contact_side_direction_rest) < 0)
        ++reversed;
      for (const auto point : v) {
        lo = {std::min(lo.x, point.x), std::min(lo.y, point.y),
              std::min(lo.z, point.z)};
        hi = {std::max(hi.x, point.x), std::max(hi.y, point.y),
              std::max(hi.z, point.z)};
      }
      if (face.key.triangle == value.representative_triangle) {
        representative_found = true;
        if (!near(
                add(add(scale(v[0], b.x), scale(v[1], b.y)), scale(v[2], b.z)),
                value.representative_point_rest_metres))
          fail("Boarding representative geometry");
      }
    }
    if (!representative_found || !near(lo, value.bounds_rest_metres[0]) ||
        !near(hi, value.bounds_rest_metres[1]) ||
        std::abs(area - value.surface_area_square_metres) >
            std::max(1e-12, area * 1e-9) ||
        (value.opposite_winding_triangles &&
         *value.opposite_winding_triangles != reversed))
      fail("Boarding derived bounds/area/winding");
    reversed_count += reversed;
    object.candidate_patches.push_back(
        static_cast<std::uint32_t>(result->patches.size()));
    result->patches.push_back(std::move(value));
  }
  if (reference_count != 1474 || reversed_count != 233 || grasp_count != 20 ||
      boot_count != 16 || rod_count != 36 ||
      !std::ranges::all_of(face_used, [](bool x) { return x; }))
    fail("Boarding candidate roster/coverage");
  return result;
}
} // namespace

OriginBoardingSupport::OriginBoardingSupport(std::shared_ptr<const Data> data)
    : data_(std::move(data)) {
}
auto OriginBoardingSupport::groups() const
    -> std::span<const BoardingSupportGroup> {
  return data_ ? std::span<const BoardingSupportGroup>{data_->groups}
               : std::span<const BoardingSupportGroup>{};
}
auto OriginBoardingSupport::objects() const
    -> std::span<const BoardingSupportObject> {
  return data_ ? std::span<const BoardingSupportObject>{data_->objects}
               : std::span<const BoardingSupportObject>{};
}
auto OriginBoardingSupport::patches() const
    -> std::span<const BoardingSupportPatch> {
  return data_ ? std::span<const BoardingSupportPatch>{data_->patches}
               : std::span<const BoardingSupportPatch>{};
}
auto OriginBoardingSupport::selected_faces() const
    -> std::span<const BoardingSelectedFace> {
  return data_ ? std::span<const BoardingSelectedFace>{data_->faces}
               : std::span<const BoardingSelectedFace>{};
}
auto decode_origin_boarding_support(std::string_view input,
                                    const OperatingMotionRecipe& motion)
    -> std::expected<OriginBoardingSupport, std::string> {
  try {
    const auto root = parse_json(input);
    auto data = structural(root);
    if (const auto valid = validate_operating_motion_recipe(motion); !valid)
      return std::unexpected(valid.error());
    // Independent CMake-pinned repository baseline, never a submitted receipt.
    static const auto approved = parse_json(detail::boarding_support_json());
    if (root != approved)
      return std::unexpected(
          "Boarding independently approved source geometry differs");
    data->motion = motion;
    return OriginBoardingSupport{std::move(data)};
  } catch (const std::exception& error) {
    return std::unexpected(std::string("Boarding malformed: ") + error.what());
  }
}
auto lookup_boarding_triangle(const OriginBoardingSupport& catalog,
                              BoardingTriangleKey key)
    -> std::expected<BoardingTriangleAttribution, std::string> {
  if (!catalog.data_) return std::unexpected("Boarding moved-from catalog");
  if (key.group >= catalog.data_->groups.size())
    return std::unexpected("Boarding unknown group");
  const auto& group = catalog.data_->groups[key.group];
  if (key.triangle >= group.triangle_count)
    return std::unexpected("Boarding triangle bound");
  const auto begin = catalog.data_->objects.begin() + group.object_start,
             end = begin + group.object_count;
  const auto found = std::upper_bound(
      begin, end, key.triangle,
      [](std::uint32_t triangle, const BoardingSupportObject& object) {
        return triangle < object.triangle_start;
      });
  const auto& object = *(found - 1);
  BoardingTriangleAttribution result{
      static_cast<std::uint32_t>((found - 1) - catalog.data_->objects.begin()),
      {}};
  for (const auto index : object.candidate_patches) {
    const auto& patch = catalog.data_->patches[index];
    if (std::ranges::any_of(patch.triangle_ranges,
                            [&](BoardingTriangleRange r) {
                              return key.triangle >= r.start &&
                                     key.triangle - r.start < r.count;
                            }))
      result.candidate_patches.push_back(index);
  }
  return result;
}
auto boarding_support_group_transform(const OriginBoardingSupport& catalog,
                                      const OperatingProgress& progress,
                                      std::uint32_t index)
    -> std::expected<OperatingTransform, std::string> {
  if (!catalog.data_) return std::unexpected("Boarding moved-from catalog");
  if (index >= catalog.data_->groups.size())
    return std::unexpected("Boarding unknown group");
  const auto pose = evaluate_operating_motion(catalog.data_->motion, progress);
  if (!pose) return std::unexpected(pose.error());
  const auto& group = catalog.data_->groups[index];
  if (!group.motion_group) return OperatingTransform{};
  return group.owner == BoardingOwner::craft
             ? pose->craft_world_deltas[*group.motion_group]
             : pose->station_contact_deltas[*group.motion_group];
}
auto assess_boarding_candidate_surface(const OriginBoardingSupport& catalog,
                                       const OperatingProgress& progress,
                                       const BoardingCandidateQuery& query)
    -> std::expected<BoardingCandidateSurfaceEvidence, std::string> {
  if (query.patch >= catalog.patches().size())
    return std::unexpected("Boarding unknown patch or moved-from catalog");
  if (static_cast<std::size_t>(query.body_category) >= bodies.size() ||
      !finite(query.point_current_metres) ||
      (query.body_side_probe_current_metres &&
       !finite(*query.body_side_probe_current_metres)))
    return std::unexpected("Boarding body category/point finite bound");
  const auto attribution = lookup_boarding_triangle(catalog, query.triangle);
  if (!attribution) return std::unexpected(attribution.error());
  const auto transform =
      boarding_support_group_transform(catalog, progress, query.triangle.group);
  if (!transform) return std::unexpected(transform.error());
  const auto& patch = catalog.patches()[query.patch];
  BoardingCandidateSurfaceEvidence result;
  result.object = attribution->object;
  result.patch = query.patch;
  result.owner = catalog.groups()[query.triangle.group].owner;
  result.point_rest_metres =
      inverse_point(*transform, query.point_current_metres);
  result.body_category_matches = query.body_category == patch.body_category;
  result.triangle_matches_patch =
      std::ranges::find(attribution->candidate_patches, query.patch) !=
      attribution->candidate_patches.end();
  result.rod_interval = patch.rod ? BoardingPredicateEvidence::not_evaluated
                                  : BoardingPredicateEvidence::undeclared;
  result.declared_side = patch.contact_side_direction_rest
                             ? BoardingSideEvidence::not_evaluated
                             : BoardingSideEvidence::undeclared;
  if (!result.triangle_matches_patch) return result;
  const auto faces = catalog.selected_faces();
  const auto found = std::lower_bound(
      faces.begin(), faces.end(), query.triangle,
      [](const BoardingSelectedFace& face, BoardingTriangleKey key) {
        return std::tie(face.key.group, face.key.triangle) <
               std::tie(key.group, key.triangle);
      });
  if (found == faces.end() || found->key != query.triangle)
    return std::unexpected("Boarding admitted face missing");
  result.point_on_triangle = on_triangle(*found, result.point_rest_metres);
  if (patch.rod) {
    const auto& rod = *patch.rod;
    const auto position =
        dot(subtract(result.point_rest_metres, rod.origin_rest_metres),
            rod.unit_axis_rest);
    result.rod_interval = position >= rod.inclusive_interval_metres[0] -
                                          kBoardingRodToleranceMetres &&
                                  position <= rod.inclusive_interval_metres[1] +
                                                  kBoardingRodToleranceMetres
                              ? BoardingPredicateEvidence::matches
                              : BoardingPredicateEvidence::mismatch;
  }
  if (patch.contact_side_direction_rest) {
    if (!query.body_side_probe_current_metres)
      result.declared_side = BoardingSideEvidence::missing;
    else {
      const auto probe =
          inverse_point(*transform, *query.body_side_probe_current_metres);
      result.declared_side = dot(subtract(probe, result.point_rest_metres),
                                 *patch.contact_side_direction_rest) >
                                     kBoardingSideProjectionMetres
                                 ? BoardingSideEvidence::matches
                                 : BoardingSideEvidence::mismatch;
    }
  }
  return result;
}
} // namespace apsis_drift
