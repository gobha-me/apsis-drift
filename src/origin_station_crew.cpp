#include "apsis_drift/origin_station_crew.hpp"

#include <cmath>
#include <numbers>

#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/save_file.hpp"

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
  auto crew = origin_station_crew(
      generate_origin_station(document_.origin.recipe.universe_seed), *actor_,
      document_.flight.tick);
  if (!crew) return crew;
  // The player explicitly shares an earned log. The attendant contributes no
  // independent catalog, signal, remote position or remembered encounter.
  if (knowledge_ && knowledge_->derelicts && knowledge_->derelicts->contact) {
    crew->topics[3] = {
        4, "Can you look at my contact log?",
        knowledge_->derelicts->inspection
            ? "Your exterior log records no active power or thrust and an "
              "unreadable registry plate. Ownership and whether anyone is "
              "aboard remain unknown. That's a report, not a solved mystery."
            : "Your log records a contact, not an explanation. The hull "
              "hasn't filled out the paperwork. If you go back, use the "
              "local bearing and Inspect derelict exterior. It is an "
              "exterior look, not permission to board."};
    crew->topic_count = 4;
  }
  const auto lead = station_stellar_lead_view();
  if (!lead) return std::unexpected{lead.error()};
  if (lead->kept || lead->can_keep) {
    std::string_view text =
        "There's an unverified stellar report beyond the chart. A rumor is "
        "cheaper than a navigation fix; that is its chief advantage. Your "
        "own sensors still have to find it. Keep the report if you want a "
        "personal note. No departure paperwork required.";
    if (lead->status == "unverified")
      text = "You kept the report. Your own sensors still need to resolve "
             "its location. Search nearby stars during ordinary space "
             "flight; this note is hearsay, not a navigation fix.";
    else if (lead->status == "sensor_fix_acquired")
      text = "Your instruments found the reported system. That makes it "
             "evidence. It doesn't tell us what you will find there.";
    else if (lead->status == "visited")
      text = "Your travel log records a visit to the reported system. The "
             "rumor now has more flight time than I do. Your own "
             "observations are the useful part.";
    crew->topics[crew->topic_count++] = {5, "Heard anything beyond the chart?",
                                         text};
  }
  return crew;
}

auto NativeFreedomFlightSession::station_stellar_lead_view() const
    -> std::expected<NativeStationStellarLeadView, std::string> {
  NativeStationStellarLeadView view;
  if (!knowledge_ ||
      knowledge_->recipe.version != kFreedomUnchartedKnowledgeVersion)
    return view;
  const auto status =
      freedom_station_stellar_lead_status(*knowledge_, document_.flight.tick);
  if (!status) return std::unexpected{"Retained stellar report is invalid"};
  view.selected = true;
  view.kept = knowledge_->station_stellar_lead.has_value();
  view.status = "unrecorded";
  view.note = "No stellar report retained. Ask the Origin bay attendant "
              "about an available report.";
  if (view.kept) {
    view.received_tick = knowledge_->station_stellar_lead->received_tick;
    view.source = "Origin bay attendant";
    switch (*status) {
      case FreedomStationStellarLeadStatus::unverified:
        view.status = "unverified";
        view.note = "Unverified stellar report beyond the chart. Search "
                    "nearby stars with your own ship sensors. The receipt "
                    "time is not a freshness or navigation guarantee.";
        break;
      case FreedomStationStellarLeadStatus::sensor_fix_acquired:
        view.status = "sensor_fix_acquired";
        view.note = "Sensor fix acquired for the reported system. The "
                    "navigation chart uses your actual sensor evidence. "
                    "No contents or safe landing are promised.";
        break;
      case FreedomStationStellarLeadStatus::visited:
        view.status = "visited";
        view.note = "Your retained travel evidence records a visit to the "
                    "reported system. The original report remains a "
                    "personal note; it offers no reward or claim.";
        break;
      case FreedomStationStellarLeadStatus::absent:
        return std::unexpected{"Retained stellar report has no status"};
    }
    return view;
  }
  if (!actor_ || !docking_ || !docking_->attached || recovery_pending() ||
      (boarding_ && boarding_->phase != FreedomBoardingPhase::station))
    return view;
  // Query the unchanged geometric eligibility provider, not station_crew(),
  // whose contextual topics consume this view. Reading never receives a note.
  const auto crew = origin_station_crew(
      generate_origin_station(document_.origin.recipe.universe_seed), *actor_,
      document_.flight.tick);
  if (!crew || !crew->available) return view;
  const auto offer = offer_freedom_station_stellar_lead(
      *knowledge_, document_.origin.state.craft, document_.flight.tick);
  if (!offer) {
    view.note = "No unverified stellar report is available.";
    return view;
  }
  view.can_keep = true;
  view.offer_tick = document_.flight.tick;
  view.source = "Origin bay attendant";
  view.note = "An unverified stellar report beyond the chart. Keep it as a "
              "personal note, then use your own sensors to find a location. "
              "The report has no expiry or freshness promise.";
  return view;
}

auto NativeFreedomFlightSession::receive_station_stellar_lead(
    SimulationTick offered_tick) -> std::expected<void, std::string> {
  const auto view = station_stellar_lead_view();
  if (!view) return std::unexpected{view.error()};
  if (!view->can_keep || view->offer_tick != offered_tick)
    return std::unexpected{
        "Station report is unavailable or the offer is stale"};
  const auto offer = offer_freedom_station_stellar_lead(
      *knowledge_, document_.origin.state.craft, document_.flight.tick);
  if (!offer) return std::unexpected{"Station report offer was refused"};
  auto knowledge = receive_freedom_station_stellar_lead(*knowledge_, *offer,
                                                        document_.flight.tick);
  if (!knowledge) return std::unexpected{"Station report receipt was refused"};
  auto candidate = *this;
  candidate.knowledge_ = std::move(*knowledge);
  const auto saved = candidate.save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (const auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return {};
}
} // namespace apsis_drift
