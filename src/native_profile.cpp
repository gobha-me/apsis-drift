#include "apsis_drift/native_profile.hpp"

#include "apsis_drift/native_flight_session.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <vector>

namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;

auto failure(std::string detail) -> ProfileCatalogError {
  return {ProfileCatalogErrorCode::invalid_profile, {}, std::move(detail)};
}

auto exact_fields(const Json& value,
                  std::initializer_list<std::string_view> names) -> bool {
  if (!value.is_object() || value.size() != names.size()) return false;
  return std::ranges::all_of(
      names, [&](auto name) { return value.contains(std::string{name}); });
}

auto decimal(const Json& value) -> std::optional<std::uint64_t> {
  if (!value.is_string()) return {};
  const auto& text = value.get_ref<const std::string&>();
  if (text.empty() || text.size() > 20 ||
      (text.size() > 1 && text.front() == '0'))
    return {};
  std::uint64_t result{};
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    return {};
  return result;
}

auto bounded_nesting(std::string_view text) -> bool {
  std::array<char, 64> opened{};
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
      continue;
    }
    if (c == '"')
      quoted = true;
    else if (c == '{' || c == '[') {
      if (depth == opened.size()) return false;
      opened[depth++] = c;
    } else if (c == '}' || c == ']') {
      if (depth == 0 || opened[depth - 1] != (c == '}' ? '{' : '['))
        return false;
      --depth;
    }
  }
  return depth == 0 && !quoted;
}

auto parse_json(std::string_view text)
    -> std::expected<Json, ProfileCatalogError> {
  if (text.empty() || text.size() > kMaximumSaveDocumentBytes ||
      !bounded_nesting(text))
    return std::unexpected{failure("Profile exceeds its byte/nesting bound")};
  std::vector<std::unordered_set<std::string>> keys;
  bool valid = true;
  try {
    auto parsed =
        Json::parse(text, [&](int, Json::parse_event_t event, Json& value) {
          if (event == Json::parse_event_t::object_start)
            keys.emplace_back();
          else if (event == Json::parse_event_t::object_end)
            keys.pop_back();
          else if (event == Json::parse_event_t::key) {
            const auto& key = value.get_ref<const std::string&>();
            if (keys.empty() || key.size() > kMaximumSaveObjectKeyBytes ||
                !keys.back().insert(key).second)
              valid = false;
          }
          return true;
        });
    if (!valid || !parsed.is_object())
      return std::unexpected{failure("Profile keys/root are invalid")};
    return parsed;
  } catch (const Json::exception&) {
    return std::unexpected{failure("Profile JSON is malformed")};
  }
}

auto parse_location(const Json& value) -> std::optional<NativeProfileLocation> {
  if (!value.is_string()) return {};
  for (const auto location :
       {NativeProfileLocation::legacy_station,
        NativeProfileLocation::station_walk, NativeProfileLocation::boarding,
        NativeProfileLocation::attached, NativeProfileLocation::flight,
        NativeProfileLocation::landed, NativeProfileLocation::surface_walk,
        NativeProfileLocation::jump_spool, NativeProfileLocation::jump_transit,
        NativeProfileLocation::recorded_loss}) {
    if (value.get_ref<const std::string&>() ==
        native_profile_location_name(location))
      return location;
  }
  return {};
}

auto header_json(const NativeProfileHeader& header) -> Json {
  return Json{
      {"version", header.version},
      {"kind", "freedom"},
      {"id", std::to_string(header.id.value)},
      {"save_sequence", std::to_string(header.save_sequence)},
      {"summary",
       {{"universe_seed", std::to_string(header.summary.universe_seed.value)},
        {"tick", std::to_string(header.summary.tick)},
        {"native_format", header.summary.native_format},
        {"location", native_profile_location_name(header.summary.location)}}}};
}
} // namespace

auto native_profile_location_name(NativeProfileLocation location) noexcept
    -> std::string_view {
  switch (location) {
    case NativeProfileLocation::legacy_station: return "legacy_station";
    case NativeProfileLocation::station_walk: return "station_walk";
    case NativeProfileLocation::boarding: return "boarding";
    case NativeProfileLocation::attached: return "attached";
    case NativeProfileLocation::flight: return "flight";
    case NativeProfileLocation::landed: return "landed";
    case NativeProfileLocation::surface_walk: return "surface_walk";
    case NativeProfileLocation::jump_spool: return "jump_spool";
    case NativeProfileLocation::jump_transit: return "jump_transit";
    case NativeProfileLocation::recorded_loss: return "recorded_loss";
  }
  return "invalid";
}

auto project_native_profile_summary(const NativeSaveDocument& document)
    -> std::expected<NativeProfileSummary, ProfileCatalogError> {
  auto encoded = encode_native_save_document_json(document);
  if (!encoded)
    return std::unexpected{failure("Authoritative native save is invalid")};
  auto selected = native_select_save_document(document);
  if (!selected || selected->mode != NativeStartup::Mode::freedom)
    return std::unexpected{
        failure("Freedom profiles cannot reinterpret legacy careers")};
  const auto format =
      Json::parse(*encoded)["format_version"].get<std::uint32_t>();
  if (const auto* station = std::get_if<FreedomSaveDocument>(&document))
    return NativeProfileSummary{station->recipe.universe_seed,
                                station->state.tick, format,
                                NativeProfileLocation::legacy_station};
  auto session = NativeFreedomFlightSession::open(std::move(*selected));
  if (!session)
    return std::unexpected{failure("Native profile phase cannot be hydrated")};
  auto location = NativeProfileLocation::flight;
  if (session->recovery_pending())
    location = NativeProfileLocation::recorded_loss;
  else if (session->surface_walker())
    location = NativeProfileLocation::surface_walk;
  else if (session->travel() &&
           session->travel()->phase == FreedomJumpPhase::spool)
    location = NativeProfileLocation::jump_spool;
  else if (session->travel() &&
           session->travel()->phase == FreedomJumpPhase::transit)
    location = NativeProfileLocation::jump_transit;
  else if (session->boarding() &&
           (session->boarding()->phase == FreedomBoardingPhase::boarding ||
            session->boarding()->phase == FreedomBoardingPhase::disembarking))
    location = NativeProfileLocation::boarding;
  else if (session->walker())
    location = NativeProfileLocation::station_walk;
  else if (session->surface() && session->surface()->landed)
    location = NativeProfileLocation::landed;
  else if (session->docking() && session->docking()->attached)
    location = NativeProfileLocation::attached;
  return NativeProfileSummary{session->document().origin.recipe.universe_seed,
                              session->document().flight.tick, format,
                              location};
}

auto make_native_profile_document(ProfileId id, std::uint64_t sequence,
                                  NativeSaveDocument document)
    -> std::expected<NativeProfileDocument, ProfileCatalogError> {
  if (id.value == 0 || sequence == 0)
    return std::unexpected{
        failure("Profile identity/sequence must be positive")};
  auto summary = project_native_profile_summary(document);
  if (!summary) return std::unexpected{summary.error()};
  return NativeProfileDocument{
      {kNativeProfileHeaderVersion, id, sequence, *summary},
      std::move(document)};
}

auto encode_native_profile_document_json(const NativeProfileDocument& profile)
    -> std::expected<std::string, ProfileCatalogError> {
  auto expected = make_native_profile_document(
      profile.header.id, profile.header.save_sequence, profile.document);
  if (!expected || expected->header != profile.header)
    return std::unexpected{
        failure("Profile header does not match its authoritative save")};
  auto encoded = encode_native_save_document_json(profile.document);
  if (!encoded)
    return std::unexpected{failure("Authoritative native save is invalid")};
  auto root = Json::parse(*encoded);
  auto header = header_json(profile.header);
  if (header.dump().size() > kMaximumNativeProfileHeaderBytes)
    return std::unexpected{failure("Profile header is too large")};
  root["profile"] = std::move(header);
  auto text = root.dump(2);
  if (text.size() > kMaximumSaveDocumentBytes)
    return std::unexpected{failure("Profile exceeds the save byte bound")};
  return text;
}

auto decode_native_profile_document_json(std::string_view text)
    -> std::expected<NativeProfileDocument, ProfileCatalogError> {
  auto parsed = parse_json(text);
  if (!parsed) return std::unexpected{parsed.error()};
  const auto found = parsed->find("profile");
  if (found == parsed->end() ||
      !exact_fields(*found,
                    {"version", "kind", "id", "save_sequence", "summary"}) ||
      found->dump().size() > kMaximumNativeProfileHeaderBytes)
    return std::unexpected{
        failure("Freedom profile header is missing/invalid")};
  const auto& header = *found;
  if (!header["version"].is_number_unsigned() ||
      header["version"] != kNativeProfileHeaderVersion ||
      !header["kind"].is_string() || header["kind"] != "freedom" ||
      !exact_fields(header["summary"],
                    {"universe_seed", "tick", "native_format", "location"}))
    return std::unexpected{
        failure("Freedom profile kind/version/summary is invalid")};
  const auto& summary = header["summary"];
  const auto id = decimal(header["id"]),
             sequence = decimal(header["save_sequence"]),
             seed = decimal(summary["universe_seed"]),
             tick = decimal(summary["tick"]);
  const auto location = parse_location(summary["location"]);
  if (!id || *id == 0 || !sequence || *sequence == 0 || !seed || !tick ||
      !location || !summary["native_format"].is_number_unsigned() ||
      summary["native_format"].get<std::uint64_t>() >
          std::numeric_limits<std::uint32_t>::max())
    return std::unexpected{failure("Freedom profile fields are invalid")};
  const NativeProfileHeader claimed{
      kNativeProfileHeaderVersion,
      {*id},
      *sequence,
      {{*seed},
       *tick,
       summary["native_format"].get<std::uint32_t>(),
       *location}};
  parsed->erase("profile");
  auto document = decode_native_save_document_json(parsed->dump());
  if (!document)
    return std::unexpected{failure("Profile native save is invalid")};
  auto result =
      make_native_profile_document({*id}, *sequence, std::move(*document));
  if (!result || result->header != claimed)
    return std::unexpected{
        failure("Profile summary does not match its authoritative save")};
  return result;
}
auto NativeProfileSession::from_catalog(LoadedNativeProfile loaded)
    -> std::expected<NativeProfileSession, ProfileCatalogError> {
  const auto validated =
      decode_native_profile_document_json(loaded.source_bytes);
  if (!validated || validated->header != loaded.profile.header ||
      validated->document != loaded.profile.document) {
    return std::unexpected{ProfileCatalogError{
        ProfileCatalogErrorCode::invalid_profile, loaded.path,
        "catalog source does not match the loaded profile"}};
  }
  const auto canonical =
      encode_native_save_document_json(loaded.profile.document);
  if (!canonical)
    return std::unexpected{
        ProfileCatalogError{ProfileCatalogErrorCode::invalid_profile,
                            loaded.path, canonical.error().detail}};
  NativeProfileSession result;
  result.m_last_successful = *canonical;
  result.m_active = std::move(loaded);
  return result;
}

auto NativeProfileSession::from_explicit_path(
    const NativeSaveDocument& document)
    -> std::expected<NativeProfileSession, ProfileCatalogError> {
  const auto canonical = encode_native_save_document_json(document);
  if (!canonical)
    return std::unexpected{
        ProfileCatalogError{ProfileCatalogErrorCode::invalid_profile,
                            {},
                            canonical.error().detail}};
  NativeProfileSession result;
  result.m_explicit_path = true;
  result.m_last_successful = *canonical;
  return result;
}

auto NativeProfileSession::dirty(const NativeSaveDocument& document) const
    -> std::expected<bool, ProfileCatalogError> {
  const auto canonical = encode_native_save_document_json(document);
  if (!canonical)
    return std::unexpected{
        ProfileCatalogError{ProfileCatalogErrorCode::invalid_profile,
                            {},
                            canonical.error().detail}};
  return !m_last_successful || *canonical != *m_last_successful;
}

auto NativeProfileSession::save(const std::filesystem::path& directory,
                                NativeSaveDocument document, bool save_as)
    -> std::expected<void, ProfileCatalogError> {
  if (m_explicit_path || (!save_as && !m_active)) {
    return std::unexpected{ProfileCatalogError{
        ProfileCatalogErrorCode::invalid_profile, directory,
        m_explicit_path
            ? "Explicit-path sessions use their existing file workflow"
            : "Save As is required before Save has an active slot"}};
  }
  const auto canonical = encode_native_save_document_json(document);
  if (!canonical)
    return std::unexpected{
        ProfileCatalogError{ProfileCatalogErrorCode::invalid_profile,
                            {},
                            canonical.error().detail}};
  auto written =
      save_as ? create_native_catalog_profile(directory, std::move(document))
              : replace_native_catalog_profile(*m_active, std::move(document));
  if (!written) return std::unexpected{written.error()};
  m_active = std::move(*written);
  m_last_successful = *canonical;
  return {};
}

} // namespace apsis_drift
