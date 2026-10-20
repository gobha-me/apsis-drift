#include "apsis_drift/craft_condition.hpp"
#include "apsis_drift/universe_navigation.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_set>

namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;
auto failure(std::string message) -> SaveSchemaError {
  return {SaveSchemaErrorCode::invalid_state, "$.condition",
          std::move(message)};
}
auto fields(const Json& j, std::initializer_list<std::string_view> names)
    -> bool {
  if (!j.is_object() || j.size() != names.size()) return false;
  return std::ranges::all_of(
      names, [&](auto n) { return j.contains(std::string(n)); });
}
auto uint(const Json& j, std::uint64_t maximum) -> bool {
  return j.is_number_unsigned() && j.get<std::uint64_t>() <= maximum;
}
auto word(const Json& j) -> std::optional<std::uint64_t> {
  if (!j.is_string()) return {};
  const auto& s = j.get_ref<const std::string&>();
  std::uint64_t value{};
  const auto p = std::from_chars(s.data(), s.data() + s.size(), value);
  if (s.empty() || (s.size() > 1 && s.front() == '0') || p.ec != std::errc{} ||
      p.ptr != s.data() + s.size())
    return {};
  return value;
}
auto vector_json(RigidVector3 v) -> Json {
  return Json::array({v.x, v.y, v.z});
}
auto vector_value(const Json& j) -> std::optional<RigidVector3> {
  if (!j.is_array() || j.size() != 3) return {};
  for (const auto& v : j)
    if (!v.is_number() || !std::isfinite(v.get<double>())) return {};
  return RigidVector3{j[0].get<double>(), j[1].get<double>(),
                      j[2].get<double>()};
}
auto finite_vector(RigidVector3 v) -> bool {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
         std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)}) <= 1e9;
}
auto damage_class(double speed) -> std::uint8_t {
  return speed <= 2 ? 0 : speed <= 8 ? 1 : speed <= 20 ? 2 : 3;
}
auto valid_witness(const TerrainImpactWitness& w) -> bool {
  const auto& t = w.triangle;
  const auto n = w.outward_normal_planet_fixed;
  const auto v = w.pre_response_velocity_planet_fixed;
  const double norm = n.x * n.x + n.y * n.y + n.z * n.z;
  const double closing = std::max(0.0, -(v.x * n.x + v.y * n.y + v.z * n.z));
  return w.version == 1 &&
         (w.terrain_policy == 1 || w.terrain_policy == 2 ||
          w.terrain_policy == 3) &&
         w.collider_policy == 1 && w.craft_frame == wayfarer_frame().recipe &&
         w.tick > 0 &&
         w.tick < std::numeric_limits<SimulationTick>::max() - 1 &&
         std::isfinite(w.fraction) && w.fraction >= 0 && w.fraction <= 1 &&
         t.planet == w.planet && static_cast<unsigned>(t.face) <= 5 &&
         t.tile_x < (1U << 13U) && t.tile_y < (1U << 13U) && t.cell_x < 32 &&
         t.cell_y < 32 && t.half <= 1 &&
         (w.region == CraftImpactRegion::forebody ||
          w.region == CraftImpactRegion::landing_structure) &&
         w.support_index < 4 &&
         (w.region != CraftImpactRegion::forebody || w.support_index == 0) &&
         finite_vector(w.contact_point_craft_metres) && finite_vector(n) &&
         finite_vector(v) && std::abs(norm - 1.0) <= 1e-6 &&
         std::isfinite(w.closing_speed_metres_per_second) &&
         w.closing_speed_metres_per_second >= 0 &&
         std::abs(w.closing_speed_metres_per_second - closing) <=
             1e-6 * std::max(1.0, closing);
}
auto valid_condition(const CraftCondition& c) -> bool {
  if (c.version != 1 || c.frame != wayfarer_frame().recipe || c.forebody > 3 ||
      c.landing_structure > 3 || c.clear_ticks > 1 ||
      (c.contact_armed && c.clear_ticks) || !c.next_event ||
      c.tick >= std::numeric_limits<SimulationTick>::max() - 1 ||
      (c.lineage && (c.lineage->version != 1 || !c.lineage->generation)))
    return false;
  if (!c.latest)
    return c.next_event == 1 && !c.forebody && !c.landing_structure &&
           c.contact_armed && !c.clear_ticks;
  const auto& r = *c.latest;
  const auto& w = r.witness;
  if (!r.serial || r.serial == std::numeric_limits<std::uint64_t>::max() ||
      c.next_event != r.serial + 1 || !valid_witness(w) || w.craft != c.craft ||
      w.craft_frame != c.frame || w.tick > c.tick ||
      r.damage_class != damage_class(w.closing_speed_metres_per_second) ||
      r.catastrophic != (w.closing_speed_metres_per_second > 60))
    return false;
  const auto area = w.region == CraftImpactRegion::forebody
                        ? c.forebody
                        : c.landing_structure;
  return area >= r.damage_class;
}
auto witness_json(const TerrainImpactWitness& w) -> Json {
  const auto& t = w.triangle;
  return {
      {"version", w.version},
      {"terrain_policy", w.terrain_policy},
      {"collider_policy", w.collider_policy},
      {"craft", std::to_string(w.craft.value)},
      {"frame_id", w.craft_frame.id.value},
      {"frame_version", w.craft_frame.version},
      {"system", std::to_string(w.system.value)},
      {"planet", std::to_string(w.planet.value)},
      {"tick", std::to_string(w.tick)},
      {"source_checksum", std::to_string(w.source_checksum)},
      {"candidate_checksum", std::to_string(w.candidate_checksum)},
      {"fraction", w.fraction},
      {"triangle",
       {{"face", static_cast<unsigned>(t.face)},
        {"tile_x", t.tile_x},
        {"tile_y", t.tile_y},
        {"cell_x", t.cell_x},
        {"cell_y", t.cell_y},
        {"half", t.half}}},
      {"region", static_cast<unsigned>(w.region)},
      {"support_index", w.support_index},
      {"contact_point_craft_metres", vector_json(w.contact_point_craft_metres)},
      {"outward_normal_planet_fixed",
       vector_json(w.outward_normal_planet_fixed)},
      {"pre_response_velocity_planet_fixed",
       vector_json(w.pre_response_velocity_planet_fixed)},
      {"closing_speed_metres_per_second", w.closing_speed_metres_per_second}};
}
auto read_witness(const Json& j) -> std::optional<TerrainImpactWitness> {
  if (!fields(j, {"version", "terrain_policy", "collider_policy", "craft",
                  "frame_id", "frame_version", "system", "planet", "tick",
                  "source_checksum", "candidate_checksum", "fraction",
                  "triangle", "region", "support_index",
                  "contact_point_craft_metres", "outward_normal_planet_fixed",
                  "pre_response_velocity_planet_fixed",
                  "closing_speed_metres_per_second"}))
    return {};
  for (auto k : {"version", "terrain_policy", "collider_policy", "frame_id",
                 "frame_version", "region", "support_index"})
    if (!uint(j[k], std::numeric_limits<std::uint32_t>::max())) return {};
  const auto craft = word(j["craft"]), system = word(j["system"]),
             planet = word(j["planet"]), tick = word(j["tick"]),
             source = word(j["source_checksum"]),
             candidate = word(j["candidate_checksum"]);
  const auto point = vector_value(j["contact_point_craft_metres"]),
             normal = vector_value(j["outward_normal_planet_fixed"]),
             velocity = vector_value(j["pre_response_velocity_planet_fixed"]);
  const auto& t = j["triangle"];
  if (!craft || !system || !planet || !tick || !source || !candidate ||
      !point || !normal || !velocity ||
      !fields(t, {"face", "tile_x", "tile_y", "cell_x", "cell_y", "half"}))
    return {};
  for (auto k : {"face", "tile_x", "tile_y", "cell_x", "cell_y", "half"})
    if (!uint(t[k], std::numeric_limits<std::uint32_t>::max())) return {};
  if (!uint(t["face"], 5) || !uint(j["region"], 2) ||
      !uint(j["support_index"], 3) || !j["fraction"].is_number() ||
      !j["closing_speed_metres_per_second"].is_number())
    return {};
  TerrainImpactWitness w;
  w.version = j["version"].get<std::uint32_t>();
  w.terrain_policy = j["terrain_policy"].get<std::uint32_t>();
  w.collider_policy = j["collider_policy"].get<std::uint32_t>();
  w.craft = {*craft};
  w.craft_frame = {{j["frame_id"].get<std::uint32_t>()},
                   j["frame_version"].get<std::uint32_t>()};
  w.system = {*system};
  w.planet = {*planet};
  w.tick = *tick;
  w.source_checksum = *source;
  w.candidate_checksum = *candidate;
  w.fraction = j["fraction"].get<double>();
  w.triangle = {w.planet,
                static_cast<CubeFace>(t["face"].get<unsigned>()),
                t["tile_x"].get<std::uint32_t>(),
                t["tile_y"].get<std::uint32_t>(),
                t["cell_x"].get<std::uint32_t>(),
                t["cell_y"].get<std::uint32_t>(),
                t["half"].get<std::uint32_t>()};
  w.region = static_cast<CraftImpactRegion>(j["region"].get<unsigned>());
  w.support_index = j["support_index"].get<std::uint8_t>();
  w.contact_point_craft_metres = *point;
  w.outward_normal_planet_fixed = *normal;
  w.pre_response_velocity_planet_fixed = *velocity;
  w.closing_speed_metres_per_second =
      j["closing_speed_metres_per_second"].get<double>();
  if (!valid_witness(w)) return {};
  return w;
}
} // namespace

auto make_craft_condition(const FreedomFlightSaveDocument& d)
    -> std::expected<CraftCondition, std::string> {
  CraftCondition c;
  c.craft = d.origin.state.craft;
  c.frame = d.flight.craft;
  c.lineage = d.origin.lineage;
  c.tick = d.flight.tick;
  if (auto v = validate_craft_condition(c, d); !v)
    return std::unexpected(v.error());
  return c;
}
auto validate_craft_condition(const CraftCondition& c,
                              const FreedomFlightSaveDocument& d)
    -> std::expected<void, std::string> {
  if (!valid_condition(c) || c.craft != d.origin.state.craft ||
      c.frame != d.flight.craft || c.lineage != d.origin.lineage ||
      c.tick != d.flight.tick || d.model.physical_catalog != 2 ||
      d.model.physical_ephemeris != 2 || !hydrate_freedom_flight_document(d))
    return std::unexpected(
        "Condition owner, tick, recipe or receipt is invalid");
  if (c.latest) {
    const auto& w = c.latest->witness;
    const auto ids =
        generate_first_intersystem_identities(d.origin.recipe.universe_seed);
    // An actual Moon has its own terrain recipe. A retained planetary receipt
    // continues to name its original, independently authenticated planet below;
    // the Moon's unused planetary model default does not reinterpret it.
    if ((!d.moon && !d.minor && w.terrain_policy != d.model.terrain_policy) ||
        (w.system != ids.origin_system && w.system != ids.target_system))
      return std::unexpected(
          "Impact receipt has an unknown physical owner or terrain selection");
    const auto source =
        w.system == ids.origin_system
            ? generate_physical_origin_system(d.origin.recipe.universe_seed, 2)
            : generate_physical_local_system(ids.target_system_seed, 2);
    if (!source || !find_local_system_planet(*source, w.planet))
      return std::unexpected(
          "Impact receipt planet is outside its complete physical owner");
  }
  return {};
}
auto encode_craft_condition_json(const CraftCondition& c)
    -> std::expected<std::string, SaveSchemaError> {
  if (!valid_condition(c))
    return std::unexpected(failure("Invalid condition record"));
  Json lineage = nullptr, latest = nullptr;
  if (c.lineage)
    lineage = {{"version", c.lineage->version},
               {"generation", std::to_string(c.lineage->generation)}};
  if (c.latest)
    latest = {{"serial", std::to_string(c.latest->serial)},
              {"source_condition_digest",
               std::to_string(c.latest->source_condition_digest)},
              {"witness", witness_json(c.latest->witness)},
              {"damage_class", c.latest->damage_class},
              {"catastrophic", c.latest->catastrophic}};
  return Json{{"version", c.version},
              {"craft", std::to_string(c.craft.value)},
              {"frame_id", c.frame.id.value},
              {"frame_version", c.frame.version},
              {"lineage", lineage},
              {"tick", std::to_string(c.tick)},
              {"forebody", c.forebody},
              {"landing_structure", c.landing_structure},
              {"contact_armed", c.contact_armed},
              {"clear_ticks", c.clear_ticks},
              {"next_event", std::to_string(c.next_event)},
              {"latest", latest}}
      .dump();
}
auto decode_craft_condition_json(std::string_view text)
    -> std::expected<CraftCondition, SaveSchemaError> {
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected(failure("Condition exceeds save limit"));
  bool duplicate{};
  std::vector<std::unordered_set<std::string>> keys;
  const Json::parser_callback_t cb = [&](int depth, Json::parse_event_t event,
                                         Json& value) {
    if (depth > 64) throw std::runtime_error("Condition nesting exceeds bound");
    if (event == Json::parse_event_t::object_start) keys.emplace_back();
    if (event == Json::parse_event_t::key &&
        (keys.empty() || !keys.back().insert(value.get<std::string>()).second))
      duplicate = true;
    if (event == Json::parse_event_t::object_end && !keys.empty())
      keys.pop_back();
    return true;
  };
  try {
    const auto j = Json::parse(text.begin(), text.end(), cb);
    if (duplicate ||
        !fields(j, {"version", "craft", "frame_id", "frame_version", "lineage",
                    "tick", "forebody", "landing_structure", "contact_armed",
                    "clear_ticks", "next_event", "latest"}))
      return std::unexpected(
          failure("Invalid closed condition shape or duplicate key"));
    for (auto k : {"version", "frame_id", "frame_version"})
      if (!uint(j[k], std::numeric_limits<std::uint32_t>::max()))
        return std::unexpected(
            failure("Condition version/frame requires unsigned u32"));
    for (auto k : {"forebody", "landing_structure", "clear_ticks"})
      if (!uint(j[k], 3))
        return std::unexpected(
            failure("Condition severity/gate outside bounds"));
    const auto craft = word(j["craft"]), tick = word(j["tick"]),
               next = word(j["next_event"]);
    if (!craft || !tick || !next || !j["contact_armed"].is_boolean())
      return std::unexpected(failure("Invalid condition identity/gate"));
    CraftCondition c;
    c.version = j["version"].get<std::uint32_t>();
    c.craft = {*craft};
    c.frame = {{j["frame_id"].get<std::uint32_t>()},
               j["frame_version"].get<std::uint32_t>()};
    c.tick = *tick;
    c.forebody = j["forebody"].get<std::uint8_t>();
    c.landing_structure = j["landing_structure"].get<std::uint8_t>();
    c.contact_armed = j["contact_armed"].get<bool>();
    c.clear_ticks = j["clear_ticks"].get<std::uint8_t>();
    c.next_event = *next;
    const auto& l = j["lineage"];
    if (!l.is_null()) {
      if (!fields(l, {"version", "generation"}) || !uint(l["version"], 1) ||
          !word(l["generation"]))
        return std::unexpected(failure("Invalid condition lineage"));
      c.lineage = FreedomCraftLineage{l["version"].get<std::uint32_t>(),
                                      *word(l["generation"])};
    }
    const auto& r = j["latest"];
    if (!r.is_null()) {
      if (!fields(r, {"serial", "source_condition_digest", "witness",
                      "damage_class", "catastrophic"}) ||
          !word(r["serial"]) || !word(r["source_condition_digest"]) ||
          !uint(r["damage_class"], 3) || !r["catastrophic"].is_boolean())
        return std::unexpected(failure("Invalid impact receipt"));
      const auto w = read_witness(r["witness"]);
      if (!w) return std::unexpected(failure("Invalid impact witness"));
      c.latest = CraftImpactReceipt{
          *word(r["serial"]), *word(r["source_condition_digest"]), *w,
          r["damage_class"].get<std::uint8_t>(), r["catastrophic"].get<bool>()};
    }
    if (!valid_condition(c))
      return std::unexpected(failure("Inconsistent condition record"));
    return c;
  } catch (const std::exception& e) {
    return std::unexpected(failure(e.what()));
  }
}
auto craft_condition_digest(const CraftCondition& c)
    -> std::expected<std::uint64_t, std::string> {
  const auto text = encode_craft_condition_json(c);
  if (!text) return std::unexpected(text.error().detail);
  std::uint64_t hash = 14695981039346656037ULL;
  for (unsigned char b : *text) {
    hash ^= b;
    hash *= 1099511628211ULL;
  }
  return hash;
}
auto reduce_craft_condition(const CraftCondition& c,
                            const FreedomFlightSaveDocument& before,
                            const FreedomFlightSaveDocument& candidate,
                            const TerrainImpactSweep& sweep)
    -> std::expected<CraftConditionReduction, std::string> {
  if (!std::isfinite(sweep.minimum_clearance_metres) ||
      (sweep.classification != TerrainImpactClass::clear &&
       sweep.minimum_clearance_metres != 0))
    return std::unexpected("Invalid or contradictory sweep clearance");
  if (sweep.version != 1 || candidate.flight.tick != before.flight.tick + 1 ||
      before.flight.tick >= std::numeric_limits<SimulationTick>::max() - 2 ||
      before.origin.recipe != candidate.origin.recipe ||
      before.origin.state.craft != candidate.origin.state.craft ||
      before.origin.lineage != candidate.origin.lineage ||
      before.flight.craft != candidate.flight.craft ||
      before.flight.frame != candidate.flight.frame ||
      before.model != candidate.model || before.world != candidate.world ||
      !hydrate_freedom_flight_document(before) ||
      !hydrate_freedom_flight_document(candidate))
    return std::unexpected(
        "Invalid consecutive impact owner or physical candidate");
  if (sweep.classification == TerrainImpactClass::impact) {
    if (!sweep.witness || !valid_witness(*sweep.witness))
      return std::unexpected("Invalid certified impact witness");
    const auto& w = *sweep.witness;
    const auto h = hydrate_freedom_flight_document(before);
    const RigidBodyWorldContext context{h->system};
    if (w.craft != c.craft || w.craft_frame != c.frame ||
        w.system != before.flight.frame.system ||
        w.planet != before.flight.frame.planet ||
        w.terrain_policy != before.model.terrain_policy ||
        w.tick != candidate.flight.tick ||
        rigid_body_state_checksum(context, before.flight) !=
            w.source_checksum ||
        rigid_body_state_checksum(context, candidate.flight) !=
            w.candidate_checksum)
      return std::unexpected(
          "Impact witness is not bound to before/candidate states");
    if (c.latest && c.latest->witness == w && c.tick == candidate.flight.tick) {
      if (!validate_craft_condition(c, candidate))
        return std::unexpected("Invalid duplicate condition state");
      return CraftConditionReduction{c, false, c.latest->catastrophic};
    }
  } else if (sweep.witness ||
             (sweep.classification != TerrainImpactClass::clear &&
              sweep.classification != TerrainImpactClass::unknown &&
              sweep.classification != TerrainImpactClass::near))
    return std::unexpected(
        "Unsupported or contradictory impact classification");
  if (!validate_craft_condition(c, before))
    return std::unexpected("Condition is not the current pre-step owner");
  auto out = c;
  out.tick = candidate.flight.tick;
  if (sweep.classification == TerrainImpactClass::clear) {
    if (!std::isfinite(sweep.minimum_clearance_metres) ||
        sweep.minimum_clearance_metres < kTerrainImpactClearMarginMetres)
      return std::unexpected("Clear sweep lacks the positive rearm margin");
    if (!out.contact_armed) {
      ++out.clear_ticks;
      if (out.clear_ticks == 2) {
        out.contact_armed = true;
        out.clear_ticks = 0;
      }
    }
    return CraftConditionReduction{out, false, false};
  }
  out.clear_ticks = 0;
  if (sweep.classification == TerrainImpactClass::unknown ||
      sweep.classification == TerrainImpactClass::near)
    return CraftConditionReduction{out, false, false};
  if (!out.contact_armed &&
      sweep.witness->closing_speed_metres_per_second <= 60)
    return CraftConditionReduction{out, false, false};
  if (out.next_event == std::numeric_limits<std::uint64_t>::max())
    return std::unexpected("Impact event serial exhausted");
  const auto digest = craft_condition_digest(c);
  if (!digest) return std::unexpected(digest.error());
  const auto& w = *sweep.witness;
  const auto damage = damage_class(w.closing_speed_metres_per_second);
  auto& area = w.region == CraftImpactRegion::forebody ? out.forebody
                                                       : out.landing_structure;
  area = static_cast<std::uint8_t>(std::min<unsigned>(3, area + damage));
  out.contact_armed = false;
  const bool catastrophic = w.closing_speed_metres_per_second > 60;
  out.latest =
      CraftImpactReceipt{out.next_event++, *digest, w, damage, catastrophic};
  return CraftConditionReduction{out, true, catastrophic};
}
} // namespace apsis_drift
