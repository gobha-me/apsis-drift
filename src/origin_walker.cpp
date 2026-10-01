#include "apsis_drift/origin_walker.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "apsis_drift/origin_walk_geometry.hpp"

namespace apsis_drift {
namespace {
using Vector = std::array<double, 3>;
using Triangle = std::array<Vector, 3>;
constexpr Vector crop_minimum{-24.5, -.05, -.875};
constexpr Vector crop_maximum{.45, 2.1, .875};
constexpr std::size_t x_bins{13}, z_bins{1};
constexpr double floor_tolerance{.03}, clearance{.045};
constexpr std::string_view source_sha{
    "6a3d4cf56af54b8b4d1cc1e344f32651609022280f1c3fb0a9109bf86dfa4fb6"};
constexpr std::string_view model_sha{
    "79c8303ebe362a619f58a931cf97102cf63cd7bdd36f46a86152368107e0be80"};
struct Bounds {
  Vector low, high;
};
struct Mesh {
  std::vector<Triangle> triangles;
  std::array<std::vector<std::size_t>, x_bins * z_bins> bins;
};
auto subtract(const Vector& a, const Vector& b) -> Vector {
  return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
auto dot(const Vector& a, const Vector& b) -> double {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
auto cross(const Vector& a, const Vector& b) -> Vector {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}
auto bounds(const Triangle& t) -> Bounds {
  Bounds result{t[0], t[0]};
  for (const auto& p : t)
    for (std::size_t i = 0; i < 3; ++i) {
      result.low[i] = std::min(result.low[i], p[i]);
      result.high[i] = std::max(result.high[i], p[i]);
    }
  return result;
}
auto overlaps(const Bounds& a, const Bounds& b) -> bool {
  for (std::size_t i = 0; i < 3; ++i)
    if (a.low[i] > b.high[i] || a.high[i] < b.low[i]) return false;
  return true;
}
auto bin(double coordinate, std::size_t axis) -> std::size_t {
  const auto count = axis == 0 ? x_bins : z_bins;
  const auto value = std::floor((coordinate - crop_minimum[axis]) / 2.0);
  return static_cast<std::size_t>(
      std::clamp(value, 0.0, static_cast<double>(count - 1)));
}
auto read_mesh(std::string_view text) -> std::expected<Mesh, std::string> {
  if (text.empty() || text.size() > std::size_t{16U} * 1024U * 1024U)
    return std::unexpected("Origin contact document byte limit");
  // Reject excessive recursion before the JSON parser allocates nested values.
  // Quoted braces and escaped quotes are data, not nesting delimiters.
  std::size_t depth{};
  bool quoted{}, escaped{};
  for (const auto character : text) {
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (character == '\\')
        escaped = true;
      else if (character == '"')
        quoted = false;
    } else if (character == '"')
      quoted = true;
    else if (character == '{' || character == '[') {
      if (++depth > 8) return std::unexpected("Origin contact nesting limit");
    } else if (character == '}' || character == ']') {
      if (depth == 0)
        return std::unexpected("Origin contact unbalanced nesting");
      --depth;
    }
  }
  try {
    bool duplicate = false;
    std::vector<std::vector<std::string>> keys;
    auto callback = [&](int, nlohmann::json::parse_event_t event,
                        nlohmann::json& value) {
      if (event == nlohmann::json::parse_event_t::object_start)
        keys.emplace_back();
      else if (event == nlohmann::json::parse_event_t::object_end)
        keys.pop_back();
      else if (event == nlohmann::json::parse_event_t::key && !keys.empty()) {
        const auto key = value.get<std::string>();
        if (std::ranges::find(keys.back(), key) != keys.back().end())
          duplicate = true;
        keys.back().push_back(key);
      }
      return true;
    };
    const auto json = nlohmann::json::parse(text, callback);
    if (duplicate || !json.is_object() || json.size() != 8 ||
        json.at("schema_version") != 1 ||
        json.at("source_sha256") != source_sha ||
        json.at("model_sha256") != model_sha ||
        json.at("quantization_metres") != .000001 ||
        json.at("crop_minimum_metres") != crop_minimum ||
        json.at("crop_maximum_metres") != crop_maximum)
      return std::unexpected("Origin contact identity/schema mismatch");
    const auto& vertices = json.at("vertices_micrometres");
    const auto& faces = json.at("triangles");
    if (!vertices.is_array() || vertices.empty() || vertices.size() > 400000 ||
        !faces.is_array() || faces.empty() || faces.size() > 200000)
      return std::unexpected("Origin contact buffer dimensions");
    std::vector<Vector> points;
    points.reserve(vertices.size());
    for (const auto& row : vertices) {
      if (!row.is_array() || row.size() != 3)
        return std::unexpected("Origin contact vertex dimensions");
      Vector point{};
      for (std::size_t i = 0; i < 3; ++i) {
        if (!row[i].is_number_integer())
          return std::unexpected("Origin contact vertex type");
        const auto value = row[i].get<double>();
        if (!std::isfinite(value) || std::abs(value) > 1e9)
          return std::unexpected("Origin contact vertex range");
        point[i] = value * .000001;
      }
      points.push_back(point);
    }
    Mesh mesh;
    mesh.triangles.reserve(faces.size());
    for (const auto& row : faces) {
      if (!row.is_array() || row.size() != 3)
        return std::unexpected("Origin contact triangle dimensions");
      Triangle triangle{};
      std::array<std::size_t, 3> ids{};
      for (std::size_t i = 0; i < 3; ++i) {
        if (!row[i].is_number_unsigned())
          return std::unexpected("Origin contact index type");
        const auto index = row[i].get<std::uint64_t>();
        if (index >= points.size())
          return std::unexpected("Origin contact index boundary");
        ids[i] = static_cast<std::size_t>(index);
        triangle[i] = points[ids[i]];
      }
      if (ids[0] == ids[1] || ids[0] == ids[2] || ids[1] == ids[2])
        return std::unexpected("Origin contact repeated triangle index");
      const auto b = bounds(triangle);
      if (!overlaps(b, {crop_minimum, crop_maximum}))
        return std::unexpected("Origin contact triangle outside crop");
      const auto id = mesh.triangles.size();
      mesh.triangles.push_back(triangle);
      for (auto x = bin(b.low[0], 0); x <= bin(b.high[0], 0); ++x)
        for (auto z = bin(b.low[2], 2); z <= bin(b.high[2], 2); ++z)
          mesh.bins[x * z_bins + z].push_back(id);
    }
    return mesh;
  } catch (const nlohmann::json::exception&) {
    return std::unexpected("Malformed Origin contact document");
  }
}
auto geometry() -> const std::expected<Mesh, std::string>& {
  static const auto mesh = read_mesh(kOriginWalkGeometryJson);
  return mesh;
}
auto candidates(const Mesh& mesh, const Bounds& box)
    -> std::vector<std::size_t> {
  std::vector<std::size_t> result;
  for (auto x = bin(box.low[0], 0); x <= bin(box.high[0], 0); ++x)
    for (auto z = bin(box.low[2], 2); z <= bin(box.high[2], 2); ++z) {
      const auto& entries = mesh.bins[x * z_bins + z];
      result.insert(result.end(), entries.begin(), entries.end());
    }
  std::ranges::sort(result);
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}
// Triangle/box SAT checks face normal, box axes and nine edge cross axes.
// The box is the complete swept standing reservation, conservatively covering
// diagonal movement too; no endpoint-only collision test or tunneling.
auto intersects(const Triangle& triangle, const Bounds& box) -> bool {
  if (!overlaps(bounds(triangle), box)) return false;
  Vector centre{}, half{};
  for (std::size_t i = 0; i < 3; ++i) {
    centre[i] = (box.low[i] + box.high[i]) * .5;
    half[i] = (box.high[i] - box.low[i]) * .5;
  }
  Triangle t{};
  for (std::size_t i = 0; i < 3; ++i)
    t[i] = subtract(triangle[i], centre);
  auto separated = [&](const Vector& axis) {
    const auto a = dot(t[0], axis), b = dot(t[1], axis), c = dot(t[2], axis);
    const auto radius = half[0] * std::abs(axis[0]) +
                        half[1] * std::abs(axis[1]) +
                        half[2] * std::abs(axis[2]);
    return std::min({a, b, c}) > radius || std::max({a, b, c}) < -radius;
  };
  const std::array<Vector, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  const std::array<Vector, 3> edges{subtract(t[1], t[0]), subtract(t[2], t[1]),
                                    subtract(t[0], t[2])};
  for (const auto& axis : axes)
    if (separated(axis)) return false;
  if (separated(cross(edges[0], edges[1]))) return false;
  for (const auto& edge : edges)
    for (const auto& axis : axes)
      if (separated(cross(edge, axis))) return false;
  return true;
}
auto standing(const RigidVector3& foot) -> Bounds {
  return {{foot.x - kOriginWalkerHalfWidthMetres, foot.y + clearance,
           foot.z - kOriginWalkerHalfWidthMetres},
          {foot.x + kOriginWalkerHalfWidthMetres,
           foot.y + clearance + kOriginWalkerHeightMetres,
           foot.z + kOriginWalkerHalfWidthMetres}};
}
auto supported(const Mesh& mesh, const RigidVector3& foot) -> bool {
  // All four full reservation corners and its centre must have real nearby
  // horizontal source floor. This blocks the open shaft rather than spanning
  // it.
  for (const auto& offset : std::array<std::array<double, 2>, 5>{
           {{-.32, -.32}, {-.32, .32}, {.32, -.32}, {.32, .32}, {0, 0}}}) {
    const auto x = foot.x + offset[0], z = foot.z + offset[1];
    const Bounds query{{x, foot.y - floor_tolerance, z},
                       {x, foot.y + floor_tolerance, z}};
    bool found = false;
    for (const auto id : candidates(mesh, query)) {
      const auto& t = mesh.triangles[id];
      if (!overlaps(bounds(t), query)) continue;
      const auto e1 = subtract(t[1], t[0]), e2 = subtract(t[2], t[0]);
      const auto normal = cross(e1, e2);
      const auto length = std::sqrt(dot(normal, normal));
      if (length == 0 || std::abs(normal[1]) < .95 * length) continue;
      const auto denominator = e1[0] * e2[2] - e1[2] * e2[0];
      if (denominator == 0) continue;
      const auto dx = x - t[0][0], dz = z - t[0][2];
      const auto u = (dx * e2[2] - dz * e2[0]) / denominator;
      const auto v = (e1[0] * dz - e1[2] * dx) / denominator;
      if (u < -1e-9 || v < -1e-9 || u + v > 1 + 1e-9) continue;
      const auto y = t[0][1] + u * e1[1] + v * e2[1];
      if (std::abs(y - foot.y) <= floor_tolerance) {
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  return true;
}
auto clear(const Mesh& mesh, const Bounds& box) -> bool {
  for (const auto id : candidates(mesh, box))
    if (intersects(mesh.triangles[id], box)) return false;
  return true;
}
auto finite(const RigidVector3& v) -> bool {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
auto in_domain(const RigidVector3& foot) -> bool {
  const auto b = standing(foot);
  for (std::size_t i = 0; i < 3; ++i)
    if (b.low[i] < crop_minimum[i] || b.high[i] > crop_maximum[i]) return false;
  return true;
}
} // namespace

auto validate_origin_walk_geometry_json(std::string_view text)
    -> std::expected<void, std::string> {
  const auto mesh = read_mesh(text);
  if (!mesh) return std::unexpected(mesh.error());
  return {};
}
auto validate_origin_walker(const OriginWalkerState& state)
    -> std::expected<void, std::string> {
  if (state.geometry_version != kOriginWalkGeometryVersion ||
      state.actor_id != 1)
    return std::unexpected("Unsupported Origin actor identity/geometry");
  if (!finite(state.foot_position_metres) ||
      !finite(state.velocity_metres_per_second) ||
      !std::isfinite(state.heading_radians) ||
      std::abs(state.heading_radians) > std::numbers::pi ||
      state.velocity_metres_per_second.y != 0 ||
      std::hypot(state.velocity_metres_per_second.x,
                 state.velocity_metres_per_second.z) >
          kOriginWalkSpeedMetresPerSecond + 1e-12 ||
      !in_domain(state.foot_position_metres))
    return std::unexpected("Invalid Origin walking state");
  const auto& mesh = geometry();
  if (!mesh) return std::unexpected(mesh.error());
  if (!supported(*mesh, state.foot_position_metres) ||
      !clear(*mesh, standing(state.foot_position_metres)))
    return std::unexpected("Origin actor lacks clear supported floor");
  return {};
}
auto advance_origin_walker(const OriginWalkerState& state,
                           const OriginWalkControls& controls,
                           SimulationSeconds step)
    -> std::expected<OriginWalkerState, std::string> {
  if (auto valid = validate_origin_walker(state); !valid)
    return std::unexpected(valid.error());
  if (step != kSimulationStep || !std::isfinite(controls.forward) ||
      !std::isfinite(controls.right) || std::abs(controls.forward) > 1 ||
      std::abs(controls.right) > 1 ||
      !std::isfinite(controls.heading_radians) ||
      std::abs(controls.heading_radians) > std::numbers::pi)
    return std::unexpected("Invalid fixed-step Origin walking command");
  auto result = state;
  result.heading_radians = controls.heading_radians;
  const auto length =
      std::max(1.0, std::hypot(controls.forward, controls.right));
  const auto forward = controls.forward / length,
             right = controls.right / length;
  const auto sine = std::sin(controls.heading_radians),
             cosine = std::cos(controls.heading_radians);
  const auto vx =
      kOriginWalkSpeedMetresPerSecond * (-sine * forward + cosine * right);
  const auto vz =
      kOriginWalkSpeedMetresPerSecond * (-cosine * forward - sine * right);
  const RigidVector3 next{state.foot_position_metres.x + vx * step.count(),
                          state.foot_position_metres.y,
                          state.foot_position_metres.z + vz * step.count()};
  auto swept = standing(state.foot_position_metres);
  const auto destination = standing(next);
  for (std::size_t i = 0; i < 3; ++i) {
    swept.low[i] = std::min(swept.low[i], destination.low[i]);
    swept.high[i] = std::max(swept.high[i], destination.high[i]);
  }
  const auto& mesh = *geometry();
  if (in_domain(next) && supported(mesh, next) && clear(mesh, swept)) {
    result.foot_position_metres = next;
    result.velocity_metres_per_second = {vx == 0 ? 0 : vx, 0, vz == 0 ? 0 : vz};
  } else
    result.velocity_metres_per_second = {};
  return result;
}
} // namespace apsis_drift
