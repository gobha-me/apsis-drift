#include "apsis_drift/freedom_knowledge_save.hpp"

#include <array>
#include <charconv>
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
      r.version == kFreedomTravelKnowledgeVersion)
    root["recipe"]["observation_policy"] = r.observation_policy;
  if (r.version == kFreedomTravelKnowledgeVersion)
    root["recipe"]["world_domain"] = r.world_domain;
  if (r.craft_lineage)
    root["recipe"]["craft_lineage"] = {
        {"version", r.craft_lineage->version},
        {"generation", std::to_string(r.craft_lineage->generation)}};
  auto text = root.dump(2) + '\n';
  if (text.size() > kMaximumFreedomKnowledgeBytes)
    return std::unexpected{failure("knowledge encoding exceeds bounded size")};
  return text;
}
auto decode_freedom_knowledge_json(std::string_view text, SimulationTick now)
    -> std::expected<FreedomKnowledge, SaveSchemaError> {
  auto root = parse(text, kMaximumFreedomKnowledgeBytes);
  if (!root) return std::unexpected{root.error()};
  if (!fields(*root, {"recipe", "entries"}) || !(*root)["recipe"].is_object())
    return std::unexpected{failure("invalid closed knowledge shape")};
  const auto& r = (*root)["recipe"];
  const bool travel = r.contains("version") &&
                      r["version"].is_number_unsigned() &&
                      r["version"] == kFreedomTravelKnowledgeVersion;
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
    if (!r["world_domain"].is_number_unsigned() || r["world_domain"] != 1)
      return std::unexpected{failure("unsupported travel knowledge domain")};
    k.recipe.world_domain = 1;
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
  const auto& entries = (*root)["entries"];
  if (!entries.is_array() || entries.size() > kMaximumFreedomKnowledgeFacts)
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
      !validate_freedom_knowledge(d.knowledge, flight.flight.tick) ||
      (r.version == kFreedomTravelKnowledgeVersion) != flight.world.has_value())
    return std::unexpected{
        failure("knowledge owner, recipe or clock mismatch")};
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
