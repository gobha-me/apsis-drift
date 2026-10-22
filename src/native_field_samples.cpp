#include "apsis_drift/moon_surface_walker.hpp"
#include "apsis_drift/native_flight_session.hpp"

#include <algorithm>
#include <tuple>

namespace apsis_drift {
namespace {
auto same_site(const FreedomFieldSampleRecord& record, BodyTarget target,
               SurfaceLandmarkId site) -> bool {
  return record.target == target && record.site == site;
}
auto sample_key(const FreedomFieldSampleRecord& record) {
  return std::tuple{record.target.system.value,
                    static_cast<unsigned>(record.target.body.kind),
                    record.target.body.value, record.site.value};
}
} // namespace

auto NativeFreedomFlightSession::field_sample_collection_source(
    SurfaceLandmarkId id) const
    -> std::expected<FreedomFieldSampleRecord, std::string> {
  if (recovery_pending() || !knowledge_ || !resources_ || !surface_walker_ ||
      !surface_ ||
      !validate_freedom_knowledge(*knowledge_, document_.flight.tick))
    return std::unexpected{
        "Collecting a specimen requires the supported outside actor"};
  FreedomFieldSampleRecord record;
  record.craft = document_.origin.state.craft;
  record.tick = document_.flight.tick;
  if (moon_) {
    if (!document_.moon || !surface_->moon_landed || !moon_walk_terrain_ ||
        moon_walk_terrain_->anchor() != *surface_->moon_landed ||
        !knowledge_->moon_surface_landmarks)
      return std::unexpected{
          "Moon specimen support or inspection owner is unavailable"};
    if (auto valid = moon_walk_terrain_->validate(*surface_walker_); !valid)
      return std::unexpected{valid.error()};
    const auto view = moon_surface_landmarks_view();
    if (!view) return std::unexpected{view.error()};
    const auto found = std::ranges::find_if(view->sites, [&](const auto& item) {
      return item.site.identity.id == id;
    });
    if (found == view->sites.end() || !found->can_inspect)
      return std::unexpected{
          "Move within 8 metres of the exposure centre to collect"};
    record.target = {system_.catalog.id, moon_->body};
    record.cell = found->site.identity.cell;
    record.site = found->site.identity.id;
    record.terrain_recipe = document_.moon->terrain_recipe;
    record.kind = found->site.identity.kind;
    const bool inspected = std::ranges::any_of(
        knowledge_->moon_surface_landmarks->inspections, [&](const auto& item) {
          return item.target == record.target && item.site == id &&
                 item.cell == record.cell &&
                 item.terrain_recipe == record.terrain_recipe &&
                 item.tick <= record.tick;
        });
    if (!inspected)
      return std::unexpected{
          "Inspect this exposure before collecting a specimen"};
  } else {
    if (minor_ || !surface_->landed || !surface_walk_terrain_ ||
        surface_walk_terrain_->anchor() != *surface_->landed ||
        !knowledge_->surface_landmarks)
      return std::unexpected{
          "Planet specimen support or inspection owner is unavailable"};
    if (auto valid = surface_walk_terrain_->validate(*surface_walker_); !valid)
      return std::unexpected{valid.error()};
    const auto view = surface_landmarks_view();
    if (!view) return std::unexpected{view.error()};
    const auto found = std::ranges::find_if(view->sites, [&](const auto& item) {
      return item.site.identity.id == id;
    });
    if (found == view->sites.end() || !found->can_inspect)
      return std::unexpected{
          "Move within 8 metres of the exposure centre to collect"};
    record.target = {system_.catalog.id, body_id(rotation_.rotation.planet)};
    record.cell = found->site.identity.cell;
    record.site = found->site.identity.id;
    record.terrain_recipe = document_.model.terrain_policy;
    record.kind = found->site.identity.kind;
    const bool inspected = std::ranges::any_of(
        knowledge_->surface_landmarks->inspections, [&](const auto& item) {
          return item.system == record.target.system &&
                 item.planet == rotation_.rotation.planet && item.site == id &&
                 item.cell == record.cell &&
                 item.terrain_policy == record.terrain_recipe &&
                 item.tick <= record.tick;
        });
    if (!inspected)
      return std::unexpected{
          "Inspect this exposure before collecting a specimen"};
  }
  if (knowledge_->field_samples) {
    const auto& samples = *knowledge_->field_samples;
    if (samples.carrier != record.craft)
      return std::unexpected{"Specimen pouch belongs to a different carrier"};
    if (std::ranges::any_of(samples.carried, [&](const auto& item) {
          return same_site(item, record.target, id);
        }))
      return std::unexpected{"This specimen is already in the pouch"};
    if (samples.carried.size() >= kFreedomFieldSampleCapacity)
      return std::unexpected{
          "Specimen pouch is full; discard a specimen to free a slot"};
    const bool noted =
        std::ranges::any_of(samples.journal, [&](const auto& item) {
          return same_site(item, record.target, id);
        });
    if (!noted &&
        samples.journal.size() >= kMaximumFreedomSurfaceLandmarkInspections)
      return std::unexpected{"The first-collection journal is full"};
  }
  return record;
}

auto NativeFreedomFlightSession::field_sample_collection_available(
    SurfaceLandmarkId id) const -> std::expected<void, std::string> {
  const auto record = field_sample_collection_source(id);
  if (!record) return std::unexpected{record.error()};
  return {};
}
auto NativeFreedomFlightSession::collect_field_sample(SurfaceLandmarkId id)
    -> std::expected<void, std::string> {
  const auto record = field_sample_collection_source(id);
  if (!record) return std::unexpected{record.error()};
  auto candidate = std::make_unique<NativeFreedomFlightSession>(*this);
  if (!candidate->knowledge_->field_samples)
    candidate->knowledge_->field_samples = FreedomFieldSampleState{
        kFreedomFieldSamplesVersion, document_.origin.state.craft, {}, {}};
  auto& samples = *candidate->knowledge_->field_samples;
  if (!std::ranges::any_of(samples.journal, [&](const auto& item) {
        return same_site(item, record->target, id);
      }))
    samples.journal.push_back(*record);
  samples.carried.push_back(*record);
  std::ranges::sort(samples.carried, {}, sample_key);
  std::ranges::sort(samples.journal, {}, sample_key);
  const auto saved = candidate->player_save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(*candidate);
  return {};
}
auto NativeFreedomFlightSession::discard_field_sample(BodyTarget target,
                                                      SurfaceLandmarkId id)
    -> std::expected<void, std::string> {
  if (recovery_pending() || !knowledge_ || !knowledge_->field_samples ||
      !validate_freedom_knowledge(*knowledge_, document_.flight.tick) ||
      knowledge_->field_samples->carrier != document_.origin.state.craft)
    return std::unexpected{"No owned specimen pouch is available"};
  const auto found = std::ranges::find_if(
      knowledge_->field_samples->carried,
      [&](const auto& item) { return same_site(item, target, id); });
  if (found == knowledge_->field_samples->carried.end())
    return std::unexpected{"That specimen is not in the pouch"};
  auto candidate = *this;
  std::erase_if(candidate.knowledge_->field_samples->carried,
                [&](const auto& item) { return same_site(item, target, id); });
  const auto saved = candidate.player_save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(candidate);
  return {};
}
auto NativeFreedomFlightSession::mark_field_sample_site(BodyTarget target,
                                                        SurfaceLandmarkId id,
                                                        bool replace)
    -> std::expected<void, std::string> {
  if (recovery_pending() || !knowledge_ || !knowledge_->field_samples ||
      (travel_ && travel_->phase != FreedomJumpPhase::idle) || !id.value ||
      !validate_freedom_knowledge(*knowledge_, document_.flight.tick))
    return std::unexpected{"Marking a specimen site requires an idle journey "
                           "and an authentic retained note"};
  const auto& journal = knowledge_->field_samples->journal;
  const auto record = std::ranges::find_if(
      journal, [&](const auto& item) { return same_site(item, target, id); });
  if (record == journal.end())
    return std::unexpected{"No retained specimen note for that site"};
  const bool present = knowledge_->surface_marker &&
                       knowledge_->surface_marker->point.has_value();
  if (present != replace)
    return std::unexpected{
        replace ? "No surface marker to replace"
                : "Use Replace marker to change the saved point"};
  const auto body = relative_state();
  if (!body || target.system != system_.catalog.id ||
      body->frame.system != target.system || minor_)
    return std::unexpected{"Return to the recorded specimen's body first"};
  FreedomSurfaceMarkerState marker;
  if (moon_) {
    if (target.body != moon_->body || body->frame.body != target.body ||
        body->frame.kind != RigidFrameKind::body_relative_inertial ||
        !document_.moon ||
        document_.moon->terrain_recipe != record->terrain_recipe)
      return std::unexpected{
          "The recorded Moon or terrain recipe is not loaded"};
    auto field =
        MoonSurfaceLandmarkField::create(*moon_, record->terrain_recipe);
    if (!field) return std::unexpected{"Recorded Moon terrain owner refused"};
    const auto site = field->site(record->cell);
    if (!site || !*site || (*site)->identity.id != id ||
        (*site)->identity.kind != record->kind)
      return std::unexpected{"The recorded Moon exposure is unavailable"};
    marker.version = kFreedomMoonSurfaceMarkerVersion;
    marker.point = FreedomMoonSurfaceMarkerPoint{target.system, target.body,
                                                 (*site)->position_metres,
                                                 document_.flight.tick};
  } else {
    if (!body->frame.planet || target.body != body_id(*body->frame.planet) ||
        body->frame.kind != RigidFrameKind::planet_relative_inertial ||
        document_.model.terrain_policy != record->terrain_recipe)
      return std::unexpected{
          "The recorded planet or terrain recipe is not loaded"};
    auto field = SurfaceLandmarkField::create(system_, target.body,
                                              record->terrain_recipe);
    if (!field) return std::unexpected{"Recorded planet terrain owner refused"};
    const auto site = field->site(record->cell);
    if (!site || !*site || (*site)->identity.id != id ||
        (*site)->identity.kind != record->kind)
      return std::unexpected{"The recorded planetary exposure is unavailable"};
    marker.point = FreedomSurfaceMarkerPoint{target.system, *body->frame.planet,
                                             (*site)->position_metres,
                                             document_.flight.tick};
  }
  auto candidate = std::make_unique<NativeFreedomFlightSession>(*this);
  candidate->knowledge_->surface_marker = std::move(marker);
  const auto saved = candidate->player_save_document();
  if (!saved) return std::unexpected{saved.error()};
  if (const auto encoded = encode_native_save_document_json(*saved); !encoded)
    return std::unexpected{encoded.error().detail};
  *this = std::move(*candidate);
  return {};
}
} // namespace apsis_drift
