#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace apsis_drift::midi_spike {

inline constexpr std::size_t kMaximumMidiBytes{std::size_t{64U} * 1024U};
inline constexpr std::uint16_t kMaximumMidiTracks{16};
inline constexpr std::size_t kMaximumMidiEvents{16'384};
inline constexpr std::uint16_t kMaximumMidiPpq{960};
inline constexpr std::uint32_t kMaximumMidiTick{100'000'000};
inline constexpr std::size_t kMaximumMusicVoices{64};
inline constexpr std::size_t kMusicLayerCount{4};
inline constexpr std::size_t kMusicCommandCapacity{32};
inline constexpr std::size_t kMusicGainRampFrames{480};
inline constexpr std::size_t kMaximumSoundFontBytes{std::size_t{4U} * 1024U *
                                                    1024U};
inline constexpr std::size_t kMaximumDecodedSoundFontBytes{std::size_t{16U} *
                                                           1024U * 1024U};

enum class MidiError : std::uint8_t {
  empty,
  too_large,
  truncated,
  invalid_header,
  unsupported_format,
  invalid_track_count,
  invalid_ppq,
  invalid_chunk,
  invalid_variable_length,
  tick_overflow,
  too_many_events,
  invalid_running_status,
  invalid_event,
  invalid_meta_event,
  missing_end_of_track,
  trailing_track_data,
  invalid_track_name,
  duplicate_track_name,
  missing_layer,
  invalid_loop,
  invalid_soundfont,
  soundfont_too_large,
  synth_initialization_failed,
  command_queue_full,
  invalid_gain,
  invalid_buffer,
  stopped,
};

[[nodiscard]] auto midi_error_name(MidiError error) noexcept
    -> std::string_view;

enum class MidiEventKind : std::uint8_t {
  note_off,
  note_on,
  control_change,
  program_change,
  pitch_bend,
};

struct MidiEvent {
  std::uint32_t tick{};
  std::uint16_t track{};
  std::uint32_t order{};
  MidiEventKind kind{MidiEventKind::note_off};
  std::uint8_t channel{};
  std::uint8_t data1{};
  std::uint8_t data2{};

  friend auto operator==(const MidiEvent&, const MidiEvent&) -> bool = default;
};

struct TempoChange {
  std::uint32_t tick{};
  std::uint32_t microseconds_per_quarter{500'000};
  friend auto operator==(const TempoChange&, const TempoChange&)
      -> bool = default;
};

struct TimeSignature {
  std::uint32_t tick{};
  std::uint8_t numerator{4};
  std::uint8_t denominator_power{2};
  friend auto operator==(const TimeSignature&, const TimeSignature&)
      -> bool = default;
};

struct Marker {
  std::uint32_t tick{};
  std::string name;
  friend auto operator==(const Marker&, const Marker&) -> bool = default;
};

struct MidiTrack {
  std::string name;
  std::vector<MidiEvent> events;
  friend auto operator==(const MidiTrack&, const MidiTrack&) -> bool = default;
};

struct MidiSchedule {
  std::uint16_t format{};
  std::uint16_t ppq{};
  std::vector<MidiTrack> tracks;
  std::vector<TempoChange> tempos;
  std::vector<TimeSignature> time_signatures;
  std::vector<Marker> markers;
  std::size_t event_count{};

  [[nodiscard]] auto checksum() const noexcept -> std::uint64_t;
};

[[nodiscard]] auto parse_smf(std::span<const std::byte> bytes)
    -> std::expected<MidiSchedule, MidiError>;

enum class MusicLayer : std::uint8_t {
  ambient,
  pulse,
  percussion,
  tension,
};

enum class TransitionBoundary : std::uint8_t {
  immediate,
  next_beat,
  next_measure,
  next_phrase,
};

struct MusicDiagnostics {
  std::uint64_t sample_frame{};
  std::uint64_t events_applied{};
  std::uint64_t render_calls{};
  std::uint64_t rendered_frames{};
  std::uint64_t loop_count{};
  std::uint64_t commands_submitted{};
  std::uint64_t commands_applied{};
  std::size_t command_queue_depth{};
  std::size_t active_voices{};
  std::array<float, kMusicLayerCount> layer_gains{};
};

class MusicEngine {
 public:
  [[nodiscard]] static auto create(MidiSchedule schedule,
                                   std::span<const std::byte> soundfont)
      -> std::expected<MusicEngine, MidiError>;

  MusicEngine(MusicEngine&&) noexcept;
  auto operator=(MusicEngine&&) noexcept -> MusicEngine&;
  ~MusicEngine();

  MusicEngine(const MusicEngine&) = delete;
  auto operator=(const MusicEngine&) -> MusicEngine& = delete;

  [[nodiscard]] auto play() noexcept -> std::optional<MidiError>;
  [[nodiscard]] auto pause() noexcept -> std::optional<MidiError>;
  [[nodiscard]] auto stop() noexcept -> std::optional<MidiError>;
  [[nodiscard]] auto set_looping(bool looping) noexcept
      -> std::optional<MidiError>;
  [[nodiscard]] auto set_music_volume(float gain) noexcept
      -> std::optional<MidiError>;
  [[nodiscard]] auto set_layer_target(MusicLayer layer, float gain,
                                      TransitionBoundary boundary) noexcept
      -> std::optional<MidiError>;
  [[nodiscard]] auto render(std::span<float> interleaved_samples) noexcept
      -> std::optional<MidiError>;
  [[nodiscard]] auto diagnostics() const noexcept -> MusicDiagnostics;

 private:
  struct Impl;
  explicit MusicEngine(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> m_impl;
};

} // namespace apsis_drift::midi_spike
