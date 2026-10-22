#include "apsis_drift/freedom_assistance_save.hpp"
#include "apsis_drift/native_startup.hpp"
#include <array>
#include <charconv>
#include <limits>
#include <nlohmann/json.hpp>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;
auto failure(std::string detail) -> SaveSchemaError {
  return {SaveSchemaErrorCode::invalid_state, "$.assistance",
          std::move(detail)};
}
// Visitors return pointers into the caller-owned lvalue, keeping projections
// borrowed without a const-reference parameter escaping from a visitor.
template <class T>
auto knowledge_ptr(const T& value) -> const FreedomKnowledgeSaveDocument* {
  if constexpr (std::is_same_v<T, FreedomKnowledgeSaveDocument>)
    return &value;
  else if constexpr (std::is_same_v<T, FreedomTravelSaveDocument>)
    return &value.voyage;
  else if constexpr (std::is_same_v<T, FreedomSurfaceWalkSaveDocument>)
    return knowledge_ptr(value.voyage);
  else
    return std::visit(
        [](const auto& inner) -> const FreedomKnowledgeSaveDocument* {
          return knowledge_ptr(inner);
        },
        value.voyage);
}
auto knowledge_of(const FreedomAssistanceVoyage& value)
    -> const FreedomKnowledgeSaveDocument& {
  return *std::visit(
      [](const auto& inner) -> const FreedomKnowledgeSaveDocument* {
        return knowledge_ptr(inner);
      },
      value);
}
template <class T>
auto binding_of(const T& value)
    -> std::optional<NativeStartingAssemblySelection> {
  if constexpr (std::is_same_v<T, FreedomKnowledgeSaveDocument>)
    return std::visit(
        [](const auto& base) -> std::optional<NativeStartingAssemblySelection> {
          using B = std::decay_t<decltype(base)>;
          if constexpr (std::is_same_v<B,
                                       FreedomStartingAssemblySaveDocument> ||
                        std::is_same_v<B, FreedomBoardingSaveDocument>)
            return base.starting_assembly;
          else
            return {};
        },
        value.voyage.voyage.base);
  else if constexpr (std::is_same_v<T, FreedomTravelSaveDocument>) {
    if (value.craft_binding) return value.craft_binding;
    return binding_of(value.voyage);
  } else if constexpr (std::is_same_v<T, FreedomSurfaceWalkSaveDocument>)
    return binding_of(value.voyage);
  else
    return std::visit([](const auto& inner) { return binding_of(inner); },
                      value.voyage);
}
template <class T>
auto encode_inner(const T& inner)
    -> std::expected<std::string, SaveSchemaError> {
  if constexpr (std::is_same_v<T, FreedomKnowledgeSaveDocument>) {
    // Retain the existing native root admission: used drive state must retain
    // its authoritative travel receipt, never a stripped Knowledge document.
    if (inner.voyage.resources.drive && inner.voyage.resources.drive->last_use)
      return std::unexpected(
          failure("A recorded drive use requires its travel owner"));
    return encode_freedom_knowledge_document_json(inner);
  } else if constexpr (std::is_same_v<T, FreedomTravelSaveDocument>)
    return encode_freedom_travel_document_json(inner);
  else if constexpr (std::is_same_v<T, FreedomRecoverySaveDocument>)
    return encode_freedom_recovery_document_json(inner);
  else if constexpr (std::is_same_v<T, FreedomSurfaceWalkSaveDocument>)
    return encode_freedom_surface_walk_document_json(inner);
  else
    return encode_freedom_condition_document_json(inner);
}
auto inner_bytes(const FreedomAssistanceVoyage& voyage)
    -> std::expected<std::string, SaveSchemaError> {
  return std::visit(
      [](const auto& inner) -> std::expected<std::string, SaveSchemaError> {
        auto bytes = encode_inner(inner);
        if (!bytes) return std::unexpected(bytes.error());
        // Reuse authoritative complete-player admission (including used-drive
        // and replacement-lineage owners), without constructing another live
        // session.
        if (const auto admitted =
                native_select_save_document(NativeSaveDocument{inner});
            !admitted)
          return std::unexpected(failure(admitted.error()));
        return bytes;
      },
      voyage);
}
template <class T>
auto decode_inner(std::string_view text) -> std::expected<T, SaveSchemaError> {
  if constexpr (std::is_same_v<T, FreedomKnowledgeSaveDocument>)
    return decode_freedom_knowledge_document_json(text);
  else if constexpr (std::is_same_v<T, FreedomTravelSaveDocument>)
    return decode_freedom_travel_document_json(text);
  else if constexpr (std::is_same_v<T, FreedomRecoverySaveDocument>)
    return decode_freedom_recovery_document_json(text);
  else if constexpr (std::is_same_v<T, FreedomSurfaceWalkSaveDocument>)
    return decode_freedom_surface_walk_document_json(text);
  else
    return decode_freedom_condition_document_json(text);
}
auto inner_value(const Json& value)
    -> std::expected<FreedomAssistanceVoyage, SaveSchemaError> {
  if (!value.is_object() || !value.contains("format_version") ||
      !value["format_version"].is_number_unsigned() ||
      value["format_version"] == kFreedomAssistanceSaveFormatVersion)
    return std::unexpected(failure("Invalid or recursive assistance voyage"));
  const auto text = value.dump();
  std::optional<FreedomAssistanceVoyage> result;
  std::optional<SaveSchemaError> error;
  const auto attempt = [&]<class T>() {
    if (result || error) return;
    auto decoded = decode_inner<T>(text);
    if (decoded)
      result = FreedomAssistanceVoyage{std::move(*decoded)};
    else if (decoded.error().code !=
             SaveSchemaErrorCode::unsupported_format_version)
      error = decoded.error();
  };
  attempt.operator()<FreedomConditionSaveDocument>();
  attempt.operator()<FreedomSurfaceWalkSaveDocument>();
  attempt.operator()<FreedomRecoverySaveDocument>();
  attempt.operator()<FreedomTravelSaveDocument>();
  attempt.operator()<FreedomKnowledgeSaveDocument>();
  if (error) return std::unexpected(*error);
  if (!result)
    return std::unexpected(
        failure("Assistance requires a complete supported voyage"));
  if (const auto valid = inner_bytes(*result); !valid)
    return std::unexpected(valid.error());
  return std::move(*result);
}
auto fields(const Json& value, std::initializer_list<std::string_view> names)
    -> bool {
  if (!value.is_object() || value.size() != names.size()) return false;
  for (const auto name : names)
    if (!value.contains(std::string(name))) return false;
  return true;
}
auto decimal_tick(const Json& value) -> std::optional<SimulationTick> {
  if (!value.is_string()) return {};
  const auto& text = value.get_ref<const std::string&>();
  SimulationTick tick{};
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), tick);
  if (text.empty() || (text.size() > 1 && text.front() == '0') ||
      parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    return {};
  return tick;
}
auto nesting(std::string_view text) -> bool {
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
auto make_origin_assistance_world_context(Seed seed)
    -> std::expected<OriginAssistanceWorldContext, SaveSchemaError> {
  const auto origin = generate_physical_origin_system(seed, 2);
  if (!origin)
    return std::unexpected(failure("Authentic Origin system unavailable"));
  const auto catalog = generate_secondary_body_catalog(*origin);
  if (!catalog)
    return std::unexpected(
        failure("Authentic Origin companion catalog unavailable"));
  const auto moon =
      generate_physical_moon_descriptor(*catalog, catalog->bodies[0].node.id);
  if (!moon) return std::unexpected(failure("Authentic home Moon unavailable"));
  return OriginAssistanceWorldContext{*moon};
}
auto assistance_player_flight(const FreedomAssistanceVoyage& voyage)
    -> FreedomFlightSaveDocument {
  return surface_base_flight(knowledge_of(voyage).voyage.voyage.base);
}
auto assistance_player_resources(const FreedomAssistanceVoyage& voyage)
    -> const FreedomResources& {
  return knowledge_of(voyage).voyage.resources;
}
auto assistance_player_attachment(const FreedomAssistanceVoyage& voyage)
    -> std::optional<OriginDockConstraint> {
  const auto flight = assistance_player_flight(voyage);
  return std::visit(
      [&](const auto& base) -> std::optional<OriginDockConstraint> {
        using B = std::decay_t<decltype(base)>;
        if constexpr (std::is_same_v<B, FreedomFlightSaveDocument>)
          return {};
        else {
          const auto& docking = [&]() -> const FreedomDockingState& {
            if constexpr (std::is_same_v<B, FreedomDockingSaveDocument>)
              return base.docking;
            else if constexpr (std::is_same_v<
                                   B, FreedomStartingAssemblySaveDocument>)
              return base.journey.voyage.docking;
            else
              return base.voyage.docking;
          }();
          if (!docking.attached) return {};
          return OriginDockConstraint{docking.geometry_version, docking.target,
                                      flight.flight.craft, flight.flight.tick};
        }
      },
      knowledge_of(voyage).voyage.voyage.base);
}
auto validate_freedom_assistance_document(
    const OriginAssistanceWorldContext& context,
    const FreedomAssistanceSaveDocument& document)
    -> std::expected<void, SaveSchemaError> {
  if (const auto inner = inner_bytes(document.voyage); !inner)
    return std::unexpected(inner.error());
  if (document.version != kFreedomAssistanceVersion)
    return std::unexpected(failure("Unsupported assistance selection"));
  const auto flight = assistance_player_flight(document.voyage);
  const auto& resources = assistance_player_resources(document.voyage);
  const auto binding = std::visit(
      [](const auto& value) { return binding_of(value); }, document.voyage);
  if (!context.origin().origin_universe_seed ||
      *context.origin().origin_universe_seed !=
          flight.origin.recipe.universe_seed ||
      flight.model.physical_catalog != 2 ||
      flight.model.physical_ephemeris != 2 ||
      flight.flight.craft !=
          CraftFrameRecipe{kWayfarerFrameId, kWayfarerFrameVersion} ||
      !binding || *binding != NativeStartingAssemblySelection{} ||
      !validate_freedom_resources(resources, flight))
    return std::unexpected(
        failure("Assistance requires the actual selected Wayfarer and matching "
                "physical Origin owner"));
  const auto attachment = assistance_player_attachment(document.voyage);
  const auto& service = *std::visit(
      [](const auto& value) -> const OriginServiceCraftState* {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, OriginServiceCraftState>)
          return &value;
        else if constexpr (std::is_same_v<T, OriginServiceSortie>)
          return &value.service;
        else
          return &value.last.service;
      },
      document.actor);
  const bool historical =
      std::holds_alternative<OriginServiceUnavailable>(document.actor);
  if ((!historical && (service.body.tick != flight.flight.tick ||
                       service.resources.tick != resources.tick)) ||
      service.descriptor.universe_seed != flight.origin.recipe.universe_seed ||
      (attachment && attachment->port == service.descriptor.home_port))
    return std::unexpected(failure(
        "Player/service clock, universe or reserved D2 occupancy differs"));
  return std::visit(
      [&](const auto& value) -> std::expected<void, SaveSchemaError> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, OriginServiceCraftState>) {
          if (!value.attached || value.delivery ||
              value.resources.flight_quanta !=
                  value.descriptor.flight_capacity_quanta ||
              value.resources.cargo_quanta !=
                  value.descriptor.cargo_capacity_quanta ||
              value.resources.jump_charges != value.descriptor.jump_capacity ||
              !validate_origin_service_craft(context.origin(), value))
            return std::unexpected(
                failure("Stationed assistance must retain its original full "
                        "actor and attachment"));
        } else if constexpr (std::is_same_v<T, OriginServiceSortie>) {
          if (const auto valid = validate_origin_service_sortie(
                  context.origin(), context.moon(), value);
              !valid)
            return std::unexpected(failure(valid.error()));
        } else {
          const auto tick = value.last.service.body.tick;
          if (value.reason !=
                  OriginServiceUnavailableReason::route_unavailable ||
              tick == std::numeric_limits<SimulationTick>::max() ||
              value.failed_tick != tick + 1 ||
              value.failed_tick > flight.flight.tick)
            return std::unexpected(failure(
                "Invalid unavailable contact reason or first failed clock"));
          // This is authenticated immutable history at its own last real tick.
          // It neither projects a present NPC pose nor refunds/ages any service
          // pool.
          if (const auto valid = validate_origin_service_sortie(
                  context.origin(), context.moon(), value.last);
              !valid)
            return std::unexpected(failure(valid.error()));
        }
        return {};
      },
      document.actor);
}
auto validate_freedom_assistance_document(
    const FreedomAssistanceSaveDocument& document)
    -> std::expected<void, SaveSchemaError> {
  if (const auto valid = inner_bytes(document.voyage); !valid)
    return std::unexpected(valid.error());
  const auto context = make_origin_assistance_world_context(
      assistance_player_flight(document.voyage).origin.recipe.universe_seed);
  if (!context) return std::unexpected(context.error());
  return validate_freedom_assistance_document(*context, document);
}
auto encode_freedom_assistance_document_json(
    const FreedomAssistanceSaveDocument& document)
    -> std::expected<std::string, SaveSchemaError> {
  if (const auto valid = inner_bytes(document.voyage); !valid)
    return std::unexpected(valid.error());
  const auto context = make_origin_assistance_world_context(
      assistance_player_flight(document.voyage).origin.recipe.universe_seed);
  if (!context) return std::unexpected(context.error());
  if (const auto valid =
          validate_freedom_assistance_document(*context, document);
      !valid)
    return std::unexpected(valid.error());
  const auto voyage = inner_bytes(document.voyage);
  if (!voyage) return std::unexpected(voyage.error());
  const auto actor = std::visit(
      [&](const auto& value) -> std::expected<std::string, SaveSchemaError> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, OriginServiceCraftState>)
          return encode_origin_service_state_json(context->origin(), value);
        else if constexpr (std::is_same_v<T, OriginServiceSortie>)
          return encode_origin_service_sortie_json(context->origin(),
                                                   context->moon(), value);
        else {
          const auto last = encode_origin_service_sortie_json(
              context->origin(), context->moon(), value.last);
          if (!last) return std::unexpected(last.error());
          const Json history{
              {"last", Json::parse(*last)},
              {"failed_tick", std::to_string(value.failed_tick)},
              {"reason", static_cast<std::uint32_t>(value.reason)}};
          return history.dump();
        }
      },
      document.actor);
  if (!actor) return std::unexpected(actor.error());
  const Json root{
      {"format_version", kFreedomAssistanceSaveFormatVersion},
      {"version", document.version},
      {"voyage", Json::parse(*voyage)},
      {"assistance",
       {{"kind", document.actor.index()}, {"state", Json::parse(*actor)}}}};
  auto text = root.dump(2) + '\n';
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected(
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "Assistance save exceeds byte limit"});
  return text;
}
auto decode_freedom_assistance_document_json(std::string_view text)
    -> std::expected<FreedomAssistanceSaveDocument, SaveSchemaError> {
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected(
        SaveSchemaError{SaveSchemaErrorCode::document_too_large, "$",
                        "Assistance save exceeds byte limit"});
  if (!nesting(text))
    return std::unexpected(failure("Invalid assistance nesting"));
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
                                             "$", "Repeated assistance key"});
    if (!root.is_object() || !root.contains("format_version") ||
        !root["format_version"].is_number_unsigned() ||
        root["format_version"] != kFreedomAssistanceSaveFormatVersion)
      return std::unexpected(
          SaveSchemaError{SaveSchemaErrorCode::unsupported_format_version,
                          "$.format_version", "Not an assistance59 save"});
    if (!fields(root, {"format_version", "version", "voyage", "assistance"}) ||
        !root["version"].is_number_unsigned() ||
        root["version"] != kFreedomAssistanceVersion)
      return std::unexpected(failure("Invalid closed assistance envelope"));
    auto voyage = inner_value(root["voyage"]);
    if (!voyage) return std::unexpected(voyage.error());
    const auto context = make_origin_assistance_world_context(
        assistance_player_flight(*voyage).origin.recipe.universe_seed);
    if (!context) return std::unexpected(context.error());
    const auto& actor = root["assistance"];
    if (!fields(actor, {"kind", "state"}) ||
        !actor["kind"].is_number_unsigned() || actor["kind"] > 2)
      return std::unexpected(
          failure("Invalid closed assistance actor selection"));
    FreedomAssistanceActor selected;
    if (actor["kind"] == 0) {
      const auto value = decode_origin_service_state_json(
          context->origin(), actor["state"].dump());
      if (!value) return std::unexpected(value.error());
      selected = *value;
    } else if (actor["kind"] == 1) {
      const auto value = decode_origin_service_sortie_json(
          context->origin(), context->moon(), actor["state"].dump());
      if (!value) return std::unexpected(value.error());
      selected = *value;
    } else {
      const auto& history = actor["state"];
      if (!fields(history, {"last", "failed_tick", "reason"}) ||
          !history["reason"].is_number_unsigned() || history["reason"] != 1)
        return std::unexpected(
            failure("Invalid closed unavailable contact/reason"));
      const auto tick = decimal_tick(history["failed_tick"]);
      if (!tick)
        return std::unexpected(failure("Invalid canonical unavailable clock"));
      const auto last = decode_origin_service_sortie_json(
          context->origin(), context->moon(), history["last"].dump());
      if (!last) return std::unexpected(last.error());
      selected = OriginServiceUnavailable{
          *last, *tick, OriginServiceUnavailableReason::route_unavailable};
    }
    FreedomAssistanceSaveDocument result{root["version"].get<std::uint32_t>(),
                                         std::move(*voyage), selected};
    if (const auto valid =
            validate_freedom_assistance_document(*context, result);
        !valid)
      return std::unexpected(valid.error());
    return result;
  } catch (const Json::exception& error) {
    return std::unexpected(SaveSchemaError{SaveSchemaErrorCode::malformed_json,
                                           "$", error.what()});
  }
}
auto make_freedom_assistance_document(FreedomKnowledgeSaveDocument voyage)
    -> std::expected<FreedomAssistanceSaveDocument, SaveSchemaError> {
  const FreedomAssistanceVoyage selected{voyage};
  if (const auto valid = inner_bytes(selected); !valid)
    return std::unexpected(valid.error());
  const auto flight = assistance_player_flight(selected);
  const auto attachment = assistance_player_attachment(selected);
  const auto& resources = assistance_player_resources(selected);
  if (flight.flight.tick != 0 || flight.origin.lineage || !attachment ||
      attachment->port.ordinal != 1 ||
      resources.flight_quanta != kFreedomFlightCapacityQuanta ||
      resources.jump_charges != kFreedomJumpCapacity ||
      (resources.drive && resources.drive->last_use))
    return std::unexpected(failure(
        "Assistance selection is explicit NewGame at original D1 only"));
  const auto context =
      make_origin_assistance_world_context(flight.origin.recipe.universe_seed);
  if (!context) return std::unexpected(context.error());
  const std::array occupants{*attachment};
  const auto service = initialize_origin_service_craft(
      context->origin(), flight.flight.tick, occupants);
  if (!service)
    return std::unexpected(
        failure("Original service actor/D2 reservation unavailable"));
  FreedomAssistanceSaveDocument document{1, std::move(voyage), *service};
  if (const auto valid =
          validate_freedom_assistance_document(*context, document);
      !valid)
    return std::unexpected(valid.error());
  return document;
}
} // namespace apsis_drift
