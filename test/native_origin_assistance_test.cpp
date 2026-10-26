// Actual shared-session rescue and complete format59 Save/Continue contract.
// The stranded-resource and exhaustion probes are explicitly labelled fixtures.
#include "apsis_drift/freedom_assistance_save.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/native_startup.hpp"
#include "apsis_drift/save_file.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <source_location>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
using Phase = OriginServiceSortiePhase;
unsigned checks{};
auto check(bool value, const char* message) -> void {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
template <class T, class E>
auto need(std::expected<T, E> value,
          std::source_location at = std::source_location::current()) -> T {
  if (!value) {
    std::cerr << at.file_name() << ':' << at.line() << ": ";
    if constexpr (std::is_same_v<E, std::string>)
      std::cerr << value.error();
    else if constexpr (requires { value.error().detail; })
      std::cerr << value.error().detail;
    else if constexpr (std::is_enum_v<E>)
      std::cerr << static_cast<unsigned>(value.error());
    std::cerr << '\n';
    throw std::runtime_error("Actual assistance fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto bytes(const NativeFreedomFlightSession& s) -> std::string {
  return need(encode_native_save_document_json(need(s.save_document())));
}
auto state(const NativeFreedomFlightSession& s)
    -> const OriginServiceCraftState& {
  check(s.origin_assistance().has_value(),
        "Selected service actor remains present");
  return *std::visit(
      [](const auto& actor) -> const OriginServiceCraftState* {
        using T = std::decay_t<decltype(actor)>;
        if constexpr (std::is_same_v<T, OriginServiceCraftState>)
          return &actor;
        else if constexpr (std::is_same_v<T, OriginServiceSortie>)
          return &actor.service;
        else
          return &actor.last.service;
      },
      *s.origin_assistance());
}
auto sortie(const NativeFreedomFlightSession& s) -> const OriginServiceSortie& {
  check(s.origin_assistance() &&
            std::holds_alternative<OriginServiceSortie>(*s.origin_assistance()),
        "Dispatched service retains its one genuine sortie");
  return std::get<OriginServiceSortie>(*s.origin_assistance());
}
auto paired_clock(const NativeFreedomFlightSession& s) -> void {
  const auto& npc = state(s);
  check(s.resources() && s.document().flight.tick == s.resources()->tick &&
            npc.resources.tick == npc.body.tick,
        "Player body/resources and saved service source each retain their one "
        "clock");
  if (const auto* history =
          std::get_if<OriginServiceUnavailable>(&*s.origin_assistance())) {
    check(npc.body.tick != std::numeric_limits<SimulationTick>::max() &&
              history->failed_tick == npc.body.tick + 1 &&
              history->failed_tick <= s.document().flight.tick,
          "Unavailable contact is exact immutable history before the first "
          "failed tick, never a current NPC pose");
  } else
    check(s.document().flight.tick == npc.body.tick,
          "One committed world tick owns both LIVE player/service actors and "
          "resources");
}
auto continued(const NativeFreedomFlightSession& s,
               const std::filesystem::path& output, const std::string& label)
    -> NativeFreedomFlightSession {
  const auto before = bytes(s);
  const auto path = output / (label + ".json");
  need(s.save_as(path));
  std::ifstream file(path, std::ios::binary);
  const std::string actual((std::istreambuf_iterator<char>(file)), {});
  check(actual == before,
        "Actual SaveAs bytes equal complete current format59 projection");
  auto restored =
      need(NativeFreedomFlightSession::open(need(native_continue(path))));
  check(
      bytes(restored) == before,
      "Actual file Continue preserves both actors and complete player voyage");
  paired_clock(restored);
  check(bytes(s) == before,
        "SaveAs/Continue never advances or mutates source session");
  return restored;
}
auto selected_inner(const NativeSaveDocument& value)
    -> FreedomAssistanceVoyage {
  return std::visit(
      [](const auto& leaf) -> FreedomAssistanceVoyage {
        using T = std::decay_t<decltype(leaf)>;
        if constexpr (std::is_constructible_v<FreedomAssistanceVoyage, T>)
          return leaf;
        else
          throw std::runtime_error(
              "Expected an unchanged complete bounded historical voyage");
      },
      value);
}
template <class T> auto knowledge(T& value) -> FreedomKnowledgeSaveDocument& {
  if constexpr (std::is_same_v<T, FreedomKnowledgeSaveDocument>)
    return value;
  else if constexpr (std::is_same_v<T, FreedomTravelSaveDocument>)
    return value.voyage;
  else if constexpr (std::is_same_v<T, FreedomSurfaceWalkSaveDocument>)
    return knowledge(value.voyage);
  else
    return std::visit(
        [](auto& inner) -> FreedomKnowledgeSaveDocument& {
          return knowledge(inner);
        },
        value.voyage);
}
auto player_knowledge(FreedomAssistanceVoyage& voyage)
    -> FreedomKnowledgeSaveDocument& {
  return std::visit(
      [](auto& inner) -> FreedomKnowledgeSaveDocument& {
        return knowledge(inner);
      },
      voyage);
}
auto retains(const FreedomKnowledge& current, const FreedomKnowledge& prior)
    -> bool {
  if (current.recipe != prior.recipe ||
      current.surface_marker != prior.surface_marker ||
      current.surface_landmarks != prior.surface_landmarks ||
      current.moon_surface_landmarks != prior.moon_surface_landmarks ||
      current.orbital_surveys != prior.orbital_surveys ||
      current.field_samples != prior.field_samples)
    return false;
  for (const auto& entry : prior.entries) {
    const auto found =
        std::ranges::find_if(current.entries, [&](const auto& e) {
          return e.subject == entry.subject && e.fact == entry.fact;
        });
    if (found == current.entries.end() ||
        found->transitions.size() < entry.transitions.size() ||
        !std::equal(entry.transitions.begin(), entry.transitions.end(),
                    found->transitions.begin()))
      return false;
  }
  return true;
}
auto pause_queries(NativeFreedomFlightSession& s) -> void {
  const auto frozen = bytes(s);
  const auto clock = s.document().flight.tick;
  for (unsigned i = 0; i < 3; ++i) {
    static_cast<void>(s.origin_assistance_available());
    static_cast<void>(s.origin_assistance());
    static_cast<void>(s.origin_assistance_world());
    need(s.observe());
    check(bytes(s) == frozen && s.document().flight.tick == clock,
          "Queries/Save without advance freeze both actors and budgets");
  }
  NativeFlightControls invalid;
  invalid.positive_translation.x = std::numeric_limits<double>::quiet_NaN();
  check(!s.advance(invalid) && bytes(s) == frozen,
        "Nonfinite controls refuse complete paired transaction atomically");
  check(!s.advance({}, SimulationSeconds{0}) && bytes(s) == frozen,
        "Invalid timestep refuses complete paired transaction atomically");
}
auto explicit_selection(const std::filesystem::path& output) -> void {
  for (std::uint32_t chart = 0; chart < 3; ++chart)
    for (std::uint32_t drive = 0; drive < 3; ++drive) {
      const auto old =
          need(drive == 2   ? native_jump_strain_new_game({42}, chart)
               : drive == 1 ? native_jump_recovery_new_game({42}, chart)
               : chart == 2 ? native_planetary_network_new_game({42})
               : chart == 1 ? native_network_new_game({42})
                            : native_new_game({42}));
      auto legacy = need(NativeFreedomFlightSession::open(old));
      const auto original = bytes(legacy);
      check(!legacy.origin_assistance() &&
                !legacy.origin_assistance_available() &&
                !legacy.request_origin_assistance() &&
                bytes(legacy) == original,
            "Existing NewGame/Continue never silently acquires a responder");
      auto selected = need(NativeFreedomFlightSession::open(
          need(native_origin_assistance_new_game({42}, chart, drive))));
      const auto wrapped = std::get<FreedomAssistanceSaveDocument>(
          need(selected.save_document()));
      check(std::get<FreedomKnowledgeSaveDocument>(wrapped.voyage) ==
                std::get<FreedomKnowledgeSaveDocument>(old.document),
            "Explicit responder opt-in preserves exact old chart/drive/player "
            "state");
      paired_clock(selected);
      check(state(selected).attached &&
                state(selected).resources.cargo_quanta ==
                    kOriginServiceCargoCapacityQuanta,
            "Actual separate original service actor starts at its constraint "
            "with full cargo");
      const auto before = bytes(selected);
      const auto port = selected.select_port(2);
      check(!port && port.error().find("reserved") != std::string::npos &&
                bytes(selected) == before,
            "D2 service reservation is authoritative and save-neutral");
      check(!selected.request_origin_assistance() && bytes(selected) == before,
            "Station actor cannot request a Moon rescue from the reader/menu");
      // One ordinary station walking tick proves the separate public tick
      // owner.
      need(selected.advance_walk({}));
      check(
          selected.document().flight.tick == 1 &&
              state(selected).body.tick == 1 && state(selected).attached,
          "Station walking commits exactly one paired attached responder tick");
      paired_clock(selected);
      if (chart == 2 && drive == 2)
        selected = continued(selected, output, "selected-station");
      check(bytes(legacy) == original, "Opt-in and selected actor ticking "
                                       "leave independent legacy source exact");
    }
  check(!native_origin_assistance_new_game({42}, 5, 0) &&
            !native_origin_assistance_new_game({42}, 0, 3),
        "Unknown chart/drive selections refuse");
}
auto full_route(const std::filesystem::path& actual_path,
                const std::filesystem::path& output) -> Json {
  auto actual = need(
      NativeFreedomFlightSession::open(need(native_continue(actual_path))));
  check(actual.moon() && actual.surface() && actual.surface()->moon_landed &&
            actual.resources() && actual.knowledge() && actual.boarding() &&
            actual.boarding()->phase == FreedomBoardingPhase::seated,
        "Source is the genuinely reached supported occupied home-Moon landing");
  const auto original = bytes(actual);
  check(!actual.origin_assistance(),
        "Historical actual landing remains assistance-absent");
  auto inner = selected_inner(need(actual.save_document()));
  auto& resource = player_knowledge(inner).voyage.resources;
  // Explicit selected-wrapper/stranded-resource fixture only. The old physical
  // landing, craft, pilot, current model, clocks, receipts and knowledge remain
  // exact. We do NOT claim a flown fuel-exhaustion history or old-save
  // adoption.
  resource.flight_quanta = 0;
  const auto world = need(make_origin_assistance_world_context(
      actual.document().origin.recipe.universe_seed));
  const auto npc = need(initialize_origin_service_craft(
      world.origin(), actual.document().flight.tick));
  FreedomAssistanceSaveDocument selected{1, std::move(inner), npc};
  auto s = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(NativeSaveDocument{selected}))));
  check(s.surface()->moon_landed == actual.surface()->moon_landed &&
            s.document() == actual.document() &&
            *s.knowledge() == *actual.knowledge() &&
            s.resources()->jump_charges == actual.resources()->jump_charges &&
            s.resources()->drive == actual.resources()->drive,
        "Labelled fuel fixture preserves every other actual source field");
  s = continued(s, output, "selected-moon");
  const auto anchor = *s.surface()->moon_landed;
  const auto craft = s.document().origin.state.craft;
  const auto lineage = s.document().origin.lineage;
  const auto model = s.document().model;
  const auto notes = *s.knowledge();
  const auto driver = s.resources()->drive;
  const auto charges = s.resources()->jump_charges;
  const auto travel = s.travel();
  const auto initial = state(s).resources;
  const auto start_tick = s.document().flight.tick;
  pause_queries(s);
  need(s.origin_assistance_available());
  const auto before_request = bytes(s);
  auto canceled = s;
  need(canceled.request_origin_assistance());
  need(canceled.cancel_origin_assistance());
  check(bytes(canceled) == before_request,
        "Cancel before departure restores exact original actor with zero "
        "tick/debit/refill");
  canceled = continued(canceled, output, "canceled-before-departure");
  need(s.request_origin_assistance());
  check(s.document().flight.tick == start_tick &&
            *s.resources() == assistance_player_resources(selected.voyage) &&
            sortie(s).phase == Phase::requested,
        "Accepted request owns zero time and changes no player quantity");
  s = continued(s, output, "requested");
  pause_queries(s);
  bool outward_saved{}, return_saved{}, partial_saved{}, walking_checked{},
      left_checked{}, cancel_checked{}, archived_checked{};
  std::uint64_t delivered{}, propulsion{};
  unsigned delivery_ticks{}, outward_transit_ticks{}, return_transit_ticks{};
  unsigned ticks{};
  for (; ticks < 180000 && sortie(s).phase != Phase::completed; ++ticks) {
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Snapshot
    // survives the next committed tick.
    const auto prior = sortie(s);
    const auto player_tick = s.document().flight.tick;
    const auto receiver = s.resources()->flight_quanta;
    const auto before_npc = prior.service.resources;
    const auto step = need(s.advance({}));
    paired_clock(s);
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization) -- Snapshot
    // survives Continue replacing the session.
    const auto current = sortie(s);
    const auto after = current.service.resources;
    check(s.document().flight.tick == player_tick + 1 && !step.jump_committed &&
              !step.jump_arrived && step.fuel_debit_quanta == 0,
          "Waiting supported player advances one tick without player "
          "jump/propulsion side effects");
    check(
        after.flight_quanta <= before_npc.flight_quanta &&
            after.cargo_quanta <= before_npc.cargo_quanta,
        "Responder cannot regenerate propulsion or delivered cargo in flight");
    const auto credit = before_npc.cargo_quanta - after.cargo_quanta;
    check(s.resources()->flight_quanta == receiver + credit,
          "Same paired transaction debits actual NPC cargo and credits only "
          "player flight fuel");
    delivered += credit;
    propulsion += before_npc.flight_quanta - after.flight_quanta;
    if (credit) ++delivery_ticks;
    if (prior.phase == Phase::outward_transit) ++outward_transit_ticks;
    if (prior.phase == Phase::return_transit) ++return_transit_ticks;
    check(s.resources()->jump_charges == charges &&
              s.resources()->drive == driver && s.travel() == travel &&
              s.document().origin.state.craft == craft &&
              s.document().origin.lineage == lineage &&
              s.document().model == model &&
              s.surface()->moon_landed == anchor &&
              retains(*s.knowledge(), notes),
          "Rescue cannot reset player charges, strain selection, receipts, "
          "identity, anchor or retained knowledge/pouch");
    if (current.phase == Phase::outward_transit && !outward_saved) {
      check(after.jump_charges + 1 == initial.jump_charges && current.pending &&
                current.commitment_resources,
            "Actual outward commitment retains immutable arrival and one "
            "charge bill");
      s = continued(s, output, "outward-transit");
      outward_saved = true;
      auto abort = s;
      const auto pending = sortie(abort).pending;
      need(abort.cancel_origin_assistance());
      check(sortie(abort).pending == pending &&
                sortie(abort).return_requested &&
                state(abort).resources == after,
            "Cancel cannot erase or refund the already committed service "
            "transit");
      abort = continued(abort, output, "canceled-committed-transit");
      const auto tank = abort.resources()->flight_quanta;
      need(abort.advance({}));
      check(sortie(abort).pending == pending &&
                abort.resources()->flight_quanta == tank &&
                sortie(abort).return_requested,
            "Canceled committed transit still advances actual pending clock "
            "without fuel credit");
      cancel_checked = true;
    }
    if (delivery_ticks == 600 && !partial_saved) {
      check(delivered == kOriginServiceCargoCapacityQuanta / 2 &&
                after.cargo_quanta == kOriginServiceCargoCapacityQuanta / 2,
            "Six hundred finite hover transfers conserve exactly half the "
            "quarter-capacity cargo");
      s = continued(s, output, "partial-600");
      pause_queries(s);
      partial_saved = true;
      const auto before = bytes(s);
      auto walked = s;
      need(walked.begin_surface_walk());
      const auto fuel = walked.resources()->flight_quanta;
      const auto cargo = state(walked).resources.cargo_quanta;
      need(walked.advance_surface_walk({}));
      paired_clock(walked);
      check(walked.surface_walker() && !sortie(walked).return_requested &&
                walked.resources()->flight_quanta - fuel ==
                    cargo - state(walked).resources.cargo_quanta,
            "Supported outside actor uses one shared tick without aborting the "
            "same parked target or duplicating credit");
      walked = continued(walked, output, "supported-outside");
      walking_checked = true;
      auto left = s;
      need(left.release_surface());
      NativeFlightControls lift;
      lift.positive_translation.y = .5;
      const auto left_fuel = left.resources()->flight_quanta;
      const auto left_cargo = state(left).resources.cargo_quanta;
      const auto lift_step = need(left.advance(lift));
      check(!left.surface()->moon_landed && sortie(left).return_requested &&
                state(left).resources.cargo_quanta == left_cargo &&
                left.resources()->flight_quanta + lift_step.fuel_debit_quanta ==
                    left_fuel &&
                lift_step.fuel_debit_quanta > 0,
            "Real paid liftoff invalidates the supported target and closes "
            "service credit immediately");
      left = continued(left, output, "target-departed");
      left_checked = true;
      auto stop = s;
      need(stop.cancel_origin_assistance());
      const auto stop_fuel = stop.resources()->flight_quanta;
      const auto stop_cargo = state(stop).resources.cargo_quanta;
      need(stop.advance({}));
      check(sortie(stop).return_requested &&
                state(stop).resources.cargo_quanta == stop_cargo &&
                stop.resources()->flight_quanta == stop_fuel,
            "Cancellation after partial delivery retains delivered fuel and "
            "closes further credit");
      stop = continued(stop, output, "canceled-partial");
      check(bytes(s) == before, "Cancellation/walking/liftoff clone probes "
                                "cannot mutate the original complete rescue");
    }
    if (current.phase == Phase::return_transit && !return_saved) {
      check(after.jump_charges + 2 == initial.jump_charges && current.pending,
            "Actual home return owns second charged immutable future-port "
            "arrival");
      s = continued(s, output, "return-transit");
      return_saved = true;
    }
    if (current.phase == Phase::returning && !archived_checked) {
      const auto untouched = bytes(s);
      auto exhausted_save =
          std::get<FreedomAssistanceSaveDocument>(need(s.save_document()));
      auto& last = std::get<OriginServiceSortie>(exhausted_save.actor);
      // Labelled propulsion-exhaustion fixture at the ACTUAL reached home
      // returning milestone, not a claim this missing fuel was flown away.
      // The complete true body/clock/phase/cargo/delivery receipt is retained.
      last.service.resources.flight_quanta = 0;
      need(validate_freedom_assistance_document(exhausted_save));
      const auto original_npc = last;
      auto exhausted = need(NativeFreedomFlightSession::open(need(
          native_select_save_document(NativeSaveDocument{exhausted_save}))));
      const auto old_tick = exhausted.document().flight.tick;
      const auto player_resources = *exhausted.resources();
      const auto before_fault = bytes(exhausted);
      const auto attempted = need(exhausted.advance({}));
      check(exhausted.origin_assistance() &&
                std::holds_alternative<OriginServiceUnavailable>(
                    *exhausted.origin_assistance()),
            "A genuine finite return-route failure archives the contact "
            "instead of trapping player time");
      const auto history =
          std::get<OriginServiceUnavailable>(*exhausted.origin_assistance());
      check(history.last == original_npc &&
                history.failed_tick == old_tick + 1 &&
                history.reason ==
                    OriginServiceUnavailableReason::route_unavailable &&
                exhausted.document().flight.tick == old_tick + 1 &&
                attempted.fuel_debit_quanta == 0 &&
                exhausted.resources()->flight_quanta ==
                    player_resources.flight_quanta &&
                exhausted.resources()->jump_charges ==
                    player_resources.jump_charges &&
                exhausted.resources()->drive == player_resources.drive,
            "Failure retains exact last real NPC pose/pools/receipt and "
            "credits/refunds nothing while the player commits one tick");
      const auto& applied = exhausted.origin_assistance_propulsion();
      check(applied.positive_force_newtons == RigidVector3{} &&
                applied.negative_force_newtons == RigidVector3{} &&
                applied.positive_torque_newton_metres == RigidVector3{} &&
                applied.negative_torque_newton_metres == RigidVector3{} &&
                applied.world_linear_impulse_newton_seconds == RigidVector3{} &&
                applied.world_angular_impulse_newton_metre_seconds ==
                    RigidVector3{},
            "Historical unavailable contact exposes no live propulsion or "
            "impulse");
      exhausted = continued(exhausted, output, "unavailable-last-contact");
      const auto frozen = bytes(exhausted);
      const auto reserved = exhausted.select_port(2);
      check(!exhausted.request_origin_assistance() &&
                !exhausted.cancel_origin_assistance() && !reserved &&
                reserved.error().find("reserved") != std::string::npos &&
                bytes(exhausted) == frozen,
            "Archived contact cannot respond/refill/cancel and its D2 "
            "reservation remains save-neutral");
      need(exhausted.advance({}));
      paired_clock(exhausted);
      check(std::get<OriginServiceUnavailable>(
                *exhausted.origin_assistance()) == history &&
                exhausted.document().flight.tick == old_tick + 2 &&
                exhausted.resources()->flight_quanta ==
                    player_resources.flight_quanta &&
                retains(*exhausted.knowledge(), notes),
            "Later player ticks advance independently without aging historical "
            "contact, copying credits or erasing notes");
      exhausted = continued(exhausted, output, "unavailable-later-player");
      check(bytes(s) == untouched && before_fault != frozen,
            "Exhaustion clone cannot alter the original normally powered "
            "return sortie");
      archived_checked = true;
    }
    if (ticks == 0 || current.phase == Phase::outward_transit ||
        current.phase == Phase::return_transit) {
      const auto before = bytes(s);
      const auto denied = s.select_port(2);
      check(!denied && denied.error().find("reserved") != std::string::npos &&
                bytes(s) == before,
            "D2 remains reserved even when the real responder is away/in "
            "transit");
    }
  }
  check(sortie(s).phase == Phase::completed && state(s).attached &&
            ticks < 180000,
        "Finite actual thruster route returns and attaches through the "
        "authoritative existing capture gate");
  check(outward_saved && return_saved && partial_saved && walking_checked &&
            left_checked && cancel_checked && archived_checked,
        "Both charged transits, partial transfer, supported/abort and actual "
        "returning exhaustion boundaries were reached");
  check(delivered == kOriginServiceCargoCapacityQuanta &&
            delivery_ticks == 1200 &&
            s.resources()->flight_quanta == delivered &&
            state(s).resources.cargo_quanta == 0 &&
            state(s).resources.jump_charges + 2 == initial.jump_charges &&
            propulsion ==
                initial.flight_quanta - state(s).resources.flight_quanta &&
            propulsion > 0,
        "Full quarter delivery over1200ticks is conserved separately from "
        "actual paid responder propulsion and two charges");
  check(outward_transit_ticks == kJumpTransitTicks &&
            return_transit_ticks == kJumpTransitTicks,
        "Both responder transits consume exactly their authoritative240ticks");
  s = continued(s, output, "completed");
  pause_queries(s);
  const auto complete = bytes(s);
  const auto completion_tick = sortie(s).phase_start_tick;
  check(!s.request_origin_assistance() && !s.cancel_origin_assistance() &&
            bytes(s) == complete,
        "Completed finite sortie cannot silently refill/restart or refund "
        "cargo/charges");
  const auto final_resources = state(s).resources;
  need(s.advance({}));
  check(state(s).attached &&
            state(s).resources.flight_quanta == final_resources.flight_quanta &&
            state(s).resources.cargo_quanta == 0 &&
            sortie(s).phase_start_tick == completion_tick &&
            sortie(s).deadline_tick == 0,
        "Attached terminal tick retains completion event, spent propulsion and "
        "exhausted cargo without restocking");
  paired_clock(s);
  check(bytes(actual) == original,
        "Actual source landing remains byte-exact and unmodified");
  return {
      {"source_tick", start_tick},
      {"completed_tick", completion_tick},
      {"route_ticks", ticks},
      {"delivered_quanta", delivered},
      {"delivery_ticks", delivery_ticks},
      {"propulsion_quanta", propulsion},
      {"outward_transit_ticks", outward_transit_ticks},
      {"return_transit_ticks", return_transit_ticks},
      {"source_has_drive", driver.has_value()},
      {"source_has_specimens", notes.field_samples.has_value()},
      {"source_scope",
       "Actual retained landing with labelled selected assistance + fuel=0 "
       "resource fixture; no flown exhaustion or old-save auto-adoption claim"},
      {"metadata_limit",
       "Absent strain/pouch source proves absence preservation only; no claim "
       "of retained present specimens or used strain provenance"}};
}
} // namespace
int main(int argc, char** argv) {
  try {
    if (argc != 3)
      throw std::runtime_error("usage: native-origin-assistance "
                               "actual-landed-save output-directory");
    const auto output = std::filesystem::absolute(argv[2]);
    std::filesystem::create_directories(output);
    explicit_selection(output);
    const auto proof = full_route(std::filesystem::absolute(argv[1]), output);
    Json report{
        {"pass", true},
        {"checks", checks},
        {"scope",
         "Actual NativeSession route plus file SaveAs/Continue after complete "
         "format59 ABI admission; no rendered/UI/hardware claim"},
        {"route", proof}};
    std::ofstream(output / "report.json") << report.dump(2) << '\n';
    std::cout << checks << " native Origin assistance checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << " after " << checks << " checks\n";
    return 1;
  }
}
