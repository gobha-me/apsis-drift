#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "apsis_drift/origin_station.hpp"
#include "apsis_drift/origin_walker.hpp"

namespace apsis_drift {
struct OriginCrewTopic {
  std::uint32_t id{};
  std::string_view title, text;
};
// One authored station fixture, identified by (station, ordinal). Conversation
// is read-only; neither the crew nor its display state adds a save generation.
struct OriginCrewView {
  OriginStationId station;
  std::uint32_t ordinal{1};
  std::string_view name{"Bay attendant"};
  RigidVector3 foot_position_metres{1.3, 0, 0};
  double heading_radians{}, height_metres{1.85};
  bool available{};
  std::string reason;
  std::array<OriginCrewTopic, 5> topics{};
  std::size_t topic_count{3};
  std::string_view remark;
};
// Read-only presentation of a personal report. Neither the private target nor
// a generated position crosses this boundary; navigation still needs sensors.
struct NativeStationStellarLeadView {
  bool selected{}, kept{}, can_keep{};
  std::optional<SimulationTick> offer_tick, received_tick;
  std::string_view status{"unselected"}, source{""};
  std::string_view note{"This journey has no station stellar report"};
};
[[nodiscard]] auto origin_station_crew(const OriginStationDescriptor&,
                                       const OriginWalkerState&, SimulationTick)
    -> std::expected<OriginCrewView, std::string>;
} // namespace apsis_drift
