#include "apsis_drift/godot/saved_contact_geometry.hpp"
#include "apsis_drift/godot/streaming.hpp"
#include "contact_geometry_internal.hpp"
#include "saved_contact_geometry_internal.hpp"

#include "apsis_drift/rigid_frame_handoff.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace apsis_drift;
using namespace apsis_drift::godot_spike;
using V = RigidVector3;
using M = std::array<std::array<double, 3>, 3>;
int failures{}, checks{};
auto check(bool value, std::string_view message) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <class T, class E> auto required(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("required saved geometry fixture");
  return std::move(*value);
}
auto plus(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto minus(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(V a, double s) -> V {
  return {a.x * s, a.y * s, a.z * s};
}
auto dot(V a, V b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto norm(V v) -> double {
  return std::hypot(v.x, v.y, v.z);
}
auto as_vector(PlanetFixedPositionMetres v) -> V {
  return {v.x, v.y, v.z};
}
auto matrix(RigidOrientation q) -> M {
  const double s = 2.0 / (((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z);
  return {{{1 - s * (q.y * q.y + q.z * q.z), s * (q.x * q.y - q.w * q.z),
            s * (q.x * q.z + q.w * q.y)},
           {s * (q.x * q.y + q.w * q.z), 1 - s * (q.x * q.x + q.z * q.z),
            s * (q.y * q.z - q.w * q.x)},
           {s * (q.x * q.z - q.w * q.y), s * (q.y * q.z + q.w * q.x),
            1 - s * (q.x * q.x + q.y * q.y)}}};
}
auto transpose(M a) -> M {
  M r{};
  for (unsigned i = 0; i < 3; ++i)
    for (unsigned j = 0; j < 3; ++j)
      r[i][j] = a[j][i];
  return r;
}
auto apply(M a, V v) -> V {
  return {a[0][0] * v.x + a[0][1] * v.y + a[0][2] * v.z,
          a[1][0] * v.x + a[1][1] * v.y + a[1][2] * v.z,
          a[2][0] * v.x + a[2][1] * v.y + a[2][2] * v.z};
}
auto product(M a, M b) -> M {
  M r{};
  for (unsigned i = 0; i < 3; ++i)
    for (unsigned j = 0; j < 3; ++j)
      for (unsigned k = 0; k < 3; ++k)
        r[i][j] += a[i][k] * b[k][j];
  return r;
}
auto close(V a, V b, double tolerance) -> bool {
  return norm(minus(a, b)) <= tolerance;
}
auto close(M a, M b, double tolerance) -> bool {
  for (unsigned i = 0; i < 3; ++i)
    for (unsigned j = 0; j < 3; ++j)
      if (std::abs(a[i][j] - b[i][j]) > tolerance) return false;
  return true;
}
auto align_up(V n) -> RigidOrientation {
  const double w = 1 + n.y, magnitude = std::hypot(w, n.z, n.x);
  if (magnitude < 1e-12) return {0, 1, 0, 0};
  return {w / magnitude, n.z / magnitude, 0, -n.x / magnitude};
}
struct Fixture {
  PhysicalLocalSystem owner =
      required(generate_physical_origin_system(Seed{42}));
  PlanetDescriptor planet =
      owner.catalog.planets[kOriginHomePlanetOrdinal].descriptor;
  PhysicalPlanetRotationRecipe rotation =
      required(generate_planet_rotation_recipe(owner, planet.id));
  RigidBodyWorldContext context{owner};
  auto state(CraftFrameRecipe craft = {kStarterShuttleFrameId, 1}) const
      -> RigidBodyState {
    auto cache = required(TerrainTileCache::create(1));
    const ContactTriangleId id{
        planet.id, CubeFace::positive_x, 4000, 4000, 12, 13, 0};
    const auto triangle = required(detail::build_selected_contact_triangle(
        planet, kExperimentalSavedContactSurface, id, cache));
    const V a = as_vector(triangle.vertices[0]);
    const V center =
        plus(a, plus(scale(minus(as_vector(triangle.vertices[1]), a), .3),
                     scale(minus(as_vector(triangle.vertices[2]), a), .3)));
    const auto properties = required(resolve_craft_frame(craft)).properties;
    const auto& support = properties.supports[0];
    RigidBodyState fixed;
    fixed.tick = 1234567;
    fixed.craft = craft;
    fixed.frame = {
        RigidFrameKind::planet_fixed, owner.catalog.id, planet.id, {}};
    const auto n = triangle.outward_normal;
    fixed.orientation = align_up({n.x, n.y, n.z});
    const V offset{(std::int64_t{support.contact_mm[0]} -
                    properties.center_of_mass_mm[0]) /
                       1000.0,
                   (std::int64_t{support.contact_mm[1]} -
                    properties.center_of_mass_mm[1]) /
                       1000.0,
                   (std::int64_t{support.contact_mm[2]} -
                    properties.center_of_mass_mm[2]) /
                       1000.0};
    fixed.position_metres =
        minus(center, apply(matrix(fixed.orientation), offset));
    fixed.linear_velocity_metres_per_second = {.125, -.25, .5};
    fixed.angular_velocity_radians_per_second = {.01, -.02, .03};
    return required(
        reframe_rigid_body(context, fixed,
                           {{RigidFrameKind::planet_relative_inertial,
                             owner.catalog.id,
                             planet.id,
                             {}},
                            fixed.tick},
                           rotation));
  }
  auto session(RigidBodyState state) const -> NativeFreedomFlightSession {
    auto origin = make_freedom_new_game_document(Seed{42});
    origin.state.tick = state.tick;
    FreedomFlightSaveDocument document{origin, state, {}};
    return required(NativeFreedomFlightSession::open(
        {NativeStartup::Mode::freedom, std::move(document), planet, {}}));
  }
};
auto frame_and_factory(const Fixture& f) -> void {
  const auto source = f.state();
  auto session = f.session(source);
  const auto before =
      required(encode_freedom_flight_document_json(session.document()));
  auto query = required(SavedContactGeometry::create(
      session, kExperimentalSavedContactSurface, 1));
  const auto& p = query.provenance();
  check(query.cache_size() == 0, "factory validates without terrain access");
  check(p.owner == f.owner && p.planet == f.planet && p.rotation == f.rotation,
        "snapshot retains actual physical/origin/rotation owner");
  check(!validate_local_system(f.owner.catalog),
        "physical catalog is not legacy-owned");
  check(p.original_state == source &&
            p.original_state_checksum ==
                required(rigid_body_state_checksum(f.context, source)),
        "original state/checksum retained");
  const auto rotation =
      required(resolve_planet_rotation(f.owner, f.rotation, source.tick))
          .geometry;
  const auto inverse = transpose(matrix(rotation.fixed_to_system));
  const V omega{rotation.angular_velocity_radians_per_second.x,
                rotation.angular_velocity_radians_per_second.y,
                rotation.angular_velocity_radians_per_second.z};
  const auto& fixed = p.fixed_query_state;
  check(close(fixed.position_metres, apply(inverse, source.position_metres),
              1e-7),
        "independent physical rotating position oracle");
  check(close(fixed.linear_velocity_metres_per_second,
              apply(inverse, minus(source.linear_velocity_metres_per_second,
                                   cross(omega, source.position_metres))),
              1e-9),
        "independent velocity includes frame spin exactly once");
  check(close(matrix(fixed.orientation),
              product(inverse, matrix(source.orientation)), 2e-14),
        "independent attitude matrix composition");
  check(close(fixed.angular_velocity_radians_per_second,
              minus(source.angular_velocity_radians_per_second,
                    apply(transpose(matrix(source.orientation)), omega)),
              2e-14),
        "independent body angular velocity subtraction");
  check(fixed.tick == source.tick && fixed.craft == source.craft &&
            fixed.frame.kind == RigidFrameKind::planet_fixed,
        "same-tick canonical temporary query frame");
  const auto first = query.query();
  check(first.support_count == 3 &&
            first.supports[3] == SavedContactPadOutcome{},
        "all nominal pads indexed and unused slot empty");
  for (unsigned i = 0; i < 3; ++i)
    check(first.supports[i].support_index == i &&
              first.supports[i].geometry.has_value(),
          "no failed foot dropped");
  check(first.supports[0].geometry && first.supports[0].geometry->has_value(),
        "real physical origin pad rectangle succeeds");
  check(query.query() == first, "evicting cache repeat deterministic");
  auto larger = required(SavedContactGeometry::create(
      session, kExperimentalSavedContactSurface, 16));
  check(larger.query() == first, "capacity-independent snapshot result");
  check(required(encode_freedom_flight_document_json(session.document())) ==
            before,
        "queries preserve exact saved document bytes");
  check(!SavedContactGeometry::create(session, kExperimentalContactSurface),
        "legacy relief1 recipe not silently reused");
  check(!SavedContactGeometry::create(session, kExperimentalSavedContactSurface,
                                      0),
        "zero cache capacity refuses");
  auto survivor = [&]() {
    auto temporary = f.session(source);
    return required(SavedContactGeometry::create(temporary));
  }();
  check(survivor.query() == first,
        "factory owns snapshot past session lifetime");
}
auto invalid_and_variants(const Fixture& f) -> void {
  const auto source = f.state();
  const auto prepare = [&](const PhysicalLocalSystem& owner,
                           const PhysicalPlanetRotationRecipe& rotation,
                           const RigidBodyState& state) {
    return detail::prepare_saved_contact(owner, rotation, state,
                                         kExperimentalSavedContactSurface);
  };
  auto wrong = f.owner;
  ++wrong.generator_version;
  check(!prepare(wrong, f.rotation, source), "forged physical owner refuses");
  auto rotation = f.rotation;
  rotation.origin_universe_seed.reset();
  check(!prepare(f.owner, rotation, source),
        "same numeric rotation IDs do not erase origin provenance");
  auto state = source;
  state.position_metres.x = std::numeric_limits<double>::quiet_NaN();
  check(!prepare(f.owner, f.rotation, state),
        "nonfinite state refuses before cache");
  state = source;
  state.orientation = {0, 0, 0, 0};
  check(!prepare(f.owner, f.rotation, state), "zero quaternion refuses");
  state = source;
  state.craft.id = {999};
  check(!prepare(f.owner, f.rotation, state), "unknown craft refuses");
  state = source;
  state.frame.kind = RigidFrameKind::planet_fixed;
  check(!prepare(f.owner, f.rotation, state),
        "public saved input frame must be planet-relative");
  const auto procedural =
      required(generate_physical_local_system(f.owner.catalog.seed));
  const auto procedural_planet =
      required(find_local_system_planet(procedural, f.planet.id))->descriptor;
  check(procedural_planet.id == f.planet.id && procedural_planet != f.planet,
        "same IDs have distinct procedural geometry");
  const auto procedural_rotation =
      required(generate_planet_rotation_recipe(procedural, f.planet.id));
  const auto origin = required(prepare(f.owner, f.rotation, source));
  const auto other = required(prepare(procedural, procedural_rotation, source));
  auto ca = required(TerrainTileCache::create()),
       cb = required(TerrainTileCache::create());
  const auto a = detail::query_saved_contact(origin, ca),
             b = detail::query_saved_contact(other, cb);
  check(a.provenance != b.provenance &&
            a.supports[0].triangle != b.supports[0].triangle,
        "origin/procedural output does not alias numeric IDs");
  auto conflict = required(TerrainTileCache::create(16));
  const auto id = a.supports[0].triangle->id;
  const TerrainTileKey key{f.planet.id, id.face, 8, id.tile_x >> 5,
                           id.tile_y >> 5};
  static_cast<void>(required(conflict.get(procedural_planet, key)));
  const auto conflicted = detail::query_saved_contact(origin, conflict);
  check(
      conflicted.supports[0].geometry && !*conflicted.supports[0].geometry &&
          conflicted.supports[0].geometry->error() ==
              ContactPatchError::surface_query_failed &&
          !conflicted.supports[0].triangle,
      "same-key descriptor cache conflict refuses rather than replacing owner");
  const auto cached = detail::query_saved_contact(origin, ca);
  for (unsigned i = 0; i < 3; ++i) {
    const auto& x = a.supports[0].triangle->vertices[i];
    const auto& y = cached.supports[0].triangle->vertices[i];
    check(std::bit_cast<std::uint64_t>(x.x) ==
                  std::bit_cast<std::uint64_t>(y.x) &&
              std::bit_cast<std::uint64_t>(x.y) ==
                  std::bit_cast<std::uint64_t>(y.y) &&
              std::bit_cast<std::uint64_t>(x.z) ==
                  std::bit_cast<std::uint64_t>(y.z),
          "repeat candidate vertex bits identical");
  }
  auto edge_fixed = origin.fixed_query_state;
  const auto properties =
      required(resolve_craft_frame(source.craft)).properties;
  const auto nominal = required(
      detail::nominal_contact_rectangle(f.planet, edge_fixed, properties, 0));
  const auto vertex = as_vector(a.supports[0].triangle->vertices[0]);
  edge_fixed.position_metres = plus(edge_fixed.position_metres,
                                    minus(vertex, as_vector(nominal.center)));
  const auto edge_source = required(reframe_rigid_body(
      f.context, edge_fixed, {source.frame, source.tick}, f.rotation));
  const auto edge_prepared =
      required(prepare(f.owner, f.rotation, edge_source));
  auto edge_cache = required(TerrainTileCache::create());
  const auto edge_batch =
      detail::query_saved_contact(edge_prepared, edge_cache);
  check(edge_batch.supports[0].triangle && edge_batch.supports[0].geometry &&
            !*edge_batch.supports[0].geometry &&
            edge_batch.supports[0].geometry->error() ==
                ContactPatchError::boundary_not_certified,
        "real terrain seam refuses whole footprint but retains its selected "
        "triangle");
  for (unsigned i = 0; i < 3; ++i)
    check(edge_batch.supports[i].support_index == i &&
              edge_batch.supports[i].geometry.has_value(),
          "seam refusal does not drop or abort other pads");
  state = source;
  const auto radius = f.planet.radius.value * 1000.0;
  state.position_metres = scale(
      state.position_metres, (radius + 200000) / norm(state.position_metres));
  const auto distant = required(prepare(f.owner, f.rotation, state));
  auto untouched = required(TerrainTileCache::create());
  const auto refused = detail::query_saved_contact(distant, untouched);
  check(untouched.size() == 0 && refused.support_count == 3,
        "out-of-domain pads do not touch cache");
  for (unsigned i = 0; i < 3; ++i)
    check(refused.supports[i].geometry && !*refused.supports[i].geometry &&
              refused.supports[i].geometry->error() ==
                  ContactPatchError::outside_query_bounds &&
              !refused.supports[i].triangle,
          "bounds refusal preserves each indexed foot");
}
auto whole_rectangle() -> void {
  ContactTriangle triangle;
  triangle.vertices = {
      PlanetFixedPositionMetres{0, 0, 0}, {20, 0, 0}, {0, 20, 0}};
  triangle.outward_normal = {0, 0, 1};
  RigidBodyState state;
  detail::NominalContactRectangle r{
      {2, 2, 3}, {.5, 0, .25}, {0, .5, -.125}, {0, 0, 1}};
  const auto result =
      required(detail::certify_contact_rectangle(state, 0, r, triangle));
  // Independent corner extrema are a test oracle, not the production proof.
  double low = std::numeric_limits<double>::infinity(), high = -low;
  for (int x : {-1, 1})
    for (int y : {-1, 1}) {
      const double z = r.center.z + x * r.width.z + y * r.depth.z;
      low = std::min(low, z);
      high = std::max(high, z);
    }
  check(result.minimum_gap_metres == low - kContactPatchAllowanceMetres &&
            result.maximum_gap_metres == high + kContactPatchAllowanceMetres,
        "whole tilted rectangle plane-gap oracle");
  check(result.minimum_edge_distances_metres[0] == 1.5 &&
            result.minimum_edge_distances_metres[2] == 1.5,
        "affine rectangle edge extrema");
  r.center.x = .5;
  check(!detail::certify_contact_rectangle(state, 0, r, triangle),
        "inside center does not certify boundary-crossing rectangle");
  r.center.x = .5 + kContactPatchAllowanceMetres;
  check(!detail::certify_contact_rectangle(state, 0, r, triangle),
        "numerical inset boundary refuses");
  r.center.x = 2;
  r.up = {1, 0, 0};
  check(detail::certify_contact_rectangle(state, 0, r, triangle).error() ==
            ContactPatchError::degenerate_projection,
        "collapsed normal projection refuses");
  r.up = {0, 0, -1};
  check(detail::certify_contact_rectangle(state, 0, r, triangle).has_value(),
        "inverted geometry has no landing policy");
  triangle.vertices[1] = triangle.vertices[0];
  check(detail::certify_contact_rectangle(state, 0, r, triangle).error() ==
            ContactPatchError::ill_conditioned_geometry,
        "degenerate primitive refuses");
}
auto terrain_identity(const Fixture& f) -> void {
  bool generated_height{};
  for (unsigned face = 0; face < 6; ++face)
    for (unsigned half = 0; half < 2; ++half) {
      auto source = required(TerrainTileCache::create(1));
      const ContactTriangleId id{
          f.planet.id, static_cast<CubeFace>(face), 4000, 4000, 12, 13, half};
      const auto triangle = required(detail::build_selected_contact_triangle(
          f.planet, kExperimentalSavedContactSurface, id, source));
      auto visual_cache = required(TerrainTileCache::create(1));
      const auto tile = build_stream_tile(
          f.planet, {face, 13, id.tile_x, id.tile_y}, 8, 0, visual_cache);
      const std::array<unsigned, 3> vertex_indices =
          half == 0 ? std::array<unsigned, 3>{13 * 33 + 12, 13 * 33 + 13,
                                              14 * 33 + 12}
                    : std::array<unsigned, 3>{13 * 33 + 13, 14 * 33 + 13,
                                              14 * 33 + 12};
      for (unsigned i = 0; i < 3; ++i) {
        const auto local = tile->vertices[vertex_indices[i]],
                   anchor = tile->anchor;
        const auto absolute = as_vector(triangle.vertices[i]);
        check(
            close(absolute,
                  {local.x + anchor.x, local.y + anchor.y, local.z + anchor.z},
                  1e-7),
            "same relief0 terrain TOP vertex, scale-qualified anchor "
            "reconstruction");
        generated_height |=
            std::abs(norm(absolute) - f.planet.radius.value * 1000.0) > .01;
      }
      const auto n = triangle.outward_normal;
      check(std::abs(norm({n.x, n.y, n.z}) - 1) < 1e-14 &&
                dot({n.x, n.y, n.z}, as_vector(triangle.vertices[0])) > 0,
            "independently outward unit geometric normal");
      check(!build_contact_triangle(f.planet, kExperimentalSavedContactSurface,
                                    id, source),
            "old API rejects new recipe before descriptor reuse");
    }
  check(generated_height, "relief0 retains generated elevation");
}
} // namespace
auto main() -> int {
  try {
    const Fixture f;
    frame_and_factory(f);
    invalid_and_variants(f);
    whole_rectangle();
    terrain_identity(f);
    auto wayfarer = f.session(f.state({kWayfarerFrameId, 1}));
    const auto batch = required(SavedContactGeometry::create(wayfarer)).query();
    check(batch.support_count == 3 && batch.supports[0].geometry->has_value(),
          "Wayfarer registered pad profile queried");
    const auto& patch = **batch.supports[0].geometry;
    check(std::abs(norm({patch.half_width.x, patch.half_width.y,
                         patch.half_width.z}) -
                   .320) < 1e-14 &&
              std::abs(norm({patch.half_length.x, patch.half_length.y,
                             patch.half_length.z}) -
                       .260) < 1e-14,
          "Wayfarer authored full rectangle dimensions");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    ++failures;
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
