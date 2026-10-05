#pragma once

#include "apsis_drift/origin_boarding_support.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace apsis_drift {
inline constexpr std::size_t kStowedContactRemovalCount{8};
struct StowedContactSourceRange {
  std::uint32_t original_object{}, group{};
  std::string_view source_object;
  BoardingTriangleRange triangles{};
};
// Source bookkeeping only: this partition does not remove collision obstacles,
// admit replacement geometry or change any actor/runtime permission. Returned
// name views and inventories live while any sharing handle remains alive.
class OriginStowedContactPartition {
 public:
  struct Data;
  OriginStowedContactPartition(const OriginStowedContactPartition&) = default;
  OriginStowedContactPartition(OriginStowedContactPartition&&) noexcept =
      default;
  auto operator=(const OriginStowedContactPartition&)
      -> OriginStowedContactPartition& = default;
  auto operator=(OriginStowedContactPartition&&) noexcept
      -> OriginStowedContactPartition& = default;
  [[nodiscard]] auto removed() const
      -> std::span<const StowedContactSourceRange>;
  [[nodiscard]] auto retained() const
      -> std::span<const StowedContactSourceRange>;

 private:
  explicit OriginStowedContactPartition(std::shared_ptr<const Data>);
  std::shared_ptr<const Data> data_;
  friend auto validate_stowed_contact_partition(
      const OriginBoardingSupport&, std::span<const StowedContactSourceRange>)
      -> std::expected<OriginStowedContactPartition, std::string>;
  friend auto stowed_contact_partition_contains_removed(
      const OriginStowedContactPartition&, BoardingTriangleKey)
      -> std::expected<bool, std::string>;
};
// Exactly the eight parked restraint/manifold objects, each in its entirety.
// Submitted names/ranges must match the owned admitted catalog. Input order is
// immaterial; both inventories preserve catalog order. No partition storage is
// allocated until the complete eight-row selection passes validation.
[[nodiscard]] auto validate_stowed_contact_partition(
    const OriginBoardingSupport&, std::span<const StowedContactSourceRange>)
    -> std::expected<OriginStowedContactPartition, std::string>;
// Bounded original-catalog membership only; halo/replacement keys have no role
// here. Invalid keys and moved-from handles refuse rather than report retained.
[[nodiscard]] auto stowed_contact_partition_contains_removed(
    const OriginStowedContactPartition&, BoardingTriangleKey)
    -> std::expected<bool, std::string>;
} // namespace apsis_drift
