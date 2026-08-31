#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <sf2cute.hpp>

namespace {

auto read_pcm16(const char* path) -> std::vector<std::int16_t> {
  std::ifstream input{path, std::ios::binary | std::ios::ate};
  if (!input) throw std::runtime_error{"cannot open PCM input"};
  const auto bytes = input.tellg();
  if (bytes <= 0 || bytes % 2 != 0) {
    throw std::runtime_error{"invalid PCM input length"};
  }
  input.seekg(0);
  std::vector<std::int16_t> samples(
      static_cast<std::size_t>(bytes) / sizeof(std::int16_t));
  input.read(reinterpret_cast<char*>(samples.data()), bytes);
  if (!input) throw std::runtime_error{"cannot read PCM input"};
  return samples;
}

auto append_be16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
    -> void {
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value));
}

auto append_be32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    -> void {
  bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value));
}

auto append_variable(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    -> void {
  std::uint32_t buffer = value & 0x7FU;
  while ((value >>= 7U) != 0U) buffer = (buffer << 8U) | (value & 0x7FU) | 0x80U;
  for (;;) {
    bytes.push_back(static_cast<std::uint8_t>(buffer));
    if ((buffer & 0x80U) == 0U) break;
    buffer >>= 8U;
  }
}

auto append_meta(std::vector<std::uint8_t>& track, std::uint32_t delta,
                 std::uint8_t type, std::span<const std::uint8_t> payload)
    -> void {
  append_variable(track, delta);
  track.push_back(0xFFU);
  track.push_back(type);
  append_variable(track, static_cast<std::uint32_t>(payload.size()));
  track.insert(track.end(), payload.begin(), payload.end());
}

auto append_text_meta(std::vector<std::uint8_t>& track, std::uint32_t delta,
                      std::uint8_t type, const std::string& text) -> void {
  append_meta(track, delta, type,
              {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

auto append_note(std::vector<std::uint8_t>& track, std::uint32_t delta,
                 std::uint8_t channel, std::uint8_t note,
                 std::uint32_t duration, std::uint8_t velocity) -> void {
  append_variable(track, delta);
  track.insert(track.end(), {static_cast<std::uint8_t>(0x90U | channel), note,
                             velocity});
  append_variable(track, duration);
  track.insert(track.end(), {static_cast<std::uint8_t>(0x80U | channel), note,
                             0U});
}

auto make_track(const std::string& name, std::uint8_t channel,
                std::uint8_t program, std::uint8_t root,
                bool conductor) -> std::vector<std::uint8_t> {
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
  track.insert(track.end(), {static_cast<std::uint8_t>(0xC0U | channel), program});

  constexpr std::uint32_t kQuarter{480U};
  constexpr std::uint32_t kMeasure{4U * kQuarter};
  for (unsigned measure = 0; measure < 8; ++measure) {
    const auto note = static_cast<std::uint8_t>(root + (measure % 4U));
    append_note(track, measure == 0 ? 0U : kMeasure - kQuarter, channel, note,
                kQuarter, static_cast<std::uint8_t>(84U + measure));
    if (conductor && measure == 3U) {
      const std::uint8_t slower[]{0x09U, 0x27U, 0xC0U};
      append_meta(track, 0, 0x51U, slower);
      append_text_meta(track, 0, 0x06U, "phrase-b");
    }
  }
  if (conductor) append_text_meta(track, 0, 0x06U, "loop-end");
  append_meta(track, 0, 0x2FU, {});
  return track;
}

auto write_midi(const char* path) -> void {
  const std::vector<std::vector<std::uint8_t>> tracks{
      make_track("ambient", 0, 0, 48, true),
      make_track("pulse", 1, 1, 60, false),
      make_track("percussion", 9, 2, 36, false),
      make_track("tension", 2, 3, 55, false),
  };
  std::vector<std::uint8_t> bytes;
  bytes.insert(bytes.end(), {'M', 'T', 'h', 'd'});
  append_be32(bytes, 6);
  append_be16(bytes, 1);
  append_be16(bytes, static_cast<std::uint16_t>(tracks.size()));
  append_be16(bytes, 480);
  for (const auto& track : tracks) {
    bytes.insert(bytes.end(), {'M', 'T', 'r', 'k'});
    append_be32(bytes, static_cast<std::uint32_t>(track.size()));
    bytes.insert(bytes.end(), track.begin(), track.end());
  }
  std::ofstream output{path, std::ios::binary};
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!output) throw std::runtime_error{"cannot write MIDI output"};
}

auto add_preset(sf2cute::SoundFont& soundfont, const std::string& name,
                std::vector<std::int16_t> samples, std::uint16_t preset,
                std::uint8_t root_key) -> void {
  const auto sample_count = static_cast<std::uint32_t>(samples.size());
  auto sample = soundfont.NewSample(name, std::move(samples), 0, sample_count,
                                    48'000, root_key, 0);
  auto instrument = soundfont.NewInstrument(
      name, std::vector<sf2cute::SFInstrumentZone>{
                sf2cute::SFInstrumentZone(sample)});
  (void)soundfont.NewPreset(
      name, preset, 0,
      std::vector<sf2cute::SFPresetZone>{sf2cute::SFPresetZone(instrument)});
}

}  // namespace

auto main(int argc, char** argv) -> int {
  if (argc != 7) {
    std::cerr << "usage: issue230_asset_builder AMBIENT.raw PULSE.raw "
                 "PERCUSSION.raw TENSION.raw OUTPUT.sf2 OUTPUT.mid\n";
    return 2;
  }
  try {
    sf2cute::SoundFont soundfont;
    soundfont.set_sound_engine("TinySoundFont");
    soundfont.set_bank_name("Apsis Drift issue 230 MechSounds subset");
    add_preset(soundfont, "ambient", read_pcm16(argv[1]), 0, 48);
    add_preset(soundfont, "pulse", read_pcm16(argv[2]), 1, 60);
    add_preset(soundfont, "percussion", read_pcm16(argv[3]), 2, 36);
    add_preset(soundfont, "tension", read_pcm16(argv[4]), 3, 55);
    std::ofstream output{argv[5], std::ios::binary};
    soundfont.Write(output);
    if (!output) throw std::runtime_error{"cannot write SoundFont output"};
    write_midi(argv[6]);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
