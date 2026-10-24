#include "apsis_drift/native_flight_session.hpp"

#include "apsis_drift/save_file.hpp"

#include <memory>

namespace apsis_drift {
auto NativeFreedomFlightSession::origin_repair_available() const -> bool {
  if (!condition_ || !docking_ || !docking_->attached || recovery_pending() ||
      moon_ || minor_ || surface_walker_ ||
      (surface_ && (surface_->landed || surface_->moon_landed)) ||
      (travel_ && travel_->phase != FreedomJumpPhase::idle) ||
      (boarding_ && boarding_->phase != FreedomBoardingPhase::station &&
       boarding_->phase != FreedomBoardingPhase::seated) ||
      (condition_->latest && condition_->latest->catastrophic) ||
      document_.flight.craft != wayfarer_frame().recipe)
    return false;
  const FreedomDockingSaveDocument docking{document_, *docking_};
  return validate_freedom_docking_document(docking).has_value() &&
         validate_craft_condition(*condition_, document_).has_value();
}
auto NativeFreedomFlightSession::origin_repair_token() const
    -> std::expected<std::string, std::string> {
  if (!condition_ || !knowledge_ || !resources_)
    return std::unexpected("Current repair owner is unavailable");
  const auto digest = craft_condition_digest(*condition_);
  if (!digest) return std::unexpected(digest.error());
  std::string token = "origin-repair1:" + paid_operations_token() + ':' +
                      std::to_string(*digest);
  const auto word = [&](std::uint64_t value) {
    token += ':';
    token += std::to_string(value);
  };
  word(docking_.has_value());
  if (docking_) {
    word(docking_->geometry_version);
    word(docking_->target.station.value);
    word(docking_->target.ordinal);
    word(docking_->attached);
  }
  word(boarding_.has_value());
  if (boarding_) word(static_cast<std::uint64_t>(boarding_->phase));
  word(actor_.has_value());
  word(surface_walker_.has_value());
  word(travel_ ? static_cast<std::uint64_t>(travel_->phase) : 0);
  return token;
}
auto NativeFreedomFlightSession::origin_repair_view() const
    -> std::expected<NativeOriginRepairView, std::string> {
  NativeOriginRepairView view;
  view.tick = document_.flight.tick;
  view.terms =
      "Immediate Origin station service repairs current forebody and landing "
      "structure damage together, while retaining the ship's impact history. "
      "Technical service is free. If paid operations are selected, the "
      "provisional tariff is 10 credits per severity unit, up to 60 credits. "
      "Paid repair uses cash after current berth charges; operating credit "
      "does not finance it. Review and confirm before any charge.";
  view.note = "No tracked functional damage is available for this craft.";
  if (!condition_ || !knowledge_ || !resources_ ||
      document_.flight.craft != wayfarer_frame().recipe)
    return view;
  if (const auto valid = validate_craft_condition(*condition_, document_);
      !valid)
    return std::unexpected(valid.error());
  view.supported = true;
  view.forebody = condition_->forebody;
  view.landing_structure = condition_->landing_structure;
  view.severity_units = view.forebody + view.landing_structure;
  view.paid_selected = knowledge_->paid_operations.has_value();
  view.repair_credits =
      view.paid_selected ? view.unit_credits * view.severity_units : 0;
  const auto token = origin_repair_token();
  if (!token) return std::unexpected(token.error());
  view.token = *token;
  view.at_origin = origin_repair_available();
  if (view.paid_selected) {
    const auto effective = effective_paid_operations_account();
    if (!effective) return std::unexpected(effective.error());
    const auto cash = paid_operations_cash(*effective, paid_income_counts());
    if (!cash) return std::unexpected(cash.error());
    const auto raw_spent = knowledge_->paid_operations->spent_credits;
    if (effective->spent_credits < raw_spent)
      return std::unexpected("Effective berth spending moved backwards");
    view.berth_credits = effective->spent_credits - raw_spent;
    view.cash_before = view.cash_after = *cash;
  }
  if (recovery_pending() ||
      (condition_->latest && condition_->latest->catastrophic)) {
    view.note = "Recorded craft loss requires recovery, not repair.";
    return view;
  }
  if (!view.at_origin) {
    view.note = "Finish boarding, then attach at Origin for station repair. "
                "The ordinary station walker may also arrange service.";
    return view;
  }
  if (!view.severity_units) {
    view.note = "The Wayfarer has no current functional damage to repair.";
    return view;
  }
  if (view.paid_selected && view.cash_before < view.repair_credits) {
    view.note = "Insufficient cash after berth charges. Repair does not draw "
                "operating credit; no bill is posted by this refusal.";
    return view;
  }
  const auto price = craft_origin_repair_price(*condition_);
  if (!price) return std::unexpected(price.error());
  if (*price != view.unit_credits * view.severity_units)
    return std::unexpected("Condition and station repair tariffs disagree");
  view.can_repair = true;
  if (view.paid_selected) view.cash_after -= view.repair_credits;
  view.note = "Confirm to repair both regions now. The same ship and its "
              "impact history remain yours.";
  return view;
}
auto NativeFreedomFlightSession::repair_at_origin(
    std::string_view reviewed_token) -> std::expected<void, std::string> {
  if (reviewed_token.empty() || reviewed_token.size() > 4096 ||
      reviewed_token.find('\0') != std::string_view::npos)
    return std::unexpected("Repair requires the current bounded quote token");
  const auto quote = origin_repair_view();
  if (!quote) return std::unexpected(quote.error());
  if (!quote->can_repair) return std::unexpected(quote->note);
  if (quote->token != reviewed_token)
    return std::unexpected("Repair quote changed; review the current terms");
  auto candidate = std::make_unique<NativeFreedomFlightSession>(*this);
  CraftRepairPayment payment;
  if (quote->paid_selected) {
    const auto purchased = purchase_paid_repair(
        *candidate->knowledge_->paid_operations,
        candidate->paid_income_counts(), document_.flight.tick,
        candidate->paid_operations_generation(), quote->severity_units);
    if (!purchased) return std::unexpected(purchased.error());
    const auto cash =
        paid_operations_cash(*purchased, candidate->paid_income_counts());
    if (!cash || *cash != quote->cash_after)
      return std::unexpected("Current repair debit differs from its quote");
    candidate->knowledge_->paid_operations = *purchased;
    payment = {true, purchased->started_tick, purchased->spent_credits};
  }
  const FreedomDockingSaveDocument docking{candidate->document_,
                                           *candidate->docking_};
  const auto repaired =
      repair_craft_condition(*candidate->condition_, docking, payment);
  if (!repaired) return std::unexpected(repaired.error());
  candidate->condition_ = *repaired;
  const auto saved = candidate->save_document();
  if (!saved) return std::unexpected(saved.error());
  if (const auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected(encoded.error().detail);
  *this = std::move(*candidate);
  return {};
}
} // namespace apsis_drift
