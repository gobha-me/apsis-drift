#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <sf2cute.hpp>

namespace {

inline constexpr std::uint32_t kSampleRate{48'000};
inline constexpr std::size_t kWavePeriod{480};

auto make_periodic_wave(std::span<const double> harmonics, double amplitude)
    -> std::vector<std::int16_t> {
  const auto normalization =
      std::ranges::fold_left(harmonics, 0.0, [](double sum, double value) {
        return sum + std::abs(value);
      });
  if (!(normalization > 0.0) || !(amplitude > 0.0) || amplitude > 1.0) {
    throw std::runtime_error{"invalid periodic waveform"};
  }
  std::vector<std::int16_t> samples(kWavePeriod);
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const auto phase = 2.0 * std::numbers::pi * static_cast<double>(index) /
                       static_cast<double>(samples.size());
    double value{};
    for (std::size_t harmonic = 0; harmonic < harmonics.size(); ++harmonic) {
      value += harmonics[harmonic] *
               std::sin(static_cast<double>(harmonic + 1U) * phase);
    }
    samples[index] = static_cast<std::int16_t>(std::lrint(
        value / normalization * amplitude *
        static_cast<double>(std::numeric_limits<std::int16_t>::max())));
  }
  return samples;
}

auto make_soft_kick() -> std::vector<std::int16_t> {
  constexpr double kDurationSeconds{0.5};
  std::vector<std::int16_t> samples(
      static_cast<std::size_t>(kSampleRate * kDurationSeconds));
  double phase{};
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const auto time = static_cast<double>(index) / kSampleRate;
    const auto progress = time / kDurationSeconds;
    const auto frequency = 86.0 - 41.0 * progress;
    phase += 2.0 * std::numbers::pi * frequency / kSampleRate;
    const auto attack = std::min(1.0, static_cast<double>(index) / 96.0);
    const auto envelope = attack * std::exp(-10.0 * time);
    samples[index] = static_cast<std::int16_t>(std::lrint(
        std::sin(phase) * envelope * 0.52 *
        static_cast<double>(std::numeric_limits<std::int16_t>::max())));
  }
  return samples;
}

auto append_be16(std::vector<std::uint8_t> &bytes, std::uint16_t value)
    -> void {
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value));
}

auto append_be32(std::vector<std::uint8_t> &bytes, std::uint32_t value)
    -> void {
  bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value));
}

auto append_variable(std::vector<std::uint8_t> &bytes, std::uint32_t value)
    -> void {
  std::uint32_t buffer = value & 0x7FU;
  while ((value >>= 7U) != 0U)
    buffer = (buffer << 8U) | (value & 0x7FU) | 0x80U;
  for (;;) {
    bytes.push_back(static_cast<std::uint8_t>(buffer));
    if ((buffer & 0x80U) == 0U)
      break;
    buffer >>= 8U;
  }
}

auto append_meta(std::vector<std::uint8_t> &track, std::uint32_t delta,
                 std::uint8_t type, std::span<const std::uint8_t> payload)
    -> void {
  append_variable(track, delta);
  track.push_back(0xFFU);
  track.push_back(type);
  append_variable(track, static_cast<std::uint32_t>(payload.size()));
  track.insert(track.end(), payload.begin(), payload.end());
}

auto append_text_meta(std::vector<std::uint8_t> &track, std::uint32_t delta,
                      std::uint8_t type, const std::string &text) -> void {
  append_meta(
      track, delta, type,
      {reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
}

auto append_note(std::vector<std::uint8_t> &track, std::uint32_t delta,
                 std::uint8_t channel, std::uint8_t note,
                 std::uint32_t duration, std::uint8_t velocity) -> void {
  append_variable(track, delta);
  track.insert(track.end(),
               {static_cast<std::uint8_t>(0x90U | channel), note, velocity});
  append_variable(track, duration);
  track.insert(track.end(),
               {static_cast<std::uint8_t>(0x80U | channel), note, 0U});
}

auto begin_track(const std::string &name, std::uint8_t channel,
                 std::uint8_t program, bool conductor)
    -> std::vector<std::uint8_t> {
  std::vector<std::uint8_t> track;
  append_text_meta(track, 0, 0x03U, name);
  if (conductor) {
    const std::uint8_t tempo[]{0x07U, 0xA1U, 0x20U};
    const std::uint8_t signature[]{4U, 2U, 24U, 8U};
    append_meta(track, 0, 0x51U, tempo);
    append_meta(track, 0, 0x58U, signature);
    append_text_meta(track, 0, 0x06U, "loop-start");
    append_text_meta(track, 0, 0x06U, "phrase-a");
  }
  append_variable(track, 0);
  track.insert(track.end(),
               {static_cast<std::uint8_t>(0xC0U | channel), program});

  return track;
}

auto finish_track(std::vector<std::uint8_t> &track) -> void {
  append_meta(track, 0, 0x2FU, {});
}

auto make_ambient_track() -> std::vector<std::uint8_t> {
  auto track = begin_track("ambient", 0, 0, true);
  constexpr std::uint32_t kQuarter{480U};
  constexpr std::uint32_t kMeasure{4U * kQuarter};
  constexpr std::array<std::uint8_t, 4> kProgression{50, 46, 53, 48};
  for (unsigned measure = 0; measure < 8; ++measure) {
    append_note(track, 0, 0, kProgression[measure % kProgression.size()],
                kMeasure, 68);
    if (measure == 3U) {
      const std::uint8_t slower[]{0x09U, 0x27U, 0xC0U};
      append_meta(track, 0, 0x51U, slower);
      append_text_meta(track, 0, 0x06U, "phrase-b");
    }
  }
  append_text_meta(track, 0, 0x06U, "loop-end");
  finish_track(track);
  return track;
}

auto make_pulse_track() -> std::vector<std::uint8_t> {
  auto track = begin_track("pulse", 1, 1, false);
  constexpr std::uint32_t kQuarter{480U};
  constexpr std::array<std::array<std::uint8_t, 4>, 4> kArpeggios{{
      {62, 65, 69, 65},
      {58, 62, 65, 62},
      {65, 69, 72, 69},
      {60, 64, 67, 64},
  }};
  for (unsigned measure = 0; measure < 8; ++measure) {
    for (const auto note : kArpeggios[measure % kArpeggios.size()]) {
      append_note(track, 0, 1, note, kQuarter, 62);
    }
  }
  finish_track(track);
  return track;
}

auto make_percussion_track() -> std::vector<std::uint8_t> {
  auto track = begin_track("percussion", 9, 2, false);
  constexpr std::uint32_t kQuarter{480U};
  for (unsigned measure = 0; measure < 8; ++measure) {
    append_note(track, measure == 0U ? 0U : kQuarter, 9, 36, kQuarter, 64);
    append_note(track, kQuarter, 9, 36, kQuarter, 52);
  }
  finish_track(track);
  return track;
}

auto make_tension_track() -> std::vector<std::uint8_t> {
  auto track = begin_track("tension", 2, 3, false);
  constexpr std::uint32_t kQuarter{480U};
  constexpr std::uint32_t kMeasure{4U * kQuarter};
  constexpr std::array<std::uint8_t, 4> kCounterline{57, 53, 60, 55};
  for (unsigned measure = 0; measure < 8; ++measure) {
    append_note(track, 0, 2, kCounterline[measure % kCounterline.size()],
                kMeasure, 54);
  }
  finish_track(track);
  return track;
}

auto write_midi(const char *path) -> void {
  const std::vector<std::vector<std::uint8_t>> tracks{
      make_ambient_track(),
      make_pulse_track(),
      make_percussion_track(),
      make_tension_track(),
  };
  std::vector<std::uint8_t> bytes;
  bytes.insert(bytes.end(), {'M', 'T', 'h', 'd'});
  append_be32(bytes, 6);
  append_be16(bytes, 1);
  append_be16(bytes, static_cast<std::uint16_t>(tracks.size()));
  append_be16(bytes, 480);
  for (const auto &track : tracks) {
    bytes.insert(bytes.end(), {'M', 'T', 'r', 'k'});
    append_be32(bytes, static_cast<std::uint32_t>(track.size()));
    bytes.insert(bytes.end(), track.begin(), track.end());
  }
  std::ofstream output{path, std::ios::binary};
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output)
    throw std::runtime_error{"cannot write MIDI output"};
}

struct Envelope {
  std::int16_t attack_timecents{};
  std::int16_t decay_timecents{};
  std::int16_t sustain_centibels{};
  std::int16_t release_timecents{};
  std::int16_t attenuation_centibels{};
  bool loop{};
};

auto add_preset(sf2cute::SoundFont &soundfont, const std::string &name,
                std::vector<std::int16_t> samples, std::uint16_t preset,
                std::uint8_t root_key, const Envelope &envelope) -> void {
  const auto sample_count = static_cast<std::uint32_t>(samples.size());
  auto sample = soundfont.NewSample(name, std::move(samples), 0, sample_count,
                                    kSampleRate, root_key, 0);
  std::vector<sf2cute::SFGeneratorItem> generators{
      {sf2cute::SFGenerator::kAttackVolEnv, envelope.attack_timecents},
      {sf2cute::SFGenerator::kDecayVolEnv, envelope.decay_timecents},
      {sf2cute::SFGenerator::kSustainVolEnv, envelope.sustain_centibels},
      {sf2cute::SFGenerator::kReleaseVolEnv, envelope.release_timecents},
      {sf2cute::SFGenerator::kInitialAttenuation,
       envelope.attenuation_centibels},
  };
  if (envelope.loop) {
    generators.emplace_back(
        sf2cute::SFGenerator::kSampleModes,
        static_cast<std::uint16_t>(sf2cute::SampleMode::kLoopContinuously));
  }
  auto instrument = soundfont.NewInstrument(
      name, std::vector<sf2cute::SFInstrumentZone>{
                sf2cute::SFInstrumentZone(sample, std::move(generators), {})});
  (void)soundfont.NewPreset(
      name, preset, 0,
      std::vector<sf2cute::SFPresetZone>{sf2cute::SFPresetZone(instrument)});
}

} // namespace

auto main(int argc, char **argv) -> int {
  if (argc != 3) {
    std::cerr << "usage: issue230_asset_builder OUTPUT.sf2 OUTPUT.mid\n";
    return 2;
  }
  try {
    sf2cute::SoundFont soundfont;
    soundfont.set_sound_engine("TinySoundFont");
    soundfont.set_bank_name("Apsis Drift issue 230 tonal prototype");
    constexpr std::array<double, 5> kAmbientHarmonics{1.0, 0.32, 0.13, 0.0,
                                                      0.04};
    constexpr std::array<double, 5> kPulseHarmonics{1.0, 0.18, 0.08, 0.03,
                                                    0.01};
    constexpr std::array<double, 5> kTensionHarmonics{1.0, 0.0, 0.16, 0.0,
                                                      0.04};
    add_preset(soundfont, "ambient",
               make_periodic_wave(kAmbientHarmonics, 0.42), 0, 43,
               {.attack_timecents = -4'372,
                .decay_timecents = -1'200,
                .sustain_centibels = 0,
                .release_timecents = -2'786,
                .attenuation_centibels = 0,
                .loop = true});
    add_preset(soundfont, "pulse", make_periodic_wave(kPulseHarmonics, 0.36), 1,
               43,
               {.attack_timecents = -7'200,
                .decay_timecents = -1'586,
                .sustain_centibels = 520,
                .release_timecents = -3'600,
                .attenuation_centibels = 40,
                .loop = true});
    add_preset(soundfont, "percussion", make_soft_kick(), 2, 36,
               {.attack_timecents = -12'000,
                .decay_timecents = -12'000,
                .sustain_centibels = 0,
                .release_timecents = -5'200,
                .attenuation_centibels = 20,
                .loop = false});
    add_preset(soundfont, "tension",
               make_periodic_wave(kTensionHarmonics, 0.34), 3, 43,
               {.attack_timecents = -3'600,
                .decay_timecents = -1'200,
                .sustain_centibels = 0,
                .release_timecents = -2'400,
                .attenuation_centibels = 50,
                .loop = true});
    std::ofstream output{argv[1], std::ios::binary};
    soundfont.Write(output);
    if (!output)
      throw std::runtime_error{"cannot write SoundFont output"};
    write_midi(argv[2]);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
