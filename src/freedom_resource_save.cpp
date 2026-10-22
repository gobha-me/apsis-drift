#include "apsis_drift/freedom_resource_save.hpp"
#include "freedom_resource_save_internal.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;
auto failure(std::string path, std::string message) -> SaveSchemaError {
  return {SaveSchemaErrorCode::invalid_state, std::move(path),
          std::move(message)};
}
auto fields(const Json& j, std::initializer_list<std::string_view> keys)
    -> bool {
  if (!j.is_object() || j.size() != keys.size()) return false;
  for (auto k : keys)
    if (!j.contains(std::string{k})) return false;
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
} // namespace

namespace detail {
auto drive_json(const FreedomDriveRecoveryState& drive) -> Json {
  Json lineage = nullptr, use = nullptr;
  if (drive.lineage)
    lineage = {{"version", drive.lineage->version},
               {"generation", std::to_string(drive.lineage->generation)}};
  if (drive.last_use) {
    const auto& s = *drive.last_use;
    use = {{"kind", static_cast<unsigned>(s.kind)},
           {"attempt", s.attempt},
           {"source_checksum", s.source_checksum},
           {"commit_tick", s.commit_tick},
           {"arrival_tick", s.arrival_tick}};
    if (drive.version == kFreedomStrainedDriveRecoveryVersion)
      use["recovery_ticks"] = s.recovery_ticks;
  }
  return {{"version", drive.version},
          {"lineage", std::move(lineage)},
          {"last_use", std::move(use)}};
}
auto drive_value(const Json& j, SimulationTick now)
    -> std::expected<FreedomDriveRecoveryState, SaveSchemaError> {
  if (!fields(j, {"version", "lineage", "last_use"}) ||
      !j["version"].is_number_unsigned() ||
      (j["version"] != kFreedomDriveRecoveryVersion &&
       j["version"] != kFreedomStrainedDriveRecoveryVersion))
    return std::unexpected{
        failure("$.resources.drive", "invalid closed drive selection")};
  FreedomDriveRecoveryState drive;
  drive.version = j["version"].get<std::uint32_t>();
  if (!j["lineage"].is_null()) {
    const auto& lineage = j["lineage"];
    if (!fields(lineage, {"version", "generation"}) ||
        !lineage["version"].is_number_unsigned() ||
        lineage["version"] != kFreedomCraftLineageVersion ||
        !lineage["generation"].is_string())
      return std::unexpected{
          failure("$.resources.drive.lineage", "invalid drive lineage")};
    const auto& text = lineage["generation"].get_ref<const std::string&>();
    std::uint64_t generation{};
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), generation);
    if (text.empty() || text.size() > 20 || text.front() == '0' ||
        parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
      return std::unexpected{
          failure("$.resources.drive.lineage", "noncanonical drive lineage")};
    drive.lineage = FreedomCraftLineage{1, generation};
  }
  if (!j["last_use"].is_null()) {
    const auto& use = j["last_use"];
    auto shape = use;
    SimulationTick duration{kFreedomDriveRecoveryTicks};
    if (drive.version == kFreedomStrainedDriveRecoveryVersion) {
      if (!use.is_object() || !use.contains("recovery_ticks") ||
          !use["recovery_ticks"].is_number_unsigned())
        return std::unexpected{failure("$.resources.drive.last_use",
                                       "unsigned recovery duration required")};
      duration = use["recovery_ticks"].get<SimulationTick>();
      shape.erase("recovery_ticks");
    }
    if (!fields(shape, {"kind", "attempt", "source_checksum", "commit_tick",
                        "arrival_tick"}))
      return std::unexpected{
          failure("$.resources.drive.last_use", "invalid closed drive stamp")};
    for (const auto name :
         {"kind", "attempt", "source_checksum", "commit_tick", "arrival_tick"})
      if (!use[name].is_number_unsigned())
        return std::unexpected{failure("$.resources.drive.last_use",
                                       "unsigned drive stamp required")};
    if (use["kind"] > static_cast<unsigned>(FreedomJumpRecoveryKind::minor))
      return std::unexpected{
          failure("$.resources.drive.last_use", "unsupported drive route")};
    drive.last_use = FreedomJumpRecoveryStamp{
        static_cast<FreedomJumpRecoveryKind>(use["kind"].get<unsigned>()),
        use["attempt"].get<std::uint64_t>(),
        use["source_checksum"].get<std::uint64_t>(),
        use["commit_tick"].get<SimulationTick>(),
        use["arrival_tick"].get<SimulationTick>(),
        duration};
  }
  if (!validate_freedom_drive_recovery(drive, now))
    return std::unexpected{
        failure("$.resources.drive", "invalid drive recipe or clock")};
  return drive;
}
auto resource_json(const FreedomResources& s) -> Json {
  Json result{{"version", s.version},
              {"craft", s.craft.value},
              {"frame_id", s.frame.id.value},
              {"frame_version", s.frame.version},
              {"tick", s.tick},
              {"flight_quanta", s.flight_quanta},
              {"jump_charges", s.jump_charges}};
  if (s.drive) result["drive"] = drive_json(*s.drive);
  return result;
}
auto resource_value(const Json& s)
    -> std::expected<FreedomResources, SaveSchemaError> {
  auto shape = s;
  const bool recovering = s.is_object() && s.contains("version") &&
                          s["version"].is_number_unsigned() &&
                          (s["version"] == kFreedomRecoveryResourceVersion ||
                           s["version"] == kFreedomStrainedResourceVersion);
  if (recovering) {
    if (!shape.contains("drive"))
      return std::unexpected{failure("$.resources", "selected drive missing")};
    shape.erase("drive");
  }
  if (!fields(shape, {"version", "craft", "frame_id", "frame_version", "tick",
                      "flight_quanta", "jump_charges"}))
    return std::unexpected{failure("$", "invalid closed resource shape")};
  for (const auto name : {"version", "craft", "frame_id", "frame_version",
                          "tick", "flight_quanta", "jump_charges"})
    if (!s[name].is_number_unsigned())
      return std::unexpected{
          failure("$.resources", "unsigned integer fields required")};
  if ((!recovering && s["version"] != kFreedomResourceVersion) ||
      s["frame_id"] != kWayfarerFrameId.value ||
      s["frame_version"] != kWayfarerFrameVersion ||
      s["jump_charges"] > kFreedomJumpCapacity ||
      s["flight_quanta"] > kFreedomFlightCapacityQuanta)
    return std::unexpected{
        failure("$.resources", "unsupported recipe or out-of-range quantity")};
  FreedomResources result{s["version"].get<std::uint32_t>(),
                          {s["craft"].get<std::uint64_t>()},
                          {kWayfarerFrameId, kWayfarerFrameVersion},
                          s["tick"].get<SimulationTick>(),
                          s["flight_quanta"].get<std::uint64_t>(),
                          s["jump_charges"].get<std::uint32_t>()};
  if (recovering) {
    auto drive = drive_value(s["drive"], result.tick);
    if (!drive) return std::unexpected{drive.error()};
    result.drive = *drive;
  }
  if (!validate_freedom_resources(result))
    return std::unexpected{
        failure("$.resources", "invalid resource or drive quantity")};
  return result;
}
auto uncertainty_json(const FrozenFreedomJumpUncertainty& u) -> Json {
  return {{"version", u.version},
          {"source_checksum", u.source_checksum},
          {"sample_seed", u.sample_seed.value},
          {"remaining_ticks", u.remaining_ticks},
          {"extra_radius_millimetres", u.extra_radius_millimetres},
          {"offset_metres",
           {u.offset_metres.x, u.offset_metres.y, u.offset_metres.z}}};
}
auto uncertainty_value(const Json& j)
    -> std::expected<FrozenFreedomJumpUncertainty, SaveSchemaError> {
  if (!fields(j,
              {"version", "source_checksum", "sample_seed", "remaining_ticks",
               "extra_radius_millimetres", "offset_metres"}))
    return std::unexpected{
        failure("$.travel.recovery", "invalid closed frozen uncertainty")};
  for (const auto name : {"version", "source_checksum", "sample_seed",
                          "remaining_ticks", "extra_radius_millimetres"})
    if (!j[name].is_number_unsigned())
      return std::unexpected{
          failure("$.travel.recovery", "unsigned frozen uncertainty required")};
  const auto& p = j["offset_metres"];
  const bool strained = j["version"] == kFreedomStrainedDriveRecoveryVersion;
  if ((!strained && j["version"] != kFreedomDriveRecoveryVersion) ||
      j["remaining_ticks"] > (strained ? kFreedomDriveMaximumRecoveryTicks
                                       : kFreedomDriveRecoveryTicks) ||
      j["extra_radius_millimetres"] >
          kFreedomJumpRecoveryMaximumRadiusMillimetres ||
      !p.is_array() || p.size() != 3)
    return std::unexpected{failure(
        "$.travel.recovery", "unsupported or unbounded frozen uncertainty")};
  for (const auto& v : p)
    if (!v.is_number() || !std::isfinite(v.get<double>()) ||
        std::abs(v.get<double>()) >
            static_cast<double>(kFreedomJumpRecoveryMaximumRadiusMillimetres) /
                1000)
      return std::unexpected{failure(
          "$.travel.recovery", "nonfinite or unbounded uncertainty offset")};
  return FrozenFreedomJumpUncertainty{
      j["version"].get<std::uint32_t>(),
      j["source_checksum"].get<std::uint64_t>(),
      {j["sample_seed"].get<std::uint64_t>()},
      j["remaining_ticks"].get<SimulationTick>(),
      j["extra_radius_millimetres"].get<std::uint32_t>(),
      {p[0].get<double>(), p[1].get<double>(), p[2].get<double>()}};
}
} // namespace detail

auto validate_freedom_resource_document(const FreedomResourceSaveDocument& d)
    -> std::expected<void, SaveSchemaError> {
  if (auto valid = validate_freedom_surface_document(d.voyage); !valid)
    return valid;
  if (!validate_freedom_resources(d.resources,
                                  surface_base_flight(d.voyage.base)))
    return std::unexpected{failure(
        "$.resources",
        "unsupported resource recipe, quantity, craft owner or shared tick")};
  return {};
}
auto encode_freedom_resource_document_json(const FreedomResourceSaveDocument& d)
    -> std::expected<std::string, SaveSchemaError> {
  if (auto v = validate_freedom_resource_document(d); !v)
    return std::unexpected{v.error()};
  auto voyage = encode_freedom_surface_document_json(d.voyage);
  if (!voyage) return std::unexpected{voyage.error()};
  const auto& s = d.resources;
  const Json root = {{"format_version", kFreedomResourceSaveFormatVersion},
                     {"voyage", Json::parse(*voyage)},
                     {"resources", detail::resource_json(s)}};
  auto text = root.dump(2) + '\n';
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "resource save exceeds limit"}};
  return text;
}
auto decode_freedom_resource_document_json(std::string_view text)
    -> std::expected<FreedomResourceSaveDocument, SaveSchemaError> {
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "resource save exceeds limit"}};
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
  if (!root.is_object() || !root.contains("format_version") ||
      !root["format_version"].is_number_unsigned())
    return std::unexpected{
        failure("$.format_version", "unsigned version required")};
  if (root["format_version"] != kFreedomResourceSaveFormatVersion)
    return std::unexpected{
        SaveSchemaError{SaveSchemaErrorCode::unsupported_format_version,
                        "$.format_version", "unsupported resource format"}};
  if (!fields(root, {"format_version", "voyage", "resources"}))
    return std::unexpected{failure("$", "invalid closed resource shape")};
  const auto& s = root["resources"];
  if (s.is_object() && s.contains("version") &&
      s["version"].is_number_unsigned() &&
      (s["version"] == kFreedomRecoveryResourceVersion ||
       s["version"] == kFreedomStrainedResourceVersion)) {
    auto ledger = detail::resource_value(s);
    if (!ledger) return std::unexpected{ledger.error()};
    auto voyage = decode_freedom_surface_document_json(root["voyage"].dump());
    if (!voyage) return std::unexpected{voyage.error()};
    FreedomResourceSaveDocument result{std::move(*voyage), *ledger};
    if (auto v = validate_freedom_resource_document(result); !v)
      return std::unexpected{v.error()};
    return result;
  }
  if (!fields(s, {"version", "craft", "frame_id", "frame_version", "tick",
                  "flight_quanta", "jump_charges"}))
    return std::unexpected{failure("$", "invalid closed resource shape")};
  for (auto name : {"version", "craft", "frame_id", "frame_version", "tick",
                    "flight_quanta", "jump_charges"})
    if (!s[name].is_number_unsigned())
      return std::unexpected{
          failure("$.resources", "unsigned integer fields required")};
  // Compare wide parsed values before any narrowing conversion.
  if (s["version"] != kFreedomResourceVersion ||
      s["frame_id"] != kWayfarerFrameId.value ||
      s["frame_version"] != kWayfarerFrameVersion ||
      s["jump_charges"] > kFreedomJumpCapacity ||
      s["flight_quanta"] > kFreedomFlightCapacityQuanta)
    return std::unexpected{
        failure("$.resources", "unsupported recipe or out-of-range quantity")};
  auto voyage = decode_freedom_surface_document_json(root["voyage"].dump());
  if (!voyage) return std::unexpected{voyage.error()};
  FreedomResources ledger{1,
                          {s["craft"].get<std::uint64_t>()},
                          {kWayfarerFrameId, 1},
                          s["tick"].get<SimulationTick>(),
                          s["flight_quanta"].get<std::uint64_t>(),
                          s["jump_charges"].get<std::uint32_t>()};
  FreedomResourceSaveDocument result{std::move(*voyage), ledger};
  if (auto v = validate_freedom_resource_document(result); !v)
    return std::unexpected{v.error()};
  return result;
}
} // namespace apsis_drift
