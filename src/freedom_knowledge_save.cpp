#include "apsis_drift/freedom_knowledge_save.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;
auto failure(std::string message) -> SaveSchemaError {
  return {SaveSchemaErrorCode::invalid_state, "$.knowledge",
          std::move(message)};
}
auto fields(const Json& j, std::initializer_list<std::string_view> keys)
    -> bool {
  if (!j.is_object() || j.size() != keys.size()) return false;
  for (auto k : keys)
    if (!j.contains(std::string{k})) return false;
  return true;
}
auto unsigned_fields(const Json& j,
                     std::initializer_list<std::string_view> keys) -> bool {
  for (auto k : keys)
    if (!j.contains(std::string{k}) || !j[std::string{k}].is_number_unsigned())
      return false;
  return true;
}
auto bounded_cell_coordinate(const Json& coordinate) -> bool {
  if (coordinate.is_number_unsigned())
    return coordinate.get<std::uint64_t>() <=
           static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
  if (!coordinate.is_number_integer()) return false;
  const auto value = coordinate.get<std::int64_t>();
  return value >= std::numeric_limits<std::int32_t>::min() &&
         value <= std::numeric_limits<std::int32_t>::max();
}
auto sample_craft(const Json& value)
    -> std::expected<StarterCraftId, SaveSchemaError> {
  if (!value.is_string())
    return std::unexpected{failure("canonical sample craft identity required")};
  const auto& text = value.get_ref<const std::string&>();
  std::uint64_t id{};
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), id);
  if (text.empty() || text.size() > 20 || !id || result.ec != std::errc{} ||
      result.ptr != text.data() + text.size() || std::to_string(id) != text)
    return std::unexpected{failure("invalid sample craft identity")};
  return StarterCraftId{id};
}
auto sample_record_json(const FreedomFieldSampleRecord& r)
    -> std::expected<Json, SaveSchemaError> {
  const auto target = encode_body_target(r.target);
  if (!target) return std::unexpected{failure("invalid sample target")};
  return Json{{"target", Json::parse(*target)},
              {"cell", Json::array({r.cell.x, r.cell.y, r.cell.z})},
              {"site", r.site.value},
              {"terrain_recipe", r.terrain_recipe},
              {"kind", static_cast<unsigned>(r.kind)},
              {"craft", std::to_string(r.craft.value)},
              {"tick", r.tick}};
}
auto sample_record_value(const Json& j)
    -> std::expected<FreedomFieldSampleRecord, SaveSchemaError> {
  if (!fields(j, {"target", "cell", "site", "terrain_recipe", "kind", "craft",
                  "tick"}) ||
      !unsigned_fields(j, {"site", "terrain_recipe", "kind", "tick"}) ||
      j["terrain_recipe"] > std::numeric_limits<std::uint32_t>::max() ||
      j["kind"] > static_cast<unsigned>(SurfaceLandmarkKind::dark_exposure) ||
      !j["cell"].is_array() || j["cell"].size() != 3)
    return std::unexpected{failure("invalid closed field sample record")};
  const auto target = decode_body_target(j["target"].dump());
  const auto craft = sample_craft(j["craft"]);
  if (!target || !craft)
    return std::unexpected{failure("invalid field sample owner")};
  const auto& cell = j["cell"];
  for (const auto& coordinate : cell)
    if (!bounded_cell_coordinate(coordinate))
      return std::unexpected{failure("bounded sample cell required")};
  return FreedomFieldSampleRecord{
      *target,
      {cell[0].get<std::int32_t>(), cell[1].get<std::int32_t>(),
       cell[2].get<std::int32_t>()},
      {j["site"].get<std::uint64_t>()},
      j["terrain_recipe"].get<std::uint32_t>(),
      static_cast<SurfaceLandmarkKind>(j["kind"].get<unsigned>()),
      *craft,
      j["tick"].get<SimulationTick>()};
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
auto commission_json(const SurveyCommission& commission) -> Json {
  Json measurement = nullptr;
  if (commission.measurement) {
    const auto& value = *commission.measurement;
    measurement = {{"craft", value.craft.value},
                   {"started_tick", value.started_tick},
                   {"last_tick", value.last_tick},
                   {"acquired_ticks", value.acquired_ticks}};
  }
  return {{"sequence", commission.sequence},
          {"system", commission.system.value},
          {"planet", commission.planet.value},
          {"accepted_tick", commission.accepted_tick},
          {"deadline_tick", commission.deadline_tick},
          {"measurement", std::move(measurement)}};
}
auto commission_value(const Json& j)
    -> std::expected<SurveyCommission, SaveSchemaError> {
  if (!fields(j, {"sequence", "system", "planet", "accepted_tick",
                  "deadline_tick", "measurement"}) ||
      !unsigned_fields(j, {"sequence", "system", "planet", "accepted_tick",
                           "deadline_tick"}) ||
      j["sequence"] > std::numeric_limits<std::uint32_t>::max())
    return std::unexpected{failure("invalid closed survey commission")};
  SurveyCommission commission{j["sequence"].get<std::uint32_t>(),
                              {j["system"].get<std::uint64_t>()},
                              {j["planet"].get<std::uint64_t>()},
                              j["accepted_tick"].get<SimulationTick>(),
                              j["deadline_tick"].get<SimulationTick>(),
                              {}};
  if (!j["measurement"].is_null()) {
    const auto& m = j["measurement"];
    if (!fields(m, {"craft", "started_tick", "last_tick", "acquired_ticks"}) ||
        !unsigned_fields(
            m, {"craft", "started_tick", "last_tick", "acquired_ticks"}) ||
        m["acquired_ticks"] > kSurveyCommissionMeasurementTicks)
      return std::unexpected{failure("invalid commission measurement")};
    commission.measurement =
        SurveyCommissionMeasurement{{m["craft"].get<std::uint64_t>()},
                                    m["started_tick"].get<SimulationTick>(),
                                    m["last_tick"].get<SimulationTick>(),
                                    m["acquired_ticks"].get<std::uint32_t>()};
  }
  return commission;
}
auto derelict_record_json(const FreedomDerelictObservation& r) -> Json {
  return {
      {"craft", r.craft.value},
      {"tick", r.tick},
      {"position_metres", Json::array({r.position_metres.x, r.position_metres.y,
                                       r.position_metres.z})},
      {"velocity_metres_per_second",
       Json::array({r.velocity_metres_per_second.x,
                    r.velocity_metres_per_second.y,
                    r.velocity_metres_per_second.z})}};
}
auto derelict_record_value(const Json& j)
    -> std::expected<FreedomDerelictObservation, SaveSchemaError> {
  if (!fields(j, {"craft", "tick", "position_metres",
                  "velocity_metres_per_second"}) ||
      !unsigned_fields(j, {"craft", "tick"}))
    return std::unexpected{failure("invalid closed derelict record")};
  for (auto name : {"position_metres", "velocity_metres_per_second"}) {
    const auto& vector = j[name];
    if (!vector.is_array() || vector.size() != 3)
      return std::unexpected{failure("derelict vector requires three values")};
    for (const auto& component : vector)
      if (!component.is_number() || !std::isfinite(component.get<double>()))
        return std::unexpected{failure("finite derelict vector required")};
  }
  const auto& p = j["position_metres"];
  const auto& v = j["velocity_metres_per_second"];
  return FreedomDerelictObservation{
      {j["craft"].get<std::uint64_t>()},
      j["tick"].get<SimulationTick>(),
      {p[0].get<double>(), p[1].get<double>(), p[2].get<double>()},
      {v[0].get<double>(), v[1].get<double>(), v[2].get<double>()}};
}
auto parse(std::string_view text, std::size_t limit)
    -> std::expected<Json, SaveSchemaError> {
  if (text.size() > limit)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "knowledge document exceeds limit"}};
  if (!bounded_nesting(text))
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::malformed_json, "$",
                        "unbalanced or excessive JSON nesting"}};
  bool duplicate{};
  std::vector<std::unordered_set<std::string>> keys;
  const Json::parser_callback_t callback = [&](int, Json::parse_event_t event,
                                               Json& value) {
    if (event == Json::parse_event_t::object_start) keys.emplace_back();
    if (event == Json::parse_event_t::key &&
        (keys.empty() || !keys.back().insert(value.get<std::string>()).second))
      duplicate = true;
    if (event == Json::parse_event_t::object_end && !keys.empty())
      keys.pop_back();
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
                                           "$", "repeated JSON key"}};
  return root;
}
} // namespace

auto encode_freedom_knowledge_json(const FreedomKnowledge& k,
                                   SimulationTick now)
    -> std::expected<std::string, SaveSchemaError> {
  if (!validate_freedom_knowledge(k, now))
    return std::unexpected{failure("invalid knowledge recipe or provenance")};
  const auto& r = k.recipe;
  Json entries = Json::array();
  for (const auto& e : k.entries) {
    Json transitions = Json::array();
    for (const auto& t : e.transitions)
      transitions.push_back({{"level", static_cast<unsigned>(t.level)},
                             {"source", static_cast<unsigned>(t.source)},
                             {"source_id", t.source_id},
                             {"tick", t.tick},
                             {"flight", t.flight.value}});
    entries.push_back({{"kind", static_cast<unsigned>(e.subject.kind)},
                       {"system", e.subject.system.value},
                       {"identity", e.subject.identity},
                       {"fact", static_cast<unsigned>(e.fact)},
                       {"transitions", std::move(transitions)}});
  }
  Json root{{"recipe",
             {{"version", r.version},
              {"universe_seed", r.universe_seed.value},
              {"chart", r.chart},
              {"topology", r.topology},
              {"physical_catalog", r.physical_catalog},
              {"ephemeris", r.ephemeris},
              {"ambient", r.ambient}}},
            {"entries", std::move(entries)}};
  if (r.version == kFreedomObservedKnowledgeVersion ||
      r.version == kFreedomTravelKnowledgeVersion ||
      freedom_knowledge_uses_network(r.version))
    root["recipe"]["observation_policy"] = r.observation_policy;
  if (r.version == kFreedomTravelKnowledgeVersion ||
      freedom_knowledge_uses_network(r.version))
    root["recipe"]["world_domain"] = r.world_domain;
  if (r.craft_lineage)
    root["recipe"]["craft_lineage"] = {
        {"version", r.craft_lineage->version},
        {"generation", std::to_string(r.craft_lineage->generation)}};
  if (k.surface_marker) {
    Json point = nullptr;
    if (k.surface_marker->point) {
      if (const auto* m = std::get_if<FreedomSurfaceMarkerPoint>(
              &*k.surface_marker->point)) {
        point = {{"system", m->system.value},
                 {"planet", m->planet.value},
                 {"body_fixed_metres",
                  Json::array({m->position.x, m->position.y, m->position.z})},
                 {"tick", m->tick}};
      } else {
        const auto& moon =
            std::get<FreedomMoonSurfaceMarkerPoint>(*k.surface_marker->point);
        const auto target = encode_body_target({moon.system, moon.body});
        if (!target)
          return std::unexpected{failure("invalid Moon marker target")};
        point = {
            {"target", Json::parse(*target)},
            {"body_fixed_metres",
             Json::array({moon.position.x, moon.position.y, moon.position.z})},
            {"tick", moon.tick}};
      }
    }
    root["surface_marker"] = {{"version", k.surface_marker->version},
                              {"point", std::move(point)}};
  }
  if (k.surface_landmarks) {
    Json inspections = Json::array();
    for (const auto& inspection : k.surface_landmarks->inspections)
      inspections.push_back(
          {{"system", inspection.system.value},
           {"planet", inspection.planet.value},
           {"cell", Json::array({inspection.cell.x, inspection.cell.y,
                                 inspection.cell.z})},
           {"site", inspection.site.value},
           {"terrain_policy", inspection.terrain_policy},
           {"tick", inspection.tick}});
    root["surface_landmarks"] = {{"version", k.surface_landmarks->version},
                                 {"inspections", std::move(inspections)}};
  }
  if (k.moon_surface_landmarks) {
    Json inspections = Json::array();
    for (const auto& inspection : k.moon_surface_landmarks->inspections) {
      const auto target = encode_body_target(inspection.target);
      if (!target)
        return std::unexpected{failure("invalid Moon exposure target")};
      inspections.push_back(
          {{"target", Json::parse(*target)},
           {"cell", Json::array({inspection.cell.x, inspection.cell.y,
                                 inspection.cell.z})},
           {"site", inspection.site.value},
           {"terrain_recipe", inspection.terrain_recipe},
           {"tick", inspection.tick}});
    }
    root["moon_surface_landmarks"] = {
        {"version", k.moon_surface_landmarks->version},
        {"inspections", std::move(inspections)}};
  }
  if (k.orbital_surveys) {
    const auto& surveys = *k.orbital_surveys;
    Json active = nullptr;
    if (surveys.active) {
      const auto& item = *surveys.active;
      active = {{"system", item.system.value},
                {"planet", item.planet.value},
                {"craft", item.craft.value},
                {"started_tick", item.started_tick},
                {"acquired_ticks", item.acquired_ticks}};
    }
    Json completed = Json::array();
    for (const auto& item : surveys.completed)
      completed.push_back({{"system", item.system.value},
                           {"planet", item.planet.value},
                           {"craft", item.craft.value},
                           {"started_tick", item.started_tick},
                           {"completed_tick", item.completed_tick}});
    root["orbital_surveys"] = {{"version", surveys.version},
                               {"active", std::move(active)},
                               {"completed", std::move(completed)}};
  }
  if (k.field_samples) {
    Json carried = Json::array(), journal = Json::array();
    for (const auto& r : k.field_samples->carried) {
      auto item = sample_record_json(r);
      if (!item) return std::unexpected{item.error()};
      carried.push_back(std::move(*item));
    }
    for (const auto& r : k.field_samples->journal) {
      auto item = sample_record_json(r);
      if (!item) return std::unexpected{item.error()};
      journal.push_back(std::move(*item));
    }
    root["field_samples"] = {
        {"version", k.field_samples->version},
        {"carrier", std::to_string(k.field_samples->carrier.value)},
        {"carried", std::move(carried)},
        {"journal", std::move(journal)}};
  }
  if (k.star_search) {
    const auto& search = *k.star_search;
    Json active = nullptr;
    if (search.active) {
      const auto& item = *search.active;
      active = {{"source", item.source.value},
                {"target", item.target.value},
                {"craft", item.craft.value},
                {"started_tick", item.started_tick},
                {"last_tick", item.last_tick},
                {"acquired_ticks", item.acquired_ticks}};
    }
    Json completed = Json::array();
    for (const auto& item : search.completed)
      completed.push_back({{"source", item.source.value},
                           {"target", item.target.value},
                           {"craft", item.craft.value},
                           {"started_tick", item.started_tick},
                           {"completed_tick", item.completed_tick}});
    root["star_search"] = {{"version", search.version},
                           {"active", std::move(active)},
                           {"completed", std::move(completed)}};
  }
  if (k.survey_commissions) {
    const auto& account = *k.survey_commissions;
    Json latest = nullptr;
    if (account.latest_paid)
      latest = {
          {"commission", commission_json(account.latest_paid->commission)},
          {"submitted_tick", account.latest_paid->submitted_tick}};
    root["survey_commissions"] = {
        {"version", account.version},
        {"next_sequence", account.next_sequence},
        {"paid_count", account.paid_count},
        {"active",
         account.active ? commission_json(*account.active) : Json(nullptr)},
        {"latest_paid", std::move(latest)}};
  }
  if (k.paid_operations) {
    const auto& account = *k.paid_operations;
    root["paid_operations"] = {
        {"version", account.version},
        {"started_tick", account.started_tick},
        {"billed_through_hours", account.billed_through_hours},
        {"spent_credits", account.spent_credits},
        {"operating_tab", account.operating_tab},
        {"recovery_due", account.recovery_due},
        {"settled_paid_count", account.settled_paid_count},
        {"supported_sequence", account.supported_sequence},
        {"charged_generation", account.charged_generation}};
  }
  if (k.station_stellar_lead) {
    const auto& lead = *k.station_stellar_lead;
    root["station_stellar_lead"] = {{"version", lead.version},
                                    {"station", lead.station.value},
                                    {"attendant", lead.attendant},
                                    {"craft", lead.craft.value},
                                    {"received_tick", lead.received_tick},
                                    {"target", lead.target.value}};
  }
  if (k.derelicts)
    root["derelicts"] = {
        {"version", k.derelicts->version},
        {"contact", k.derelicts->contact
                        ? derelict_record_json(*k.derelicts->contact)
                        : Json(nullptr)},
        {"inspection", k.derelicts->inspection
                           ? derelict_record_json(*k.derelicts->inspection)
                           : Json(nullptr)}};
  auto text = root.dump(2) + '\n';
  const auto limit = freedom_knowledge_uses_network(r.version)
                         ? kMaximumFreedomNetworkKnowledgeBytes
                         : kMaximumFreedomKnowledgeBytes;
  if (text.size() > limit)
    return std::unexpected{failure("knowledge encoding exceeds bounded size")};
  return text;
}
auto decode_freedom_knowledge_json(std::string_view text, SimulationTick now)
    -> std::expected<FreedomKnowledge, SaveSchemaError> {
  auto root = parse(text, kMaximumFreedomNetworkKnowledgeBytes);
  if (!root) return std::unexpected{root.error()};
  const bool marker_selected =
      root->is_object() && root->contains("surface_marker");
  const bool landmarks_selected =
      root->is_object() && root->contains("surface_landmarks");
  const bool moon_landmarks_selected =
      root->is_object() && root->contains("moon_surface_landmarks");
  const bool surveys_selected =
      root->is_object() && root->contains("orbital_surveys");
  const bool samples_selected =
      root->is_object() && root->contains("field_samples");
  const bool search_selected =
      root->is_object() && root->contains("star_search");
  const bool commissions_selected =
      root->is_object() && root->contains("survey_commissions");
  const bool paid_selected =
      root->is_object() && root->contains("paid_operations");
  const bool lead_selected =
      root->is_object() && root->contains("station_stellar_lead");
  const bool derelicts_selected =
      root->is_object() && root->contains("derelicts");
  auto root_shape = *root;
  if (marker_selected) root_shape.erase("surface_marker");
  if (landmarks_selected) root_shape.erase("surface_landmarks");
  if (moon_landmarks_selected) root_shape.erase("moon_surface_landmarks");
  if (surveys_selected) root_shape.erase("orbital_surveys");
  if (samples_selected) root_shape.erase("field_samples");
  if (search_selected) root_shape.erase("star_search");
  if (derelicts_selected) root_shape.erase("derelicts");
  if (lead_selected) root_shape.erase("station_stellar_lead");
  if (commissions_selected) root_shape.erase("survey_commissions");
  if (paid_selected) root_shape.erase("paid_operations");
  if (!fields(root_shape, {"recipe", "entries"}) ||
      !(*root)["recipe"].is_object())
    return std::unexpected{failure("invalid closed knowledge shape")};
  const auto& r = (*root)["recipe"];
  const bool network = r.contains("version") &&
                       r["version"].is_number_unsigned() &&
                       (r["version"] == kFreedomNetworkKnowledgeVersion ||
                        r["version"] == kFreedomPlanetaryKnowledgeVersion ||
                        r["version"] == kFreedomUnchartedKnowledgeVersion);
  const bool uncharted =
      network && r["version"] == kFreedomUnchartedKnowledgeVersion;
  if (!network && text.size() > kMaximumFreedomKnowledgeBytes)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "knowledge document exceeds limit"}};
  const bool travel =
      r.contains("version") && r["version"].is_number_unsigned() &&
      (r["version"] == kFreedomTravelKnowledgeVersion || network);
  const bool observed =
      r.contains("version") && r["version"].is_number_unsigned() &&
      (r["version"] == kFreedomObservedKnowledgeVersion || travel);
  auto shape = r;
  if (travel) shape.erase("craft_lineage");
  if (!(travel ? fields(shape, {"version", "universe_seed", "chart", "topology",
                                "physical_catalog", "ephemeris", "ambient",
                                "observation_policy", "world_domain"})
        : observed ? fields(r, {"version", "universe_seed", "chart", "topology",
                                "physical_catalog", "ephemeris", "ambient",
                                "observation_policy"})
                   : fields(r, {"version", "universe_seed", "chart", "topology",
                                "physical_catalog", "ephemeris", "ambient"})))
    return std::unexpected{failure("invalid closed knowledge shape")};
  if (!unsigned_fields(r, {"version", "universe_seed", "chart", "topology",
                           "physical_catalog", "ephemeris", "ambient"}))
    return std::unexpected{failure("unsigned recipe fields required")};
  for (auto name : {"version", "chart", "topology", "physical_catalog",
                    "ephemeris", "ambient"})
    if (r[name] > std::numeric_limits<std::uint32_t>::max())
      return std::unexpected{failure("recipe value cannot narrow")};
  if (observed && (!r["observation_policy"].is_number_unsigned() ||
                   r["observation_policy"] != 1))
    return std::unexpected{failure("unsupported observation policy")};
  FreedomKnowledge k{{r["version"].get<std::uint32_t>(),
                      {r["universe_seed"].get<std::uint64_t>()},
                      r["chart"].get<std::uint32_t>(),
                      r["topology"].get<std::uint32_t>(),
                      r["physical_catalog"].get<std::uint32_t>(),
                      r["ephemeris"].get<std::uint32_t>(),
                      r["ambient"].get<std::uint32_t>()},
                     {}};
  if (observed) k.recipe.observation_policy = 1;
  if (travel) {
    if (!r["world_domain"].is_number_unsigned() ||
        r["world_domain"] != (network ? 2U : 1U))
      return std::unexpected{failure("unsupported travel knowledge domain")};
    k.recipe.world_domain = network ? 2U : 1U;
  }
  if (r.contains("craft_lineage")) {
    const auto& l = r["craft_lineage"];
    if (!travel || !fields(l, {"version", "generation"}) ||
        !unsigned_fields(l, {"version"}) ||
        l["version"] != kFreedomCraftLineageVersion ||
        !l["generation"].is_string())
      return std::unexpected{failure("invalid selected craft lineage shape")};
    const auto& digits = l["generation"].get_ref<const std::string&>();
    std::uint64_t generation{};
    const auto parsed = std::from_chars(
        digits.data(), digits.data() + digits.size(), generation);
    if (digits.empty() || digits.size() > 20 || digits.front() == '0' ||
        parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size())
      return std::unexpected{failure("invalid selected craft generation")};
    k.recipe.craft_lineage = FreedomCraftLineage{1, generation};
  }
  if (marker_selected) {
    const auto& marker = (*root)["surface_marker"];
    if (!observed || !fields(marker, {"version", "point"}) ||
        !unsigned_fields(marker, {"version"}) ||
        (marker["version"] != kFreedomSurfaceMarkerVersion &&
         marker["version"] != kFreedomMoonSurfaceMarkerVersion &&
         marker["version"] != kFreedomNetworkMoonSurfaceMarkerVersion))
      return std::unexpected{failure("unsupported surface marker selection")};
    k.surface_marker =
        FreedomSurfaceMarkerState{marker["version"].get<std::uint32_t>(), {}};
    const auto& m = marker["point"];
    if (!m.is_null()) {
      const bool moon =
          k.surface_marker->version != kFreedomSurfaceMarkerVersion;
      if (!(moon ? fields(m, {"target", "body_fixed_metres", "tick"})
                 : fields(m,
                          {"system", "planet", "body_fixed_metres", "tick"})) ||
          !(moon ? unsigned_fields(m, {"tick"})
                 : unsigned_fields(m, {"system", "planet", "tick"})) ||
          !m["body_fixed_metres"].is_array() ||
          m["body_fixed_metres"].size() != 3)
        return std::unexpected{failure("invalid closed surface marker point")};
      const auto& p = m["body_fixed_metres"];
      for (const auto& component : p)
        if (!component.is_number())
          return std::unexpected{
              failure("numeric surface marker point required")};
      if (moon) {
        const auto target = decode_body_target(m["target"].dump());
        if (!target || target->body.kind != BodyKind::moon)
          return std::unexpected{failure("invalid typed Moon marker target")};
        k.surface_marker->point = FreedomMoonSurfaceMarkerPoint{
            target->system,
            target->body,
            {p[0].get<double>(), p[1].get<double>(), p[2].get<double>()},
            m["tick"].get<SimulationTick>()};
      } else {
        k.surface_marker->point = FreedomSurfaceMarkerPoint{
            {m["system"].get<std::uint64_t>()},
            {m["planet"].get<std::uint64_t>()},
            {p[0].get<double>(), p[1].get<double>(), p[2].get<double>()},
            m["tick"].get<SimulationTick>()};
      }
    }
  }
  if (landmarks_selected) {
    const auto& landmarks = (*root)["surface_landmarks"];
    if (!observed || !fields(landmarks, {"version", "inspections"}) ||
        !unsigned_fields(landmarks, {"version"}) ||
        landmarks["version"] != kFreedomSurfaceLandmarkVersion ||
        !landmarks["inspections"].is_array() ||
        landmarks["inspections"].size() >
            kMaximumFreedomSurfaceLandmarkInspections)
      return std::unexpected{failure("unsupported surface landmark selection")};
    k.surface_landmarks = FreedomSurfaceLandmarkState{};
    for (const auto& inspection : landmarks["inspections"]) {
      if (!fields(inspection, {"system", "planet", "cell", "site",
                               "terrain_policy", "tick"}) ||
          !unsigned_fields(inspection, {"system", "planet", "site",
                                        "terrain_policy", "tick"}) ||
          inspection["terrain_policy"] >
              std::numeric_limits<std::uint32_t>::max() ||
          !inspection["cell"].is_array() || inspection["cell"].size() != 3)
        return std::unexpected{failure("invalid closed landmark inspection")};
      const auto& cell = inspection["cell"];
      for (const auto& coordinate : cell)
        if (!bounded_cell_coordinate(coordinate))
          return std::unexpected{
              failure("bounded integer landmark cell required")};
      k.surface_landmarks->inspections.push_back(
          {{inspection["system"].get<std::uint64_t>()},
           {inspection["planet"].get<std::uint64_t>()},
           {cell[0].get<std::int32_t>(), cell[1].get<std::int32_t>(),
            cell[2].get<std::int32_t>()},
           {inspection["site"].get<std::uint64_t>()},
           inspection["terrain_policy"].get<std::uint32_t>(),
           inspection["tick"].get<SimulationTick>()});
    }
  }
  if (moon_landmarks_selected) {
    const auto& landmarks = (*root)["moon_surface_landmarks"];
    if (!observed || !fields(landmarks, {"version", "inspections"}) ||
        !unsigned_fields(landmarks, {"version"}) ||
        (landmarks["version"] != kFreedomMoonSurfaceLandmarkVersion &&
         landmarks["version"] != kFreedomNetworkMoonSurfaceLandmarkVersion) ||
        !landmarks["inspections"].is_array() ||
        landmarks["inspections"].size() >
            kMaximumFreedomSurfaceLandmarkInspections)
      return std::unexpected{failure("unsupported Moon exposure selection")};
    k.moon_surface_landmarks = FreedomMoonSurfaceLandmarkState{};
    k.moon_surface_landmarks->version =
        landmarks["version"].get<std::uint32_t>();
    for (const auto& inspection : landmarks["inspections"]) {
      if (!fields(inspection,
                  {"target", "cell", "site", "terrain_recipe", "tick"}) ||
          !unsigned_fields(inspection, {"site", "terrain_recipe", "tick"}) ||
          inspection["terrain_recipe"] >
              std::numeric_limits<std::uint32_t>::max() ||
          !inspection["cell"].is_array() || inspection["cell"].size() != 3)
        return std::unexpected{
            failure("invalid closed Moon exposure inspection")};
      const auto target = decode_body_target(inspection["target"].dump());
      if (!target)
        return std::unexpected{failure("invalid Moon exposure target")};
      const auto& cell = inspection["cell"];
      for (const auto& coordinate : cell)
        if (!bounded_cell_coordinate(coordinate))
          return std::unexpected{
              failure("bounded integer Moon exposure cell required")};
      k.moon_surface_landmarks->inspections.push_back(
          {*target,
           {cell[0].get<std::int32_t>(), cell[1].get<std::int32_t>(),
            cell[2].get<std::int32_t>()},
           {inspection["site"].get<std::uint64_t>()},
           inspection["terrain_recipe"].get<std::uint32_t>(),
           inspection["tick"].get<SimulationTick>()});
    }
  }
  if (surveys_selected) {
    const auto& surveys = (*root)["orbital_surveys"];
    if (!fields(surveys, {"version", "active", "completed"}) ||
        !unsigned_fields(surveys, {"version"}) ||
        surveys["version"] != kFreedomOrbitalSurveyVersion ||
        !surveys["completed"].is_array() ||
        surveys["completed"].size() > kMaximumFreedomOrbitalSurveys)
      return std::unexpected{failure("invalid orbital survey state")};
    FreedomOrbitalSurveyState state;
    if (!surveys["active"].is_null()) {
      const auto& item = surveys["active"];
      if (!fields(item, {"system", "planet", "craft", "started_tick",
                         "acquired_ticks"}) ||
          !unsigned_fields(item, {"system", "planet", "craft", "started_tick",
                                  "acquired_ticks"}) ||
          item["acquired_ticks"] >= kFreedomOrbitalSurveyTicks)
        return std::unexpected{failure("invalid active orbital survey")};
      state.active = FreedomOrbitalSurveyAcquisition{
          {item["system"].get<std::uint64_t>()},
          {item["planet"].get<std::uint64_t>()},
          {item["craft"].get<std::uint64_t>()},
          item["started_tick"].get<SimulationTick>(),
          item["acquired_ticks"].get<std::uint32_t>()};
    }
    for (const auto& item : surveys["completed"]) {
      if (!fields(item, {"system", "planet", "craft", "started_tick",
                         "completed_tick"}) ||
          !unsigned_fields(item, {"system", "planet", "craft", "started_tick",
                                  "completed_tick"}))
        return std::unexpected{failure("invalid completed orbital survey")};
      state.completed.push_back({{item["system"].get<std::uint64_t>()},
                                 {item["planet"].get<std::uint64_t>()},
                                 {item["craft"].get<std::uint64_t>()},
                                 item["started_tick"].get<SimulationTick>(),
                                 item["completed_tick"].get<SimulationTick>()});
    }
    k.orbital_surveys = std::move(state);
  }
  if (samples_selected) {
    const auto& samples = (*root)["field_samples"];
    if (!observed ||
        !fields(samples, {"version", "carrier", "carried", "journal"}) ||
        !unsigned_fields(samples, {"version"}) ||
        (samples["version"] != kFreedomFieldSamplesVersion &&
         samples["version"] != kFreedomNetworkFieldSamplesVersion) ||
        !samples["carried"].is_array() || !samples["journal"].is_array() ||
        samples["carried"].size() > kFreedomFieldSampleCapacity ||
        samples["journal"].size() > kMaximumFreedomSurfaceLandmarkInspections)
      return std::unexpected{failure("invalid field sample state")};
    const auto carrier = sample_craft(samples["carrier"]);
    if (!carrier) return std::unexpected{carrier.error()};
    FreedomFieldSampleState state;
    state.version = samples["version"].get<std::uint32_t>();
    state.carrier = *carrier;
    for (const auto& j : samples["carried"]) {
      auto r = sample_record_value(j);
      if (!r) return std::unexpected{r.error()};
      state.carried.push_back(*r);
    }
    for (const auto& j : samples["journal"]) {
      auto r = sample_record_value(j);
      if (!r) return std::unexpected{r.error()};
      state.journal.push_back(*r);
    }
    k.field_samples = std::move(state);
  }
  if (search_selected) {
    const auto& search = (*root)["star_search"];
    if (!uncharted || !fields(search, {"version", "active", "completed"}) ||
        !unsigned_fields(search, {"version"}) ||
        search["version"] != kFreedomStarSearchVersion ||
        !search["completed"].is_array() || search["completed"].size() > 2)
      return std::unexpected{failure("invalid star search state")};
    FreedomStarSearchState state;
    if (!search["active"].is_null()) {
      const auto& item = search["active"];
      if (!fields(item, {"source", "target", "craft", "started_tick",
                         "last_tick", "acquired_ticks"}) ||
          !unsigned_fields(item, {"source", "target", "craft", "started_tick",
                                  "last_tick", "acquired_ticks"}) ||
          item["acquired_ticks"] >= kFreedomStarSearchTicks)
        return std::unexpected{failure("invalid active star search")};
      state.active = FreedomStarSearchAcquisition{
          {item["source"].get<std::uint64_t>()},
          {item["target"].get<std::uint64_t>()},
          {item["craft"].get<std::uint64_t>()},
          item["started_tick"].get<SimulationTick>(),
          item["last_tick"].get<SimulationTick>(),
          item["acquired_ticks"].get<std::uint32_t>()};
    }
    for (const auto& item : search["completed"]) {
      if (!fields(item, {"source", "target", "craft", "started_tick",
                         "completed_tick"}) ||
          !unsigned_fields(item, {"source", "target", "craft", "started_tick",
                                  "completed_tick"}))
        return std::unexpected{failure("invalid completed star search")};
      state.completed.push_back({{item["source"].get<std::uint64_t>()},
                                 {item["target"].get<std::uint64_t>()},
                                 {item["craft"].get<std::uint64_t>()},
                                 item["started_tick"].get<SimulationTick>(),
                                 item["completed_tick"].get<SimulationTick>()});
    }
    k.star_search = std::move(state);
  }
  if (commissions_selected) {
    const auto& account = (*root)["survey_commissions"];
    if (!network ||
        (r["version"] != kFreedomPlanetaryKnowledgeVersion &&
         r["version"] != kFreedomUnchartedKnowledgeVersion) ||
        !fields(account, {"version", "next_sequence", "paid_count", "active",
                          "latest_paid"}) ||
        !unsigned_fields(account, {"version", "next_sequence", "paid_count"}) ||
        account["version"] != kSurveyCommissionVersion ||
        account["next_sequence"] > std::numeric_limits<std::uint32_t>::max() ||
        account["paid_count"] > std::numeric_limits<std::uint32_t>::max())
      return std::unexpected{failure("invalid survey commission account")};
    SurveyCommissionAccount state;
    state.next_sequence = account["next_sequence"].get<std::uint32_t>();
    state.paid_count = account["paid_count"].get<std::uint32_t>();
    if (!account["active"].is_null()) {
      auto active = commission_value(account["active"]);
      if (!active) return std::unexpected{active.error()};
      state.active = *active;
    }
    if (!account["latest_paid"].is_null()) {
      const auto& receipt = account["latest_paid"];
      if (!fields(receipt, {"commission", "submitted_tick"}) ||
          !unsigned_fields(receipt, {"submitted_tick"}))
        return std::unexpected{failure("invalid commission settlement")};
      auto commission = commission_value(receipt["commission"]);
      if (!commission) return std::unexpected{commission.error()};
      state.latest_paid = SurveyCommissionReceipt{
          *commission, receipt["submitted_tick"].get<SimulationTick>()};
    }
    k.survey_commissions = state;
  }
  if (paid_selected) {
    const auto& account = (*root)["paid_operations"];
    if (!network ||
        (r["version"] != kFreedomPlanetaryKnowledgeVersion &&
         r["version"] != kFreedomUnchartedKnowledgeVersion) ||
        !fields(account, {"version", "started_tick", "billed_through_hours",
                          "spent_credits", "operating_tab", "recovery_due",
                          "settled_paid_count", "supported_sequence", "charged_generation"}) ||
        !unsigned_fields(account, {"version", "started_tick", "billed_through_hours",
                                   "spent_credits", "operating_tab", "recovery_due",
                                   "settled_paid_count", "supported_sequence", "charged_generation"}) ||
        account["version"] != kPaidOperationsVersion ||
        account["settled_paid_count"] > std::numeric_limits<std::uint32_t>::max() ||
        account["supported_sequence"] > std::numeric_limits<std::uint32_t>::max())
      return std::unexpected{failure("invalid closed paid operating account")};
    k.paid_operations = PaidOperationsAccount{
        account["version"].get<std::uint32_t>(),
        account["started_tick"].get<SimulationTick>(),
        account["billed_through_hours"].get<std::uint64_t>(),
        account["spent_credits"].get<std::uint64_t>(),
        account["operating_tab"].get<std::uint64_t>(),
        account["recovery_due"].get<std::uint64_t>(),
        account["settled_paid_count"].get<std::uint32_t>(),
        account["supported_sequence"].get<std::uint32_t>(),
        account["charged_generation"].get<std::uint64_t>()};
  }
  if (lead_selected) {
    const auto& lead = (*root)["station_stellar_lead"];
    if (!uncharted ||
        !fields(lead, {"version", "station", "attendant", "craft",
                       "received_tick", "target"}) ||
        !unsigned_fields(lead, {"version", "station", "attendant", "craft",
                                "received_tick", "target"}) ||
        lead["version"] != kFreedomStationStellarLeadVersion ||
        lead["attendant"] != 1U)
      return std::unexpected{failure("invalid station stellar lead")};
    k.station_stellar_lead =
        FreedomStationStellarLead{kFreedomStationStellarLeadVersion,
                                  {lead["station"].get<std::uint64_t>()},
                                  1,
                                  {lead["craft"].get<std::uint64_t>()},
                                  lead["received_tick"].get<SimulationTick>(),
                                  {lead["target"].get<std::uint64_t>()}};
  }
  if (derelicts_selected) {
    const auto& selected = (*root)["derelicts"];
    if (!uncharted || !fields(selected, {"version", "contact", "inspection"}) ||
        !unsigned_fields(selected, {"version"}) ||
        selected["version"] != kFreedomDerelictVersion)
      return std::unexpected{failure("invalid derelict state selection")};
    FreedomDerelictState state;
    if (!selected["contact"].is_null()) {
      auto record = derelict_record_value(selected["contact"]);
      if (!record) return std::unexpected{record.error()};
      state.contact = *record;
    }
    if (!selected["inspection"].is_null()) {
      auto record = derelict_record_value(selected["inspection"]);
      if (!record) return std::unexpected{record.error()};
      state.inspection = *record;
    }
    k.derelicts = state;
  }
  const auto& entries = (*root)["entries"];
  const auto maximum_facts = uncharted ? kMaximumFreedomUnchartedKnowledgeFacts
                             : network ? kMaximumFreedomNetworkKnowledgeFacts
                                       : kMaximumFreedomKnowledgeFacts;
  if (!entries.is_array() || entries.size() > maximum_facts)
    return std::unexpected{failure("invalid or oversized fact array")};
  for (const auto& e : entries) {
    if (!fields(e, {"kind", "system", "identity", "fact", "transitions"}) ||
        !unsigned_fields(e, {"kind", "system", "identity", "fact"}) ||
        e["kind"] > static_cast<unsigned>(KnowledgeSubjectKind::station) ||
        e["fact"] > static_cast<unsigned>(KnowledgeFact::presence))
      return std::unexpected{failure("invalid subject/fact fields")};
    KnowledgeEntry row{
        {static_cast<KnowledgeSubjectKind>(e["kind"].get<unsigned>()),
         {e["system"].get<std::uint64_t>()},
         e["identity"].get<std::uint64_t>()},
        static_cast<KnowledgeFact>(e["fact"].get<unsigned>()),
        {}};
    const auto& history = e["transitions"];
    if (!history.is_array() || history.empty() ||
        history.size() > kMaximumFreedomKnowledgeTransitions)
      return std::unexpected{failure("invalid or oversized transition array")};
    for (const auto& t : history) {
      if (!fields(t, {"level", "source", "source_id", "tick", "flight"}) ||
          !unsigned_fields(
              t, {"level", "source", "source_id", "tick", "flight"}) ||
          t["level"] >
              static_cast<unsigned>(NavigationKnowledgeLevel::visited) ||
          t["source"] >
              static_cast<unsigned>(KnowledgeSource::physical_arrival))
        return std::unexpected{failure("invalid confidence/source fields")};
      row.transitions.push_back(
          {static_cast<NavigationKnowledgeLevel>(t["level"].get<unsigned>()),
           static_cast<KnowledgeSource>(t["source"].get<unsigned>()),
           t["source_id"].get<std::uint64_t>(),
           t["tick"].get<SimulationTick>(),
           {t["flight"].get<std::uint64_t>()}});
    }
    k.entries.push_back(std::move(row));
  }
  if (!validate_freedom_knowledge(k, now))
    return std::unexpected{
        failure("unsupported recipe, subject or provenance")};
  return k;
}
auto validate_freedom_knowledge_document(const FreedomKnowledgeSaveDocument& d)
    -> std::expected<void, SaveSchemaError> {
  if (auto v = validate_freedom_resource_document(d.voyage); !v) return v;
  const auto flight = surface_base_flight(d.voyage.voyage.base);
  const auto& r = d.knowledge.recipe;
  if (r.universe_seed != flight.origin.recipe.universe_seed ||
      r.physical_catalog != flight.model.physical_catalog ||
      r.ephemeris != flight.model.physical_ephemeris ||
      r.craft_lineage != flight.origin.lineage ||
      d.knowledge.derelicts.has_value() !=
          flight.origin.derelicts.has_value() ||
      !validate_freedom_knowledge(d.knowledge, flight.flight.tick) ||
      (freedom_knowledge_uses_network(r.version)) !=
          flight.origin.network.has_value() ||
      (flight.origin.network
           ? (r.world_domain != 2 ||
              flight.origin.network->version !=
                  freedom_knowledge_network_version(r.version) ||
              r.chart != freedom_knowledge_chart_version(r.version) ||
              r.topology != 2)
           : ((r.version == kFreedomTravelKnowledgeVersion) !=
              (flight.world.has_value() || flight.moon.has_value() ||
               flight.minor.has_value()))))
    return std::unexpected{
        failure("knowledge owner, recipe or clock mismatch")};
  if (d.knowledge.orbital_surveys && d.knowledge.orbital_surveys->active) {
    const auto& active = *d.knowledge.orbital_surveys->active;
    const auto system =
        flight.world ? flight.world->system : flight.flight.frame.system;
    const auto planet = flight.world ? std::optional{flight.world->planet}
                                     : flight.flight.frame.planet;
    if (active.craft != flight.origin.state.craft || flight.moon ||
        flight.minor || active.system != system || !planet ||
        active.planet != *planet)
      return std::unexpected{failure("active orbital survey owner mismatch")};
  }
  if (d.knowledge.survey_commissions &&
      d.knowledge.survey_commissions->active &&
      d.knowledge.survey_commissions->active->measurement) {
    const auto& measurement =
        *d.knowledge.survey_commissions->active->measurement;
    if (measurement.acquired_ticks < kSurveyCommissionMeasurementTicks &&
        measurement.craft != flight.origin.state.craft)
      return std::unexpected{failure("unfinished commission craft mismatch")};
  }
  if (d.knowledge.star_search && d.knowledge.star_search->active) {
    const auto& active = *d.knowledge.star_search->active;
    const auto system =
        flight.world ? flight.world->system : flight.flight.frame.system;
    if (active.craft != flight.origin.state.craft || flight.moon ||
        flight.minor || active.source != system)
      return std::unexpected{failure("active star search owner mismatch")};
  }
  return {};
}
auto encode_freedom_knowledge_document_json(
    const FreedomKnowledgeSaveDocument& d)
    -> std::expected<std::string, SaveSchemaError> {
  if (auto v = validate_freedom_knowledge_document(d); !v)
    return std::unexpected{v.error()};
  auto voyage = encode_freedom_resource_document_json(d.voyage);
  auto knowledge =
      encode_freedom_knowledge_json(d.knowledge, d.voyage.resources.tick);
  if (!voyage) return std::unexpected{voyage.error()};
  if (!knowledge) return std::unexpected{knowledge.error()};
  Json root{{"format_version", kFreedomKnowledgeSaveFormatVersion},
            {"voyage", Json::parse(*voyage)},
            {"knowledge", Json::parse(*knowledge)}};
  auto text = root.dump(2) + '\n';
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{failure("knowledge save exceeds limit")};
  return text;
}
auto decode_freedom_knowledge_document_json(std::string_view text)
    -> std::expected<FreedomKnowledgeSaveDocument, SaveSchemaError> {
  auto root = parse(text, kMaximumSaveDocumentBytes);
  if (!root) return std::unexpected{root.error()};
  if (!root->is_object() || !root->contains("format_version") ||
      !(*root)["format_version"].is_number_unsigned())
    return std::unexpected{failure("unsigned format version required")};
  if ((*root)["format_version"] != kFreedomKnowledgeSaveFormatVersion)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::unsupported_format_version,
                        "$.format_version", "unsupported knowledge format"}};
  if (!fields(*root, {"format_version", "voyage", "knowledge"}))
    return std::unexpected{failure("invalid closed knowledge save shape")};
  auto voyage = decode_freedom_resource_document_json((*root)["voyage"].dump());
  if (!voyage) return std::unexpected{voyage.error()};
  auto knowledge = decode_freedom_knowledge_json((*root)["knowledge"].dump(),
                                                 voyage->resources.tick);
  if (!knowledge) return std::unexpected{knowledge.error()};
  FreedomKnowledgeSaveDocument d{std::move(*voyage), std::move(*knowledge)};
  if (auto v = validate_freedom_knowledge_document(d); !v)
    return std::unexpected{v.error()};
  return d;
}
} // namespace apsis_drift
