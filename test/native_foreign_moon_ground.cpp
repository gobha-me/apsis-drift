#include "native_foreign_moon_ground.hpp"

#include "apsis_drift/moon_surface_walker.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "godot/contact_geometry_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace apsis_drift::test_fixture {
namespace {
unsigned checks{};
auto check(bool value, const char* message) -> void {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
template <class T, class E>
auto need(std::expected<T, E> value,
          std::source_location where = std::source_location::current()) -> T {
  if (!value) {
    std::cerr << where.file_name() << ':' << where.line() << ' ';
    if constexpr (std::is_same_v<E, std::string>)
      std::cerr << value.error();
    else if constexpr (requires { value.error().detail; })
      std::cerr << value.error().detail;
    else if constexpr (std::is_enum_v<E>)
      std::cerr << static_cast<unsigned>(value.error());
    std::cerr << '\n';
    throw std::runtime_error("Foreign Moon ground fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
template <class T, class E>
auto require_moon_contact(std::expected<T, E> value) -> T {
  return need(std::move(value));
}
auto bytes(const NativeFreedomFlightSession& session) -> std::string {
  return need(encode_native_save_document_json(need(session.save_document())));
}
auto continued(const NativeFreedomFlightSession& source)
    -> std::unique_ptr<NativeFreedomFlightSession> {
  const auto before = bytes(source);
  auto saved = std::make_unique<NativeSaveDocument>(
      need(decode_native_save_document_json(before)));
  auto selected = need(native_select_save_document(std::move(*saved)));
  auto result = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(std::move(selected))));
  check(bytes(*result) == before,
        "Foreign ground complete Continue preserves exact bytes");
  return result;
}
// Same front-pad5cm/facet-normal/rear-edge correction as moon_contact_fixture.
// Only this labelled fixture is placed. Every candidate still needs all real
// pad, hull and hatch certificates; production terrain/tolerances never change.
auto placed_source(const PhysicalMoonDescriptor& moon, SimulationTick tick,
                   std::uint32_t cell_x, std::uint32_t cell_y)
    -> std::optional<RigidBodyState> {
  const auto sampler = require_moon_contact(MoonTerrainSampler::create(moon));
  const auto triangle = require_moon_contact(
      build_moon_contact_triangle(sampler, {moon.body, CubeFace::positive_y,
                                            4096, 4096, cell_x, cell_y, 0}));
  const auto& a = triangle.vertices[0];
  const auto& b = triangle.vertices[1];
  const auto& c = triangle.vertices[2];
  const RigidVector3 center{a.x + ((b.x - a.x) + (c.x - a.x)) / 3,
                            a.y + ((b.y - a.y) + (c.y - a.y)) / 3,
                            a.z + ((b.z - a.z) + (c.z - a.z)) / 3};
  const auto n = triangle.outward_normal;
  const auto w = 1 + n.y, magnitude = std::hypot(w, n.z, n.x);
  RigidBodyState fixed;
  fixed.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  fixed.tick = tick;
  fixed.frame = {RigidFrameKind::body_fixed,
                 moon.catalog.owner.catalog.id,
                 {},
                 {},
                 moon.body};
  fixed.orientation = {w / magnitude, n.z / magnitude, 0, -n.x / magnitude};
  const auto craft = require_moon_contact(resolve_craft_frame(fixed.craft));
  const auto& pad = craft.properties.supports[0];
  const RigidVector3 offset{
      static_cast<double>(std::int64_t{pad.contact_mm[0]} -
                          craft.properties.center_of_mass_mm[0]) /
          1000,
      static_cast<double>(std::int64_t{pad.contact_mm[1]} -
                          craft.properties.center_of_mass_mm[1]) /
          1000,
      static_cast<double>(std::int64_t{pad.contact_mm[2]} -
                          craft.properties.center_of_mass_mm[2]) /
          1000};
  // Independent unit-equivalent quaternion vector rotation for fixture setup.
  const auto& q = fixed.orientation;
  const RigidVector3 v{q.x, q.y, q.z};
  const RigidVector3 cross1{v.y * offset.z - v.z * offset.y,
                            v.z * offset.x - v.x * offset.z,
                            v.x * offset.y - v.y * offset.x};
  const RigidVector3 cross2{v.y * cross1.z - v.z * cross1.y,
                            v.z * cross1.x - v.x * cross1.z,
                            v.x * cross1.y - v.y * cross1.x};
  const auto scale = 2 / (((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z);
  const RigidVector3 rotated{offset.x + scale * (q.w * cross1.x + cross2.x),
                             offset.y + scale * (q.w * cross1.y + cross2.y),
                             offset.z + scale * (q.w * cross1.z + cross2.z)};
  fixed.position_metres = {center.x - .05 * n.x - rotated.x,
                           center.y - .05 * n.y - rotated.y,
                           center.z - .05 * n.z - rotated.z};
  // The rear-starboard footprint crosses its selected facet's first edge at
  // the front-pad centroid placement. Translate the whole craft within the
  // ORIGINAL front-pad plane, just far enough to leave 5cm of footprint edge
  // clearance. This is one geometric placement, not a terrain/location search;
  // the seed, facets, orientation, pad sizes and production tolerances stay
  // put.
  const auto rear =
      require_moon_contact(godot_spike::detail::nominal_contact_rectangle(
          moon.radius_metres, fixed, craft.properties, 2));
  const auto rear_id = require_moon_contact(locate_moon_contact_triangle(
      sampler, {rear.center.x, rear.center.y, rear.center.z}));
  const auto rear_triangle =
      require_moon_contact(build_moon_contact_triangle(sampler, rear_id));
  const auto& edge_a = rear_triangle.vertices[0];
  const auto& edge_b = rear_triangle.vertices[1];
  const auto& opposite = rear_triangle.vertices[2];
  const RigidVector3 edge{edge_b.x - edge_a.x, edge_b.y - edge_a.y,
                          edge_b.z - edge_a.z};
  const auto rn = rear_triangle.outward_normal;
  RigidVector3 inward{rn.y * edge.z - rn.z * edge.y,
                      rn.z * edge.x - rn.x * edge.z,
                      rn.x * edge.y - rn.y * edge.x};
  const auto dot = [](const auto& lhs, const auto& rhs) {
    return (lhs.x * rhs.x + lhs.y * rhs.y) + lhs.z * rhs.z;
  };
  const RigidVector3 toward{opposite.x - edge_a.x, opposite.y - edge_a.y,
                            opposite.z - edge_a.z};
  const auto inward_length = std::hypot(inward.x, inward.y, inward.z);
  const auto inward_scale =
      (dot(inward, toward) < 0 ? -1.0 : 1.0) / inward_length;
  inward = {inward.x * inward_scale, inward.y * inward_scale,
            inward.z * inward_scale};
  const RigidVector3 rear_relative{rear.center.x - edge_a.x,
                                   rear.center.y - edge_a.y,
                                   rear.center.z - edge_a.z};
  const auto minimum = dot(inward, rear_relative) -
                       std::abs(dot(inward, rear.width)) -
                       std::abs(dot(inward, rear.depth));
  const auto normal_component = dot(inward, n);
  const RigidVector3 tangent{inward.x - normal_component * n.x,
                             inward.y - normal_component * n.y,
                             inward.z - normal_component * n.z};
  const auto shift = std::max(0.0, (.05 - minimum) / dot(inward, tangent));
  if (!std::isfinite(shift) || shift >= 1) return {};
  fixed.position_metres = {fixed.position_metres.x + shift * tangent.x,
                           fixed.position_metres.y + shift * tangent.y,
                           fixed.position_metres.z + shift * tangent.z};
  const RigidBodyWorldContext context{moon};
  fixed = require_moon_contact(canonicalize_rigid_body_state(context, fixed));
  const auto source = require_moon_contact(
      reframe_rigid_body(context, fixed,
                         {{RigidFrameKind::body_relative_inertial,
                           moon.catalog.owner.catalog.id,
                           {},
                           {},
                           moon.body},
                          fixed.tick},
                         moon));
  return source;
}
using V = RigidVector3;
auto sub(V a, V b) -> V {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
auto scale(V a, double k) -> V {
  return {a.x * k, a.y * k, a.z * k};
}
auto dot(V a, V b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto length(V a) -> double {
  return std::hypot(a.x, a.y, a.z);
}
auto cross(V a, V b) -> V {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto rotate(RigidOrientation q, V v) -> V {
  const V u{q.x, q.y, q.z};
  const auto a = cross(u, v), b = cross(u, a);
  const double k = 2 / (q.w * q.w + dot(u, u));
  return {v.x + k * (q.w * a.x + b.x), v.y + k * (q.w * a.y + b.y),
          v.z + k * (q.w * a.z + b.z)};
}
auto walk_tick(NativeFreedomFlightSession& session, V target) -> void {
  const auto foot = session.surface_walker()->foot_position_metres;
  const auto delta = sub(target, foot);
  const auto gap = length(delta);
  check(std::isfinite(gap) && gap > 1e-6,
        "Foreign foot route has a finite actual bearing");
  const auto up = scale(foot, 1 / length(foot));
  const auto craft_right =
      rotate(session.surface()->moon_landed->fixed.orientation, {1, 0, 0});
  const auto projected = sub(craft_right, scale(up, dot(craft_right, up)));
  const auto right = scale(projected, 1 / length(projected));
  const auto back = cross(right, up);
  need(session.advance_surface_walk(
      {-dot(delta, back) / gap, dot(delta, right) / gap, 0}));
  check(!session.surface_walk_blockage(),
        "Foreign foot route remains supported without pathfinding or geometry "
        "changes");
}
auto phase(NativeFreedomFlightSession& session,
           const std::filesystem::path& directory, const char* name) -> void {
  if (directory.empty()) return;
  std::filesystem::create_directories(directory);
  need(session.save_as(std::filesystem::absolute(directory / name)));
}
auto supported(const NativeFreedomFlightSession& original)
    -> std::unique_ptr<NativeFreedomFlightSession> {
  auto session = continued(original);
  // One real tick leaves the immutable instant-arrival comparison boundary.
  // The following placement is explicitly a separate ground fixture.
  need(session->advance({}));
  const auto& moon = *session->moon();
  constexpr std::array<std::array<std::int32_t, 2>, 17> cells{{{0, 0},
                                                               {1, 0},
                                                               {-1, 0},
                                                               {0, 1},
                                                               {0, -1},
                                                               {1, 1},
                                                               {1, -1},
                                                               {-1, 1},
                                                               {-1, -1},
                                                               {2, 0},
                                                               {-2, 0},
                                                               {0, 2},
                                                               {0, -2},
                                                               {2, 2},
                                                               {2, -2},
                                                               {-2, 2},
                                                               {-2, -2}}};
  std::optional<RigidBodyState> accepted;
  unsigned attempted{};
  for (const auto cell : cells) {
    ++attempted;
    const auto candidate =
        placed_source(moon, session->document().flight.tick,
                      static_cast<std::uint32_t>(16 + cell[0]),
                      static_cast<std::uint32_t>(16 + cell[1]));
    if (!candidate) continue;
    const auto checksum = need(
        rigid_body_state_checksum(RigidBodyWorldContext{moon}, *candidate));
    const auto anchor =
        prepare_moon_landed_craft(moon, *candidate, true, checksum);
    if (!anchor) continue;
    auto terrain =
        MoonSurfaceWalkTerrain::create(moon, *anchor, candidate->tick);
    if (!terrain || !terrain->entry()) continue;
    accepted = *candidate;
    break;
  }
  std::cout << "Foreign ground fixed candidates tried=" << attempted << "/17\n";
  check(accepted.has_value(),
        "No actual supported foreign Moon contact/hatch in17 fixed candidates");
  auto travel = std::make_unique<FreedomTravelSaveDocument>(
      need(session->travel_document()));
  auto flight = session->document();
  flight.flight = *accepted;
  // Change only the labelled fixture's current flight. All real paid route,
  // pilot, craft, model, knowledge, resources and source clock remain intact.
  travel->voyage.voyage.voyage.base = std::move(flight);
  travel->voyage.voyage.voyage.surface = {};
  auto selected =
      need(native_select_save_document(NativeSaveDocument{std::move(*travel)}));
  session = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(std::move(selected))));
  need(session->set_landing_gear(true));
  need(session->commit_touchdown(need(rigid_body_state_checksum(
      RigidBodyWorldContext{*session->moon()}, session->document().flight))));
  check(session->surface()->moon_landed.has_value(),
        "Real commit_touchdown admits every foreign pad/hull certificate");
  return session;
}
} // namespace

auto run_foreign_moon_ground(
    const NativeFreedomFlightSession& genuine_foreign_arrival,
    const std::filesystem::path& output) -> unsigned {
  const auto baseline = checks;
  const auto original = bytes(genuine_foreign_arrival);
  check(genuine_foreign_arrival.moon() &&
            genuine_foreign_arrival.document().moon &&
            genuine_foreign_arrival.document().moon->version ==
                kFreedomForeignMoonWorldVersion,
        "Ground sidecar requires actual foreign Moon arrival");
  auto session = supported(genuine_foreign_arrival);
  const auto original_charges = session->resources()->jump_charges;
  const auto original_routes = *session->travel();
  phase(*session, output, "foreign-landed.json");
  need(session->begin_surface_walk());
  phase(*session, output, "foreign-outside.json");
  const auto before_enable = session->document().flight.tick;
  need(session->enable_surface_landmarks());
  check(session->knowledge()->moon_surface_landmarks &&
            session->knowledge()->moon_surface_landmarks->version ==
                kFreedomNetworkMoonSurfaceLandmarkVersion &&
            session->document().flight.tick == before_enable,
        "Foreign Enable explicitly selects inspection2 without advancing time");
  const auto view = need(session->moon_surface_landmarks_view());
  check(view.selected && !view.sites.empty(),
        "First supported foreign fixture has an actual nearby exposure");
  // Choose actual nearest once, independently of presentation ordering.
  // A blocked route never selects an alternate site or path.
  const auto nearest = std::ranges::min_element(
      view.sites, {}, &FreedomMoonSurfaceLandmarkView::distance_metres);
  const auto site = nearest->site;
  const V target_position{site.position_metres.x, site.position_metres.y,
                          site.position_metres.z};
  unsigned outward{};
  while (length(sub(target_position,
                    session->surface_walker()->foot_position_metres)) > 7.5 &&
         outward < 24000) {
    walk_tick(*session, target_position);
    ++outward;
  }
  check(length(sub(target_position,
                   session->surface_walker()->foot_position_metres)) <= 7.5,
        "Actual foreign foot route reaches the nearest exposure within8metres "
        "and24000ticks");
  const auto id = site.identity.id;
  const BodyTarget target{session->system().catalog.id, session->moon()->body};
  const auto uninspected = bytes(*session);
  check(
      !session->collect_field_sample(id) && bytes(*session) == uninspected,
      "Foreign sample still requires actual inspection without mutating Save");
  need(session->inspect_surface_landmark(id));
  const auto inspected = bytes(*session);
  need(session->inspect_surface_landmark(id));
  check(bytes(*session) == inspected,
        "Foreign repeated inspection preserves exact first notes and time");
  const auto clock = session->document().flight.tick;
  const auto flight = session->document();
  const auto resources = *session->resources();
  const auto facts = session->knowledge()->entries;
  need(session->collect_field_sample(id));
  check(session->knowledge()->field_samples &&
            session->knowledge()->field_samples->version ==
                kFreedomNetworkFieldSamplesVersion &&
            session->knowledge()->field_samples->carried.size() == 1 &&
            session->knowledge()->field_samples->journal.size() == 1 &&
            session->knowledge()->field_samples->carried.front() ==
                FreedomFieldSampleRecord{
                    target, site.identity.cell, id, 1, site.identity.kind,
                    session->document().origin.state.craft, clock} &&
            session->document() == flight &&
            *session->resources() == resources &&
            session->knowledge()->entries == facts,
        "Actual foreign collection selects sample2 with typed provenance and "
        "changes no flight/resources/facts/time");
  const auto journal = session->knowledge()->field_samples->journal;
  const auto collected = bytes(*session);
  check(!session->collect_field_sample(id) && bytes(*session) == collected,
        "Duplicate foreign specimen collection refuses atomically");
  session = continued(*session);
  need(session->discard_field_sample(target, id));
  check(session->knowledge()->field_samples->carried.empty() &&
            session->knowledge()->field_samples->journal == journal,
        "Discard retains the authentic foreign specimen's first journal notes");
  need(session->mark_field_sample_site(target, id, false));
  const auto& marker = *session->knowledge()->surface_marker;
  const auto& point = std::get<FreedomMoonSurfaceMarkerPoint>(*marker.point);
  check(marker.version == kFreedomNetworkMoonSurfaceMarkerVersion &&
            point.system == target.system && point.body == target.body &&
            point.position == site.position_metres && point.tick == clock &&
            session->document() == flight &&
            *session->resources() == resources &&
            session->knowledge()->entries == facts,
        "Discarded foreign note marks only its authenticated centre using "
        "marker3 at zero time");
  const auto marked = bytes(*session);
  check(!session->mark_field_sample_site(target, id, false) &&
            bytes(*session) == marked,
        "Foreign note cannot silently overwrite the existing personal marker");
  need(session->mark_field_sample_site(target, id, true));
  check(bytes(*session) == marked,
        "Explicit same-site replacement preserves complete Save");
  session = continued(*session);
  need(session->advance_surface_walk({}));
  need(session->collect_field_sample(id));
  check(session->knowledge()->field_samples->journal == journal &&
            session->knowledge()->field_samples->carried.front().tick ==
                clock + 1,
        "Real next-tick recollection keeps first notes while recording current "
        "carried tick");
  phase(*session, output, "foreign-marked.json");
  const auto pouch = *session->knowledge()->field_samples;
  unsigned back{};
  while (!need(session->surface_return_access()).within_reach && back < 24000) {
    walk_tick(*session,
              need(session->surface_return_access()).foot_position_metres);
    ++back;
  }
  check(need(session->surface_return_access()).within_reach,
        "Actual foreign foot route returns to the original hatch "
        "within24000ticks");
  need(session->return_from_surface_walk());
  check(!session->surface_walker() &&
            *session->knowledge()->field_samples == pouch &&
            session->resources()->jump_charges == original_charges &&
            *session->travel() == original_routes,
        "Foreign reboard retains specimen and every original paid receipt "
        "without spending jump charges");
  session = continued(*session);
  check(bytes(genuine_foreign_arrival) == original,
        "Separate labelled ground fixture leaves the genuine three-charge "
        "outing exact");
  std::cout << "Foreign ground actual foot ticks out=" << outward
            << " return=" << back << '\n';
  return checks - baseline;
}
} // namespace apsis_drift::test_fixture
