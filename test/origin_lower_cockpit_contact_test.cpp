#include "apsis_drift/boarding_support_data.hpp"
#include "apsis_drift/operating_motion_recipe.hpp"
#include "apsis_drift/origin_lower_cockpit_contact.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace {
using namespace apsis_drift;
using Json = nlohmann::json;
std::size_t checks{};
int failures{};
auto check(bool value, std::string_view label) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
template <class T> auto require(std::expected<T, std::string> result) -> T {
  if (!result) throw std::runtime_error(result.error());
  return std::move(*result);
}
auto read(const char* path) -> std::string {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("Missing admitted contact fixture");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>{}};
}
constexpr LowerCockpitTriangleKey top0{LowerCockpitContactBuffer::halo, 0, 662},
    top1{LowerCockpitContactBuffer::halo, 0, 663};
constexpr double top_y = -230000.0 * 1e-6;
auto negative_buffers(const std::string& halo, const std::string& policy,
                      const OriginCabinSeamGeometry& original) -> void {
  check(!decode_origin_lower_cockpit_contact({}, policy, original),
        "empty halo refuses");
  check(!decode_origin_lower_cockpit_contact(
            std::string(kLowerCockpitMaximumDocumentBytes + 1, ' '), policy,
            original),
        "oversized halo refuses");
  check(!decode_origin_lower_cockpit_contact(std::string(17, '[') + "0" +
                                                 std::string(17, ']'),
                                             policy, original),
        "deep halo refuses");
  check(!decode_origin_lower_cockpit_contact("}{", policy, original),
        "unbalanced buffer refuses");
  check(!decode_origin_lower_cockpit_contact("{\"a\":1,\"a\":2}", policy,
                                             original),
        "duplicate keys refuse");
  check(!decode_origin_lower_cockpit_contact("{\"a\":NaN}", policy, original),
        "nonfinite token refuses");
  check(!decode_origin_lower_cockpit_contact(halo, {}, original),
        "empty policy refuses");
  check(!decode_origin_lower_cockpit_contact(
            halo, std::string(kLowerCockpitMaximumPolicyBytes + 1, ' '),
            original),
        "oversized policy refuses");
  const auto baseline = Json::parse(halo),
             approved_policy = Json::parse(policy);
  check(std::abs(baseline["objects"][10]["source_corrected_world_rows"][1][1]
                     .get<double>() -
                 .528) < 1e-7,
        "actual scaled affine source provenance remains legitimate");
  const auto mutate = [&](auto edit, std::string_view label,
                          std::string_view stage) {
    auto j = baseline;
    edit(j);
    auto r = decode_origin_lower_cockpit_contact(j.dump(), policy, original);
    check(!r, label);
    check(!r && r.error().find(stage) != std::string::npos,
          "malformation reaches real validator before byte gate");
  };
  mutate([](Json& j) { j["unknown"] = 1; }, "unknown root refuses",
         "closed object");
  mutate([](Json& j) { j["schema_version"] = true; }, "boolean version refuses",
         "integer type");
  mutate([](Json& j) { j["vertices_micrometres"][0] = Json::array({0, 0}); },
         "vertex dimensions refuse", "dimensions");
  mutate([](Json& j) { j["vertices_micrometres"][0][0] = true; },
         "boolean coordinate refuses", "integer type");
  mutate([](Json& j) { j["vertices_micrometres"][0][0] = 1e308; },
         "nonfinite/extreme coordinate refuses", "integer type");
  mutate([](Json& j) { j["vertices_micrometres"][0][0] = 128000001; },
         "coordinate bound refuses", "coordinate bound");
  mutate([](Json& j) { j["triangles"][0][0] = -1; }, "negative index refuses",
         "unsigned index");
  mutate([](Json& j) { j["triangles"][0][0] = 4598; }, "one-past index refuses",
         "unsigned index");
  mutate([](Json& j) { j["triangles"][0][0] = 4294967295ULL; },
         "overflow index refuses", "unsigned index");
  mutate([](Json& j) { j["triangles"][0][1] = j["triangles"][0][0]; },
         "repeated indices refuse", "repeated triangle");
  mutate(
      [](Json& j) {
        const auto a = j["triangles"][0][0].get<std::size_t>(),
                   b = j["triangles"][0][1].get<std::size_t>();
        j["vertices_micrometres"][b] = j["vertices_micrometres"][a];
      },
      "distinct-index zero-area face refuses", "nondegenerate");
  mutate([](Json& j) { j["objects"][10]["source_evaluated_triangles"] = 0; },
         "zero source count refuses before subtractingone", "nonzero source");
  mutate(
      [](Json& j) {
        j["objects"][10]["evaluated_source_triangle_indices"][187] = 188;
      },
      "one-past source face refuses", "unsigned index");
  mutate([](Json& j) { j["objects"][10]["halo_triangle_start"] = 481; },
         "object range gap refuses", "gap-free");
  mutate([](Json& j) { j["objects"][10]["halo_triangle_start"] = 479; },
         "object range overlap refuses", "gap-free");
  mutate(
      [](Json& j) { j["objects"][10]["halo_triangle_count"] = 4294967295ULL; },
      "checked range overflow refuses", "unsigned index");
  mutate([](Json& j) { j["objects"][10]["owner"] = "station"; },
         "wrong owner refuses", "static craft");
  mutate([](Json& j) { j["objects"][10]["motion_group"] = "seat_lift"; },
         "moving halo refuses", "scalar type");
  mutate([](Json& j) { j["objects"][10]["source_object"] = "unknown"; },
         "unknown entirely omitted source refuses", "unique source");
  mutate(
      [](Json& j) { j["objects"][0]["admitted_range"]["group"] = "seat_lift"; },
      "wrong original namespace refuses", "original fixed");
  mutate(
      [](Json& j) {
        j["objects"][0]["admitted_range"]["triangle_start"] = 62016;
      },
      "stale original range refuses", "original fixed");
  mutate(
      [](Json& j) {
        j["objects"][10]["evaluated_source_triangle_indices"][182] = 181;
      },
      "duplicate source face refuses", "ordered unique");
  mutate(
      [](Json& j) {
        j["objects"][10]["source_corrected_world_rows"][3][0] = 1;
      },
      "non-affine provenance refuses", "nonsingular affine");
  mutate(
      [](Json& j) {
        j["objects"][10]["source_corrected_world_rows"][0][0] = 0;
      },
      "singular source provenance refuses", "nonsingular affine");
  mutate(
      [](Json& j) {
        j["vertices_micrometres"][0][0] =
            j["vertices_micrometres"][0][0].get<int>() + 1;
      },
      "finite substituted geometry refuses", "independently approved");
  mutate(
      [](Json& j) {
        auto& v = j["derivative_corrections_baked_once"]["operations"][0]
                   ["parameters"]["inward_metres"];
        v = v.get<double>() + .001;
      },
      "finite correction substitution refuses", "independently approved");
  for (auto key : {"contact_sha256", "support_runtime_sha256",
                   "operating_motion_sha256", "craft_model_sha256"}) {
    auto p = approved_policy;
    p["identities"][key] = std::string(64, '0');
    auto r = decode_origin_lower_cockpit_contact(halo, p.dump(), original);
    check(!r, "independent original/support/motion/model identity gate");
  }
  auto p = approved_policy;
  p["coverage"]["lower_static_bounds_micrometres"][1][2] = -400000;
  check(!decode_origin_lower_cockpit_contact(halo, p.dump(), original),
        "enclosing box cannot replace exact union");
  check(!decode_origin_lower_cockpit_contact(halo + " ", policy, original),
        "semantic self-rehashed source cannot replace approved bytes");
  check(!decode_origin_lower_cockpit_contact(halo, policy + " ", original),
        "policy bytes independently pinned");
}
auto queries(const OriginLowerCockpitContact& geometry) -> void {
  check(geometry.objects().size() == 75, "immutable source object roster");
  std::uint32_t partition{};
  for (const auto& object : geometry.objects()) {
    check(object.triangle_start == partition,
          "gap-free source object partition");
    partition += object.triangle_count;
    check(object.evaluated_source_triangles.size() == object.triangle_count,
          "every output face has a source face");
  }
  check(partition == 8100, "all actual halo faces attributed");
  for (auto key : {top0, top1}) {
    const auto face = require(lookup_lower_cockpit_face(geometry, key));
    check(face.object == 10 &&
              face.evaluated_source_triangle == key.triangle - 480 &&
              face.source_object ==
                  "CRAFT | pilot transition intermediate step",
          "real omitted upper face attribution");
    check(face.unit_normal_current.y > .99, "real upward source winding");
    for (auto v : face.points_current_metres)
      check(v.y == top_y, "source micrometre plane applied once");
    auto centroid = RigidVector3{
        (face.points_current_metres[0].x + face.points_current_metres[1].x +
         face.points_current_metres[2].x) /
            3,
        top_y,
        (face.points_current_metres[0].z + face.points_current_metres[1].z +
         face.points_current_metres[2].z) /
            3};
    check(require(assess_lower_cockpit_surface_point(geometry, key, centroid))
              .point_on_face,
          "actual candidate on actual source triangle");
    centroid.y += .001;
    check(!require(assess_lower_cockpit_surface_point(geometry, key, centroid))
               .point_on_face,
          "1mm plane error is not point evidence");
  }
  const auto tub = require(assess_lower_cockpit_surface_point(
      geometry, {LowerCockpitContactBuffer::halo, 0, 464}, {0, -.57, -1.7}));
  check(tub.point_on_face && tub.face.object == 9 &&
            tub.face.evaluated_source_triangle == 8 &&
            std::abs(tub.barycentric.x - 47.0 / 102) < 1e-10 &&
            std::abs(tub.barycentric.y - 7.0 / 34) < 1e-10 &&
            std::abs(tub.barycentric.z - 1.0 / 3) < 1e-10,
        "independent omitted tub source8 barycentric witness");
  const auto old8 = require(lookup_lower_cockpit_face(
      geometry, {LowerCockpitContactBuffer::original, 0, 8}));
  const auto halo8 = require(lookup_lower_cockpit_face(
      geometry, {LowerCockpitContactBuffer::halo, 0, 8}));
  check(!old8.evaluated_source_triangle &&
            halo8.evaluated_source_triangle == 8 &&
            old8.source_object != halo8.source_object &&
            halo8.source_object != tub.face.source_object,
        "original triangle8, halo8 and tub source8 are distinct");
  check(!require(assess_lower_cockpit_surface_point(
                     geometry, {LowerCockpitContactBuffer::halo, 0, 8},
                     {0, -.57, -1.7}))
             .point_on_face,
        "wrong buffer/source face cannot manufacture floor evidence");
  check(
      !lookup_lower_cockpit_face(geometry,
                                 {LowerCockpitContactBuffer::halo, 1, 662}) &&
          !lookup_lower_cockpit_face(
              geometry, {LowerCockpitContactBuffer::halo, 0, 8100}) &&
          !lookup_lower_cockpit_face(
              geometry, {static_cast<LowerCockpitContactBuffer>(9), 0, 662}) &&
          !lookup_lower_cockpit_face(
              geometry, {LowerCockpitContactBuffer::original, 14, 0}),
      "unknown buffer/group/station namespace refuses");
  const RigidVector3 step{0, top_y, -1.265};
  const auto old = require(assess_origin_cabin_proxy_clearance(
      *geometry.original_geometry(), step, step));
  const auto actual =
      require(assess_lower_cockpit_reservations(geometry, step, step));
  check(!old.coverage_complete && !old.interior_clear &&
            actual.coverage_complete,
        "real diagnostic lower boots require exact composite source union");
  check(!actual.interior_clear,
        "unchanged technical proxy truthfully reports neighboring obstruction");
  for (auto key : {top0, top1})
    check(std::ranges::any_of(
              actual.contacts,
              [&](const LowerCockpitReservationContact& c) {
                return c.key == key &&
                       c.intersection == CabinIntersection::boundary &&
                       c.part != CabinProxyPart::body && c.object == 10 &&
                       c.evaluated_source_triangle == key.triangle - 480;
              }),
          "actual step top contact is boundary only");
  auto below = step;
  below.y -= .001;
  const auto penetrating =
      require(assess_lower_cockpit_reservations(geometry, below, below));
  check(penetrating.coverage_complete && !penetrating.interior_clear &&
            std::ranges::any_of(penetrating.contacts,
                                [](const LowerCockpitReservationContact& c) {
                                  return (c.key == top0 || c.key == top1) &&
                                         c.intersection ==
                                             CabinIntersection::interior;
                                }),
        "real 1mm omitted top penetration refuses");
  for (auto y : {std::nextafter(top_y, -1.), std::nextafter(top_y, 1.)}) {
    auto p = step;
    p.y = y;
    const auto e = require(assess_lower_cockpit_reservations(geometry, p, p));
    const auto interiors = std::ranges::count_if(
        e.contacts, [](const LowerCockpitReservationContact& c) {
          return (c.key == top0 || c.key == top1) &&
                 c.intersection == CabinIntersection::interior;
        });
    check(y < top_y ? interiors > 0 : interiors == 0,
          "adjacent representable top intrusion versus separation");
  }
  const RigidVector3 join{0, -.24, -.64};
  check(require(assess_lower_cockpit_reservations(geometry, join, join))
            .coverage_complete,
        "reservation spanning shared join fits true union");
  for (auto p : {RigidVector3{0, -.24, -.639}, RigidVector3{0, -.25, -.64},
                 RigidVector3{0, -.9, -1.265}, RigidVector3{1, -.23, -1.265},
                 RigidVector3{0, -.57, -.4}}) {
    const auto e = require(assess_lower_cockpit_reservations(geometry, p, p));
    check(!e.coverage_complete && !e.interior_clear && e.contacts.empty(),
          "partial/outlying/enclosing-box coverage cannot grant clearance");
  }
  check(!require(assess_lower_cockpit_reservations(geometry, join,
                                                   {0, -.24, -.639}))
             .coverage_complete,
        "complete swept reservation must stay in union");
  const auto seam = require(certify_origin_cabin_seam_translation(
      *geometry.original_geometry(), 3.84, 3.45));
  check(seam.nonpenetrating_crossing &&
            std::abs(seam.minimum_sole_area_fraction - 123.0 / 140) < 1e-10 &&
            std::abs(seam.minimum_load_margin_metres - .106) < 1e-10,
        "original seam certificate unchanged");
  for (auto y : {std::nextafter(kCabinSeamFloorMetres, -1.),
                 std::nextafter(kCabinSeamFloorMetres, 1.)}) {
    const RigidVector3 p{0, y, 3.6475};
    const auto e = require(assess_origin_cabin_proxy_clearance(
        *geometry.original_geometry(), p, p));
    const auto interiors =
        std::ranges::count_if(e.contacts, [](const CabinProxyContact& c) {
          return (c.triangle == BoardingTriangleKey{0, 85129} ||
                  c.triangle == BoardingTriangleKey{0, 85130} ||
                  c.triangle == BoardingTriangleKey{0, 85537} ||
                  c.triangle == BoardingTriangleKey{0, 85538}) &&
                 c.intersection == CabinIntersection::interior;
        });
    check(e.coverage_complete &&
              (y < kCabinSeamFloorMetres ? interiors > 0 : interiors == 0),
          "old seam retains representable intrusion/separation distinction");
  }
  const auto combined = require(assess_lower_cockpit_reservations(
      geometry, {0, kCabinSeamFloorMetres, 3.84},
      {0, kCabinSeamFloorMetres, 3.45}));
  check(combined.coverage_complete && combined.interior_clear &&
            combined.contacts.size() == seam.swept_clearance.contacts.size(),
        "old-only seam contact inventory preserved");
  const auto ladder = require(assess_lower_cockpit_reservations(
      geometry, {0, kCabinSeamFloorMetres, 5.5},
      {0, kCabinSeamFloorMetres, 5.5}));
  check(!ladder.interior_clear &&
            std::ranges::any_of(
                ladder.contacts,
                [](const LowerCockpitReservationContact& c) {
                  return c.key ==
                             LowerCockpitTriangleKey{
                                 LowerCockpitContactBuffer::original, 5, 144} &&
                         c.object == 766 &&
                         c.intersection == CabinIntersection::interior;
                }),
        "full existing moving hardware remains authoritative obstacle");
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  check(!assess_lower_cockpit_reservations(geometry, {nan, 0, 0}, step) &&
            !assess_lower_cockpit_surface_point(geometry, top0, {0, nan, 0}),
        "nonfinite queries refuse");
  check(require(assess_lower_cockpit_reservations(geometry, step, step))
                .contacts.size() == actual.contacts.size(),
        "refusals preserve immutable original and halo inventories");
}
} // namespace
int main() {
  try {
    auto motion = require(
        decode_operating_motion_recipe(detail::kOperatingMotionRecipeJson));
    auto catalog = require(decode_origin_boarding_support(
        detail::boarding_support_json(), motion));
    auto original = require(decode_origin_cabin_seam_contact(
        read(APSIS_CABIN_CONTACT_FIXTURE), catalog));
    const auto halo = read(APSIS_LOWER_CONTACT_FIXTURE),
               policy = read(APSIS_LOWER_CONTACT_POLICY_FIXTURE);
    negative_buffers(halo, policy, original);
    auto geometry =
        require(decode_origin_lower_cockpit_contact(halo, policy, original));
    queries(geometry);
    auto moved = std::move(geometry);
    // NOLINTBEGIN(bugprone-use-after-move) -- Immutable contact handle
    // explicitly supports empty views and refused queries after move.
    check(geometry.objects().empty() &&
              geometry.original_geometry() == nullptr &&
              !lookup_lower_cockpit_face(geometry, top0) &&
              !assess_lower_cockpit_reservations(geometry, {0, top_y, -1.265},
                                                 {0, top_y, -1.265}),
          "moved-from lower handle refuses");
    // NOLINTEND(bugprone-use-after-move) -- End documented moved-from handle
    // checks.
    check(require(lookup_lower_cockpit_face(moved, top0)).object == 10,
          "sharing moved handle retains attribution");
    auto original_moved = std::move(original);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Empty-handle query contract.
    check(!decode_origin_lower_cockpit_contact(halo, policy, original),
          "moved-from original cannot qualify composition");
    check(require(
              certify_origin_cabin_seam_translation(original_moved, 3.84, 3.45))
              .nonpenetrating_crossing,
          "original geometry unchanged by composition");
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout << "Lower cockpit contact: " << checks << " checks, " << failures
            << " failures\n";
  return failures ? 1 : 0;
}
