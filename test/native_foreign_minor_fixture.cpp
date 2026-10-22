#include "native_foreign_minor_fixture.hpp"
#include "native_minor_route_fixture.hpp"

#include "apsis_drift/freedom_system_network.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"

#include <iostream>
#include <limits>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace apsis_drift::test_fixture {
namespace {
unsigned checks{};
auto check(bool value, const char* text) -> void {
  ++checks;
  if (!value) throw std::runtime_error(text);
}
template <class T, class E>
auto need(std::expected<T, E> result,
          std::source_location where = std::source_location::current()) -> T {
  if (!result) {
    std::cerr << where.file_name() << ':' << where.line() << ' ';
    if constexpr (std::is_same_v<E, std::string>)
      std::cerr << result.error();
    else if constexpr (requires { result.error().detail; })
      std::cerr << result.error().detail;
    else if constexpr (std::is_enum_v<E>)
      std::cerr << static_cast<unsigned>(result.error());
    std::cerr << '\n';
    throw std::runtime_error("Foreign minor native fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*result);
}
auto bytes(const NativeFreedomFlightSession& s) -> std::string {
  return need(encode_native_save_document_json(need(s.save_document())));
}
auto continued(const NativeFreedomFlightSession& s)
    -> std::unique_ptr<NativeFreedomFlightSession> {
  const auto before = bytes(s);
  auto decoded = std::make_unique<NativeSaveDocument>(
      need(decode_native_save_document_json(before)));
  auto selected = need(native_select_save_document(std::move(*decoded)));
  auto next = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(std::move(selected))));
  check(bytes(*next) == before,
        "Continue preserves complete foreign route bytes");
  return next;
}
auto source(std::uint32_t drive)
    -> std::unique_ptr<NativeFreedomFlightSession> {
  auto selected = need(drive == 0   ? native_network_new_game({42})
                       : drive == 1 ? native_jump_recovery_new_game({42}, 2)
                                    : native_jump_strain_new_game({42}, 2));
  auto s = std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(std::move(selected))));
  for (unsigned i = 0; i < 1194; ++i)
    need(s->advance_walk({0, -1, 0}));
  need(s->begin_boarding());
  for (SimulationTick i = 0; i < kGameplayBoardingTicks; ++i)
    need(s->advance_walk({}));
  need(s->release_port());
  auto document = std::make_unique<FreedomKnowledgeSaveDocument>(
      std::get<FreedomKnowledgeSaveDocument>(need(s->save_document())));
  auto& flight =
      std::get<FreedomBoardingSaveDocument>(document->voyage.voyage.base)
          .voyage.flight;
  const auto home = need(
      find_local_system_planet(s->system(), flight.origin.recipe.home_planet));
  // Labelled saved-space boundary, not a flown ascent. Retain the genuinely
  // boarded pilot, source model, chart, spent fuel, craft and authoritative
  // tick.
  flight.flight.position_metres = {
      0, 0, static_cast<double>(home->descriptor.radius.value) * 10000};
  flight.flight.linear_velocity_metres_per_second = {};
  flight.flight.angular_velocity_radians_per_second = {};
  flight.flight = need(canonicalize_rigid_body_state(
      RigidBodyWorldContext{s->system()}, flight.flight));
  auto next = need(native_select_save_document(std::move(*document)));
  return std::make_unique<NativeFreedomFlightSession>(
      need(NativeFreedomFlightSession::open(std::move(next))));
}
auto leg(std::unique_ptr<NativeFreedomFlightSession>& s,
         FreedomJumpRecoveryKind kind) -> void {
  const auto tick = s->document().flight.tick;
  const auto charges = s->resources()->jump_charges;
  const auto attempt = s->travel()->next_attempt;
  const auto prior_drive = s->resources()->drive;
  need(s->begin_jump());
  s = continued(*s);
  for (SimulationTick i = 1; i <= kJumpSpoolTicks + kJumpTransitTicks; ++i) {
    const auto step = need(s->advance({}));
    if (i < kJumpSpoolTicks)
      check(
          s->resources()->jump_charges == charges && !step.jump_committed &&
              s->resources()->drive == prior_drive,
          "Spool preserves charge, drive history and attempt until commitment");
    if (i == kJumpSpoolTicks) {
      check(step.jump_committed &&
                s->resources()->jump_charges + 1 == charges &&
                s->travel()->next_attempt == attempt + 1,
            "Actual commitment spends exactly one charge and one attempt");
      if (s->resources()->drive) {
        const auto& stamp = *s->resources()->drive->last_use;
        check(stamp.kind == kind && stamp.attempt == attempt &&
                  stamp.commit_tick == tick + i,
              "Foreign route binds the existing actual drive recovery stamp");
      }
      const auto committed = bytes(*s);
      check(!s->cancel_jump() && !s->set_assistance(false) &&
                bytes(*s) == committed,
            "Frozen transit refuses conflicting commands atomically");
    }
    if (i > kJumpSpoolTicks)
      check(step.fuel_debit_quanta == 0,
            "Transit advances time without invented flight fuel");
    if (i == kJumpSpoolTicks - 1 || i == kJumpSpoolTicks ||
        i == kJumpSpoolTicks + 1 || i == kJumpSpoolTicks + kJumpTransitTicks)
      s = continued(*s);
    if (i == kJumpSpoolTicks + kJumpTransitTicks)
      check(step.jump_arrived && s->travel()->phase == FreedomJumpPhase::idle,
            "The actual session installs the scheduled physical owner");
  }
  check(s->document().flight.tick == tick + kJumpSpoolTicks + kJumpTransitTicks,
        "All three legs share the exact authoritative simulation clock");
}
auto phase(NativeFreedomFlightSession& s,
           const std::filesystem::path& directory, const char* name) -> void {
  if (directory.empty()) return;
  std::filesystem::create_directories(directory);
  need(s.save_as(std::filesystem::absolute(directory / name)));
}
auto recover_foreign_minor_clone(const NativeFreedomFlightSession& source)
    -> void {
  const auto original = bytes(source);
  auto copy = continued(source);
  const auto checksum = need(rigid_body_state_checksum(
      RigidBodyWorldContext{*copy->minor()}, copy->document().flight));
  need(copy->record_loss(FreedomLossCause::recoverable_destruction,
                         copy->document().origin.state.craft,
                         copy->document().flight.tick, checksum));
  copy = continued(*copy);
  check(copy->recovery_pending(),
        "Actual foreign-minor loss survives pending Save/Continue");
  need(copy->complete_recovery());
  copy = continued(*copy);
  auto history = std::make_unique<FreedomRecoverySaveDocument>(
      need(copy->recovery_document()));
  check(history->recovery.latest && !copy->recovery_pending() &&
            copy->walker() && !copy->moon() && !copy->minor() &&
            copy->document().origin.lineage &&
            copy->document().origin.recipe == source.document().origin.recipe &&
            need(encode_freedom_travel_document_json(
                std::get<FreedomTravelSaveDocument>(
                    history->recovery.latest->retired))) == original &&
            bytes(source) == original,
        "Standard replacement retains the exact retired63 voyage without "
        "changing the independent three-charge outing");
}
} // namespace

auto run_foreign_minor_outing(std::uint32_t drive,
                              const std::filesystem::path& directory)
    -> unsigned {
  check(drive <= 2, "Unsupported test drive");
  const auto baseline = checks;
  auto s = source(drive);
  const auto network = need(generate_freedom_system_network({42}));
  const auto origin = s->document().origin;
  const BodyTarget home{network.nodes[0].system,
                        body_id(origin.recipe.home_planet)};
  need(s->select_jump(network.nodes[1].system));
  leg(s, FreedomJumpRecoveryKind::system);
  check(s->resources()->jump_charges == 2 && !s->minor(),
        "Paid genuine system arrival leaves two starter charges");
  phase(*s, directory, "foreign-entry.json");
  const auto catalog = need(generate_secondary_body_catalog(s->system()));
  const BodyTarget minor{s->system().catalog.id, catalog.bodies[1].node.id};
  auto expedition_profile = s->document().model;
  expedition_profile.hold = {};
  auto invalid = minor;
  invalid.body.value ^= 1;
  const auto before = bytes(*s);
  check(!s->select_local_jump(invalid) && bytes(*s) == before,
        "Foreign minor selection rejects another owner atomically");
  need(s->select_local_jump(minor));
  check(s->travel()->version == kFreedomForeignMinorTravelStateVersion &&
            s->travel()->composed->active ==
                FreedomComposedRouteKind::foreign_minor,
        "Explicit Minor choice adopts only the new composed policy");
  need(s->begin_jump());
  need(s->advance({}));
  need(s->cancel_jump());
  check(s->resources()->jump_charges == 2 && s->travel()->next_attempt == 2,
        "Canceling an actual foreign spool spends no charge or attempt");
  leg(s, FreedomJumpRecoveryKind::minor);
  check(s->minor() &&
            s->document().minor->version == kFreedomForeignMinorWorldVersion &&
            s->document().minor->target == minor &&
            s->resources()->jump_charges == 1 &&
            s->document().origin.recipe == origin.recipe &&
            s->boarding()->phase == FreedomBoardingPhase::seated,
        "Foreign minor arrival retains original Home identity, pilot and last "
        "charge");
  phase(*s, directory, "foreign-arrival.json");
  if (drive == 2) {
    recover_foreign_minor_clone(*s);
    const auto actual_arrival = bytes(*s);
    auto inspection = continued(*s);
    const auto inward = minor_route_fixture::approach(*inspection);
    check(inspection->minor() && !inspection->surface() &&
              inspection->document().minor->version ==
                  kFreedomForeignMinorWorldVersion,
          "Actual foreign approach retains inspection-only ownership");
    phase(*inspection, directory, "foreign-approach.json");
    const auto outward = minor_route_fixture::withdraw(*inspection);
    check(need(inspection->observe()).minor->altitude_metres >= 2000 &&
              !inspection->surface_walker(),
          "Real outward thrust leaves the existing exclusion limit");
    phase(*inspection, directory, "foreign-withdraw.json");
    check(bytes(*s) == actual_arrival,
          "Separate real approach/withdrawal leaves the main outing unchanged");
    std::cout << "Foreign minor actual approach ticks=" << inward
              << " withdrawal=" << outward << '\n';
  }
  const auto foreign_bytes = bytes(*s);
  check(!s->select_jump(home.system) && !s->select_planet_jump(home) &&
            bytes(*s) == foreign_bytes,
        "Foreign minor does not gain unrelated system or planet targeting");
  need(s->select_local_jump(home));
  check(need(s->local_jump_preview()).request.home_model.hold ==
            OrbitHoldRequest{},
        "Return selects the actual expedition planetary profile without a "
        "stale body hold");
  leg(s, FreedomJumpRecoveryKind::minor);
  check(!s->minor() && s->system().catalog.id == home.system &&
            s->document().world->planet == origin.recipe.home_planet &&
            s->resources()->jump_charges == 0 &&
            s->travel()->next_attempt == 4 &&
            s->document().model == expedition_profile &&
            s->travel()->composed->foreign_minor.outbound &&
            s->travel()->composed->foreign_minor.committed,
        "Three real charges return directly to Home with retained foreign "
        "receipts");
  check(!s->docking() || !s->docking()->attached,
        "Home arrival grants no station attachment or resource service");
  phase(*s, directory, "foreign-home.json");
  const auto retained = s->travel()->composed->foreign_minor;
  need(s->select_jump(network.nodes[2].system));
  const auto selected = bytes(*s);
  check(!s->begin_jump() && bytes(*s) == selected &&
            s->resources()->jump_charges == 0 &&
            s->travel()->composed->foreign_minor == retained,
        "Later destination selection retains the foreign excursion but cannot "
        "refill fuel");
  return checks - baseline;
}
auto run_foreign_minor_moon_switch() -> unsigned {
  const auto baseline = checks;
  auto s = source(2);
  const auto network = need(generate_freedom_system_network({42}));
  const BodyTarget home{network.nodes[0].system,
                        body_id(s->document().origin.recipe.home_planet)};
  need(s->select_jump(network.nodes[1].system));
  leg(s, FreedomJumpRecoveryKind::system);
  const auto catalog = need(generate_secondary_body_catalog(s->system()));
  const BodyTarget minor{s->system().catalog.id, catalog.bodies[1].node.id};
  const BodyTarget moon{s->system().catalog.id, catalog.bodies[0].node.id};
  need(s->select_local_jump(minor));
  s = continued(*s);
  check(s->travel()->version == kFreedomForeignMinorTravelStateVersion &&
            s->resources()->jump_charges == 2 &&
            !s->travel()->composed->foreign_minor.committed,
        "Free minor choice selects carrier9 without inventing a paid receipt");
  need(s->select_local_jump(moon));
  check(s->travel()->version == kFreedomForeignMinorTravelStateVersion &&
            s->travel()->composed->version ==
                kFreedomForeignMinorComposedRoutesVersion &&
            s->travel()->composed->active ==
                FreedomComposedRouteKind::foreign_moon,
        "Switching to the genuine Moon preserves the explicitly selected "
        "carrier9");
  leg(s, FreedomJumpRecoveryKind::moon);
  need(s->select_local_jump(home));
  leg(s, FreedomJumpRecoveryKind::moon);
  check(s->resources()->jump_charges == 0 && !s->moon() && !s->minor() &&
            s->travel()->version == kFreedomForeignMinorTravelStateVersion &&
            s->travel()->composed->foreign_moon.outbound &&
            s->travel()->composed->foreign_moon.committed &&
            !s->travel()->composed->foreign_minor.outbound &&
            !s->travel()->composed->foreign_minor.committed,
        "Reachable minor-choice/Moon outing retains its real three-charge "
        "history");
  const auto retained = s->travel()->composed->foreign_moon;
  const auto home_catalog = need(generate_secondary_body_catalog(s->system()));
  need(s->select_local_jump({home.system, home_catalog.bodies[0].node.id}));
  check(s->travel()->version == kFreedomForeignMinorTravelStateVersion &&
            s->travel()->composed->foreign_moon == retained,
        "A later Origin companion choice cannot downgrade carrier9 or discard "
        "Moon receipts");
  return checks - baseline;
}
} // namespace apsis_drift::test_fixture
