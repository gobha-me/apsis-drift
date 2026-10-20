#include "apsis_drift/origin_station_crew.hpp"

#include <cmath>
#include <numbers>

#include "apsis_drift/native_flight_session.hpp"

namespace apsis_drift {
auto origin_station_crew(const OriginStationDescriptor& station,
                         const OriginWalkerState& actor, SimulationTick tick)
    -> std::expected<OriginCrewView, std::string> {
  if (station != generate_origin_station(station.universe_seed))
    return std::unexpected("Crew requires the actual Origin station");
  if (auto valid = validate_origin_walker(actor); !valid)
    return std::unexpected(valid.error());
  OriginCrewView result;
  result.station = station.id;
  result.heading_radians = std::numbers::pi / 2;
  result.topics = {{{1, "Which way is my ship?",
                     "D1, through the workshop. Approach the ladder and use "
                     "the boarding action. Get seated before departure. "
                     "You can leave whenever you are ready."},
                    {2, "Any advice before I leave?",
                     "Watch your fuel and leave yourself a way home. The "
                     "flight guide is in Controls if you need it. No one "
                     "here is grading your departure. Officially."},
                    {3, "What about landing on a moon?",
                     "The landing control deploys the gear; you still fly "
                     "the descent. Releasing surface support does not lift "
                     "you off, either. That part takes up thrusters."}}};
  constexpr std::array<std::string_view, 3> remarks{
      "Quiet shift. I am not putting that in writing.",
      "Checklist says inspect the hull. Good. It has been expecting company.",
      "They spent the budget on the ship. I am still waiting on my third "
      "dimension."};
  // Independent derived display selection. No random stream consumption,
  // mutable chatter clock, tick addition or generated discovery is involved.
  result.remark = remarks[(station.id.value % remarks.size() +
                           (tick / 3600) % remarks.size()) %
                          remarks.size()];
  const auto& foot = actor.foot_position_metres;
  const auto dx = result.foot_position_metres.x - foot.x;
  const auto dz = result.foot_position_metres.z - foot.z;
  const auto distance = std::hypot(dx, dz);
  // This convex, source-checked hub head-height prism is open. The attendant
  // stands beyond the existing arrival guard and outside every player standing
  // reservation, preserving the original corridor and all historical poses.
  if (foot.x < -1 || foot.z < -.555 || foot.z > .555 || foot.y != 0 ||
      distance > 2.5) {
    result.reason = "Approach the attendant at the arrival guard";
  } else if (actor.velocity_metres_per_second != RigidVector3{}) {
    result.reason = "Stop moving to talk";
  } else if ((-std::sin(actor.heading_radians) * dx -
              std::cos(actor.heading_radians) * dz) < .5 * distance) {
    result.reason = "Face the attendant to talk";
  } else {
    result.available = true;
    result.reason = "Talk to the bay attendant";
  }
  return result;
}

auto NativeFreedomFlightSession::station_crew() const
    -> std::expected<OriginCrewView, std::string> {
  if (!actor_ || !docking_ || !docking_->attached || recovery_pending() ||
      (boarding_ && boarding_->phase != FreedomBoardingPhase::station))
    return std::unexpected("Station crew is available while walking on Origin");
  return origin_station_crew(
      generate_origin_station(document_.origin.recipe.universe_seed), *actor_,
      document_.flight.tick);
}
} // namespace apsis_drift
