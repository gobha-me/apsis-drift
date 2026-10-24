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
auto repair_source_digest(const CraftCondition&, const CraftRepairReceipt&)
    -> std::uint64_t;
auto valid_impact(const CraftImpactReceipt& r, const CraftCondition& c,
                  SimulationTick now) -> bool {
  const auto& w = r.witness;
  return r.serial && r.serial != std::numeric_limits<std::uint64_t>::max() &&
         valid_witness(w) && w.craft == c.craft && w.craft_frame == c.frame &&
         w.tick <= now &&
         r.damage_class == damage_class(w.closing_speed_metres_per_second) &&
         r.catastrophic == (w.closing_speed_metres_per_second > 60);
}
auto valid_condition(const CraftCondition& c) -> bool {
  if ((c.version != kCraftConditionVersion &&
       c.version != kRepairedCraftConditionVersion) ||
      c.frame != wayfarer_frame().recipe || c.forebody > 3 ||
      c.landing_structure > 3 || c.clear_ticks > 1 ||
      (c.contact_armed && c.clear_ticks) || !c.next_event ||
      c.tick >= std::numeric_limits<SimulationTick>::max() - 1 ||
      (c.lineage && (c.lineage->version != 1 || !c.lineage->generation)))
    return false;
  if (c.version == kCraftConditionVersion && c.latest_repair) return false;
  if (!c.latest)
    return c.version == kCraftConditionVersion && c.next_event == 1 &&
           !c.forebody && !c.landing_structure && c.contact_armed &&
           !c.clear_ticks;
  const auto& r = *c.latest;
  if (!valid_impact(r, c, c.tick) || c.next_event != r.serial + 1) return false;
  const auto area = r.witness.region == CraftImpactRegion::forebody
                        ? c.forebody
                        : c.landing_structure;
  if (c.version == kCraftConditionVersion) return area >= r.damage_class;
  if (!c.latest_repair) return false;
  const auto& repair = *c.latest_repair;
  const auto& source = repair.before;
  const auto& attachment = repair.attachment;
  const auto units = static_cast<unsigned>(source.forebody) +
                     static_cast<unsigned>(source.landing_structure);
  const auto source_area =
      source.latest.witness.region == CraftImpactRegion::forebody
          ? source.forebody
          : source.landing_structure;
  if (repair.version != kCraftRepairReceiptVersion ||
      attachment.geometry_version != 1 || !attachment.port.station.value ||
      attachment.port.ordinal < 1 || attachment.port.ordinal > 2 ||
      attachment.craft != c.frame || attachment.tick > c.tick ||
      source.forebody > 3 || source.landing_structure > 3 || !units ||
      units > kCraftOriginRepairMaximumUnits || source.clear_ticks > 1 ||
      (source.contact_armed && source.clear_ticks) ||
      !valid_impact(source.latest, c, attachment.tick) ||
      source.latest.catastrophic ||
      source.next_event != source.latest.serial + 1 ||
      source_area < source.latest.damage_class)
    return false;
  const auto fee = kCraftOriginRepairUnitCredits * units;
  if (repair.payment.paid) {
    if (repair.credits != fee ||
        repair.payment.started_tick > attachment.tick ||
        repair.payment.spent_after < fee)
      return false;
  } else if (repair.credits || repair.payment.started_tick ||
             repair.payment.spent_after) {
    return false;
  }
  if (repair.source_projection_digest != repair_source_digest(c, repair))
    return false;
  if (r.serial == source.latest.serial)
    return r == source.latest && c.next_event == source.next_event &&
           !c.forebody && !c.landing_structure &&
           (c.tick != attachment.tick ||
            (c.contact_armed == source.contact_armed &&
             c.clear_ticks == source.clear_ticks));
  return r.serial > source.latest.serial && r.witness.tick > attachment.tick &&
         area >= r.damage_class;
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
auto impact_json(const CraftImpactReceipt& r) -> Json {
  return {
      {"serial", std::to_string(r.serial)},
      {"source_condition_digest", std::to_string(r.source_condition_digest)},
      {"witness", witness_json(r.witness)},
      {"damage_class", r.damage_class},
      {"catastrophic", r.catastrophic}};
}
auto read_impact(const Json& j) -> std::optional<CraftImpactReceipt> {
  if (!fields(j, {"serial", "source_condition_digest", "witness",
                  "damage_class", "catastrophic"}) ||
      !word(j["serial"]) || !word(j["source_condition_digest"]) ||
      !uint(j["damage_class"], 3) || !j["catastrophic"].is_boolean())
    return {};
  const auto witness = read_witness(j["witness"]);
  if (!witness) return {};
  return CraftImpactReceipt{
      *word(j["serial"]), *word(j["source_condition_digest"]), *witness,
      j["damage_class"].get<std::uint8_t>(), j["catastrophic"].get<bool>()};
}
auto repair_json(const CraftRepairReceipt& r, bool include_digest) -> Json {
  Json result{
      {"version", r.version},
      {"attachment",
       {{"geometry_version", r.attachment.geometry_version},
        {"station", std::to_string(r.attachment.port.station.value)},
        {"port", r.attachment.port.ordinal},
        {"frame_id", r.attachment.craft.id.value},
        {"frame_version", r.attachment.craft.version},
        {"tick", std::to_string(r.attachment.tick)}}},
      {"service_pose_checksum", std::to_string(r.service_pose_checksum)},
      {"before",
       {{"forebody", r.before.forebody},
        {"landing_structure", r.before.landing_structure},
        {"contact_armed", r.before.contact_armed},
        {"clear_ticks", r.before.clear_ticks},
        {"next_event", std::to_string(r.before.next_event)},
        {"latest", impact_json(r.before.latest)}}},
      {"paid", r.payment.paid},
      {"credits", std::to_string(r.credits)},
      {"paid_started_tick", std::to_string(r.payment.started_tick)},
      {"paid_spent_after", std::to_string(r.payment.spent_after)}};
  if (include_digest)
    result["source_projection_digest"] =
        std::to_string(r.source_projection_digest);
  return result;
}
auto repair_source_digest(const CraftCondition& c, const CraftRepairReceipt& r)
    -> std::uint64_t {
  Json lineage = nullptr;
  if (c.lineage)
    lineage = {{"version", c.lineage->version},
               {"generation", std::to_string(c.lineage->generation)}};
  const auto text = Json{{"domain", "apsis.craft-origin-repair-source/1"},
                         {"craft", std::to_string(c.craft.value)},
                         {"frame_id", c.frame.id.value},
                         {"frame_version", c.frame.version},
                         {"lineage", lineage},
                         {"repair", repair_json(r, false)}}
                        .dump();
  std::uint64_t hash{14695981039346656037ULL};
  for (unsigned char b : text) {
    hash ^= b;
    hash *= 1099511628211ULL;
  }
  return hash;
}
auto read_repair(const Json& j) -> std::optional<CraftRepairReceipt> {
  if (!fields(j, {"version", "attachment", "service_pose_checksum", "before",
                  "paid", "credits", "paid_started_tick", "paid_spent_after",
                  "source_projection_digest"}) ||
      !uint(j["version"], std::numeric_limits<std::uint32_t>::max()) ||
      !j["paid"].is_boolean())
    return {};
  const auto& a = j["attachment"];
  const auto& b = j["before"];
  if (!fields(a, {"geometry_version", "station", "port", "frame_id",
                  "frame_version", "tick"}) ||
      !fields(b, {"forebody", "landing_structure", "contact_armed",
                  "clear_ticks", "next_event", "latest"}))
    return {};
  for (auto key : {"geometry_version", "port", "frame_id", "frame_version"})
    if (!uint(a[key], std::numeric_limits<std::uint32_t>::max())) return {};
  for (auto key : {"forebody", "landing_structure", "clear_ticks"})
    if (!uint(b[key], 3)) return {};
  const auto station = word(a["station"]), tick = word(a["tick"]),
             pose = word(j["service_pose_checksum"]),
             next = word(b["next_event"]),
             digest = word(j["source_projection_digest"]),
             credits = word(j["credits"]),
             started = word(j["paid_started_tick"]),
             spent = word(j["paid_spent_after"]);
  const auto latest = read_impact(b["latest"]);
  if (!station || !tick || !pose || !next || !digest || !credits || !started ||
      !spent || !latest || !b["contact_armed"].is_boolean())
    return {};
  CraftRepairReceipt result;
  result.version = j["version"].get<std::uint32_t>();
  result.attachment = {
      a["geometry_version"].get<std::uint32_t>(),
      {OriginStationId{*station}, a["port"].get<std::uint32_t>()},
      {{a["frame_id"].get<std::uint32_t>()},
       a["frame_version"].get<std::uint32_t>()},
      *tick};
  result.service_pose_checksum = *pose;
  result.before = {b["forebody"].get<std::uint8_t>(),
                   b["landing_structure"].get<std::uint8_t>(),
                   b["contact_armed"].get<bool>(),
                   b["clear_ticks"].get<std::uint8_t>(),
                   *next,
                   *latest};
  result.source_projection_digest = *digest;
  result.payment = {j["paid"].get<bool>(), *started, *spent};
  result.credits = *credits;
  return result;
}
auto validate_impact_owner(const TerrainImpactWitness& w,
                           const FreedomFlightSaveDocument& d)
    -> std::expected<void, std::string> {
  const auto ids =
      generate_first_intersystem_identities(d.origin.recipe.universe_seed);
  // Retained planetary impacts keep their own owner while visiting another
  // body.
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
  return {};
}
auto service_pose_checksum(Seed seed, const OriginDockConstraint& attachment)
    -> std::expected<std::uint64_t, std::string> {
  const auto system = generate_physical_origin_system(seed, 2);
  const auto station = generate_origin_station(seed);
  const auto geometry = origin_station_geometry(station);
  if (!system || !geometry)
    return std::unexpected("Repair Origin owner or geometry is unavailable");
  const auto body =
      release_origin_port(*system, station, *geometry, attachment);
  if (!body)
    return std::unexpected("Repair attachment is not a canonical Origin port");
  const auto checksum =
      rigid_body_state_checksum(RigidBodyWorldContext{*system}, *body);
  if (!checksum)
    return std::unexpected("Repair service pose is not a canonical rigid body");
  return *checksum;
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
    if (const auto valid = validate_impact_owner(c.latest->witness, d); !valid)
      return valid;
  }
  if (c.latest_repair) {
    const auto& repair = *c.latest_repair;
    if (const auto valid =
            validate_impact_owner(repair.before.latest.witness, d);
        !valid)
      return valid;
    const auto checksum =
        service_pose_checksum(d.origin.recipe.universe_seed, repair.attachment);
    if (!checksum || *checksum != repair.service_pose_checksum)
      return std::unexpected(
          "Repair historical Origin pose checksum is invalid");
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
  if (c.latest) latest = impact_json(*c.latest);
  Json result{{"version", c.version},
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
              {"latest", latest}};
  if (c.latest_repair)
    result["latest_repair"] = repair_json(*c.latest_repair, true);
  return result.dump();
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
    if (duplicate || !j.is_object() || !j.contains("version") ||
        !uint(j["version"], std::numeric_limits<std::uint32_t>::max()))
      return std::unexpected(failure("Invalid condition shape or version"));
    const auto version = j["version"].get<std::uint32_t>();
    const bool valid_shape =
        version == kCraftConditionVersion
            ? fields(j,
                     {"version", "craft", "frame_id", "frame_version",
                      "lineage", "tick", "forebody", "landing_structure",
                      "contact_armed", "clear_ticks", "next_event", "latest"})
            : version == kRepairedCraftConditionVersion &&
                  fields(j, {"version", "craft", "frame_id", "frame_version",
                             "lineage", "tick", "forebody", "landing_structure",
                             "contact_armed", "clear_ticks", "next_event",
                             "latest", "latest_repair"});
    if (!valid_shape)
      return std::unexpected(
          failure("Invalid closed condition shape or version"));
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
    if (!j["latest"].is_null()) {
      c.latest = read_impact(j["latest"]);
      if (!c.latest) return std::unexpected(failure("Invalid impact receipt"));
    }
    if (version == kRepairedCraftConditionVersion) {
      c.latest_repair = read_repair(j["latest_repair"]);
      if (!c.latest_repair)
        return std::unexpected(failure("Invalid closed repair receipt"));
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
auto craft_origin_repair_price(const CraftCondition& c)
    -> std::expected<std::uint64_t, std::string> {
  if (!valid_condition(c) || !c.latest || c.latest->catastrophic ||
      (!c.forebody && !c.landing_structure))
    return std::unexpected("Repair requires valid noncatastrophic damage");
  return kCraftOriginRepairUnitCredits *
         (static_cast<std::uint64_t>(c.forebody) + c.landing_structure);
}
auto repair_craft_condition(const CraftCondition& c,
                            const FreedomDockingSaveDocument& docking,
                            CraftRepairPayment payment)
    -> std::expected<CraftCondition, std::string> {
  const auto price = craft_origin_repair_price(c);
  if (!price) return std::unexpected(price.error());
  if (!docking.docking.attached)
    return std::unexpected("Repair requires an actual Origin attachment");
  if (const auto valid = validate_freedom_docking_document(docking); !valid)
    return std::unexpected(valid.error().detail);
  if (const auto valid = validate_craft_condition(c, docking.flight); !valid)
    return std::unexpected(valid.error());
  if ((payment.paid &&
       (payment.started_tick > c.tick || payment.spent_after < *price)) ||
      (!payment.paid && (payment.started_tick || payment.spent_after)))
    return std::unexpected("Repair payment anchors do not cover the service");
  CraftRepairReceipt receipt;
  receipt.attachment = {docking.docking.geometry_version,
                        docking.docking.target, c.frame, c.tick};
  const auto checksum = service_pose_checksum(
      docking.flight.origin.recipe.universe_seed, receipt.attachment);
  if (!checksum) return std::unexpected(checksum.error());
  receipt.service_pose_checksum = *checksum;
  receipt.before = {c.forebody,    c.landing_structure, c.contact_armed,
                    c.clear_ticks, c.next_event,        *c.latest};
  receipt.payment = payment;
  receipt.credits = payment.paid ? *price : 0;
  receipt.source_projection_digest = repair_source_digest(c, receipt);
  auto result = c;
  result.version = kRepairedCraftConditionVersion;
  result.forebody = 0;
  result.landing_structure = 0;
  result.latest_repair = receipt;
  if (const auto valid = validate_craft_condition(result, docking.flight);
      !valid)
    return std::unexpected(valid.error());
  return result;
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
