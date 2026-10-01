#include "apsis_drift/boarding_support_data.hpp"
#include "apsis_drift/operating_motion_recipe.hpp"
#include "apsis_drift/origin_cabin_seam.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
namespace {
using namespace apsis_drift;
using Json = nlohmann::json;
int failures{};
std::size_t checks{};
auto check(bool value, std::string_view label) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
template <class T> auto require(std::expected<T, std::string> r) -> T {
  if (!r) throw std::runtime_error(r.error());
  return std::move(*r);
}
auto fixture() -> std::string {
  std::ifstream input(APSIS_CABIN_CONTACT_FIXTURE);
  if (!input) throw std::runtime_error("Missing admitted full contact fixture");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>{}};
}
auto negatives(const OriginBoardingSupport& support, const std::string& bytes)
    -> void {
  check(!decode_origin_cabin_seam_contact({}, support),
        "empty fullcontact refuses");
  check(!decode_origin_cabin_seam_contact(
            std::string(kCabinContactMaximumBytes + 1, ' '), support),
        "oversized fullcontact refuses before parse");
  check(!decode_origin_cabin_seam_contact(
            std::string(17, '[') + "0" + std::string(17, ']'), support),
        "excessive depth refuses");
  check(!decode_origin_cabin_seam_contact("}{", support),
        "unbalancedbuffer refuses");
  check(!decode_origin_cabin_seam_contact("{\"a\":1,\"a\":2}", support),
        "duplicatefields refuse");
  check(!decode_origin_cabin_seam_contact("{\"a\":NaN}", support),
        "nonfiniteJSON token refuses");
  const auto baseline = Json::parse(bytes);
  const auto mutate = [&](auto edit, std::string_view label,
                          std::string_view stage) {
    auto json = baseline;
    edit(json);
    const auto r = decode_origin_cabin_seam_contact(json.dump(), support);
    check(!r, label);
    check(!r && r.error().find(stage) != std::string::npos,
          "malformation reaches intended validator before source gate");
  };
  mutate([](Json& j) { j["unknown"] = 1; }, "closedroot refuses",
         "root schema");
  mutate([](Json& j) { j["schema_version"] = true; }, "boolean version refuses",
         "integer type");
  mutate([](Json& j) { j["quantization_metres"] = nullptr; },
         "nonnumeric quantization refuses", "root schema");
  mutate(
      [](Json& j) { j["sources"]["wayfarer_sha256"] = std::string(64, '0'); },
      "stale source refuses", "source identities");
  mutate([](Json& j) { j["groups"][0]["motion_group"] = "roof_port"; },
         "fixedgroup motion impostor refuses", "fixed group motion");
  mutate([](Json& j) { j["groups"][1]["motion_group"] = "wrong"; },
         "movinggroup relation refuses", "moving group relation");
  mutate([](Json& j) { j["groups"][0]["source_objects"][0] = "wrong"; },
         "ambiguous object attribution refuses", "source object roster");
  mutate(
      [](Json& j) {
        j["groups"][0]["vertices_micrometres"][0] = Json::array({0, 0});
      },
      "invalid vertex dimensions refuse", "array dimensions");
  mutate([](Json& j) { j["groups"][0]["vertices_micrometres"][0][0] = true; },
         "boolean coordinate refuses", "quantized integer");
  mutate([](Json& j) { j["groups"][0]["vertices_micrometres"][0][0] = 1e308; },
         "extreme noninteger coordinate refuses", "quantized integer");
  mutate([](Json& j) { j["groups"][0]["triangles"][0][0] = 4294967295ULL; },
         "index overflow refuses", "integer type");
  mutate([](Json& j) { j["groups"][0]["triangles"][0][0] = -1; },
         "negative vertex index refuses", "integer type");
  mutate(
      [](Json& j) {
        j["groups"][0]["triangles"][0][0] = j["groups"][0]["triangles"][0][1];
      },
      "degenerate triangle refuses", "repeated triangle");
  mutate([](Json& j) { j["crop_bounds_metres"]["craft"][0][1] = -.3; },
         "caller enlarged coverage refuses", "coverage binding");
  mutate(
      [](Json& j) {
        j["groups"][0]["vertices_micrometres"][0][0] =
            j["groups"][0]["vertices_micrometres"][0][0].get<int>() + 1;
      },
      "finite geometry substitution refuses", "independently approved");
  const auto& craft = baseline["groups"][0];
  const auto z_bounds = [&](std::uint32_t object) {
    std::array<std::int64_t, 2> bounds{128000000, -128000000};
    const auto& source = support.objects()[object];
    for (std::uint32_t i = source.triangle_start;
         i < source.triangle_start + source.triangle_count; ++i)
      for (const auto& vertex : craft["triangles"][i]) {
        const auto z =
            craft["vertices_micrometres"][vertex.get<std::size_t>()][2]
                .get<std::int64_t>();
        bounds[0] = std::min(bounds[0], z);
        bounds[1] = std::max(bounds[1], z);
      }
    return bounds;
  };
  const auto tile0 = z_bounds(178), tile1 = z_bounds(181);
  check(tile0[0] - tile1[1] == 16000,
        "whole object bevel bounds retain16mm gap");
  check(craft["vertices_micrometres"]
             [craft["triangles"][85129][0].get<std::size_t>()][2]
                    .get<std::int64_t>() -
                craft["vertices_micrometres"]
                     [craft["triangles"][85537][2].get<std::size_t>()][2]
                         .get<std::int64_t>() ==
            34000,
        "actual coplanar top boundaries retain34mm gap");
  auto changed = bytes;
  changed.push_back(' ');
  const auto r = decode_origin_cabin_seam_contact(changed, support);
  check(!r && r.error().find("independently approved") != std::string::npos,
        "finite self-rehashed payload cannotreplace independent byte source");
}
auto geometry_queries(const OriginCabinSeamGeometry& geometry) -> void {
  const auto cert =
      require(certify_origin_cabin_seam_translation(geometry, 3.84, 3.45));
  check(cert.nonpenetrating_crossing && cert.finite_support,
        "actual entirefirstseam has finite support and nonpenetrating "
        "craft-local clearance");
  check(cert.swept_clearance.coverage_complete &&
            cert.swept_clearance.interior_clear,
        "fixed crop and fullmovinggroup coverage complete");
  check(std::abs(cert.minimum_sole_area_fraction - 123.0 / 140) < 1e-10,
        "independent exact rational minimum123/140");
  check(std::abs(cert.minimum_load_margin_metres - .106) < 1e-10,
        "actual supporthull minimum106mm exceeds10mm");
  const std::array<double, 6> expected{3.45,   3.4905, 3.5245,
                                       3.7705, 3.8045, 3.84};
  for (const auto event : expected)
    check(std::ranges::any_of(
              cert.critical_centers_z_metres,
              [&](double z) { return std::abs(z - event) < 1e-12; }),
          "all analytic soleedge/rectangleedge critical points retained");
  const auto reverse =
      require(certify_origin_cabin_seam_translation(geometry, 3.45, 3.84));
  check(reverse.nonpenetrating_crossing &&
            reverse.minimum_sole_area_fraction ==
                cert.minimum_sole_area_fraction &&
            reverse.minimum_load_margin_metres ==
                cert.minimum_load_margin_metres,
        "reverse shares complete support/sweep bounds");
  std::array<std::size_t, 3> contacts{};
  std::set<std::uint32_t> touched;
  for (const auto& c : cert.swept_clearance.contacts) {
    ++contacts[static_cast<std::size_t>(c.part)];
    touched.insert(c.triangle.triangle);
    check(c.intersection == CabinIntersection::boundary,
          "reportedfloor and bevel contact is boundary only");
  }
  check(contacts[0] == 0 && contacts[1] == 6 && contacts[2] == 6,
        "wholebodyclear and each boot reports all6realboundaryfaces");
  for (const auto index : {85081U, 85129U, 85130U, 85525U, 85537U, 85538U})
    check(touched.contains(index),
          "exacttop/bevel triangles remain classified geometry");
  for (const auto z : {3.45, 3.6475, 3.84}) {
    const auto s = require(assess_origin_cabin_seam_support(geometry, z));
    check(s.area_sufficient && s.load_supported,
          "real clipped soles and loadhull atsource seam");
    check(!s.support_hull.empty(), "actual clippedpolygon hull retained");
    for (const auto& sole : s.soles)
      for (const auto& piece : sole.pieces) {
        check(piece.area_square_metres > 0 && piece.polygon.size() >= 3,
              "nonzero exactsource clipped support piece");
        for (const auto p : piece.polygon)
          check(p.y == kCabinSeamFloorMetres,
                "every support vertex lies onadmitted plane");
      }
  }
  const auto insufficient = require(assess_origin_cabin_proxy_support(
      geometry, {0, kCabinSeamFloorMetres, 3.10}));
  check(!insufficient.area_sufficient,
        "finite sole partlyoff actualtile fails75percent area");
  const auto unstable = require(assess_origin_cabin_proxy_support(
      geometry, {0, kCabinSeamFloorMetres, 3.075}));
  check(unstable.projected_load_margin_metres > 0 &&
            unstable.projected_load_margin_metres < .01 &&
            !unstable.load_supported,
        "real near-edge load lacks declared10mm stability margin");
  const auto missing = require(assess_origin_cabin_proxy_support(
      geometry, {0, kCabinSeamFloorMetres, 2.0}));
  check(!missing.area_sufficient && !missing.load_supported &&
            missing.support_hull.empty(),
        "missing sourcefaces provide nofloor");
  const auto recess =
      require(assess_origin_cabin_proxy_support(geometry, {0, -.1775, 3.6475}));
  check(!recess.area_sufficient,
        "recess floor datum cannotimpersonate tiletop support");
  const auto penetrated = require(assess_origin_cabin_proxy_clearance(
      geometry, {0, kCabinSeamFloorMetres - .001, 3.6475},
      {0, kCabinSeamFloorMetres - .001, 3.6475}));
  check(penetrated.coverage_complete && !penetrated.interior_clear,
        "one millimetre penetration intoactualfloor refuses");
  check(std::ranges::any_of(penetrated.contacts,
                            [](const CabinProxyContact& c) {
                              return c.intersection ==
                                     CabinIntersection::interior;
                            }),
        "interior SAT isnot suppressed byzero crossaxes");
  const auto side = require(assess_origin_cabin_proxy_clearance(
      geometry, {.3, kCabinSeamFloorMetres, 1.0},
      {.3, kCabinSeamFloorMetres, 1.0}));
  check(side.coverage_complete && !side.interior_clear,
        "real neighboring cabin geometry obstructs shiftedbody");
  const RigidVector3 tunnel_from{-.7, kCabinSeamFloorMetres, 0},
      tunnel_to{-.7, kCabinSeamFloorMetres, 2};
  const auto start = require(
      assess_origin_cabin_proxy_clearance(geometry, tunnel_from, tunnel_from));
  const auto finish = require(
      assess_origin_cabin_proxy_clearance(geometry, tunnel_to, tunnel_to));
  const auto sweep = require(
      assess_origin_cabin_proxy_clearance(geometry, tunnel_from, tunnel_to));
  check(start.coverage_complete && finish.coverage_complete &&
            start.interior_clear && finish.interior_clear,
        "actual tunnel control endpoints independently clear");
  check(sweep.coverage_complete && !sweep.interior_clear,
        "whole source sweep blocks tunneling between clear endpoints");
  check(std::ranges::any_of(sweep.contacts,
                            [&](const CabinProxyContact& c) {
                              return c.intersection ==
                                         CabinIntersection::interior &&
                                     c.part == CabinProxyPart::body &&
                                     c.triangle ==
                                         BoardingTriangleKey{0, 44814} &&
                                     c.object == 121 &&
                                     geometry.support_catalog()
                                             ->objects()[c.object]
                                             .source_object ==
                                         "CABIN LSC | LSC01 | lower latch.002";
                            }),
        "tunnel obstruction retains actual source body attribution");
  const RigidVector3 deployed_ladder{0, kCabinSeamFloorMetres, 5.5};
  const auto hardware = require(assess_origin_cabin_proxy_clearance(
      geometry, deployed_ladder, deployed_ladder));
  check(hardware.coverage_complete && !hardware.interior_clear &&
            std::ranges::any_of(hardware.contacts,
                                [&](const CabinProxyContact& c) {
                                  return c.intersection ==
                                             CabinIntersection::interior &&
                                         c.part == CabinProxyPart::body &&
                                         c.triangle ==
                                             BoardingTriangleKey{5, 144} &&
                                         c.object == 766 &&
                                         geometry.support_catalog()
                                                 ->objects()[c.object]
                                                 .source_object ==
                                             "AFT01 | ladder pivot arm 0.18";
                                }),
        "whole moving ladder source retains posed body obstruction");
  const auto incomplete = require(assess_origin_cabin_proxy_clearance(
      geometry, {1, kCabinSeamFloorMetres, 3.6475},
      {1, kCabinSeamFloorMetres, 3.6475}));
  check(!incomplete.coverage_complete && !incomplete.interior_clear,
        "outofcrop body cannotbe certified byray misses");
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  check(!assess_origin_cabin_proxy_clearance(geometry, {nan, 0, 0}, {0, 0, 0}),
        "nonfinite endpoint refuses");
  check(!assess_origin_cabin_seam_support(geometry, nan) &&
            !certify_origin_cabin_seam_translation(geometry, 3.0, 3.84),
        "nonfinite/outofroute certificate refuses");
  check(require(certify_origin_cabin_seam_translation(geometry, 3.84, 3.45))
            .nonpenetrating_crossing,
        "allrefusals leave immutablegeometry unchanged");
}
} // namespace
int main() {
  try {
    const auto motion = require(
        decode_operating_motion_recipe(detail::kOperatingMotionRecipeJson));
    const auto support = require(decode_origin_boarding_support(
        detail::boarding_support_json(), motion));
    const auto bytes = fixture();
    negatives(support, bytes);
    auto geometry = require(decode_origin_cabin_seam_contact(bytes, support));
    geometry_queries(geometry);
    auto moved = std::move(geometry);
    // Geometry explicitly supports absent metadata and refused queries after
    // move. This deliberately exercises that documented contract.
    // NOLINTBEGIN(bugprone-use-after-move) -- Moved-from refusal contract.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move) -- Moved-from refusal.
    check(geometry.support_catalog() == nullptr &&
              !assess_origin_cabin_proxy_support(
                  geometry, {0, kCabinSeamFloorMetres, 3.6475}),
          "movedfrom handle refuses");
    // NOLINTEND(bugprone-use-after-move) -- End moved-from refusal checks.
    check(require(certify_origin_cabin_seam_translation(moved, 3.84, 3.45))
              .nonpenetrating_crossing,
          "movedcatalog retains source-qualified evidence");
  } catch (const std::exception& e) {
    ++failures;
    std::cerr << "EXCEPTION: " << e.what() << '\n';
  }
  std::cout << "Cabin seam: " << checks << " checks, " << failures
            << " failures\n";
  return failures ? 1 : 0;
}
