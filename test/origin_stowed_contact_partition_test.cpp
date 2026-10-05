#include "apsis_drift/origin_lower_cockpit_contact.hpp"
#include "apsis_drift/origin_stowed_contact_partition.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "apsis_drift/boarding_support_data.hpp"
#include "apsis_drift/operating_motion_recipe.hpp"

namespace {
using namespace apsis_drift;
static_assert(!std::is_invocable_v<
              decltype(&stowed_contact_partition_contains_removed),
              const OriginStowedContactPartition&, LowerCockpitTriangleKey>);
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
constexpr std::array<StowedContactSourceRange, 8> selected{
    StowedContactSourceRange{846, 11, "Anti-submarining strap", {108, 20}},
    {853, 11, "Buckle release", {2720, 108}},
    {886, 11, "Five-point buckle", {13940, 108}},
    {891, 11, "Lap restraint", {14480, 20}},
    {892, 11, "Lap restraint.001", {14500, 20}},
    {904, 11, "Seat service manifold", {20228, 108}},
    {905, 11, "Shoulder restraint", {20336, 36}},
    {906, 11, "Shoulder restraint.001", {20372, 36}}};
auto same(const StowedContactSourceRange& a, const StowedContactSourceRange& b)
    -> bool {
  return a.original_object == b.original_object && a.group == b.group &&
         a.source_object == b.source_object &&
         a.triangles.start == b.triangles.start &&
         a.triangles.count == b.triangles.count;
}
auto negatives(const OriginBoardingSupport& catalog) -> void {
  check(!validate_stowed_contact_partition(catalog, {}), "empty selection");
  check(
      !validate_stowed_contact_partition(catalog, std::span{selected}.first(7)),
      "missing mandatory object");
  std::array<StowedContactSourceRange, 9> oversized{};
  std::copy(selected.begin(), selected.end(), oversized.begin());
  oversized.back() = selected.front();
  const auto too_many = validate_stowed_contact_partition(catalog, oversized);
  check(!too_many &&
            too_many.error().find("exactly eight") != std::string::npos,
        "oversized roster refuses before row access or partition allocation");
  for (std::size_t i = 0; i < selected.size(); ++i) {
    const auto mutation = [&](auto alter, std::string_view label) {
      auto rows = selected;
      alter(rows[i]);
      check(!validate_stowed_contact_partition(catalog, rows), label);
    };
    mutation([](auto& r) { r.original_object = 1087; }, "one-past object");
    mutation(
        [](auto& r) {
          r.original_object = std::numeric_limits<std::uint32_t>::max();
        },
        "maximum object");
    mutation([](auto& r) { r.group = 0; }, "wrong craft group");
    mutation([](auto& r) { r.group = 32; }, "one-past group");
    mutation([](auto& r) { r.source_object = "unselected source"; },
             "wrong source identity");
    mutation([](auto& r) { ++r.triangles.start; }, "shifted start");
    mutation([](auto& r) { --r.triangles.count; }, "partial removal");
    mutation([](auto& r) { ++r.triangles.count; }, "widened removal");
    mutation([](auto& r) { r.triangles.count = 0; }, "empty removal");
    mutation(
        [](auto& r) {
          r.triangles.start = std::numeric_limits<std::uint32_t>::max();
          r.triangles.count = std::numeric_limits<std::uint32_t>::max();
        },
        "overflow range");
    auto duplicate = selected;
    duplicate[i] = selected[(i + 1) % selected.size()];
    check(!validate_stowed_contact_partition(catalog, duplicate),
          "duplicate replaces a mandatory object");
  }
  for (const auto object : {std::uint32_t{847}, std::uint32_t{1086}}) {
    auto rows = selected;
    const auto& original = catalog.objects()[object];
    rows[0] = {object,
               original.group,
               original.source_object,
               {original.triangle_start, original.triangle_count}};
    check(!validate_stowed_contact_partition(catalog, rows),
          "genuine unrelated craft/station object still refuses");
  }
  auto overlap = selected;
  overlap[1].triangles = selected[0].triangles;
  check(!validate_stowed_contact_partition(catalog, overlap),
        "overlap cannot replace exact whole-object ranges");
}
auto conservation(const OriginBoardingSupport& catalog,
                  const OriginStowedContactPartition& partition) -> void {
  check(partition.removed().size() == 8 && partition.retained().size() == 1079,
        "complete immutable object inventories");
  for (std::size_t i = 0; i < selected.size(); ++i)
    check(same(partition.removed()[i], selected[i]),
          "fixed exact removed rows");
  std::size_t removed_faces{}, retained_faces{}, retained_seat_faces{},
      retained_seat_objects{}, total_faces{}, removed_objects{};
  std::size_t previous{};
  bool first = true;
  for (const auto& row : partition.retained()) {
    check(first || row.original_object > previous, "retained catalog ordering");
    first = false;
    previous = row.original_object;
    const auto& original = catalog.objects()[row.original_object];
    check(same(row, {row.original_object,
                     original.group,
                     original.source_object,
                     {original.triangle_start, original.triangle_count}}),
          "retained exact original attribution");
    if (row.group == 11) {
      ++retained_seat_objects;
      retained_seat_faces += row.triangles.count;
    }
  }
  // Independent oracle: original catalog ownership plus fixed selected object
  // IDs. No replacement geometry, transform, normal or clearance query.
  for (std::size_t g = 0; g < catalog.groups().size(); ++g) {
    for (std::uint32_t f = 0; f < catalog.groups()[g].triangle_count; ++f) {
      const BoardingTriangleKey key{static_cast<std::uint32_t>(g), f};
      const auto attribution = require(lookup_boarding_triangle(catalog, key));
      const bool expected =
          std::any_of(selected.begin(), selected.end(), [&](const auto& row) {
            return row.original_object == attribution.object;
          });
      const bool removed =
          require(stowed_contact_partition_contains_removed(partition, key));
      check(removed == expected, "every original face conserved exactly once");
      ++total_faces;
      if (removed)
        ++removed_faces;
      else
        ++retained_faces;
    }
    check(!stowed_contact_partition_contains_removed(
              partition, {static_cast<std::uint32_t>(g),
                          catalog.groups()[g].triangle_count}),
          "one-past triangle refuses for every group");
  }
  for (const auto& row : partition.removed())
    removed_objects += row.triangles.count;
  check(total_faces == 475212 && removed_faces == 456 &&
            retained_faces == 474756 && removed_objects == removed_faces,
        "all 475212 original faces partition into exact 456/474756 totals");
  check(retained_seat_objects == 56 && retained_seat_faces == 22148,
        "all 56 unrelated seat objects and 22148 faces retained");
  check(!stowed_contact_partition_contains_removed(partition, {32, 0}) &&
            !stowed_contact_partition_contains_removed(
                partition, {std::numeric_limits<std::uint32_t>::max(), 0}) &&
            !stowed_contact_partition_contains_removed(
                partition, {11, std::numeric_limits<std::uint32_t>::max()}),
        "invalid original namespaces never silently retain");
}
auto make_partition() -> OriginStowedContactPartition {
  const auto motion = require(
      decode_operating_motion_recipe(detail::kOperatingMotionRecipeJson));
  auto catalog = require(
      decode_origin_boarding_support(detail::boarding_support_json(), motion));
  negatives(catalog);
  const auto partition =
      require(validate_stowed_contact_partition(catalog, selected));
  conservation(catalog, partition);
  auto reordered = selected;
  std::reverse(reordered.begin(), reordered.end());
  const auto reordered_partition =
      require(validate_stowed_contact_partition(catalog, reordered));
  for (std::size_t i = 0; i < selected.size(); ++i)
    check(same(partition.removed()[i], reordered_partition.removed()[i]),
          "input permutation preserves catalog ordering");
  auto caller_rows = selected;
  std::array<std::string, 8> caller_names;
  for (std::size_t i = 0; i < selected.size(); ++i) {
    caller_names[i] = selected[i].source_object;
    caller_rows[i].source_object = caller_names[i];
  }
  auto owning =
      require(validate_stowed_contact_partition(catalog, caller_rows));
  for (auto& name : caller_names)
    name.assign("changed caller storage");
  caller_rows.fill({});
  for (std::size_t i = 0; i < selected.size(); ++i)
    check(same(owning.removed()[i], selected[i]),
          "partition owns catalog rather than caller name/range views");
  auto destination = std::move(catalog);
  // NOLINTBEGIN(bugprone-use-after-move) -- Catalog documents empty moved-from
  // views; this constructor must refuse rather than use stale attribution.
  check(!validate_stowed_contact_partition(catalog, selected),
        "moved-from source catalog refuses");
  // NOLINTEND(bugprone-use-after-move) -- End explicit catalog move contract.
  check(destination.objects().size() == 1087,
        "construction/refusals leave original catalog unchanged");
  return owning;
}
} // namespace
int main() {
  try {
    auto partition = make_partition();
    const auto saved_names = partition.removed();
    const auto retained = partition;
    auto moved = std::move(partition);
    // NOLINTBEGIN(bugprone-use-after-move) -- Empty-handle API contract.
    // Empty inventories and refusal are documented after moving the handle.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move) -- Empty-handle API.
    check(partition.removed().empty() && partition.retained().empty() &&
              !stowed_contact_partition_contains_removed(partition, {11, 108}),
          "moved-from partition views empty and queries refuse");
    // NOLINTEND(bugprone-use-after-move) -- End empty-handle contract.
    check(saved_names[0].source_object == "Anti-submarining strap" &&
              retained.retained().size() == 1079 &&
              require(stowed_contact_partition_contains_removed(moved,
                                                                {11, 108})) &&
              !require(
                  stowed_contact_partition_contains_removed(moved, {11, 107})),
          "views/query lifetime outlast source catalog and caller storage");
    auto assigned = retained;
    assigned = std::move(moved);
    // NOLINTBEGIN(bugprone-use-after-move) -- Empty-handle API contract.
    // Saved views retain shared ownership after move assignment.
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.Move) -- Empty-handle API.
    check(moved.removed().empty() && moved.retained().empty(),
          "move assignment empties source");
    // NOLINTEND(bugprone-use-after-move) -- End empty-handle contract.
    check(require(stowed_contact_partition_contains_removed(assigned,
                                                            {11, 20372})) &&
              saved_names[7].source_object == "Shoulder restraint.001",
          "sharing/copy/move assignment preserves exact attribution");
  } catch (const std::exception& error) {
    ++failures;
    std::cerr << "EXCEPTION: " << error.what() << '\n';
  }
  std::cout << "Stowed contact partition: " << checks << " checks, " << failures
            << " failures\n";
  return failures ? 1 : 0;
}
