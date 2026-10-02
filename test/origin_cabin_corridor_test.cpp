#include "apsis_drift/boarding_support_data.hpp"
#include "apsis_drift/operating_motion_recipe.hpp"
#include "apsis_drift/origin_cabin_corridor.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <utility>

namespace {
using namespace apsis_drift;
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
  if (!input) throw std::runtime_error("Missing admitted corridor fixture");
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>{}};
}
struct SourceFace {
  std::uint32_t triangle, object;
  std::array<std::array<std::int64_t, 2>, 3> xz;
};
// Independent exact corrected-source coordinates, not provider-derived bounds.
constexpr std::array<SourceFace, 18> source_faces{{
    {85129, 178, {{{711000, 3664500}, {-711000, 3664500}, {-711000, 4225500}}}},
    {85130, 178, {{{711000, 3664500}, {-711000, 4225500}, {711000, 4225500}}}},
    {85537, 181, {{{711000, 3069500}, {-711000, 3069500}, {-711000, 3630500}}}},
    {85538, 181, {{{711000, 3069500}, {-711000, 3630500}, {711000, 3630500}}}},
    {85945, 184, {{{711000, 2474500}, {-711000, 2474500}, {-711000, 3035500}}}},
    {85946, 184, {{{711000, 2474500}, {-711000, 3035500}, {711000, 3035500}}}},
    {86353, 187, {{{711000, 1879500}, {-711000, 1879500}, {-711000, 2440500}}}},
    {86354, 187, {{{711000, 1879500}, {-711000, 2440500}, {711000, 2440500}}}},
    {86761, 190, {{{711000, 1284500}, {-711000, 1284500}, {-711000, 1845500}}}},
    {86762, 190, {{{711000, 1284500}, {-711000, 1845500}, {711000, 1845500}}}},
    {87211, 193, {{{710992, 700000}, {-710992, 700000}, {-710992, 1250500}}}},
    {87212, 193, {{{710992, 700000}, {-710992, 1250500}, {710992, 1250500}}}},
    {87219, 193, {{{710365, 689500}, {-710365, 689500}, {-710992, 700000}}}},
    {87220, 193, {{{710365, 689500}, {-710992, 700000}, {710992, 700000}}}},
    {87645, 196, {{{674846, 94500}, {-674846, 94500}, {-708328, 655500}}}},
    {87646, 196, {{{674846, 94500}, {-708328, 655500}, {708328, 655500}}}},
    {88053, 199, {{{639335, -500500}, {-639335, -500500}, {-672817, 60500}}}},
    {88054, 199, {{{639335, -500500}, {-672817, 60500}, {672817, 60500}}}},
}};
constexpr std::array<std::array<std::int64_t, 2>, 8> top_intervals{
    {{3664500, 4225500},
     {3069500, 3630500},
     {2474500, 3035500},
     {1879500, 2440500},
     {1284500, 1845500},
     {689500, 1250500},
     {94500, 655500},
     {-500500, 60500}}};
constexpr std::array<std::int64_t, 32> source_events{
    -350000, -79500,  -45500,  200500,  234500,  515500,  549500,  560000,
    795500,  829500,  840000,  1110500, 1144500, 1390500, 1424500, 1705500,
    1739500, 1985500, 2019500, 2300500, 2334500, 2580500, 2614500, 2895500,
    2929500, 3175500, 3209500, 3490500, 3524500, 3770500, 3804500, 3840000};
constexpr double floor_y{-100000.0 * 1e-6};
struct Point {
  long double x, z;
};
// Independent Sutherland-Hodgman source-triangle clipping in micrometres.
// It retains the actual tapered outer edges instead of filling their AABBs.
auto clip(std::vector<Point> polygon, bool x_axis, long double edge,
          bool keep_above) -> std::vector<Point> {
  std::vector<Point> result;
  if (polygon.empty()) return result;
  const auto coordinate = [x_axis](Point p) { return x_axis ? p.x : p.z; };
  auto previous = polygon.back();
  auto previous_inside =
      keep_above ? coordinate(previous) >= edge : coordinate(previous) <= edge;
  for (auto current : polygon) {
    const auto inside =
        keep_above ? coordinate(current) >= edge : coordinate(current) <= edge;
    if (inside != previous_inside) {
      const auto t = (edge - coordinate(previous)) /
                     (coordinate(current) - coordinate(previous));
      result.push_back({previous.x + t * (current.x - previous.x),
                        previous.z + t * (current.z - previous.z)});
    }
    if (inside) result.push_back(current);
    previous = current;
    previous_inside = inside;
  }
  return result;
}
auto reference_fraction(double x, double z, std::size_t foot) -> double {
  const auto center_x = static_cast<long double>(x) * 1000000;
  const auto center_z = static_cast<long double>(z) * 1000000;
  const auto left = center_x + (foot == 0 ? -200000 : 80000);
  const auto right = left + 120000;
  long double area{};
  for (const auto& face : source_faces) {
    std::vector<Point> polygon;
    polygon.reserve(face.xz.size());
    for (const auto& v : face.xz)
      polygon.push_back(
          {static_cast<long double>(v[0]), static_cast<long double>(v[1])});
    polygon = clip(std::move(polygon), true, left, true);
    polygon = clip(std::move(polygon), true, right, false);
    polygon = clip(std::move(polygon), false, center_z - 140000, true);
    polygon = clip(std::move(polygon), false, center_z + 140000, false);
    if (polygon.empty()) continue;
    long double twice_area{};
    auto previous = polygon.back();
    for (auto current : polygon) {
      twice_area += previous.x * current.z - current.x * previous.z;
      previous = current;
    }
    area += std::abs(twice_area) / 2;
  }
  return static_cast<double>(area / (120000.L * 280000.L));
}
auto reference_margin(double z) -> double {
  const auto center = static_cast<long double>(z) * 1000000;
  long double low = std::numeric_limits<long double>::infinity();
  long double high = -low;
  for (const auto& interval : top_intervals) {
    const auto a =
        std::max(center - 140000, static_cast<long double>(interval[0]));
    const auto b =
        std::min(center + 140000, static_cast<long double>(interval[1]));
    if (b > a) {
      low = std::min(low, a);
      high = std::max(high, b);
    }
  }
  return static_cast<double>(std::min({200000.L, center - low, high - center}) /
                             1000000);
}
auto support_check(const OriginCabinCorridorGeometry& geometry, double z)
    -> void {
  const auto support =
      require(assess_origin_cabin_corridor_support(geometry, z));
  for (std::size_t foot = 0; foot < 2; ++foot) {
    check(
        std::abs(support.soles[foot].area_fraction -
                 reference_fraction(0, z, foot)) < 2e-10,
        "actual finite sole fraction agrees with independent source clipping");
    for (const auto& piece : support.soles[foot].pieces) {
      check(piece.triangle.group == 0 && piece.area_square_metres > 0 &&
                !piece.polygon.empty() &&
                std::ranges::any_of(source_faces,
                                    [&](const auto& face) {
                                      return face.triangle ==
                                             piece.triangle.triangle;
                                    }),
            "every positive polygon has original selected source face");
      for (const auto p : piece.polygon)
        check(p.y == floor_y && p.x >= (foot == 0 ? -.20 : .08) - 1e-12 &&
                  p.x <= (foot == 0 ? -.08 : .20) + 1e-12 &&
                  p.z >= z - .14 - 1e-12 && p.z <= z + .14 + 1e-12,
              "source polygon clipped to its actual finite sole");
    }
  }
  const auto equal =
      require(certify_origin_cabin_corridor_translation(geometry, z, z));
  const auto margin_bounded = equal.minimum_load_margin_metres <=
                              support.projected_load_margin_metres +
                                  kCabinCorridorLengthReportingBoundMetres;
  check(margin_bounded,
        "equal event certificate lower bound does not exceed actual pose hull");
  if (!margin_bounded)
    std::cerr << std::setprecision(17) << "EVENT MARGIN: z=" << z
              << " certificate=" << equal.minimum_load_margin_metres
              << " pose=" << support.projected_load_margin_metres << '\n';
  for (std::size_t foot = 0; foot < 2; ++foot) {
    const auto oracle = reference_fraction(0, z, foot);
    const auto area_bounded =
        equal.minimum_sole_area_fraction <=
            support.soles[foot].area_fraction +
                kCabinCorridorAreaFractionReportingBound &&
        equal.minimum_sole_area_fraction <=
            oracle + kCabinCorridorAreaFractionReportingBound;
    check(area_bounded,
          "equal event area lower bound does not exceed pose or source oracle");
    if (!area_bounded)
      std::cerr << std::setprecision(17) << "EVENT AREA: z=" << z
                << " foot=" << foot
                << " certificate=" << equal.minimum_sole_area_fraction
                << " pose=" << support.soles[foot].area_fraction
                << " oracle=" << oracle << '\n';
  }
  // At exact disappearance events, rounded coordinate arithmetic can select
  // either topology. The conservative analytic one-sided bound remains106mm.
  check(support.projected_load_margin_metres >= .106 - 2e-10 &&
            support.projected_load_margin_metres <= .14 + 2e-10 &&
            support.area_sufficient && support.load_supported,
        "every event-side pose retains finite area and conservative load");
}
auto source_checks(const OriginCabinCorridorGeometry& geometry) -> void {
  check(geometry.selected_top_faces().size() == source_faces.size() &&
            geometry.lower_contact() != nullptr,
        "immutable corridor retains exactly18 actual source faces");
  for (const auto& expected : source_faces) {
    const auto found = std::ranges::find_if(
        geometry.selected_top_faces(), [&](const auto& face) {
          return face.key.triangle == expected.triangle;
        });
    check(found != geometry.selected_top_faces().end(),
          "every exact main and sliver triangle remains present");
    if (found == geometry.selected_top_faces().end()) continue;
    check(found->key.buffer == LowerCockpitContactBuffer::original &&
              found->key.group == 0 && found->object == expected.object &&
              !found->evaluated_source_triangle &&
              found->source_object.starts_with("CABIN | lift-out walking tile"),
          "original namespace owner and source name preserved");
    for (std::size_t i = 0; i < 3; ++i)
      check(found->points_current_metres[i].x ==
                    static_cast<double>(expected.xz[i][0]) * 1e-6 &&
                found->points_current_metres[i].y == floor_y &&
                found->points_current_metres[i].z ==
                    static_cast<double>(expected.xz[i][1]) * 1e-6,
            "exact corrected integer triangle applied once without AABB fill");
  }
}
auto certificate_checks(const OriginCabinCorridorGeometry& geometry) -> void {
  const auto whole =
      require(certify_origin_cabin_corridor_translation(geometry, 3.84, -.35));
  check(whole.nonpenetrating_crossing && whole.finite_support &&
            whole.boundary_support_compatible &&
            whole.swept_clearance.coverage_complete &&
            whole.swept_clearance.interior_clear,
        "complete4.19m corridor has continuous support and complete clearance");
  check(std::abs(whole.minimum_sole_area_fraction - 123.0 / 140) < 2e-10 &&
            std::abs(whole.minimum_load_margin_metres - .106) < 2e-10,
        "independent exact rational whole-route minima");
  check(
      whole.source_events_micrometres ==
          std::vector<std::int64_t>(source_events.begin(), source_events.end()),
      "all32 exact source events including real tile05 join retained");
  check(std::ranges::is_sorted(whole.critical_centers_z_metres) &&
            std::adjacent_find(whole.critical_centers_z_metres.begin(),
                               whole.critical_centers_z_metres.end()) ==
                whole.critical_centers_z_metres.end(),
        "runtime event partition sorted and exactly deduplicated");
  std::set<double> candidates{-.35, 3.84};
  for (const auto& interval : top_intervals)
    for (auto edge : interval)
      for (auto offset : {-.14, .14}) {
        const auto event = static_cast<double>(edge) * 1e-6 + offset;
        if (event >= -.35 && event <= 3.84) candidates.insert(event);
      }
  for (auto edge : {700000})
    for (auto offset : {-.14, .14})
      candidates.insert(static_cast<double>(edge) * 1e-6 + offset);
  for (auto event : source_events)
    candidates.insert(static_cast<double>(event) * 1e-6);
  for (auto event : candidates) {
    check(
        std::ranges::find(whole.critical_centers_z_metres, event) !=
            whole.critical_centers_z_metres.end(),
        "distinct mathematical and runtime binary events never epsilon merged");
    for (auto z :
         {std::nextafter(event, -std::numeric_limits<double>::infinity()),
          event,
          std::nextafter(event, std::numeric_limits<double>::infinity())})
      if (z >= -.35 && z <= 3.84) support_check(geometry, z);
  }
  check(!whole.support_limits.empty(),
        "analytic one-sided support limits reported");
  bool below{}, above{};
  for (const auto& limit : whole.support_limits) {
    below |= limit.side == CabinCorridorLimitSide::below;
    above |= limit.side == CabinCorridorLimitSide::above;
    check(std::isfinite(limit.center_z_metres) &&
              std::isfinite(limit.sole_area_fraction) &&
              std::isfinite(limit.projected_load_margin_metres) &&
              limit.sole_area_fraction >= 123.0 / 140 - 2e-10 &&
              limit.projected_load_margin_metres >= .106 - 2e-10,
          "explicit positive-support limits conservatively cover every "
          "partition");
  }
  check(below && above, "both one-sided limit directions retained");
  constexpr std::array<std::size_t, 8> counts{6, 8, 8, 8, 8, 12, 8, 6};
  std::array<std::size_t, 8> actual{};
  for (const auto& contact : whole.swept_clearance.contacts) {
    check(contact.key.buffer == LowerCockpitContactBuffer::original &&
              contact.key.group == 0 && contact.part != CabinProxyPart::body &&
              contact.intersection == CabinIntersection::boundary,
          "whole sweep contains only actual boot boundary contacts");
    const auto owner =
        std::ranges::find_if(source_faces, [&](const auto& face) {
          return face.object == contact.object;
        });
    check(owner != source_faces.end(),
          "no neighboring object globally exempted");
    if (owner != source_faces.end()) {
      const auto tile = static_cast<std::size_t>((contact.object - 178) / 3);
      if (tile < actual.size()) ++actual[tile];
    }
  }
  check(whole.swept_clearance.contacts.size() == 64 && actual == counts,
        "all64 real tile and bevel boundary contacts retained by owner");
  const auto reverse =
      require(certify_origin_cabin_corridor_translation(geometry, -.35, 3.84));
  check(reverse.nonpenetrating_crossing &&
            reverse.source_events_micrometres ==
                whole.source_events_micrometres &&
            reverse.critical_centers_z_metres ==
                whole.critical_centers_z_metres &&
            reverse.minimum_sole_area_fraction ==
                whole.minimum_sole_area_fraction &&
            reverse.minimum_load_margin_metres ==
                whole.minimum_load_margin_metres,
        "reverse whole route uses same continuous proof");
  for (const auto z : {-.35, 3.84, .7, 3.45}) {
    const auto equal =
        require(certify_origin_cabin_corridor_translation(geometry, z, z));
    check(equal.nonpenetrating_crossing &&
              std::abs(equal.minimum_sole_area_fraction -
                       reference_fraction(0, z, 0)) < 2e-10 &&
              std::abs(equal.minimum_load_margin_metres - reference_margin(z)) <
                  2e-10,
          "equal endpoints retain actual pose support and margin");
  }
  for (const auto ends :
       {std::array{.9, 1.}, std::array{3.45, 3.84}, std::array{.56, .84}}) {
    const auto sub = require(
        certify_origin_cabin_corridor_translation(geometry, ends[0], ends[1]));
    check(sub.nonpenetrating_crossing &&
              sub.minimum_sole_area_fraction >= 123.0 / 140 - 2e-10,
          "subinterval continuous certificate retains support");
  }
  for (std::size_t i = 1; i < whole.critical_centers_z_metres.size(); ++i) {
    const auto a = whole.critical_centers_z_metres[i - 1];
    const auto b = whole.critical_centers_z_metres[i];
    if (std::nextafter(a, std::numeric_limits<double>::infinity()) == b) {
      const auto tiny =
          require(certify_origin_cabin_corridor_translation(geometry, a, b));
      check(tiny.nonpenetrating_crossing &&
                tiny.critical_centers_z_metres.front() == a &&
                tiny.critical_centers_z_metres.back() == b,
            "oneULP interval is not epsilon collapsed or orphaned");
    }
  }
}

auto controls(const OriginCabinCorridorGeometry& geometry) -> void {
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  const auto infinity = std::numeric_limits<double>::infinity();
  for (const auto invalid : {nan, infinity, -infinity}) {
    check(
        !certify_origin_cabin_corridor_translation(geometry, invalid, 0) &&
            !certify_origin_cabin_corridor_translation(geometry, 0, invalid) &&
            !assess_origin_cabin_corridor_support(geometry, invalid),
        "nonfinite certificate/support centers refuse");
    for (const auto pose :
         {RigidVector3{invalid, floor_y, 0}, RigidVector3{0, invalid, 0},
          RigidVector3{0, floor_y, invalid}})
      check(!assess_origin_cabin_corridor_proxy_support(geometry, pose),
            "each nonfinite finite-proxy coordinate refuses");
  }
  for (const auto z : {std::nextafter(-.35, -infinity),
                       std::nextafter(3.84, infinity), -1., 4.08})
    check(!assess_origin_cabin_corridor_support(geometry, z) &&
              !certify_origin_cabin_corridor_translation(geometry, z, 0),
          "outside closed corridor endpoints never gain route authority");
  for (const auto y : {std::nextafter(floor_y, -infinity),
                       std::nextafter(floor_y, infinity), floor_y - .001}) {
    const auto support = require(
        assess_origin_cabin_corridor_proxy_support(geometry, {0, y, 3.84}));
    check(!support.area_sufficient && !support.load_supported &&
              support.soles[0].pieces.empty() &&
              support.soles[1].pieces.empty(),
          "wrong floor and representable deviations have no exact top support");
    const auto collision = require(assess_lower_cockpit_reservations(
        *geometry.lower_contact(), {0, y, 3.84}, {0, y, 3.84}));
    check(collision.coverage_complete &&
              collision.interior_clear == (y > floor_y),
          "lower exactSAT retains oneULP and1mm intrusion/separation "
          "distinction");
  }
  const auto unsupported = require(assess_origin_cabin_corridor_proxy_support(
      geometry, {0, -230000.0 * 1e-6, -1.265}));
  check(!unsupported.area_sufficient && !unsupported.load_supported,
        "upper floor provider does not pretend lower intermediate support");
  for (const auto pose :
       {RigidVector3{.50, floor_y, .10}, RigidVector3{-.50, floor_y, .10},
        RigidVector3{.46, floor_y, -.35}, RigidVector3{-.46, floor_y, -.35},
        RigidVector3{.55, floor_y, .70}}) {
    const auto support =
        require(assess_origin_cabin_corridor_proxy_support(geometry, pose));
    for (std::size_t foot = 0; foot < 2; ++foot)
      check(std::abs(support.soles[foot].area_fraction -
                     reference_fraction(pose.x, pose.z, foot)) < 2e-10,
            "off-axis finite support clips real taper instead of filledAABB");
  }
  const auto sliver =
      require(assess_origin_cabin_corridor_support(geometry, .70));
  check(std::ranges::any_of(sliver.soles[0].pieces,
                            [](const auto& piece) {
                              return piece.triangle.triangle == 87219 ||
                                     piece.triangle.triangle == 87220;
                            }) &&
            std::ranges::any_of(sliver.soles[1].pieces,
                                [](const auto& piece) {
                                  return piece.triangle.triangle == 87219 ||
                                         piece.triangle.triangle == 87220;
                                }),
        "real10.5mm tile05 sliver supports both finite soles");
  // These points lie in the global topAABB but outside actual tapered tops.
  for (const auto& control :
       {std::pair{std::array<std::uint32_t, 2>{87219, 87220},
                  RigidVector3{.7108, floor_y, .6900}},
        std::pair{std::array<std::uint32_t, 2>{87645, 87646},
                  RigidVector3{.7000, floor_y, .1000}},
        std::pair{std::array<std::uint32_t, 2>{88053, 88054},
                  RigidVector3{.6600, floor_y, -.4900}}})
    for (const auto id : control.first)
      check(!require(assess_lower_cockpit_surface_point(
                         *geometry.lower_contact(),
                         {LowerCockpitContactBuffer::original, 0, id},
                         control.second))
                 .point_on_face,
            "source taper exterior never becomes a face merely inside itsAABB");
  const auto hardware = require(assess_lower_cockpit_reservations(
      *geometry.lower_contact(), {0, floor_y, 5.5}, {0, floor_y, 5.5}));
  check(!hardware.interior_clear &&
            std::ranges::any_of(
                hardware.contacts,
                [](const auto& contact) {
                  return contact.key ==
                             LowerCockpitTriangleKey{
                                 LowerCockpitContactBuffer::original, 5, 144} &&
                         contact.object == 766 &&
                         contact.intersection == CabinIntersection::interior;
                }),
        "full posed moving hardware remains a real neighboring obstacle");
  const auto cropped = require(assess_lower_cockpit_reservations(
      *geometry.lower_contact(), {1, floor_y, 3.84}, {1, floor_y, 3.84}));
  check(!cropped.coverage_complete && !cropped.interior_clear,
        "retained triangle extrema cannot supply missing crop coverage");
}
auto source_refusals(const OriginBoardingSupport& catalog,
                     const OriginCabinSeamGeometry& original,
                     const std::string& original_bytes, const std::string& halo,
                     const std::string& policy) -> void {
  check(!decode_origin_lower_cockpit_contact({}, policy, original) &&
            !decode_origin_lower_cockpit_contact(halo, "}{", original),
        "malformed contact composition cannot produce corridor geometry");
  const auto baseline = nlohmann::json::parse(original_bytes);
  for (const auto id : {87219U, 87645U}) {
    auto changed = baseline;
    const auto vertex =
        changed["groups"][0]["triangles"][id][0].get<std::size_t>();
    changed["groups"][0]["vertices_micrometres"][vertex][2] =
        changed["groups"][0]["vertices_micrometres"][vertex][2]
            .get<std::int64_t>() +
        1;
    check(!decode_origin_cabin_seam_contact(changed.dump(), catalog),
          "sliver or taper coordinate substitution refuses immutable source "
          "gate");
  }
  auto missing = baseline;
  missing["groups"][0]["triangles"][87219] =
      missing["groups"][0]["triangles"][87211];
  check(!decode_origin_cabin_seam_contact(missing.dump(), catalog),
        "missing sliver cannot pass merely because false44.5mmgap still "
        "supports75percent");
}
} // namespace
int main() {
  try {
    const auto motion = require(
        decode_operating_motion_recipe(detail::kOperatingMotionRecipeJson));
    const auto catalog = require(decode_origin_boarding_support(
        detail::boarding_support_json(), motion));
    const auto original_bytes = read(APSIS_CABIN_CONTACT_FIXTURE);
    const auto original =
        require(decode_origin_cabin_seam_contact(original_bytes, catalog));
    const auto halo = read(APSIS_LOWER_CONTACT_FIXTURE);
    const auto policy = read(APSIS_LOWER_CONTACT_POLICY_FIXTURE);
    source_refusals(catalog, original, original_bytes, halo, policy);
    auto lower =
        require(decode_origin_lower_cockpit_contact(halo, policy, original));
    auto handle = require(make_origin_cabin_corridor_geometry(lower));
    auto geometry = std::move(handle);
    // The immutable handle supports empty views and refused queries after move.
    // NOLINTBEGIN(bugprone-use-after-move) -- Empty-handle query contract.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move) -- Empty-handle API.
    check(handle.selected_top_faces().empty() &&
              handle.lower_contact() == nullptr &&
              !assess_origin_cabin_corridor_proxy_support(handle,
                                                          {0, floor_y, 0}) &&
              !assess_origin_cabin_corridor_support(handle, 0) &&
              !certify_origin_cabin_corridor_translation(handle, 0, 0),
          "moved-from corridor refuses all queries and exposes empty views");
    // NOLINTEND(bugprone-use-after-move) -- End documented moved-from checks.
    auto retained = std::move(lower);
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Empty-handle factory contract.
    check(!make_origin_cabin_corridor_geometry(lower),
          "moved-from lower contact cannot qualify corridor");
    controls(geometry);
    source_checks(geometry);
    certificate_checks(geometry);
    check(
        require(lookup_lower_cockpit_face(
                    retained, {LowerCockpitContactBuffer::original, 0, 87219}))
                .object == 193,
        "sharing handles preserve immutable source attribution");
    check(require(certify_origin_cabin_seam_translation(original, 3.84, 3.45))
              .nonpenetrating_crossing,
          "original first-seam semantics remain independent and unchanged");
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << "EXCEPTION: " << error.what() << '\n';
  }
  std::cout << "Cabin corridor: " << checks << " checks, " << failures
            << " failures\n";
  return failures ? 1 : 0;
}
