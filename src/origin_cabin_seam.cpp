#include "apsis_drift/origin_cabin_seam.hpp"
#include "apsis_drift/cabin_contact_data.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace apsis_drift {
namespace {
using Json = nlohmann::json;
struct Box {
  RigidVector3 low, high;
};
struct Triangle {
  BoardingTriangleKey key;
  std::uint32_t object{};
  std::array<RigidVector3, 3> points;
  Box bounds;
};
constexpr std::array<std::uint32_t, 4> floor_ids{85129, 85130, 85537, 85538};
constexpr double area_tolerance{1e-12}, sole_area{.12 * .28};
auto fail(std::string_view reason) -> void {
  throw std::runtime_error(std::string(reason));
}
auto keys(const Json& value, std::initializer_list<std::string_view> names)
    -> bool {
  return value.is_object() && value.size() == names.size() &&
         std::ranges::all_of(names, [&](std::string_view k) {
           return value.contains(std::string(k));
         });
}
auto array_size(const Json& value, std::size_t size) -> void {
  if (!value.is_array() || value.size() != size) fail("Cabin array dimensions");
}
auto index(const Json& value, std::size_t maximum) -> std::size_t {
  if (!value.is_number_unsigned() || value.get<std::uint64_t>() > maximum)
    fail("Cabin integer type/bound");
  return value.get<std::size_t>();
}
auto finite(RigidVector3 p) -> bool {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) &&
         std::abs(p.x) <= 128 && std::abs(p.y) <= 128 && std::abs(p.z) <= 128;
}
auto component(RigidVector3 p, std::size_t i) -> double {
  return i == 0 ? p.x : i == 1 ? p.y : p.z;
}
auto add(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
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
auto transform_point(const OperatingTransform& t, RigidVector3 p)
    -> RigidVector3 {
  return add(add(add(scale(t.columns[0], p.x), scale(t.columns[1], p.y)),
                 scale(t.columns[2], p.z)),
             t.columns[3]);
}
auto unite(Box a, Box b) -> Box {
  return {{std::min(a.low.x, b.low.x), std::min(a.low.y, b.low.y),
           std::min(a.low.z, b.low.z)},
          {std::max(a.high.x, b.high.x), std::max(a.high.y, b.high.y),
           std::max(a.high.z, b.high.z)}};
}
auto bounds(const std::array<RigidVector3, 3>& points) -> Box {
  Box result{points[0], points[0]};
  for (const auto p : points)
    result = unite(result, {p, p});
  return result;
}
auto overlapping(Box a, Box b) -> bool {
  return a.high.x >= b.low.x && a.low.x <= b.high.x && a.high.y >= b.low.y &&
         a.low.y <= b.high.y && a.high.z >= b.low.z && a.low.z <= b.high.z;
}
auto reservations(RigidVector3 foot) -> std::array<Box, 3> {
  return {
      Box{add(foot, {-.32, .045, -.32}), add(foot, {.32, .045 + 1.93, .32})},
      Box{add(foot, {-.20, 0, -.14}), add(foot, {-.08, .10, .14})},
      Box{add(foot, {.08, 0, -.14}), add(foot, {.20, .10, .14})}};
}
// Closed-set SAT separates boundary-only contact from triangle entry into the
// open box interior. A zero normal/edge-cross axis carries no separation or
// boundary evidence and must be skipped. No penetration tolerance is applied.
auto intersection(const Triangle& triangle, Box box)
    -> std::optional<CabinIntersection> {
  if (!overlapping(triangle.bounds, box)) return std::nullopt;
  const auto centre = scale(add(box.low, box.high), .5),
             half = scale(sub(box.high, box.low), .5);
  std::array<RigidVector3, 3> p;
  for (std::size_t i = 0; i < 3; ++i)
    p[i] = sub(triangle.points[i], centre);
  const std::array<RigidVector3, 3> edges{sub(p[1], p[0]), sub(p[2], p[1]),
                                          sub(p[0], p[2])};
  constexpr std::array<RigidVector3, 3> box_axes{
      RigidVector3{1, 0, 0}, RigidVector3{0, 1, 0}, RigidVector3{0, 0, 1}};
  std::array<RigidVector3, 13> axes;
  std::copy(box_axes.begin(), box_axes.end(), axes.begin());
  axes[3] = cross(edges[0], edges[1]);
  std::size_t cursor{4};
  for (const auto edge : edges)
    for (const auto axis : box_axes)
      axes[cursor++] = cross(edge, axis);
  bool boundary{};
  for (const auto axis : axes) {
    if (dot(axis, axis) == 0) continue;
    const auto a = dot(p[0], axis), b = dot(p[1], axis), c = dot(p[2], axis);
    const auto lo = std::min({a, b, c}), hi = std::max({a, b, c});
    const auto radius = half.x * std::abs(axis.x) + half.y * std::abs(axis.y) +
                        half.z * std::abs(axis.z);
    if (lo > radius || hi < -radius) return std::nullopt;
    boundary = boundary || lo == radius || hi == -radius;
  }
  return boundary ? CabinIntersection::boundary : CabinIntersection::interior;
}
auto parse(std::string_view input) -> Json {
  if (input.empty() || input.size() > kCabinContactMaximumBytes)
    fail("Cabin contact byte limit");
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
      if (++depth > 16) fail("Cabin contact nesting limit");
    } else if (c == ']' || c == '}') {
      if (depth == 0) fail("Cabin contact unbalanced nesting");
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
  if (duplicate) fail("Cabin duplicate key");
  return result;
}
auto quantized_point(const Json& value) -> RigidVector3 {
  array_size(value, 3);
  std::array<double, 3> parts;
  for (std::size_t i = 0; i < 3; ++i) {
    const auto& v = value[i];
    if (!v.is_number_integer() ||
        (v.is_number_unsigned() && v.get<std::uint64_t>() > 128000000))
      fail("Cabin quantized integer type/bound");
    const auto n = v.get<std::int64_t>();
    if (n < -128000000 || n > 128000000) fail("Cabin quantized integer bound");
    parts[i] = static_cast<double>(n) * 1e-6;
  }
  return {parts[0], parts[1], parts[2]};
}
auto clipped(std::vector<RigidVector3> polygon, std::size_t axis, double limit,
             bool greater) -> std::vector<RigidVector3> {
  std::vector<RigidVector3> result;
  for (std::size_t i = 0; i < polygon.size(); ++i) {
    const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
    const auto da = component(a, axis) - limit, db = component(b, axis) - limit;
    const bool ia = greater ? da >= 0 : da <= 0,
               ib = greater ? db >= 0 : db <= 0;
    if (ia) result.push_back(a);
    if (ia != ib) {
      auto point = add(a, scale(sub(b, a), da / (da - db)));
      if (axis == 0)
        point.x = limit;
      else
        point.z = limit;
      result.push_back(point);
    }
  }
  return result;
}
auto polygon_area(const std::vector<RigidVector3>& points) -> double {
  if (points.size() < 3) return 0;
  double sum{};
  const auto origin = points[0];
  for (std::size_t i = 1; i + 1 < points.size(); ++i) {
    const auto a = sub(points[i], origin), b = sub(points[i + 1], origin);
    sum += a.x * b.z - a.z * b.x;
  }
  return std::abs(sum) * .5;
}
auto turn(RigidVector3 a, RigidVector3 b, RigidVector3 c) -> double {
  return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
}
auto hull(std::vector<RigidVector3> points) -> std::vector<RigidVector3> {
  std::ranges::sort(points, [](RigidVector3 a, RigidVector3 b) {
    return std::tie(a.x, a.z) < std::tie(b.x, b.z);
  });
  points.erase(std::unique(points.begin(), points.end()), points.end());
  if (points.size() < 3) return points;
  std::vector<RigidVector3> result;
  for (const auto p : points) {
    while (result.size() >= 2 &&
           turn(result[result.size() - 2], result.back(), p) <= 0)
      result.pop_back();
    result.push_back(p);
  }
  const auto lower = result.size();
  for (auto i = points.size() - 1; i > 0; --i) {
    const auto p = points[i - 1];
    while (result.size() > lower &&
           turn(result[result.size() - 2], result.back(), p) <= 0)
      result.pop_back();
    result.push_back(p);
  }
  result.pop_back();
  return result;
}
auto valid_center(double z) -> bool {
  return std::isfinite(z) && z >= kCabinSeamMinimumCenterZMetres &&
         z <= kCabinSeamMaximumCenterZMetres;
}
} // namespace
struct OriginCabinSeamGeometry::Data {
  explicit Data(OriginBoardingSupport selected)
      : support(std::move(selected)) {}
  OriginBoardingSupport support;
  std::vector<Triangle> triangles;
  std::array<Triangle, 4> floor;
  Box fixed_crop;
};
OriginCabinSeamGeometry::OriginCabinSeamGeometry(
    std::shared_ptr<const Data> data)
    : data_(std::move(data)) {
}
auto OriginCabinSeamGeometry::support_catalog() const
    -> const OriginBoardingSupport* {
  return data_ ? &data_->support : nullptr;
}
auto decode_origin_cabin_seam_contact(std::string_view input,
                                      const OriginBoardingSupport& support)
    -> std::expected<OriginCabinSeamGeometry, std::string> {
  try {
    if (support.groups().size() != 32)
      fail("Cabin qualified support catalog required");
    const auto root = parse(input);
    if (!keys(root, {"schema_version", "sources", "coordinate_contracts",
                     "quantization_metres", "crop_bounds_metres", "groups"}) ||
        index(root["schema_version"], 1) != 1 ||
        !root["quantization_metres"].is_number() ||
        root["quantization_metres"] != 1e-6)
      fail("Cabin root schema/version/quantization");
    if (!keys(root["sources"], {"station_reference_sha256", "wayfarer_sha256",
                                "station_closure_sha256"}) ||
        root["sources"]["station_reference_sha256"] !=
            "6a3d4cf56af54b8b4d1cc1e344f32651609022280f1c3fb0a9109bf86dfa4fb"
            "6" ||
        root["sources"]["wayfarer_sha256"] !=
            "87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad67"
            "7" ||
        root["sources"]["station_closure_sha256"] !=
            "6a12e1e6846be4de6c89ce0c65b154567a8a07818d37bf574de2a319109319b4")
      fail("Cabin source identities");
    if (!keys(root["coordinate_contracts"], {"craft", "station"}) ||
        root["coordinate_contracts"]["craft"] !=
            "Blender(x,y,z)->(x,z,-y); source-rest world baked" ||
        root["coordinate_contracts"]["station"] !=
            "Blender(x,y,z)->(x-.97,z,-y+.978)")
      fail("Cabin coordinate contracts");
    if (!keys(root["crop_bounds_metres"], {"craft", "station"}))
      fail("Cabin crop schema");
    const auto& crop = root["crop_bounds_metres"];
    const Json expected_craft = {{-1.05, -.2, -3.5}, {1.05, 2.97, 6.35}},
               expected_station = {{-24.8, -1.8, -1.65}, {-18.4, 3.5, 1.65}};
    if (crop["craft"] != expected_craft || crop["station"] != expected_station)
      fail("Cabin crop coverage binding");
    array_size(root["groups"], 32);
    auto data = std::make_shared<OriginCabinSeamGeometry::Data>(support);
    data->fixed_crop = {{-1.05, -.2, -3.5}, {1.05, 2.97, 6.35}};
    data->triangles.reserve(414000);
    std::array<bool, 4> floors{};
    for (std::size_t gi = 0; gi < 32; ++gi) {
      const auto& group = root["groups"][gi];
      const auto& expected = support.groups()[gi];
      if (!keys(group, {"id", "owner", "motion_group", "source_objects",
                        "vertices_micrometres", "triangles"}) ||
          group["id"] != expected.id ||
          group["owner"] !=
              (expected.owner == BoardingOwner::craft ? "craft" : "station"))
        fail("Cabin group roster/owner schema");
      if (!expected.motion_group) {
        if (!group["motion_group"].is_null()) fail("Cabin fixed group motion");
      } else {
        const auto motion =
            expected.owner == BoardingOwner::craft
                ? kOperatingCraftGroupIds[*expected.motion_group]
                : kOperatingStationGroupIds[*expected.motion_group];
        if (group["motion_group"] != motion)
          fail("Cabin moving group relation");
      }
      array_size(group["source_objects"], expected.object_count);
      for (std::size_t oi = 0; oi < expected.object_count; ++oi)
        if (group["source_objects"][oi] !=
            support.objects()[expected.object_start + oi].source_object)
          fail("Cabin exact source object roster");
      array_size(group["vertices_micrometres"], expected.vertex_count);
      array_size(group["triangles"], expected.triangle_count);
      std::vector<RigidVector3> vertices;
      vertices.reserve(expected.vertex_count);
      for (const auto& value : group["vertices_micrometres"])
        vertices.push_back(quantized_point(value));
      const auto transform = boarding_support_group_transform(
          support, kCabinSeamHardware, static_cast<std::uint32_t>(gi));
      if (!transform) fail(transform.error());
      for (std::size_t ti = 0; ti < expected.triangle_count; ++ti) {
        const auto& face = group["triangles"][ti];
        array_size(face, 3);
        std::array<std::size_t, 3> indices;
        for (std::size_t vi = 0; vi < 3; ++vi)
          indices[vi] = index(face[vi], vertices.size() - 1);
        if (indices[0] == indices[1] || indices[0] == indices[2] ||
            indices[1] == indices[2])
          fail("Cabin repeated triangle vertex");
        std::array<RigidVector3, 3> rest{
            vertices[indices[0]], vertices[indices[1]], vertices[indices[2]]};
        const auto normal = cross(sub(rest[1], rest[0]), sub(rest[2], rest[0]));
        if (dot(normal, normal) == 0)
          fail("Cabin degenerate triangle geometry");
        if (expected.owner != BoardingOwner::craft) continue;
        Triangle triangle;
        triangle.key = {static_cast<std::uint32_t>(gi),
                        static_cast<std::uint32_t>(ti)};
        const auto attributed = lookup_boarding_triangle(support, triangle.key);
        if (!attributed) fail(attributed.error());
        triangle.object = attributed->object;
        for (std::size_t vi = 0; vi < 3; ++vi)
          triangle.points[vi] = transform_point(*transform, rest[vi]);
        triangle.bounds = bounds(triangle.points);
        if (expected.id == "craft_fixed")
          for (std::size_t fi = 0; fi < 4; ++fi)
            if (ti == floor_ids[fi]) {
              data->floor[fi] = triangle;
              floors[fi] = true;
            }
        data->triangles.push_back(triangle);
      }
    }
    if (!std::ranges::all_of(floors, [](bool b) { return b; }))
      fail("Cabin missing selected floor tops");
    for (std::size_t i = 0; i < 4; ++i) {
      const auto& floor = data->floor[i];
      const auto& object = support.objects()[floor.object];
      if (object.source_object != (i < 2
                                       ? "CABIN | lift-out walking tile 00-1"
                                       : "CABIN | lift-out walking tile 01-1"))
        fail("Cabin floor source object binding");
      for (const auto p : floor.points)
        if (p.y != kCabinSeamFloorMetres) fail("Cabin exact floor datum");
    }
    // Explicitly establish the two rectangular partitions required by the
    // finite support proof, including each shared diagonal. These are source
    // micrometres, converted by the same arithmetic as admitted vertices.
    const auto q = [](int x, int z) -> RigidVector3 {
      return {static_cast<double>(x) * 1e-6, kCabinSeamFloorMetres,
              static_cast<double>(z) * 1e-6};
    };
    const std::array<std::array<RigidVector3, 3>, 4> rectangles{
        {{q(711000, 3664500), q(-711000, 3664500), q(-711000, 4225500)},
         {q(711000, 3664500), q(-711000, 4225500), q(711000, 4225500)},
         {q(711000, 3069500), q(-711000, 3069500), q(-711000, 3630500)},
         {q(711000, 3069500), q(-711000, 3630500), q(711000, 3630500)}}};
    for (std::size_t i = 0; i < 4; ++i)
      if (data->floor[i].points != rectangles[i])
        fail("Cabin exact rectangular floor partition");
    // Source bytes, not a self-rehashed receipt, are the independent final
    // gate. CMake separately pins the extractor proving only FIXED groups are
    // cropped; every moving group here contains its full admitted source mesh.
    if (!detail::approved_cabin_contact_bytes(input))
      fail("Cabin independently approved contact bytes differ");
    return OriginCabinSeamGeometry{std::move(data)};
  } catch (const std::exception& e) {
    return std::unexpected(std::string("Cabin malformed: ") + e.what());
  }
}
auto assess_origin_cabin_proxy_clearance(
    const OriginCabinSeamGeometry& geometry, RigidVector3 from, RigidVector3 to)
    -> std::expected<CabinProxyClearance, std::string> {
  if (!geometry.data_) return std::unexpected("Cabin moved-from geometry");
  if (!finite(from) || !finite(to))
    return std::unexpected("Cabin finite bounded foot poses required");
  const auto a = reservations(from), b = reservations(to);
  std::array<Box, 3> swept;
  const auto crop = geometry.data_->fixed_crop;
  CabinProxyClearance result;
  result.coverage_complete = true;
  result.interior_clear = true;
  for (std::size_t i = 0; i < 3; ++i) {
    swept[i] = unite(a[i], b[i]);
    for (std::size_t axis = 0; axis < 3; ++axis)
      if (component(swept[i].low, axis) < component(crop.low, axis) ||
          component(swept[i].high, axis) > component(crop.high, axis))
        result.coverage_complete = false;
  }
  if (!result.coverage_complete) {
    result.interior_clear = false;
    return result;
  }
  for (const auto& triangle : geometry.data_->triangles)
    for (std::size_t part = 0; part < 3; ++part)
      if (const auto contact = intersection(triangle, swept[part]); contact) {
        result.contacts.push_back({triangle.key, triangle.object,
                                   static_cast<CabinProxyPart>(part),
                                   *contact});
        if (*contact == CabinIntersection::interior)
          result.interior_clear = false;
      }
  return result;
}
auto assess_origin_cabin_proxy_support(const OriginCabinSeamGeometry& geometry,
                                       RigidVector3 foot)
    -> std::expected<CabinSeamSupport, std::string> {
  if (!geometry.data_) return std::unexpected("Cabin moved-from geometry");
  if (!finite(foot))
    return std::unexpected("Cabin finite bounded sole pose required");
  CabinSeamSupport result;
  if (foot.y != kCabinSeamFloorMetres) return result;
  const auto center = foot.z;
  std::vector<RigidVector3> support_points;
  for (std::size_t sole = 0; sole < 2; ++sole) {
    const auto x = foot.x + (sole == 0 ? -.14 : .14);
    auto& evidence = result.soles[sole];
    for (const auto& triangle : geometry.data_->floor) {
      std::vector<RigidVector3> polygon(triangle.points.begin(),
                                        triangle.points.end());
      polygon = clipped(std::move(polygon), 0, x - .06, true);
      polygon = clipped(std::move(polygon), 0, x + .06, false);
      polygon = clipped(std::move(polygon), 2, center - .14, true);
      polygon = clipped(std::move(polygon), 2, center + .14, false);
      const auto area = polygon_area(polygon);
      evidence.area_square_metres += area;
      if (area > area_tolerance) {
        support_points.insert(support_points.end(), polygon.begin(),
                              polygon.end());
        evidence.pieces.push_back({triangle.key, area, std::move(polygon)});
      }
    }
    evidence.area_fraction = evidence.area_square_metres / sole_area;
  }
  result.support_hull = hull(std::move(support_points));
  result.area_sufficient =
      std::ranges::all_of(result.soles, [](const CabinSoleSupport& s) {
        return s.area_square_metres - area_tolerance >= .75 * sole_area;
      });
  if (result.support_hull.size() >= 3) {
    double margin = 128;
    const RigidVector3 load{foot.x, kCabinSeamFloorMetres, center};
    for (std::size_t i = 0; i < result.support_hull.size(); ++i) {
      const auto a = result.support_hull[i],
                 b = result.support_hull[(i + 1) % result.support_hull.size()];
      const auto edge = sub(b, a);
      const auto length = std::hypot(edge.x, edge.z);
      if (length == 0) continue;
      margin = std::min(margin, turn(a, b, load) / length);
    }
    result.projected_load_margin_metres = margin;
    result.load_supported = margin >= .01;
  }
  return result;
}
auto assess_origin_cabin_seam_support(const OriginCabinSeamGeometry& geometry,
                                      double center)
    -> std::expected<CabinSeamSupport, std::string> {
  if (!valid_center(center))
    return std::unexpected("Cabin seam center outside qualified bounds");
  return assess_origin_cabin_proxy_support(geometry,
                                           {0, kCabinSeamFloorMetres, center});
}
auto certify_origin_cabin_seam_translation(
    const OriginCabinSeamGeometry& geometry, double from, double to)
    -> std::expected<CabinSeamCertificate, std::string> {
  if (!geometry.data_) return std::unexpected("Cabin moved-from geometry");
  if (!valid_center(from) || !valid_center(to))
    return std::unexpected("Cabin translation outside qualified seam bounds");
  CabinSeamCertificate result;
  result.from_center_z_metres = from;
  result.to_center_z_metres = to;
  const auto lo = std::min(from, to), hi = std::max(from, to);
  result.critical_centers_z_metres = {lo, hi};
  // The independently pinned four faces tile two axis-aligned rectangles.
  // Summed clipped area is affine between sole-edge/rectangle-edge events.
  // The load hull is their rectangular support hull; its edge margin minima
  // occur at these same events or their one-sided limits (zero-area pieces
  // carry no load). This complete partition is not a temporal sample grid.
  for (const auto& triangle : geometry.data_->floor)
    for (const auto vertex : triangle.points)
      for (const auto offset : {-.14, .14}) {
        const auto event = vertex.z + offset;
        if (event > lo && event < hi)
          result.critical_centers_z_metres.push_back(event);
      }
  std::ranges::sort(result.critical_centers_z_metres);
  auto& events = result.critical_centers_z_metres;
  events.erase(std::unique(events.begin(), events.end()), events.end());
  result.minimum_sole_area_fraction = 1;
  result.minimum_load_margin_metres = 128;
  result.finite_support = true;
  for (const auto event : events) {
    const auto support = assess_origin_cabin_seam_support(geometry, event);
    if (!support) return std::unexpected(support.error());
    for (const auto& sole : support->soles)
      result.minimum_sole_area_fraction =
          std::min(result.minimum_sole_area_fraction, sole.area_fraction);
    result.minimum_load_margin_metres =
        std::min(result.minimum_load_margin_metres,
                 support->projected_load_margin_metres);
    result.finite_support = result.finite_support && support->area_sufficient &&
                            support->load_supported;
  }
  const auto clearance = assess_origin_cabin_proxy_clearance(
      geometry, {0, kCabinSeamFloorMetres, from},
      {0, kCabinSeamFloorMetres, to});
  if (!clearance) return std::unexpected(clearance.error());
  result.swept_clearance = *clearance;
  result.nonpenetrating_crossing = result.finite_support &&
                                   clearance->coverage_complete &&
                                   clearance->interior_clear;
  // Every boundary touch must be the sole underside, including an original
  // bevel's shared top edge. This does not skip the bevel or the tile object.
  for (const auto& contact : clearance->contacts) {
    if (contact.part == CabinProxyPart::body) {
      result.nonpenetrating_crossing = false;
      continue;
    }
    const auto& tri = *std::ranges::find_if(
        geometry.data_->triangles,
        [&](const Triangle& t) { return t.key == contact.triangle; });
    if (std::ranges::any_of(tri.points, [](RigidVector3 p) {
          return p.y > kCabinSeamFloorMetres;
        }))
      result.nonpenetrating_crossing = false;
    // Prove that the triangle's actual intersection with the sole plane
    // belongs to a selected top rectangle. Below-plane geometry receives no
    // object exemption: the full SAT above has already classified its entry.
    const auto rectangle =
        std::ranges::find_if(geometry.data_->floor, [&](const Triangle& t) {
          return t.object == contact.object;
        });
    if (rectangle == geometry.data_->floor.end()) {
      result.nonpenetrating_crossing = false;
      continue;
    }
    Box top = rectangle->bounds;
    for (const auto& t : geometry.data_->floor)
      if (t.object == contact.object) top = unite(top, t.bounds);
    bool plane_contact{};
    for (const auto p : tri.points)
      if (p.y == kCabinSeamFloorMetres) {
        plane_contact = true;
        if (p.x < top.low.x || p.x > top.high.x || p.z < top.low.z ||
            p.z > top.high.z)
          result.nonpenetrating_crossing = false;
      }
    if (!plane_contact) result.nonpenetrating_crossing = false;
    const auto& source = geometry.data_->support.objects()[contact.object];
    if (source.source_object != "CABIN | lift-out walking tile 00-1" &&
        source.source_object != "CABIN | lift-out walking tile 01-1")
      result.nonpenetrating_crossing = false;
  }
  return result;
}
} // namespace apsis_drift
