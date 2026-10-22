#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/origin_service_sortie_save.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <source_location>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace apsis_drift;
unsigned checks{};
auto check(bool v, const char* message) -> void {
  ++checks;
  if (!v) throw std::runtime_error(message);
}
template <class T, class E>
auto need(std::expected<T, E> value,
          std::source_location at = std::source_location::current()) -> T {
  if (!value) {
    std::cerr << at.file_name() << ':' << at.line() << " refused: ";
    if constexpr (std::is_same_v<E, std::string>)
      std::cerr << value.error();
    else if constexpr (requires { value.error().detail; })
      std::cerr << value.error().detail;
    else if constexpr (std::is_enum_v<E>)
      std::cerr << static_cast<unsigned>(value.error());
    std::cerr << '\n';
    throw std::runtime_error("Sortie fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}

std::size_t max_bytes{};
std::array<bool, 12> seen{};
unsigned serde_cases{};
auto roundtrip(const PhysicalLocalSystem& owner,
               const PhysicalMoonDescriptor& moon,
               const OriginServiceSortie& state) -> OriginServiceSortie {
  const auto text = need(encode_origin_service_sortie_json(owner, moon, state));
  max_bytes = std::max(max_bytes, text.size());
  const auto decoded =
      need(decode_origin_service_sortie_json(owner, moon, text));
  check(
      decoded == state,
      "Complete typed sortie including rigid poses/bill/anchor survives codec");
  check(need(encode_origin_service_sortie_json(owner, moon, decoded)) == text,
        "Canonical sortie bytes are stable after Continue");
  ++serde_cases;
  seen[static_cast<unsigned>(state.phase)] = true;
  auto base = nlohmann::ordered_json::parse(text);
  auto rejects = [&](nlohmann::ordered_json value, const char* why) {
    check(!decode_origin_service_sortie_json(owner, moon, value.dump()), why);
  };
  auto bad = base;
  bad["version"] = 2U;
  rejects(bad, "Unknown sortie version refuses");
  bad = base;
  bad["phase"] = 4294967296ULL;
  rejects(bad, "Phase narrowing overflow refuses");
  bad = base;
  bad["phase"] = -1;
  rejects(bad, "Signed phase refuses");
  bad = base;
  bad["phase"] = 1.5;
  rejects(bad, "Floating phase refuses");
  bad = base;
  bad["phase_start_tick"] = std::numeric_limits<std::uint64_t>::max();
  rejects(bad, "Invalid event clock refuses");
  bad = base;
  bad["deadline_tick"] = state.deadline_tick + 1;
  rejects(bad, "Altered phase deadline refuses");
  bad = base;
  bad["extra"] = true;
  rejects(bad, "Unknown closed sortie field refuses");
  bad = base;
  bad["beacon"]["anchor"]["extra"] = true;
  rejects(bad, "Unknown nested anchor field refuses");
  bad = base;
  bad["beacon"]["lineage"] = {{"version", 1U}, {"generation", 0U}};
  rejects(bad, "Invalid selected lineage refuses");
  bad = base;
  bad["beacon"]["target"] = state.beacon.target.value ^ 1ULL;
  rejects(bad, "Foreign beacon craft refuses");
  bad = base;
  bad["beacon"]["anchor"]["rotation_version"] = 2U;
  rejects(bad, "Unknown anchor rotation refuses");
  bad = base;
  bad["service"]["owner"]["craft"] = state.service.descriptor.id.value ^ 1ULL;
  rejects(bad, "Foreign responder identity refuses");
  bad = base;
  bad["service"]["resources"]["tick"] = state.service.body.tick + 1;
  rejects(bad, "Actor/resource clock mismatch refuses");
  if (state.pending) {
    bad = base;
    bad["pending"]["source_checksum"] = state.pending->source_checksum ^ 1ULL;
    rejects(bad, "Frozen source checksum tampering refuses");
    bad = base;
    bad["pending"]["arrival_tick"] = state.pending->arrival_tick + 1;
    rejects(bad, "Frozen arrival clock tampering refuses");
    bad = base;
    bad["commitment_resources"]["flight_quanta"] =
        state.commitment_resources->flight_quanta - 1;
    rejects(bad, "Frozen pre-debit fuel bill tampering refuses");
    bad = base;
    bad["commitment_resources"]["jump_charges"] = 4294967296ULL;
    rejects(bad, "Pre-debit charge narrowing overflow refuses");
    bad = base;
    bad["commitment_resources"]["craft"] =
        state.service.descriptor.id.value ^ 1ULL;
    rejects(bad, "Pre-debit bill owner tampering refuses");
    bad = base;
    bad["pending"]["extra"] = true;
    rejects(bad, "Unknown pending field refuses");
    bad = base;
    bad["pending"]["beacon"] = base["beacon"];
    bad["pending"]["beacon"]["target"] = state.beacon.target.value ^ 1ULL;
    rejects(bad,
            "Pending arrival cannot substitute a different immutable beacon");
    bad = base;
    bad["pending"] = nullptr;
    rejects(bad, "Transit cannot discard its frozen receipt");
  }
  return decoded;
}
auto closed_boundaries(const PhysicalLocalSystem& owner,
                       const PhysicalMoonDescriptor& moon,
                       const OriginServiceSortie& state) -> void {
  const auto bytes =
      need(encode_origin_service_sortie_json(owner, moon, state));
  auto padded = bytes;
  padded.resize(kMaximumOriginServiceSortieStateBytes, ' ');
  check(decode_origin_service_sortie_json(owner, moon, padded).has_value(),
        "Exact fixed byte boundary admits valid document");
  padded.push_back(' ');
  const auto excess = decode_origin_service_sortie_json(owner, moon, padded);
  check(!excess &&
            excess.error().code == SaveSchemaErrorCode::document_too_large,
        "One byte over fixed bound refuses");
  check(!decode_origin_service_sortie_json(
            owner, moon, std::string(33, '[') + std::string(33, ']')),
        "Nesting bound refuses before unbounded parser work");
  auto duplicate = bytes;
  duplicate.insert(duplicate.find('{') + 1, "\"version\":1,");
  const auto repeated =
      decode_origin_service_sortie_json(owner, moon, duplicate);
  check(!repeated &&
            repeated.error().code == SaveSchemaErrorCode::duplicate_key,
        "Duplicate root field refuses");
  auto nested = bytes;
  const auto key = nested.find("\"target\"");
  nested.insert(key, "\"target\":0,");
  const auto repeated_nested =
      decode_origin_service_sortie_json(owner, moon, nested);
  check(!repeated_nested &&
            repeated_nested.error().code == SaveSchemaErrorCode::duplicate_key,
        "Duplicate nested beacon field refuses");
  const auto foreign = need(generate_physical_origin_system({43}, 2));
  check(!decode_origin_service_sortie_json(foreign, moon, bytes),
        "Foreign admitted world refuses");
  auto forged = moon;
  ++forged.radius_metres;
  check(!decode_origin_service_sortie_json(owner, forged, bytes),
        "Forged Moon owner refuses before pose decoding");
  check(!decode_origin_service_sortie_json(owner, moon, "{}x"),
        "Trailing malformed input refuses");
}

} // namespace
int main(int argc, char** argv) {
  try {
    if (argc != 3)
      throw std::runtime_error(
          "usage: service-sortie actual-landed-save report.json");
    auto actual =
        need(NativeFreedomFlightSession::open(need(native_continue(argv[1]))));
    check(actual.moon() && actual.surface() && actual.surface()->moon_landed &&
              actual.resources(),
          "Actual supported Moon source required");
    const auto original =
        need(encode_native_save_document_json(need(actual.save_document())));
    const auto moon = *actual.moon();
    const auto system = moon.catalog.owner;
    auto player = actual.document();
    auto receiver = *actual.resources();
    // Labelled stranded-resource fixture only; no claim this player used the
    // missing fuel on a flown route. Original actual landing/identity retained.
    receiver.flight_quanta = 0;
    const auto anchor = *actual.surface()->moon_landed;
    const OriginServiceBeacon beacon{1, player.origin.state.craft,
                                     player.origin.lineage, anchor};
    auto service =
        need(initialize_origin_service_craft(system, player.flight.tick));
    OriginServiceSortieRecipient recipient{player, anchor, receiver};
    const auto started = need(
        begin_origin_service_sortie(system, moon, service, beacon, recipient));
    need(validate_origin_service_sortie(system, moon, started));
    closed_boundaries(system, moon, started);
    const auto canceled =
        roundtrip(system, moon,
                  need(cancel_origin_service_sortie(system, moon, started)));
    check(canceled.phase == OriginServiceSortiePhase::canceled &&
              canceled.service == started.service,
          "Cancel before departure preserves attachment, fuel, charges and "
          "cargo exactly");
    const auto canceled_next = need(advance_origin_service_sortie(
        system, moon, canceled, service.body.tick + 1));
    check(canceled_next.sortie.service.attached && !canceled_next.receiver &&
              canceled_next.transferred_quanta == 0,
          "Canceled service advances only its actual attached clock and "
          "credits nothing");
    check(!advance_origin_service_sortie(system, moon, started,
                                         service.body.tick + 2, &recipient),
          "Skipped outer world tick refuses atomically");
    auto impossible_return = started;
    impossible_return.return_requested = true;
    check(!validate_origin_service_sortie(system, moon, impossible_return) &&
              !advance_origin_service_sortie(system, moon, impossible_return,
                                             service.body.tick + 1, &recipient),
          "Impossible return-only requested phase refuses before movement or "
          "fuel");
    auto invalid = started;
    invalid.deadline_tick++;
    check(!validate_origin_service_sortie(system, moon, invalid),
          "Forged phase deadline refuses");
    auto low = service;
    low.resources.jump_charges = 1;
    check(!begin_origin_service_sortie(system, moon, low, beacon, recipient),
          "Cannot dispatch without both finite route charges");
    low = service;
    low.resources.flight_quanta = 2 * kFreedomFlightReserveQuanta - 1;
    check(!begin_origin_service_sortie(system, moon, low, beacon, recipient),
          "Cannot dispatch below disclosed roundtrip reserve");
    auto sortie = roundtrip(system, moon, started);
    const auto initial_fuel = service.resources.flight_quanta;
    const auto initial_cargo = service.resources.cargo_quanta;
    const auto initial_charges = service.resources.jump_charges;
    std::uint64_t propulsion{}, delivery{};
    unsigned ticks{}, transfer_ticks{}, transit_ticks{};
    unsigned outward_transits{}, return_transits{};
    bool partial_checked{}, outward_cancel_checked{}, leave_checked{};
    for (;
         ticks < 180000 && sortie.phase != OriginServiceSortiePhase::completed;
         ++ticks) {
      const auto next_tick = sortie.service.body.tick + 1;
      player.flight = need(resolve_moon_landed_craft(moon, anchor, next_tick));
      player.origin.state.tick = next_tick;
      receiver =
          need(advance_freedom_resource_tick(receiver, receiver.tick, 0));
      const auto phase_before = sortie.phase;
      if (phase_before == OriginServiceSortiePhase::outward_transit &&
          !outward_cancel_checked) {
        const auto aborted =
            need(cancel_origin_service_sortie(system, moon, sortie));
        check(aborted.pending == sortie.pending &&
                  aborted.service == sortie.service && aborted.return_requested,
              "Cancellation cannot erase a genuine outward commitment or "
              "refund its charge");
        const auto step = need(advance_origin_service_sortie(
            system, moon, aborted, next_tick, nullptr));
        check(!step.receiver && step.sortie.pending == sortie.pending &&
                  step.sortie.service.resources.jump_charges ==
                      sortie.service.resources.jump_charges,
              "Aborted transit still ages its authentic pending arrival "
              "without credit");
        auto forged = sortie;
        forged.pending->source_checksum ^= 1;
        check(!validate_origin_service_sortie(system, moon, forged),
              "Frozen transit receipt rederives and rejects source tampering");
        outward_cancel_checked = true;
      }
      if (phase_before == OriginServiceSortiePhase::delivering &&
          !leave_checked) {
        auto forbidden = sortie;
        forbidden.return_requested = true;
        check(!validate_origin_service_sortie(system, moon, forbidden) &&
                  !advance_origin_service_sortie(system, moon, forbidden,
                                                 next_tick, &recipient),
              "Return-only delivery cannot produce another fuel credit");
        const auto absent = need(advance_origin_service_sortie(
            system, moon, sortie, next_tick, nullptr));
        check(absent.sortie.return_requested &&
                  absent.sortie.phase ==
                      OriginServiceSortiePhase::withdrawing &&
                  !absent.receiver && absent.transferred_quanta == 0,
              "Target leave immediately withdraws without another fuel credit");
        leave_checked = true;
      }
      const auto step = need(advance_origin_service_sortie(
          system, moon, sortie, next_tick, &recipient));
      check(step.sortie.service.body.tick == next_tick &&
                step.sortie.service.resources.tick == next_tick,
            "Every phase consumes exactly one shared outer authoritative tick");
      propulsion += step.propulsion_quanta;
      delivery += step.transferred_quanta;
      if (step.receiver) {
        auto expected = receiver;
        expected.flight_quanta += step.transferred_quanta;
        check(*step.receiver == expected,
              "Paired service credit changes no player "
              "metadata/drive/charges/clock");
        receiver = *step.receiver;
        ++transfer_ticks;
      }
      if (phase_before == OriginServiceSortiePhase::outward_transit ||
          phase_before == OriginServiceSortiePhase::return_transit)
        ++transit_ticks;
      if (step.sortie.phase == OriginServiceSortiePhase::outward_transit &&
          phase_before != step.sortie.phase)
        ++outward_transits;
      if (step.sortie.phase == OriginServiceSortiePhase::return_transit &&
          phase_before != step.sortie.phase)
        ++return_transits;
      sortie = step.sortie;
      if (!seen[static_cast<unsigned>(sortie.phase)])
        sortie = roundtrip(system, moon, sortie);
      if (!partial_checked && sortie.service.delivery) {
        need(validate_origin_service_sortie(system, moon, sortie));
        sortie = roundtrip(system, moon, sortie);
        auto bad = sortie;
        bad.service.delivery->target_anchor_checksum ^= 1;
        check(!validate_origin_service_sortie(system, moon, bad),
              "Partial delivery remains bound to original stable beacon");
        partial_checked = true;
      }
      if (phase_before != sortie.phase)
        std::cerr << "phase " << unsigned(sortie.phase) << " tick " << next_tick
                  << '\n';
    }
    check(ticks < 180000 &&
              sortie.phase == OriginServiceSortiePhase::completed &&
              sortie.service.attached,
          "One real finite sortie returns through the unchanged mechanical "
          "capture gate");
    check(outward_transits == 1 && return_transits == 1 &&
              transit_ticks == 2 * kJumpTransitTicks,
          "Exactly two genuine charged transits complete at their frozen "
          "authoritative clocks");
    check(transfer_ticks == 1200 && delivery == initial_cargo &&
              receiver.flight_quanta == initial_cargo &&
              sortie.service.resources.cargo_quanta == 0,
          "Actual1200hover ticks conserve complete quarter-tank delivery");
    check(initial_fuel - sortie.service.resources.flight_quanta == propulsion &&
              initial_charges - sortie.service.resources.jump_charges == 2,
          "Actual powered ticks and both route charges remain billed");
    // Labelled long-lived mechanical idle fixture, not a second flown route:
    // use the same genuine attachment at a later shared world clock.
    auto aged = sortie;
    const auto event_tick = sortie.phase_start_tick;
    const auto station =
        generate_origin_station(service.descriptor.universe_seed);
    const auto geometry = need(origin_station_geometry(station));
    const auto future =
        sortie.service.body.tick + 2 * kOriginServiceSortiePhaseTicks;
    aged.service.body = need(
        release_origin_port(system, station, geometry,
                            {geometry.version, service.descriptor.home_port,
                             service.descriptor.frame, future}));
    aged.service.resources.tick = future;
    need(validate_origin_service_sortie(system, moon, aged));
    aged = roundtrip(system, moon, aged);
    const auto aged_step =
        need(advance_origin_service_sortie(system, moon, aged, future + 1));
    check(aged_step.sortie.phase_start_tick == event_tick &&
              aged_step.sortie.deadline_tick == 0 && !aged_step.receiver &&
              aged_step.sortie.service.resources.flight_quanta ==
                  sortie.service.resources.flight_quanta,
          "Long-lived completed station attachment preserves actual event "
          "clock and no deadline");
    check(need(encode_native_save_document_json(
              need(actual.save_document()))) == original,
          "Original actual player source remains byte-exact");
    for (const auto phase : {OriginServiceSortiePhase::requested,
                             OriginServiceSortiePhase::outward_transit,
                             OriginServiceSortiePhase::approaching,
                             OriginServiceSortiePhase::delivering,
                             OriginServiceSortiePhase::return_transit,
                             OriginServiceSortiePhase::completed,
                             OriginServiceSortiePhase::canceled})
      check(seen[static_cast<unsigned>(phase)],
            "Every required persisted sortie milestone was actually exercised");
    std::cout << serde_cases << " actual milestone codec cases, maximum "
              << max_bytes << " bytes\n";
    std::ofstream out(argv[2]);
    out << "{\n\"pass\":true,\n\"checks\":" << checks
        << ",\n\"ticks\":" << ticks
        << ",\n\"delivery_ticks\":" << transfer_ticks
        << ",\n\"delivered_quanta\":" << delivery
        << ",\n\"propulsion_quanta\":" << propulsion
        << ",\n\"scope\":\"Sortie phase/codec contract with genuine actual "
           "landed source and labelled zero-fuel metadata; player counterpart "
           "resolved from real landed constraint each shared oracle tick, not "
           "integrated NativeSession rescue\"\n}\n";
    std::cout << checks << " private sortie checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
