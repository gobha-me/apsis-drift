#include "apsis_drift/origin_service_save.hpp"

#include <array>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <vector>

namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;
auto failure(std::string message) -> SaveSchemaError {
  return {SaveSchemaErrorCode::invalid_state, "$.assistance.service",
          std::move(message)};
}
auto fields(const Json& value, std::initializer_list<std::string_view> names)
    -> bool {
  if (!value.is_object() || value.size() != names.size()) return false;
  for (const auto name : names)
    if (!value.contains(std::string{name})) return false;
  return true;
}
auto nesting(std::string_view text) -> bool {
  std::array<char, 32> stack{};
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
auto unsigned_fields(const Json& value,
                     std::initializer_list<std::string_view> names) -> bool {
  if (!fields(value, names)) return false;
  for (const auto name : names)
    if (!value[std::string{name}].is_number_unsigned()) return false;
  return true;
}
} // namespace

auto encode_origin_service_state_json(const PhysicalLocalSystem& owner,
                                      const OriginServiceCraftState& state,
                                      const PhysicalMoonDescriptor* moon)
    -> std::expected<std::string, SaveSchemaError> {
  if (!validate_origin_service_craft(owner, state, moon))
    return std::unexpected(
        failure("Invalid service owner, quantities or pose"));
  const auto station = generate_origin_station(state.descriptor.universe_seed);
  const bool lunar =
      state.body.frame.kind == RigidFrameKind::body_relative_inertial;
  const auto body = encode_rigid_body_state_json(
      lunar ? RigidBodyWorldContext{*moon, &station}
            : RigidBodyWorldContext{owner, &station},
      state.body);
  if (!body)
    return std::unexpected(failure("Service rigid state cannot encode"));
  const auto& d = state.descriptor;
  const auto& r = state.resources;
  Json delivery = nullptr;
  if (state.delivery) {
    const auto& receipt = *state.delivery;
    delivery = {
        {"target_craft", receipt.target_craft.value},
        {"target_system", receipt.target_body.system.value},
        {"target_kind", static_cast<unsigned>(receipt.target_body.body.kind)},
        {"target_body", receipt.target_body.body.value},
        {"target_anchor_checksum", receipt.target_anchor_checksum},
        {"initial_target_quanta", receipt.initial_target_quanta},
        {"tick", receipt.tick},
        {"delivered_quanta", receipt.delivered_quanta}};
  }
  const Json result{
      {"version", kOriginServiceCraftVersion},
      {"owner", {{"seed", d.universe_seed.value}, {"craft", d.id.value}}},
      {"body", Json::parse(*body)},
      {"resources",
       {{"tick", r.tick},
        {"flight_quanta", r.flight_quanta},
        {"cargo_quanta", r.cargo_quanta},
        {"jump_charges", r.jump_charges}}},
      {"attached", state.attached},
      {"delivery", std::move(delivery)}};
  auto text = result.dump(2) + '\n';
  if (text.size() > kMaximumOriginServiceStateBytes)
    return std::unexpected(
        SaveSchemaError{SaveSchemaErrorCode::document_too_large,
                        "$.assistance.service", "Service state exceeds bound"});
  return text;
}

auto decode_origin_service_state_json(const PhysicalLocalSystem& owner,
                                      std::string_view text,
                                      const PhysicalMoonDescriptor* moon)
    -> std::expected<OriginServiceCraftState, SaveSchemaError> {
  if (text.size() > kMaximumOriginServiceStateBytes)
    return std::unexpected(
        SaveSchemaError{SaveSchemaErrorCode::document_too_large,
                        "$.assistance.service", "Service state exceeds bound"});
  if (!nesting(text))
    return std::unexpected(failure("Invalid service nesting"));
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
                                             "$.assistance.service",
                                             "Repeated service key"});
    if (!fields(root, {"version", "owner", "body", "resources", "attached",
                       "delivery"}) ||
        !root["version"].is_number_unsigned() ||
        root["version"] != kOriginServiceCraftVersion ||
        !unsigned_fields(root["owner"], {"seed", "craft"}) ||
        !root["attached"].is_boolean() ||
        !unsigned_fields(root["resources"], {"tick", "flight_quanta",
                                             "cargo_quanta", "jump_charges"}))
      return std::unexpected(failure("Invalid closed service shape/version"));
    auto descriptor = generate_origin_service_craft_descriptor(owner);
    if (!descriptor ||
        root["owner"]["seed"] != descriptor->universe_seed.value ||
        root["owner"]["craft"] != descriptor->id.value)
      return std::unexpected(failure("Foreign service owner"));
    const auto& r = root["resources"];
    if (r["jump_charges"] > descriptor->jump_capacity)
      return std::unexpected(failure("Service charge count exceeds capacity"));
    const auto station = generate_origin_station(descriptor->universe_seed);
    const auto body_text = root["body"].dump();
    auto body = decode_rigid_body_state_json(
        RigidBodyWorldContext{owner, &station}, body_text);
    if (!body && moon)
      body = decode_rigid_body_state_json(
          RigidBodyWorldContext{*moon, &station}, body_text);
    if (!body)
      return std::unexpected(failure("Invalid or foreign service rigid state"));
    OriginServiceCraftState state{*descriptor,
                                  *body,
                                  {descriptor->id,
                                   r["tick"].get<SimulationTick>(),
                                   r["flight_quanta"].get<std::uint64_t>(),
                                   r["cargo_quanta"].get<std::uint64_t>(),
                                   r["jump_charges"].get<std::uint32_t>()},
                                  root["attached"].get<bool>(),
                                  {}};
    const auto& delivery = root["delivery"];
    if (!delivery.is_null()) {
      if (!unsigned_fields(delivery,
                           {"target_craft", "target_system", "target_kind",
                            "target_body", "target_anchor_checksum",
                            "initial_target_quanta", "tick",
                            "delivered_quanta"}) ||
          delivery["target_kind"] != static_cast<unsigned>(BodyKind::moon))
        return std::unexpected(failure("Invalid closed service receipt"));
      state.delivery = OriginServiceFuelReceipt{
          {delivery["target_craft"].get<std::uint64_t>()},
          {{delivery["target_system"].get<std::uint64_t>()},
           {kBodyIdentityVersion, BodyKind::moon,
            delivery["target_body"].get<std::uint64_t>()}},
          delivery["target_anchor_checksum"].get<std::uint64_t>(),
          delivery["initial_target_quanta"].get<std::uint64_t>(),
          delivery["tick"].get<SimulationTick>(),
          delivery["delivered_quanta"].get<std::uint64_t>()};
    }
    if (!validate_origin_service_craft(owner, state, moon))
      return std::unexpected(
          failure("Invalid service owner, quantities or pose"));
    return state;
  } catch (const Json::exception& exception) {
    return std::unexpected(SaveSchemaError{SaveSchemaErrorCode::malformed_json,
                                           "$.assistance.service",
                                           exception.what()});
  }
}
} // namespace apsis_drift
