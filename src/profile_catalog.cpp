#include "apsis_drift/profile_catalog.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <format>
#include <limits>
#include <nlohmann/json.hpp>
#include <system_error>
#include <unordered_set>
#include <utility>

#include "apsis_drift/save_file.hpp"
#include "apsis_drift/version.hpp"

namespace apsis_drift {
namespace {

using Json = nlohmann::ordered_json;

[[nodiscard]] auto failure(ProfileCatalogErrorCode code,
                           const std::filesystem::path& path,
                           std::string detail) -> ProfileCatalogError {
  if (detail.size() > kMaximumProfileDiagnosticBytes) {
    detail.resize(kMaximumProfileDiagnosticBytes);
  }
  for (char& byte : detail) {
    if (static_cast<unsigned char>(byte) < 0x20U || byte == 0x7f) byte = ' ';
  }
  return {code, path, std::move(detail)};
}

[[nodiscard]] auto system_detail(std::string_view action, int error)
    -> std::string {
  return std::format("{}: {}", action, std::system_category().message(error));
}

class FileDescriptor {
 public:
  explicit FileDescriptor(int value = -1) noexcept : m_value(value) {}
  FileDescriptor(const FileDescriptor&) = delete;
  auto operator=(const FileDescriptor&) -> FileDescriptor& = delete;
  FileDescriptor(FileDescriptor&& other) noexcept
      : m_value(std::exchange(other.m_value, -1)) {}
  auto operator=(FileDescriptor&& other) noexcept -> FileDescriptor& {
    if (this != &other) {
      reset();
      m_value = std::exchange(other.m_value, -1);
    }
    return *this;
  }
  ~FileDescriptor() { reset(); }

  [[nodiscard]] auto get() const noexcept -> int { return m_value; }
  [[nodiscard]] auto valid() const noexcept -> bool { return m_value >= 0; }
  auto reset() noexcept -> void {
    if (m_value >= 0) (void)::close(m_value);
    m_value = -1;
  }

 private:
  int m_value{-1};
};

[[nodiscard]] auto parse_canonical_u64(const Json& value, bool positive = false)
    -> std::optional<std::uint64_t> {
  if (!value.is_string()) return std::nullopt;
  const auto& text = value.get_ref<const std::string&>();
  if (text.empty() || (text.size() > 1 && text.front() == '0')) {
    return std::nullopt;
  }
  std::uint64_t parsed{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (error != std::errc{} || end != text.data() + text.size() ||
      (positive && parsed == 0) || std::to_string(parsed) != text) {
    return std::nullopt;
  }
  return parsed;
}

[[nodiscard]] auto canonical_profile_id(std::string_view filename)
    -> std::optional<ProfileId> {
  constexpr std::string_view prefix{"profile-"};
  constexpr std::string_view suffix{".json"};
  if (filename.size() != prefix.size() + 16U + suffix.size() ||
      !filename.starts_with(prefix) || !filename.ends_with(suffix)) {
    return std::nullopt;
  }
  const auto digits = filename.substr(prefix.size(), 16U);
  if (!std::ranges::all_of(digits, [](char value) {
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
      })) {
    return std::nullopt;
  }
  std::uint64_t value{};
  const auto [end, error] =
      std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
  if (error != std::errc{} || end != digits.data() + digits.size() ||
      value == 0) {
    return std::nullopt;
  }
  return ProfileId{value};
}

[[nodiscard]] auto profile_filename(ProfileId id) -> std::string {
  return std::format("profile-{:016x}.json", id.value);
}

[[nodiscard]] auto read_regular_file(const std::filesystem::path& path)
    -> std::expected<std::string, ProfileCatalogError> {
  FileDescriptor descriptor{
      ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
  if (!descriptor.valid()) {
    const int error = errno;
    return std::unexpected{
        failure(error == ENOENT ? ProfileCatalogErrorCode::stale_entry
                                : ProfileCatalogErrorCode::storage_unavailable,
                path, system_detail("cannot open profile", error))};
  }
  struct stat status{};
  if (::fstat(descriptor.get(), &status) < 0 || !S_ISREG(status.st_mode)) {
    return std::unexpected{failure(ProfileCatalogErrorCode::invalid_profile,
                                   path, "profile is not a regular file")};
  }
  std::string contents;
  contents.reserve(std::min<std::size_t>(
      kMaximumSaveDocumentBytes,
      status.st_size > 0 ? static_cast<std::size_t>(status.st_size) : 0U));
  std::array<char, std::size_t{16U} * 1024U> buffer{};
  while (contents.size() <= kMaximumSaveDocumentBytes) {
    const auto remaining = kMaximumSaveDocumentBytes + 1U - contents.size();
    const auto count = ::read(descriptor.get(), buffer.data(),
                              std::min(buffer.size(), remaining));
    if (count > 0) {
      contents.append(buffer.data(), static_cast<std::size_t>(count));
      continue;
    }
    if (count == 0) break;
    if (errno == EINTR) continue;
    const int error = errno;
    return std::unexpected{
        failure(ProfileCatalogErrorCode::storage_unavailable, path,
                system_detail("cannot read profile", error))};
  }
  if (contents.size() > kMaximumSaveDocumentBytes) {
    return std::unexpected{failure(ProfileCatalogErrorCode::invalid_profile,
                                   path,
                                   "profile exceeds the save byte bound")};
  }
  return contents;
}

[[nodiscard]] auto penalty_name(IntersystemRuleProfile profile)
    -> std::string_view {
  return profile == IntersystemRuleProfile::assisted ? "assisted" : "pilot";
}

[[nodiscard]] auto onboarding_name(OnboardingState state) -> std::string_view {
  switch (state) {
    case OnboardingState::guided: return "guided";
    case OnboardingState::skipped: return "skipped";
    case OnboardingState::completed: return "completed";
  }
  return "invalid";
}

[[nodiscard]] auto chapter_name(std::optional<OnboardingChapter> chapter)
    -> Json {
  if (!chapter) return nullptr;
  switch (*chapter) {
    case OnboardingChapter::contract_one: return "contract_one";
    case OnboardingChapter::contract_two: return "contract_two";
    case OnboardingChapter::contract_three: return "contract_three";
  }
  return nullptr;
}

[[nodiscard]] auto profile_location(const SaveDocument& document)
    -> ProfileLocation {
  if (!document.state.intersystem_contract) {
    return document.state.location == OriginLocation::docked_at_origin
               ? ProfileLocation::docked_at_origin
               : ProfileLocation::target_planet_flight;
  }
  switch (document.state.intersystem_contract->travel_phase) {
    case IntersystemTravelPhase::docked_at_origin:
      return ProfileLocation::docked_at_origin;
    case IntersystemTravelPhase::origin_system_flight:
      return ProfileLocation::origin_system_flight;
    case IntersystemTravelPhase::outbound_jump_spooling:
      return ProfileLocation::outbound_jump_spooling;
    case IntersystemTravelPhase::outbound_jump_committed:
      return ProfileLocation::outbound_jump_committed;
    case IntersystemTravelPhase::target_system_flight:
      return ProfileLocation::target_system_flight;
    case IntersystemTravelPhase::target_planet_flight:
      return ProfileLocation::target_planet_flight;
    case IntersystemTravelPhase::return_jump_spooling:
      return ProfileLocation::return_jump_spooling;
    case IntersystemTravelPhase::return_jump_committed:
      return ProfileLocation::return_jump_committed;
    case IntersystemTravelPhase::origin_system_return:
      return ProfileLocation::origin_system_return;
  }
  return ProfileLocation::docked_at_origin;
}

[[nodiscard]] auto metadata_for(ProfileId id, std::uint64_t sequence,
                                const SaveDocument& document)
    -> std::optional<ProfileMetadata> {
  if (!document.state.intersystem_contract || id.value == 0 || sequence == 0) {
    return std::nullopt;
  }
  return ProfileMetadata{
      .id = id,
      .save_sequence = sequence,
      .application_version = std::string{kApplicationVersion},
      .format_version = kSaveFormatVersion,
      .universe_seed = document.recipe.universe_seed,
      .penalty_mode = document.state.intersystem_contract->rule_profile,
      .onboarding_state = document.state.onboarding.state,
      .onboarding_chapter = document.state.onboarding.chapter,
      .location = profile_location(document),
      .tick = document.state.intersystem_contract->universe_tick,
  };
}

[[nodiscard]] auto parse_onboarding(const Json& value)
    -> std::optional<OnboardingState> {
  if (!value.is_string()) return std::nullopt;
  const auto text = value.get<std::string>();
  if (text == "guided") return OnboardingState::guided;
  if (text == "skipped") return OnboardingState::skipped;
  if (text == "completed") return OnboardingState::completed;
  return std::nullopt;
}

[[nodiscard]] auto parse_chapter(const Json& value)
    -> std::optional<std::optional<OnboardingChapter>> {
  if (value.is_null()) return std::optional<OnboardingChapter>{};
  if (!value.is_string()) return std::nullopt;
  const auto text = value.get<std::string>();
  if (text == "contract_one") return OnboardingChapter::contract_one;
  if (text == "contract_two") return OnboardingChapter::contract_two;
  if (text == "contract_three") return OnboardingChapter::contract_three;
  return std::nullopt;
}

[[nodiscard]] auto parse_penalty(const Json& value)
    -> std::optional<IntersystemRuleProfile> {
  if (!value.is_string()) return std::nullopt;
  const auto text = value.get<std::string>();
  if (text == "assisted") return IntersystemRuleProfile::assisted;
  if (text == "pilot") return IntersystemRuleProfile::pilot;
  return std::nullopt;
}

[[nodiscard]] auto parse_location(const Json& value)
    -> std::optional<ProfileLocation> {
  if (!value.is_string()) return std::nullopt;
  const auto text = value.get<std::string>();
  for (const auto location : {
           ProfileLocation::docked_at_origin,
           ProfileLocation::origin_system_flight,
           ProfileLocation::outbound_jump_spooling,
           ProfileLocation::outbound_jump_committed,
           ProfileLocation::target_system_flight,
           ProfileLocation::target_planet_flight,
           ProfileLocation::return_jump_spooling,
           ProfileLocation::return_jump_committed,
           ProfileLocation::origin_system_return,
       }) {
    if (text == profile_location_name(location)) return location;
  }
  return std::nullopt;
}

[[nodiscard]] auto decode_metadata(std::string_view contents,
                                   ProfileId filename_id,
                                   const SaveDocument& document)
    -> std::expected<ProfileMetadata, ProfileCatalogError> {
  Json root;
  try {
    root = Json::parse(contents);
  } catch (const nlohmann::json::exception& error) {
    return std::unexpected{
        failure(ProfileCatalogErrorCode::invalid_profile, {}, error.what())};
  }
  const auto profile_it = root.find("profile");
  if (profile_it == root.end() || !profile_it->is_object() ||
      profile_it->dump().size() > 2'048U) {
    return std::unexpected{failure(ProfileCatalogErrorCode::invalid_profile, {},
                                   "profile header is missing or invalid")};
  }
  const auto& profile = *profile_it;
  const auto summary_it = profile.find("summary");
  if (profile.size() != 3U || summary_it == profile.end() ||
      !summary_it->is_object() || summary_it->size() != 6U) {
    return std::unexpected{failure(ProfileCatalogErrorCode::invalid_profile, {},
                                   "profile summary is missing")};
  }
  const auto& summary = *summary_it;
  const auto id_it = profile.find("id");
  const auto sequence_it = profile.find("save_sequence");
  const auto seed_it = summary.find("universe_seed");
  const auto penalty_it = summary.find("penalty_mode");
  const auto onboarding_it = summary.find("onboarding_state");
  const auto chapter_it = summary.find("onboarding_chapter");
  const auto location_it = summary.find("location");
  const auto tick_it = summary.find("tick");
  if (id_it == profile.end() || sequence_it == profile.end() ||
      seed_it == summary.end() || penalty_it == summary.end() ||
      onboarding_it == summary.end() || chapter_it == summary.end() ||
      location_it == summary.end() || tick_it == summary.end()) {
    return std::unexpected{failure(ProfileCatalogErrorCode::invalid_profile, {},
                                   "profile header fields are incomplete")};
  }
  const auto id = parse_canonical_u64(*id_it, true);
  const auto sequence = parse_canonical_u64(*sequence_it, true);
  const auto seed = parse_canonical_u64(*seed_it);
  const auto penalty = parse_penalty(*penalty_it);
  const auto onboarding = parse_onboarding(*onboarding_it);
  const auto chapter = parse_chapter(*chapter_it);
  const auto location = parse_location(*location_it);
  const auto tick = parse_canonical_u64(*tick_it);
  if (!id || !sequence || !seed || !penalty || !onboarding || !chapter ||
      !location || !tick || *id != filename_id.value) {
    return std::unexpected{failure(ProfileCatalogErrorCode::invalid_profile, {},
                                   "profile header values are invalid")};
  }
  const auto expected = metadata_for(filename_id, *sequence, document);
  if (!expected || expected->universe_seed.value != *seed ||
      expected->penalty_mode != *penalty ||
      expected->onboarding_state != *onboarding ||
      expected->onboarding_chapter != *chapter ||
      expected->location != *location || expected->tick != *tick) {
    return std::unexpected{
        failure(ProfileCatalogErrorCode::invalid_profile, {},
                "profile summary does not match save state")};
  }
  auto result = *expected;
  if (const auto app = root.find("application_version");
      app != root.end() && app->is_string()) {
    result.application_version = app->get<std::string>();
  }
  return result;
}

[[nodiscard]] auto encode_profile(const ProfileMetadata& metadata,
                                  const SaveDocument& document)
    -> std::expected<std::string, ProfileCatalogError> {
  auto encoded = encode_save_document_json(document);
  if (!encoded) {
    return std::unexpected{failure(ProfileCatalogErrorCode::invalid_profile, {},
                                   encoded.error().detail)};
  }
  Json root;
  try {
    root = Json::parse(*encoded);
  } catch (const nlohmann::json::exception& error) {
    return std::unexpected{
        failure(ProfileCatalogErrorCode::invalid_profile, {}, error.what())};
  }
  root["profile"] = Json{
      {"id", std::to_string(metadata.id.value)},
      {"save_sequence", std::to_string(metadata.save_sequence)},
      {"summary",
       Json{{"universe_seed", std::to_string(metadata.universe_seed.value)},
            {"penalty_mode", penalty_name(metadata.penalty_mode)},
            {"onboarding_state", onboarding_name(metadata.onboarding_state)},
            {"onboarding_chapter", chapter_name(metadata.onboarding_chapter)},
            {"location", profile_location_name(metadata.location)},
            {"tick", std::to_string(metadata.tick)}}},
  };
  auto result = root.dump(2);
  result.push_back('\n');
  if (result.size() > kMaximumSaveDocumentBytes) {
    return std::unexpected{failure(ProfileCatalogErrorCode::invalid_profile, {},
                                   "profile exceeds the save byte bound")};
  }
  return result;
}

[[nodiscard]] auto ensure_directory(const std::filesystem::path& directory)
    -> std::expected<void, ProfileCatalogError> {
  if (directory.empty() ||
      directory.string().size() > kMaximumProfilePathBytes ||
      !directory.is_absolute() || directory == directory.root_path()) {
    return std::unexpected{
        failure(ProfileCatalogErrorCode::invalid_path, directory,
                "profile directory must be a bounded absolute path")};
  }
  FileDescriptor current{
      ::open(directory.root_path().c_str(),
             O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
  if (!current.valid()) {
    return std::unexpected{
        failure(ProfileCatalogErrorCode::storage_unavailable, directory,
                system_detail("cannot open profile root", errno))};
  }
  for (const auto& component : directory.relative_path()) {
    if (component.empty() || component == "." || component == "..") {
      return std::unexpected{
          failure(ProfileCatalogErrorCode::invalid_path, directory,
                  "profile directory contains an unsafe component")};
    }
    if (::mkdirat(current.get(), component.c_str(), S_IRWXU) < 0 &&
        errno != EEXIST) {
      return std::unexpected{
          failure(ProfileCatalogErrorCode::storage_unavailable, directory,
                  system_detail("cannot create profile directory", errno))};
    }
    FileDescriptor next{
        ::openat(current.get(), component.c_str(),
                 O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
    if (!next.valid()) {
      return std::unexpected{
          failure(ProfileCatalogErrorCode::storage_unavailable, directory,
                  system_detail("cannot open profile directory", errno))};
    }
    current = std::move(next);
  }
  if (::fchmod(current.get(), S_IRWXU) < 0) {
    return std::unexpected{
        failure(ProfileCatalogErrorCode::storage_unavailable, directory,
                system_detail("cannot secure profile directory", errno))};
  }
  return {};
}

[[nodiscard]] auto write_new_profile(const std::filesystem::path& path,
                                     std::string_view bytes)
    -> std::expected<void, ProfileCatalogError> {
  std::filesystem::path temporary;
  FileDescriptor output;
  for (unsigned attempt = 0; attempt < 64U && !output.valid(); ++attempt) {
    temporary = path.parent_path() /
                std::format(".{}.tmp.{}.{}", path.filename().string(),
                            static_cast<long long>(::getpid()), attempt);
    output = FileDescriptor{::open(
        temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
        S_IRUSR | S_IWUSR)};
    if (!output.valid() && errno != EEXIST) break;
  }
  if (!output.valid()) {
    return std::unexpected{
        failure(ProfileCatalogErrorCode::write_failure, path,
                system_detail("cannot create temporary profile", errno))};
  }
  std::size_t offset{};
  while (offset < bytes.size()) {
    const auto count =
        ::write(output.get(), bytes.data() + offset, bytes.size() - offset);
    if (count > 0) {
      offset += static_cast<std::size_t>(count);
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return std::unexpected{
        failure(ProfileCatalogErrorCode::write_failure, path,
                system_detail("cannot write profile", errno))};
  }
  if (::fsync(output.get()) < 0) {
    const int error = errno;
    output.reset();
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return std::unexpected{
        failure(ProfileCatalogErrorCode::write_failure, path,
                system_detail("cannot synchronize profile", error))};
  }
  output.reset();
  if (::link(temporary.c_str(), path.c_str()) < 0) {
    const int error = errno;
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return std::unexpected{
        failure(ProfileCatalogErrorCode::write_failure, path,
                system_detail("cannot commit new profile", error))};
  }
  std::error_code ignored;
  std::filesystem::remove(temporary, ignored);
  FileDescriptor directory{
      ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
  if (!directory.valid() || ::fsync(directory.get()) < 0) {
    return std::unexpected{failure(ProfileCatalogErrorCode::write_failure, path,
                                   "profile exists but directory sync failed")};
  }
  return {};
}

} // namespace

auto profile_location_name(ProfileLocation location) noexcept
    -> std::string_view {
  switch (location) {
    case ProfileLocation::docked_at_origin: return "docked_at_origin";
    case ProfileLocation::origin_system_flight: return "origin_system_flight";
    case ProfileLocation::outbound_jump_spooling:
      return "outbound_jump_spooling";
    case ProfileLocation::outbound_jump_committed:
      return "outbound_jump_committed";
    case ProfileLocation::target_system_flight: return "target_system_flight";
    case ProfileLocation::target_planet_flight: return "target_planet_flight";
    case ProfileLocation::return_jump_spooling: return "return_jump_spooling";
    case ProfileLocation::return_jump_committed: return "return_jump_committed";
    case ProfileLocation::origin_system_return: return "origin_system_return";
  }
  return "invalid";
}

auto resolve_profile_directory(std::optional<std::string> xdg_data_home,
                               std::optional<std::string> home)
    -> std::expected<std::filesystem::path, ProfileCatalogError> {
  if (!xdg_data_home) {
    if (const char* value = std::getenv("XDG_DATA_HOME")) {
      xdg_data_home = value;
    }
  }
  std::filesystem::path base;
  if (xdg_data_home && !xdg_data_home->empty() &&
      std::filesystem::path{*xdg_data_home}.is_absolute()) {
    base = *xdg_data_home;
  } else {
    if (!home) {
      if (const char* value = std::getenv("HOME")) home = value;
    }
    if (!home || home->empty() || !std::filesystem::path{*home}.is_absolute()) {
      return std::unexpected{
          failure(ProfileCatalogErrorCode::storage_unavailable, {},
                  "HOME does not identify an absolute data directory")};
    }
    base = std::filesystem::path{*home} / ".local" / "share";
  }
  auto result = base / "apsis-drift" / "profiles";
  if (result.string().size() > kMaximumProfilePathBytes) {
    return std::unexpected{failure(ProfileCatalogErrorCode::invalid_path,
                                   result,
                                   "profile directory exceeds the path bound")};
  }
  return result;
}

auto scan_profile_catalog(const std::filesystem::path& directory)
    -> ProfileCatalogSnapshot {
  ProfileCatalogSnapshot result{.directory = directory};
  if (directory.empty() || !directory.is_absolute() ||
      directory.string().size() > kMaximumProfilePathBytes) {
    result.diagnostic = "profile directory is unavailable";
    return result;
  }
  std::error_code error;
  const auto status = std::filesystem::symlink_status(directory, error);
  if (error || !std::filesystem::exists(status)) {
    result.writable = true;
    result.diagnostic = "no local profiles yet";
    return result;
  }
  if (!std::filesystem::is_directory(status) ||
      std::filesystem::is_symlink(status)) {
    result.diagnostic = "profile path is not a real directory";
    return result;
  }
  result.writable = true;
  std::vector<std::pair<std::filesystem::path, ProfileId>> candidates;
  for (std::filesystem::directory_iterator it{directory, error}, end;
       !error && it != end; it.increment(error)) {
    const auto id = canonical_profile_id(it->path().filename().string());
    if (!id) continue;
    const auto file_status = it->symlink_status(error);
    if (error) break;
    if (!std::filesystem::is_regular_file(file_status) ||
        std::filesystem::is_symlink(file_status)) {
      continue;
    }
    candidates.emplace_back(it->path(), *id);
  }
  if (error) {
    result.writable = false;
    result.diagnostic = "profile directory cannot be enumerated";
    return result;
  }
  if (candidates.size() > kMaximumLocalProfiles) {
    result.overflow = true;
    result.writable = false;
    result.diagnostic = "more than 64 canonical profiles exist";
    return result;
  }
  result.entries.reserve(candidates.size());
  for (const auto& [path, id] : candidates) {
    ProfileCatalogEntry entry{.path = path};
    const auto contents = read_regular_file(path);
    if (!contents) {
      entry.status = ProfileCatalogStatus::unreadable;
      entry.diagnostic = contents.error().detail;
      result.entries.push_back(std::move(entry));
      continue;
    }
    const auto document = decode_save_document_json(*contents);
    if (!document) {
      entry.status = ProfileCatalogStatus::invalid_document;
      entry.diagnostic = document.error().detail;
      result.entries.push_back(std::move(entry));
      continue;
    }
    const auto metadata = decode_metadata(*contents, id, *document);
    if (!metadata) {
      entry.status = ProfileCatalogStatus::invalid_header;
      entry.diagnostic = metadata.error().detail;
      result.entries.push_back(std::move(entry));
      continue;
    }
    entry.metadata = *metadata;
    entry.status = ProfileCatalogStatus::available;
    entry.source_bytes = *contents;
    result.entries.push_back(std::move(entry));
  }
  const auto catalog_order = [](const auto& left, const auto& right) {
    if (left.activatable() != right.activatable()) return left.activatable();
    if (left.activatable()) {
      if (left.metadata->save_sequence != right.metadata->save_sequence) {
        return left.metadata->save_sequence > right.metadata->save_sequence;
      }
      return left.metadata->id < right.metadata->id;
    }
    return left.path.filename().string() < right.path.filename().string();
  };
  std::ranges::sort(result.entries, catalog_order);
  std::unordered_set<std::uint64_t> sequences;
  for (auto& entry : result.entries) {
    if (!entry.activatable()) continue;
    if (!sequences.insert(entry.metadata->save_sequence).second) {
      entry.status = ProfileCatalogStatus::invalid_header;
      entry.diagnostic = "duplicate catalog save sequence";
      entry.metadata.reset();
    }
  }
  std::ranges::sort(result.entries, catalog_order);
  for (std::size_t index = 0; index < result.entries.size(); ++index) {
    if (result.entries[index].activatable()) {
      result.continue_index = index;
      break;
    }
  }
  if (result.entries.empty())
    result.diagnostic = "no local profiles yet";
  else if (!result.continue_index)
    result.diagnostic = "no usable local profile";
  return result;
}

auto load_catalog_profile(const ProfileCatalogEntry& entry)
    -> std::expected<LoadedProfile, ProfileCatalogError> {
  const auto id = canonical_profile_id(entry.path.filename().string());
  if (!id || !entry.activatable()) {
    return std::unexpected{failure(ProfileCatalogErrorCode::invalid_profile,
                                   entry.path,
                                   "selected profile is not activatable")};
  }
  const auto contents = read_regular_file(entry.path);
  if (!contents) return std::unexpected{contents.error()};
  if (*contents != entry.source_bytes) {
    return std::unexpected{failure(ProfileCatalogErrorCode::stale_entry,
                                   entry.path,
                                   "profile changed after the catalog scan")};
  }
  const auto document = decode_save_document_json(*contents);
  if (!document) {
    return std::unexpected{failure(ProfileCatalogErrorCode::invalid_profile,
                                   entry.path, document.error().detail)};
  }
  const auto metadata = decode_metadata(*contents, *id, *document);
  if (!metadata || *metadata != *entry.metadata) {
    return std::unexpected{failure(ProfileCatalogErrorCode::stale_entry,
                                   entry.path,
                                   "profile changed after the catalog scan")};
  }
  return LoadedProfile{*metadata, *document, entry.path, *contents};
}

auto create_catalog_profile(const std::filesystem::path& directory,
                            const SaveDocument& document)
    -> std::expected<LoadedProfile, ProfileCatalogError> {
  if (auto ensured = ensure_directory(directory); !ensured) {
    return std::unexpected{ensured.error()};
  }
  const auto lock_path = directory / ".profiles.lock";
  FileDescriptor lock{::open(lock_path.c_str(),
                             O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
                             S_IRUSR | S_IWUSR)};
  if (!lock.valid() || ::flock(lock.get(), LOCK_EX | LOCK_NB) < 0) {
    return std::unexpected{failure(ProfileCatalogErrorCode::lock_unavailable,
                                   lock_path,
                                   "another profile write is in progress")};
  }
  auto catalog = scan_profile_catalog(directory);
  if (catalog.overflow) {
    return std::unexpected{failure(ProfileCatalogErrorCode::catalog_overflow,
                                   directory, catalog.diagnostic)};
  }
  if (catalog.entries.size() >= kMaximumLocalProfiles) {
    return std::unexpected{failure(ProfileCatalogErrorCode::catalog_full,
                                   directory,
                                   "the 64-profile catalog is full")};
  }
  std::unordered_set<std::uint64_t> ids;
  std::uint64_t maximum_sequence{};
  for (const auto& entry : catalog.entries) {
    if (const auto id = canonical_profile_id(entry.path.filename().string())) {
      ids.insert(id->value);
    }
    if (entry.metadata) {
      maximum_sequence =
          std::max(maximum_sequence, entry.metadata->save_sequence);
    }
  }
  ProfileId id{1};
  while (ids.contains(id.value) &&
         id.value != std::numeric_limits<std::uint64_t>::max()) {
    ++id.value;
  }
  if (id.value == 0 || ids.contains(id.value)) {
    return std::unexpected{failure(ProfileCatalogErrorCode::catalog_full,
                                   directory,
                                   "no profile identity is available")};
  }
  if (maximum_sequence == std::numeric_limits<std::uint64_t>::max()) {
    return std::unexpected{failure(ProfileCatalogErrorCode::sequence_overflow,
                                   directory,
                                   "profile save sequence is exhausted")};
  }
  const auto metadata = metadata_for(id, maximum_sequence + 1U, document);
  if (!metadata) {
    return std::unexpected{
        failure(ProfileCatalogErrorCode::invalid_profile, directory,
                "new career cannot produce profile metadata")};
  }
  const auto encoded = encode_profile(*metadata, document);
  if (!encoded) return std::unexpected{encoded.error()};
  const auto path = directory / profile_filename(id);
  if (auto written = write_new_profile(path, *encoded); !written) {
    return std::unexpected{written.error()};
  }
  return LoadedProfile{*metadata, document, path, *encoded};
}

auto profile_catalog_error_message(const ProfileCatalogError& error)
    -> std::string {
  if (error.path.empty()) return error.detail;
  return std::format("{}: {}", error.path.string(), error.detail);
}

} // namespace apsis_drift
