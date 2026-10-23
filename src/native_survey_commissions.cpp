#include "apsis_drift/native_flight_session.hpp"

#include "apsis_drift/save_file.hpp"

namespace apsis_drift {
namespace {
auto target_name(const FreedomKnowledge& knowledge, SystemId system,
                 PlanetId planet, SimulationTick tick) -> std::string {
  const auto identity = query_freedom_knowledge(
      knowledge, {KnowledgeSubjectKind::planet, system, planet.value},
      KnowledgeFact::identity_type, tick);
  const auto location = query_freedom_knowledge(
      knowledge, {KnowledgeSubjectKind::planet, system, planet.value},
      KnowledgeFact::location, tick);
  if (!identity || !*identity || !location || !*location) return {};
  if (const auto* named = std::get_if<KnownIdentity>(&(**identity).value))
    return named->name;
  return {};
}
} // namespace

auto NativeFreedomFlightSession::survey_commission_view() const
    -> std::expected<NativeSurveyCommissionView, std::string> {
  NativeSurveyCommissionView view;
  view.tick = document_.flight.tick;
  view.note = "Survey commissions require a planetary-chart journey. "
              "Existing flight and free station service remain available.";
  if (!knowledge_ ||
      !freedom_knowledge_has_planetary_chart(knowledge_->recipe.version))
    return view;
  const auto ids = generate_first_intersystem_identities(
      document_.origin.recipe.universe_seed);
  const auto target = generate_physical_local_system(
      ids.target_system_seed, knowledge_->recipe.physical_catalog);
  if (!target || target->catalog.planets.empty())
    return std::unexpected{"Commission destination is unavailable"};
  view.system = target->catalog.id;
  view.planet = target->catalog.planets.front().descriptor.id;
  view.body_name =
      target_name(*knowledge_, view.system, view.planet, view.tick);
  if (view.body_name.empty()) {
    view.note =
        "No identified, charted remote measurement target is available.";
    return view;
  }
  view.supported = true;
  view.selected = knowledge_->survey_commissions.has_value();
  const auto account =
      knowledge_->survey_commissions.value_or(SurveyCommissionAccount{});
  if (auto valid = validate_survey_commissions(account, view.tick); !valid)
    return std::unexpected{valid.error()};
  view.earnings = survey_commission_earnings(account);
  view.at_origin =
      !recovery_pending() && docking_ && docking_->attached && !moon_ &&
      !minor_ && system_.catalog.id == ids.origin_system &&
      (!boarding_ || boarding_->phase == FreedomBoardingPhase::station ||
       boarding_->phase == FreedomBoardingPhase::seated);
  const auto offer_until = survey_commission_offer_deadline(view.tick);
  if (offer_until) view.offer_until = *offer_until;
  view.offer_sequence = account.next_sequence;
  if (account.latest_paid) {
    view.last_payment =
        "Commission " +
        std::to_string(account.latest_paid->commission.sequence) +
        " paid 100 credits at ship tick " +
        std::to_string(account.latest_paid->submitted_tick) + ".";
  }
  if (account.active) {
    const auto& job = *account.active;
    view.sequence = job.sequence;
    view.accepted_tick = job.accepted_tick;
    view.deadline_tick = job.deadline_tick;
    view.expired = view.tick >= job.deadline_tick;
    if (job.measurement) view.acquired_ticks = job.measurement->acquired_ticks;
    view.completed = view.acquired_ticks == kSurveyCommissionMeasurementTicks;
    view.can_abandon = view.at_origin;
    view.can_submit = view.at_origin && view.completed && !view.expired;
    if (view.expired)
      view.note = "Submission deadline passed; this commission pays nothing. "
                  "At Origin, explicitly replace it with a new commission. "
                  "Your exploration observations are retained.";
    else if (view.completed)
      view.note = "Fresh measurement complete. Return to Origin, attach to "
                  "a port and explicitly submit before the deadline.";
    else if (system_.catalog.id != job.system ||
             rotation_.rotation.planet != job.planet || moon_ || minor_)
      view.note = "Travel to the accepted planet. The scanner acquires only "
                  "new qualifying flight intervals there.";
    else if (auto ready = orbital_measurement_conditions(); !ready)
      view.note = ready.error();
    else
      view.note = "Acquiring fresh orbital reference measurements. Maintain "
                  "a stable orbit above the atmosphere, within two radii.";
  } else {
    view.note = "Standing request: record 30 new seconds in a stable orbit "
                "at this remote planet, then return to Origin and submit. "
                "Reward: 100 prototype credits; reservation: four game hours.";
  }
  if (view.at_origin && offer_until)
    view.can_accept = apsis_drift::accept_survey_commission(
                          account, view.system, view.planet, view.tick)
                          .has_value();
  return view;
}

auto NativeFreedomFlightSession::commit_survey_commission_account(
    SurveyCommissionAccount account) -> std::expected<void, std::string> {
  auto candidate = *this;
  candidate.knowledge_->survey_commissions = account;
  if (knowledge_->paid_operations) {
    const auto paid =
        account.paid_count != survey_paid_count()
            ? settle_paid_survey_income(
                  *knowledge_->paid_operations,
                  PaidIncomeCounts{account.paid_count,
                                   paid_income_counts().specimen_sold},
                  document_.flight.tick, paid_operations_generation())
            : reconcile_paid_berth(
                  *knowledge_->paid_operations,
                  PaidIncomeCounts{account.paid_count,
                                   paid_income_counts().specimen_sold},
                  document_.flight.tick, paid_operations_generation());
    if (!paid) return std::unexpected{paid.error()};
    candidate.knowledge_->paid_operations = *paid;
  }
  const auto saved = candidate.save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return {};
}

auto NativeFreedomFlightSession::accept_survey_commission(
    SimulationTick offer_until, std::uint32_t sequence)
    -> std::expected<void, std::string> {
  const auto view = survey_commission_view();
  if (!view) return std::unexpected{view.error()};
  if (!view->can_accept || offer_until != view->offer_until)
    return std::unexpected{"Review a current offer while attached at Origin"};
  const auto account =
      knowledge_->survey_commissions.value_or(SurveyCommissionAccount{});
  if (sequence != account.next_sequence)
    return std::unexpected{"The displayed commission offer is stale"};
  auto next = apsis_drift::accept_survey_commission(
      account, view->system, view->planet, document_.flight.tick);
  if (!next) return std::unexpected{next.error()};
  return commit_survey_commission_account(*next);
}

auto NativeFreedomFlightSession::submit_survey_commission(
    std::uint32_t sequence) -> std::expected<void, std::string> {
  const auto view = survey_commission_view();
  if (!view) return std::unexpected{view.error()};
  if (!view->can_submit || !knowledge_->survey_commissions)
    return std::unexpected{"A completed, unexpired report must be submitted "
                           "while attached at Origin"};
  auto next = apsis_drift::submit_survey_commission(
      *knowledge_->survey_commissions, sequence, document_.flight.tick);
  if (!next) return std::unexpected{next.error()};
  return commit_survey_commission_account(*next);
}

auto NativeFreedomFlightSession::abandon_survey_commission(
    std::uint32_t sequence) -> std::expected<void, std::string> {
  const auto view = survey_commission_view();
  if (!view) return std::unexpected{view.error()};
  if (!view->can_abandon || !knowledge_->survey_commissions)
    return std::unexpected{"Abandon the accepted commission at Origin"};
  auto next = apsis_drift::abandon_survey_commission(
      *knowledge_->survey_commissions, sequence, document_.flight.tick);
  if (!next) return std::unexpected{next.error()};
  return commit_survey_commission_account(*next);
}

auto NativeFreedomFlightSession::advance_survey_commission_tick()
    -> std::expected<void, std::string> {
  if (recovery_pending() || !knowledge_ || !knowledge_->survey_commissions ||
      !knowledge_->survey_commissions->active)
    return {};
  const auto& job = *knowledge_->survey_commissions->active;
  const bool qualifies = !moon_ && !minor_ &&
                         system_.catalog.id == job.system &&
                         rotation_.rotation.planet == job.planet &&
                         orbital_measurement_conditions().has_value();
  auto next = apsis_drift::advance_survey_commission(
      *knowledge_->survey_commissions, document_.origin.state.craft,
      document_.flight.tick, qualifies);
  if (!next) return std::unexpected{next.error()};
  knowledge_->survey_commissions = *next;
  return {};
}
} // namespace apsis_drift
