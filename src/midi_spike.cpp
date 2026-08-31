#include "midi_spike.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <ranges>
#include <utility>

#include "apsis_drift/audio.hpp"

#define TSF_IMPLEMENTATION
#define TSF_NO_STDIO
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnull-pointer-subtraction"
#endif
#include <tsf.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace apsis_drift::midi_spike {
namespace {

class Reader {
 public:
  explicit Reader(std::span<const std::byte> bytes) : m_bytes{bytes} {}

  [[nodiscard]] auto remaining() const noexcept -> std::size_t {
    return m_bytes.size() - m_position;
  }
  [[nodiscard]] auto position() const noexcept -> std::size_t {
    return m_position;
  }
  [[nodiscard]] auto read_u8(std::uint8_t& value) noexcept -> bool {
    if (remaining() < 1) return false;
    value = std::to_integer<std::uint8_t>(m_bytes[m_position++]);
    return true;
  }
  [[nodiscard]] auto read_be16(std::uint16_t& value) noexcept -> bool {
    std::uint8_t first{};
    std::uint8_t second{};
    if (!read_u8(first) || !read_u8(second)) return false;
    value = static_cast<std::uint16_t>((first << 8U) | second);
    return true;
  }
  [[nodiscard]] auto read_be32(std::uint32_t& value) noexcept -> bool {
    std::uint8_t bytes[4]{};
    if (!read_u8(bytes[0]) || !read_u8(bytes[1]) || !read_u8(bytes[2]) ||
        !read_u8(bytes[3])) {
      return false;
    }
    value = (static_cast<std::uint32_t>(bytes[0]) << 24U) |
            (static_cast<std::uint32_t>(bytes[1]) << 16U) |
            (static_cast<std::uint32_t>(bytes[2]) << 8U) | bytes[3];
    return true;
  }
  [[nodiscard]] auto read_variable(std::uint32_t& value) noexcept -> bool {
    value = 0;
    for (unsigned index = 0; index < 4; ++index) {
      std::uint8_t byte{};
      if (!read_u8(byte) || value > 0x01FF'FFFFU) return false;
      value = (value << 7U) | (byte & 0x7FU);
      if ((byte & 0x80U) == 0U) return true;
    }
    return false;
  }
  [[nodiscard]] auto read_bytes(std::size_t count,
                                std::span<const std::byte>& value) noexcept
      -> bool {
    if (count > remaining()) return false;
    value = m_bytes.subspan(m_position, count);
    m_position += count;
    return true;
  }

 private:
  std::span<const std::byte> m_bytes;
  std::size_t m_position{};
};

[[nodiscard]] auto valid_text(std::span<const std::byte> bytes) noexcept
    -> bool {
  return !bytes.empty() && bytes.size() <= 64U &&
         std::ranges::all_of(bytes, [](std::byte value) {
           const auto character = std::to_integer<unsigned char>(value);
           return character >= 0x20U && character <= 0x7EU;
         });
}

[[nodiscard]] auto make_text(std::span<const std::byte> bytes) -> std::string {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[nodiscard]] auto little_endian_u32(std::span<const std::byte> bytes,
                                     std::size_t offset) noexcept
    -> std::optional<std::uint32_t> {
  if (offset > bytes.size() || bytes.size() - offset < 4U) return std::nullopt;
  return std::to_integer<std::uint32_t>(bytes[offset]) |
         (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
         (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
         (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] auto validate_soundfont(
    std::span<const std::byte> bytes) noexcept -> std::optional<MidiError> {
  if (bytes.size() < 12U || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
      std::memcmp(bytes.data() + 8U, "sfbk", 4) != 0) {
    return MidiError::invalid_soundfont;
  }
  const auto riff_size = little_endian_u32(bytes, 4U);
  if (!riff_size || *riff_size != bytes.size() - 8U) {
    return MidiError::invalid_soundfont;
  }

  bool found_samples{};
  std::size_t chunk_offset{12U};
  while (chunk_offset < bytes.size()) {
    const auto chunk_size = little_endian_u32(bytes, chunk_offset + 4U);
    if (!chunk_size || chunk_offset > bytes.size() - 8U) {
      return MidiError::invalid_soundfont;
    }
    const auto data_offset = chunk_offset + 8U;
    if (*chunk_size > bytes.size() - data_offset) {
      return MidiError::invalid_soundfont;
    }
    if (std::memcmp(bytes.data() + chunk_offset, "LIST", 4) == 0 &&
        *chunk_size >= 4U &&
        std::memcmp(bytes.data() + data_offset, "sdta", 4) == 0) {
      std::size_t sample_offset{data_offset + 4U};
      const auto list_end = data_offset + *chunk_size;
      while (sample_offset < list_end) {
        const auto sample_size = little_endian_u32(bytes, sample_offset + 4U);
        if (!sample_size || sample_offset > list_end - 8U) {
          return MidiError::invalid_soundfont;
        }
        if (std::memcmp(bytes.data() + sample_offset, "smpl", 4) == 0) {
          if (found_samples || (*sample_size & 1U) != 0U) {
            return MidiError::invalid_soundfont;
          }
          if (*sample_size > kMaximumDecodedSoundFontBytes) {
            return MidiError::soundfont_too_large;
          }
          found_samples = true;
        }
        const auto padded_size = static_cast<std::size_t>(*sample_size) +
                                 (*sample_size & 1U);
        if (padded_size > list_end - (sample_offset + 8U)) {
          return MidiError::invalid_soundfont;
        }
        sample_offset += 8U + padded_size;
      }
    }
    const auto padded_size =
        static_cast<std::size_t>(*chunk_size) + (*chunk_size & 1U);
    if (padded_size > bytes.size() - data_offset) {
      return MidiError::invalid_soundfont;
    }
    chunk_offset = data_offset + padded_size;
  }
  return found_samples ? std::nullopt
                       : std::optional{MidiError::invalid_soundfont};
}

[[nodiscard]] auto append_event(MidiSchedule& schedule, MidiTrack& track,
                                MidiEvent event) -> std::optional<MidiError> {
  if (schedule.event_count >= kMaximumMidiEvents) {
    return MidiError::too_many_events;
  }
  event.order = static_cast<std::uint32_t>(schedule.event_count);
  track.events.push_back(event);
  ++schedule.event_count;
  return std::nullopt;
}

[[nodiscard]] auto parse_track(std::span<const std::byte> bytes,
                               std::uint16_t track_index,
                               MidiSchedule& schedule)
    -> std::optional<MidiError> {
  Reader reader{bytes};
  MidiTrack track;
  std::uint32_t absolute_tick{};
  std::uint8_t running_status{};
  bool ended{};
  while (reader.remaining() != 0U) {
    std::uint32_t delta{};
    if (!reader.read_variable(delta)) return MidiError::invalid_variable_length;
    if (delta > kMaximumMidiTick - absolute_tick) return MidiError::tick_overflow;
    absolute_tick += delta;

    std::uint8_t first{};
    if (!reader.read_u8(first)) return MidiError::truncated;
    std::uint8_t status = first;
    bool first_is_data{};
    if (first < 0x80U) {
      if (running_status < 0x80U || running_status >= 0xF0U) {
        return MidiError::invalid_running_status;
      }
      status = running_status;
      first_is_data = true;
    } else if (first < 0xF0U) {
      running_status = first;
    } else {
      running_status = 0;
    }

    if (status == 0xFFU) {
      std::uint8_t type{};
      std::uint32_t length{};
      std::span<const std::byte> payload;
      if (!reader.read_u8(type) || !reader.read_variable(length) ||
          !reader.read_bytes(length, payload)) {
        return MidiError::invalid_meta_event;
      }
      if (type == 0x2FU) {
        if (length != 0U) return MidiError::invalid_meta_event;
        ended = true;
        if (reader.remaining() != 0U) return MidiError::trailing_track_data;
        break;
      }
      if (type == 0x03U) {
        if (!track.name.empty() || !valid_text(payload)) {
          return MidiError::invalid_track_name;
        }
        track.name = make_text(payload);
      } else if (type == 0x06U) {
        if (!valid_text(payload)) return MidiError::invalid_meta_event;
        schedule.markers.push_back({absolute_tick, make_text(payload)});
      } else if (type == 0x51U) {
        if (payload.size() != 3U) return MidiError::invalid_meta_event;
        const auto tempo =
            (std::to_integer<std::uint32_t>(payload[0]) << 16U) |
            (std::to_integer<std::uint32_t>(payload[1]) << 8U) |
            std::to_integer<std::uint32_t>(payload[2]);
        if (tempo == 0U) return MidiError::invalid_meta_event;
        schedule.tempos.push_back({absolute_tick, tempo});
      } else if (type == 0x58U) {
        if (payload.size() != 4U) return MidiError::invalid_meta_event;
        const auto numerator = std::to_integer<std::uint8_t>(payload[0]);
        const auto denominator = std::to_integer<std::uint8_t>(payload[1]);
        if (numerator == 0U || denominator > 6U) {
          return MidiError::invalid_meta_event;
        }
        schedule.time_signatures.push_back(
            {absolute_tick, numerator, denominator});
      }
      continue;
    }

    if (status == 0xF0U || status == 0xF7U) return MidiError::invalid_event;
    if (status < 0x80U || status >= 0xF0U) return MidiError::invalid_event;

    const auto message = static_cast<std::uint8_t>(status & 0xF0U);
    const auto channel = static_cast<std::uint8_t>(status & 0x0FU);
    const bool one_data_byte = message == 0xC0U || message == 0xD0U;
    std::uint8_t data1{};
    std::uint8_t data2{};
    if (first_is_data) {
      data1 = first;
    } else if (!reader.read_u8(data1)) {
      return MidiError::truncated;
    }
    if (data1 >= 0x80U ||
        (!one_data_byte && (!reader.read_u8(data2) || data2 >= 0x80U))) {
      return MidiError::invalid_event;
    }

    std::optional<MidiEventKind> kind;
    if (message == 0x80U) kind = MidiEventKind::note_off;
    if (message == 0x90U) {
      kind = data2 == 0U ? MidiEventKind::note_off : MidiEventKind::note_on;
    }
    if (message == 0xB0U) kind = MidiEventKind::control_change;
    if (message == 0xC0U) kind = MidiEventKind::program_change;
    if (message == 0xE0U) kind = MidiEventKind::pitch_bend;
    if (!kind) return MidiError::invalid_event;
    if (const auto error = append_event(
            schedule, track,
            {.tick = absolute_tick,
             .track = track_index,
             .kind = *kind,
             .channel = channel,
             .data1 = data1,
             .data2 = data2})) {
      return error;
    }
  }
  if (!ended) return MidiError::missing_end_of_track;
  if (track.name.empty()) return MidiError::invalid_track_name;
  if (std::ranges::any_of(schedule.tracks, [&](const MidiTrack& candidate) {
        return candidate.name == track.name;
      })) {
    return MidiError::duplicate_track_name;
  }
  schedule.tracks.push_back(std::move(track));
  return std::nullopt;
}

template <typename Type>
auto hash_value(std::uint64_t& hash, Type value) noexcept -> void {
  const auto bytes = std::bit_cast<std::array<std::byte, sizeof(Type)>>(value);
  for (const auto byte : bytes) {
    hash ^= std::to_integer<std::uint8_t>(byte);
    hash *= 1'099'511'628'211ULL;
  }
}

auto hash_text(std::uint64_t& hash, std::string_view text) noexcept -> void {
  for (const unsigned char byte : text) {
    hash ^= byte;
    hash *= 1'099'511'628'211ULL;
  }
}

[[nodiscard]] auto layer_index(std::string_view name)
    -> std::optional<std::size_t> {
  if (name == "ambient") return 0U;
  if (name == "pulse") return 1U;
  if (name == "percussion") return 2U;
  if (name == "tension") return 3U;
  return std::nullopt;
}

[[nodiscard]] auto valid_gain(float gain) noexcept -> bool {
  return std::isfinite(gain) && gain >= 0.0F && gain <= 1.0F;
}

[[nodiscard]] auto validate_schedule(const MidiSchedule& schedule) noexcept
    -> std::optional<MidiError> {
  if (schedule.format > 1U) return MidiError::unsupported_format;
  if (schedule.ppq == 0U || schedule.ppq > kMaximumMidiPpq) {
    return MidiError::invalid_ppq;
  }
  if (schedule.tracks.empty() ||
      schedule.tracks.size() > kMaximumMidiTracks) {
    return MidiError::invalid_track_count;
  }
  std::size_t event_count{};
  for (const auto& track : schedule.tracks) {
    if (track.name.empty()) return MidiError::invalid_track_name;
    if (track.events.size() > kMaximumMidiEvents - event_count) {
      return MidiError::too_many_events;
    }
    event_count += track.events.size();
    if (std::ranges::any_of(track.events, [](const MidiEvent& event) {
          return event.tick > kMaximumMidiTick || event.channel >= 16U ||
                 event.data1 >= 128U || event.data2 >= 128U ||
                 event.kind > MidiEventKind::pitch_bend;
        }) ||
        !std::ranges::is_sorted(track.events, {}, &MidiEvent::tick)) {
      return MidiError::invalid_event;
    }
  }
  if (event_count != schedule.event_count || schedule.tempos.empty() ||
      std::ranges::any_of(schedule.tempos, [](const TempoChange& tempo) {
        return tempo.tick > kMaximumMidiTick ||
               tempo.microseconds_per_quarter == 0U;
      }) ||
      schedule.time_signatures.empty() ||
      std::ranges::any_of(
          schedule.time_signatures, [](const TimeSignature& signature) {
            return signature.tick > kMaximumMidiTick ||
                   signature.numerator == 0U ||
                   signature.denominator_power > 6U;
          }) ||
      !std::ranges::is_sorted(schedule.tempos, {}, &TempoChange::tick) ||
      !std::ranges::is_sorted(schedule.time_signatures, {},
                              &TimeSignature::tick) ||
      !std::ranges::is_sorted(schedule.markers, {}, &Marker::tick) ||
      std::ranges::adjacent_find(schedule.tempos, std::ranges::equal_to{},
                                 &TempoChange::tick) != schedule.tempos.end() ||
      std::ranges::adjacent_find(schedule.time_signatures,
                                 std::ranges::equal_to{},
                                 &TimeSignature::tick) !=
          schedule.time_signatures.end()) {
    return MidiError::invalid_meta_event;
  }
  return std::nullopt;
}

[[nodiscard]] auto frame_at_tick(const MidiSchedule& schedule,
                                 std::uint32_t target_tick) noexcept
    -> std::uint64_t {
  constexpr std::uint64_t kRateNumerator{6U};
  const auto denominator = static_cast<std::uint64_t>(schedule.ppq) * 125U;
  std::uint64_t numerator{};
  std::uint32_t cursor{};
  std::uint32_t tempo{500'000};
  for (const auto& change : schedule.tempos) {
    if (change.tick > target_tick) break;
    numerator += static_cast<std::uint64_t>(change.tick - cursor) * tempo *
                 kRateNumerator;
    cursor = change.tick;
    tempo = change.microseconds_per_quarter;
  }
  numerator += static_cast<std::uint64_t>(target_tick - cursor) * tempo *
               kRateNumerator;
  return numerator / denominator;
}

[[nodiscard]] auto tick_at_frame(const MidiSchedule& schedule,
                                 std::uint32_t maximum_tick,
                                 std::uint64_t frame) noexcept
    -> std::uint32_t {
  std::uint32_t low{};
  std::uint32_t high{maximum_tick};
  while (low < high) {
    const auto middle = low + (high - low + 1U) / 2U;
    if (frame_at_tick(schedule, middle) <= frame) {
      low = middle;
    } else {
      high = middle - 1U;
    }
  }
  return low;
}

struct ScheduledEvent {
  std::uint64_t frame{};
  MidiEvent event;
};

enum class MusicCommandKind : std::uint8_t {
  play,
  pause,
  stop,
  set_looping,
  set_volume,
  set_layer,
};

struct MusicCommand {
  MusicCommandKind kind{MusicCommandKind::play};
  MusicLayer layer{MusicLayer::ambient};
  float gain{};
  TransitionBoundary boundary{TransitionBoundary::immediate};
  bool enabled{};
};

struct LayerState {
  float gain{};
  float target{};
  float step{};
  std::size_t ramp_remaining{};
  bool pending{};
  std::uint64_t pending_frame{};
  float pending_gain{};
};

}  // namespace

auto midi_error_name(MidiError error) noexcept -> std::string_view {
  switch (error) {
    case MidiError::empty: return "empty";
    case MidiError::too_large: return "too-large";
    case MidiError::truncated: return "truncated";
    case MidiError::invalid_header: return "invalid-header";
    case MidiError::unsupported_format: return "unsupported-format";
    case MidiError::invalid_track_count: return "invalid-track-count";
    case MidiError::invalid_ppq: return "invalid-ppq";
    case MidiError::invalid_chunk: return "invalid-chunk";
    case MidiError::invalid_variable_length: return "invalid-variable-length";
    case MidiError::tick_overflow: return "tick-overflow";
    case MidiError::too_many_events: return "too-many-events";
    case MidiError::invalid_running_status: return "invalid-running-status";
    case MidiError::invalid_event: return "invalid-event";
    case MidiError::invalid_meta_event: return "invalid-meta-event";
    case MidiError::missing_end_of_track: return "missing-end-of-track";
    case MidiError::trailing_track_data: return "trailing-track-data";
    case MidiError::invalid_track_name: return "invalid-track-name";
    case MidiError::duplicate_track_name: return "duplicate-track-name";
    case MidiError::missing_layer: return "missing-layer";
    case MidiError::invalid_loop: return "invalid-loop";
    case MidiError::invalid_soundfont: return "invalid-soundfont";
    case MidiError::soundfont_too_large: return "soundfont-too-large";
    case MidiError::synth_initialization_failed:
      return "synth-initialization-failed";
    case MidiError::command_queue_full: return "command-queue-full";
    case MidiError::invalid_gain: return "invalid-gain";
    case MidiError::invalid_buffer: return "invalid-buffer";
    case MidiError::stopped: return "stopped";
  }
  return "unknown";
}

auto MidiSchedule::checksum() const noexcept -> std::uint64_t {
  std::uint64_t hash{1'469'598'103'934'665'603ULL};
  hash_value(hash, format);
  hash_value(hash, ppq);
  for (const auto& track : tracks) {
    hash_text(hash, track.name);
    for (const auto& event : track.events) {
      hash_value(hash, event.tick);
      hash_value(hash, event.track);
      hash_value(hash, event.order);
      hash_value(hash, event.kind);
      hash_value(hash, event.channel);
      hash_value(hash, event.data1);
      hash_value(hash, event.data2);
    }
  }
  for (const auto& tempo : tempos) {
    hash_value(hash, tempo.tick);
    hash_value(hash, tempo.microseconds_per_quarter);
  }
  for (const auto& signature : time_signatures) {
    hash_value(hash, signature.tick);
    hash_value(hash, signature.numerator);
    hash_value(hash, signature.denominator_power);
  }
  for (const auto& marker : markers) {
    hash_value(hash, marker.tick);
    hash_text(hash, marker.name);
  }
  return hash;
}

auto parse_smf(std::span<const std::byte> bytes)
    -> std::expected<MidiSchedule, MidiError> {
  if (bytes.empty()) return std::unexpected{MidiError::empty};
  if (bytes.size() > kMaximumMidiBytes) {
    return std::unexpected{MidiError::too_large};
  }
  Reader reader{bytes};
  std::span<const std::byte> magic;
  std::uint32_t header_length{};
  if (!reader.read_bytes(4, magic) ||
      std::memcmp(magic.data(), "MThd", 4) != 0 ||
      !reader.read_be32(header_length)) {
    return std::unexpected{MidiError::invalid_header};
  }
  if (header_length != 6U) return std::unexpected{MidiError::invalid_header};
  MidiSchedule schedule;
  std::uint16_t track_count{};
  if (!reader.read_be16(schedule.format) || !reader.read_be16(track_count) ||
      !reader.read_be16(schedule.ppq)) {
    return std::unexpected{MidiError::truncated};
  }
  if (schedule.format > 1U) {
    return std::unexpected{MidiError::unsupported_format};
  }
  if (track_count == 0U || track_count > kMaximumMidiTracks ||
      (schedule.format == 0U && track_count != 1U)) {
    return std::unexpected{MidiError::invalid_track_count};
  }
  if ((schedule.ppq & 0x8000U) != 0U || schedule.ppq == 0U ||
      schedule.ppq > kMaximumMidiPpq) {
    return std::unexpected{MidiError::invalid_ppq};
  }
  schedule.tracks.reserve(track_count);
  for (std::uint16_t track = 0; track < track_count; ++track) {
    std::span<const std::byte> chunk_magic;
    std::uint32_t chunk_length{};
    std::span<const std::byte> chunk;
    if (!reader.read_bytes(4, chunk_magic) ||
        std::memcmp(chunk_magic.data(), "MTrk", 4) != 0 ||
        !reader.read_be32(chunk_length) ||
        !reader.read_bytes(chunk_length, chunk)) {
      return std::unexpected{MidiError::invalid_chunk};
    }
    if (const auto error = parse_track(chunk, track, schedule)) {
      return std::unexpected{*error};
    }
  }
  if (reader.remaining() != 0U) {
    return std::unexpected{MidiError::invalid_chunk};
  }
  if (schedule.tempos.empty()) schedule.tempos.push_back({0, 500'000});
  if (schedule.time_signatures.empty()) {
    schedule.time_signatures.push_back({0, 4, 2});
  }
  const auto by_tick = [](const auto& left, const auto& right) {
    return left.tick < right.tick;
  };
  std::ranges::stable_sort(schedule.tempos, by_tick);
  std::ranges::stable_sort(schedule.time_signatures, by_tick);
  std::ranges::stable_sort(schedule.markers, by_tick);
  if (std::ranges::adjacent_find(schedule.tempos, std::ranges::equal_to{},
                                 &TempoChange::tick) !=
          schedule.tempos.end() ||
      std::ranges::adjacent_find(schedule.time_signatures,
                                 std::ranges::equal_to{},
                                 &TimeSignature::tick) !=
          schedule.time_signatures.end()) {
    return std::unexpected{MidiError::invalid_meta_event};
  }
  return schedule;
}

struct MusicEngine::Impl {
  MidiSchedule schedule;
  std::vector<std::byte> soundfont_bytes;
  std::array<tsf*, kMusicLayerCount> synths{};
  std::array<std::vector<ScheduledEvent>, kMusicLayerCount> events;
  std::array<std::size_t, kMusicLayerCount> event_indices{};
  std::array<LayerState, kMusicLayerCount> layers{};
  std::array<std::array<float, kMaximumAudioFramesPerCallback *
                                  kAudioChannelCount>,
             kMusicLayerCount>
      scratch{};
  std::array<MusicCommand, kMusicCommandCapacity> commands{};
  alignas(64) std::atomic<std::uint32_t> command_write{};
  alignas(64) std::atomic<std::uint32_t> command_read{};
  std::uint32_t maximum_tick{};
  std::uint64_t loop_start_frame{};
  std::uint64_t loop_end_frame{};
  std::vector<std::uint64_t> phrase_frames;
  std::uint64_t current_frame{};
  std::uint64_t events_applied{};
  std::uint64_t render_calls{};
  std::uint64_t rendered_frames{};
  std::uint64_t loop_count{};
  std::atomic<std::uint64_t> commands_submitted{};
  std::uint64_t commands_applied{};
  float master_gain{0.2F};
  bool playing{};
  bool looping{true};

  ~Impl() {
    for (auto iterator = synths.rbegin(); iterator != synths.rend(); ++iterator) {
      if (*iterator != nullptr) tsf_close(*iterator);
    }
  }

  [[nodiscard]] auto command_depth() const noexcept -> std::size_t {
    return command_write.load(std::memory_order_acquire) -
           command_read.load(std::memory_order_acquire);
  }

  [[nodiscard]] auto submit(MusicCommand command) noexcept -> bool {
    const auto write = command_write.load(std::memory_order_relaxed);
    const auto read = command_read.load(std::memory_order_acquire);
    if (write - read >= kMusicCommandCapacity) return false;
    commands[write % kMusicCommandCapacity] = command;
    command_write.store(write + 1U, std::memory_order_release);
    commands_submitted.fetch_add(1U, std::memory_order_relaxed);
    return true;
  }

  [[nodiscard]] auto take_command() noexcept -> std::optional<MusicCommand> {
    const auto read = command_read.load(std::memory_order_relaxed);
    if (read == command_write.load(std::memory_order_acquire)) return std::nullopt;
    const auto command = commands[read % kMusicCommandCapacity];
    command_read.store(read + 1U, std::memory_order_release);
    return command;
  }

  [[nodiscard]] auto boundary_frame(TransitionBoundary boundary) const noexcept
      -> std::uint64_t {
    if (boundary == TransitionBoundary::immediate) return current_frame;
    if (boundary == TransitionBoundary::next_phrase) {
      const auto found = std::ranges::upper_bound(phrase_frames, current_frame);
      return found == phrase_frames.end() ? current_frame : *found;
    }
    const auto tick = tick_at_frame(schedule, maximum_tick, current_frame);
    std::uint32_t target_tick{};
    if (boundary == TransitionBoundary::next_beat) {
      target_tick = (tick / schedule.ppq + 1U) * schedule.ppq;
    } else {
      auto signature = schedule.time_signatures.front();
      for (const auto& candidate : schedule.time_signatures) {
        if (candidate.tick > tick) break;
        signature = candidate;
      }
      const auto denominator = 1U << signature.denominator_power;
      const auto measure_ticks =
          static_cast<std::uint32_t>(signature.numerator) * schedule.ppq * 4U /
          denominator;
      if (measure_ticks == 0U) return current_frame;
      const auto relative = tick - signature.tick;
      target_tick = signature.tick +
                    (relative / measure_ticks + 1U) * measure_ticks;
    }
    return target_tick > maximum_tick ? current_frame
                                      : frame_at_tick(schedule, target_tick);
  }

  auto service_commands() noexcept -> void {
    while (const auto command = take_command()) {
      switch (command->kind) {
        case MusicCommandKind::play:
          playing = true;
          break;
        case MusicCommandKind::pause:
          playing = false;
          break;
        case MusicCommandKind::stop:
          playing = false;
          reset_to(loop_start_frame);
          break;
        case MusicCommandKind::set_looping:
          looping = command->enabled;
          break;
        case MusicCommandKind::set_volume:
          master_gain = command->gain;
          break;
        case MusicCommandKind::set_layer: {
          const auto index = static_cast<std::size_t>(command->layer);
          auto& layer = layers[index];
          layer.pending = true;
          layer.pending_frame = boundary_frame(command->boundary);
          layer.pending_gain = command->gain;
          break;
        }
      }
      ++commands_applied;
    }
  }

  auto begin_due_ramps() noexcept -> void {
    for (auto& layer : layers) {
      if (!layer.pending || layer.pending_frame > current_frame) continue;
      layer.pending = false;
      layer.target = layer.pending_gain;
      layer.ramp_remaining = kMusicGainRampFrames;
      layer.step = (layer.target - layer.gain) /
                   static_cast<float>(kMusicGainRampFrames);
    }
  }

  auto apply_event(std::size_t layer_index_value,
                   const MidiEvent& event) noexcept -> void {
    auto* synth = synths[layer_index_value];
    switch (event.kind) {
      case MidiEventKind::note_off:
        tsf_channel_note_off(synth, event.channel, event.data1);
        break;
      case MidiEventKind::note_on:
        (void)tsf_channel_note_on(synth, event.channel, event.data1,
                                  static_cast<float>(event.data2) / 127.0F);
        break;
      case MidiEventKind::control_change:
        (void)tsf_channel_midi_control(synth, event.channel, event.data1,
                                       event.data2);
        break;
      case MidiEventKind::program_change:
        (void)tsf_channel_set_presetnumber(synth, event.channel, event.data1,
                                           0);
        break;
      case MidiEventKind::pitch_bend: {
        const auto bend = static_cast<int>(event.data1) |
                          (static_cast<int>(event.data2) << 7);
        (void)tsf_channel_set_pitchwheel(synth, event.channel, bend);
        break;
      }
    }
    ++events_applied;
  }

  auto apply_due_events() noexcept -> void {
    for (std::size_t layer = 0; layer < kMusicLayerCount; ++layer) {
      while (event_indices[layer] < events[layer].size() &&
             events[layer][event_indices[layer]].frame <= current_frame) {
        apply_event(layer, events[layer][event_indices[layer]].event);
        ++event_indices[layer];
      }
    }
  }

  [[nodiscard]] auto next_boundary(std::uint64_t requested_end) const noexcept
      -> std::uint64_t {
    auto boundary = std::min(requested_end, loop_end_frame);
    for (std::size_t layer = 0; layer < kMusicLayerCount; ++layer) {
      if (event_indices[layer] < events[layer].size()) {
        boundary = std::min(boundary, events[layer][event_indices[layer]].frame);
      }
      if (layers[layer].pending) {
        boundary = std::min(boundary, layers[layer].pending_frame);
      }
    }
    return boundary;
  }

  auto reset_to(std::uint64_t frame) noexcept -> void {
    for (auto* synth : synths) tsf_reset(synth);
    current_frame = frame;
    for (std::size_t layer = 0; layer < kMusicLayerCount; ++layer) {
      event_indices[layer] = static_cast<std::size_t>(
          std::ranges::lower_bound(events[layer], frame, {},
                                   &ScheduledEvent::frame) -
          events[layer].begin());
    }
  }
};

MusicEngine::MusicEngine(std::unique_ptr<Impl> impl) noexcept
    : m_impl{std::move(impl)} {}
MusicEngine::MusicEngine(MusicEngine&&) noexcept = default;
auto MusicEngine::operator=(MusicEngine&&) noexcept -> MusicEngine& = default;
MusicEngine::~MusicEngine() = default;

auto MusicEngine::create(MidiSchedule schedule,
                         std::span<const std::byte> soundfont)
    -> std::expected<MusicEngine, MidiError> {
  if (const auto error = validate_schedule(schedule)) {
    return std::unexpected{*error};
  }
  if (soundfont.empty()) return std::unexpected{MidiError::invalid_soundfont};
  if (soundfont.size() > kMaximumSoundFontBytes) {
    return std::unexpected{MidiError::soundfont_too_large};
  }
  if (const auto error = validate_soundfont(soundfont)) {
    return std::unexpected{*error};
  }
  auto impl = std::make_unique<Impl>();
  impl->schedule = std::move(schedule);
  impl->soundfont_bytes.assign(soundfont.begin(), soundfont.end());

  std::array<bool, kMusicLayerCount> found_layers{};
  for (const auto& track : impl->schedule.tracks) {
    const auto index = layer_index(track.name);
    if (!index || found_layers[*index]) {
      return std::unexpected{MidiError::missing_layer};
    }
    found_layers[*index] = true;
    for (const auto& event : track.events) {
      impl->events[*index].push_back(
          {frame_at_tick(impl->schedule, event.tick), event});
      impl->maximum_tick = std::max(impl->maximum_tick, event.tick);
    }
  }
  if (!std::ranges::all_of(found_layers, std::identity{})) {
    return std::unexpected{MidiError::missing_layer};
  }

  std::optional<std::uint32_t> loop_start;
  std::optional<std::uint32_t> loop_end;
  for (const auto& marker : impl->schedule.markers) {
    impl->maximum_tick = std::max(impl->maximum_tick, marker.tick);
    if (marker.name == "loop-start") {
      if (loop_start) return std::unexpected{MidiError::invalid_loop};
      loop_start = marker.tick;
    }
    if (marker.name == "loop-end") {
      if (loop_end) return std::unexpected{MidiError::invalid_loop};
      loop_end = marker.tick;
    }
    if (marker.name.starts_with("phrase-")) {
      impl->phrase_frames.push_back(frame_at_tick(impl->schedule, marker.tick));
    }
  }
  if (!loop_start || !loop_end || *loop_start >= *loop_end ||
      impl->phrase_frames.empty()) {
    return std::unexpected{MidiError::invalid_loop};
  }
  impl->loop_start_frame = frame_at_tick(impl->schedule, *loop_start);
  impl->loop_end_frame = frame_at_tick(impl->schedule, *loop_end);
  if (impl->loop_end_frame <= impl->loop_start_frame) {
    return std::unexpected{MidiError::invalid_loop};
  }
  std::ranges::sort(impl->phrase_frames);

  impl->synths[0] = tsf_load_memory(impl->soundfont_bytes.data(),
                                    static_cast<int>(impl->soundfont_bytes.size()));
  if (impl->synths[0] == nullptr) {
    return std::unexpected{MidiError::invalid_soundfont};
  }
  for (const auto& track : impl->schedule.tracks) {
    for (const auto& event : track.events) {
      if (event.kind == MidiEventKind::program_change &&
          tsf_get_presetindex(impl->synths[0], 0, event.data1) < 0) {
        return std::unexpected{MidiError::invalid_soundfont};
      }
    }
  }
  for (std::size_t index = 1; index < kMusicLayerCount; ++index) {
    impl->synths[index] = tsf_copy(impl->synths[0]);
    if (impl->synths[index] == nullptr) {
      return std::unexpected{MidiError::synth_initialization_failed};
    }
  }
  for (auto* synth : impl->synths) {
    tsf_set_output(synth, TSF_STEREO_INTERLEAVED,
                   static_cast<int>(kAudioSampleRate), 0.0F);
    static_assert(kMaximumMusicVoices % kMusicLayerCount == 0U);
    constexpr auto kMaximumVoicesPerLayer =
        kMaximumMusicVoices / kMusicLayerCount;
    if (tsf_set_max_voices(synth,
                           static_cast<int>(kMaximumVoicesPerLayer)) == 0) {
      return std::unexpected{MidiError::synth_initialization_failed};
    }
    for (int channel = 0; channel < 16; ++channel) {
      (void)tsf_channel_set_presetnumber(synth, channel, 0, 0);
    }
  }
  impl->layers[0].gain = impl->layers[0].target = 1.0F;
  impl->reset_to(impl->loop_start_frame);
  return MusicEngine{std::move(impl)};
}

auto MusicEngine::play() noexcept -> std::optional<MidiError> {
  if (!m_impl || !m_impl->submit({.kind = MusicCommandKind::play})) {
    return MidiError::command_queue_full;
  }
  return std::nullopt;
}

auto MusicEngine::pause() noexcept -> std::optional<MidiError> {
  if (!m_impl || !m_impl->submit({.kind = MusicCommandKind::pause})) {
    return MidiError::command_queue_full;
  }
  return std::nullopt;
}

auto MusicEngine::stop() noexcept -> std::optional<MidiError> {
  if (!m_impl || !m_impl->submit({.kind = MusicCommandKind::stop})) {
    return MidiError::command_queue_full;
  }
  return std::nullopt;
}

auto MusicEngine::set_looping(bool looping) noexcept
    -> std::optional<MidiError> {
  if (!m_impl ||
      !m_impl->submit(
          {.kind = MusicCommandKind::set_looping, .enabled = looping})) {
    return MidiError::command_queue_full;
  }
  return std::nullopt;
}

auto MusicEngine::set_music_volume(float gain) noexcept
    -> std::optional<MidiError> {
  if (!m_impl || !valid_gain(gain)) return MidiError::invalid_gain;
  if (!m_impl->submit({.kind = MusicCommandKind::set_volume, .gain = gain})) {
    return MidiError::command_queue_full;
  }
  return std::nullopt;
}

auto MusicEngine::set_layer_target(MusicLayer layer, float gain,
                                   TransitionBoundary boundary) noexcept
    -> std::optional<MidiError> {
  if (!m_impl || static_cast<std::size_t>(layer) >= kMusicLayerCount ||
      !valid_gain(gain)) {
    return MidiError::invalid_gain;
  }
  if (!m_impl->submit({.kind = MusicCommandKind::set_layer,
                       .layer = layer,
                       .gain = gain,
                       .boundary = boundary})) {
    return MidiError::command_queue_full;
  }
  return std::nullopt;
}

auto MusicEngine::render(std::span<float> interleaved_samples) noexcept
    -> std::optional<MidiError> {
  if (!m_impl || interleaved_samples.empty() ||
      interleaved_samples.size() % kAudioChannelCount != 0U ||
      interleaved_samples.size() / kAudioChannelCount >
          kMaximumAudioFramesPerCallback) {
    return MidiError::invalid_buffer;
  }
  std::ranges::fill(interleaved_samples, 0.0F);
  ++m_impl->render_calls;
  const auto requested_frames = interleaved_samples.size() / kAudioChannelCount;
  m_impl->service_commands();
  if (!m_impl->playing) return std::nullopt;

  std::size_t output_frame{};
  while (output_frame < requested_frames && m_impl->playing) {
    if (m_impl->current_frame >= m_impl->loop_end_frame) {
      if (!m_impl->looping) {
        m_impl->playing = false;
        break;
      }
      m_impl->reset_to(m_impl->loop_start_frame);
      ++m_impl->loop_count;
    }
    m_impl->begin_due_ramps();
    m_impl->apply_due_events();
    const auto requested_end =
        m_impl->current_frame + requested_frames - output_frame;
    auto boundary = m_impl->next_boundary(requested_end);
    if (boundary <= m_impl->current_frame) {
      m_impl->begin_due_ramps();
      m_impl->apply_due_events();
      boundary = std::min(requested_end, m_impl->current_frame + 1U);
    }
    const auto block_frames = static_cast<std::size_t>(
        std::min<std::uint64_t>(boundary - m_impl->current_frame,
                                requested_frames - output_frame));
    if (block_frames == 0U) continue;

    for (std::size_t layer = 0; layer < kMusicLayerCount; ++layer) {
      tsf_render_float(m_impl->synths[layer], m_impl->scratch[layer].data(),
                       static_cast<int>(block_frames), 0);
    }
    for (std::size_t frame = 0; frame < block_frames; ++frame) {
      float left{};
      float right{};
      for (std::size_t layer = 0; layer < kMusicLayerCount; ++layer) {
        auto& state = m_impl->layers[layer];
        if (state.ramp_remaining != 0U) {
          state.gain += state.step;
          --state.ramp_remaining;
          if (state.ramp_remaining == 0U) state.gain = state.target;
        }
        left += m_impl->scratch[layer][frame * 2U] * state.gain;
        right += m_impl->scratch[layer][frame * 2U + 1U] * state.gain;
      }
      interleaved_samples[(output_frame + frame) * 2U] =
          std::clamp(left * m_impl->master_gain, -1.0F, 1.0F);
      interleaved_samples[(output_frame + frame) * 2U + 1U] =
          std::clamp(right * m_impl->master_gain, -1.0F, 1.0F);
    }
    output_frame += block_frames;
    m_impl->current_frame += block_frames;
    m_impl->rendered_frames += block_frames;
  }
  return std::nullopt;
}

auto MusicEngine::diagnostics() const noexcept -> MusicDiagnostics {
  if (!m_impl) return {};
  MusicDiagnostics diagnostics{
      .sample_frame = m_impl->current_frame,
      .events_applied = m_impl->events_applied,
      .render_calls = m_impl->render_calls,
      .rendered_frames = m_impl->rendered_frames,
      .loop_count = m_impl->loop_count,
      .commands_submitted =
          m_impl->commands_submitted.load(std::memory_order_relaxed),
      .commands_applied = m_impl->commands_applied,
      .command_queue_depth = m_impl->command_depth(),
  };
  for (std::size_t layer = 0; layer < kMusicLayerCount; ++layer) {
    diagnostics.active_voices += static_cast<std::size_t>(
        tsf_active_voice_count(m_impl->synths[layer]));
    diagnostics.layer_gains[layer] = m_impl->layers[layer].gain;
  }
  return diagnostics;
}

}  // namespace apsis_drift::midi_spike
