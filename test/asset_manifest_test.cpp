#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <ranges>
#include <string>
#include <string_view>

#include "asset_manifest.hpp"

namespace {

using Json = nlohmann::ordered_json;
using apsis_drift::asset_provenance::Diagnostics;
using apsis_drift::asset_provenance::validate_manifest_json;

int failures{};

auto check(bool condition, std::string_view message) -> void {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %.*s\n", static_cast<int>(message.size()),
               message.data());
  ++failures;
}

struct TemporaryRepository {
  std::filesystem::path path;

  TemporaryRepository() {
    const auto identity =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() /
           std::format("apsis-drift-asset-manifest-test-{}", identity);
    std::filesystem::create_directories(path / "assets/visual");
    std::filesystem::create_directories(path / "assets/font");
    std::filesystem::create_directories(path / "assets/music");
    std::filesystem::create_directories(path / "assets/voice");
    std::filesystem::create_directories(path / "assets/sfx");
    std::ofstream{path / "LICENSE.md"} << "test license\n";
    std::ofstream{path / "source.cpp"} << "// fixture\n";
    std::ofstream{path / "capture.png"} << "fixture\n";
    std::ofstream{path / "assets/visual/derived-image.png"} << "fixture\n";
    std::ofstream{path / "assets/font/upstream-font.ttf"} << "fixture\n";
    std::ofstream{path / "assets/music/generated-score.ogg"} << "fixture\n";
    std::ofstream{path / "assets/voice/code-voice.wav"} << "fixture\n";
    std::ofstream{path / "assets/sfx/code-cue.wav"} << "fixture\n";
  }

  ~TemporaryRepository() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};

[[nodiscard]] auto license() -> Json {
  return {{"expression", "BSD-3-Clause"},
          {"terms", "LICENSE.md"},
          {"permitted_uses", {"source", "documentation", "runtime"}},
          {"redistribution", "allowed"},
          {"derivatives", "allowed"},
          {"attribution", "Fixture author"}};
}

[[nodiscard]] auto record(std::string id, std::string media_type,
                          std::string source_kind, std::string file) -> Json {
  return {{"id", std::move(id)},
          {"media_type", std::move(media_type)},
          {"source_kind", std::move(source_kind)},
          {"files", {std::move(file)}},
          {"purpose", "Validator fixture"},
          {"edits", Json::array()},
          {"license", license()}};
}

[[nodiscard]] auto valid_manifest() -> Json {
  auto generated = record("music/generated-score", "music", "generated",
                          "assets/music/generated-score.ogg");
  generated["generated"] = {
      {"provider", "Example provider"},
      {"tool", "Example generator"},
      {"model", "example-model"},
      {"model_version", "1"},
      {"prompt", "A bounded ambient score"},
      {"generation_date", "2026-08-30"},
      {"source_output", "provider-output-1"},
      {"seed", {{"unavailable", "provider did not expose a seed"}}}};

  auto third_party = record("font/upstream-font", "font", "third-party",
                            "assets/font/upstream-font.ttf");
  third_party["third_party"] = {
      {"publisher", "Example foundry"},
      {"canonical_url", "https://example.invalid/font"},
      {"retrieved", "2026-08-30"},
      {"upstream_license", "BSD-3-Clause"}};

  auto code =
      record("visual/code-panel", "visual", "code-authored", "source.cpp");
  code["code_authored"] = {{"author", "Fixture author"},
                           {"date", "2026-08-30"},
                           {"construction", "Authored directly in C++"},
                           {"derived_inputs", Json::array()}};

  auto capture =
      record("visual/test-capture", "visual", "capture", "capture.png");
  capture["capture"] = {
      {"application", "Apsis Drift"},
      {"scenario", "asset-manifest-test"},
      {"application_version", "0.4.42"},
      {"date", "2026-08-30"},
      {"deterministic_inputs", {{"values", {{"seed", 42}, {"tick", 120}}}}},
      {"tooling", {"fixture writer"}}};

  auto derived = record("visual/derived-image", "visual", "derived",
                        "assets/visual/derived-image.png");
  derived["derived"] = {{"parents", {"visual/test-capture"}},
                        {"transformation", "Lossless crop"},
                        {"tooling", {"fixture transformer"}},
                        {"date", "2026-08-30"}};

  auto voice = record("voice/code-voice", "voice", "code-authored",
                      "assets/voice/code-voice.wav");
  voice["code_authored"] = {{"author", "Fixture author"},
                            {"date", "2026-08-30"},
                            {"construction", "Synthesized by test code"},
                            {"derived_inputs", Json::array()}};

  auto sfx =
      record("sfx/code-cue", "sfx", "code-authored", "assets/sfx/code-cue.wav");
  sfx["code_authored"] = {{"author", "Fixture author"},
                          {"date", "2026-08-30"},
                          {"construction", "Synthesized by test code"},
                          {"derived_inputs", Json::array()}};

  return {
      {"schema_version", 1},
      {"assets", {generated, third_party, code, capture, derived, voice, sfx}}};
}

[[nodiscard]] auto mentions(const Diagnostics& diagnostics,
                            std::string_view text) -> bool {
  return std::ranges::any_of(diagnostics, [&](const auto& diagnostic) {
    return diagnostic.path.contains(text) || diagnostic.detail.contains(text);
  });
}

auto failure_matrix(const TemporaryRepository& repository) -> void {
  const auto valid = valid_manifest();
  check(validate_manifest_json(valid.dump(), repository.path).empty(),
        "all source kinds and media types must validate without binary media");

  auto candidate = valid;
  candidate["assets"][0]["generated"]["seed"] = {{"value", "42"}};
  check(validate_manifest_json(candidate.dump(), repository.path).empty(),
        "available generated seeds must validate");

  candidate = valid;
  candidate["assets"][1]["third_party"].erase("canonical_url");
  candidate["assets"][1]["third_party"]["package"] = {{"name", "example-font"},
                                                      {"version", "1.0"}};
  check(validate_manifest_json(candidate.dump(), repository.path).empty(),
        "pinned third-party packages must validate without a URL");

  candidate = valid;
  candidate["assets"][3]["capture"]["deterministic_inputs"] = {
      {"not_applicable", "capture is not produced by a deterministic path"}};
  check(validate_manifest_json(candidate.dump(), repository.path).empty(),
        "capture inputs may be explicitly inapplicable");

  candidate = valid;
  candidate["assets"][0]["generated"].erase("provider");
  check(mentions(validate_manifest_json(candidate.dump(), repository.path),
                 "provider"),
        "missing conditionally required generated fields must fail");

  candidate = valid;
  candidate["assets"][1]["third_party"].erase("publisher");
  check(mentions(validate_manifest_json(candidate.dump(), repository.path),
                 "author or publisher"),
        "third-party origin identity must be required");

  candidate = valid;
  candidate["assets"][2]["code_authored"].erase("construction");
  check(mentions(validate_manifest_json(candidate.dump(), repository.path),
                 "construction"),
        "code-authored construction evidence must be required");

  candidate = valid;
  candidate["assets"][3]["capture"].erase("tooling");
  check(mentions(validate_manifest_json(candidate.dump(), repository.path),
                 "tooling"),
        "capture tooling must be required");

  candidate = valid;
  candidate["assets"][4]["derived"].erase("parents");
  check(mentions(validate_manifest_json(candidate.dump(), repository.path),
                 "parents"),
        "derived parent evidence must be required");

  candidate = valid;
  candidate["assets"][0]["source_kind"] = "unknown";
  check(mentions(validate_manifest_json(candidate.dump(), repository.path),
                 "unknown source kind"),
        "unknown source kinds must fail");

  candidate = valid;
  candidate["assets"][1]["id"] = candidate["assets"][0]["id"];
  check(mentions(validate_manifest_json(candidate.dump(), repository.path),
                 "duplicates an earlier asset ID"),
        "duplicate IDs must fail");

  candidate = valid;
  candidate["assets"][0]["files"][0] = "assets/music/missing.ogg";
  check(mentions(validate_manifest_json(candidate.dump(), repository.path),
                 "does not exist"),
        "missing files must fail");

  candidate = valid;
  candidate["assets"][0]["files"][0] = "../outside.ogg";
  check(mentions(validate_manifest_json(candidate.dump(), repository.path),
                 "without traversal"),
        "paths outside the repository must fail");

  candidate = valid;
  candidate["assets"][1]["files"][0] = candidate["assets"][0]["files"][0];
  check(mentions(validate_manifest_json(candidate.dump(), repository.path),
                 "already owned"),
        "one file must not have multiple provenance owners");

  candidate = valid;
  candidate["assets"][0]["files"][0] = "capture.png";
  check(mentions(validate_manifest_json(candidate.dump(), repository.path),
                 "must live under assets"),
        "selected generated media must use the asset directory");

  candidate = valid;
  candidate["assets"][4]["derived"]["parents"][0] = "visual/missing";
  check(mentions(validate_manifest_json(candidate.dump(), repository.path),
                 "unknown asset ID"),
        "unknown parent IDs must fail");

  candidate = valid;
  candidate["assets"][2]["code_authored"]["derived_inputs"] = {
      "visual/derived-image"};
  candidate["assets"][4]["derived"]["parents"] = {"visual/code-panel"};
  check(mentions(validate_manifest_json(candidate.dump(), repository.path),
                 "contains a cycle"),
        "cyclic parent graphs must fail");

  candidate = valid;
  candidate["assets"][3]["license"]["derivatives"] = "prohibited";
  check(mentions(validate_manifest_json(candidate.dump(), repository.path),
                 "does not permit derived"),
        "derived assets must respect parent license constraints");

  candidate = valid;
  candidate["assets"][0]["license"]["redistribution"] = "prohibited";
  check(mentions(validate_manifest_json(candidate.dump(), repository.path),
                 "must permit redistribution"),
        "repository assets must permit redistribution");

  candidate = valid;
  candidate["assets"][0]["license"]["permitted_uses"] = {"documentation"};
  check(mentions(validate_manifest_json(candidate.dump(), repository.path),
                 "must permit runtime use"),
        "file placement and declared usage must remain compatible");

  candidate = valid;
  candidate["assets"][0]["unexpected"] = true;
  check(mentions(validate_manifest_json(candidate.dump(), repository.path),
                 "unknown field"),
        "unknown fields must fail closed");

  check(mentions(validate_manifest_json(
                     R"({"schema_version":1,"schema_version":1,"assets":[]})",
                     repository.path),
                 "cannot repeat a key"),
        "duplicate JSON keys must fail");
  check(
      mentions(validate_manifest_json("{", repository.path), "malformed JSON"),
      "malformed JSON must fail");
  check(mentions(validate_manifest_json(std::string((1U << 20U) + 1U, 'x'),
                                        repository.path),
                 "exceeds 1048576 bytes"),
        "oversized manifests must fail before parsing");
}

} // namespace

auto main() -> int {
  const TemporaryRepository repository;
  failure_matrix(repository);
  if (failures != 0) {
    std::fprintf(stderr, "%d asset manifest test(s) failed\n", failures);
    return 1;
  }
  std::puts("all asset manifest tests passed");
  return 0;
}
