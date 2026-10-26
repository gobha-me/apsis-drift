#include "apsis_drift/native_flight_session.hpp"

#include "apsis_drift/freedom_jump_targeting.hpp"
#include "apsis_drift/freedom_system_network.hpp"

#include "apsis_drift/save_file.hpp"

#include <algorithm>

namespace apsis_drift {
namespace {
auto known_name(const FreedomKnowledge& knowledge, KnowledgeSubject subject,
                SimulationTick tick) -> std::string {
  const auto identity = query_freedom_knowledge(
      knowledge, subject, KnowledgeFact::identity_type, tick);
  const auto location = query_freedom_knowledge(knowledge, subject,
                                                KnowledgeFact::location, tick);
  if (!identity || !*identity || !location || !*location ||
      (**identity).provenance.level < NavigationKnowledgeLevel::resolved ||
      (**location).provenance.level < NavigationKnowledgeLevel::resolved)
    return {};
  if (const auto* named = std::get_if<KnownIdentity>(&(**identity).value))
    return named->name;
  return {};
}
auto target_name(const FreedomKnowledge& knowledge, SystemId system,
                 PlanetId planet, SimulationTick tick) -> std::string {
  return known_name(knowledge,
                    {KnowledgeSubjectKind::planet, system, planet.value}, tick);
}
} // namespace

auto NativeFreedomFlightSession::survey_commission_offers() const
    -> std::expected<NativeSurveyCommissionOffersView, std::string> {
  NativeSurveyCommissionOffersView view;
  view.tick = document_.flight.tick;
  view.note = "Select identified charted orbital work at Origin. Existing work "
              "must be abandoned before changing destination.";
  if (!knowledge_ || !resources_ || !document_.origin.network ||
      !freedom_knowledge_has_planetary_chart(knowledge_->recipe.version))
    return view;
  if (!validate_freedom_knowledge(*knowledge_, view.tick))
    return std::unexpected{"Commission knowledge is invalid"};
  const auto network = generate_freedom_system_network(
      document_.origin.recipe.universe_seed, document_.origin.network->version);
  if (!network || network->nodes.size() < 3)
    return std::unexpected{"Commission network is unavailable"};
  for (const auto ordinal : {1U, 2U}) {
    const auto system = network->nodes[ordinal].system;
    const auto system_name = known_name(
        *knowledge_, {KnowledgeSubjectKind::system, system, system.value},
        view.tick);
    if (system_name.empty()) continue;
    const auto physical = resolve_freedom_network_system(
        *network, system, knowledge_->recipe.physical_catalog);
    if (!physical || physical->catalog.planets.empty())
      return std::unexpected{"Commission body is unavailable"};
    const auto planet = physical->catalog.planets.front().descriptor.id;
    const auto body_name = target_name(*knowledge_, system, planet, view.tick);
    if (body_name.empty()) continue;
    const auto route = commission_return_charge_view(system);
    if (!route) return std::unexpected{route.error()};
    if (!route->available || route->leg_count != 2) continue;
    view.offers[view.offer_count++] = {BodyTarget{system, body_id(planet)},
                                       system_name, body_name, *route};
  }
  return view;
}

auto NativeFreedomFlightSession::survey_commission_view(
    SurveyCommissionKind requested_kind,
    std::optional<BodyTarget> requested_target) const
    -> std::expected<NativeSurveyCommissionView, std::string> {
  if (requested_kind != SurveyCommissionKind::orbital &&
      requested_kind != SurveyCommissionKind::geological)
    return std::unexpected{"Unknown commission kind"};
  NativeSurveyCommissionView view;
  view.kind = requested_kind;
  view.tick = document_.flight.tick;
  view.note = "Survey commissions require a planetary-chart journey. "
              "Existing flight and free station service remain available.";
  if (!knowledge_ ||
      !freedom_knowledge_has_planetary_chart(knowledge_->recipe.version))
    return view;
  const auto ids = generate_first_intersystem_identities(
      document_.origin.recipe.universe_seed);
  const auto account =
      knowledge_->survey_commissions.value_or(SurveyCommissionAccount{});
  if (auto valid = validate_survey_commissions(account, view.tick); !valid)
    return std::unexpected{valid.error()};
  if (requested_target &&
      (requested_kind != SurveyCommissionKind::orbital ||
       (account.active &&
        account.active->kind != SurveyCommissionKind::orbital)))
    return std::unexpected{"Explicit destinations apply only to orbital work"};
  if (account.active) view.kind = account.active->kind;
  if (account.active) {
    view.system = account.active->system;
    view.planet = account.active->planet;
    if (requested_target &&
        *requested_target != BodyTarget{view.system, body_id(view.planet)})
      return std::unexpected{
          "Abandon existing work before changing destination"};
  } else if (requested_target) {
    const auto offers = survey_commission_offers();
    if (!offers) return std::unexpected{offers.error()};
    bool admitted = false;
    for (std::uint32_t i = 0; i < offers->offer_count; ++i)
      if (offers->offers[i].target == *requested_target) admitted = true;
    if (!admitted)
      return std::unexpected{
          "Select an admitted charted commission destination"};
    view.system = requested_target->system;
    view.planet = PlanetId{requested_target->body.value};
  } else {
    const auto target =
        view.kind == SurveyCommissionKind::geological
            ? generate_physical_origin_system(
                  document_.origin.recipe.universe_seed,
                  knowledge_->recipe.physical_catalog)
            : generate_physical_local_system(
                  ids.target_system_seed, knowledge_->recipe.physical_catalog);
    if (!target || target->catalog.planets.empty())
      return std::unexpected{"Commission destination is unavailable"};
    view.system = target->catalog.id;
    view.planet = target->catalog.planets.front().descriptor.id;
  }
  view.body_name =
      target_name(*knowledge_, view.system, view.planet, view.tick);
  if (view.body_name.empty()) {
    view.note =
        "No identified, charted remote measurement target is available.";
    return view;
  }
  view.supported = true;
  view.selected = knowledge_->survey_commissions.has_value();
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
    if (job.kind == SurveyCommissionKind::geological &&
        knowledge_->field_samples &&
        knowledge_->field_samples->carrier == document_.origin.state.craft) {
      const auto& items = knowledge_->field_samples->carried;
      for (std::size_t index = 0; index < items.size(); ++index) {
        if (!apsis_drift::submit_geological_survey_commission(
                account, job.sequence, view.tick, items[index]))
          continue;
        view.completed = true;
        std::string label;
        switch (items[index].kind) {
          case SurfaceLandmarkKind::pale_exposure:
            label = "Pale specimen";
            break;
          case SurfaceLandmarkKind::ochre_exposure:
            label = "Ochre specimen";
            break;
          case SurfaceLandmarkKind::dark_exposure:
            label = "Dark specimen";
            break;
        }
        view.specimen = "Pouch slot " + std::to_string(index + 1) + ": " +
                        label + " - " + view.body_name;
        view.submission_token = geological_commission_token();
        break;
      }
    }
    view.can_abandon = view.at_origin;
    view.can_submit = view.at_origin && view.completed && !view.expired;
    if (view.expired)
      view.note = "Submission deadline passed; this commission pays nothing. "
                  "At Origin, renew this destination or abandon it before "
                  "choosing different work. "
                  "Your exploration observations are retained.";
    else if (job.kind == SurveyCommissionKind::geological)
      view.note = view.completed
                      ? "Fresh specimen carried. Return to Origin and confirm "
                        "delivery of the named pouch item before the deadline. "
                        "Discovery notes and other specimens remain."
                      : "Collect a new specimen at a geological exposure on "
                        "the named planet after acceptance. An earlier sample "
                        "or a field note cannot fulfill this request.";
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
  } else if (view.kind == SurveyCommissionKind::geological) {
    view.note = "Standing request: collect one new physical specimen on the "
                "named planet, then deliver it at Origin. Reward: 100 "
                "prototype credits; reservation: four game hours. Delivery "
                "consumes one specimen and retains its discovery note.";
  } else {
    view.note = "Standing request: record 30 new seconds in a stable orbit "
                "at this remote planet, then return to Origin and submit. "
                "Reward: 100 prototype credits; reservation: four game hours.";
    if (requested_target && view.system != ids.target_system)
      view.note =
          "Standing request at " + view.body_name +
          ": record 30 new seconds in a stable orbit, then return to Origin "
          "and submit. "
          "Reward: 100 prototype credits; reservation: four game hours.";
  }
  if (view.at_origin && offer_until)
    view.can_accept = (view.kind == SurveyCommissionKind::geological
                           ? apsis_drift::accept_geological_survey_commission(
                                 account, view.system, view.planet, view.tick)
                       : view.system != ids.target_system
                           ? apsis_drift::accept_targeted_survey_commission(
                                 account, view.system, view.planet, view.tick)
                           : apsis_drift::accept_survey_commission(
                                 account, view.system, view.planet, view.tick))
                          .has_value();
  return view;
}

auto NativeFreedomFlightSession::commit_survey_commission_account(
    SurveyCommissionAccount account,
    const std::optional<FreedomFieldSampleRecord>& delivered)
    -> std::expected<void, std::string> {
  auto candidate = *this;
  if (delivered) {
    if (!candidate.knowledge_->field_samples ||
        candidate.knowledge_->field_samples->carrier !=
            document_.origin.state.craft)
      return std::unexpected{"A physical specimen must belong to this carrier"};
    auto& carried = candidate.knowledge_->field_samples->carried;
    const auto item = std::find(carried.begin(), carried.end(), *delivered);
    if (item == carried.end())
      return std::unexpected{"The reviewed specimen is no longer carried"};
    carried.erase(item);
  }
  candidate.knowledge_->survey_commissions = account;
  if (knowledge_->paid_operations) {
    auto counts = paid_income_counts();
    counts.survey_paid = account.paid_count;
    const auto paid =
        account.paid_count != survey_paid_count()
            ? settle_paid_survey_income(*knowledge_->paid_operations, counts,
                                        document_.flight.tick,
                                        paid_operations_generation())
            : reconcile_paid_berth(*knowledge_->paid_operations, counts,
                                   document_.flight.tick,
                                   paid_operations_generation());
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
  if (!knowledge_) return std::unexpected{"No selected commission knowledge"};
  const auto ids = generate_first_intersystem_identities(
      document_.origin.recipe.universe_seed);
  const auto nearby = generate_physical_local_system(
      ids.target_system_seed, knowledge_->recipe.physical_catalog);
  if (!nearby || nearby->catalog.planets.empty())
    return std::unexpected{"Nearby commission target is unavailable"};
  const auto view = survey_commission_view(
      SurveyCommissionKind::orbital,
      BodyTarget{nearby->catalog.id,
                 body_id(nearby->catalog.planets.front().descriptor.id)});
  if (!view) return std::unexpected{view.error()};
  if (view->kind != SurveyCommissionKind::orbital || !view->can_accept ||
      offer_until != view->offer_until)
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

auto NativeFreedomFlightSession::accept_survey_commission_target(
    BodyTarget target, SimulationTick offer_until, std::uint32_t sequence)
    -> std::expected<void, std::string> {
  const auto view =
      survey_commission_view(SurveyCommissionKind::orbital, target);
  if (!view) return std::unexpected{view.error()};
  if (view->kind != SurveyCommissionKind::orbital || !view->can_accept ||
      view->offer_until != offer_until || view->offer_sequence != sequence ||
      BodyTarget{view->system, body_id(view->planet)} != target)
    return std::unexpected{
        "Review current target-bound orbital terms at Origin"};
  const auto account =
      knowledge_->survey_commissions.value_or(SurveyCommissionAccount{});
  const auto ids = generate_first_intersystem_identities(
      document_.origin.recipe.universe_seed);
  auto next = target.system == ids.target_system
                  ? apsis_drift::accept_survey_commission(
                        account, view->system, view->planet, view->tick)
                  : apsis_drift::accept_targeted_survey_commission(
                        account, view->system, view->planet, view->tick);
  if (!next) return std::unexpected{next.error()};
  return commit_survey_commission_account(*next);
}

auto NativeFreedomFlightSession::submit_survey_commission(
    std::uint32_t sequence) -> std::expected<void, std::string> {
  const auto view = survey_commission_view();
  if (!view) return std::unexpected{view.error()};
  if (view->kind != SurveyCommissionKind::orbital || !view->can_submit ||
      !knowledge_->survey_commissions)
    return std::unexpected{"A completed, unexpired report must be submitted "
                           "while attached at Origin"};
  auto next = apsis_drift::submit_survey_commission(
      *knowledge_->survey_commissions, sequence, document_.flight.tick);
  if (!next) return std::unexpected{next.error()};
  return commit_survey_commission_account(*next);
}

auto NativeFreedomFlightSession::geological_commission_token() const
    -> std::string {
  std::string token = "geo1:" + specimen_sale_token();
  if (!knowledge_ || !knowledge_->survey_commissions) return token;
  const auto& account = *knowledge_->survey_commissions;
  const auto word = [&](auto value) {
    token += ':';
    token += std::to_string(value);
  };
  word(account.version);
  word(account.next_sequence);
  word(account.paid_count);
  word(account.geological_paid_count);
  if (account.active) {
    const auto& job = *account.active;
    word(static_cast<unsigned>(job.kind));
    word(job.sequence);
    word(job.system.value);
    word(job.planet.value);
    word(job.accepted_tick);
    word(job.deadline_tick);
  }
  return token;
}

auto NativeFreedomFlightSession::accept_geological_survey_commission(
    SimulationTick offer_until, std::uint32_t sequence)
    -> std::expected<void, std::string> {
  const auto view = survey_commission_view(SurveyCommissionKind::geological);
  if (!view) return std::unexpected{view.error()};
  if (view->kind != SurveyCommissionKind::geological || !view->can_accept ||
      offer_until != view->offer_until)
    return std::unexpected{"Review a current geological offer at Origin"};
  const auto account =
      knowledge_->survey_commissions.value_or(SurveyCommissionAccount{});
  if (sequence != account.next_sequence)
    return std::unexpected{"The displayed geological offer is stale"};
  const auto next = apsis_drift::accept_geological_survey_commission(
      account, view->system, view->planet, document_.flight.tick);
  if (!next) return std::unexpected{next.error()};
  return commit_survey_commission_account(*next);
}

auto NativeFreedomFlightSession::submit_geological_survey_commission(
    std::string_view token) -> std::expected<void, std::string> {
  const auto view = survey_commission_view(SurveyCommissionKind::geological);
  if (!view) return std::unexpected{view.error()};
  if (view->kind != SurveyCommissionKind::geological || !view->can_submit ||
      token.empty() || token != view->submission_token ||
      !knowledge_->survey_commissions || !knowledge_->field_samples)
    return std::unexpected{
        "Review the current carried-specimen delivery at Origin"};
  const auto& account = *knowledge_->survey_commissions;
  for (const auto& item : knowledge_->field_samples->carried) {
    const auto next = apsis_drift::submit_geological_survey_commission(
        account, view->sequence, document_.flight.tick, item);
    if (next) return commit_survey_commission_account(*next, item);
  }
  return std::unexpected{"The reviewed fresh specimen is no longer available"};
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
  if (job.kind != SurveyCommissionKind::orbital) return {};
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
