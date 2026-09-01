#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/audio.hpp"

#ifndef APSIS_DRIFT_AUDIO_ASSET_DIR
#error "APSIS_DRIFT_AUDIO_ASSET_DIR must be defined"
#endif

namespace {
using namespace apsis_drift;

int failures{};

auto check(bool condition, std::string_view message) -> void {
  if (condition)
    return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}

struct TemporaryAssets {
  std::filesystem::path path;

  TemporaryAssets() {
    const auto identity =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() /
           ("apsis-drift-audio-pack-test-" + std::to_string(identity));
    std::filesystem::create_directories(path);
    std::filesystem::copy(std::filesystem::path{APSIS_DRIFT_AUDIO_ASSET_DIR},
                          path, std::filesystem::copy_options::recursive);
  }

  ~TemporaryAssets() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};

[[nodiscard]] auto read_text(const std::filesystem::path &path) -> std::string {
  std::ifstream input{path};
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

auto write_text(const std::filesystem::path &path, std::string_view contents)
    -> void {
  std::ofstream output{path, std::ios::trunc};
  output << contents;
}

auto production_pack_contract() -> void {
  const std::filesystem::path root{APSIS_DRIFT_AUDIO_ASSET_DIR};
  auto loaded = load_first_light_audio_pack(root);
  check(loaded.has_value(), "the committed First Light pack must load");
  if (!loaded)
    return;
  check(loaded->packaged_bytes() <= 5U * 1024U * 1024U,
        "the combined committed payload must remain bounded");
  check(loaded->decoded_bytes() <= 20U * 1024U * 1024U,
        "the combined decoded payload must remain bounded");

  auto pack = std::make_unique<FirstLightAudioPack>(std::move(*loaded));
  AudioRuntime runtime{
      AudioRuntimeMode::no_device, nullptr, {}, std::move(pack)};
  MusicDirector director{runtime};
  director.update(MusicState::docked);
  director.update(MusicState::flight);
  director.update(MusicState::scanning);
  director.update(MusicState::warning);
  check(director.state() == MusicState::warning,
        "the director must retain its current semantic state");

  constexpr std::array cues{kUiNavigateAudioCue, kUiConfirmAudioCue,
                            kUiRejectAudioCue,   kCommsNoticeAudioCue,
                            kSignalLockAudioCue, kSignalCompleteAudioCue};
  for (const auto cue : cues) {
    check(runtime.emit(SimulationTick{1}, cue).status ==
              AudioEmitStatus::queued,
          "each stable production cue must enter the audio queue");
  }

  std::vector<float> samples(4096U * kAudioChannelCount);
  check(!runtime.render(samples), "the production pack must render");
  check(std::ranges::all_of(samples,
                            [](float sample) {
                              return std::isfinite(sample) && sample >= -1.0F &&
                                     sample <= 1.0F;
                            }),
        "mixed output must remain finite and clamped");
  check(
      std::ranges::any_of(samples, [](float sample) { return sample != 0.0F; }),
      "the production cue trace must be audible");
  auto diagnostics = runtime.diagnostics();
  check(diagnostics.asset_pack_loaded &&
            diagnostics.music_state == MusicState::warning &&
            diagnostics.active_sfx_voices == cues.size() &&
            diagnostics.dropped_sfx_voices == 0,
        "pack state and fixed voice diagnostics must be observable");

  director.pause();
  director.pause();
  director.resume();
  director.resume();
  director.update(MusicState::complete);
  check(runtime.diagnostics().music_state == MusicState::complete,
        "completion must be forwarded as a semantic transition");
  runtime.reset(AudioResetReason::return_to_title);
  director.reset();
  diagnostics = runtime.diagnostics();
  check(diagnostics.music_state == MusicState::silent &&
            diagnostics.active_sfx_voices == 0 &&
            director.state() == MusicState::silent,
        "a playback epoch reset must clear music and transient voices");

  std::vector<float> invalid(3U, 0.25F);
  check(runtime.render(invalid) == AudioBufferError::invalid_dimensions &&
            std::ranges::all_of(invalid,
                                [](float sample) { return sample == 0.25F; }),
        "invalid callback dimensions must not mutate caller memory");
}

auto failure_contract() -> void {
  const auto missing = load_first_light_audio_pack(
      std::filesystem::temp_directory_path() /
      "apsis-drift-audio-pack-deliberately-missing");
  check(!missing && missing.error() == AudioPackError::missing,
        "a missing asset root must fail closed");

  TemporaryAssets assets;
  const auto sidecar = assets.path / "music/first-light-score.json";
  const auto original_sidecar = read_text(sidecar);
  write_text(sidecar, R"({"schema_version":1,"schema_version":1})");
  auto loaded = load_first_light_audio_pack(assets.path);
  check(!loaded && loaded.error() == AudioPackError::invalid_sidecar,
        "duplicate sidecar keys must fail closed");
  write_text(sidecar, original_sidecar);

  const auto wav = assets.path / "sfx/ui-navigate.wav";
  const auto displaced = assets.path / "sfx/ui-navigate-original.wav";
  std::filesystem::rename(wav, displaced);
  std::filesystem::create_symlink(displaced.filename(), wav);
  loaded = load_first_light_audio_pack(assets.path);
  check(!loaded && loaded.error() == AudioPackError::unsafe_path,
        "symlinked payloads must fail before decoding");
  std::filesystem::remove(wav);
  std::filesystem::rename(displaced, wav);

  std::ofstream{wav, std::ios::binary | std::ios::trunc} << "not a wave";
  loaded = load_first_light_audio_pack(assets.path);
  check(!loaded && loaded.error() == AudioPackError::invalid_sfx,
        "malformed WAV payloads must fail before playback");
}

} // namespace

auto main() -> int {
  production_pack_contract();
  failure_contract();
  if (failures == 0) {
    std::cout << "First Light audio pack contract passed\n";
  }
  return failures == 0 ? 0 : 1;
}
