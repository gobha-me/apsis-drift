#include "apsis_drift/native_flight_session.hpp"

#include <array>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
using namespace apsis_drift;
unsigned checks{};
auto check(bool good, const char* why) -> void {
  ++checks;
  if (!good) throw std::runtime_error(why);
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Crew fixture refused");
  return *value;
}
auto bytes(const NativeFreedomFlightSession& live) -> std::string {
  return need(encode_native_save_document_json(need(live.save_document())));
}
auto run() -> void {
  OriginWalkerState actor;
  actor.heading_radians = -std::numbers::pi / 2;
  for (const auto seed : {0ULL, 42ULL, 123456ULL,
                          std::numeric_limits<unsigned long long>::max()}) {
    const auto station = generate_origin_station(Seed{seed});
    const auto crew = need(origin_station_crew(station, actor, 0));
    check(crew.available && crew.station == station.id && crew.ordinal == 1,
          "Crew is bound to the actual station and reachable from its hub");
    check(crew.foot_position_metres.x - .32 > kOriginWalkMaximumXMetres,
          "Crew body stays beyond all permitted player standing reservations");
    for (std::size_t i = 0; i < crew.topics.size(); ++i)
      check(crew.topics[i].id == i + 1 && !crew.topics[i].title.empty() &&
                !crew.topics[i].text.empty(),
            "Small authored topic set is stable and complete");
    const auto later = need(origin_station_crew(
        station, actor, std::numeric_limits<SimulationTick>::max()));
    check(
        later.available && !later.remark.empty(),
        "Maximum clock derives chatter without overflow or eligibility change");
    check(need(origin_station_crew(station, actor, 3600)).remark ==
              need(origin_station_crew(station, actor, 3600)).remark,
          "Same identity and clock reproduce the same passing remark");
    const OriginStationDescriptor foreign{
        station.universe_seed, station.home_system_seed, station.station_seed,
        OriginStationId{station.id.value ^ 1U}, station.orbit};
    check(!origin_station_crew(foreign, actor, 0),
          "Forged station identity refuses");
  }
  const auto station = generate_origin_station(Seed{42});
  auto changed = actor;
  changed.heading_radians = 0;
  check(!need(origin_station_crew(station, changed, 0)).available,
        "Looking away does not offer conversation");
  changed = actor;
  changed.velocity_metres_per_second.x = .1;
  check(!need(origin_station_crew(station, changed, 0)).available,
        "Moving actor must stop before conversation");
  changed = actor;
  changed.foot_position_metres.x = -1;
  check(need(origin_station_crew(station, changed, 0)).available,
        "Qualified hub prism admits its exact boundary");
  changed.foot_position_metres.x = -1.00001;
  check(!need(origin_station_crew(station, changed, 0)).available,
        "Beyond qualified hub prism refuses before through-wall interaction");
  for (const auto invalid : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()}) {
    for (unsigned field = 0; field < 7; ++field) {
      changed = actor;
      const std::array<double*, 7> fields{&changed.foot_position_metres.x,
                                          &changed.foot_position_metres.y,
                                          &changed.foot_position_metres.z,
                                          &changed.velocity_metres_per_second.x,
                                          &changed.velocity_metres_per_second.y,
                                          &changed.velocity_metres_per_second.z,
                                          &changed.heading_radians};
      *fields[field] = invalid;
      check(!origin_station_crew(station, changed, 0),
            "Every nonfinite actor component refuses");
    }
  }
  auto live =
      need(NativeFreedomFlightSession::open(need(native_new_game(Seed{42}))));
  need(live.advance_walk({0, 0, -std::numbers::pi / 2}));
  const auto before = bytes(live);
  for (unsigned i = 0; i < 8; ++i)
    check(need(live.station_crew()).available && bytes(live) == before,
          "Actual conversation query preserves complete Save, clock and "
          "resources");
  auto continued = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(need(live.save_document())))));
  check(bytes(continued) == before && need(continued.station_crew()).available,
        "Continue preserves the real actor and crew eligibility without "
        "migration");
}
} // namespace
auto main() -> int {
  try {
    run();
    std::cout << "ORIGIN_CREW_CHECKS checks=" << checks << " failures=0\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
