#include "apsis_drift/godot/saved_contact_geometry.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"
#include "godot/contact_geometry_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;
using V = RigidVector3;
unsigned checks{};
auto check(bool value, const char* text) -> void {
  ++checks;
  if (!value) throw std::runtime_error(text);
}
template <class T, class E>
auto need(std::expected<T, E> result,
          std::source_location where = std::source_location::current()) -> T {
  if (!result) {
    auto text = "Native specimen scenario line " + std::to_string(where.line());
    if constexpr (std::is_same_v<E, std::string>)
      text += ": " + result.error();
    else if constexpr (requires { result.error().detail; })
      text += ": " + result.error().detail;
    throw std::runtime_error(text);
  }
  if constexpr (!std::is_void_v<T>) return std::move(*result);
}
auto bytes(const NativeFreedomFlightSession& session) -> std::string {
  return need(encode_native_save_document_json(need(session.save_document())));
}
auto reopen(const NativeFreedomFlightSession& source)
    -> NativeFreedomFlightSession {
  const auto saved = bytes(source);
  auto result =
      need(NativeFreedomFlightSession::open(need(native_select_save_document(
          need(decode_native_save_document_json(saved))))));
  check(bytes(result) == saved, "Buyer whole Save/Continue differs");
  return result;
}
auto knowledge_pointer(Json& document) -> Json* {
  auto* current = &document;
  for (unsigned depth = 0; depth < 12; ++depth) {
    if (current->contains("knowledge")) return &(*current)["knowledge"];
    auto* next = static_cast<Json*>(nullptr);
    for (const auto* key : {"voyage", "base", "journey"})
      if (current->contains(key) && (*current)[key].is_object()) {
        next = &(*current)[key];
        break;
      }
    if (!next) break;
    current = next;
  }
  throw std::runtime_error("Buyer test missing bounded knowledge owner");
}
auto only_sale_changed(std::string_view before,
                       const NativeFreedomFlightSession& session) -> void {
  auto original = Json::parse(before);
  auto after = Json::parse(bytes(session));
  auto* old = knowledge_pointer(original);
  auto* current = knowledge_pointer(after);
  if (old->contains("specimen_sales"))
    (*current)["specimen_sales"] = (*old)["specimen_sales"];
  else
    current->erase("specimen_sales");
  if (old->contains("paid_operations"))
    (*current)["paid_operations"] = (*old)["paid_operations"];
  else
    current->erase("paid_operations");
  (*current)["field_samples"]["carried"] = (*old)["field_samples"]["carried"];
  check(after == original,
        "Buyer changed state beyond carried specimens/sale/income account");
}
auto plus(V a, V b) -> V {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto scale(V a, double s) -> V {
  return {s * a.x, s * a.y, s * a.z};
}
auto rotate(RigidOrientation q, V v) -> V {
  const V u{q.x, q.y, q.z};
  const auto cross = [](V a, V b) -> V {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
  };
  const auto a = cross(u, v), b = cross(u, a);
  const auto factor = 2 / (q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
  return plus(v, scale(plus(scale(a, q.w), b), factor));
}
auto collection_session() -> NativeFreedomFlightSession {
  auto session = need(NativeFreedomFlightSession::open(
      need(native_planetary_network_new_game({42}))));
  // Actual station walking/boarding; the following descent/support and actor
  // placement are explicitly initialized, not an uninterrupted flown excursion.
  for (unsigned tick = 0; tick < 1194; ++tick)
    need(session.advance_walk({0, -1, 0}));
  need(session.begin_boarding());
  for (SimulationTick tick = 0; tick < kGameplayBoardingTicks; ++tick)
    need(session.advance_walk({}));
  need(session.release_port());
  auto selected =
      std::get<FreedomKnowledgeSaveDocument>(need(session.save_document()));
  auto& flight =
      std::get<FreedomBoardingSaveDocument>(selected.voyage.voyage.base)
          .voyage.flight;
  const auto planet =
      session.system().catalog.planets[kOriginHomePlanetOrdinal].descriptor;
  const auto rotation =
      need(generate_planet_rotation_recipe(session.system(), planet.id));
  auto cache = need(TerrainTileCache::create(4));
  const auto triangle =
      need(godot_spike::detail::build_selected_contact_triangle(
          session.system(), planet,
          godot_spike::kExperimentalSavedContactSurface,
          {planet.id, CubeFace::positive_x, 4000, 4000, 12, 13, 0}, cache));
  const auto& a = triangle.vertices[0];
  const auto& b = triangle.vertices[1];
  const auto& c = triangle.vertices[2];
  const V center{a.x + .3 * ((b.x - a.x) + (c.x - a.x)),
                 a.y + .3 * ((b.y - a.y) + (c.y - a.y)),
                 a.z + .3 * ((b.z - a.z) + (c.z - a.z))};
  const auto n = triangle.outward_normal;
  const auto w = 1 + n.y, magnitude = std::hypot(w, n.z, n.x);
  RigidBodyState fixed;
  fixed.craft = flight.flight.craft;
  fixed.tick = flight.flight.tick;
  fixed.frame = {
      RigidFrameKind::planet_fixed, session.system().catalog.id, planet.id, {}};
  fixed.orientation = {w / magnitude, n.z / magnitude, 0, -n.x / magnitude};
  fixed.position_metres =
      plus(plus(center, scale({n.x, n.y, n.z}, -.05)),
           scale(rotate(fixed.orientation, {0, -2.08, -.7}), -1));
  flight.flight =
      need(reframe_rigid_body({session.system()}, fixed,
                              {{RigidFrameKind::planet_relative_inertial,
                                session.system().catalog.id,
                                planet.id,
                                {}},
                               fixed.tick},
                              rotation));
  flight.model.terrain_policy = kFreedomLegacyTerrainPolicy;
  flight.model.assistance = false;
  flight.model.hold = {};
  session = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(selected))));
  need(session.set_landing_gear(true));
  need(session.commit_touchdown(need(rigid_body_state_checksum(
      {session.system()}, session.document().flight))));
  need(session.begin_surface_walk());
  auto field = need(SurfaceLandmarkField::create(
      session.system(), body_id(planet.id), kFreedomLegacyTerrainPolicy));
  std::vector<SurfaceLandmarkSite> sites;
  for (unsigned offset = 0; offset < 8 && sites.empty(); ++offset)
    sites = need(field.nearby({fixed.position_metres.x,
                               fixed.position_metres.y + offset * 256,
                               fixed.position_metres.z}));
  // Initialize the same actor at an existing, dry, terrain-certified site. No
  // saved inspection or sample is injected: both actions below are real APIs.
  const auto outside = need(session.surface_walk_document());
  std::optional<NativeFreedomFlightSession> chosen;
  SurfaceLandmarkId site;
  for (const auto& candidate : sites) {
    auto placed = outside;
    placed.actor->foot_position_metres = {candidate.position_metres.x,
                                          candidate.position_metres.y,
                                          candidate.position_metres.z};
    placed.actor->velocity_metres_per_second = {};
    auto startup = native_select_save_document(NativeSaveDocument{placed});
    if (!startup) continue;
    auto trial = NativeFreedomFlightSession::open(*startup);
    if (!trial) continue;
    const auto view = need(trial->surface_landmarks_view());
    if (!std::ranges::any_of(view.sites, [&](const auto& row) {
          return row.site.identity.id == candidate.identity.id &&
                 row.can_inspect;
        }))
      continue;
    chosen.emplace(std::move(*trial));
    site = candidate.identity.id;
    break;
  }
  check(chosen.has_value(),
        "No bounded existing dry site admits initialized outside actor");
  auto collected = std::move(*chosen);
  const auto untouched = bytes(collected);
  check(!collected.collect_field_sample(site) && bytes(collected) == untouched,
        "Actual specimen collection must require prior inspection");
  need(collected.inspect_surface_landmark(site));
  need(collected.collect_field_sample(site));
  check(collected.knowledge()->field_samples->carried.size() == 1 &&
            collected.knowledge()->field_samples->journal ==
                collected.knowledge()->field_samples->carried,
        "Actual collection did not create one exact physical specimen/first "
        "note");
  return collected;
}
auto at_origin(const NativeFreedomFlightSession& collected, bool seated)
    -> NativeFreedomFlightSession {
  auto document = std::get<FreedomKnowledgeSaveDocument>(
      need(native_planetary_network_new_game({42})).document);
  auto& assembly = std::get<FreedomStartingAssemblySaveDocument>(
      document.voyage.voyage.base);
  auto& flight = assembly.journey.voyage.flight;
  const auto tick = collected.document().flight.tick;
  const auto owner = need(generate_physical_origin_system({42}, 2));
  const auto station = generate_origin_station({42});
  const auto geometry = need(origin_station_geometry(station));
  flight.flight = need(release_origin_port(
      owner, station, geometry,
      {geometry.version, assembly.journey.voyage.docking.target,
       flight.flight.craft, tick}));
  flight.origin.state.tick = tick;
  document.voyage.resources = *collected.resources();
  document.knowledge = *collected.knowledge();
  if (seated) {
    // Retain the completed, real boarding route from the collection source;
    // only the same-tick attached voyage is initialized at Origin here.
    auto boarding = std::get<FreedomBoardingSaveDocument>(
        collected.surface_document().base);
    boarding.voyage = assembly.journey.voyage;
    document.voyage.voyage.base = std::move(boarding);
  }
  return need(NativeFreedomFlightSession::open(
      need(native_select_save_document(document))));
}
auto with_full_financial_pouch(const NativeFreedomFlightSession& one)
    -> NativeFreedomFlightSession {
  auto document =
      std::get<FreedomKnowledgeSaveDocument>(need(one.save_document()));
  auto& samples = *document.knowledge.field_samples;
  const auto& body =
      one.system().catalog.planets[kOriginHomePlanetOrdinal].descriptor;
  const auto radius = static_cast<double>(body.radius.value) * 1000;
  // Additional exact site identities/inspection/sample records and debt
  // balances are initialized financial fixtures, not four walked collections or
  // debt history.
  for (int offset = 0; offset < 64 && samples.carried.size() < 4; ++offset) {
    const SurfaceLandmarkCell base{static_cast<std::int32_t>(radius / 512),
                                   offset, 0};
    for (std::int32_t x = -1; x <= 1; ++x)
      for (std::int32_t y = -1; y <= 1; ++y)
        for (std::int32_t z = -1; z <= 1; ++z) {
          const SurfaceLandmarkCell cell{base.x + x, base.y + y, base.z + z};
          const auto identity = need(identify_surface_landmark(body, cell));
          if (!identity || samples.carried.size() == 4 ||
              std::ranges::any_of(samples.carried, [&](const auto& r) {
                return r.site == identity->id;
              }))
            continue;
          const FreedomFieldSampleRecord record{
              {one.system().catalog.id, body_id(body.id)},
              cell,
              identity->id,
              1,
              identity->kind,
              samples.carrier,
              one.document().flight.tick};
          document.knowledge.surface_landmarks->inspections.push_back(
              {record.target.system, body.id, cell, record.site, 1,
               record.tick});
          samples.carried.push_back(record);
          samples.journal.push_back(record);
        }
  }
  const auto key = [](const auto& r) {
    return std::tuple{r.target.system.value,
                      static_cast<unsigned>(r.target.body.kind),
                      r.target.body.value, r.site.value};
  };
  std::ranges::sort(samples.carried, {}, key);
  std::ranges::sort(samples.journal, {}, key);
  std::ranges::sort(
      document.knowledge.surface_landmarks->inspections, {}, [](const auto& r) {
        return std::tuple{r.system.value, r.planet.value, r.site.value};
      });
  check(samples.carried.size() == 4 && samples.journal.size() == 4,
        "Full-pouch initialized records unavailable");
  document.knowledge.paid_operations = need(make_paid_operations(0, 0, 0));
  document.knowledge.paid_operations->spent_credits = 200;
  document.knowledge.paid_operations->operating_tab = 200;
  document.knowledge.paid_operations->recovery_due = 100;
  return need(NativeFreedomFlightSession::open(
      need(native_select_save_document(document))));
}
auto checkpoint(const NativeFreedomFlightSession& session,
                const std::optional<std::filesystem::path>& output,
                std::string_view name, Json& phases) -> void {
  auto restored = reopen(session);
  const auto view = need(session.specimen_sale_view());
  check(need(restored.specimen_sale_view()).earned_credits ==
            view.earned_credits,
        "Buyer Continue changed personal specimen earnings");
  if (output) need(session.save_as(*output / (std::string{name} + ".json")));
  phases.push_back({{"file", std::string{name} + ".json"},
                    {"tick", std::to_string(view.tick)},
                    {"carried", view.carried_count},
                    {"earned", std::to_string(view.earned_credits)}});
}
auto sell(NativeFreedomFlightSession& session, std::uint64_t gross,
          std::uint64_t insurance, std::uint64_t tab) -> void {
  const auto quote = need(session.specimen_sale_view());
  check(
      quote.can_sell && quote.gross_credits == gross &&
          quote.insurance_credits == insurance && quote.tab_credits == tab &&
          quote.net_credits == gross - insurance - tab,
      "Actual buyer quote disagrees with disclosed physical batch/deductions");
  const auto before = bytes(session);
  check(!session.sell_specimens(quote.token + "x") && bytes(session) == before,
        "Stale specimen quote changed whole Save or destroyed specimens");
  for (unsigned i = 0; i < 3; ++i)
    (void)need(session.specimen_sale_view());
  check(bytes(session) == before,
        "Buyer polling posted income or removed carried records");
  need(session.sell_specimens(quote.token));
  only_sale_changed(before, session);
  const auto after = bytes(session);
  check(
      session.knowledge()->field_samples->carried.empty() &&
          !need(session.specimen_sale_view()).can_sell &&
          !session.sell_specimens(quote.token) && bytes(session) == after,
      "Sale did not remove actual pouch or duplicate buyer command paid again");
}
} // namespace

auto run_native_specimen_sale_scenario(
    const std::optional<std::filesystem::path>& parent) -> void {
  std::optional<std::filesystem::path> output;
  if (parent) {
    output = *parent / "specimen-sales";
    std::filesystem::create_directories(*output);
  }
  Json phases = Json::array();
  auto unsupported =
      need(NativeFreedomFlightSession::open(need(native_new_game({42}))));
  const auto unsupported_before = bytes(unsupported);
  check(!need(unsupported.specimen_sale_view()).supported &&
            !unsupported.sell_specimens("foreign") &&
            bytes(unsupported) == unsupported_before,
        "Ordinary historical chart gained a sale policy or malformed command "
        "mutation");
  auto remote = collection_session();
  const auto remote_bytes = bytes(remote);
  const auto remote_quote = need(remote.specimen_sale_view());
  check(!remote_quote.at_origin && remote_quote.carried_count == 1 &&
            !remote.sell_specimens(remote_quote.token) &&
            bytes(remote) == remote_bytes,
        "Outside collected sample was sold remotely");
  checkpoint(remote, output, "sale-remote", phases);
  // Same-tick initialized return to actual moving Origin attachment. Resources,
  // journal and collected provenance are preserved; no flown return is claimed.
  auto origin = at_origin(remote, false);
  checkpoint(origin, output, "sale-origin-one", phases);
  auto paid = origin;
  auto enrollment = need(paid.paid_operations_view());
  need(paid.enable_paid_operations(enrollment.token));
  check(paid.knowledge()->paid_operations->version == 1,
        "No-sales enrollment unexpectedly promoted paid version");
  checkpoint(paid, output, "sale-origin-paid", phases);
  sell(paid, 40, 0, 0);
  check(need(paid.paid_operations_view()).cash == 220 &&
            paid.knowledge()->paid_operations->version == 2,
        "First paid sale reset opening credits or lost40income");
  checkpoint(paid, output, "sale-paid-one", phases);
  {
    // Exercise the unchanged-count commission command path on a private
    // paid-v2 clone. Original exported buyer source/result bytes stay fixed.
    auto commissions = reopen(paid);
    const auto account = *paid.knowledge()->paid_operations;
    const auto offer = need(commissions.survey_commission_view());
    need(commissions.accept_survey_commission(offer.offer_until,
                                              offer.offer_sequence));
    auto continued = reopen(commissions);
    need(commissions.abandon_survey_commission(offer.offer_sequence));
    need(continued.abandon_survey_commission(offer.offer_sequence));
    check(bytes(commissions) == bytes(continued) &&
              commissions.document() == paid.document() &&
              commissions.resources() == paid.resources() &&
              commissions.knowledge()->paid_operations == account &&
              commissions.knowledge()->specimen_sales ==
                  paid.knowledge()->specimen_sales &&
              need(commissions.paid_operations_view()).cash == 220,
          "Paid-v2 accept/abandon or Continue lost specimen income or changed "
          "physical owners");
    const auto next = need(commissions.survey_commission_view());
    check(next.offer_sequence > offer.offer_sequence,
          "Abandoned paid-v2 commission did not advance the fresh reservation");
    need(commissions.accept_survey_commission(next.offer_until,
                                              next.offer_sequence));
    check(commissions.knowledge()->paid_operations == account &&
              commissions.knowledge()->specimen_sales ==
                  paid.knowledge()->specimen_sales &&
              need(commissions.paid_operations_view()).cash == 220,
          "Repeated fresh paid-v2 acceptance reset specimen counter or cash");
  }
  sell(origin, 40, 0, 0);
  check(!origin.knowledge()->paid_operations &&
            need(origin.specimen_sale_view()).earned_credits == 40,
        "Pre-enrollment sale implicitly selected paid profile or lost gross "
        "earnings");
  checkpoint(origin, output, "sale-earning-one", phases);
  enrollment = need(origin.paid_operations_view());
  need(origin.enable_paid_operations(enrollment.token));
  check(need(origin.paid_operations_view()).cash == 220 &&
            origin.knowledge()->paid_operations->version == 2,
        "Explicit enrollment failed to carry prior specimen income");
  checkpoint(origin, output, "sale-enrolled", phases);
  auto seated = with_full_financial_pouch(at_origin(remote, true));
  check(seated.boarding() &&
            seated.boarding()->phase == FreedomBoardingPhase::seated &&
            seated.docking()->attached,
        "Four-specimen quote lacks actual seated/attached owner");
  checkpoint(seated, output, "sale-origin-four", phases);
  sell(seated, 160, 100, 30);
  check(need(seated.paid_operations_view()).cash == 30 &&
            need(seated.paid_operations_view()).operating_tab == 170 &&
            need(seated.paid_operations_view()).recovery_due == 0,
        "Four-specimen batch did not settle insurance then half remaining "
        "toward tab");
  checkpoint(seated, output, "sale-paid-four", phases);
  auto service_document =
      std::get<FreedomKnowledgeSaveDocument>(need(paid.save_document()));
  service_document.voyage.resources.flight_quanta =
      kFreedomFlightCapacityQuanta / 2;
  service_document.voyage.resources.jump_charges = 2;
  auto service = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(service_document))));
  checkpoint(service, output, "sale-service-source", phases);
  const auto fuel = need(service.paid_operations_view());
  check(fuel.can_purchase && fuel.service_credits == 35 && fuel.cash == 220,
        "Native-v2 service quote ignored specimen income or actual stores");
  need(service.purchase_paid_fuel(fuel.token));
  check(need(service.paid_operations_view()).cash == 185 &&
            service.knowledge()->specimen_sales ==
                paid.knowledge()->specimen_sales &&
            service.knowledge()->paid_operations->settled_specimen_count == 1 &&
            service.resources()->jump_charges == 3,
        "Native-v2 ordinary paid service lost sample income/count/provenance");
  checkpoint(service, output, "sale-service-paid", phases);
  auto exhausted_document = std::get<FreedomKnowledgeSaveDocument>(
      need(at_origin(remote, false).save_document()));
  // This exhausted-counter metadata fixture needs a distinct historical sold
  // collection; the actual current pouch cannot also be its latest sold batch.
  const auto& historical = seated.knowledge()->specimen_sales->latest.specimens;
  const auto prior = std::ranges::find_if(historical, [&](const auto& record) {
    return record.site !=
           exhausted_document.knowledge.field_samples->carried.front().site;
  });
  check(prior != historical.end(),
        "Distinct authenticated exhausted-counter sale history unavailable");
  exhausted_document.knowledge.field_samples->journal =
      seated.knowledge()->field_samples->journal;
  exhausted_document.knowledge.surface_landmarks =
      seated.knowledge()->surface_landmarks;
  exhausted_document.knowledge.specimen_sales = FreedomSpecimenSalesAccount{
      1,
      UINT32_MAX,
      {exhausted_document.knowledge.field_samples->carrier,
       remote.document().flight.tick,
       {*prior}}};
  auto exhausted = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(exhausted_document))));
  const auto exhausted_before = bytes(exhausted);
  const auto exhausted_quote = need(exhausted.specimen_sale_view());
  check(!exhausted_quote.can_sell &&
            !exhausted.sell_specimens(exhausted_quote.token) &&
            bytes(exhausted) == exhausted_before &&
            exhausted.knowledge()->field_samples->carried.size() == 1,
        "Exhausted initialized counter destroyed or paid its actual specimen");
  auto lost = at_origin(remote, false);
  const auto recovery = need(lost.recovery_document());
  need(lost.record_loss(
      FreedomLossCause::recoverable_destruction,
      lost.document().origin.state.craft, lost.document().flight.tick,
      need(freedom_recovery_source_checksum(recovery.voyage))));
  const auto pending_before = bytes(lost);
  check(!need(lost.specimen_sale_view()).can_sell &&
            !lost.sell_specimens(need(lost.specimen_sale_view()).token) &&
            bytes(lost) == pending_before,
        "Pending loss sold its frozen carried specimen");
  need(lost.complete_recovery());
  check(!lost.knowledge()->specimen_sales &&
            lost.knowledge()->field_samples->carried.empty() &&
            lost.knowledge()->field_samples->journal ==
                remote.knowledge()->field_samples->journal,
        "Loss of unsold physical sample minted a sale or discarded notes");
  auto recovered = paid;
  const auto sold_recovery = need(recovered.recovery_document());
  need(recovered.record_loss(
      FreedomLossCause::recoverable_destruction,
      recovered.document().origin.state.craft, recovered.document().flight.tick,
      need(freedom_recovery_source_checksum(sold_recovery.voyage))));
  need(recovered.complete_recovery());
  check(recovered.knowledge()->specimen_sales ==
                paid.knowledge()->specimen_sales &&
            recovered.knowledge()->field_samples->journal ==
                paid.knowledge()->field_samples->journal &&
            need(recovered.paid_operations_view()).cash == 120,
        "Replacement reset prior sale, notes or insurance debit");
  const auto replacement = bytes(recovered);
  need(recovered.complete_recovery());
  check(bytes(recovered) == replacement,
        "Matching replacement billed specimen income twice");
  checkpoint(recovered, output, "sale-recovered", phases);
  if (output) {
    std::ofstream file{*output / "manifest.json"};
    file << Json{{"schema", "apsis.native-specimen-sale-scenario/1"},
                 {"checks", checks},
                 {"phases", phases},
                 {"scope",
                  "Actual station walking/boarding and "
                  "inspect/collect/buyer/enroll/service/replacement APIs. "
                  "Initialized genuine legacy-contact landing and outside-foot "
                  "position at one existing dry exposure, same-tick Origin "
                  "return attachment; additional three specimen "
                  "identities/inspection records/full-pouch, debt balances, "
                  "depleted service stores and exhausted counter are "
                  "explicitly initialized fixtures. Not an uninterrupted flown "
                  "excursion, four walked collections, earned debt history, "
                  "ordinary profitability, manual or GPU acceptance."}}
                .dump(2)
         << '\n';
    check(static_cast<bool>(file), "Specimen scenario manifest write failed");
  }
  std::cout << "Native specimen sales: " << checks << " checks, 0 failures\n";
}
} // namespace apsis_drift
