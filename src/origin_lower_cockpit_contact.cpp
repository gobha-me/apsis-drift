#include "apsis_drift/origin_lower_cockpit_contact.hpp"
#include "apsis_drift/lower_cockpit_contact_data.hpp"
#include "origin_cabin_contact_internal.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
namespace apsis_drift {
namespace {
using Json = nlohmann::json;
using Box = detail::CabinContactBox;
using Obstacle = detail::CabinContactObstacle;
using Access = detail::CabinContactAccess;
constexpr Box original_box{{-1.05, -.2, -3.5}, {1.05, 2.97, 6.35}};
constexpr Box lower_box{{-1.05, -.85, -3.5}, {1.05, -.2, -.5}};
auto fail(std::string_view message) -> void {
  throw std::runtime_error(std::string(message));
}
auto parse(std::string_view bytes, std::size_t maximum) -> Json {
  if (bytes.empty() || bytes.size() > maximum) fail("Lower contact byte bound");
  std::size_t depth{};
  bool quoted{}, escaped{};
  for (auto c : bytes) {
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
    } else if (c == '"')
      quoted = true;
    else if (c == '{' || c == '[') {
      if (++depth > 16) fail("Lower contact nesting bound");
    } else if (c == '}' || c == ']') {
      if (depth == 0) fail("Lower contact unbalanced buffer");
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
  auto result = Json::parse(bytes, callback);
  if (duplicate) fail("Lower contact duplicate key");
  return result;
}
auto approved_halo() -> const Json& {
  static const Json approved = [] {
    std::string bytes;
    for (auto chunk : detail::kLowerContactChunks)
      bytes.append(chunk);
    return parse(bytes, kLowerCockpitMaximumDocumentBytes);
  }();
  return approved;
}
// The independently compiled source selects a closed, bounded schema, including
// the six optional archived region lists. Values still undergo the semantic and
// geometry validators below before the final exact byte-equivalence gate.
auto shape(const Json& value, const Json& approved) -> void {
  if (approved.is_object()) {
    if (!value.is_object() || value.size() != approved.size())
      fail("Lower closed object schema");
    for (auto it = approved.begin(); it != approved.end(); ++it) {
      if (!value.contains(it.key())) fail("Lower unknown/missing field");
      shape(value.at(it.key()), it.value());
    }
  } else if (approved.is_array()) {
    if (!value.is_array() || value.size() != approved.size())
      fail("Lower array dimensions");
    for (std::size_t i = 0; i < approved.size(); ++i)
      shape(value[i], approved[i]);
  } else if (approved.is_number_integer()) {
    if (!value.is_number_integer()) fail("Lower integer type");
  } else if (approved.is_number_float()) {
    if (!value.is_number() || !std::isfinite(value.get<double>()))
      fail("Lower finite numeric type");
  } else if (approved.is_string()) {
    if (!value.is_string() || value.get_ref<const std::string&>().size() > 4096)
      fail("Lower source string bound");
  } else if (value.type() != approved.type())
    fail("Lower scalar type");
}
auto index(const Json& value, std::size_t maximum) -> std::uint32_t {
  if (!value.is_number_unsigned() || value.get<std::uint64_t>() > maximum)
    fail("Lower unsigned index/range bound");
  return value.get<std::uint32_t>();
}
auto number(const Json& value) -> double {
  if (!value.is_number() || !std::isfinite(value.get<double>()) ||
      std::abs(value.get<double>()) > 128)
    fail("Lower finite provenance bound");
  return value.get<double>();
}
auto point(const Json& value) -> RigidVector3 {
  if (!value.is_array() || value.size() != 3) fail("Lower point dimensions");
  return {number(value[0]), number(value[1]), number(value[2])};
}
auto subtract(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto dot(RigidVector3 a, RigidVector3 b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto cross(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto affine(const Json& rows) -> void {
  if (!rows.is_array() || rows.size() != 4) fail("Lower affine rows");
  std::array<RigidVector3, 3> linear;
  for (std::size_t i = 0; i < 4; ++i) {
    if (!rows[i].is_array() || rows[i].size() != 4)
      fail("Lower affine dimensions");
    for (std::size_t j = 0; j < 4; ++j)
      static_cast<void>(number(rows[i][j]));
    if (i < 3)
      linear[i] = {number(rows[i][0]), number(rows[i][1]), number(rows[i][2])};
  }
  if (rows[3] != Json::array({0.0, 0.0, 0.0, 1.0}) ||
      dot(linear[0], cross(linear[1], linear[2])) == 0)
    fail("Lower nonsingular affine provenance");
  // Legitimate source scale is retained; this matrix is never reapplied.
}
auto check_bounds(const Json& bounds) -> void {
  const auto low = point(bounds[0]), high = point(bounds[1]);
  if (low.x > high.x || low.y > high.y || low.z > high.z)
    fail("Lower provenance bounds ordering");
}
auto union_covers(Box b) -> bool {
  // Closed comparisons use the original declared decimal-metre domain, with
  // the identical shared join in both boxes. There is no extent-growth epsilon.
  if (b.low.x < original_box.low.x || b.high.x > original_box.high.x ||
      b.low.z < original_box.low.z || b.high.z > original_box.high.z ||
      b.high.y > original_box.high.y || b.low.y < lower_box.low.y)
    return false;
  // Only a nonempty portion below the join requires the lower Z restriction.
  return b.low.y >= original_box.low.y || b.high.z <= lower_box.high.z;
}
struct FaceCoordinates {
  std::array<std::int64_t, 9> coordinates;
  friend auto operator==(const FaceCoordinates&, const FaceCoordinates&)
      -> bool = default;
};
struct FaceHash {
  auto operator()(const FaceCoordinates& face) const -> std::size_t {
    std::size_t h{};
    for (auto v : face.coordinates)
      h ^= std::hash<std::int64_t>{}(v) + 0x9e3779b9U + (h << 6) + (h >> 2);
    return h;
  }
};
auto coordinates(const std::array<RigidVector3, 3>& p) -> FaceCoordinates {
  auto sorted = p;
  std::ranges::sort(sorted, [](RigidVector3 a, RigidVector3 b) {
    return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
  });
  FaceCoordinates result;
  std::size_t i{};
  for (auto v : sorted)
    for (auto component : {v.x, v.y, v.z})
      result.coordinates[i++] = std::llround(component * 1e6);
  return result;
}
auto overlaps(Box a, Box b) -> bool {
  return a.low.x <= b.high.x && a.high.x >= b.low.x && a.low.y <= b.high.y &&
         a.high.y >= b.low.y && a.low.z <= b.high.z && a.high.z >= b.low.z;
}
} // namespace
struct OriginLowerCockpitContact::Data {
  explicit Data(OriginCabinSeamGeometry selected)
      : original(std::move(selected)) {}
  OriginCabinSeamGeometry original;
  std::vector<LowerCockpitSourceObject> objects;
  std::vector<Obstacle> triangles;
};
OriginLowerCockpitContact::OriginLowerCockpitContact(
    std::shared_ptr<const Data> selected)
    : data_(std::move(selected)) {
}
auto OriginLowerCockpitContact::objects() const
    -> std::span<const LowerCockpitSourceObject> {
  return data_ ? std::span<const LowerCockpitSourceObject>{data_->objects}
               : std::span<const LowerCockpitSourceObject>{};
}
auto OriginLowerCockpitContact::original_geometry() const
    -> const OriginCabinSeamGeometry* {
  return data_ ? &data_->original : nullptr;
}
auto decode_origin_lower_cockpit_contact(
    std::string_view bytes, std::string_view policy_bytes,
    const OriginCabinSeamGeometry& original)
    -> std::expected<OriginLowerCockpitContact, std::string> {
  try {
    if (!original.support_catalog() || Access::obstacles(original).empty())
      fail("Lower qualified original geometry required");
    const auto policy = parse(policy_bytes, kLowerCockpitMaximumPolicyBytes);
    const auto approved_policy =
        parse(detail::kLowerContactPolicyJson, kLowerCockpitMaximumPolicyBytes);
    shape(policy, approved_policy);
    if (policy != approved_policy)
      fail("Lower independent policy identities/domain/ownership binding");
    const auto root = parse(bytes, kLowerCockpitMaximumDocumentBytes);
    const auto& approved = approved_halo();
    shape(root, approved);
    for (const auto key : {"schema_version", "id", "contact_sha256", "sources",
                           "coordinate_contract", "quantization_metres",
                           "query_bounds_rest_m", "motion_contract", "policy"})
      if (root[key] != approved[key])
        fail("Lower source identity/coordinate/domain binding");
    const auto& corrections = root["derivative_corrections_baked_once"];
    if (index(corrections["schema_version"], 1) != 1)
      fail("Lower corrections version");
    for (const auto& operation : corrections["operations"]) {
      for (const auto& source : operation["source_objects"]) {
        for (const auto name : {"original_rest_local_transform",
                                "operating_rest_local_transform"})
          for (const auto& column : source[name])
            static_cast<void>(point(column));
        static_cast<void>(point(source["local_translation_metres"]));
      }
      for (const auto& value : operation["parameters"])
        static_cast<void>(number(value));
    }
    auto data = std::make_shared<OriginLowerCockpitContact::Data>(original);
    const auto& support = *original.support_catalog();
    std::vector<RigidVector3> vertices;
    vertices.reserve(4598);
    for (const auto& vertex : root["vertices_micrometres"]) {
      std::array<double, 3> v;
      for (std::size_t a = 0; a < 3; ++a) {
        const auto& n = vertex[a];
        if (!n.is_number_integer() ||
            (n.is_number_unsigned() && n.get<std::uint64_t>() > 128000000))
          fail("Lower quantized integer coordinate bound/type");
        const auto q = n.get<std::int64_t>();
        if (q < -128000000 || q > 128000000)
          fail("Lower quantized coordinate bound");
        v[a] = static_cast<double>(q) * 1e-6;
      }
      vertices.push_back({v[0], v[1], v[2]});
    }
    std::set<std::string> names;
    std::uint32_t cursor{};
    for (std::size_t oi = 0; oi < 75; ++oi) {
      const auto& object = root["objects"][oi];
      const auto& expected = approved["objects"][oi];
      if (object["owner"] != "craft" || !object["motion_group"].is_null())
        fail("Lower static craft ownership");
      const auto name = object["source_object"].get<std::string>();
      if (!names.insert(name).second ||
          object["source_object"] != expected["source_object"])
        fail("Lower exact unique source object");
      LowerCockpitSourceObject output;
      output.source_object = name;
      output.source_evaluated_triangles =
          index(object["source_evaluated_triangles"], 1000000);
      if (output.source_evaluated_triangles == 0)
        fail("Lower nonzero source face count");
      const auto retained = index(object["crop_retained_triangles"],
                                  output.source_evaluated_triangles);
      output.triangle_start = index(object["halo_triangle_start"], 8100);
      output.triangle_count =
          index(object["halo_triangle_count"], 8100 - output.triangle_start);
      if (output.triangle_start != cursor || output.triangle_count == 0)
        fail("Lower gap-free object partition");
      if (output.triangle_count > output.source_evaluated_triangles - retained)
        fail("Lower disjoint retained/source count");
      cursor += output.triangle_count;
      if (object["evaluated_source_triangle_indices"].size() !=
          output.triangle_count)
        fail("Lower source face count");
      std::optional<std::uint32_t> prior;
      for (const auto& value : object["evaluated_source_triangle_indices"]) {
        const auto source = index(value, output.source_evaluated_triangles - 1);
        if (prior && source <= *prior)
          fail("Lower ordered unique source face indices");
        output.evaluated_source_triangles.push_back(source);
        prior = source;
      }
      if (object["evaluated_source_triangle_indices"] !=
          expected["evaluated_source_triangle_indices"])
        fail("Lower exact source face attribution");
      if (object.contains("evaluated_source_triangle_indices_in_region")) {
        std::uint32_t end{};
        for (const auto& region :
             object["evaluated_source_triangle_indices_in_region"]) {
          const auto start =
              index(region[0], output.source_evaluated_triangles);
          const auto count =
              index(region[1], output.source_evaluated_triangles - start);
          if (start < end || count == 0) fail("Lower source region range");
          end = start + count;
        }
      }
      affine(object["source_corrected_world_rows"]);
      check_bounds(object["bounds_corrected_rest_m"]);
      check_bounds(object["halo_bounds_rest_m"]);
      const auto found = std::ranges::find_if(
          support.objects(), [&](const BoardingSupportObject& o) {
            return o.source_object == name;
          });
      const auto& range = object["admitted_range"];
      if (range.is_null()) {
        if (retained != 0 || found != support.objects().end())
          fail("Lower missing original source range");
      } else {
        if (found == support.objects().end() || found->group != 0 ||
            support.groups()[found->group].owner != BoardingOwner::craft ||
            support.groups()[found->group].motion_group ||
            range["group"] != "craft_fixed" ||
            range["exact_coordinate_match"] != true ||
            index(range["triangle_start"],
                  support.groups()[0].triangle_count) !=
                found->triangle_start ||
            index(range["triangle_count"],
                  support.groups()[0].triangle_count) !=
                found->triangle_count ||
            retained != found->triangle_count)
          fail("Lower exact original fixed ownership/range");
        output.original_object =
            static_cast<std::uint32_t>(found - support.objects().begin());
      }
      data->objects.push_back(std::move(output));
    }
    if (cursor != 8100) fail("Lower complete object partition");
    std::unordered_set<FaceCoordinates, FaceHash> added;
    data->triangles.reserve(8100);
    std::size_t object_index{};
    for (std::size_t ti = 0; ti < 8100; ++ti) {
      while (ti >= data->objects[object_index].triangle_start +
                       data->objects[object_index].triangle_count)
        ++object_index;
      const auto& face = root["triangles"][ti];
      const std::array<std::uint32_t, 3> vi{
          index(face[0], vertices.size() - 1),
          index(face[1], vertices.size() - 1),
          index(face[2], vertices.size() - 1)};
      if (vi[0] == vi[1] || vi[0] == vi[2] || vi[1] == vi[2])
        fail("Lower repeated triangle index");
      Obstacle triangle;
      triangle.key = {0, static_cast<std::uint32_t>(ti)};
      triangle.object = static_cast<std::uint32_t>(object_index);
      for (std::size_t i = 0; i < 3; ++i)
        triangle.points[i] = vertices[vi[i]];
      const auto normal =
          cross(subtract(triangle.points[1], triangle.points[0]),
                subtract(triangle.points[2], triangle.points[0]));
      if (dot(normal, normal) == 0) fail("Lower nondegenerate geometry");
      triangle.bounds = Access::bounds(triangle.points);
      // Use exact integer-micrometre selection comparisons: quantization does
      // not expand either source crop. Complete outlying triangles add no
      // coverage.
      Box quantized_original{{-1050000, -200000, -3500000},
                             {1050000, 2970000, 6350000}},
          quantized_lower{{-1050000, -850000, -3500000},
                          {1050000, -200000, -500000}};
      std::array<RigidVector3, 3> qp;
      for (std::size_t i = 0; i < 3; ++i) {
        const auto& v = root["vertices_micrometres"][vi[i]];
        qp[i] = {v[0].get<double>(), v[1].get<double>(), v[2].get<double>()};
      }
      const auto qb = Access::bounds(qp);
      if (overlaps(qb, quantized_original) || !overlaps(qb, quantized_lower))
        fail("Lower static selection/old crop overlap");
      if (!added.insert(coordinates(triangle.points)).second)
        fail("Lower duplicate geometry");
      data->triangles.push_back(triangle);
    }
    for (const auto& old : Access::obstacles(original))
      if (old.key.group == 0 && added.contains(coordinates(old.points)))
        fail("Lower duplicated original source face");
    if (!detail::approved_lower_contact_bytes(bytes) ||
        policy_bytes != detail::kLowerContactPolicyJson)
      fail("Lower independently approved bytes differ");
    return OriginLowerCockpitContact{std::move(data)};
  } catch (const std::exception& e) {
    return std::unexpected(std::string("Lower malformed: ") + e.what());
  }
}
auto lookup_lower_cockpit_face(const OriginLowerCockpitContact& geometry,
                               LowerCockpitTriangleKey key)
    -> std::expected<LowerCockpitFace, std::string> {
  if (!geometry.data_) return std::unexpected("Lower moved-from geometry");
  const Obstacle* triangle{};
  LowerCockpitFace result;
  result.key = key;
  if (key.buffer == LowerCockpitContactBuffer::halo) {
    if (key.group != 0 || key.triangle >= geometry.data_->triangles.size())
      return std::unexpected("Lower halo buffer key bound");
    triangle = &geometry.data_->triangles[key.triangle];
    const auto& object = geometry.data_->objects[triangle->object];
    result.source_object = object.source_object;
    result.evaluated_source_triangle =
        object.evaluated_source_triangles[key.triangle - object.triangle_start];
  } else if (key.buffer == LowerCockpitContactBuffer::original) {
    const auto obstacles = Access::obstacles(geometry.data_->original);
    const auto found = std::ranges::find_if(obstacles, [&](const Obstacle& t) {
      return t.key == BoardingTriangleKey{key.group, key.triangle};
    });
    if (found == obstacles.end())
      return std::unexpected("Lower original craft buffer key bound");
    triangle = &*found;
    result.source_object = geometry.data_->original.support_catalog()
                               ->objects()[triangle->object]
                               .source_object;
  } else
    return std::unexpected("Lower unknown buffer namespace");
  result.object = triangle->object;
  result.points_current_metres = triangle->points;
  const auto n = cross(subtract(triangle->points[1], triangle->points[0]),
                       subtract(triangle->points[2], triangle->points[0]));
  const auto length = std::sqrt(dot(n, n));
  result.unit_normal_current = {n.x / length, n.y / length, n.z / length};
  return result;
}
auto assess_lower_cockpit_reservations(
    const OriginLowerCockpitContact& geometry, RigidVector3 from,
    RigidVector3 to)
    -> std::expected<LowerCockpitReservationEvidence, std::string> {
  if (!geometry.data_) return std::unexpected("Lower moved-from geometry");
  if (!Access::finite(from) || !Access::finite(to))
    return std::unexpected("Lower finite bounded foot poses required");
  const auto a = Access::reservations(from), b = Access::reservations(to);
  std::array<Box, 3> swept;
  LowerCockpitReservationEvidence result;
  result.coverage_complete = true;
  result.interior_clear = true;
  for (std::size_t i = 0; i < 3; ++i) {
    swept[i] = Access::unite(a[i], b[i]);
    if (!union_covers(swept[i])) result.coverage_complete = false;
  }
  if (!result.coverage_complete) {
    result.interior_clear = false;
    return result;
  }
  const auto collect = [&](std::span<const Obstacle> obstacles,
                           LowerCockpitContactBuffer buffer) {
    for (const auto& triangle : obstacles)
      for (std::size_t i = 0; i < 3; ++i)
        if (auto contact = Access::intersection(triangle, swept[i])) {
          std::optional<std::uint32_t> source;
          if (buffer == LowerCockpitContactBuffer::halo) {
            const auto& o = geometry.data_->objects[triangle.object];
            source = o.evaluated_source_triangles[triangle.key.triangle -
                                                  o.triangle_start];
          }
          result.contacts.push_back(
              {{buffer, triangle.key.group, triangle.key.triangle},
               triangle.object,
               source,
               static_cast<CabinProxyPart>(i),
               *contact});
          if (*contact == CabinIntersection::interior)
            result.interior_clear = false;
        }
  };
  collect(Access::obstacles(geometry.data_->original),
          LowerCockpitContactBuffer::original);
  collect(geometry.data_->triangles, LowerCockpitContactBuffer::halo);
  return result;
}
auto assess_lower_cockpit_surface_point(
    const OriginLowerCockpitContact& geometry, LowerCockpitTriangleKey key,
    RigidVector3 point_current)
    -> std::expected<LowerCockpitSurfaceEvidence, std::string> {
  if (!Access::finite(point_current))
    return std::unexpected("Lower finite bounded surface point required");
  auto face = lookup_lower_cockpit_face(geometry, key);
  if (!face) return std::unexpected(face.error());
  LowerCockpitSurfaceEvidence result;
  result.face = *face;
  const auto a = face->points_current_metres[0],
             u = subtract(face->points_current_metres[1], a),
             v = subtract(face->points_current_metres[2], a),
             p = subtract(point_current, a);
  result.signed_plane_distance_metres = dot(p, face->unit_normal_current);
  const auto n = cross(u, v);
  const auto det = dot(n, n), uu = dot(u, u), vv = dot(v, v);
  if (!std::isfinite(det) || det <= 0)
    return std::unexpected("Lower numerically unresolved surface determinant");
  const auto b = dot(cross(p, v), n) / det, c = dot(cross(u, p), n) / det;
  if (!std::isfinite(b) || !std::isfinite(c) || !std::isfinite(1 - b - c) ||
      !std::isfinite(result.signed_plane_distance_metres))
    return std::unexpected("Lower nonfinite surface evidence");
  result.barycentric = {1 - b - c, b, c};
  const auto twice_area = std::sqrt(dot(cross(u, v), cross(u, v)));
  const std::array<double, 3> tolerance{
      kBoardingSurfaceToleranceMetres *
          std::sqrt(dot(subtract(v, u), subtract(v, u))) / twice_area,
      kBoardingSurfaceToleranceMetres * std::sqrt(vv) / twice_area,
      kBoardingSurfaceToleranceMetres * std::sqrt(uu) / twice_area};
  result.point_on_face = std::abs(result.signed_plane_distance_metres) <=
                             kBoardingSurfaceToleranceMetres &&
                         result.barycentric.x >= -tolerance[0] &&
                         b >= -tolerance[1] && c >= -tolerance[2];
  return result;
}
} // namespace apsis_drift
