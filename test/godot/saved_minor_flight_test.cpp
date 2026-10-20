#include "apsis_drift/godot/saved_minor_flight.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace apsis_drift;
using namespace apsis_drift::godot_spike;
using V = RigidVector3;
int checks{}, failures{};
auto check(bool ok, std::string_view text) -> void {
  ++checks;
  if (!ok) {
    ++failures;
    std::cerr << "FAIL: " << text << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Minor projection fixture refused");
  return std::move(*value);
}
template <class F> auto rejects(F operation, std::string_view text) -> void {
  bool refused{};
  try {
    operation();
  } catch (const std::invalid_argument&) {
    refused = true;
  }
  check(refused, text);
}
auto length(V v) -> double {
  return std::hypot(v.x, v.y, v.z);
}
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto dot(V a, V b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto near(double a, double b, double tolerance, std::string_view text) -> void {
  check(std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= tolerance,
        text);
}
auto near_vector(V a, V b, double tolerance, std::string_view text) -> void {
  near(length(sub(a, b)), 0, tolerance, text);
}
// Independent +Z axis-angle and latitude/longitude ENU expressions, avoiding
// the production quaternion/vector helpers in the expected result.
auto inverse_spin(V v, double phase) -> V {
  const long double c = std::cos(static_cast<long double>(phase)),
                    s = std::sin(static_cast<long double>(phase));
  return {static_cast<double>(c * v.x + s * v.y),
          static_cast<double>(-s * v.x + c * v.y), v.z};
}
auto expected_local(const SavedMinorFlightProjection& view, V fixed) -> V {
  const long double latitude = view.pose.latitude_radians,
                    longitude = view.pose.longitude_radians;
  const auto slat = std::sin(latitude), clat = std::cos(latitude),
             slon = std::sin(longitude), clon = std::cos(longitude);
  return {static_cast<double>(-slon * fixed.x + clon * fixed.y),
          static_cast<double>(clat * clon * fixed.x + clat * slon * fixed.y +
                              slat * fixed.z),
          static_cast<double>(slat * clon * fixed.x + slat * slon * fixed.y -
                              clat * fixed.z)};
}
auto godot(LocalPositionMetres v) -> V {
  return {v.east, v.up, -v.north};
}
auto transform_point(const SavedMinorFlightProjection& view, V fixed) -> V {
  const auto& b = view.minor_fixed_basis;
  const auto& p = view.minor_center;
  return {p.x + b[0].x * fixed.x + b[1].x * fixed.y + b[2].x * fixed.z,
          p.y + b[0].y * fixed.x + b[1].y * fixed.y + b[2].y * fixed.z,
          p.z + b[0].z * fixed.x + b[1].z * fixed.y + b[2].z * fixed.z};
}
auto source(const PhysicalMinorDescriptor& owner) -> RigidBodyState {
  RigidBodyState body;
  body.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  body.frame = {RigidFrameKind::body_relative_inertial,
                owner.catalog.owner.catalog.id,
                {},
                {},
                owner.body};
  body.tick = 123456;
  body.position_metres = {owner.radius_metres + 25000., 8000, -5000};
  // Exactly unit; independently known cyclic body XYZ -> system YZX.
  body.orientation = {.5, .5, .5, .5};
  body.linear_velocity_metres_per_second = {3, -4, 5};
  return need(
      canonicalize_rigid_body_state(RigidBodyWorldContext{owner}, body));
}
auto verify_projection(const PhysicalMinorDescriptor& owner,
                       const RigidBodyState& body) -> void {
  const auto before =
      need(encode_rigid_body_state_json(RigidBodyWorldContext{owner}, body));
  const auto view = project_saved_minor_flight(owner, body);
  check(view.version == 1 && view.minor == owner && view.body == body &&
            view.fixed_frame == RigidCoordinateFrame{RigidFrameKind::body_fixed,
                                                     body.frame.system,
                                                     {},
                                                     {},
                                                     owner.body},
        "Projection retains exact minor owner, current state and typed frames");
  check(!view.fixed_frame.planet && !view.body.frame.planet &&
            view.minor.body.kind == BodyKind::minor,
        "Actual minor never enters a PlanetId or Moon carrier");
  const auto observer =
      inverse_spin(body.position_metres, view.rotation.phase_radians);
  near_vector(view.tangent.origin, observer, 1e-8,
              "Observer uses independent inverse source +Z spin");
  const auto radius = length(observer);
  near(view.pose.altitude_metres, radius - owner.radius_metres, 1e-8,
       "Display altitude uses exact source radius, with no terrain floor");
  const auto actual_rotation = need(
      resolve_physical_minor_rotation(owner, body.tick, view.tangent.origin));
  check(
      view.rotation == actual_rotation && view.rotation.tick == body.tick,
      "Actual star and body ephemeris use source owner and exact current tick");
  const std::array<V, 3> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  for (std::size_t i = 0; i < axes.size(); ++i) {
    near_vector(view.minor_fixed_basis[i], expected_local(view, axes[i]), 2e-14,
                "Fixed mesh basis has independent ENU handedness");
    near_vector(godot(view.system_axes[i]),
                expected_local(
                    view, inverse_spin(axes[i], view.rotation.phase_radians)),
                2e-14, "System basis uses inverse spin at the same tick");
    near_vector(godot(view.body_axes[i]),
                expected_local(view, inverse_spin(axes[(i + 1) % 3],
                                                  view.rotation.phase_radians)),
                2e-14, "Actual craft attitude survives local projection");
    near(length(view.minor_fixed_basis[i]), 1, 2e-14,
         "Mesh transform axes remain unit");
    near(dot(view.minor_fixed_basis[i], view.minor_fixed_basis[(i + 1) % 3]), 0,
         2e-14, "Mesh transform axes remain perpendicular");
  }
  near_vector(cross(view.minor_fixed_basis[0], view.minor_fixed_basis[1]),
              view.minor_fixed_basis[2], 2e-14,
              "Mesh transform preserves positive handedness");
  near_vector(view.minor_center, {0, -radius, 0}, 1e-8,
              "Body center is craft-relative, not a system-scale translation");
  const V fixed{owner.radius_metres * .6, owner.radius_metres * .8, 0};
  near_vector(transform_point(view, fixed),
              expected_local(view, sub(fixed, observer)), 1e-8,
              "Snapshot mesh transform equals independent point subtraction");
  near_vector(minor_fixed_point_to_local(view, fixed),
              transform_point(view, fixed), 1e-8,
              "Typed point helper agrees with one snapshot mesh transform");
  const auto& r = view.rotation;
  const auto toward = sub(r.star_position_fixed, observer);
  const auto distance = length(toward);
  const V unit{toward.x / distance, toward.y / distance, toward.z / distance};
  near_vector(godot(view.star_direction), expected_local(view, unit), 2e-14,
              "Finite-distance illumination originates at actual craft COM");
  check(project_saved_minor_flight(owner, body) == view,
        "Repeated projection has no hidden rendering clock/state");
  check(need(encode_rigid_body_state_json(RigidBodyWorldContext{owner},
                                          body)) == before,
        "Projection preserves complete source state bytes");
}
auto verify_mesh(const PhysicalMinorDescriptor& owner,
                 MinorProxyMeshDimensions dimensions) -> void {
  const auto count = need(minor_proxy_mesh_counts(dimensions));
  const MinorProxyMeshVertex sentinel{{123, 456, 789}, {-2, -3, -4}};
  constexpr std::uint32_t sentinel_index{0xfedcba98U};
  std::vector<MinorProxyMeshVertex> vertices(count.vertices + 1, sentinel);
  std::vector<std::uint32_t> indices(count.indices + 1, sentinel_index);
  check(need(fill_minor_proxy_mesh(owner, dimensions, vertices, indices)) ==
            count,
        "Exact bounded mesh fills requested counts");
  check(vertices.back() == sentinel && indices.back() == sentinel_index,
        "Successful fills preserve both trailing buffer elements");
  for (std::size_t i = 0; i < count.vertices; ++i) {
    near(length(vertices[i].position_metres), owner.radius_metres, 1e-8,
         "Every fixed vertex lies on the true source spherical radius");
    near(length(vertices[i].normal), 1, 1e-14,
         "Every source radial normal is unit");
    near_vector(vertices[i].position_metres,
                {owner.radius_metres * vertices[i].normal.x,
                 owner.radius_metres * vertices[i].normal.y,
                 owner.radius_metres * vertices[i].normal.z},
                0, "Positions and normals share the actual fixed source frame");
  }
  for (std::size_t i = 0; i < count.indices; i += 3) {
    const auto a = indices[i], b = indices[i + 1], c = indices[i + 2];
    const bool bounded =
        a < count.vertices && b < count.vertices && c < count.vertices;
    check(bounded, "All indices stay inside the exact vertex buffer");
    if (!bounded) continue;
    const auto& p = vertices[a].position_metres;
    check(dot(cross(sub(vertices[b].position_metres, p),
                    sub(vertices[c].position_metres, p)),
              p) < 0,
          "Exterior uses Godot-clockwise nondegenerate triangles, even caps");
  }
  const auto stride = dimensions.longitude_intervals + 1;
  for (std::uint32_t row = 0; row <= dimensions.latitude_intervals; ++row)
    check(vertices[row * stride] ==
              vertices[row * stride + dimensions.longitude_intervals],
          "Longitude seam is bit-identical, including polar vertices");
  // These owned snapshots detect partial writes on refusal, including tails.
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Snapshot
  const auto saved_vertices = vertices;
  // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Snapshot
  const auto saved_indices = indices;
  auto refused = fill_minor_proxy_mesh(
      owner, dimensions, std::span{vertices}.first(count.vertices - 1),
      indices);
  check(!refused && refused.error() == MinorProxyMeshError::short_buffer &&
            vertices == saved_vertices && indices == saved_indices,
        "Short vertex buffer refuses without touching either caller buffer");
  refused = fill_minor_proxy_mesh(owner, dimensions, vertices,
                                  std::span{indices}.first(count.indices - 1));
  check(!refused && refused.error() == MinorProxyMeshError::short_buffer &&
            vertices == saved_vertices && indices == saved_indices,
        "Short index buffer refuses without touching either caller buffer");
  auto forged = owner;
  forged.density_kilograms_per_cubic_metre =
      std::numeric_limits<double>::quiet_NaN();
  refused = fill_minor_proxy_mesh(forged, dimensions, vertices, indices);
  check(!refused && refused.error() == MinorProxyMeshError::invalid_owner &&
            vertices == saved_vertices && indices == saved_indices,
        "Nonfinite forged owner refuses atomically before caller writes");
  auto wrong_radius = owner;
  ++wrong_radius.radius_metres;
  refused = fill_minor_proxy_mesh(wrong_radius, dimensions, vertices, indices);
  check(!refused && vertices == saved_vertices && indices == saved_indices,
        "No presentation radius retuning can replace authenticated source");
  auto future_owner = owner;
  future_owner.version = 99;
  refused = fill_minor_proxy_mesh(future_owner, dimensions, vertices, indices);
  check(!refused && vertices == saved_vertices && indices == saved_indices,
        "Unknown physical recipe refuses without touching output buffers");
  for (auto bad : {MinorProxyMeshDimensions{0, 64},
                   {1, 64},
                   {33, 64},
                   {32, 0},
                   {32, 2},
                   {32, 65},
                   {std::numeric_limits<std::uint32_t>::max(),
                    std::numeric_limits<std::uint32_t>::max()}}) {
    check(!minor_proxy_mesh_counts(bad),
          "Invalid dimensions refuse before count arithmetic");
    refused = fill_minor_proxy_mesh(owner, bad, vertices, indices);
    check(!refused &&
              refused.error() == MinorProxyMeshError::invalid_dimensions &&
              vertices == saved_vertices && indices == saved_indices,
          "Invalid dimensions leave complete caller buffers unchanged");
  }
  check(need(fill_minor_proxy_mesh(owner, dimensions, vertices, indices)) ==
                count &&
            vertices == saved_vertices && indices == saved_indices,
        "Repeated fills reproduce identical fixed geometry and tails");
}
auto run() -> void {
  for (std::uint32_t version : {1U, 2U}) {
    const auto system =
        need(generate_physical_origin_system(Seed{42}, version));
    const auto catalog = need(generate_secondary_body_catalog(system));
    const auto owner = need(
        generate_physical_minor_descriptor(catalog, catalog.bodies[1].node.id));
    check(owner.radius_metres == catalog.bodies[1].radius_metres &&
              owner.tint == catalog.bodies[1].tint,
          "Proxy uses exact existing catalog size and appearance");
    const auto body = source(owner);
    verify_projection(owner, body);
    auto later = body;
    later.tick += owner.spin_period_ticks / 4;
    verify_projection(owner, later);
    check(
        project_saved_minor_flight(owner, body).minor_fixed_basis !=
            project_saved_minor_flight(owner, later).minor_fixed_basis,
        "Same inertial craft at a different tick changes fixed mesh transform");
    for (const double sign : {-1., 1.}) {
      auto pole = body;
      pole.position_metres = {0, 0, sign * (owner.radius_metres + 1000)};
      const auto view = project_saved_minor_flight(owner, pole);
      check(view.tangent.east == V{0, 1, 0} &&
                view.tangent.north == V{-sign, 0, 0},
            "Both exact poles use deterministic handed tangent conventions");
      near_vector(view.minor_center, {0, -(owner.radius_metres + 1000.), 0},
                  1e-8, "Polar mesh center remains observer-relative");
    }
    auto bad = body;
    bad.frame.body->kind = BodyKind::moon;
    rejects([&] { (void)project_saved_minor_flight(owner, bad); },
            "Minor cannot masquerade as a Moon owner");
    bad = body;
    ++bad.frame.body->value;
    rejects([&] { (void)project_saved_minor_flight(owner, bad); },
            "Foreign body refuses before projection");
    bad = body;
    bad.frame.kind = RigidFrameKind::body_fixed;
    rejects([&] { (void)project_saved_minor_flight(owner, bad); },
            "Raw fixed state cannot masquerade as saved canonical frame5");
    bad = body;
    bad.position_metres.x = std::numeric_limits<double>::infinity();
    rejects([&] { (void)project_saved_minor_flight(owner, bad); },
            "Nonfinite craft position refuses");
    bad = body;
    bad.position_metres = {};
    rejects([&] { (void)project_saved_minor_flight(owner, bad); },
            "Undefined central tangent refuses");
    auto forged = owner;
    ++forged.radius_metres;
    rejects([&] { (void)project_saved_minor_flight(forged, body); },
            "Retuned radius cannot enter projection");
    const auto view = project_saved_minor_flight(owner, body);
    rejects(
        [&] {
          (void)system_vector_to_minor_local(
              view, {0, std::numeric_limits<double>::quiet_NaN(), 0});
        },
        "Nonfinite display vector refuses");
    verify_mesh(owner, {2, 3});
    verify_mesh(owner, {32, 64});
    if (version == 2) {
      FreedomFlightSaveDocument document{
          make_freedom_new_game_document(Seed{42}), body, {}};
      document.origin.state.tick = body.tick;
      document.model.physical_catalog = 2;
      document.model.physical_ephemeris = 2;
      document.minor =
          FreedomMinorWorldSelection{1, {system.catalog.id, owner.body}};
      auto session = need(NativeFreedomFlightSession::open(
          need(native_select_save_document(NativeSaveDocument{document}))));
      const auto before =
          need(encode_native_save_document_json(need(session.save_document())));
      check(project_saved_minor_flight(session) == view,
            "Session overload projects its authenticated actual minor/state");
      check(need(encode_native_save_document_json(
                need(session.save_document()))) == before,
            "Session projection preserves complete save bytes");
    }
  }
  const auto legacy =
      need(NativeFreedomFlightSession::open(need(native_new_game(Seed{42}))));
  rejects([&] { (void)project_saved_minor_flight(legacy); },
          "Historical planet session remains outside minor projection");
}
} // namespace
int main() {
  try {
    run();
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << e.what() << '\n';
  }
  std::cout << checks << " saved minor projection/proxy checks, " << failures
            << " failures\n";
  return failures ? 1 : 0;
}
