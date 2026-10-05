#include "apsis_drift/origin_lower_cockpit_contact.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>

#include <nlohmann/json.hpp>

#include "apsis_drift/boarding_support_data.hpp"
#include "apsis_drift/operating_motion_recipe.hpp"
#include "apsis_drift/origin_cabin_corridor.hpp"
#include "apsis_drift/origin_stowed_contact_partition.hpp"
#include "apsis_drift/stowed_contact_data.hpp"

namespace {
using namespace apsis_drift;
using Json = nlohmann::json;
int failures{};
auto check(bool value, std::string_view label) -> void {
  if (!value) {
    std::cerr << "FAIL: " << label << '\n';
    ++failures;
  }
}
template <class T> auto require(std::expected<T, std::string> value) -> T {
  if (!value) throw std::runtime_error(value.error());
  return std::move(*value);
}
auto read(const char* path) -> std::string {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Missing contact fixture");
  return {std::istreambuf_iterator<char>(file), {}};
}
auto selected_bytes() -> std::string {
  std::string result;
  for (auto chunk : detail::kStowedContactChunks)
    result.append(chunk);
  return result;
}
constexpr std::array<StowedContactSourceRange, 8> removed{{
    {846, 11, "Anti-submarining strap", {108, 20}},
    {853, 11, "Buckle release", {2720, 108}},
    {886, 11, "Five-point buckle", {13940, 108}},
    {891, 11, "Lap restraint", {14480, 20}},
    {892, 11, "Lap restraint.001", {14500, 20}},
    {904, 11, "Seat service manifold", {20228, 108}},
    {905, 11, "Shoulder restraint", {20336, 36}},
    {906, 11, "Shoulder restraint.001", {20372, 36}},
}};
auto is_removed(LowerCockpitTriangleKey key) -> bool {
  return key.buffer == LowerCockpitContactBuffer::original &&
         std::ranges::any_of(removed, [&](const auto& range) {
           return key.group == range.group &&
                  key.triangle >= range.triangles.start &&
                  key.triangle - range.triangles.start < range.triangles.count;
         });
}
auto same_face(const LowerCockpitFace& a, const LowerCockpitFace& b) -> bool {
  return a.key == b.key && a.object == b.object &&
         a.source_object == b.source_object &&
         a.evaluated_source_triangle == b.evaluated_source_triangle &&
         a.points_current_metres == b.points_current_metres &&
         a.unit_normal_current == b.unit_normal_current;
}
auto distance(RigidVector3 a, RigidVector3 b) -> double {
  return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}
// Independent archived combined source pose: operating-motion-fixtures-01/
// craft-combined.json, tuple roof=1, door=1, seat=1. Its binary32 matrix has
// cos(pi/2)=-4.371138828673793e-8; comparison tolerance covers source rounding.
// This is the complete ancestor-composed delta, not just seat translation.
auto source_pose(RigidVector3 p) -> RigidVector3 {
  constexpr double c = -4.371138828673793e-8;
  return {c * p.x + p.z + 2.5299999713897705, p.y + .1599999964237213,
          -p.x + c * p.z - 1.7000001668930054};
}
using ContactId = std::tuple<LowerCockpitContactBuffer, std::uint32_t,
                             std::uint32_t, CabinProxyPart>;
auto id(const LowerCockpitReservationContact& c) -> ContactId {
  return {c.key.buffer, c.key.group, c.key.triangle, c.part};
}
auto positive(const OriginLowerCockpitContact& base,
              const OriginLowerCockpitContact& selected, const Json& document)
    -> void {
  check(base.replacement_objects().empty() &&
            base.stowed_partition() == nullptr,
        "base remains uninstalled");
  check(selected.objects().size() == 75 &&
            selected.replacement_objects().size() == 13,
        "halo inventory separate from thirteen replacements");
  const auto* partition = selected.stowed_partition();
  check(partition != nullptr, "effective handle retains immutable partition");
  if (!partition) return;
  std::size_t removed_faces{}, retained_seat_faces{}, retained_seat_objects{};
  for (const auto& range : partition->removed()) {
    removed_faces += range.triangles.count;
    const auto expected = std::ranges::find_if(removed, [&](const auto& r) {
      return r.original_object == range.original_object;
    });
    check(expected != removed.end() && expected->group == range.group &&
              expected->source_object == range.source_object &&
              expected->triangles.start == range.triangles.start &&
              expected->triangles.count == range.triangles.count,
          "removed inventory equals independent whole-source ranges");
  }
  for (const auto& range : partition->retained()) {
    if (range.group != 11) continue;
    ++retained_seat_objects;
    retained_seat_faces += range.triangles.count;
    for (const auto face :
         {range.triangles.start,
          range.triangles.start + range.triangles.count - 1}) {
      const LowerCockpitTriangleKey key{LowerCockpitContactBuffer::original, 11,
                                        face};
      check(same_face(require(lookup_lower_cockpit_face(base, key)),
                      require(lookup_lower_cockpit_face(selected, key))),
            "retained moving-source endpoints unchanged");
    }
  }
  check(partition->removed().size() == 8 && removed_faces == 456 &&
            retained_seat_objects == 56 && retained_seat_faces == 22148,
        "whole seat inventory conserved");
  for (const auto& range : removed)
    for (std::uint32_t i = 0; i < range.triangles.count; ++i) {
      const LowerCockpitTriangleKey key{LowerCockpitContactBuffer::original, 11,
                                        range.triangles.start + i};
      check(lookup_lower_cockpit_face(base, key).has_value() &&
                !lookup_lower_cockpit_face(selected, key),
            "every removed original refuses without redirection");
    }
  for (std::uint32_t i = 0; i < 8100; ++i) {
    const LowerCockpitTriangleKey key{LowerCockpitContactBuffer::halo, 0, i};
    check(same_face(require(lookup_lower_cockpit_face(base, key)),
                    require(lookup_lower_cockpit_face(selected, key))),
          "all halo faces retain points normals namespace and attribution");
  }
  for (const auto key :
       {LowerCockpitTriangleKey{LowerCockpitContactBuffer::original, 0, 8},
        LowerCockpitTriangleKey{LowerCockpitContactBuffer::original, 5, 144}})
    check(same_face(require(lookup_lower_cockpit_face(base, key)),
                    require(lookup_lower_cockpit_face(selected, key))),
          "unrelated fixed originals unchanged");
  std::uint32_t cursor{};
  std::size_t connectors{};
  bool differs_from_rest{}, differs_from_twice{};
  for (std::size_t oi = 0; oi < document["objects"].size(); ++oi) {
    const auto& raw = document["objects"][oi];
    const auto& object = selected.replacement_objects()[oi];
    check(object.source_object == raw["source_object"].get<std::string>() &&
              object.triangle_start == cursor &&
              object.triangle_count == raw["triangles"].size() &&
              object.source_evaluated_triangles == object.triangle_count &&
              object.evaluated_source_triangles.size() == object.triangle_count,
          "replacement inventory complete and gap-free");
    const bool connector = raw["introduced_connector"].get<bool>();
    connectors += connector;
    check(object.original_object.has_value() != connector,
          "connector attribution never invents original object");
    for (std::uint32_t ti = 0; ti < object.triangle_count; ++ti) {
      const auto face = require(lookup_lower_cockpit_face(
          selected, {LowerCockpitContactBuffer::replacement, 0, cursor++}));
      check(face.object == oi && face.source_object == object.source_object &&
                face.evaluated_source_triangle == ti &&
                object.evaluated_source_triangles[ti] == ti,
            "every replacement source face appears exactly once");
      for (std::size_t vi = 0; vi < 3; ++vi) {
        const auto& v =
            raw["vertices"][raw["triangles"][ti][vi].get<std::size_t>()];
        const RigidVector3 rest{v[0].get<double>(), v[1].get<double>(),
                                v[2].get<double>()};
        const auto current = face.points_current_metres[vi];
        const auto once = source_pose(rest);
        check(
            distance(current, once) < 5e-6,
            "all replacement corners match independent full owner delta once");
        differs_from_rest |= distance(current, rest) > .1;
        differs_from_twice |= distance(current, source_pose(once)) > .1;
      }
      check(std::abs(std::hypot(face.unit_normal_current.x,
                                face.unit_normal_current.y,
                                face.unit_normal_current.z) -
                     1) < 1e-12,
            "replacement finite nondegenerate unit normal");
    }
  }
  check(cursor == 1508 && connectors == 5 && differs_from_rest &&
            differs_from_twice,
        "1508 faces five connectors and nontrivial exactly-once transform");
}
auto queries(const OriginLowerCockpitContact& base,
             const OriginLowerCockpitContact& selected) -> void {
  const LowerCockpitTriangleKey replacement_key{
      LowerCockpitContactBuffer::replacement, 0, 0};
  const auto replacement =
      require(lookup_lower_cockpit_face(selected, replacement_key));
  RigidVector3 centroid{};
  for (auto p : replacement.points_current_metres) {
    centroid.x += p.x / 3;
    centroid.y += p.y / 3;
    centroid.z += p.z / 3;
  }
  // The real shoulder triangle lies strictly inside the declared body box
  // centered in X/Z on its centroid, with the admitted floor foot height.
  const RigidVector3 foot{centroid.x, kCabinSeamFloorMetres, centroid.z};
  for (auto p : replacement.points_current_metres)
    check(p.x > foot.x - .32 && p.x < foot.x + .32 && p.z > foot.z - .32 &&
              p.z < foot.z + .32 && p.y > foot.y + .045 &&
              p.y < foot.y + .045 + 1.93,
          "chosen actual replacement triangle is strictly within body proxy");
  const auto hit =
      require(assess_lower_cockpit_reservations(selected, foot, foot));
  check(hit.coverage_complete && !hit.interior_clear &&
            std::ranges::any_of(hit.contacts,
                                [&](const auto& c) {
                                  return c.key == replacement_key &&
                                         c.part == CabinProxyPart::body &&
                                         c.intersection ==
                                             CabinIntersection::interior &&
                                         c.object == 0 &&
                                         c.evaluated_source_triangle == 0;
                                }),
        "effective reservations include actual replacement body intrusion");
  check(require(assess_lower_cockpit_surface_point(selected, replacement_key,
                                                   centroid))
            .point_on_face,
        "replacement namespace surface query uses installed face");
  for (const auto p :
       {RigidVector3{0, -.23, -1.265}, RigidVector3{0, -.231, -1.265},
        RigidVector3{0, -.24, -.64}, RigidVector3{0, -.1, 5.5},
        RigidVector3{0, -.1, 3.84}}) {
    const auto old = require(assess_lower_cockpit_reservations(base, p, p));
    const auto now = require(assess_lower_cockpit_reservations(selected, p, p));
    check(now.coverage_complete == old.coverage_complete,
          "replacement preserves declared reservation coverage");
    std::set<ContactId> unique;
    bool interior{};
    for (const auto& c : now.contacts) {
      check(!is_removed(c.key) && unique.insert(id(c)).second,
            "effective reservation has no removed face or duplicate key/part");
      interior |= c.intersection == CabinIntersection::interior;
      if (c.key.buffer != LowerCockpitContactBuffer::replacement) {
        const auto found = std::ranges::find_if(
            old.contacts, [&](const auto& o) { return id(o) == id(c); });
        check(found != old.contacts.end() && found->object == c.object &&
                  found->intersection == c.intersection &&
                  found->evaluated_source_triangle ==
                      c.evaluated_source_triangle,
              "retained reservation evidence matches base exactly");
      }
    }
    for (const auto& c : old.contacts)
      if (!is_removed(c.key))
        check(unique.contains(id(c)), "no retained contact disappears");
    check(now.interior_clear == (now.coverage_complete && !interior),
          "clearance reports actual effective interior contacts");
  }
  const auto corridor = require(make_origin_cabin_corridor_geometry(selected));
  const auto old_corridor = require(make_origin_cabin_corridor_geometry(base));
  check(corridor.selected_top_faces().size() ==
            old_corridor.selected_top_faces().size(),
        "corridor retains declared floor support roster");
  const auto certificate =
      require(certify_origin_cabin_corridor_translation(corridor, 3.84, 3.45));
  const auto direct = require(assess_lower_cockpit_reservations(
      selected, {0, kCabinCorridorFloorMetres, 3.84},
      {0, kCabinCorridorFloorMetres, 3.45}));
  check(
      certificate.swept_clearance.coverage_complete ==
              direct.coverage_complete &&
          certificate.swept_clearance.interior_clear == direct.interior_clear &&
          certificate.swept_clearance.contacts.size() == direct.contacts.size(),
      "corridor consumes effective reservation handle");
  check(!assess_lower_cockpit_reservations(
            selected, {0, std::numeric_limits<double>::quiet_NaN(), 0}, {}),
        "nonfinite query refuses");
  for (const auto key :
       {LowerCockpitTriangleKey{LowerCockpitContactBuffer::replacement, 1, 0},
        LowerCockpitTriangleKey{LowerCockpitContactBuffer::replacement, 0,
                                1508},
        LowerCockpitTriangleKey{LowerCockpitContactBuffer::halo, 0, 8100}})
    check(!lookup_lower_cockpit_face(selected, key),
          "buffer bounds remain distinct");
}
auto invalid(const OriginLowerCockpitContact& base, const std::string& bytes)
    -> void {
  const auto original = Json::parse(bytes);
  auto deny = [&](Json j, std::string_view label) {
    const auto result = make_origin_stowed_lower_cockpit_contact(
        base, j.dump(), detail::kStowedFrameJson);
    check(!result && result.error().find("independently approved bytes") ==
                         std::string::npos,
          label);
  };
  for (const auto& value : {Json(true), Json(-1), Json(0.5), Json(999999)}) {
    auto j = original;
    j["objects"][0]["triangles"][0][0] = value;
    deny(j, "typed finite bounded index");
  }
  auto j = original;
  j["objects"][0]["triangles"][0][1] = j["objects"][0]["triangles"][0][0];
  deny(j, "repeated index refuses");
  j = original;
  j["objects"][0]["vertices"][0][0] = true;
  deny(j, "boolean coordinate refuses");
  j = original;
  j["objects"][0]["vertices"][0][0] =
      std::nextafter(j["objects"][0]["vertices"][0][0].get<double>(),
                     std::numeric_limits<double>::infinity());
  deny(j, "non-binary32 coordinate refuses before byte gate");
  j = original;
  j["objects"][0]["vertices"][0][0] = 21;
  deny(j, "finite coordinate domain bound");
  j = original;
  j["objects"][0]["vertices"][0].push_back(0);
  deny(j, "coordinate dimensions closed");
  j = original;
  j["objects"][0]["source_faces"][1] = 0;
  deny(j, "duplicate source face refuses");
  j = original;
  j["objects"][0]["original_catalog_range"]["count"] = 35;
  deny(j, "partial original removal refuses");
  j = original;
  j["objects"][8]["original_catalog_range"] =
      j["objects"][0]["original_catalog_range"];
  deny(j, "connector cannot remove original");
  j = original;
  j["runtime_actor_admitted"] = true;
  deny(j, "scope cannot grant actor admission");
  j = original;
  j["objects"].erase(0);
  deny(j, "missing object refuses");
  j = original;
  j["unexpected"] = 0;
  deny(j, "closed contact keys");
  j = original;
  j["model_sha256"] = std::string(64, '0');
  deny(j, "model binding refuses");
  check(!make_origin_stowed_lower_cockpit_contact(base, bytes + " ",
                                                  detail::kStowedFrameJson),
        "equivalent unapproved bytes refuse");
  check(!make_origin_stowed_lower_cockpit_contact(
            base, std::string(kStowedCockpitMaximumDocumentBytes + 1, ' '),
            detail::kStowedFrameJson),
        "contact byte cap");
  check(!make_origin_stowed_lower_cockpit_contact(
            base, bytes, std::string(kStowedCockpitMaximumFrameBytes + 1, ' ')),
        "frame byte cap");
  for (const auto field : {"catalog_group", "motion_group", "units",
                           "operating_tuple", "source_matrices"}) {
    auto frame = Json::parse(detail::kStowedFrameJson);
    frame[field] = nullptr;
    check(!make_origin_stowed_lower_cockpit_contact(base, bytes, frame.dump()),
          "wrong frame semantics refuse");
  }
  auto malformed = bytes;
  malformed.replace(malformed.find("\"schema\":"), 9, "\"schema\":NaN,");
  check(!make_origin_stowed_lower_cockpit_contact(base, malformed,
                                                  detail::kStowedFrameJson),
        "nonfinite JSON token refuses");
  check(!make_origin_stowed_lower_cockpit_contact(
            base, bytes.substr(0, bytes.size() - 1), detail::kStowedFrameJson),
        "truncated contact refuses");
  const auto duplicate =
      std::string("{\"schema\":\"duplicate\",") + bytes.substr(1);
  const auto duplicate_result = make_origin_stowed_lower_cockpit_contact(
      base, duplicate, detail::kStowedFrameJson);
  check(!duplicate_result &&
            duplicate_result.error().find("duplicate key") != std::string::npos,
        "duplicate JSON keys refuse before byte gate");
  const auto nested = std::string(17, '[') + "0" + std::string(17, ']');
  const auto nested_result = make_origin_stowed_lower_cockpit_contact(
      base, nested, detail::kStowedFrameJson);
  check(!nested_result &&
            nested_result.error().find("nesting bound") != std::string::npos,
        "JSON depth cap refuses before byte gate");
  check(base.replacement_objects().empty() &&
            base.stowed_partition() == nullptr,
        "invalid installation never alters base");
}
} // namespace
int main() {
  try {
    const auto motion = require(
        decode_operating_motion_recipe(detail::kOperatingMotionRecipeJson));
    const auto catalog = require(decode_origin_boarding_support(
        detail::boarding_support_json(), motion));
    const auto original = require(decode_origin_cabin_seam_contact(
        read(APSIS_CABIN_CONTACT_FIXTURE), catalog));
    const auto base = require(decode_origin_lower_cockpit_contact(
        read(APSIS_LOWER_CONTACT_FIXTURE),
        read(APSIS_LOWER_CONTACT_POLICY_FIXTURE), original));
    auto bytes = selected_bytes();
    const auto selected = require(make_origin_stowed_lower_cockpit_contact(
        base, bytes, detail::kStowedFrameJson));
    positive(base, selected, Json::parse(bytes));
    queries(base, selected);
    invalid(base, bytes);
    check(!make_origin_stowed_lower_cockpit_contact(selected, bytes,
                                                    detail::kStowedFrameJson),
          "double installation refuses");
    const auto retained_view = require(lookup_lower_cockpit_face(
        selected, {LowerCockpitContactBuffer::replacement, 0, 0}));
    auto shared = selected;
    auto moved = std::move(shared);
    bytes.clear();
    bytes.shrink_to_fit();
    check(same_face(retained_view, require(lookup_lower_cockpit_face(
                                       moved, retained_view.key))),
          "sharing move and temporary input lifetime");
    // NOLINTBEGIN(bugprone-use-after-move) -- documented empty moved-from
    // handle contract.
    check(shared.replacement_objects().empty() &&
              shared.stowed_partition() == nullptr &&
              !lookup_lower_cockpit_face(shared, retained_view.key),
          "moved-from handle refuses");
    // NOLINTEND(bugprone-use-after-move) -- end documented moved-from refusal
    // check.
  } catch (const std::exception& error) {
    std::cerr << "Unexpected: " << error.what() << '\n';
    return 1;
  }
  if (failures) return 1;
  std::cout << "Stowed lower cockpit contact contract passed\n";
}
