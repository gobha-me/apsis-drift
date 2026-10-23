#include "apsis_drift/freedom_recovery_save.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <nlohmann/json.hpp>
#include <type_traits>
#include <unordered_set>

namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;
auto failure(std::string message) -> SaveSchemaError {
  return {SaveSchemaErrorCode::invalid_state, "$.recovery", std::move(message)};
}
auto validate_voyage(const FreedomRecoveryVoyage& voyage)
    -> std::expected<void, SaveSchemaError> {
  return std::visit(
      [](const auto& d) {
        if constexpr (std::is_same_v<std::decay_t<decltype(d)>,
                                     FreedomTravelSaveDocument>)
          return validate_freedom_travel_document(d);
        else {
          if (d.voyage.resources.drive && d.voyage.resources.drive->last_use)
            return std::expected<void, SaveSchemaError>{std::unexpected{
                failure("A recorded drive use requires its travel57 owner")}};
          return validate_freedom_knowledge_document(d);
        }
      },
      voyage);
}
auto encode_voyage(const FreedomRecoveryVoyage& voyage)
    -> std::expected<std::string, SaveSchemaError> {
  return std::visit(
      [](const auto& d) {
        if constexpr (std::is_same_v<std::decay_t<decltype(d)>,
                                     FreedomTravelSaveDocument>)
          return encode_freedom_travel_document_json(d);
        else
          return encode_freedom_knowledge_document_json(d);
      },
      voyage);
}
auto fields(const Json& j, std::initializer_list<std::string_view> names)
    -> bool {
  if (!j.is_object() || j.size() != names.size()) return false;
  for (auto name : names)
    if (!j.contains(std::string{name})) return false;
  return true;
}
auto uints(const Json& j, std::initializer_list<std::string_view> names)
    -> bool {
  for (auto name : names)
    if (!j.contains(std::string{name}) ||
        !j[std::string{name}].is_number_unsigned())
      return false;
  return true;
}
auto decode_voyage(const Json& j)
    -> std::expected<FreedomRecoveryVoyage, SaveSchemaError> {
  if (!j.is_object() || !uints(j, {"format_version"}))
    return std::unexpected{failure("unsigned voyage format required")};
  const auto text = j.dump();
  if (j["format_version"] == kFreedomKnowledgeSaveFormatVersion) {
    auto d = decode_freedom_knowledge_document_json(text);
    if (!d) return std::unexpected{d.error()};
    return FreedomRecoveryVoyage{std::move(*d)};
  }
  if (j["format_version"] == kFreedomTravelSaveFormatVersion ||
      j["format_version"] == kFreedomLocalTravelSaveFormatVersion ||
      j["format_version"] == kFreedomMinorTravelSaveFormatVersion ||
      j["format_version"] == kFreedomOriginLocalTravelSaveFormatVersion ||
      j["format_version"] == kFreedomNetworkTravelSaveFormatVersion ||
      j["format_version"] == kFreedomComposedTravelSaveFormatVersion ||
      j["format_version"] == kFreedomRecoveringTravelSaveFormatVersion ||
      j["format_version"] == kFreedomStrainedTravelSaveFormatVersion ||
      j["format_version"] == kFreedomForeignMoonTravelSaveFormatVersion ||
      j["format_version"] == kFreedomForeignMinorTravelSaveFormatVersion) {
    auto d = decode_freedom_travel_document_json(text);
    if (!d) return std::unexpected{d.error()};
    return FreedomRecoveryVoyage{std::move(*d)};
  }
  return std::unexpected{
      failure("only a single knowledge/travel voyage is supported")};
}
auto generation(const FreedomSaveDocument& d) -> std::uint64_t {
  return d.lineage ? d.lineage->generation : 0;
}
auto starter_selected(const FreedomRecoveryVoyage& v) -> bool {
  if (const auto* t = std::get_if<FreedomTravelSaveDocument>(&v))
    return t->craft_binding &&
           *t->craft_binding == NativeStartingAssemblySelection{};
  return std::visit(
      [](const auto& b) -> bool {
        using T = std::decay_t<decltype(b)>;
        if constexpr (std::is_same_v<T, FreedomStartingAssemblySaveDocument> ||
                      std::is_same_v<T, FreedomBoardingSaveDocument>)
          return b.starting_assembly == NativeStartingAssemblySelection{};
        else
          return false;
      },
      recovery_knowledge(v).voyage.voyage.base);
}
template <class T>
auto retains(const std::vector<T>& now, const std::vector<T>& old) -> bool {
  return old.size() <= now.size() &&
         std::equal(old.begin(), old.end(), now.begin());
}
auto retains_commissions(const std::optional<SurveyCommissionAccount>& now,
                         const std::optional<SurveyCommissionAccount>& old,
                         StarterCraftId craft, SimulationTick loss_tick,
                         SimulationTick now_tick) -> bool {
  if (!now) return !old;
  if (now_tick < loss_tick ||
      (old && (now->version != old->version ||
               now->next_sequence < old->next_sequence ||
               now->paid_count < old->paid_count)))
    return false;
  const auto prior_next = old ? old->next_sequence : 1U;
  const auto prior_paid = old ? old->paid_count : 0U;
  const auto same_terms = [](const SurveyCommission& a,
                             const SurveyCommission& b) {
    return a.sequence == b.sequence && a.system == b.system &&
           a.planet == b.planet && a.accepted_tick == b.accepted_tick &&
           a.deadline_tick == b.deadline_tick;
  };
  const auto admitted = [&](const SurveyCommission& commission) {
    if (old && old->active && commission.sequence == old->active->sequence) {
      if (!same_terms(commission, *old->active)) return false;
      if (old->active->measurement &&
          old->active->measurement->acquired_ticks ==
              kSurveyCommissionMeasurementTicks)
        return commission.measurement == old->active->measurement;
    } else if (commission.sequence < prior_next ||
               commission.accepted_tick < loss_tick) {
      return false;
    }
    // Replacement discards unfinished work. Any later acquisition belongs to
    // its own craft and begins after the retired source's loss boundary.
    return !commission.measurement ||
           (commission.measurement->craft == craft &&
            commission.measurement->started_tick > loss_tick);
  };
  if (now->active && !admitted(*now->active)) return false;
  if (now->paid_count == prior_paid)
    return now->latest_paid == (old ? old->latest_paid : std::nullopt);
  if (!now->latest_paid || now->latest_paid->submitted_tick < loss_tick ||
      !admitted(now->latest_paid->commission) ||
      (old && old->latest_paid &&
       now->latest_paid->commission.sequence <=
           old->latest_paid->commission.sequence))
    return false;
  const auto paid = static_cast<std::uint64_t>(now->paid_count - prior_paid);
  const auto new_acceptances =
      static_cast<std::uint64_t>(now->next_sequence - prior_next);
  if (paid > new_acceptances + (old && old->active ? 1U : 0U)) return false;
  const bool retained_ready = old && old->active && old->active->measurement &&
                              old->active->measurement->acquired_ticks ==
                                  kSurveyCommissionMeasurementTicks &&
                              loss_tick < old->active->deadline_tick;
  // Only one already-complete report can pay immediately after replacement;
  // every additional payment needs 3600 fresh committed intervals. This bounds
  // cumulative earnings without introducing an unbounded receipt history.
  return paid <= (now_tick - loss_tick) / kSurveyCommissionMeasurementTicks +
                     (retained_ready ? 1U : 0U);
}
auto retains_paid_operations(const FreedomKnowledge& now,
                             const FreedomKnowledge& old,
                             SimulationTick loss_tick) -> bool {
  if (!now.paid_operations) return !old.paid_operations;
  if (!old.paid_operations)
    return now.paid_operations->started_tick >= loss_tick;
  const auto new_generation =
      now.recipe.craft_lineage ? now.recipe.craft_lineage->generation : 0;
  const auto prior_count =
      old.survey_commissions ? old.survey_commissions->paid_count : 0U;
  const auto charged = charge_paid_recovery(*old.paid_operations, prior_count,
                                            loss_tick, new_generation);
  if (!charged) return false;
  const auto& current = *now.paid_operations;
  const auto& required = *charged;
  if (current.version != required.version ||
      current.started_tick != required.started_tick ||
      current.billed_through_hours < required.billed_through_hours ||
      current.spent_credits < required.spent_credits ||
      current.supported_sequence < required.supported_sequence ||
      current.settled_paid_count < required.settled_paid_count ||
      current.recovery_due > required.recovery_due)
    return false;
  // Actual post-loss spending/support may change the final debt amounts. Keep
  // necessary conservation and income-priority bounds rather than inventing
  // an unbounded transaction history or requiring the immediate result forever.
  // Counts are validated u32 values; their difference times100 fits in u64.
  const auto income = static_cast<std::uint64_t>(current.settled_paid_count -
                                                 required.settled_paid_count) *
                      kPaidSurveyRewardCredits;
  const auto insurance = std::min(required.recovery_due, income);
  const auto tab_payment =
      std::min(required.operating_tab, (income - insurance) / 2);
  if (current.recovery_due != required.recovery_due - insurance ||
      current.operating_tab < required.operating_tab - tab_payment)
    return false;
  const auto debt_reduction =
      required.recovery_due - current.recovery_due +
      (current.operating_tab < required.operating_tab
           ? required.operating_tab - current.operating_tab
           : 0);
  return current.spent_credits - required.spent_credits >=
         std::max(insurance + tab_payment, debt_reduction);
}
auto retains_knowledge(const FreedomKnowledge& now, const FreedomKnowledge& old,
                       StarterCraftId current, SimulationTick loss_tick,
                       SimulationTick now_tick) -> bool {
  auto recipe = old.recipe;
  // The only approved expansion is the existing neighboring-world domain.
  if (recipe.version == kFreedomObservedKnowledgeVersion &&
      now.recipe.version == kFreedomTravelKnowledgeVersion) {
    recipe.version = kFreedomTravelKnowledgeVersion;
    recipe.world_domain = 1;
  }
  recipe.craft_lineage = now.recipe.craft_lineage;
  if (now.recipe != recipe) return false;
  if (!retains_commissions(now.survey_commissions, old.survey_commissions,
                           current, loss_tick, now_tick))
    return false;
  if (!retains_paid_operations(now, old, loss_tick)) return false;
  if (old.station_stellar_lead) {
    if (now.station_stellar_lead != old.station_stellar_lead) return false;
  } else if (now.station_stellar_lead &&
             (now.station_stellar_lead->craft != current ||
              now.station_stellar_lead->received_tick < loss_tick)) {
    return false;
  }
  if (now.derelicts.has_value() != old.derelicts.has_value()) return false;
  if (old.derelicts) {
    if (now.derelicts->version != old.derelicts->version) return false;
    const auto retained = [&](const auto& current_record,
                              const auto& old_record) {
      if (old_record) return current_record == old_record;
      return !current_record || (current_record->craft == current &&
                                 current_record->tick >= loss_tick);
    };
    if (!retained(now.derelicts->contact, old.derelicts->contact) ||
        !retained(now.derelicts->inspection, old.derelicts->inspection))
      return false;
  }
  const auto same_sample = [](const auto& a, const auto& b) {
    return a.target == b.target && a.site == b.site;
  };
  if (old.field_samples) {
    if (!now.field_samples ||
        (now.field_samples->version != old.field_samples->version &&
         !(old.field_samples->version == kFreedomFieldSamplesVersion &&
           now.field_samples->version == kFreedomNetworkFieldSamplesVersion)))
      return false;
    for (const auto& record : old.field_samples->journal) {
      const auto& journal = now.field_samples->journal;
      const auto found = std::ranges::find_if(
          journal, [&](const auto& item) { return same_sample(item, record); });
      if (found == journal.end() || *found != record) return false;
    }
  }
  if (now.field_samples) {
    if (now.field_samples->carrier != current) return false;
    for (const auto& record : now.field_samples->journal) {
      const bool retained =
          old.field_samples &&
          std::ranges::any_of(
              old.field_samples->journal,
              [&](const auto& item) { return same_sample(item, record); });
      if (!retained && (record.craft != current || record.tick < loss_tick))
        return false;
    }
    // Replacement starts with an empty pouch. A later collection must belong
    // to the new carrier and cannot be backdated before its loss boundary.
    for (const auto& record : now.field_samples->carried)
      if (record.craft != current || record.tick < loss_tick) return false;
  }
  if (old.orbital_surveys) {
    if (!now.orbital_surveys ||
        now.orbital_surveys->version != old.orbital_surveys->version)
      return false;
    for (const auto& record : old.orbital_surveys->completed) {
      const auto found = std::ranges::find_if(
          now.orbital_surveys->completed, [&](const auto& candidate) {
            return candidate.system == record.system &&
                   candidate.planet == record.planet;
          });
      if (found == now.orbital_surveys->completed.end() || *found != record)
        return false;
    }
  }
  if (now.orbital_surveys) {
    for (const auto& record : now.orbital_surveys->completed) {
      const bool retained =
          old.orbital_surveys &&
          std::ranges::any_of(old.orbital_surveys->completed,
                              [&](const auto& previous) {
                                return previous.system == record.system &&
                                       previous.planet == record.planet;
                              });
      if (!retained &&
          (record.craft != current || record.started_tick < loss_tick ||
           record.completed_tick < loss_tick))
        return false;
    }
    if (now.orbital_surveys->active &&
        (now.orbital_surveys->active->craft != current ||
         now.orbital_surveys->active->started_tick < loss_tick))
      return false;
  }
  if (old.star_search &&
      (!now.star_search ||
       now.star_search->version != old.star_search->version ||
       !retains(now.star_search->completed, old.star_search->completed)))
    return false;
  if (now.star_search) {
    const auto retained_count =
        old.star_search ? old.star_search->completed.size() : 0;
    for (auto i = retained_count; i < now.star_search->completed.size(); ++i) {
      const auto& record = now.star_search->completed[i];
      if (record.craft != current || record.started_tick < loss_tick ||
          record.completed_tick < loss_tick)
        return false;
    }
    if (now.star_search->active &&
        (now.star_search->active->craft != current ||
         now.star_search->active->started_tick < loss_tick))
      return false;
  }
  // Inspections are append-only personal history. Unlike the editable marker,
  // replacement cannot clear a selection or rewrite an already recorded site.
  const auto same_site = [](const auto& a, const auto& b) {
    return a.system == b.system && a.planet == b.planet && a.site == b.site;
  };
  if (old.surface_landmarks) {
    if (!now.surface_landmarks ||
        now.surface_landmarks->version != old.surface_landmarks->version)
      return false;
    for (const auto& inspection : old.surface_landmarks->inspections) {
      const auto& records = now.surface_landmarks->inspections;
      const auto found = std::ranges::find_if(records, [&](const auto& record) {
        return same_site(record, inspection);
      });
      if (found == records.end() || *found != inspection) return false;
    }
  }
  if (now.surface_landmarks)
    for (const auto& inspection : now.surface_landmarks->inspections) {
      const bool retained =
          old.surface_landmarks &&
          std::ranges::any_of(old.surface_landmarks->inspections,
                              [&](const auto& record) {
                                return same_site(record, inspection);
                              });
      if (!retained && inspection.tick < loss_tick) return false;
    }
  const auto same_moon_site = [](const auto& a, const auto& b) {
    return a.target == b.target && a.site == b.site;
  };
  if (old.moon_surface_landmarks) {
    if (!now.moon_surface_landmarks ||
        (now.moon_surface_landmarks->version !=
             old.moon_surface_landmarks->version &&
         !(old.moon_surface_landmarks->version ==
               kFreedomMoonSurfaceLandmarkVersion &&
           now.moon_surface_landmarks->version ==
               kFreedomNetworkMoonSurfaceLandmarkVersion)))
      return false;
    for (const auto& inspection : old.moon_surface_landmarks->inspections) {
      const auto& records = now.moon_surface_landmarks->inspections;
      const auto found = std::ranges::find_if(records, [&](const auto& record) {
        return same_moon_site(record, inspection);
      });
      if (found == records.end() || *found != inspection) return false;
    }
  }
  if (now.moon_surface_landmarks)
    for (const auto& inspection : now.moon_surface_landmarks->inspections) {
      const bool retained =
          old.moon_surface_landmarks &&
          std::ranges::any_of(old.moon_surface_landmarks->inspections,
                              [&](const auto& record) {
                                return same_moon_site(record, inspection);
                              });
      if (!retained && inspection.tick < loss_tick) return false;
    }
  for (const auto& e : old.entries) {
    auto found = std::find_if(
        now.entries.begin(), now.entries.end(), [&](const auto& n) {
          return n.subject == e.subject && n.fact == e.fact;
        });
    if (found == now.entries.end() ||
        !retains(found->transitions, e.transitions))
      return false;
  }
  for (const auto& entry : now.entries) {
    const auto prior = std::find_if(
        old.entries.begin(), old.entries.end(), [&](const auto& e) {
          return e.subject == entry.subject && e.fact == entry.fact;
        });
    const auto retained_count =
        prior == old.entries.end() ? 0 : prior->transitions.size();
    for (auto i = retained_count; i < entry.transitions.size(); ++i) {
      const auto& transition = entry.transitions[i];
      if (transition.source == KnowledgeSource::starting_chart ||
          transition.flight != current ||
          transition.source_id != current.value || transition.tick < loss_tick)
        return false;
    }
  }
  return true;
}
auto bounded_nesting(std::string_view text) -> bool {
  std::array<char, 64> stack{};
  std::size_t depth{};
  bool quoted{}, escaped{};
  for (char c : text) {
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
    } else if (c == '"')
      quoted = true;
    else if (c == '{' || c == '[') {
      if (depth == stack.size()) return false;
      stack[depth++] = c;
    } else if (c == '}' || c == ']') {
      if (!depth || stack[depth - 1] != (c == '}' ? '{' : '[')) return false;
      --depth;
    }
  }
  return !depth && !quoted;
}
} // namespace

auto recovery_knowledge(const FreedomRecoveryVoyage& v)
    -> const FreedomKnowledgeSaveDocument& {
  if (const auto* k = std::get_if<FreedomKnowledgeSaveDocument>(&v)) return *k;
  return std::get<FreedomTravelSaveDocument>(v).voyage;
}
auto recovery_flight(const FreedomRecoveryVoyage& v)
    -> FreedomFlightSaveDocument {
  return surface_base_flight(recovery_knowledge(v).voyage.voyage.base);
}
auto freedom_recovery_source_checksum(const FreedomRecoveryVoyage& v)
    -> std::expected<std::uint64_t, SaveSchemaError> {
  const auto& flight = recovery_flight(v);
  auto hydrated = hydrate_freedom_flight_document(flight);
  if (!hydrated) return std::unexpected{hydrated.error()};
  const RigidBodyWorldContext context =
      hydrated->minor  ? RigidBodyWorldContext{hydrated->minor->minor}
      : hydrated->moon ? RigidBodyWorldContext{hydrated->moon->moon}
                       : RigidBodyWorldContext{hydrated->system};
  auto checksum = rigid_body_state_checksum(context, flight.flight);
  if (!checksum) return std::unexpected{failure("invalid loss source physics")};
  return *checksum;
}
auto validate_freedom_recovery_document(const FreedomRecoverySaveDocument& d)
    -> std::expected<void, SaveSchemaError> {
  if (auto v = validate_voyage(d.voyage); !v) return v;
  const auto& current = recovery_flight(d.voyage);
  const auto& s = d.recovery;
  if (s.version != kFreedomRecoveryVersion || !starter_selected(d.voyage) ||
      current.model.physical_catalog != 2 ||
      current.model.physical_ephemeris != 2)
    return std::unexpected{
        failure("recovery requires the supported native starter recipe")};
  if (s.checkpoint &&
      (s.checkpoint->version != kFreedomRecoveryVersion ||
       !s.checkpoint->port.station.value || !s.checkpoint->port.ordinal ||
       s.checkpoint->tick > current.flight.tick))
    return std::unexpected{
        failure("invalid checkpoint version, identity or future clock")};
  if (!s.latest) {
    if (s.pending || current.origin.lineage)
      return std::unexpected{
          failure("pending loss/replacement requires retired history")};
    return {};
  }
  const auto& loss = *s.latest;
  if (loss.cause != FreedomLossCause::recoverable_destruction &&
      loss.cause != FreedomLossCause::irrecoverable_destruction)
    return std::unexpected{failure("unknown loss cause")};
  if (auto v = validate_voyage(loss.retired); !v) return v;
  const auto& retired = recovery_flight(loss.retired);
  if (!starter_selected(loss.retired) || retired.model.physical_catalog != 2 ||
      retired.model.physical_ephemeris != 2 ||
      freedom_recovery_source_checksum(loss.retired) != loss.source_checksum ||
      retired.flight.tick > current.flight.tick)
    return std::unexpected{
        failure("invalid retired starter, source checksum or loss clock")};
  if (s.pending) {
    if (d.voyage != loss.retired ||
        retired.flight.tick >= std::numeric_limits<SimulationTick>::max() - 2 ||
        !derive_freedom_craft_identity(retired.origin.recipe.universe_seed,
                                       generation(retired.origin) + 1))
      return std::unexpected{
          failure("pending owner differs from source or cannot install an "
                  "advanceable replacement")};
  } else {
    const auto old_generation = generation(retired.origin);
    const auto& old_resources =
        recovery_knowledge(loss.retired).voyage.resources;
    const auto& current_resources =
        recovery_knowledge(d.voyage).voyage.resources;
    if (old_resources.version != current_resources.version ||
        old_resources.drive.has_value() !=
            current_resources.drive.has_value() ||
        (old_resources.drive && current_resources.drive &&
         old_resources.drive->version != current_resources.drive->version) ||
        (current_resources.drive && current_resources.drive->last_use &&
         (current_resources.drive->last_use->commit_tick <
              retired.flight.tick ||
          current.flight.tick == retired.flight.tick)))
      return std::unexpected{failure("replacement drive selection or new use "
                                     "differs from its own craft history")};
    if (old_generation >= std::numeric_limits<std::uint64_t>::max() - 1 ||
        !current.origin.lineage ||
        generation(current.origin) != old_generation + 1 ||
        current.origin.recipe != retired.origin.recipe ||
        current.origin.network != retired.origin.network ||
        current.origin.derelicts != retired.origin.derelicts ||
        current.origin.state.station != retired.origin.state.station ||
        current.origin.state.craft == retired.origin.state.craft ||
        !retains(current.origin.state.discoveries,
                 retired.origin.state.discoveries) ||
        !retains(current.origin.state.world_deltas,
                 retired.origin.state.world_deltas) ||
        !retains_knowledge(recovery_knowledge(d.voyage).knowledge,
                           recovery_knowledge(loss.retired).knowledge,
                           current.origin.state.craft, retired.flight.tick,
                           current.flight.tick))
      return std::unexpected{
          failure("replacement lineage or retained universe/history differs")};
  }
  return {};
}
auto record_freedom_loss(const FreedomRecoverySaveDocument& d,
                         FreedomLossCause cause, StarterCraftId craft,
                         SimulationTick tick, std::uint64_t checksum)
    -> std::expected<FreedomRecoverySaveDocument, SaveSchemaError> {
  if (auto v = validate_freedom_recovery_document(d); !v)
    return std::unexpected{v.error()};
  if (d.recovery.latest) {
    const auto& previous = *d.recovery.latest;
    const auto& source = recovery_flight(previous.retired);
    if (source.origin.state.craft == craft && source.flight.tick == tick &&
        previous.source_checksum == checksum && previous.cause == cause)
      return d;
  }
  const auto& source = recovery_flight(d.voyage);
  if (d.recovery.pending || source.origin.state.craft != craft ||
      source.flight.tick != tick ||
      tick >= std::numeric_limits<SimulationTick>::max() - 2 ||
      freedom_recovery_source_checksum(d.voyage) != checksum ||
      !derive_freedom_craft_identity(source.origin.recipe.universe_seed,
                                     generation(source.origin) + 1))
    return std::unexpected{
        failure("loss event differs from the active instance/clock/source or "
                "lineage is exhausted")};
  auto result = d;
  result.recovery.latest = FreedomLossRecord{cause, d.voyage, checksum};
  result.recovery.pending = true;
  if (auto v = validate_freedom_recovery_document(result); !v)
    return std::unexpected{v.error()};
  // Refuse an oversized snapshot before committing the loss in memory.
  if (auto encoded = encode_freedom_recovery_document_json(result); !encoded)
    return std::unexpected{encoded.error()};
  return result;
}
auto resolve_freedom_recovery_station(const FreedomRecoverySaveDocument& d)
    -> std::expected<FreedomRecoveryStation, SaveSchemaError> {
  if (auto v = validate_freedom_recovery_document(d); !v)
    return std::unexpected{v.error()};
  const auto& flight = recovery_flight(d.voyage);
  const auto seed = flight.origin.recipe.universe_seed;
  const auto station = generate_origin_station(seed);
  const auto geometry = origin_station_geometry(station);
  auto system = generate_physical_origin_system(seed, 2);
  const OriginPortId port{station.id, 1};
  // Only Origin D1 has an approved walker/entry/service assembly. A stale
  // remembered station is a fallback request, never authority to create it.
  if (!system || !geometry ||
      !release_origin_port(*system, station, *geometry,
                           {geometry->version, port, wayfarer_frame().recipe,
                            flight.flight.tick}))
    return std::unexpected{
        failure("no validated advanceable safe-station replacement pose")};
  return FreedomRecoveryStation{port, !d.recovery.checkpoint ||
                                          d.recovery.checkpoint->port != port};
}
auto complete_freedom_recovery(const FreedomRecoverySaveDocument& d)
    -> std::expected<FreedomRecoverySaveDocument, SaveSchemaError> {
  if (auto v = validate_freedom_recovery_document(d); !v)
    return std::unexpected{v.error()};
  if (!d.recovery.pending) {
    if (d.recovery.latest) return d;
    return std::unexpected{failure("no recorded loss to continue")};
  }
  const auto safe = resolve_freedom_recovery_station(d);
  if (!safe) return std::unexpected{safe.error()};
  const auto& old = recovery_flight(d.voyage);
  const auto seed = old.origin.recipe.universe_seed;
  auto assembly = make_freedom_starting_assembly_new_game_document(seed);
  if (!assembly) return std::unexpected{assembly.error()};
  auto& replacement = assembly->journey.voyage.flight;
  replacement.origin = old.origin;
  replacement.origin.lineage =
      FreedomCraftLineage{1, generation(old.origin) + 1};
  auto identity = derive_freedom_craft_identity(
      seed, replacement.origin.lineage->generation);
  if (!identity) return std::unexpected{identity.error()};
  replacement.origin.state.craft = *identity;
  replacement.model = old.model;
  replacement.model.hold = {};
  const auto station = generate_origin_station(seed);
  const auto geometry = origin_station_geometry(station);
  auto system = generate_physical_origin_system(seed, 2);
  if (!system || !geometry)
    return std::unexpected{failure("safe station unavailable")};
  auto body = release_origin_port(*system, station, *geometry,
                                  {geometry->version, safe->port,
                                   wayfarer_frame().recipe, old.flight.tick});
  if (!body)
    return std::unexpected{failure("safe replacement constraint refused")};
  replacement.flight = *body;
  replacement.world = FreedomActiveWorldSelection{old.origin.network ? 2U : 1U,
                                                  system->catalog.id,
                                                  station.orbit.host_planet};
  assembly->journey.voyage.docking = {geometry->version, safe->port, true};
  FreedomResources resources{1, *identity, body->craft, body->tick};
  if (recovery_knowledge(d.voyage).voyage.resources.drive) {
    const auto& old_ledger = recovery_knowledge(d.voyage).voyage.resources;
    resources.version = old_ledger.version;
    resources.drive = FreedomDriveRecoveryState{
        old_ledger.drive->version, replacement.origin.lineage, {}};
  }
  auto knowledge = recovery_knowledge(d.voyage).knowledge;
  if (knowledge.orbital_surveys) knowledge.orbital_surveys->active.reset();
  if (knowledge.star_search) knowledge.star_search->active.reset();
  if (knowledge.survey_commissions && knowledge.survey_commissions->active &&
      knowledge.survey_commissions->active->measurement &&
      knowledge.survey_commissions->active->measurement->acquired_ticks <
          kSurveyCommissionMeasurementTicks)
    knowledge.survey_commissions->active->measurement.reset();
  if (knowledge.field_samples) {
    knowledge.field_samples->carried.clear();
    knowledge.field_samples->carrier = *identity;
  }
  if (knowledge.recipe.version == kFreedomObservedKnowledgeVersion) {
    knowledge.recipe.version = kFreedomTravelKnowledgeVersion;
    knowledge.recipe.world_domain = 1;
  }
  knowledge.recipe.craft_lineage = replacement.origin.lineage;
  if (knowledge.paid_operations) {
    const auto count = knowledge.survey_commissions
                           ? knowledge.survey_commissions->paid_count
                           : 0U;
    const auto charged = charge_paid_recovery(
        *knowledge.paid_operations, count, replacement.flight.tick,
        replacement.origin.lineage->generation);
    if (!charged) return std::unexpected{failure(charged.error())};
    knowledge.paid_operations = *charged;
  }
  auto result = d;
  // Retired travel/crew/hardware remain history. A fresh craft has no jump
  // commitment, lost hardware, surface anchor or completed pilot entry.
  result.voyage = FreedomKnowledgeSaveDocument{
      {{std::move(*assembly), {}}, resources}, std::move(knowledge)};
  result.recovery.pending = false;
  if (auto v = validate_freedom_recovery_document(result); !v)
    return std::unexpected{v.error()};
  if (auto encoded = encode_freedom_recovery_document_json(result); !encoded)
    return std::unexpected{encoded.error()};
  return result;
}
auto freedom_recovery_explanation(const FreedomRecoverySaveDocument& d)
    -> std::expected<std::string, SaveSchemaError> {
  if (auto v = validate_freedom_recovery_document(d); !v)
    return std::unexpected{v.error()};
  if (!d.recovery.latest) return std::string{"No recorded craft loss."};
  auto station = resolve_freedom_recovery_station(d);
  if (!station) return std::unexpected{station.error()};
  std::string text =
      d.recovery.latest->cause == FreedomLossCause::irrecoverable_destruction
          ? "Craft destroyed; no reachable wreck remains. "
          : "Craft lost; any recoverable wreck is separate from replacement. ";
  text += station->fallback ? "Safe fallback: Origin Station D1. "
                            : "Safe station: Origin Station D1. ";
  text += d.recovery.pending
              ? "Continue with a fresh starter, full flight fuel and three "
                "jump charges. "
              : "Replacement delivered once; the lost craft remains retired. ";
  text += "Your universe, discoveries and world changes are retained. No fee "
          "or wreck retrieval is required.";
  if (const auto& samples = recovery_knowledge(d.recovery.latest->retired)
                                .knowledge.field_samples;
      samples && !samples->carried.empty())
    text += " Carried specimens are lost with this replacement; your "
            "collection notes are retained.";
  return text;
}
auto encode_freedom_recovery_document_json(const FreedomRecoverySaveDocument& d)
    -> std::expected<std::string, SaveSchemaError> {
  if (auto v = validate_freedom_recovery_document(d); !v)
    return std::unexpected{v.error()};
  auto voyage = encode_voyage(d.voyage);
  if (!voyage) return std::unexpected{voyage.error()};
  Json checkpoint = nullptr, loss = nullptr;
  if (d.recovery.checkpoint) {
    const auto& c = *d.recovery.checkpoint;
    checkpoint = {{"version", c.version},
                  {"station", c.port.station.value},
                  {"port", c.port.ordinal},
                  {"tick", c.tick}};
  }
  if (d.recovery.latest) {
    const auto& l = *d.recovery.latest;
    auto retired = encode_voyage(l.retired);
    if (!retired) return std::unexpected{retired.error()};
    loss = {{"cause", static_cast<unsigned>(l.cause)},
            {"retired", Json::parse(*retired)},
            {"source_checksum", l.source_checksum}};
  }
  const Json root{{"format_version", kFreedomRecoverySaveFormatVersion},
                  {"voyage", Json::parse(*voyage)},
                  {"recovery",
                   {{"version", d.recovery.version},
                    {"checkpoint", std::move(checkpoint)},
                    {"latest", std::move(loss)},
                    {"pending", d.recovery.pending}}}};
  auto text = root.dump(2) + '\n';
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "recovery save exceeds limit"}};
  return text;
}
auto decode_freedom_recovery_document_json(std::string_view text)
    -> std::expected<FreedomRecoverySaveDocument, SaveSchemaError> {
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "recovery save exceeds limit"}};
  if (!bounded_nesting(text))
    return std::unexpected{failure("unbalanced or excessive recovery nesting")};
  bool duplicate{};
  std::vector<std::unordered_set<std::string>> keys;
  const Json::parser_callback_t callback = [&](int, Json::parse_event_t e,
                                               Json& value) {
    if (e == Json::parse_event_t::object_start) keys.emplace_back();
    if (e == Json::parse_event_t::key &&
        (keys.empty() || !keys.back().insert(value.get<std::string>()).second))
      duplicate = true;
    if (e == Json::parse_event_t::object_end && !keys.empty()) keys.pop_back();
    return true;
  };
  Json root;
  try {
    root = Json::parse(text.begin(), text.end(), callback);
  } catch (const Json::exception& e) {
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::malformed_json, "$", e.what()}};
  }
  if (duplicate)
    return std::unexpected{SaveSchemaError{SaveSchemaErrorCode::duplicate_key,
                                           "$", "repeated recovery key"}};
  if (!root.is_object() || !uints(root, {"format_version"}))
    return std::unexpected{failure("unsigned recovery format required")};
  if (root["format_version"] != kFreedomRecoverySaveFormatVersion)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::unsupported_format_version,
                        "$.format_version", "unsupported recovery format"}};
  if (!fields(root, {"format_version", "voyage", "recovery"}))
    return std::unexpected{failure("invalid closed recovery wrapper")};
  const auto& s = root["recovery"];
  if (!fields(s, {"version", "checkpoint", "latest", "pending"}) ||
      !uints(s, {"version"}) || s["version"] != kFreedomRecoveryVersion ||
      !s["pending"].is_boolean())
    return std::unexpected{failure("invalid closed recovery state")};
  auto voyage = decode_voyage(root["voyage"]);
  if (!voyage) return std::unexpected{voyage.error()};
  FreedomRecoverySaveDocument result{std::move(*voyage), {}};
  result.recovery.pending = s["pending"].get<bool>();
  const auto& c = s["checkpoint"];
  if (!c.is_null()) {
    if (!fields(c, {"version", "station", "port", "tick"}) ||
        !uints(c, {"version", "station", "port", "tick"}) ||
        c["version"] != kFreedomRecoveryVersion ||
        c["port"] > std::numeric_limits<std::uint32_t>::max())
      return std::unexpected{failure("invalid checkpoint shape or wide port")};
    result.recovery.checkpoint = FreedomSafeStationCheckpoint{
        1,
        {{c["station"].get<std::uint64_t>()}, c["port"].get<std::uint32_t>()},
        c["tick"].get<std::uint64_t>()};
  }
  const auto& l = s["latest"];
  if (!l.is_null()) {
    if (!fields(l, {"cause", "retired", "source_checksum"}) ||
        !uints(l, {"cause", "source_checksum"}) ||
        l["cause"] >
            static_cast<unsigned>(FreedomLossCause::irrecoverable_destruction))
      return std::unexpected{failure("unknown loss cause or record shape")};
    auto retired = decode_voyage(l["retired"]);
    if (!retired) return std::unexpected{retired.error()};
    result.recovery.latest = FreedomLossRecord{
        static_cast<FreedomLossCause>(l["cause"].get<unsigned>()),
        std::move(*retired), l["source_checksum"].get<std::uint64_t>()};
  }
  if (auto v = validate_freedom_recovery_document(result); !v)
    return std::unexpected{v.error()};
  return result;
}
} // namespace apsis_drift
