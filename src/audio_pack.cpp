#include "apsis_drift/audio.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <ranges>
#include <set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "midi_spike.hpp"

namespace apsis_drift {
namespace {

using Json = nlohmann::json;
using midi_spike::MusicEngine;
using midi_spike::MusicLayer;
using midi_spike::TransitionBoundary;

inline constexpr std::size_t kMaximumSidecarBytes{16U * 1024U};
inline constexpr std::size_t kMaximumSfxPackagedBytes{1U * 1024U * 1024U};
inline constexpr std::size_t kMaximumSfxDecodedBytes{4U * 1024U * 1024U};
inline constexpr std::size_t kMaximumSfxFrames{kAudioSampleRate * 2U};
inline constexpr std::size_t kMaximumSfxAggregateFrames{kAudioSampleRate * 8U};
inline constexpr std::size_t kSfxVoiceCount{8};

struct SfxDefinition {
  AudioCueId cue;
  std::string_view file;
};

inline constexpr std::array kSfxDefinitions{
    SfxDefinition{kUiNavigateAudioCue, "sfx/ui-navigate.wav"},
    SfxDefinition{kUiConfirmAudioCue, "sfx/ui-confirm.wav"},
    SfxDefinition{kUiRejectAudioCue, "sfx/ui-reject.wav"},
    SfxDefinition{kCommsNoticeAudioCue, "sfx/comms-notice.wav"},
    SfxDefinition{kSignalLockAudioCue, "sfx/signal-lock.wav"},
    SfxDefinition{kSignalCompleteAudioCue, "sfx/signal-complete.wav"},
};

[[nodiscard]] auto little_u16(std::span<const std::byte> bytes,
                              std::size_t offset)
    -> std::optional<std::uint16_t> {
  if (offset > bytes.size() || bytes.size() - offset < 2U)
    return std::nullopt;
  return std::to_integer<std::uint16_t>(bytes[offset]) |
         (std::to_integer<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] auto little_u32(std::span<const std::byte> bytes,
                              std::size_t offset)
    -> std::optional<std::uint32_t> {
  if (offset > bytes.size() || bytes.size() - offset < 4U)
    return std::nullopt;
  return std::to_integer<std::uint32_t>(bytes[offset]) |
         (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
         (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
         (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] auto safe_regular_file(const std::filesystem::path &root,
                                     std::string_view relative)
    -> std::expected<std::filesystem::path, AudioPackError> {
  const std::filesystem::path encoded{relative};
  if (encoded.empty() || encoded.is_absolute() || encoded.has_root_path() ||
      std::ranges::any_of(encoded, [](const auto &component) {
        return component == ".." || component == "." || component.empty();
      })) {
    return std::unexpected{AudioPackError::unsafe_path};
  }
  std::error_code error;
  const auto canonical_root = std::filesystem::canonical(root, error);
  if (error || std::filesystem::is_symlink(
                   std::filesystem::symlink_status(root, error))) {
    return std::unexpected{error ? AudioPackError::missing
                                 : AudioPackError::unsafe_path};
  }
  auto candidate = canonical_root;
  for (const auto &component : encoded) {
    candidate /= component;
    const auto status = std::filesystem::symlink_status(candidate, error);
    if (error || status.type() == std::filesystem::file_type::not_found) {
      return std::unexpected{AudioPackError::missing};
    }
    if (std::filesystem::is_symlink(status)) {
      return std::unexpected{AudioPackError::unsafe_path};
    }
  }
  if (!std::filesystem::is_regular_file(candidate, error) || error) {
    return std::unexpected{AudioPackError::unsafe_path};
  }
  return candidate;
}

[[nodiscard]] auto read_bounded(const std::filesystem::path &path,
                                std::size_t maximum, AudioPackError too_large)
    -> std::expected<std::vector<std::byte>, AudioPackError> {
  std::ifstream input{path, std::ios::binary | std::ios::ate};
  if (!input)
    return std::unexpected{AudioPackError::missing};
  const auto end = input.tellg();
  if (end < 0 || static_cast<std::uint64_t>(end) > maximum) {
    return std::unexpected{too_large};
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(end));
  input.seekg(0);
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char *>(bytes.data()), end);
  }
  if (!input)
    return std::unexpected{AudioPackError::missing};
  return bytes;
}

[[nodiscard]] auto exact_keys(const Json &object,
                              std::initializer_list<std::string_view> keys)
    -> bool {
  if (!object.is_object() || object.size() != keys.size())
    return false;
  return std::ranges::all_of(keys, [&](std::string_view key) {
    return object.contains(std::string{key});
  });
}

[[nodiscard]] auto parse_sidecar(std::span<const std::byte> bytes)
    -> std::optional<Json> {
  bool invalid{};
  std::vector<std::set<std::string, std::less<>>> object_keys;
  const auto callback = [&](int depth, Json::parse_event_t event,
                            Json &parsed) {
    if (depth > 8)
      invalid = true;
    if (event == Json::parse_event_t::object_start)
      object_keys.emplace_back();
    if (event == Json::parse_event_t::key) {
      if (object_keys.empty() || !parsed.is_string() ||
          !object_keys.back().insert(parsed.get<std::string>()).second) {
        invalid = true;
      }
    }
    if (event == Json::parse_event_t::object_end && !object_keys.empty()) {
      object_keys.pop_back();
    }
    return true;
  };
  const auto first = reinterpret_cast<const char *>(bytes.data());
  auto parsed = Json::parse(first, first + bytes.size(), callback, false, true);
  if (invalid || parsed.is_discarded())
    return std::nullopt;
  return parsed;
}

[[nodiscard]] auto valid_sidecar(const Json &root) -> bool {
  if (!exact_keys(root, {"schema_version", "score", "bank", "master_gain",
                         "layers", "markers", "allowed_transitions"}) ||
      !root["schema_version"].is_number_unsigned() ||
      root["schema_version"].get<unsigned>() != 1U) {
    return false;
  }
  const auto valid_payload = [](const Json &value, std::string_view id,
                                std::string_view file) {
    return exact_keys(value, {"asset_id", "file"}) &&
           value["asset_id"].is_string() && value["file"].is_string() &&
           value["asset_id"] == id && value["file"] == file;
  };
  if (!valid_payload(root["score"], "music/first-light-score",
                     "music/first-light-score.mid") ||
      !valid_payload(root["bank"], "music/first-light-bank",
                     "music/first-light-bank.sf2")) {
    return false;
  }
  if (!root["master_gain"].is_number())
    return false;
  const double master_gain = root["master_gain"].get<double>();
  if (!std::isfinite(master_gain) || master_gain < 0.0 || master_gain > 1.0) {
    return false;
  }
  constexpr std::array<std::string_view, 4> names{"ambient", "pulse",
                                                  "percussion", "tension"};
  if (!root["layers"].is_array() || root["layers"].size() != names.size()) {
    return false;
  }
  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto &layer = root["layers"][index];
    if (!exact_keys(layer, {"id", "track", "default_gain"}) ||
        !layer["id"].is_string() || !layer["track"].is_string() ||
        layer["id"] != names[index] || layer["track"] != names[index] ||
        !layer["default_gain"].is_number()) {
      return false;
    }
    const double gain = layer["default_gain"].get<double>();
    if (!std::isfinite(gain) || gain < 0.0 || gain > 1.0)
      return false;
  }
  const auto &markers = root["markers"];
  if (!exact_keys(markers, {"loop_start", "loop_end", "phrase_prefix"}) ||
      markers["loop_start"] != "loop-start" ||
      markers["loop_end"] != "loop-end" ||
      markers["phrase_prefix"] != "phrase-") {
    return false;
  }
  constexpr std::array<std::string_view, 4> transitions{
      "immediate", "next_beat", "next_measure", "next_phrase"};
  if (!root["allowed_transitions"].is_array() ||
      root["allowed_transitions"].size() != transitions.size()) {
    return false;
  }
  std::set<std::string, std::less<>> found;
  for (const auto &transition : root["allowed_transitions"]) {
    if (!transition.is_string())
      return false;
    found.insert(transition.get<std::string>());
  }
  return std::ranges::all_of(transitions, [&](std::string_view transition) {
    return found.contains(transition);
  });
}

[[nodiscard]] auto decode_pcm16_mono_wav(std::span<const std::byte> bytes)
    -> std::optional<std::vector<float>> {
  if (bytes.size() < 44U || std::memcmp(bytes.data(), "RIFF", 4U) != 0 ||
      std::memcmp(bytes.data() + 8U, "WAVE", 4U) != 0) {
    return std::nullopt;
  }
  const auto riff_size = little_u32(bytes, 4U);
  if (!riff_size || static_cast<std::size_t>(*riff_size) + 8U != bytes.size()) {
    return std::nullopt;
  }
  bool format_found{};
  std::span<const std::byte> pcm;
  for (std::size_t offset = 12U; offset < bytes.size();) {
    if (bytes.size() - offset < 8U)
      return std::nullopt;
    const auto size = little_u32(bytes, offset + 4U);
    if (!size)
      return std::nullopt;
    const std::size_t data_offset = offset + 8U;
    if (*size > bytes.size() - data_offset)
      return std::nullopt;
    if (std::memcmp(bytes.data() + offset, "fmt ", 4U) == 0) {
      if (*size < 16U || little_u16(bytes, data_offset) != 1U ||
          little_u16(bytes, data_offset + 2U) != 1U ||
          little_u32(bytes, data_offset + 4U) != kAudioSampleRate ||
          little_u32(bytes, data_offset + 8U) != kAudioSampleRate * 2U ||
          little_u16(bytes, data_offset + 12U) != 2U ||
          little_u16(bytes, data_offset + 14U) != 16U) {
        return std::nullopt;
      }
      format_found = true;
    } else if (std::memcmp(bytes.data() + offset, "data", 4U) == 0) {
      if (!pcm.empty() || (*size & 1U) != 0U)
        return std::nullopt;
      pcm = bytes.subspan(data_offset, *size);
    }
    const std::size_t padded = static_cast<std::size_t>(*size) + (*size & 1U);
    if (padded > bytes.size() - data_offset)
      return std::nullopt;
    offset = data_offset + padded;
  }
  if (!format_found || pcm.empty() || pcm.size() / 2U > kMaximumSfxFrames) {
    return std::nullopt;
  }
  std::vector<float> samples(pcm.size() / 2U);
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const auto encoded = little_u16(pcm, index * 2U);
    if (!encoded)
      return std::nullopt;
    samples[index] =
        static_cast<float>(static_cast<std::int16_t>(*encoded)) / 32'768.0F;
  }
  return samples;
}

[[nodiscard]] auto decoded_soundfont_bytes(std::span<const std::byte> bytes)
    -> std::optional<std::size_t> {
  std::optional<std::size_t> decoded;
  for (std::size_t chunk_offset = 12U; chunk_offset < bytes.size();) {
    const auto chunk_size = little_u32(bytes, chunk_offset + 4U);
    if (!chunk_size || chunk_offset > bytes.size() - 8U)
      return std::nullopt;
    const auto data_offset = chunk_offset + 8U;
    if (*chunk_size > bytes.size() - data_offset)
      return std::nullopt;
    if (std::memcmp(bytes.data() + chunk_offset, "LIST", 4U) == 0 &&
        *chunk_size >= 4U &&
        std::memcmp(bytes.data() + data_offset, "sdta", 4U) == 0) {
      const auto list_end = data_offset + *chunk_size;
      for (std::size_t sample_offset = data_offset + 4U;
           sample_offset < list_end;) {
        const auto sample_size = little_u32(bytes, sample_offset + 4U);
        if (!sample_size || sample_offset > list_end - 8U)
          return std::nullopt;
        if (std::memcmp(bytes.data() + sample_offset, "smpl", 4U) == 0) {
          if (decoded)
            return std::nullopt;
          decoded = *sample_size;
        }
        const auto padded = static_cast<std::size_t>(*sample_size) +
                            static_cast<std::size_t>(*sample_size & 1U);
        if (padded > list_end - sample_offset - 8U)
          return std::nullopt;
        sample_offset += 8U + padded;
      }
    }
    const auto padded = static_cast<std::size_t>(*chunk_size) +
                        static_cast<std::size_t>(*chunk_size & 1U);
    if (padded > bytes.size() - data_offset)
      return std::nullopt;
    chunk_offset = data_offset + padded;
  }
  return decoded;
}

} // namespace

struct FirstLightAudioPack::Impl {
  struct Sample {
    AudioCueId cue;
    std::vector<float> frames;
  };
  struct Voice {
    const std::vector<float> *frames{};
    std::size_t cursor{};
  };

  MusicEngine music;
  std::array<float, 4> default_gains{};
  std::array<Sample, kSfxDefinitions.size()> samples;
  std::array<Voice, kSfxVoiceCount> voices{};
  std::size_t packaged{};
  std::size_t decoded{};
  std::uint64_t dropped{};
  MusicState state{MusicState::silent};

  explicit Impl(MusicEngine created) : music{std::move(created)} {}

  auto set_layers(std::array<float, 4> gains,
                  TransitionBoundary boundary) noexcept -> void {
    for (std::size_t index = 0; index < gains.size(); ++index) {
      (void)music.set_layer_target(static_cast<MusicLayer>(index), gains[index],
                                   boundary);
    }
  }
};

FirstLightAudioPack::FirstLightAudioPack(std::unique_ptr<Impl> impl) noexcept
    : m_impl{std::move(impl)} {}
FirstLightAudioPack::FirstLightAudioPack(FirstLightAudioPack &&) noexcept =
    default;
auto FirstLightAudioPack::operator=(FirstLightAudioPack &&) noexcept
    -> FirstLightAudioPack & = default;
FirstLightAudioPack::~FirstLightAudioPack() = default;

auto FirstLightAudioPack::packaged_bytes() const noexcept -> std::size_t {
  return m_impl ? m_impl->packaged : 0U;
}
auto FirstLightAudioPack::decoded_bytes() const noexcept -> std::size_t {
  return m_impl ? m_impl->decoded : 0U;
}

auto FirstLightAudioPack::cue(AudioCueId cue_id) noexcept -> void {
  if (!m_impl)
    return;
  const auto sample =
      std::ranges::find_if(m_impl->samples, [&](const Impl::Sample &candidate) {
        return candidate.cue == cue_id;
      });
  if (sample == m_impl->samples.end())
    return;
  const auto voice =
      std::ranges::find_if(m_impl->voices, [](const Impl::Voice &candidate) {
        return candidate.frames == nullptr;
      });
  if (voice == m_impl->voices.end()) {
    ++m_impl->dropped;
    return;
  }
  *voice = {.frames = &sample->frames};
}

auto FirstLightAudioPack::set_music_state(MusicState state) noexcept -> void {
  if (!m_impl || state == m_impl->state)
    return;
  m_impl->state = state;
  const auto scale = [&](std::array<float, 4> values) {
    for (std::size_t index = 0; index < values.size(); ++index) {
      values[index] *= m_impl->default_gains[index];
    }
    return values;
  };
  switch (state) {
  case MusicState::silent:
    m_impl->set_layers({}, TransitionBoundary::immediate);
    break;
  case MusicState::docked:
    m_impl->set_layers(scale({1.0F, 0.0F, 0.0F, 0.0F}),
                       TransitionBoundary::next_measure);
    break;
  case MusicState::flight:
    m_impl->set_layers(scale({1.0F, 1.0F, 0.0F, 0.0F}),
                       TransitionBoundary::next_measure);
    break;
  case MusicState::scanning:
    m_impl->set_layers(scale({0.9F, 1.0F, 1.0F, 0.0F}),
                       TransitionBoundary::next_beat);
    break;
  case MusicState::warning:
    m_impl->set_layers(scale({0.8F, 1.0F, 0.6F, 1.0F}),
                       TransitionBoundary::immediate);
    break;
  case MusicState::complete:
    m_impl->set_layers(scale({1.0F, 0.5F, 0.0F, 0.0F}),
                       TransitionBoundary::next_phrase);
    break;
  }
}

auto FirstLightAudioPack::pause_music() noexcept -> void {
  if (m_impl)
    (void)m_impl->music.pause();
}
auto FirstLightAudioPack::resume_music() noexcept -> void {
  if (m_impl)
    (void)m_impl->music.play();
}
auto FirstLightAudioPack::reset() noexcept -> void {
  if (!m_impl)
    return;
  for (auto &voice : m_impl->voices)
    voice = {};
  (void)m_impl->music.stop();
  (void)m_impl->music.set_looping(true);
  (void)m_impl->music.play();
  m_impl->state = MusicState::docked;
  set_music_state(MusicState::silent);
}

auto FirstLightAudioPack::render(std::span<float> interleaved_samples) noexcept
    -> bool {
  if (!m_impl || m_impl->music.render(interleaved_samples))
    return false;
  const auto frames = interleaved_samples.size() / kAudioChannelCount;
  for (std::size_t frame = 0; frame < frames; ++frame) {
    float sfx{};
    for (auto &voice : m_impl->voices) {
      if (!voice.frames)
        continue;
      sfx += (*voice.frames)[voice.cursor++] * 0.65F;
      if (voice.cursor == voice.frames->size())
        voice = {};
    }
    for (std::size_t channel = 0; channel < kAudioChannelCount; ++channel) {
      auto &output = interleaved_samples[frame * kAudioChannelCount + channel];
      output = std::clamp(output + sfx, -1.0F, 1.0F);
    }
  }
  return true;
}

auto FirstLightAudioPack::active_sfx_voices() const noexcept -> std::size_t {
  return m_impl ? static_cast<std::size_t>(
                      std::ranges::count_if(m_impl->voices,
                                            [](const Impl::Voice &voice) {
                                              return voice.frames != nullptr;
                                            }))
                : 0U;
}
auto FirstLightAudioPack::dropped_sfx_voices() const noexcept -> std::uint64_t {
  return m_impl ? m_impl->dropped : 0U;
}

auto load_first_light_audio_pack(const std::filesystem::path &asset_root)
    -> std::expected<FirstLightAudioPack, AudioPackError> {
  const auto sidecar_path =
      safe_regular_file(asset_root, "music/first-light-score.json");
  if (!sidecar_path)
    return std::unexpected{sidecar_path.error()};
  const auto sidecar = read_bounded(*sidecar_path, kMaximumSidecarBytes,
                                    AudioPackError::sidecar_too_large);
  if (!sidecar)
    return std::unexpected{sidecar.error()};
  const auto document = parse_sidecar(*sidecar);
  if (!document || !valid_sidecar(*document)) {
    return std::unexpected{AudioPackError::invalid_sidecar};
  }

  const auto score_path = safe_regular_file(
      asset_root, (*document)["score"]["file"].get<std::string>());
  const auto bank_path = safe_regular_file(
      asset_root, (*document)["bank"]["file"].get<std::string>());
  if (!score_path || !bank_path) {
    return std::unexpected{!score_path ? score_path.error()
                                       : bank_path.error()};
  }
  const auto score = read_bounded(*score_path, midi_spike::kMaximumMidiBytes,
                                  AudioPackError::packaged_budget_exceeded);
  const auto bank = read_bounded(*bank_path, midi_spike::kMaximumSoundFontBytes,
                                 AudioPackError::packaged_budget_exceeded);
  if (!score || !bank) {
    return std::unexpected{!score ? score.error() : bank.error()};
  }
  const auto schedule = midi_spike::parse_smf(*score);
  if (!schedule)
    return std::unexpected{AudioPackError::invalid_midi};
  auto engine = MusicEngine::create(*schedule, *bank);
  if (!engine)
    return std::unexpected{AudioPackError::invalid_soundfont};

  auto impl = std::make_unique<FirstLightAudioPack::Impl>(std::move(*engine));
  impl->packaged = score->size() + bank->size();
  const auto soundfont_decoded = decoded_soundfont_bytes(*bank);
  if (!soundfont_decoded ||
      *soundfont_decoded > midi_spike::kMaximumDecodedSoundFontBytes) {
    return std::unexpected{AudioPackError::decoded_budget_exceeded};
  }
  impl->decoded = *soundfont_decoded;
  impl->default_gains = {
      static_cast<float>(
          (*document)["layers"][0]["default_gain"].get<double>()),
      static_cast<float>(
          (*document)["layers"][1]["default_gain"].get<double>()),
      static_cast<float>(
          (*document)["layers"][2]["default_gain"].get<double>()),
      static_cast<float>(
          (*document)["layers"][3]["default_gain"].get<double>()),
  };

  std::size_t sfx_packaged{};
  std::size_t sfx_frames{};
  for (std::size_t index = 0; index < kSfxDefinitions.size(); ++index) {
    const auto &definition = kSfxDefinitions[index];
    const auto path = safe_regular_file(asset_root, definition.file);
    if (!path)
      return std::unexpected{path.error()};
    const auto encoded = read_bounded(*path, kMaximumSfxPackagedBytes,
                                      AudioPackError::packaged_budget_exceeded);
    if (!encoded)
      return std::unexpected{encoded.error()};
    auto decoded = decode_pcm16_mono_wav(*encoded);
    if (!decoded)
      return std::unexpected{AudioPackError::invalid_sfx};
    if (sfx_packaged > kMaximumSfxPackagedBytes - encoded->size()) {
      return std::unexpected{AudioPackError::packaged_budget_exceeded};
    }
    if (sfx_frames > kMaximumSfxAggregateFrames - decoded->size()) {
      return std::unexpected{AudioPackError::decoded_budget_exceeded};
    }
    sfx_packaged += encoded->size();
    sfx_frames += decoded->size();
    impl->samples[index] = {.cue = definition.cue,
                            .frames = std::move(*decoded)};
  }
  const std::size_t sfx_decoded =
      sfx_frames * kAudioChannelCount * sizeof(float);
  if (sfx_packaged > kMaximumSfxPackagedBytes ||
      sfx_decoded > kMaximumSfxDecodedBytes) {
    return std::unexpected{AudioPackError::decoded_budget_exceeded};
  }
  impl->packaged += sfx_packaged;
  impl->decoded += sfx_decoded;
  (void)impl->music.set_looping(true);
  (void)impl->music.set_music_volume(
      static_cast<float>((*document)["master_gain"].get<double>()));
  for (std::size_t index = 0; index < 4U; ++index) {
    (void)impl->music.set_layer_target(static_cast<MusicLayer>(index), 0.0F,
                                       TransitionBoundary::immediate);
  }
  if (impl->music.play()) {
    return std::unexpected{AudioPackError::invalid_soundfont};
  }
  return FirstLightAudioPack{std::move(impl)};
}

auto audio_pack_error_name(AudioPackError error) noexcept -> std::string_view {
  switch (error) {
  case AudioPackError::missing:
    return "missing";
  case AudioPackError::unsafe_path:
    return "unsafe-path";
  case AudioPackError::sidecar_too_large:
    return "sidecar-too-large";
  case AudioPackError::invalid_sidecar:
    return "invalid-sidecar";
  case AudioPackError::invalid_midi:
    return "invalid-midi";
  case AudioPackError::invalid_soundfont:
    return "invalid-soundfont";
  case AudioPackError::invalid_sfx:
    return "invalid-sfx";
  case AudioPackError::packaged_budget_exceeded:
    return "packaged-budget-exceeded";
  case AudioPackError::decoded_budget_exceeded:
    return "decoded-budget-exceeded";
  }
  return "unknown";
}

} // namespace apsis_drift
