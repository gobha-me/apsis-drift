#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>

#include "apsis_drift/audio.hpp"

namespace {
using namespace apsis_drift;

inline constexpr std::uint64_t kAuditionTicks{7'200U};

auto write_le16(std::ostream& output, std::uint16_t value) -> void {
  const char bytes[]{static_cast<char>(value), static_cast<char>(value >> 8U)};
  output.write(bytes, 2);
}

auto write_le32(std::ostream& output, std::uint32_t value) -> void {
  const char bytes[]{static_cast<char>(value), static_cast<char>(value >> 8U),
                     static_cast<char>(value >> 16U),
                     static_cast<char>(value >> 24U)};
  output.write(bytes, 4);
}

auto write_wav_header(std::ostream& output) -> bool {
  constexpr std::uint64_t kDataBytes =
      kAuditionTicks * kAudioFramesPerSimulationTick * kAudioChannelCount *
      sizeof(std::int16_t);
  static_assert(kDataBytes <= std::numeric_limits<std::uint32_t>::max() - 36U);
  output.write("RIFF", 4);
  write_le32(output, static_cast<std::uint32_t>(36U + kDataBytes));
  output.write("WAVEfmt ", 8);
  write_le32(output, 16U);
  write_le16(output, 1U);
  write_le16(output, kAudioChannelCount);
  write_le32(output, kAudioSampleRate);
  write_le32(output, kAudioSampleRate * kAudioChannelCount * 2U);
  write_le16(output, kAudioChannelCount * 2U);
  write_le16(output, 16U);
  output.write("data", 4);
  write_le32(output, static_cast<std::uint32_t>(kDataBytes));
  return static_cast<bool>(output);
}

auto update_hash(std::uint64_t& hash, float value) noexcept -> void {
  const auto word = std::bit_cast<std::uint32_t>(value);
  for (unsigned byte = 0; byte < 4U; ++byte) {
    hash ^= (word >> (byte * 8U)) & 0xFFU;
    hash *= 1'099'511'628'211ULL;
  }
}

auto update_trace(MusicDirector& director, AudioRuntime& runtime,
                  SimulationTick tick, bool music_only) -> bool {
  const auto emit = [&](AudioCueId cue) {
    return music_only ||
           runtime.emit(tick, cue).status == AudioEmitStatus::queued;
  };
  switch (tick) {
    case 0U: director.update(MusicState::docked); break;
    case 120U: return emit(kUiNavigateAudioCue);
    case 240U: return emit(kUiConfirmAudioCue);
    case 1'200U:
      director.update(MusicState::flight);
      return emit(kCommsNoticeAudioCue);
    case 2'400U:
      director.update(MusicState::scanning);
      return emit(kSignalLockAudioCue);
    case 3'600U:
      director.update(MusicState::warning);
      return emit(kUiRejectAudioCue);
    case 4'800U:
      director.update(MusicState::complete);
      return emit(kSignalCompleteAudioCue);
    case 5'400U:
      if (!music_only) director.pause();
      break;
    case 5'520U:
      if (!music_only) director.resume();
      break;
    case 6'000U: director.update(MusicState::docked); break;
    default: break;
  }
  return true;
}

} // namespace

auto main(int argc, char** argv) -> int {
  if (argc != 4 && argc != 5) {
    std::cerr << "usage: apsis-drift-audio-pack-audition ASSET_ROOT "
                 "OUTPUT.wav REPORT.json [--music-only]\n";
    return 2;
  }
  const bool music_only =
      argc == 5 && std::string_view{argv[4]} == "--music-only";
  if (argc == 5 && !music_only) return 2;
  auto loaded = load_first_light_audio_pack(argv[1]);
  if (!loaded) {
    std::cerr << "audio pack load failed: "
              << audio_pack_error_name(loaded.error()) << '\n';
    return 1;
  }
  const auto packaged = loaded->packaged_bytes();
  const auto decoded = loaded->decoded_bytes();
  auto pack = std::make_unique<FirstLightAudioPack>(std::move(*loaded));
  AudioRuntime runtime{
      AudioRuntimeMode::no_device, nullptr, {}, std::move(pack)};
  MusicDirector director{runtime};
  std::ofstream output{argv[2], std::ios::binary};
  if (!write_wav_header(output)) return 1;

  std::array<float, kAudioFramesPerSimulationTick * kAudioChannelCount> block{};
  std::array<std::int16_t, kAudioFramesPerSimulationTick * kAudioChannelCount>
      encoded{};
  std::uint64_t checksum{1'469'598'103'934'665'603ULL};
  float peak{};
  float maximum_delta{};
  float previous{};
  for (std::uint64_t value = 0; value < kAuditionTicks; ++value) {
    const SimulationTick tick{value};
    if (!update_trace(director, runtime, tick, music_only) ||
        runtime.render(block)) {
      return 1;
    }
    for (std::size_t index = 0; index < block.size(); ++index) {
      const float sample = block[index];
      if (!std::isfinite(sample) || sample < -1.0F || sample > 1.0F) {
        return 1;
      }
      peak = std::max(peak, std::abs(sample));
      maximum_delta = std::max(maximum_delta, std::abs(sample - previous));
      previous = sample;
      update_hash(checksum, sample);
      encoded[index] = static_cast<std::int16_t>(
          std::lrint(std::clamp(sample, -1.0F, 1.0F) * 32767.0F));
    }
    output.write(
        reinterpret_cast<const char*>(encoded.data()),
        static_cast<std::streamsize>(encoded.size() * sizeof(std::int16_t)));
  }
  if (!output) return 1;

  const auto diagnostics = runtime.diagnostics();
  std::ofstream report{argv[3]};
  report << "{\n"
         << "  \"schema_version\": 1,\n"
         << "  \"music_only\": " << (music_only ? "true" : "false") << ",\n"
         << "  \"duration_seconds\": 60,\n"
         << "  \"sample_rate\": " << kAudioSampleRate << ",\n"
         << "  \"channels\": " << static_cast<unsigned>(kAudioChannelCount)
         << ",\n"
         << "  \"packaged_bytes\": " << packaged << ",\n"
         << "  \"decoded_bytes\": " << decoded << ",\n"
         << "  \"checksum\": " << checksum << ",\n"
         << "  \"peak\": " << peak << ",\n"
         << "  \"maximum_delta\": " << maximum_delta << ",\n"
         << "  \"dropped_events\": " << diagnostics.events_dropped << ",\n"
         << "  \"dropped_sfx_voices\": " << diagnostics.dropped_sfx_voices
         << "\n"
         << "}\n";
  return report ? 0 : 1;
}
