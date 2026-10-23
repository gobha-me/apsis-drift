#include "apsis_drift/native_flight_session.hpp"

#include "apsis_drift/save_file.hpp"

#include <limits>
#include <memory>

namespace apsis_drift {
static_assert(kFreedomSpecimenSaleCredits == kPaidSpecimenCredits);
static_assert(kFreedomFieldSampleCapacity == kPaidSpecimenBatchLimit);
namespace {
auto specimen_label(SurfaceLandmarkKind kind) -> std::string_view {
  switch (kind) {
    case SurfaceLandmarkKind::pale_exposure: return "Pale specimen";
    case SurfaceLandmarkKind::ochre_exposure: return "Ochre specimen";
    case SurfaceLandmarkKind::dark_exposure: return "Dark specimen";
  }
  return "Specimen";
}
} // namespace

auto NativeFreedomFlightSession::specimen_sale_token() const -> std::string {
  std::string token = "sale1:" + paid_operations_token();
  const auto word = [&](auto value) {
    token += ':';
    token += std::to_string(value);
  };
  if (!knowledge_ || !knowledge_->field_samples) {
    word(0);
    return token;
  }
  const auto& pouch = *knowledge_->field_samples;
  word(pouch.version);
  word(pouch.carrier.value);
  word(pouch.carried.size());
  for (const auto& item : pouch.carried) {
    word(item.target.system.value);
    word(static_cast<unsigned>(item.target.body.kind));
    word(item.target.body.value);
    word(item.cell.x);
    word(item.cell.y);
    word(item.cell.z);
    word(item.site.value);
    word(item.terrain_recipe);
    word(static_cast<unsigned>(item.kind));
    word(item.craft.value);
    word(item.tick);
  }
  return token;
}

auto NativeFreedomFlightSession::specimen_sale_view() const
    -> std::expected<NativeSpecimenSaleView, std::string> {
  NativeSpecimenSaleView view;
  view.tick = document_.flight.tick;
  view.capacity = static_cast<std::uint32_t>(kFreedomFieldSampleCapacity);
  view.unit_credits = kFreedomSpecimenSaleCredits;
  view.note = "The specimen buyer requires a planetary-chart Wayfarer journey.";
  if (!knowledge_ || !resources_ ||
      !freedom_knowledge_has_planetary_chart(knowledge_->recipe.version) ||
      document_.flight.craft != CraftFrameRecipe{kWayfarerFrameId, 1})
    return view;
  view.supported = true;
  view.at_origin = paid_operations_origin_available();
  view.paid_selected = knowledge_->paid_operations.has_value();
  view.earned_credits =
      static_cast<std::uint64_t>(paid_income_counts().specimen_sold) *
      kFreedomSpecimenSaleCredits;
  view.token = specimen_sale_token();
  if (knowledge_->field_samples) {
    const auto& samples = knowledge_->field_samples->carried;
    if (samples.size() > kFreedomFieldSampleCapacity)
      return std::unexpected{"Specimen pouch exceeds the starter capacity"};
    view.carried_count = static_cast<std::uint32_t>(samples.size());
    for (const auto& item : samples) {
      std::string source =
          item.target.body.kind == BodyKind::moon ? "Moon" : "Planet";
      if (item.target.body.kind == BodyKind::planet) {
        const auto known = query_freedom_knowledge(
            *knowledge_,
            {KnowledgeSubjectKind::planet, item.target.system,
             item.target.body.value},
            KnowledgeFact::identity_type, view.tick);
        if (known && *known &&
            std::holds_alternative<KnownIdentity>((*known)->value))
          source = std::get<KnownIdentity>((*known)->value).name;
      }
      if (!view.contents.empty()) view.contents += '\n';
      view.contents += std::string{specimen_label(item.kind)} + " - " + source;
    }
  }
  view.gross_credits = view.carried_count * view.unit_credits;
  view.net_credits = view.gross_credits;
  if (!view.at_origin) {
    view.note =
        "Return and physically attach at Origin to sell carried specimens. "
        "Field notes alone have no sale value.";
    return view;
  }
  if (!view.carried_count) {
    view.note =
        "No physical specimens are carried. Inspect an exposure and collect "
        "within eight metres on supported ground. Discarded or lost specimens "
        "cannot be sold from their notes.";
    return view;
  }
  auto counts = paid_income_counts();
  if (view.tick == std::numeric_limits<SimulationTick>::max() ||
      view.carried_count >
          std::numeric_limits<std::uint32_t>::max() - counts.specimen_sold) {
    view.note = "The buyer cannot process another sale in this journey; "
                "your specimens remain in the pouch.";
    return view;
  }
  counts.specimen_sold += view.carried_count;
  if (knowledge_->paid_operations) {
    const auto before = effective_paid_operations_account();
    if (!before) return std::unexpected{before.error()};
    const auto after = settle_paid_specimen_income(
        *knowledge_->paid_operations, counts, view.tick,
        paid_operations_generation(), view.carried_count);
    if (!after) return std::unexpected{after.error()};
    view.insurance_credits = before->recovery_due - after->recovery_due;
    view.tab_credits = before->operating_tab - after->operating_tab;
    if (view.insurance_credits + view.tab_credits > view.gross_credits)
      return std::unexpected{"Specimen income deductions exceed the quote"};
    view.net_credits =
        view.gross_credits - view.insurance_credits - view.tab_credits;
    const auto cash = paid_operations_cash(*after, counts);
    if (!cash) return std::unexpected{cash.error()};
    view.cash_after = *cash;
  }
  view.can_sell = true;
  view.note =
      "Provisional buyer: 40 credits per carried specimen, with no assay "
      "or material rarity price. Confirm to sell the entire pouch and "
      "retain its field notes. Paid income settles insurance first, then "
      "up to half the remainder toward the operating tab. No sale happens "
      "on docking, reading or Continue.";
  return view;
}

auto NativeFreedomFlightSession::sell_specimens(std::string_view token)
    -> std::expected<void, std::string> {
  const auto quote = specimen_sale_view();
  if (!quote) return std::unexpected{quote.error()};
  if (!quote->can_sell || token != quote->token)
    return std::unexpected{"Review the current specimen quote at Origin"};
  const auto& source = *knowledge_->field_samples;
  if (source.carrier != document_.origin.state.craft)
    return std::unexpected{"The pouch belongs to a different carrier"};
  auto candidate = std::make_unique<NativeFreedomFlightSession>(*this);
  auto counts = paid_income_counts();
  counts.specimen_sold += quote->carried_count;
  candidate->knowledge_->specimen_sales = FreedomSpecimenSalesAccount{
      kFreedomSpecimenSalesVersion,
      counts.specimen_sold,
      {document_.origin.state.craft, document_.flight.tick, source.carried}};
  candidate->knowledge_->field_samples->carried.clear();
  if (knowledge_->paid_operations) {
    const auto paid = settle_paid_specimen_income(
        *knowledge_->paid_operations, counts, document_.flight.tick,
        paid_operations_generation(), quote->carried_count);
    if (!paid) return std::unexpected{paid.error()};
    candidate->knowledge_->paid_operations = *paid;
  }
  const auto saved = candidate->save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(*candidate);
  return {};
}
} // namespace apsis_drift
