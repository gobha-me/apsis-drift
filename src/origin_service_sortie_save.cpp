#include "apsis_drift/origin_service_sortie_save.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;
auto failure(std::string reason) -> SaveSchemaError {
  return {SaveSchemaErrorCode::invalid_state, "$.assistance.sortie",
          std::move(reason)};
}
auto fields(const Json& v, std::initializer_list<std::string_view> names)
    -> bool {
  if (!v.is_object() || v.size() != names.size()) return false;
  for (const auto n : names)
    if (!v.contains(std::string{n})) return false;
  return true;
}
auto unsigned_fields(const Json& v,
                     std::initializer_list<std::string_view> names) -> bool {
  if (!fields(v, names)) return false;
  for (const auto n : names)
    if (!v[std::string{n}].is_number_unsigned()) return false;
  return true;
}
auto unsigned_value(const Json& v) -> std::uint64_t {
  if (!v.is_number_unsigned())
    throw failure("Expected an unsigned integral sortie value");
  return v.get<std::uint64_t>();
}
auto nesting(std::string_view text) -> bool {
  std::array<char, 32> stack{};
  std::size_t depth{};
  bool quoted{}, escaped{};
  for (const char c : text) {
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
auto encode_body(const PhysicalLocalSystem& owner,
                 const PhysicalMoonDescriptor& moon, const RigidBodyState& body)
    -> Json {
  const auto station = generate_origin_station(*owner.origin_universe_seed);
  const bool lunar =
      body.frame.kind == RigidFrameKind::body_relative_inertial ||
      body.frame.kind == RigidFrameKind::body_fixed;
  const auto encoded = encode_rigid_body_state_json(
      lunar ? RigidBodyWorldContext{moon, &station}
            : RigidBodyWorldContext{owner, &station},
      body);
  if (!encoded) throw failure("Sortie rigid owner cannot encode");
  return Json::parse(*encoded);
}
auto decode_body(const PhysicalLocalSystem& owner,
                 const PhysicalMoonDescriptor& moon, const Json& encoded)
    -> RigidBodyState {
  const auto station = generate_origin_station(*owner.origin_universe_seed);
  const auto text = encoded.dump();
  auto body = decode_rigid_body_state_json(
      RigidBodyWorldContext{owner, &station}, text);
  if (!body)
    body = decode_rigid_body_state_json(RigidBodyWorldContext{moon, &station},
                                        text);
  if (!body) throw failure("Invalid or foreign sortie rigid owner");
  return *body;
}
auto encode_beacon(const PhysicalLocalSystem& owner,
                   const PhysicalMoonDescriptor& moon,
                   const OriginServiceBeacon& b) -> Json {
  Json lineage = nullptr;
  if (b.lineage)
    lineage = {{"version", b.lineage->version},
               {"generation", b.lineage->generation}};
  return {{"version", b.version},
          {"target", b.target.value},
          {"lineage", std::move(lineage)},
          {"anchor",
           {{"version", b.anchor.version},
            {"terrain_version", b.anchor.terrain_version},
            {"rotation_version", b.anchor.rotation_version},
            {"fixed", encode_body(owner, moon, b.anchor.fixed)}}}};
}
auto decode_beacon(const PhysicalLocalSystem& owner,
                   const PhysicalMoonDescriptor& moon, const Json& value)
    -> OriginServiceBeacon {
  if (!fields(value, {"version", "target", "lineage", "anchor"}) ||
      unsigned_value(value["version"]) != 1)
    throw failure("Invalid closed sortie beacon/version");
  OriginServiceBeacon b;
  b.target = {unsigned_value(value["target"])};
  const auto& lineage = value["lineage"];
  if (!lineage.is_null()) {
    if (!unsigned_fields(lineage, {"version", "generation"}) ||
        lineage["version"] != kFreedomCraftLineageVersion)
      throw failure("Invalid closed sortie lineage/version");
    b.lineage = FreedomCraftLineage{kFreedomCraftLineageVersion,
                                    lineage["generation"].get<std::uint64_t>()};
  }
  const auto& anchor = value["anchor"];
  if (!fields(anchor,
              {"version", "terrain_version", "rotation_version", "fixed"}) ||
      unsigned_value(anchor["version"]) != kMoonLandedCraftAnchorVersion ||
      unsigned_value(anchor["terrain_version"]) != kMoonTerrainRecipeVersion ||
      unsigned_value(anchor["rotation_version"]) !=
          kPhysicalMoonRotationVersion)
    throw failure("Invalid closed sortie anchor/version");
  b.anchor.fixed = decode_body(owner, moon, anchor["fixed"]);
  return b;
}
auto encode_bill(const OriginServiceCraftResources& r) -> Json {
  return {{"craft", r.craft.value},
          {"tick", r.tick},
          {"flight_quanta", r.flight_quanta},
          {"cargo_quanta", r.cargo_quanta},
          {"jump_charges", r.jump_charges}};
}
auto decode_bill(const Json& v, const OriginServiceCraftDescriptor& d)
    -> OriginServiceCraftResources {
  if (!unsigned_fields(v, {"craft", "tick", "flight_quanta", "cargo_quanta",
                           "jump_charges"}) ||
      v["craft"] != d.id.value ||
      v["flight_quanta"] > d.flight_capacity_quanta ||
      v["cargo_quanta"] > d.cargo_capacity_quanta ||
      v["jump_charges"] > d.jump_capacity)
    throw failure("Invalid closed sortie resource bill");
  return {d.id, v["tick"].get<SimulationTick>(),
          v["flight_quanta"].get<std::uint64_t>(),
          v["cargo_quanta"].get<std::uint64_t>(),
          v["jump_charges"].get<std::uint32_t>()};
}
} // namespace
auto encode_origin_service_sortie_json(const PhysicalLocalSystem& owner,
                                       const PhysicalMoonDescriptor& moon,
                                       const OriginServiceSortie& s)
    -> std::expected<std::string, SaveSchemaError> {
  if (!validate_origin_service_sortie(owner, moon, s))
    return std::unexpected(
        failure("Invalid sortie owner/phase/clock/commitment"));
  const auto service =
      encode_origin_service_state_json(owner, s.service, &moon);
  if (!service) return std::unexpected(service.error());
  try {
    const auto beacon = encode_beacon(owner, moon, s.beacon);
    Json pending = nullptr, bill = nullptr;
    if (s.pending) {
      const auto& p = *s.pending;
      pending = {{"version", p.version},
                 {"source", encode_body(owner, moon, p.source)},
                 {"source_checksum", p.source_checksum},
                 {"arrival_tick", p.arrival_tick},
                 {"body", encode_body(owner, moon, p.body)},
                 {"beacon", p.beacon ? beacon : Json(nullptr)}};
    }
    if (s.commitment_resources) bill = encode_bill(*s.commitment_resources);
    const Json root{{"version", s.version},
                    {"service", Json::parse(*service)},
                    {"beacon", beacon},
                    {"phase", static_cast<unsigned>(s.phase)},
                    {"start_tick", s.start_tick},
                    {"phase_start_tick", s.phase_start_tick},
                    {"deadline_tick", s.deadline_tick},
                    {"return_requested", s.return_requested},
                    {"pending", std::move(pending)},
                    {"commitment_resources", std::move(bill)}};
    auto text = root.dump(2) + '\n';
    if (text.size() > kMaximumOriginServiceSortieStateBytes)
      return std::unexpected(SaveSchemaError{
          SaveSchemaErrorCode::document_too_large, "$.assistance.sortie",
          "Sortie exceeds fixed state byte bound"});
    return text;
  } catch (const SaveSchemaError& e) {
    return std::unexpected(e);
  } catch (const Json::exception& e) {
    return std::unexpected(failure(e.what()));
  }
}
auto decode_origin_service_sortie_json(const PhysicalLocalSystem& owner,
                                       const PhysicalMoonDescriptor& moon,
                                       std::string_view text)
    -> std::expected<OriginServiceSortie, SaveSchemaError> {
  if (text.size() > kMaximumOriginServiceSortieStateBytes)
    return std::unexpected(SaveSchemaError{
        SaveSchemaErrorCode::document_too_large, "$.assistance.sortie",
        "Sortie exceeds fixed state byte bound"});
  if (!nesting(text)) return std::unexpected(failure("Invalid sortie nesting"));
  if (!owner.origin_universe_seed || !validate_physical_moon_descriptor(moon) ||
      moon.catalog.owner != owner)
    return std::unexpected(failure("Invalid admitted sortie world"));
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
  try {
    const auto root = Json::parse(text.begin(), text.end(), callback);
    if (duplicate)
      return std::unexpected(SaveSchemaError{SaveSchemaErrorCode::duplicate_key,
                                             "$.assistance.sortie",
                                             "Repeated sortie key"});
    if (!fields(root, {"version", "service", "beacon", "phase", "start_tick",
                       "phase_start_tick", "deadline_tick", "return_requested",
                       "pending", "commitment_resources"}) ||
        unsigned_value(root["version"]) != kOriginServiceSortieVersion ||
        unsigned_value(root["phase"]) >
            static_cast<unsigned>(OriginServiceSortiePhase::canceled) ||
        !root["return_requested"].is_boolean())
      return std::unexpected(
          failure("Invalid closed sortie shape/version/phase"));
    const auto service =
        decode_origin_service_state_json(owner, root["service"].dump(), &moon);
    if (!service) return std::unexpected(service.error());
    OriginServiceSortie s;
    s.service = *service;
    s.beacon = decode_beacon(owner, moon, root["beacon"]);
    s.phase =
        static_cast<OriginServiceSortiePhase>(root["phase"].get<unsigned>());
    s.start_tick = unsigned_value(root["start_tick"]);
    s.phase_start_tick = unsigned_value(root["phase_start_tick"]);
    s.deadline_tick = unsigned_value(root["deadline_tick"]);
    s.return_requested = root["return_requested"].get<bool>();
    const auto& p = root["pending"];
    if (!p.is_null()) {
      if (!fields(p, {"version", "source", "source_checksum", "arrival_tick",
                      "body", "beacon"}) ||
          unsigned_value(p["version"]) != 1)
        return std::unexpected(
            failure("Invalid closed sortie arrival/version"));
      std::optional<OriginServiceBeacon> beacon;
      if (!p["beacon"].is_null()) {
        beacon = decode_beacon(owner, moon, p["beacon"]);
        if (*beacon != s.beacon)
          return std::unexpected(
              failure("Pending arrival names another beacon"));
      }
      s.pending =
          OriginServiceRouteArrival{1,
                                    s.service.descriptor.id,
                                    decode_body(owner, moon, p["source"]),
                                    unsigned_value(p["source_checksum"]),
                                    unsigned_value(p["arrival_tick"]),
                                    decode_body(owner, moon, p["body"]),
                                    s.service.descriptor,
                                    beacon};
    }
    if (!root["commitment_resources"].is_null())
      s.commitment_resources =
          decode_bill(root["commitment_resources"], s.service.descriptor);
    if (!validate_origin_service_sortie(owner, moon, s))
      return std::unexpected(
          failure("Invalid sortie owner/phase/clock/commitment"));
    return s;
  } catch (const SaveSchemaError& e) {
    return std::unexpected(e);
  } catch (const Json::exception& e) {
    return std::unexpected(SaveSchemaError{SaveSchemaErrorCode::malformed_json,
                                           "$.assistance.sortie", e.what()});
  }
}
} // namespace apsis_drift
