#include "apsis_drift/origin_boarding_support.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "apsis_drift/boarding_support_data.hpp"
#include "apsis_drift/operating_motion_recipe.hpp"

namespace {
using namespace apsis_drift;
using Json = nlohmann::json;
int failures{};
std::size_t checks{};
auto check(bool condition, std::string_view label) -> void {
  ++checks;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << label << '\n';
  }
}
template <class T> auto require(std::expected<T, std::string> result) -> T {
  if (!result) throw std::runtime_error(result.error());
  return std::move(*result);
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
auto length(RigidVector3 a) -> double {
  return std::sqrt(dot(a, a));
}
auto point(const OperatingTransform& t, RigidVector3 p) -> RigidVector3 {
  return add(add(add(scale(t.columns[0], p.x), scale(t.columns[1], p.y)),
                 scale(t.columns[2], p.z)),
             t.columns[3]);
}
auto centroid(const BoardingSelectedFace& f) -> RigidVector3 {
  return scale(add(add(f.vertices_rest_metres[0], f.vertices_rest_metres[1]),
                   f.vertices_rest_metres[2]),
               1.0 / 3);
}
auto representative_face(const OriginBoardingSupport& c,
                         const BoardingSupportPatch& p)
    -> const BoardingSelectedFace& {
  for (const auto index : p.selected_faces)
    if (c.selected_faces()[index].key.triangle == p.representative_triangle)
      return c.selected_faces()[index];
  throw std::runtime_error("Missing representative selected face");
}
auto query_for(const OriginBoardingSupport& c, std::uint32_t patch,
               const OperatingProgress& progress, RigidVector3 rest,
               BoardingTriangleKey key) -> BoardingCandidateQuery {
  const auto transform =
      require(boarding_support_group_transform(c, progress, key.group));
  const auto& p = c.patches()[patch];
  BoardingCandidateQuery query{patch, key, p.body_category,
                               point(transform, rest), std::nullopt};
  if (p.contact_side_direction_rest)
    query.body_side_probe_current_metres =
        point(transform, add(rest, scale(*p.contact_side_direction_rest, .01)));
  return query;
}
auto negatives(const OperatingMotionRecipe& motion) -> void {
  check(!decode_origin_boarding_support({}, motion), "empty buffer refuses");
  check(!decode_origin_boarding_support(
            std::string(kBoardingSupportMaximumDocumentBytes + 1, ' '), motion),
        "oversized buffer refuses before parse");
  check(!decode_origin_boarding_support(
            std::string(17, '[') + "0" + std::string(17, ']'), motion),
        "nesting bound refuses");
  check(!decode_origin_boarding_support("}{", motion),
        "unbalanced delimiters refuse");
  check(!decode_origin_boarding_support("{\"schema\":1,\"schema\":2}", motion),
        "duplicate field refuses");
  check(!decode_origin_boarding_support("{\"x\":NaN}", motion),
        "nonfinite JSON token refuses");
  const auto baseline = Json::parse(detail::boarding_support_json());
  const auto mutate = [&](auto alteration, std::string_view label,
                          std::string_view expected) {
    auto json = baseline;
    alteration(json);
    const auto result = decode_origin_boarding_support(json.dump(), motion);
    check(!result, label);
    check(!result && result.error().find(expected) != std::string::npos,
          "negative reaches intended validator before canonical gate");
  };
  mutate([](Json& j) { j["unknown"] = 1; }, "closed root refuses",
         "root schema");
  mutate([](Json& j) { j["version"] = true; }, "boolean version refuses",
         "integer type");
  mutate([](Json& j) { j["counts"]["objects"] = 1087.0; },
         "fractional integer representation refuses", "integer type");
  mutate([](Json& j) { j["groups"][0]["object_count"] = 1088; },
         "group count bounds refuse", "integer type");
  mutate([](Json& j) { j["groups"][0]["id"] = "unknown"; },
         "unknown fixed group refuses", "fixed group");
  mutate([](Json& j) { j["groups"][1]["motion_group"] = "unknown"; },
         "unknown motion relation refuses", "motion group");
  mutate([](Json& j) { j["objects"][0]["triangle_start"] = 1; },
         "initial object gap refuses", "ownership partition");
  mutate([](Json& j) { j["objects"][1]["triangle_start"] = 0; },
         "object overlap refuses", "ownership partition");
  mutate([](Json& j) { j["objects"][0]["triangle_count"] = 4294967295ULL; },
         "range overflow refuses", "integer type");
  mutate([](Json& j) { j["objects"][0]["default_collision"] = "allowed"; },
         "blanket collision waiver refuses", "obstacle policy");
  mutate([](Json& j) { j["objects"][0]["group"] = -1; },
         "negative group index refuses", "integer type");
  mutate(
      [](Json& j) {
        j["vertices"][0]["position_micrometres"] = Json::array({0, 0});
      },
      "vertex dimensions refuse", "array dimensions");
  mutate([](Json& j) { j["vertices"][0]["position_micrometres"][0] = true; },
         "boolean quantized value refuses", "quantized integer type");
  mutate(
      [](Json& j) { j["vertices"][0]["position_micrometres"][0] = 128000001; },
      "quantized bounds refuse", "quantized integer bound");
  mutate(
      [](Json& j) {
        j["vertices"][1]["source_vertex"] = j["vertices"][0]["source_vertex"];
      },
      "duplicate vertex identity refuses", "vertex identity");
  mutate([](Json& j) { j["faces"][0]["vertices"][0] = 1217; },
         "one-past vertex refuses", "integer type");
  mutate(
      [](Json& j) {
        j["faces"][0]["vertices"][0] = j["faces"][0]["vertices"][1];
      },
      "degenerate triangle refuses", "degenerate face");
  mutate(
      [](Json& j) {
        j["faces"][1]["source_triangle"] = j["faces"][0]["source_triangle"];
      },
      "duplicate triangle identity refuses", "face identity");
  mutate(
      [](Json& j) { j["patches"][0]["triangle_ranges"][0][0] = 4294967295ULL; },
      "patch interval overflow refuses", "integer type");
  mutate([](Json& j) { j["patches"][0]["triangle_ranges"][0][1] = 0; },
         "empty patch range refuses", "range bound");
  mutate([](Json& j) { j["patches"][0]["body_contact"] = "whole body"; },
         "altered body contract refuses", "body/identity");
  mutate([](Json& j) { j["patches"][0]["role"] = "any_contact"; },
         "unknown role refuses", "patch role");
  mutate([](Json& j) { j["patches"][0]["selected_faces"][0] = 1322; },
         "one-past selected face refuses", "integer type");
  mutate([](Json& j) { j["patches"][0]["representative_barycentric"][0] = 2; },
         "invalid barycentric refuses", "barycentric domain");
  mutate(
      [](Json& j) {
        j["patches"][0]["representative_contact_point_rest_m"][0] = 1e308;
      },
      "nonfinite/extreme source vector refuses", "scalar finite");
  mutate([](Json& j) { j["patches"][0]["surface_area_m2"] = 0; },
         "zero source area refuses", "area positive");
  mutate([](Json& j) { j["patches"][0]["surface_area_m2"] = .001; },
         "wrong measured area refuses", "derived bounds");
  mutate([](Json& j) { j["patches"][0]["bounds_rest_m"][0][0] = 0; },
         "wrong measured bounds refuse", "derived bounds");
  mutate(
      [](Json& j) {
        j["patches"][0]["required_point_predicate"]["unit_axis_rest"] =
            Json::array({0, 0, 0});
      },
      "zero rod axis refuses", "rod axis");
  mutate(
      [](Json& j) {
        j["patches"][0]["required_point_predicate"]["inclusive_interval_m"] =
            Json::array({1, -1});
      },
      "reversed rod interval refuses", "rod axis");
  mutate(
      [](Json& j) {
        j["patches"][0]["required_point_predicate"]["end_margin_m"] = .02;
      },
      "wrong rod end margin refuses", "rod axis");
  mutate(
      [](Json& j) { j["identities"]["contact_sha256"] = std::string(64, '0'); },
      "stale contact identity refuses", "source/contact/model/motion");
  mutate(
      [](Json& j) {
        j["identities"]["operating_motion_sha256"] = std::string(64, '0');
      },
      "stale motion identity refuses", "source/contact/model/motion");
  mutate(
      [](Json& j) {
        j["objects"][0]["semantic_label"] = "edited finite source";
      },
      "finite self-rehashed alteration cannot replace independent policy",
      "independently approved");
  mutate(
      [](Json& j) {
        j["objects"][0]["semantic_label"] = R"QUOTE(quoted " [ { \)QUOTE";
      },
      "quoted braces and escapes reach source-equivalence refusal",
      "independently approved");
  mutate(
      [](Json& j) {
        for (auto& patch : j["patches"]) {
          if (!patch["contact_side_direction_rest"].is_null()) {
            patch["contact_side_direction_rest"] = Json::array({0, 0, 0});
            break;
          }
        }
      },
      "nonunit declared side refuses", "side unit");
  auto changed_motion = motion;
  changed_motion.craft_tracks[0].knots.back().value += .001;
  check(!decode_origin_boarding_support(detail::boarding_support_json(),
                                        changed_motion),
        "unrelated edited finite recipe refuses");
  auto nan_motion = motion;
  nan_motion.craft_nodes[0].location_metres.x =
      std::numeric_limits<double>::quiet_NaN();
  check(!decode_origin_boarding_support(detail::boarding_support_json(),
                                        nan_motion),
        "nonfinite selected motion refuses");
}
auto partitions(const OriginBoardingSupport& catalog) -> void {
  std::uint32_t total{};
  for (std::uint32_t g = 0; g < catalog.groups().size(); ++g) {
    const auto& group = catalog.groups()[g];
    std::uint32_t next{};
    for (std::uint32_t i = group.object_start;
         i < group.object_start + group.object_count; ++i) {
      const auto& object = catalog.objects()[i];
      check(object.triangle_start == next,
            "gap-free object starts in exact source order");
      check(
          require(lookup_boarding_triangle(catalog, {g, object.triangle_start}))
                  .object == i,
          "every object first triangle attributes exactly");
      check(require(lookup_boarding_triangle(
                        catalog,
                        {g, object.triangle_start + object.triangle_count - 1}))
                    .object == i,
            "every object last triangle attributes exactly");
      for (std::uint32_t t = object.triangle_start;
           t < object.triangle_start + object.triangle_count; ++t) {
        check(require(lookup_boarding_triangle(catalog, {g, t})).object == i,
              "exhaustive triangle partition attribution");
        ++total;
      }
      next += object.triangle_count;
    }
    check(next == group.triangle_count, "group endpoints complete");
    check(!lookup_boarding_triangle(catalog, {g, group.triangle_count}),
          "group exclusive endpoint refuses");
  }
  check(total == 475212, "all 475212 triangles attributed exactly once");
  check(require(lookup_boarding_triangle(catalog, {0, 0})).object !=
            require(lookup_boarding_triangle(catalog, {14, 0})).object,
        "same numeric triangle in different group has different attribution");
  bool overlapping{};
  for (const auto& face : catalog.selected_faces())
    if (require(lookup_boarding_triangle(catalog, face.key))
            .candidate_patches.size() > 1)
      overlapping = true;
  check(overlapping, "intentional boot/grasp patch overlap remains valid");
}
auto rod_sample(const OriginBoardingSupport& c, const BoardingSupportPatch& p,
                double target) -> std::pair<BoardingTriangleKey, RigidVector3> {
  const auto& rod = *p.rod;
  for (const auto index : p.selected_faces) {
    const auto& face = c.selected_faces()[index];
    const auto& v = face.vertices_rest_metres;
    for (std::size_t i = 0; i < 3; ++i) {
      const auto a = dot(sub(v[i], rod.origin_rest_metres), rod.unit_axis_rest);
      const auto b =
          dot(sub(v[(i + 1) % 3], rod.origin_rest_metres), rod.unit_axis_rest);
      if (std::min(a, b) <= target && target <= std::max(a, b) &&
          std::abs(b - a) > 1e-12) {
        const auto t = (target - a) / (b - a);
        return {face.key, add(v[i], scale(sub(v[(i + 1) % 3], v[i]), t))};
      }
    }
  }
  throw std::runtime_error("Requested source rod sample unavailable");
}
auto surfaces(const OriginBoardingSupport& catalog,
              const OperatingMotionRecipe& motion) -> void {
  const OperatingProgress progress{.425, .137, .775, .483};
  bool aabb_counterexample{}, reversed_counterexample{};
  bool double_ladder_negative{}, double_seat_negative{};
  for (std::uint32_t i = 0; i < catalog.patches().size(); ++i) {
    const auto& patch = catalog.patches()[i];
    const auto& face = representative_face(catalog, patch);
    auto query = query_for(catalog, i, progress,
                           patch.representative_point_rest_metres, face.key);
    const auto evidence =
        require(assess_boarding_candidate_surface(catalog, progress, query));
    check(evidence.body_category_matches && evidence.triangle_matches_patch &&
              evidence.point_on_triangle,
          "each real representative remains on admitted posed triangle");
    check(length(sub(evidence.point_rest_metres,
                     patch.representative_point_rest_metres)) < 1e-12,
          "full owner delta inverse maps once to exact rest");
    const auto& group = catalog.groups()[face.key.group];
    if (group.owner == BoardingOwner::craft && group.motion_group) {
      auto doubled = query;
      const auto delta = require(
          boarding_support_group_transform(catalog, progress, face.key.group));
      doubled.point_current_metres = point(delta, doubled.point_current_metres);
      const auto rejected = require(
          assess_boarding_candidate_surface(catalog, progress, doubled));
      if (!rejected.point_on_triangle) {
        if (group.id.find("ladder") != std::string::npos)
          double_ladder_negative = true;
        if (group.id.find("seat") != std::string::npos)
          double_seat_negative = true;
      }
    }

    check(evidence.declared_side == (patch.contact_side_direction_rest
                                         ? BoardingSideEvidence::matches
                                         : BoardingSideEvidence::undeclared),
          "declared source side versus explicit undeclared grasp");
    query.body_category = static_cast<BoardingBodyCategory>(
        (static_cast<std::size_t>(patch.body_category) + 1) % 6);
    check(!require(assess_boarding_candidate_surface(catalog, progress, query))
               .body_category_matches,
          "wrong named body category mismatches");
    query.body_category = patch.body_category;
    const auto normal =
        cross(sub(face.vertices_rest_metres[1], face.vertices_rest_metres[0]),
              sub(face.vertices_rest_metres[2], face.vertices_rest_metres[0]));
    const auto offplane = add(patch.representative_point_rest_metres,
                              scale(normal, 1e-4 / length(normal)));
    query = query_for(catalog, i, progress, offplane, face.key);
    check(!require(assess_boarding_candidate_surface(catalog, progress, query))
               .point_on_triangle,
          "off-plane point cannot borrow valid triangle identity");
    const auto center = scale(
        add(patch.bounds_rest_metres[0], patch.bounds_rest_metres[1]), .5);
    if (!require(assess_boarding_candidate_surface(
                     catalog, progress,
                     query_for(catalog, i, progress, center, face.key)))
             .point_on_triangle)
      aabb_counterexample = true;
    if (patch.contact_side_direction_rest) {
      query = query_for(catalog, i, progress,
                        patch.representative_point_rest_metres, face.key);
      query.body_side_probe_current_metres.reset();
      check(require(assess_boarding_candidate_surface(catalog, progress, query))
                    .declared_side == BoardingSideEvidence::missing,
            "absent sided probe remains missing");
      query.body_side_probe_current_metres = query.point_current_metres;
      check(require(assess_boarding_candidate_surface(catalog, progress, query))
                    .declared_side == BoardingSideEvidence::mismatch,
            "coincident probe supplies no positive side");
      const auto transform = require(
          boarding_support_group_transform(catalog, progress, face.key.group));
      query.body_side_probe_current_metres =
          point(transform, sub(patch.representative_point_rest_metres,
                               scale(*patch.contact_side_direction_rest, .01)));
      check(require(assess_boarding_candidate_surface(catalog, progress, query))
                    .declared_side == BoardingSideEvidence::mismatch,
            "opposite source side refuses");
      const auto side = *patch.contact_side_direction_rest;
      const auto tangent =
          cross(side, std::abs(side.x) < .9 ? RigidVector3{1, 0, 0}
                                            : RigidVector3{0, 1, 0});
      query.body_side_probe_current_metres =
          point(transform, add(patch.representative_point_rest_metres,
                               scale(tangent, .01)));
      check(require(assess_boarding_candidate_surface(catalog, progress, query))
                    .declared_side == BoardingSideEvidence::mismatch,
            "tangent probe supplies no positive side");
      for (const auto index : patch.selected_faces) {
        const auto& reversed = catalog.selected_faces()[index];
        const auto& v = reversed.vertices_rest_metres;
        if (dot(cross(sub(v[1], v[0]), sub(v[2], v[0])), side) < 0) {
          const auto sample = centroid(reversed);
          const auto e = require(assess_boarding_candidate_surface(
              catalog, progress,
              query_for(catalog, i, progress, sample, reversed.key)));
          check(e.point_on_triangle &&
                    e.declared_side == BoardingSideEvidence::matches,
                "reversed-winding actual pad face uses declared side");
          reversed_counterexample = true;
        }
      }
    }
    if (patch.rod) {
      for (std::size_t end = 0; end < 2; ++end) {
        const auto limit = patch.rod->inclusive_interval_metres[end];
        const auto exact = rod_sample(catalog, patch, limit);
        const auto e = require(assess_boarding_candidate_surface(
            catalog, progress,
            query_for(catalog, i, progress, exact.second, exact.first)));
        check(e.point_on_triangle &&
                  e.rod_interval == BoardingPredicateEvidence::matches,
              "already trimmed inclusive rod endpoint passes without second "
              "margin");
        const auto outside =
            rod_sample(catalog, patch, limit + (end == 0 ? -1e-5 : 1e-5));
        const auto n = require(assess_boarding_candidate_surface(
            catalog, progress,
            query_for(catalog, i, progress, outside.second, outside.first)));
        check(n.point_on_triangle && n.triangle_matches_patch &&
                  n.rod_interval == BoardingPredicateEvidence::mismatch,
              "real rod face beyond trimmed endpoint mismatches despite "
              "triangle membership");
      }
    }
  }
  check(aabb_counterexample,
        "actual in-bounds point fails exact triangle surface evidence");
  check(reversed_counterexample,
        "actual source opposite-winding pad counterexample exercised");
  check(double_ladder_negative && double_seat_negative,
        "double-applying actual ladder and seat delta loses the admitted "
        "surface");

  const auto pose = require(evaluate_operating_motion(motion, progress));
  bool d1_local_negative{};
  for (std::uint32_t i = 0; i < catalog.groups().size(); ++i) {
    const auto& group = catalog.groups()[i];
    const auto delta =
        require(boarding_support_group_transform(catalog, progress, i));
    if (!group.motion_group) {
      check(delta == OperatingTransform{},
            "fixed owner group retains identity");
      continue;
    }
    const auto expected =
        group.owner == BoardingOwner::craft
            ? pose.craft_world_deltas[*group.motion_group]
            : pose.station_contact_deltas[*group.motion_group];
    check(delta == expected,
          "each group uses full admitted owner contact delta once");
    if (group.owner == BoardingOwner::station &&
        delta != pose.station_node_local[*group.motion_group])
      d1_local_negative = true;
  }
  check(d1_local_negative,
        "D1 imported local pose is demonstrably not canonical contact delta");
  auto query =
      query_for(catalog, 0, progress,
                catalog.patches()[0].representative_point_rest_metres,
                representative_face(catalog, catalog.patches()[0]).key);
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  query.point_current_metres.x = nan;
  check(!assess_boarding_candidate_surface(catalog, progress, query),
        "nonfinite query point refuses");
  query.point_current_metres.x = 0;
  query.body_side_probe_current_metres = RigidVector3{nan, 0, 0};
  check(!assess_boarding_candidate_surface(catalog, progress, query),
        "nonfinite side probe refuses");
  query.body_side_probe_current_metres.reset();
  query.body_category = static_cast<BoardingBodyCategory>(255);
  check(!assess_boarding_candidate_surface(catalog, progress, query),
        "unknown body enum refuses");
  query.body_category = BoardingBodyCategory::hand_fingers;
  query.patch = 42;
  check(!assess_boarding_candidate_surface(catalog, progress, query),
        "one-past patch refuses");
  check(!boarding_support_group_transform(catalog, {nan, 0, 0, 0}, 0),
        "nonfinite progress refuses even fixed group");
  check(!boarding_support_group_transform(catalog, {1.01, 0, 0, 0}, 0),
        "out-of-domain progress refuses");
  check(!boarding_support_group_transform(catalog, progress, 32),
        "one-past group refuses");
  const auto& first = catalog.patches()[0];
  const auto& owner = catalog.objects()[first.object];
  if (owner.triangle_start > 0) {
    query =
        query_for(catalog, 0, progress, first.representative_point_rest_metres,
                  {owner.group, owner.triangle_start - 1});
    const auto n =
        require(assess_boarding_candidate_surface(catalog, progress, query));
    check(!n.triangle_matches_patch && !n.point_on_triangle &&
              n.rod_interval == BoardingPredicateEvidence::not_evaluated,
          "neighbor object triangle cannot acquire another patch");
  }
  const auto again = require(assess_boarding_candidate_surface(
      catalog, progress,
      query_for(catalog, 0, progress, first.representative_point_rest_metres,
                representative_face(catalog, first).key)));
  check(again.point_on_triangle,
        "query refusals leave immutable catalog intact");
}
} // namespace
int main() {
  try {
    const auto motion = require(
        decode_operating_motion_recipe(detail::kOperatingMotionRecipeJson));
    negatives(motion);
    auto catalog = require(decode_origin_boarding_support(
        detail::boarding_support_json(), motion));
    partitions(catalog);
    surfaces(catalog, motion);
    auto supplied_motion = motion;
    const auto privately_bound = require(decode_origin_boarding_support(
        detail::boarding_support_json(), supplied_motion));
    supplied_motion.version = 0;
    check(require(boarding_support_group_transform(
              privately_bound, {.425, .137, .775, .483}, 5)) ==
              require(boarding_support_group_transform(
                  catalog, {.425, .137, .775, .483}, 5)),
          "catalog privately owns qualified recipe despite caller mutation");

    const auto retained = catalog;
    auto moved = std::move(catalog);
    // NOLINTBEGIN(bugprone-use-after-move) -- Catalog explicitly supports empty
    // views and refusals after move.
    check(catalog.groups().empty() && catalog.objects().empty() &&
              catalog.patches().empty() && catalog.selected_faces().empty(),
          "moved-from handle has empty spans");
    check(!lookup_boarding_triangle(catalog, {0, 0}) &&
              !boarding_support_group_transform(catalog, {}, 0),
          "moved-from queries refuse");
    // NOLINTEND(bugprone-use-after-move) -- End documented moved-from catalog
    // contract checks.

    check(require(lookup_boarding_triangle(moved, {0, 0})).object == 0 &&
              retained.objects().size() == 1087,
          "shared immutable views survive copy and move");
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << "EXCEPTION: " << error.what() << '\n';
  }
  std::cout << "Boarding support: " << checks << " checks, " << failures
            << " failures\n";
  return failures ? 1 : 0;
}
