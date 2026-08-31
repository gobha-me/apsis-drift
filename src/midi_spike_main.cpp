#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <format>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <sys/resource.h>

#include "apsis_drift/audio.hpp"
#include "midi_spike.hpp"

namespace {
using namespace apsis_drift;
using namespace apsis_drift::midi_spike;

struct Options {
  std::filesystem::path midi;
  std::filesystem::path soundfont;
  std::filesystem::path output;
  std::filesystem::path report;
  std::uint64_t blocks{2'000};
};

class MidiSpikeRenderSource final : public AudioRenderSource {
 public:
  explicit MidiSpikeRenderSource(MusicEngine& engine) : m_engine{engine} {}

  [[nodiscard]] auto render(
      std::span<float> interleaved_samples) noexcept
      -> std::optional<AudioBufferError> override {
    const auto error = m_engine.render(interleaved_samples);
    if (!error) return std::nullopt;
    return *error == MidiError::stopped ? AudioBufferError::stopped
                                        : AudioBufferError::invalid_dimensions;
  }

 private:
  MusicEngine& m_engine;
};

[[nodiscard]] auto parse_count(std::string_view text)
    -> std::optional<std::uint64_t> {
  std::uint64_t value{};
  if (text.empty()) return std::nullopt;
  for (const char character : text) {
    if (character < '0' || character > '9' ||
        value > (std::numeric_limits<std::uint64_t>::max() - 9U) / 10U) {
      return std::nullopt;
    }
    value = value * 10U + static_cast<unsigned>(character - '0');
  }
  if (value < 2'000U || value > 100'000U) return std::nullopt;
  return value;
}

[[nodiscard]] auto parse_options(int argc, char** argv)
    -> std::optional<Options> {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (index + 1 >= argc) return std::nullopt;
    const std::string_view value{argv[++index]};
    if (argument == "--midi") options.midi = value;
    else if (argument == "--soundfont") options.soundfont = value;
    else if (argument == "--output") options.output = value;
    else if (argument == "--report") options.report = value;
    else if (argument == "--blocks") {
      const auto count = parse_count(value);
      if (!count) return std::nullopt;
      options.blocks = *count;
    } else {
      return std::nullopt;
    }
  }
  if (options.midi.empty() || options.soundfont.empty() ||
      options.output.empty() || options.report.empty()) {
    return std::nullopt;
  }
  if (options.output != "/dev/null" && options.blocks > 10'000U) {
    return std::nullopt;
  }
  return options;
}

[[nodiscard]] auto read_file(const std::filesystem::path& path,
                             std::size_t maximum)
    -> std::optional<std::vector<std::byte>> {
  std::ifstream input{path, std::ios::binary | std::ios::ate};
  if (!input) return std::nullopt;
  const auto end = input.tellg();
  if (end <= 0 || static_cast<std::uint64_t>(end) > maximum) {
    return std::nullopt;
  }
  input.seekg(0);
  std::vector<std::byte> bytes(static_cast<std::size_t>(end));
  input.read(reinterpret_cast<char*>(bytes.data()), end);
  if (!input) return std::nullopt;
  return bytes;
}

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

auto write_wav(const std::filesystem::path& path,
               std::span<const std::int16_t> samples) -> bool {
  const auto data_bytes = samples.size() * sizeof(std::int16_t);
  if (data_bytes > std::numeric_limits<std::uint32_t>::max() - 36U) return false;
  std::ofstream output{path, std::ios::binary};
  output.write("RIFF", 4);
  write_le32(output, static_cast<std::uint32_t>(36U + data_bytes));
  output.write("WAVEfmt ", 8);
  write_le32(output, 16);
  write_le16(output, 1);
  write_le16(output, kAudioChannelCount);
  write_le32(output, kAudioSampleRate);
  write_le32(output, kAudioSampleRate * kAudioChannelCount * 2U);
  write_le16(output, kAudioChannelCount * 2U);
  write_le16(output, 16);
  output.write("data", 4);
  write_le32(output, static_cast<std::uint32_t>(data_bytes));
  output.write(reinterpret_cast<const char*>(samples.data()),
               static_cast<std::streamsize>(data_bytes));
  return static_cast<bool>(output);
}

auto update_hash(std::uint64_t& hash, float value) noexcept -> void {
  const auto word = std::bit_cast<std::uint32_t>(value);
  for (unsigned byte = 0; byte < 4; ++byte) {
    hash ^= (word >> (byte * 8U)) & 0xFFU;
    hash *= 1'099'511'628'211ULL;
  }
}

}  // namespace

auto main(int argc, char** argv) -> int {
  const auto options = parse_options(argc, argv);
  if (!options) {
    std::cerr << "usage: apsis-drift-midi-spike --midi FILE --soundfont FILE "
                 "--output FILE --report FILE [--blocks 2000..100000] "
                 "(WAV output is capped at 10000 blocks)\n";
    return 2;
  }
  const auto midi = read_file(options->midi, kMaximumMidiBytes);
  const auto soundfont = read_file(options->soundfont, kMaximumSoundFontBytes);
  if (!midi || !soundfont) {
    std::cerr << "cannot read bounded spike assets\n";
    return 1;
  }
  const auto schedule = parse_smf(*midi);
  if (!schedule) {
    std::cerr << "MIDI parse failed: " << midi_error_name(schedule.error())
              << '\n';
    return 1;
  }
  auto engine_result = MusicEngine::create(*schedule, *soundfont);
  if (!engine_result) {
    std::cerr << "music engine creation failed: "
              << midi_error_name(engine_result.error()) << '\n';
    return 1;
  }
  auto engine = std::move(*engine_result);
  if (engine.set_looping(true) || engine.play()) return 1;
  MidiSpikeRenderSource render_source{engine};

  constexpr std::size_t kBlockFrames{kAudioFramesPerSimulationTick};
  std::array<float, kBlockFrames * kAudioChannelCount> block{};
  std::vector<std::int16_t> wav;
  if (options->output != "/dev/null") {
    wav.reserve(static_cast<std::size_t>(options->blocks) * block.size());
  }
  std::vector<double> callback_milliseconds;
  callback_milliseconds.reserve(static_cast<std::size_t>(options->blocks));
  std::uint64_t checksum{1'469'598'103'934'665'603ULL};
  long double square_sum{};
  float peak{};
  float maximum_delta{};
  float previous{};
  std::uint64_t deadline_overruns{};

  for (std::uint64_t index = 0; index < options->blocks; ++index) {
    if (index == options->blocks / 5U) {
      if (engine.set_layer_target(MusicLayer::pulse, 0.7F,
                                  TransitionBoundary::next_measure)) {
        return 1;
      }
    }
    if (index == options->blocks * 2U / 5U) {
      if (engine.set_layer_target(MusicLayer::percussion, 0.65F,
                                  TransitionBoundary::next_beat)) {
        return 1;
      }
    }
    if (index == options->blocks * 3U / 5U) {
      if (engine.set_layer_target(MusicLayer::tension, 0.55F,
                                  TransitionBoundary::next_phrase)) {
        return 1;
      }
    }
    if (index == options->blocks * 4U / 5U) {
      if (engine.set_layer_target(MusicLayer::pulse, 0.0F,
                                  TransitionBoundary::immediate) ||
          engine.set_layer_target(MusicLayer::tension, 0.0F,
                                  TransitionBoundary::next_measure)) {
        return 1;
      }
    }
    const auto start = std::chrono::steady_clock::now();
    if (const auto error = render_source.render(block)) {
      std::cerr << "render source failed: "
                << (*error == AudioBufferError::stopped ? "stopped"
                                                        : "invalid-dimensions")
                << '\n';
      return 1;
    }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    const auto milliseconds =
        std::chrono::duration<double, std::milli>{elapsed}.count();
    callback_milliseconds.push_back(milliseconds);
    if (milliseconds >= 1000.0 * static_cast<double>(kBlockFrames) /
                            static_cast<double>(kAudioSampleRate)) {
      ++deadline_overruns;
    }
    for (const float sample : block) {
      if (!std::isfinite(sample) || sample < -1.0F || sample > 1.0F) {
        std::cerr << "non-finite or unbounded PCM\n";
        return 1;
      }
      peak = std::max(peak, std::abs(sample));
      maximum_delta = std::max(maximum_delta, std::abs(sample - previous));
      previous = sample;
      square_sum += static_cast<long double>(sample) * sample;
      update_hash(checksum, sample);
      if (options->output != "/dev/null") {
        wav.push_back(static_cast<std::int16_t>(
            std::lrint(std::clamp(sample, -1.0F, 1.0F) * 32767.0F)));
      }
    }
  }
  if (options->output != "/dev/null" && !write_wav(options->output, wav)) {
    std::cerr << "cannot write audition WAV\n";
    return 1;
  }

  std::ranges::sort(callback_milliseconds);
  const auto p99_index = std::min(
      callback_milliseconds.size() - 1U,
      static_cast<std::size_t>(static_cast<double>(callback_milliseconds.size()) *
                               0.99));
  const auto total_samples = options->blocks * block.size();
  const auto rms = std::sqrt(static_cast<double>(square_sum / total_samples));
  const auto average = std::ranges::fold_left(callback_milliseconds, 0.0,
                                               std::plus<>{}) /
                       static_cast<double>(callback_milliseconds.size());
  rusage usage{};
  (void)getrusage(RUSAGE_SELF, &usage);
  const auto diagnostics = engine.diagnostics();
  const bool transitions_complete =
      diagnostics.commands_applied == 7U &&
      diagnostics.command_queue_depth == 0U &&
      std::abs(diagnostics.layer_gains[0] - 1.0F) < 0.001F &&
      std::abs(diagnostics.layer_gains[1]) < 0.001F &&
      std::abs(diagnostics.layer_gains[2] - 0.65F) < 0.001F &&
      std::abs(diagnostics.layer_gains[3]) < 0.001F;
  if (!transitions_complete) {
    std::cerr << "semantic layer transitions did not complete\n";
    return 1;
  }

  std::ofstream report{options->report};
  report << std::format(
      "{{\n"
      "  \"schema_version\": 1,\n"
      "  \"decision_candidate\": \"TinySoundFont + Drift bounded SMF parser\",\n"
      "  \"schedule_checksum\": \"{}\",\n"
      "  \"pcm_checksum\": \"{}\",\n"
      "  \"blocks\": {},\n"
      "  \"sample_frames\": {},\n"
      "  \"peak\": {:.9f},\n"
      "  \"rms\": {:.9f},\n"
      "  \"maximum_sample_delta\": {:.9f},\n"
      "  \"callback_average_ms\": {:.9f},\n"
      "  \"callback_p99_ms\": {:.9f},\n"
      "  \"callback_maximum_ms\": {:.9f},\n"
      "  \"callback_deadline_overruns\": {},\n"
      "  \"peak_rss_kib\": {},\n"
      "  \"events_applied\": {},\n"
      "  \"loops\": {},\n"
      "  \"active_voices_at_end\": {}\n"
      "}}\n",
      schedule->checksum(), checksum, options->blocks,
      options->blocks * kBlockFrames, peak, rms, maximum_delta, average,
      callback_milliseconds[p99_index], callback_milliseconds.back(),
      deadline_overruns, usage.ru_maxrss, diagnostics.events_applied,
      diagnostics.loop_count, diagnostics.active_voices);
  if (!report) return 1;
  std::cout << "schedule_checksum=" << schedule->checksum()
            << " pcm_checksum=" << checksum
            << " p99_ms=" << callback_milliseconds[p99_index]
            << " max_delta=" << maximum_delta << '\n';
  constexpr double kP99BudgetMilliseconds{1000.0 * kBlockFrames /
                                           kAudioSampleRate / 2.0};
  return callback_milliseconds[p99_index] < kP99BudgetMilliseconds &&
                 maximum_delta < 0.1F
             ? 0
             : 1;
}
