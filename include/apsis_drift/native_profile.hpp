#pragma once

#include "apsis_drift/profile_catalog.hpp"
#include "apsis_drift/save_file.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kNativeProfileHeaderVersion{1};
inline constexpr std::size_t kMaximumNativeProfileHeaderBytes{2'048};

// Listing hints only; never added to an authoritative world/save document.
enum class NativeProfileLocation : std::uint8_t {
  legacy_station,
  station_walk,
  boarding,
  attached,
  flight,
  landed,
  surface_walk,
  jump_spool,
  jump_transit,
  recorded_loss,
};

struct NativeProfileSummary {
  Seed universe_seed{};
  SimulationTick tick{};
  std::uint32_t native_format{};
  NativeProfileLocation location{};
  friend auto operator==(const NativeProfileSummary&,
                         const NativeProfileSummary&) -> bool = default;
};

struct NativeProfileHeader {
  std::uint32_t version{kNativeProfileHeaderVersion};
  ProfileId id{};
  std::uint64_t save_sequence{};
  NativeProfileSummary summary{};
  friend auto operator==(const NativeProfileHeader&, const NativeProfileHeader&)
      -> bool = default;
};

struct NativeProfileDocument {
  NativeProfileHeader header;
  NativeSaveDocument document;
};

[[nodiscard]] auto native_profile_location_name(NativeProfileLocation) noexcept
    -> std::string_view;
[[nodiscard]] auto project_native_profile_summary(const NativeSaveDocument&)
    -> std::expected<NativeProfileSummary, ProfileCatalogError>;
[[nodiscard]] auto make_native_profile_document(ProfileId, std::uint64_t,
                                                NativeSaveDocument)
    -> std::expected<NativeProfileDocument, ProfileCatalogError>;
[[nodiscard]] auto encode_native_profile_document_json(
    const NativeProfileDocument&)
    -> std::expected<std::string, ProfileCatalogError>;
[[nodiscard]] auto decode_native_profile_document_json(std::string_view)
    -> std::expected<NativeProfileDocument, ProfileCatalogError>;

struct NativeProfileEntry {
  std::filesystem::path path;
  std::optional<NativeProfileHeader> header;
  ProfileCatalogStatus status{ProfileCatalogStatus::invalid_header};
  std::string diagnostic;
  std::string source_bytes;
  [[nodiscard]] auto activatable() const noexcept -> bool {
    return status == ProfileCatalogStatus::available && header.has_value();
  }
};

struct NativeProfileCatalog {
  std::filesystem::path directory;
  std::vector<NativeProfileEntry> entries;
  std::optional<std::size_t> continue_index;
  bool writable{};
  bool overflow{};
  std::string diagnostic;
};

struct LoadedNativeProfile {
  NativeProfileDocument profile;
  std::filesystem::path path;
  std::string source_bytes;
};

// Session bookkeeping is outside the deterministic world. Failed persistence
// never adopts another slot or changes the last successful canonical bytes.
class NativeProfileSession {
 public:
  [[nodiscard]] static auto from_catalog(LoadedNativeProfile)
      -> std::expected<NativeProfileSession, ProfileCatalogError>;
  [[nodiscard]] static auto from_explicit_path(const NativeSaveDocument&)
      -> std::expected<NativeProfileSession, ProfileCatalogError>;
  [[nodiscard]] auto active() const noexcept
      -> const std::optional<LoadedNativeProfile>& {
    return m_active;
  }
  [[nodiscard]] auto explicit_path() const noexcept -> bool {
    return m_explicit_path;
  }
  [[nodiscard]] auto dirty(const NativeSaveDocument&) const
      -> std::expected<bool, ProfileCatalogError>;
  [[nodiscard]] auto save(const std::filesystem::path&, NativeSaveDocument,
                          bool save_as)
      -> std::expected<void, ProfileCatalogError>;

 private:
  std::optional<LoadedNativeProfile> m_active;
  std::optional<std::string> m_last_successful;
  bool m_explicit_path{};
};

[[nodiscard]] auto scan_native_profile_catalog(const std::filesystem::path&)
    -> NativeProfileCatalog;
[[nodiscard]] auto load_native_catalog_profile(const NativeProfileEntry&)
    -> std::expected<LoadedNativeProfile, ProfileCatalogError>;
[[nodiscard]] auto create_native_catalog_profile(const std::filesystem::path&,
                                                 NativeSaveDocument)
    -> std::expected<LoadedNativeProfile, ProfileCatalogError>;
[[nodiscard]] auto replace_native_catalog_profile(const LoadedNativeProfile&,
                                                  NativeSaveDocument)
    -> std::expected<LoadedNativeProfile, ProfileCatalogError>;

} // namespace apsis_drift
