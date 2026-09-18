#include "asset_manifest.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <nlohmann/json.hpp>
#include <optional>
#include <ranges>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace apsis_drift::asset_provenance {
namespace {

using Json = nlohmann::ordered_json;

constexpr std::size_t kMaximumFilesPerAsset{32U};
constexpr std::size_t kMaximumIdentifierBytes{128U};

struct AssetSummary {
  std::size_t record_index{};
  std::string id;
  std::string source_kind;
  std::string derivatives;
  std::vector<std::string> parents;
};

struct LicenseSummary {
  std::string derivatives;
  std::unordered_set<std::string> permitted_uses;
};

auto add(Diagnostics& diagnostics, std::string path, std::string detail)
    -> void {
  diagnostics.push_back({std::move(path), std::move(detail)});
}

[[nodiscard]] auto contains(std::initializer_list<std::string_view> values,
                            std::string_view value) -> bool {
  return std::ranges::find(values, value) != values.end();
}

auto validate_fields(const Json& object, std::string_view path,
                     std::initializer_list<std::string_view> required,
                     std::initializer_list<std::string_view> optional,
                     Diagnostics& diagnostics) -> void {
  if (!object.is_object()) {
    add(diagnostics, std::string{path}, "must be an object");
    return;
  }
  for (const auto required_field : required) {
    if (!object.contains(required_field)) {
      add(diagnostics, std::format("{}.{}", path, required_field),
          "required field is missing");
    }
  }
  for (const auto& [key, unused] : object.items()) {
    (void)unused;
    if (!contains(required, key) && !contains(optional, key)) {
      add(diagnostics, std::format("{}.{}", path, key), "unknown field");
    }
  }
}

[[nodiscard]] auto read_string(const Json& object, std::string_view field,
                               std::string_view path, Diagnostics& diagnostics,
                               bool allow_empty = false)
    -> std::optional<std::string> {
  const auto found = object.find(field);
  if (found == object.end()) return std::nullopt;
  const auto field_path = std::format("{}.{}", path, field);
  if (!found->is_string()) {
    add(diagnostics, field_path, "must be a string");
    return std::nullopt;
  }
  auto value = found->get<std::string>();
  if ((!allow_empty && value.empty()) ||
      value.size() > kMaximumManifestStringBytes) {
    add(diagnostics, field_path,
        allow_empty ? "exceeds the string byte bound"
                    : "must contain 1 to 16384 bytes");
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] auto valid_date(std::string_view value) -> bool {
  static const std::regex pattern{
      R"(^[0-9]{4}-(0[1-9]|1[0-2])-([0-2][0-9]|3[01])$)"};
  return std::regex_match(value.begin(), value.end(), pattern);
}

auto validate_date(const Json& object, std::string_view field,
                   std::string_view path, Diagnostics& diagnostics) -> void {
  if (const auto value = read_string(object, field, path, diagnostics);
      value && !valid_date(*value)) {
    add(diagnostics, std::format("{}.{}", path, field),
        "must use YYYY-MM-DD form");
  }
}

[[nodiscard]] auto validate_string_array(
    const Json& object, std::string_view field, std::string_view path,
    Diagnostics& diagnostics, bool allow_empty,
    std::initializer_list<std::string_view> allowed = {})
    -> std::vector<std::string> {
  std::vector<std::string> result;
  const auto found = object.find(field);
  if (found == object.end()) return result;
  const auto field_path = std::format("{}.{}", path, field);
  if (!found->is_array()) {
    add(diagnostics, field_path, "must be an array");
    return result;
  }
  if ((!allow_empty && found->empty()) ||
      found->size() > kMaximumManifestCollectionEntries) {
    add(diagnostics, field_path,
        allow_empty ? "exceeds the collection bound"
                    : "must contain 1 to 256 entries");
  }
  std::unordered_set<std::string> unique;
  for (std::size_t index = 0; index < found->size(); ++index) {
    const auto item_path = std::format("{}[{}]", field_path, index);
    const auto& item = (*found)[index];
    if (!item.is_string()) {
      add(diagnostics, item_path, "must be a string");
      continue;
    }
    auto value = item.get<std::string>();
    if (value.empty() || value.size() > kMaximumManifestStringBytes) {
      add(diagnostics, item_path, "must contain 1 to 16384 bytes");
      continue;
    }
    if (allowed.size() != 0U && !contains(allowed, value)) {
      add(diagnostics, item_path, "contains an unknown value");
      continue;
    }
    if (!unique.insert(value).second) {
      add(diagnostics, item_path, "duplicates an earlier value");
      continue;
    }
    result.push_back(std::move(value));
  }
  return result;
}

auto validate_json_bounds(const Json& value, std::string_view path,
                          Diagnostics& diagnostics) -> void {
  if (value.is_string() && value.get_ref<const std::string&>().size() >
                               kMaximumManifestStringBytes) {
    add(diagnostics, std::string{path}, "string exceeds 16384 bytes");
    return;
  }
  if ((value.is_array() || value.is_object()) &&
      value.size() > (path == "$.assets" ? kMaximumAssetRecords
                                         : kMaximumManifestCollectionEntries)) {
    add(diagnostics, std::string{path},
        path == "$.assets" ? "asset collection exceeds 1024 entries"
                           : "collection exceeds 256 entries");
  }
  if (value.is_array()) {
    for (std::size_t index = 0; index < value.size(); ++index) {
      validate_json_bounds(value[index], std::format("{}[{}]", path, index),
                           diagnostics);
    }
  } else if (value.is_object()) {
    for (const auto& [key, child] : value.items()) {
      validate_json_bounds(child, std::format("{}.{}", path, key), diagnostics);
    }
  }
}

[[nodiscard]] auto safe_repository_path(std::string_view encoded) -> bool {
  const std::filesystem::path path{encoded};
  if (encoded.empty() || encoded.contains('\\') || path.is_absolute() ||
      path.has_root_name()) {
    return false;
  }
  for (const auto& component : path) {
    if (component.empty() || component == "." || component == "..") {
      return false;
    }
  }
  return true;
}

auto validate_existing_file(const std::filesystem::path& repository_root,
                            std::string_view encoded, std::string_view path,
                            Diagnostics& diagnostics) -> void {
  if (!safe_repository_path(encoded)) {
    add(diagnostics, std::string{path},
        "must be a normalized repository-relative path without traversal");
    return;
  }
  std::error_code error;
  auto current = repository_root;
  for (const auto& component : std::filesystem::path{encoded}) {
    current /= component;
    const auto status = std::filesystem::symlink_status(current, error);
    if (error) {
      if (error == std::errc::no_such_file_or_directory) {
        add(diagnostics, std::string{path}, "referenced file does not exist");
        return;
      }
      add(diagnostics, std::string{path},
          std::format("cannot inspect repository path: {}", error.message()));
      return;
    }
    if (std::filesystem::is_symlink(status)) {
      add(diagnostics, std::string{path}, "must not traverse a symbolic link");
      return;
    }
  }
  if (!std::filesystem::is_regular_file(current, error) || error) {
    add(diagnostics, std::string{path}, "referenced file does not exist");
  }
}

[[nodiscard]] auto starts_with_https(std::string_view value) -> bool {
  return value.starts_with("https://");
}

[[nodiscard]] auto validate_license(
    const Json& asset, std::string_view path,
    const std::filesystem::path& repository_root, Diagnostics& diagnostics)
    -> LicenseSummary {
  const auto found = asset.find("license");
  if (found == asset.end()) return {};
  const auto license_path = std::format("{}.license", path);
  validate_fields(*found, license_path,
                  {"expression", "terms", "permitted_uses", "redistribution",
                   "derivatives", "attribution"},
                  {}, diagnostics);
  if (!found->is_object()) return {};

  (void)read_string(*found, "expression", license_path, diagnostics);
  if (const auto terms =
          read_string(*found, "terms", license_path, diagnostics);
      terms && !starts_with_https(*terms)) {
    validate_existing_file(repository_root, *terms,
                           std::format("{}.terms", license_path), diagnostics);
  }
  const auto permitted_uses =
      validate_string_array(*found, "permitted_uses", license_path, diagnostics,
                            false, {"source", "documentation", "runtime"});
  const auto redistribution =
      read_string(*found, "redistribution", license_path, diagnostics);
  if (redistribution && *redistribution != "allowed" &&
      *redistribution != "prohibited") {
    add(diagnostics, std::format("{}.redistribution", license_path),
        "must be allowed or prohibited");
  } else if (redistribution && *redistribution == "prohibited") {
    add(diagnostics, std::format("{}.redistribution", license_path),
        "repository assets must permit redistribution");
  }
  const auto derivatives =
      read_string(*found, "derivatives", license_path, diagnostics);
  if (derivatives && *derivatives != "allowed" &&
      *derivatives != "prohibited" && *derivatives != "not-applicable") {
    add(diagnostics, std::format("{}.derivatives", license_path),
        "must be allowed, prohibited, or not-applicable");
  }
  (void)read_string(*found, "attribution", license_path, diagnostics);
  return {.derivatives = derivatives.value_or(""),
          .permitted_uses = {permitted_uses.begin(), permitted_uses.end()}};
}

auto validate_file_usage(const std::vector<std::string>& files,
                         const LicenseSummary& license, std::string_view path,
                         Diagnostics& diagnostics) -> void {
  for (std::size_t index = 0; index < files.size(); ++index) {
    const auto& file = files[index];
    const auto required =
        file.starts_with("assets/")
            ? "runtime"
            : (file.starts_with("docs/") ? "documentation" : "source");
    if (!license.permitted_uses.contains(required)) {
      add(diagnostics, std::format("{}.files[{}]", path, index),
          std::format("license must permit {} use for this path", required));
    }
  }
}

auto validate_generated(const Json& object, std::string_view path,
                        Diagnostics& diagnostics) -> void {
  validate_fields(object, path,
                  {"provider", "tool", "model", "model_version", "prompt",
                   "generation_date", "source_output", "seed"},
                  {}, diagnostics);
  if (!object.is_object()) return;
  for (const auto field : {"provider", "tool", "model", "model_version",
                           "prompt", "source_output"}) {
    (void)read_string(object, field, path, diagnostics);
  }
  validate_date(object, "generation_date", path, diagnostics);
  const auto seed = object.find("seed");
  if (seed == object.end()) return;
  validate_fields(*seed, std::format("{}.seed", path), {},
                  {"value", "unavailable"}, diagnostics);
  if (!seed->is_object()) return;
  const bool has_value = seed->contains("value");
  const bool has_unavailable = seed->contains("unavailable");
  if (has_value == has_unavailable) {
    add(diagnostics, std::format("{}.seed", path),
        "must contain exactly one of value or unavailable");
  }
  if (has_value) {
    (void)read_string(*seed, "value", std::format("{}.seed", path),
                      diagnostics);
  }
  if (has_unavailable) {
    (void)read_string(*seed, "unavailable", std::format("{}.seed", path),
                      diagnostics);
  }
}

auto validate_third_party(const Json& object, std::string_view path,
                          Diagnostics& diagnostics) -> void {
  validate_fields(object, path, {"retrieved", "upstream_license"},
                  {"author", "publisher", "canonical_url", "package"},
                  diagnostics);
  if (!object.is_object()) return;
  const auto author = read_string(object, "author", path, diagnostics);
  const auto publisher = read_string(object, "publisher", path, diagnostics);
  if (!author && !publisher) {
    add(diagnostics, std::string{path}, "must identify an author or publisher");
  }
  validate_date(object, "retrieved", path, diagnostics);
  (void)read_string(object, "upstream_license", path, diagnostics);
  const auto url = read_string(object, "canonical_url", path, diagnostics);
  if (url && !starts_with_https(*url)) {
    add(diagnostics, std::format("{}.canonical_url", path),
        "must be an https URL");
  }
  const auto package = object.find("package");
  if (package != object.end()) {
    validate_fields(*package, std::format("{}.package", path),
                    {"name", "version"}, {}, diagnostics);
    if (package->is_object()) {
      (void)read_string(*package, "name", std::format("{}.package", path),
                        diagnostics);
      (void)read_string(*package, "version", std::format("{}.package", path),
                        diagnostics);
    }
  }
  if (!url && package == object.end()) {
    add(diagnostics, std::string{path},
        "must identify a canonical URL or pinned package");
  }
}

[[nodiscard]] auto validate_code_authored(const Json& object,
                                          std::string_view path,
                                          Diagnostics& diagnostics)
    -> std::vector<std::string> {
  validate_fields(object, path,
                  {"author", "date", "construction", "derived_inputs"}, {},
                  diagnostics);
  if (!object.is_object()) return {};
  (void)read_string(object, "author", path, diagnostics);
  validate_date(object, "date", path, diagnostics);
  (void)read_string(object, "construction", path, diagnostics);
  return validate_string_array(object, "derived_inputs", path, diagnostics,
                               true);
}

auto validate_capture(const Json& object, std::string_view path,
                      Diagnostics& diagnostics) -> void {
  validate_fields(object, path,
                  {"application", "scenario", "application_version", "date",
                   "deterministic_inputs", "tooling"},
                  {}, diagnostics);
  if (!object.is_object()) return;
  for (const auto field : {"application", "scenario", "application_version"}) {
    (void)read_string(object, field, path, diagnostics);
  }
  validate_date(object, "date", path, diagnostics);
  (void)validate_string_array(object, "tooling", path, diagnostics, false);
  const auto inputs = object.find("deterministic_inputs");
  if (inputs == object.end()) return;
  validate_fields(*inputs, std::format("{}.deterministic_inputs", path), {},
                  {"values", "not_applicable"}, diagnostics);
  if (!inputs->is_object()) return;
  const bool has_values = inputs->contains("values");
  const bool has_not_applicable = inputs->contains("not_applicable");
  if (has_values == has_not_applicable) {
    add(diagnostics, std::format("{}.deterministic_inputs", path),
        "must contain exactly one of values or not_applicable");
  }
  if (has_values && !(*inputs)["values"].is_object()) {
    add(diagnostics, std::format("{}.deterministic_inputs.values", path),
        "must be an object");
  }
  if (has_not_applicable) {
    (void)read_string(*inputs, "not_applicable",
                      std::format("{}.deterministic_inputs", path),
                      diagnostics);
  }
}

[[nodiscard]] auto validate_derived(const Json& object, std::string_view path,
                                    Diagnostics& diagnostics)
    -> std::vector<std::string> {
  validate_fields(object, path,
                  {"parents", "transformation", "tooling", "date"}, {},
                  diagnostics);
  if (!object.is_object()) return {};
  auto parents =
      validate_string_array(object, "parents", path, diagnostics, false);
  (void)read_string(object, "transformation", path, diagnostics);
  (void)validate_string_array(object, "tooling", path, diagnostics, false);
  validate_date(object, "date", path, diagnostics);
  return parents;
}

[[nodiscard]] auto valid_asset_id(std::string_view value) -> bool {
  static const std::regex pattern{
      R"(^(visual|font|music|voice|sfx)/[a-z0-9]+(?:-[a-z0-9]+)*$)"};
  return value.size() <= kMaximumIdentifierBytes &&
         std::regex_match(value.begin(), value.end(), pattern);
}

[[nodiscard]] auto valid_asset_filename(std::string_view value) -> bool {
  static const std::regex pattern{
      R"(^[a-z0-9]+(?:-[a-z0-9]+)*(?:\.[a-z0-9]+)+$)"};
  return std::regex_match(value.begin(), value.end(), pattern);
}

auto validate_asset_path_convention(std::string_view encoded,
                                    std::string_view media_type,
                                    std::string_view path,
                                    Diagnostics& diagnostics) -> void {
  const std::filesystem::path asset_path{encoded};
  auto component = asset_path.begin();
  if (component == asset_path.end() || *component != "assets") return;
  ++component;
  if (component == asset_path.end() || component->string() != media_type) {
    add(diagnostics, std::string{path},
        "asset paths must use assets/<media-type>/<lower-kebab-name>.<ext>");
    return;
  }
  ++component;
  if (component == asset_path.end() ||
      !valid_asset_filename(component->string())) {
    add(diagnostics, std::string{path},
        "asset filename must use lower-kebab-name.<ext>");
    return;
  }
  ++component;
  if (component != asset_path.end()) {
    add(diagnostics, std::string{path},
        "asset paths must not add directories below the media type");
  }
}

auto validate_relationships(const std::vector<AssetSummary>& assets,
                            Diagnostics& diagnostics) -> void {
  std::unordered_map<std::string, std::size_t> indices;
  for (std::size_t index = 0; index < assets.size(); ++index) {
    indices.try_emplace(assets[index].id, index);
  }
  for (std::size_t index = 0; index < assets.size(); ++index) {
    for (std::size_t parent_index = 0;
         parent_index < assets[index].parents.size(); ++parent_index) {
      const auto& parent = assets[index].parents[parent_index];
      const auto found = indices.find(parent);
      const auto path =
          std::format("$.assets[{}].{}[{}]", assets[index].record_index,
                      assets[index].source_kind == "derived"
                          ? "derived.parents"
                          : "code_authored.derived_inputs",
                      parent_index);
      if (found == indices.end()) {
        add(diagnostics, path, "references an unknown asset ID");
      } else if (assets[found->second].derivatives != "allowed") {
        add(diagnostics, path, "parent license does not permit derived assets");
      }
    }
  }

  std::vector<unsigned char> state(assets.size());
  std::function<void(std::size_t)> visit = [&](std::size_t index) {
    if (state[index] == 2U) return;
    if (state[index] == 1U) {
      add(diagnostics, std::format("$.assets[{}]", assets[index].record_index),
          "asset parent graph contains a cycle");
      return;
    }
    state[index] = 1U;
    for (const auto& parent : assets[index].parents) {
      if (const auto found = indices.find(parent); found != indices.end()) {
        visit(found->second);
      }
    }
    state[index] = 2U;
  };
  for (std::size_t index = 0; index < assets.size(); ++index)
    visit(index);
}

} // namespace

auto validate_manifest_json(std::string_view json_text,
                            const std::filesystem::path& repository_root)
    -> Diagnostics {
  Diagnostics diagnostics;
  if (json_text.size() > kMaximumManifestBytes) {
    add(diagnostics, "$", "manifest exceeds 1048576 bytes");
    return diagnostics;
  }

  bool duplicate_key{};
  bool excessive_depth{};
  std::vector<std::unordered_set<std::string>> object_keys;
  const Json::parser_callback_t callback =
      [&](int depth, Json::parse_event_t event, Json& parsed) {
        if (depth > static_cast<int>(kMaximumManifestDepth)) {
          excessive_depth = true;
        }
        if (event == Json::parse_event_t::object_start) {
          object_keys.emplace_back();
        } else if (event == Json::parse_event_t::key) {
          if (object_keys.empty() ||
              !object_keys.back().insert(parsed.get<std::string>()).second) {
            duplicate_key = true;
          }
        } else if (event == Json::parse_event_t::object_end &&
                   !object_keys.empty()) {
          object_keys.pop_back();
        }
        return true;
      };

  Json root;
  try {
    root =
        Json::parse(json_text.begin(), json_text.end(), callback, true, false);
  } catch (const nlohmann::json::exception& error) {
    add(diagnostics, "$", std::format("malformed JSON: {}", error.what()));
    return diagnostics;
  }
  if (duplicate_key) {
    add(diagnostics, "$", "JSON objects cannot repeat a key");
  }
  if (excessive_depth) {
    add(diagnostics, "$", "manifest exceeds the maximum nesting depth of 32");
  }
  if (duplicate_key || excessive_depth) return diagnostics;

  validate_json_bounds(root, "$", diagnostics);
  validate_fields(root, "$", {"schema_version", "assets"}, {}, diagnostics);
  if (!root.is_object()) return diagnostics;
  const auto version = root.find("schema_version");
  if (version != root.end() &&
      (!version->is_number_unsigned() || version->get<std::uint64_t>() != 1U)) {
    add(diagnostics, "$.schema_version",
        "only asset provenance schema version 1 is supported");
  }
  const auto records = root.find("assets");
  if (records == root.end()) return diagnostics;
  if (!records->is_array()) {
    add(diagnostics, "$.assets", "must be an array");
    return diagnostics;
  }
  if (records->size() > kMaximumAssetRecords) {
    add(diagnostics, "$.assets", "cannot contain more than 1024 assets");
    return diagnostics;
  }

  std::unordered_set<std::string> ids;
  std::unordered_set<std::string> files;
  std::vector<AssetSummary> summaries;
  summaries.reserve(records->size());
  for (std::size_t index = 0; index < records->size(); ++index) {
    const auto path = std::format("$.assets[{}]", index);
    const auto& asset = (*records)[index];
    validate_fields(
        asset, path,
        {"id", "media_type", "source_kind", "files", "purpose", "edits",
         "license"},
        {"generated", "third_party", "code_authored", "capture", "derived"},
        diagnostics);
    if (!asset.is_object()) continue;

    auto id = read_string(asset, "id", path, diagnostics).value_or("");
    auto media_type =
        read_string(asset, "media_type", path, diagnostics).value_or("");
    auto source_kind =
        read_string(asset, "source_kind", path, diagnostics).value_or("");
    (void)read_string(asset, "purpose", path, diagnostics);
    (void)validate_string_array(asset, "edits", path, diagnostics, true);

    if (!id.empty() && !valid_asset_id(id)) {
      add(diagnostics, std::format("{}.id", path),
          "must be <media-type>/<lower-kebab-name> and at most 128 bytes");
    }
    if (!id.empty() && !ids.insert(id).second) {
      add(diagnostics, std::format("{}.id", path),
          "duplicates an earlier asset ID");
    }
    if (!contains({"visual", "font", "music", "voice", "sfx"}, media_type)) {
      add(diagnostics, std::format("{}.media_type", path),
          "unknown media type");
    } else if (!id.empty() && !id.starts_with(media_type + "/")) {
      add(diagnostics, std::format("{}.id", path),
          "ID namespace must match media_type");
    }
    if (!contains(
            {"generated", "third-party", "code-authored", "capture", "derived"},
            source_kind)) {
      add(diagnostics, std::format("{}.source_kind", path),
          "unknown source kind");
    }

    const auto encoded_files =
        validate_string_array(asset, "files", path, diagnostics, false);
    if (encoded_files.size() > kMaximumFilesPerAsset) {
      add(diagnostics, std::format("{}.files", path),
          "cannot contain more than 32 files");
    }
    for (std::size_t file_index = 0; file_index < encoded_files.size();
         ++file_index) {
      const auto file_path = std::format("{}.files[{}]", path, file_index);
      const auto& encoded = encoded_files[file_index];
      validate_existing_file(repository_root, encoded, file_path, diagnostics);
      if (contains({"generated", "third-party", "derived"}, source_kind) &&
          !encoded.starts_with("assets/")) {
        add(diagnostics, file_path,
            "selected generated, third-party, and derived media must live "
            "under assets/<media-type>");
      }
      validate_asset_path_convention(encoded, media_type, file_path,
                                     diagnostics);
      if (!files.insert(encoded).second) {
        add(diagnostics, file_path,
            "file is already owned by another provenance record");
      }
    }

    const auto license =
        validate_license(asset, path, repository_root, diagnostics);
    validate_file_usage(encoded_files, license, path, diagnostics);
    AssetSummary summary{.record_index = index,
                         .id = id,
                         .source_kind = source_kind,
                         .derivatives = license.derivatives,
                         .parents = {}};
    const std::array kind_fields{"generated", "third_party", "code_authored",
                                 "capture", "derived"};
    std::string expected_field;
    if (source_kind == "generated") expected_field = "generated";
    if (source_kind == "third-party") expected_field = "third_party";
    if (source_kind == "code-authored") expected_field = "code_authored";
    if (source_kind == "capture") expected_field = "capture";
    if (source_kind == "derived") expected_field = "derived";
    for (const auto field : kind_fields) {
      if (asset.contains(field) && field != expected_field) {
        add(diagnostics, std::format("{}.{}", path, field),
            "does not match source_kind");
      }
    }
    if (!expected_field.empty() && !asset.contains(expected_field)) {
      add(diagnostics, std::format("{}.{}", path, expected_field),
          "required source-kind record is missing");
    } else if (expected_field == "generated") {
      validate_generated(asset[expected_field],
                         std::format("{}.{}", path, expected_field),
                         diagnostics);
    } else if (expected_field == "third_party") {
      validate_third_party(asset[expected_field],
                           std::format("{}.{}", path, expected_field),
                           diagnostics);
    } else if (expected_field == "code_authored") {
      summary.parents = validate_code_authored(
          asset[expected_field], std::format("{}.{}", path, expected_field),
          diagnostics);
    } else if (expected_field == "capture") {
      validate_capture(asset[expected_field],
                       std::format("{}.{}", path, expected_field), diagnostics);
    } else if (expected_field == "derived") {
      summary.parents = validate_derived(
          asset[expected_field], std::format("{}.{}", path, expected_field),
          diagnostics);
    }
    summaries.push_back(std::move(summary));
  }
  validate_relationships(summaries, diagnostics);
  return diagnostics;
}

auto validate_manifest_file(const std::filesystem::path& manifest_path,
                            const std::filesystem::path& repository_root)
    -> Diagnostics {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(manifest_path, error);
  if (error || !std::filesystem::is_regular_file(status) ||
      std::filesystem::is_symlink(status)) {
    return {{"$", "manifest must be a readable regular file, not a symlink"}};
  }
  const auto size = std::filesystem::file_size(manifest_path, error);
  if (error) return {{"$", "cannot determine manifest size"}};
  if (size > kMaximumManifestBytes) {
    return {{"$", "manifest exceeds 1048576 bytes"}};
  }
  std::ifstream input{manifest_path, std::ios::binary};
  if (!input) return {{"$", "cannot open manifest"}};
  const std::string contents{std::istreambuf_iterator<char>{input},
                             std::istreambuf_iterator<char>{}};
  if (!input.good() && !input.eof()) return {{"$", "cannot read manifest"}};
  return validate_manifest_json(contents, repository_root);
}

} // namespace apsis_drift::asset_provenance
