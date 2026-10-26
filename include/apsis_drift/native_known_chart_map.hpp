#pragma once

#include "apsis_drift/intersystem_contract.hpp"
#include "apsis_drift/simulation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace apsis_drift {
inline constexpr std::size_t kNativeKnownChartMapCapacity{8};
struct NativeKnownChartMapNode {
  SystemId system;
  std::string name;
  bool current{}, selected{};
  double plot_x{}, plot_y{};
  friend auto operator==(const NativeKnownChartMapNode&,
                         const NativeKnownChartMapNode&) -> bool = default;
};
// Presentation only: normalized oblique points from admitted system anchors.
struct NativeKnownChartMapView {
  std::uint32_t version{1};
  bool available{};
  SimulationTick tick{};
  std::uint32_t node_count{};
  std::array<NativeKnownChartMapNode, kNativeKnownChartMapCapacity> nodes{};
  std::string note;
  friend auto operator==(const NativeKnownChartMapView&,
                         const NativeKnownChartMapView&) -> bool = default;
};
} // namespace apsis_drift
