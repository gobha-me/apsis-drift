#include "apsis_drift/rigid_frame_handoff.hpp"
#include "apsis_drift/terrain_touchdown.hpp"
#include "contact_geometry_internal.hpp"
#include "terrain_touchdown_internal.hpp"

#include <bit>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace apsis_drift;
using namespace apsis_drift::godot_spike;
using V = RigidVector3;
int failures{}, checks{};
auto check(bool condition, std::string_view message) -> void {
  ++checks;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <class T, class E> auto required(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("required terrain contact fixture");
  return std::move(*value);
}
auto add(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto mul(V a, double s) -> V {
  return {a.x * s, a.y * s, a.z * s};
}
auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
template <class T> auto v(T a) -> V {
  return {a.x, a.y, a.z};
}
auto rotate(RigidOrientation q, V a) -> V {
  const V u{q.x, q.y, q.z};
  const double s = 2 / (((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z);
  return add(a, mul(add(mul(cross(u, a), q.w), cross(u, cross(u, a))), s));
}
auto align_up(V n) -> RigidOrientation {
  const double w = 1 + n.y, d = std::hypot(w, n.z, n.x);
  return {w / d, n.z / d, 0, -n.x / d};
}
auto bits(TouchdownMargin margin) -> std::uint32_t {
  return static_cast<std::uint32_t>(margin);
}

struct Fixture {
  Seed seed;
  explicit Fixture(Seed universe_seed = Seed{42}) : seed(universe_seed) {}
  PhysicalLocalSystem owner = required(generate_physical_origin_system(seed));
  PlanetDescriptor planet =
      owner.catalog.planets[kOriginHomePlanetOrdinal].descriptor;
  PhysicalPlanetRotationRecipe rotation =
      required(generate_planet_rotation_recipe(owner, planet.id));
  RigidBodyWorldContext context{owner};
  auto session(RigidBodyState fixed) const -> NativeFreedomFlightSession {
    auto source =
        required(reframe_rigid_body(context, fixed,
                                    {{RigidFrameKind::planet_relative_inertial,
                                      owner.catalog.id,
                                      planet.id,
                                      {}},
                                     fixed.tick},
                                    rotation));
    auto origin = make_freedom_new_game_document(seed);
    origin.state.tick = source.tick;
    FreedomFlightSaveDocument document{origin, source, {}};
    return required(NativeFreedomFlightSession::open(
        {NativeStartup::Mode::freedom, std::move(document), planet, {}}));
  }
  auto fixed(const ContactTriangle& triangle, double gap = -.05) const
      -> RigidBodyState {
    RigidBodyState state;
    state.tick = 1200;
    state.craft = {kWayfarerFrameId, 1};
    state.frame = {
        RigidFrameKind::planet_fixed, owner.catalog.id, planet.id, {}};
    state.orientation = align_up(v(triangle.outward_normal));
    const auto craft = required(resolve_craft_frame(state.craft)).properties;
    const auto& pad = craft.supports[0];
    const V offset{
        (std::int64_t{pad.contact_mm[0]} - craft.center_of_mass_mm[0]) / 1000.0,
        (std::int64_t{pad.contact_mm[1]} - craft.center_of_mass_mm[1]) / 1000.0,
        (std::int64_t{pad.contact_mm[2]} - craft.center_of_mass_mm[2]) /
            1000.0};
    const auto a = v(triangle.vertices[0]);
    const auto center = add(a, mul(add(sub(v(triangle.vertices[1]), a),
                                       sub(v(triangle.vertices[2]), a)),
                                   .3));
    state.position_metres =
        sub(add(center, mul(v(triangle.outward_normal), gap)),
            rotate(state.orientation, offset));
    return state;
  }
};

// Independent long-double Moller-Trumbore intersection: different arithmetic
// from the provider's affine radial-cone inequalities, including interior rays.
auto ray_hit(V start, V direction, const ContactTriangle& triangle)
    -> std::optional<long double> {
  using L = std::array<long double, 3>;
  const auto convert = [](V p) -> L { return {p.x, p.y, p.z}; };
  const auto subtract = [](L a, L b) -> L {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
  };
  const auto product = [](L a, L b) -> long double {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  };
  const auto vector_product = [](L a, L b) -> L {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]};
  };
  const auto a = convert(v(triangle.vertices[0]));
  const auto e1 = subtract(convert(v(triangle.vertices[1])), a);
  const auto e2 = subtract(convert(v(triangle.vertices[2])), a);
  const auto d = convert(direction);
  const auto p = vector_product(d, e2);
  const auto det = product(e1, p);
  if (std::abs(det) < 1e-12L) return {};
  const auto s = subtract(convert(start), a);
  const auto u = product(s, p) / det;
  const auto q = vector_product(s, e1);
  const auto w = product(d, q) / det;
  if (u < 0 || w < 0 || u + w > 1) return {};
  return product(e2, q) / det;
}

auto synthetic_patch(const Fixture& f, double altitude = 100, double slope = 0)
    -> ContactPatch {
  const auto radius = static_cast<double>(f.planet.radius.value) * 1000;
  const auto sine = std::sin(slope), cosine = std::cos(slope);
  const auto local = [&](V p) -> PlanetFixedPositionMetres {
    return {p.x, radius + altitude + cosine * p.y - sine * p.z,
            sine * p.y + cosine * p.z};
  };
  ContactPatch patch;
  patch.triangle = {
      kExperimentalSavedContactSurface,
      {f.planet.id, CubeFace::positive_y, 4096, 4096, 0, 0, 0},
      {local({-20, 0, -20}), local({20, 0, -20}), local({0, 0, 20})},
      {0, cosine, sine}};
  patch.pad_center = local({0, -.01, 0});
  patch.half_width = {.32, 0, 0};
  patch.half_length = {0, -sine * .26, cosine * .26};
  patch.minimum_gap_metres = -.0101;
  patch.maximum_gap_metres = -.0099;
  return patch;
}

auto arithmetic_and_boundaries(const Fixture& f) -> void {
  const auto base = synthetic_patch(f);
  for (const double slope : {0.0, .1, .3, .6}) {
    const auto patch = synthetic_patch(f, 100, slope);
    const auto observation =
        apsis_drift::detail::terrain_pad_observation(f.planet, patch);
    check(observation && observation->surface == TouchdownSurface::solid &&
              observation->footprint_supported,
          "level and steep dry facets have geometric bedrock support");
    for (int x = -4; x <= 4; ++x)
      for (int z = -4; z <= 4; ++z) {
        const auto point =
            add(v(patch.pad_center), add(mul(v(patch.half_width), x / 4.0),
                                         mul(v(patch.half_length), z / 4.0)));
        const auto hit =
            ray_hit(point, v(patch.triangle.outward_normal), patch.triangle);
        check(hit && std::abs(*hit - .01L) < 1e-8L,
              "whole-pad certificate agrees with independent interior normal "
              "rays");
      }
  }
  const auto water = apsis_drift::detail::terrain_pad_observation(
      f.planet, synthetic_patch(f, -100));
  check(water && water->surface == TouchdownSurface::water &&
            !water->footprint_supported,
        "sub-datum facet is non-bearing water");
  const auto shore = apsis_drift::detail::terrain_pad_observation(
      f.planet, synthetic_patch(f, 0));
  check(shore && shore->surface == TouchdownSurface::unsupported &&
            !shore->footprint_supported,
        "sea-datum uncertainty is unsupported");
  const auto reject = [&](ContactPatch patch, TerrainSupportError error) {
    const auto result =
        apsis_drift::detail::terrain_pad_observation(f.planet, patch);
    check(!result && result.error() == error,
          "malformed or ambiguous geometry refuses");
  };
  auto patch = base;
  patch.pad_center.x = 100;
  reject(patch, TerrainSupportError::first_surface_not_certified);
  patch = base;
  patch.pad_center.y = -patch.pad_center.y;
  reject(patch, TerrainSupportError::first_surface_not_certified);
  patch = base;
  patch.support_index = 4;
  reject(patch, TerrainSupportError::invalid_geometry);
  patch = base;
  patch.triangle.id.half = 2;
  reject(patch, TerrainSupportError::invalid_geometry);
  patch = base;
  patch.triangle.id.tile_x = 8192;
  reject(patch, TerrainSupportError::invalid_geometry);
  patch = base;
  patch.triangle.id.planet.value ^= 1;
  reject(patch, TerrainSupportError::invalid_geometry);
  patch = base;
  patch.half_width = {};
  reject(patch, TerrainSupportError::invalid_geometry);
  patch = base;
  patch.triangle.outward_normal = {};
  reject(patch, TerrainSupportError::invalid_geometry);
  patch = base;
  patch.triangle.vertices[1] = patch.triangle.vertices[0];
  reject(patch, TerrainSupportError::invalid_geometry);
  patch = base;
  patch.minimum_gap_metres = 1;
  reject(patch, TerrainSupportError::invalid_geometry);
  patch = base;
  patch.triangle.recipe.source_lod = 7;
  reject(patch, TerrainSupportError::invalid_geometry);
  // Its normal projection still sits at the facet centre; the starting pad
  // is in a different radial cone. Projection containment alone is
  // insufficient.
  patch = synthetic_patch(f, 100, .6);
  const auto n = v(patch.triangle.outward_normal);
  const auto distant = add(v(patch.pad_center), mul(n, 1000.01));
  patch.pad_center = {distant.x, distant.y, distant.z};
  patch.minimum_gap_metres = 999.9999;
  patch.maximum_gap_metres = 1000.0001;
  reject(patch, TerrainSupportError::first_surface_not_certified);
  // Strict cone inset: clearly outside, in the uncertainty band and safely
  // inside, using real world-coordinate binary64 positions.
  for (const auto inset : {-0.001, 0.0, .00005, .0002, .001}) {
    patch = base;
    const auto y = patch.pad_center.y;
    const auto anchor_y = patch.triangle.vertices[0].y;
    patch.pad_center.z = -20 * y / anchor_y + .26 + inset;
    const auto outcome =
        apsis_drift::detail::terrain_pad_observation(f.planet, patch);
    check(static_cast<bool>(outcome) == (inset > kContactPatchAllowanceMetres),
          "cone inset is strict; zero and uncertain positive coverage refuse");
  }
  for (const auto invalid : {std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN(), 1e9}) {
    patch = base;
    patch.pad_center.x = invalid;
    reject(patch, TerrainSupportError::invalid_geometry);
    patch = base;
    patch.triangle.vertices[2].z = invalid;
    reject(patch, TerrainSupportError::invalid_geometry);
    patch = base;
    patch.triangle.outward_normal.y = invalid;
    reject(patch, TerrainSupportError::invalid_geometry);
  }
}

auto generated_terrain(const Fixture& f) -> void {
  auto cache = required(TerrainTileCache::create(1));
  bool found{};
  for (unsigned trial = 0; trial < 64 && !found; ++trial) {
    const auto triangle =
        required(godot_spike::detail::build_selected_contact_triangle(
            f.planet, kExperimentalSavedContactSurface,
            {f.planet.id, CubeFace::positive_x, 4000 + trial * 3, 4000, 12, 13,
             0},
            cache));
    const auto fixed = f.fixed(triangle);
    auto session = f.session(fixed);
    const auto before =
        required(encode_freedom_flight_document_json(session.document()));
    auto snapshot =
        required(TerrainTouchdownSnapshot::create(session, true, 1, 1));
    check(snapshot.cache_size() == 0,
          "factory rejects/validates before terrain sampling");
    const auto result = snapshot.query();
    check(result == snapshot.query(),
          "repeat assessment and cache eviction are exact");
    check(required(encode_freedom_flight_document_json(session.document())) ==
              before,
          "provider never mutates actual save or flight state");
    auto resident =
        required(TerrainTouchdownSnapshot::create(session, true, 1, 64));
    check(result == resident.query(),
          "cache capacities/order cannot change classification");
    check(!result.supports[3] && !result.geometry.supports[3].geometry &&
              !result.geometry.supports[3].triangle,
          "three Wayfarer pads preserve empty fourth buffer slot");
    if (!result.assessment ||
        result.assessment->classification != TouchdownClass::pad_contact_ready)
      continue;
    found = true;
    check(trial == 0,
          "version-one generated golden location remains unchanged");
    check(result.assessment->failed_margins == 0,
          "generated dry terrain grants readiness only with every margin");
    auto stowed = required(TerrainTouchdownSnapshot::create(session, false));
    const auto stowed_result = stowed.query();
    check(stowed_result.assessment &&
              stowed_result.assessment->classification ==
                  TouchdownClass::pad_contact_unsafe &&
              stowed_result.assessment->failed_margins ==
                  bits(TouchdownMargin::gear_not_deployed),
          "prospective deployed footprint never invents deployed gear");
    for (unsigned i = 0; i < 3; ++i) {
      const auto& patch = **result.geometry.supports[i].geometry;
      std::array<ContactTriangle, 18> neighbourhood;
      const auto id = patch.triangle.id;
      const auto cell_x = static_cast<int>(id.tile_x * 32 + id.cell_x);
      const auto cell_y = static_cast<int>(id.tile_y * 32 + id.cell_y);
      unsigned slot{};
      for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
          for (unsigned half = 0; half < 2; ++half) {
            const auto x = static_cast<unsigned>(cell_x + dx);
            const auto y = static_cast<unsigned>(cell_y + dy);
            neighbourhood[slot++] =
                required(godot_spike::detail::build_selected_contact_triangle(
                    f.planet, kExperimentalSavedContactSurface,
                    {f.planet.id, id.face, x / 32, y / 32, x % 32, y % 32,
                     half},
                    cache));
          }
      for (int x = -2; x <= 2; ++x)
        for (int z = -2; z <= 2; ++z) {
          const auto point =
              add(v(patch.pad_center), add(mul(v(patch.half_width), x / 2.0),
                                           mul(v(patch.half_length), z / 2.0)));
          const auto hit =
              ray_hit(point, v(patch.triangle.outward_normal), patch.triangle);
          check(hit && -*hit >= patch.minimum_gap_metres &&
                    -*hit <= patch.maximum_gap_metres,
                "actual generated whole-footprint gaps enclose independent ray "
                "hits");
          for (const auto& neighbour : neighbourhood) {
            if (neighbour.id == id) continue;
            const auto earlier =
                ray_hit(point, v(patch.triangle.outward_normal), neighbour);
            check(!earlier || !hit || *earlier < 0 || *earlier >= *hit - 1e-7L,
                  "independent adjacent-facet rays find no earlier surface in "
                  "certified segment");
          }
        }
      const auto& observation = **result.supports[i];
      constexpr std::array<std::array<std::uint64_t, 2>, 3> gap_golden{
          {{13810753028725748325ULL, 13810724205688133147ULL},
           {13810753028773796853ULL, 13810724205736181675ULL},
           {13810753028676831381ULL, 13810724205639216203ULL}}};
      check(std::bit_cast<std::uint64_t>(observation.minimum_gap_metres) ==
                    gap_golden[i][0] &&
                std::bit_cast<std::uint64_t>(observation.maximum_gap_metres) ==
                    gap_golden[i][1] &&
                std::bit_cast<std::uint64_t>(observation.outward_normal.x) ==
                    4607177516790421873ULL &&
                std::bit_cast<std::uint64_t>(observation.outward_normal.y) ==
                    13805753507761050597ULL &&
                std::bit_cast<std::uint64_t>(observation.outward_normal.z) ==
                    13805751309939253147ULL,
            "generated whole-pad gaps/normals retain exact binary64 golden "
            "vectors");
      std::cout << "fixture " << trial << " pad " << i << " gapbits "
                << std::bit_cast<std::uint64_t>(observation.minimum_gap_metres)
                << ' '
                << std::bit_cast<std::uint64_t>(observation.maximum_gap_metres)
                << ' '
                << std::bit_cast<std::uint64_t>(observation.outward_normal.x)
                << ' '
                << std::bit_cast<std::uint64_t>(observation.outward_normal.y)
                << ' '
                << std::bit_cast<std::uint64_t>(observation.outward_normal.z)
                << '\n';
    }
    auto fast = fixed;
    fast.linear_velocity_metres_per_second =
        mul(v(triangle.outward_normal), -2.01);
    auto fast_session = f.session(fast);
    auto fast_snapshot =
        required(TerrainTouchdownSnapshot::create(fast_session, true));
    const auto fast_result = fast_snapshot.query();
    check(fast_result.assessment &&
              fast_result.assessment->classification ==
                  TouchdownClass::pad_contact_unsafe &&
              (fast_result.assessment->failed_margins &
               bits(TouchdownMargin::descent_speed_exceeded)),
          "generated contact cannot conceal hazardous descent");
    auto clear = fixed;
    clear.position_metres =
        add(clear.position_metres, mul(v(triangle.outward_normal), 1));
    auto clear_session = f.session(clear);
    auto clear_snapshot =
        required(TerrainTouchdownSnapshot::create(clear_session, true));
    const auto clear_result = clear_snapshot.query();
    check(clear_result.assessment && clear_result.assessment->classification ==
                                         TouchdownClass::pads_clear,
          "actual separated generated footprints report clear pads");
    const auto bad_policy = TerrainTouchdownSnapshot::create(session, true, 0);
    check(!bad_policy &&
              bad_policy.error() == SavedContactError::unsupported_recipe,
          "unknown physical policy cannot silently adopt current recipe");
    const auto bad_capacity =
        TerrainTouchdownSnapshot::create(session, true, 1, 0);
    check(!bad_capacity &&
              bad_capacity.error() == SavedContactError::invalid_cache_capacity,
          "invalid bounded cache refuses before sampling");
  }
  check(found,
        "bounded actual generated-terrain fixture yields ready touchdown");
  const auto triangle =
      required(godot_spike::detail::build_selected_contact_triangle(
          f.planet, kExperimentalSavedContactSurface,
          {f.planet.id, CubeFace::positive_x, 4000, 4000, 12, 13, 0}, cache));
  auto fixed = f.fixed(triangle);
  fixed.position_metres = mul(fixed.position_metres, 1.1);
  auto high_session = f.session(fixed);
  auto high = required(TerrainTouchdownSnapshot::create(high_session, true));
  const auto unresolved = high.query();
  check(!unresolved.assessment && !unresolved.envelope_error &&
            unresolved.supports[0] && !*unresolved.supports[0] &&
            unresolved.supports[0]->error() ==
                TerrainSupportError::unresolved_geometry,
        "out-of-bounds query is explicitly unknown, never fabricated "
        "safe/unsafe");
  // Put pad zero directly across a real mesh diagonal. This is a coverage
  // refusal, not unsupported soil, unsafe impact or permission to clamp flight.
  fixed = f.fixed(triangle);
  const auto a = v(triangle.vertices[0]);
  const auto b = v(triangle.vertices[1]);
  const auto c = v(triangle.vertices[2]);
  const auto inside = add(a, mul(add(sub(b, a), sub(c, a)), .3));
  const auto diagonal = mul(add(b, c), .5);
  fixed.position_metres = add(fixed.position_metres, sub(diagonal, inside));
  auto seam_session = f.session(fixed);
  auto seam = required(TerrainTouchdownSnapshot::create(seam_session, true));
  const auto refused = seam.query();
  check(!refused.assessment && refused.supports[0] && !*refused.supports[0] &&
            refused.geometry.supports[0].geometry &&
            !*refused.geometry.supports[0].geometry &&
            refused.geometry.supports[0].geometry->error() ==
                ContactPatchError::boundary_not_certified,
        "actual diagonal crossing retains indexed coverage refusal and unknown "
        "assessment");
  const Fixture wet_fixture{Seed{4}};
  bool found_water{};
  for (unsigned trial = 0; trial < 49 && !found_water; ++trial) {
    const auto wet_triangle =
        required(godot_spike::detail::build_selected_contact_triangle(
            wet_fixture.planet, kExperimentalSavedContactSurface,
            {wet_fixture.planet.id, CubeFace::positive_x,
             1000 + (trial % 7) * 1000, 1000 + (trial / 7) * 1000, 12, 13, 0},
            cache));
    auto wet_session = wet_fixture.session(wet_fixture.fixed(wet_triangle));
    auto wet = required(TerrainTouchdownSnapshot::create(wet_session, true));
    const auto result = wet.query();
    if (!result.assessment) continue;
    bool all_water = true;
    for (unsigned i = 0; i < 3; ++i)
      all_water = all_water && result.supports[i] && *result.supports[i] &&
                  (**result.supports[i]).surface == TouchdownSurface::water;
    if (!all_water) continue;
    found_water = true;
    check(trial == 10 && result.assessment->failed_margins == 12,
          "generated water location and failed-margin golden remain exact");
    check(result.assessment->classification ==
                  TouchdownClass::pad_contact_unsafe &&
              (result.assessment->failed_margins &
               bits(TouchdownMargin::unsupported_material)) &&
              (result.assessment->failed_margins &
               bits(TouchdownMargin::unsupported_footprint)),
          "actual generated water refuses bedrock bearing and touchdown "
          "readiness");
    std::cout << "water fixture " << trial << " failed margins "
              << result.assessment->failed_margins << '\n';
  }
  check(found_water,
        "bounded actual generator fixtures include represented water");
}
} // namespace

auto main() -> int {
  try {
    const Fixture fixture;
    arithmetic_and_boundaries(fixture);
    generated_terrain(fixture);
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    ++failures;
  }
  std::cout << "Generated terrain touchdown: " << checks << " checks, "
            << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
