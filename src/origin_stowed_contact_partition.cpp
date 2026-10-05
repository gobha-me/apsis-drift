#include "apsis_drift/origin_stowed_contact_partition.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <utility>
#include <vector>

namespace apsis_drift {
namespace {
constexpr std::uint32_t selected_group{11};
constexpr std::array<std::string_view, kStowedContactRemovalCount>
    selected_names{"Anti-submarining strap", "Buckle release",
                   "Five-point buckle",      "Lap restraint",
                   "Lap restraint.001",      "Seat service manifold",
                   "Shoulder restraint",     "Shoulder restraint.001"};
auto range_for(std::uint32_t index, const BoardingSupportObject& object)
    -> StowedContactSourceRange {
  return {index,
          object.group,
          object.source_object,
          {object.triangle_start, object.triangle_count}};
}
} // namespace
struct OriginStowedContactPartition::Data {
  explicit Data(OriginBoardingSupport selected)
      : catalog(std::move(selected)) {}
  OriginBoardingSupport catalog;
  std::array<StowedContactSourceRange, kStowedContactRemovalCount> removed{};
  std::vector<StowedContactSourceRange> retained;
};
OriginStowedContactPartition::OriginStowedContactPartition(
    std::shared_ptr<const Data> selected)
    : data_(std::move(selected)) {
}
auto OriginStowedContactPartition::removed() const
    -> std::span<const StowedContactSourceRange> {
  return data_ ? std::span<const StowedContactSourceRange>{data_->removed}
               : std::span<const StowedContactSourceRange>{};
}
auto OriginStowedContactPartition::retained() const
    -> std::span<const StowedContactSourceRange> {
  return data_ ? std::span<const StowedContactSourceRange>{data_->retained}
               : std::span<const StowedContactSourceRange>{};
}
auto validate_stowed_contact_partition(
    const OriginBoardingSupport& catalog,
    std::span<const StowedContactSourceRange> submitted)
    -> std::expected<OriginStowedContactPartition, std::string> {
  if (submitted.size() != kStowedContactRemovalCount)
    return std::unexpected("Stowed partition requires exactly eight objects");
  const auto groups = catalog.groups();
  const auto objects = catalog.objects();
  if (groups.size() <= selected_group || objects.size() < submitted.size())
    return std::unexpected("Stowed partition requires an admitted catalog");
  const auto& group = groups[selected_group];
  if (group.id != "craft_seat_lift" || group.owner != BoardingOwner::craft ||
      group.motion_group != std::optional<std::size_t>{10})
    return std::unexpected("Stowed partition seat-lift source binding");
  std::array<bool, kStowedContactRemovalCount> seen{};
  std::array<std::uint32_t, kStowedContactRemovalCount> selected{};
  for (std::size_t i = 0; i < submitted.size(); ++i) {
    const auto& row = submitted[i];
    if (row.original_object >= objects.size() || row.group != selected_group)
      return std::unexpected("Stowed partition object/group bound");
    const auto& object = objects[row.original_object];
    if (object.group != row.group ||
        object.source_object != row.source_object ||
        object.triangle_start != row.triangles.start ||
        object.triangle_count != row.triangles.count)
      return std::unexpected("Stowed partition exact original object range");
    if (row.triangles.count == 0 ||
        row.triangles.start >= group.triangle_count ||
        row.triangles.count > group.triangle_count - row.triangles.start)
      return std::unexpected("Stowed partition triangle range bound");
    const auto name = std::find(selected_names.begin(), selected_names.end(),
                                row.source_object);
    if (name == selected_names.end())
      return std::unexpected("Stowed partition fixed source roster");
    const auto position =
        static_cast<std::size_t>(name - selected_names.begin());
    if (seen[position])
      return std::unexpected("Stowed partition duplicate source object");
    seen[position] = true;
    selected[i] = row.original_object;
    for (std::size_t j = 0; j < i; ++j) {
      const auto previous = submitted[j].triangles;
      if (row.triangles.start < previous.start + previous.count &&
          previous.start < row.triangles.start + row.triangles.count)
        return std::unexpected("Stowed partition overlapping original ranges");
    }
  }
  // Eight unique members of the fixed eight-name roster are complete. The
  // admitted catalog already proves its whole-object ownership partition.
  try {
    auto data = std::make_shared<OriginStowedContactPartition::Data>(catalog);
    data->retained.reserve(objects.size() - kStowedContactRemovalCount);
    std::size_t removed_count{};
    for (std::size_t i = 0; i < objects.size(); ++i) {
      const auto row = range_for(static_cast<std::uint32_t>(i), objects[i]);
      if (std::find(selected.begin(), selected.end(), i) != selected.end())
        data->removed[removed_count++] = row;
      else
        data->retained.push_back(row);
    }
    return OriginStowedContactPartition{std::move(data)};
  } catch (const std::exception& error) {
    return std::unexpected(error.what());
  }
}
auto stowed_contact_partition_contains_removed(
    const OriginStowedContactPartition& partition, BoardingTriangleKey key)
    -> std::expected<bool, std::string> {
  if (!partition.data_)
    return std::unexpected("Stowed partition moved-from handle");
  const auto groups = partition.data_->catalog.groups();
  if (key.group >= groups.size() ||
      key.triangle >= groups[key.group].triangle_count)
    return std::unexpected("Stowed partition original triangle bound");
  for (const auto& row : partition.data_->removed)
    if (key.group == row.group && key.triangle >= row.triangles.start &&
        key.triangle - row.triangles.start < row.triangles.count)
      return true;
  return false;
}
} // namespace apsis_drift
