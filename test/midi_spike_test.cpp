#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include "apsis_drift/audio.hpp"
#include "midi_spike.hpp"

#ifndef APSIS_DRIFT_MIDI_ASSET_DIR
#error "APSIS_DRIFT_MIDI_ASSET_DIR must be defined"
#endif

namespace {
using namespace apsis_drift;
using namespace apsis_drift::midi_spike;

int failures{};

auto check(bool condition, std::string_view message) -> void {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}

[[nodiscard]] auto read_file(const std::filesystem::path& path)
    -> std::vector<std::byte> {
  std::ifstream input{path, std::ios::binary | std::ios::ate};
  if (!input) return {};
  const auto size = input.tellg();
  input.seekg(0);
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  return bytes;
}

auto hash_sample(std::uint64_t& hash, float sample) noexcept -> void {
  const auto word = std::bit_cast<std::uint32_t>(sample);
  for (unsigned byte = 0; byte < 4; ++byte) {
    hash ^= (word >> (byte * 8U)) & 0xFFU;
    hash *= 1'099'511'628'211ULL;
  }
}

struct RenderEvidence {
  std::uint64_t checksum{1'469'598'103'934'665'603ULL};
  float peak{};
  float maximum_delta{};
  bool finite{true};
  bool audible{};
  std::uint64_t events_applied{};
};

[[nodiscard]] auto render_trace(const MidiSchedule& schedule,
                                std::span<const std::byte> soundfont,
                                std::size_t chunk_frames) -> RenderEvidence {
  auto created = MusicEngine::create(schedule, soundfont);
  if (!created) return {.finite = false};
  auto engine = std::move(*created);
  if (engine.set_looping(false) || engine.play()) return {.finite = false};
  constexpr std::size_t kFrames{80'000};
  std::vector<float> buffer(chunk_frames * kAudioChannelCount);
  RenderEvidence evidence;
  float previous{};
  for (std::size_t rendered = 0; rendered < kFrames;) {
    const auto frames = std::min(chunk_frames, kFrames - rendered);
    const auto samples = std::span{buffer}.first(frames * kAudioChannelCount);
    if (engine.render(samples)) return {.finite = false};
    for (const auto sample : samples) {
      if (!std::isfinite(sample) || sample < -1.0F || sample > 1.0F) {
        evidence.finite = false;
      }
      evidence.peak = std::max(evidence.peak, std::abs(sample));
      evidence.maximum_delta =
          std::max(evidence.maximum_delta, std::abs(sample - previous));
      evidence.audible = evidence.audible || sample != 0.0F;
      previous = sample;
      hash_sample(evidence.checksum, sample);
    }
    rendered += frames;
  }
  evidence.events_applied = engine.diagnostics().events_applied;
  return evidence;
}

}  // namespace

auto main() -> int {
  const std::filesystem::path asset_root{APSIS_DRIFT_MIDI_ASSET_DIR};
  const auto midi = read_file(asset_root / "issue230-layer-demo.mid");
  const auto soundfont = read_file(asset_root / "issue230-mechsounds.sf2");
  check(!midi.empty() && midi.size() <= kMaximumMidiBytes,
        "the MIDI fixture must exist inside its byte budget");
  check(!soundfont.empty() && soundfont.size() <= kMaximumSoundFontBytes,
        "the SoundFont fixture must exist inside its byte budget");

  const auto parsed = parse_smf(midi);
  check(parsed.has_value(), "the bounded MIDI fixture must parse");
  if (!parsed) return 1;
  check(parsed->format == 1 && parsed->ppq == 480 &&
            parsed->tracks.size() == kMusicLayerCount &&
            parsed->event_count == 68 && parsed->tempos.size() == 2 &&
            parsed->time_signatures.size() == 1 &&
            parsed->markers.size() == 4,
        "the fixture must retain tracks, events, tempo, meter, and markers");
  check(parsed->tracks[0].name == "ambient" &&
            parsed->tracks[1].name == "pulse" &&
            parsed->tracks[2].name == "percussion" &&
            parsed->tracks[3].name == "tension",
        "track identity must be semantic rather than channel-based");
  check(parsed->checksum() == parse_smf(midi)->checksum(),
        "schedule checksums must repeat exactly");

  check(parse_smf({}).error() == MidiError::empty,
        "empty MIDI must fail closed");
  std::vector<std::byte> oversized(kMaximumMidiBytes + 1U);
  check(parse_smf(oversized).error() == MidiError::too_large,
        "oversized MIDI must fail before parsing");
  auto truncated = midi;
  truncated.pop_back();
  check(!parse_smf(truncated), "truncated MIDI must fail");
  auto invalid_ppq = midi;
  invalid_ppq[12] = std::byte{0};
  invalid_ppq[13] = std::byte{0};
  check(parse_smf(invalid_ppq).error() == MidiError::invalid_ppq,
        "zero PPQ must fail before allocation or commit");
  auto invalid_tracks = midi;
  invalid_tracks[10] = std::byte{0};
  invalid_tracks[11] = std::byte{17};
  check(parse_smf(invalid_tracks).error() == MidiError::invalid_track_count,
        "track counts above the contract must fail closed");
  auto invalid_chunk = midi;
  invalid_chunk[14] = std::byte{'X'};
  check(parse_smf(invalid_chunk).error() == MidiError::invalid_chunk,
        "invalid track chunk identities must fail closed");
  auto invalid_variable = midi;
  invalid_variable[81] = std::byte{0x81};
  invalid_variable[82] = std::byte{0x80};
  invalid_variable[83] = std::byte{0x80};
  invalid_variable[84] = std::byte{0x80};
  check(parse_smf(invalid_variable).error() ==
            MidiError::invalid_variable_length,
        "unterminated four-byte variable lengths must fail closed");
  auto invalid_running_status = midi;
  invalid_running_status[75] = std::byte{0};
  check(parse_smf(invalid_running_status).error() ==
            MidiError::invalid_running_status,
        "data without channel running status must fail closed");
  auto invalid_meta = midi;
  invalid_meta[36] = std::byte{2};
  check(parse_smf(invalid_meta).error() == MidiError::invalid_meta_event,
        "invalid fixed-size meta events must fail closed");
  auto missing_end = midi;
  missing_end[missing_end.size() - 2U] = std::byte{1};
  check(parse_smf(missing_end).error() == MidiError::missing_end_of_track,
        "tracks without an explicit end marker must fail closed");
  auto unsupported_message = midi;
  const auto note_on = std::ranges::search(
      unsupported_message,
      std::array{std::byte{0}, std::byte{0x90}, std::byte{0x30},
                 std::byte{0x54}});
  check(!note_on.empty(), "the fixture must contain the first note-on event");
  if (!note_on.empty()) {
    note_on[1] = std::byte{0xA0};
    check(parse_smf(unsupported_message).error() == MidiError::invalid_event,
          "unsupported channel message classes must fail closed");
  }

  auto invalid_schedule = *parsed;
  invalid_schedule.ppq = 0;
  check(MusicEngine::create(invalid_schedule, soundfont).error() ==
            MidiError::invalid_ppq,
        "constructed schedules must be revalidated before time projection");
  invalid_schedule = *parsed;
  invalid_schedule.tracks[0].events.resize(kMaximumMidiEvents + 1U);
  invalid_schedule.event_count = kMaximumMidiEvents + 1U;
  check(MusicEngine::create(invalid_schedule, soundfont).error() ==
            MidiError::too_many_events,
        "constructed schedules above the event budget must fail closed");
  invalid_schedule = *parsed;
  invalid_schedule.tracks[0].name = "unknown";
  check(MusicEngine::create(invalid_schedule, soundfont).error() ==
            MidiError::missing_layer,
        "unknown semantic layer names must fail before synthesis");
  invalid_schedule = *parsed;
  std::erase_if(invalid_schedule.markers,
                [](const Marker& marker) { return marker.name == "loop-end"; });
  check(MusicEngine::create(invalid_schedule, soundfont).error() ==
            MidiError::invalid_loop,
        "a score without an explicit loop region must fail closed");

  std::vector<std::byte> invalid_soundfont{
      std::byte{static_cast<unsigned char>('n')},
      std::byte{static_cast<unsigned char>('o')}};
  check(MusicEngine::create(*parsed, invalid_soundfont).error() ==
            MidiError::invalid_soundfont,
        "invalid SoundFont bytes must fail before playback");
  std::vector<std::byte> oversized_soundfont(kMaximumSoundFontBytes + 1U);
  check(MusicEngine::create(*parsed, oversized_soundfont).error() ==
            MidiError::soundfont_too_large,
        "oversized SoundFonts must fail before decoding");
  auto invalid_riff_size = soundfont;
  invalid_riff_size[4] = std::byte{0};
  invalid_riff_size[5] = std::byte{0};
  invalid_riff_size[6] = std::byte{0};
  invalid_riff_size[7] = std::byte{0};
  check(MusicEngine::create(*parsed, invalid_riff_size).error() ==
            MidiError::invalid_soundfont,
        "inconsistent RIFF dimensions must fail before synthesis");
  auto oversized_decoded_soundfont = soundfont;
  const auto sample_chunk = std::ranges::search(
      oversized_decoded_soundfont,
      std::array{std::byte{'s'}, std::byte{'m'}, std::byte{'p'}, std::byte{'l'}});
  check(!sample_chunk.empty(), "the fixture must contain an smpl chunk");
  if (!sample_chunk.empty()) {
    const auto offset = static_cast<std::size_t>(
        sample_chunk.begin() - oversized_decoded_soundfont.begin());
    constexpr std::uint32_t kOversizedDecoded =
        static_cast<std::uint32_t>(kMaximumDecodedSoundFontBytes + 2U);
    for (unsigned byte = 0; byte < 4; ++byte) {
      oversized_decoded_soundfont[offset + 4U + byte] =
          std::byte{static_cast<unsigned char>(kOversizedDecoded >>
                                               (byte * 8U))};
    }
    check(MusicEngine::create(*parsed, oversized_decoded_soundfont).error() ==
              MidiError::soundfont_too_large,
          "declared decoded sample payloads above budget must fail closed");
  }

  const auto whole_tick = render_trace(*parsed, soundfont, 400);
  const auto whole_tick_repeat = render_trace(*parsed, soundfont, 400);
  const auto split_tick = render_trace(*parsed, soundfont, 137);
  check(whole_tick.finite && whole_tick.audible && whole_tick.peak <= 1.0F &&
            whole_tick.maximum_delta < 0.1F,
        "offline MIDI synthesis must be finite, bounded, audible, and click-free");
  check(whole_tick.checksum == whole_tick_repeat.checksum &&
            whole_tick.peak == whole_tick_repeat.peak &&
            whole_tick.maximum_delta == whole_tick_repeat.maximum_delta,
        "same-toolchain PCM must repeat exactly");
  check(whole_tick.events_applied == split_tick.events_applied &&
            std::abs(whole_tick.peak - split_tick.peak) < 0.001F &&
            split_tick.finite && split_tick.audible &&
            split_tick.maximum_delta < 0.1F,
        "callback partitioning must preserve scheduling and bounded PCM properties");

  auto created = MusicEngine::create(*parsed, soundfont);
  check(created.has_value(), "the curated bank must initialize the synth");
  if (created) {
    auto engine = std::move(*created);
    check(!engine.set_music_volume(0.25F),
          "bounded music volume must be accepted");
    check(engine.set_music_volume(std::numeric_limits<float>::infinity()) ==
              MidiError::invalid_gain,
          "non-finite music volume must fail closed");
    check(!engine.play(), "the initialized engine must queue play");
    check(!engine.pause(), "pause must queue without blocking the caller");
    std::array<float, kAudioFramesPerSimulationTick * kAudioChannelCount>
        block{};
    block.fill(9.0F);
    check(!engine.render(block) &&
              std::ranges::all_of(block,
                                  [](float value) { return value == 0.0F; }),
          "pause must preserve position while producing silence");
    check(!engine.play(), "a paused engine must queue resume");
    check(!engine.set_layer_target(MusicLayer::pulse, 0.7F,
                                   TransitionBoundary::next_beat),
          "a bounded semantic layer command must queue");
    for (unsigned count = 0; count < 80; ++count) {
      check(!engine.render(block), "valid callback buffers must render");
    }
    check(engine.diagnostics().layer_gains[1] > 0.0F &&
              engine.diagnostics().commands_applied == 5,
          "next-beat activation must apply with a gain ramp");
    check(engine.render(std::span<float>{block}.first(3)) ==
              MidiError::invalid_buffer,
          "odd callback dimensions must fail before touching synth state");

    auto queue_engine_result = MusicEngine::create(*parsed, soundfont);
    check(queue_engine_result.has_value(),
          "a second engine must initialize for queue bounds");
    if (queue_engine_result) {
      auto queue_engine = std::move(*queue_engine_result);
      for (std::size_t index = 0; index < kMusicCommandCapacity; ++index) {
        check(!queue_engine.set_layer_target(
                  MusicLayer::ambient, index % 2U == 0U ? 0.0F : 1.0F,
                  TransitionBoundary::immediate),
              "commands inside the fixed queue budget must be accepted");
      }
      check(queue_engine.set_layer_target(MusicLayer::ambient, 0.5F,
                                          TransitionBoundary::immediate) ==
                MidiError::command_queue_full,
            "queue overflow must reject the newest command");
    }
    check(!engine.stop(), "stop must queue without touching callback state");
    block.fill(9.0F);
    check(!engine.render(block) &&
              std::ranges::all_of(block, [](float value) { return value == 0.0F; }),
          "stop must clear voices and render silence without stale notes");
  }

  if (failures == 0) std::cout << "MIDI spike tests passed\n";
  return failures == 0 ? 0 : 1;
}
