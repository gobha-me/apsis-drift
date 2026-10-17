#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <utility>
#include <vector>

namespace apsis_drift::origin_walk_detail {
// Opt-in diagnostics for the bounded Origin route test. The caller owns the
// counter, and ordinary gameplay leaves the thread-local pointer null.
struct QueryWork {
  std::uint64_t queries{}, broad_entries{}, node_tests{}, triangle_tests{},
      candidate_ids{};
};
auto exchange_query_work(QueryWork*) noexcept -> QueryWork*;

struct Bounds {
  std::array<double, 3> low, high;
};

// Immutable broad phase over the admitted Origin triangles. Leaves retain
// original triangle IDs; query results use their original ascending order.
class TriangleIndex {
 public:
  TriangleIndex() = default;
  explicit TriangleIndex(std::vector<Bounds> bounds)
      : bounds_(std::move(bounds)), ids_(bounds_.size()) {
    std::iota(ids_.begin(), ids_.end(), std::size_t{});
    nodes_.reserve(bounds_.size() / 2 + 1);
    if (!ids_.empty()) build(0, ids_.size());
  }

  [[nodiscard]] auto query(const Bounds& box, QueryWork* work = nullptr) const
      -> std::vector<std::size_t> {
    if (work) ++work->queries;
    std::vector<std::size_t> result;
    if (!nodes_.empty()) collect(0, box, result, work);
    std::ranges::sort(result);
    if (work) work->candidate_ids += result.size();
    return result;
  }

 private:
  struct Node {
    Bounds bounds;
    std::size_t begin{}, count{}, left{}, right{};
  };
  static auto overlaps(const Bounds& a, const Bounds& b) -> bool {
    for (std::size_t i = 0; i < 3; ++i)
      if (a.low[i] > b.high[i] || a.high[i] < b.low[i]) return false;
    return true;
  }
  auto build(std::size_t begin, std::size_t end) -> std::size_t {
    auto extent = bounds_[ids_[begin]];
    for (auto i = begin + 1; i < end; ++i)
      for (std::size_t axis = 0; axis < 3; ++axis) {
        extent.low[axis] =
            std::min(extent.low[axis], bounds_[ids_[i]].low[axis]);
        extent.high[axis] =
            std::max(extent.high[axis], bounds_[ids_[i]].high[axis]);
      }
    const auto node = nodes_.size();
    nodes_.push_back({extent, begin, end - begin});
    if (end - begin <= 8) return node;
    std::size_t axis{};
    for (std::size_t i = 1; i < 3; ++i)
      if (extent.high[i] - extent.low[i] > extent.high[axis] - extent.low[axis])
        axis = i;
    const auto middle = begin + (end - begin) / 2;
    std::nth_element(ids_.begin() + static_cast<std::ptrdiff_t>(begin),
                     ids_.begin() + static_cast<std::ptrdiff_t>(middle),
                     ids_.begin() + static_cast<std::ptrdiff_t>(end),
                     [&](std::size_t a, std::size_t b) {
                       const auto ca =
                           bounds_[a].low[axis] + bounds_[a].high[axis];
                       const auto cb =
                           bounds_[b].low[axis] + bounds_[b].high[axis];
                       return ca < cb || (ca == cb && a < b);
                     });
    const auto left = build(begin, middle), right = build(middle, end);
    nodes_[node].left = left;
    nodes_[node].right = right;
    nodes_[node].count = 0;
    return node;
  }
  auto collect(std::size_t id, const Bounds& box,
               std::vector<std::size_t>& result, QueryWork* work) const
      -> void {
    const auto& node = nodes_[id];
    if (work) ++work->node_tests;
    if (!overlaps(node.bounds, box)) return;
    if (node.count == 0) {
      collect(node.left, box, result, work);
      collect(node.right, box, result, work);
      return;
    }
    if (work) work->broad_entries += node.count;
    for (auto i = node.begin; i < node.begin + node.count; ++i) {
      if (work) ++work->triangle_tests;
      if (overlaps(bounds_[ids_[i]], box)) result.push_back(ids_[i]);
    }
  }
  std::vector<Bounds> bounds_;
  std::vector<std::size_t> ids_;
  std::vector<Node> nodes_;
};
} // namespace apsis_drift::origin_walk_detail
