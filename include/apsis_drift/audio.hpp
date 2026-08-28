#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "apsis_drift/simulation.hpp"

namespace apsis_drift {

inline constexpr std::uint32_t kAudioSampleRate{48'000};
inline constexpr std::uint8_t kAudioChannelCount{2};
inline constexpr std::uint32_t kAudioFramesPerSimulationTick{
    kAudioSampleRate / kSimulationHz};
inline constexpr std::size_t kAudioEventQueueCapacity{256};
inline constexpr std::size_t kMaximumAudioFramesPerCallback{4096};

enum class AudioSampleFormat : std::uint8_t { float32 };

struct AudioFormat {
  std::uint32_t sample_rate{kAudioSampleRate};
  std::uint8_t channels{kAudioChannelCount};
  AudioSampleFormat sample_format{AudioSampleFormat::float32};

  friend auto operator==(const AudioFormat&, const AudioFormat&)
      -> bool = default;
};

inline constexpr AudioFormat kAudioFormat{};

struct AudioCueId {
  std::uint32_t value{};

  [[nodiscard]] constexpr auto valid() const noexcept -> bool {
    return value != 0;
  }

  friend auto operator==(const AudioCueId&, const AudioCueId&)
      -> bool = default;
};

struct AudioEventIdentity {
  SimulationTick tick{};
  std::uint16_t sequence{};

  friend auto operator==(const AudioEventIdentity&,
                         const AudioEventIdentity&) -> bool = default;
};

struct AudioEvent {
  AudioEventIdentity identity;
  AudioCueId cue;

  friend auto operator==(const AudioEvent&, const AudioEvent&)
      -> bool = default;
};

[[nodiscard]] auto audio_sample_frame(AudioEventIdentity identity) noexcept
    -> std::optional<std::uint64_t>;

enum class AudioEmitStatus : std::uint8_t {
  queued,
  disabled,
  dropped_queue_full,
  rejected_invalid_cue,
  rejected_tick_regression,
  rejected_sequence_exhausted,
  rejected_timestamp_overflow,
  stopped,
};

struct AudioEmitResult {
  AudioEmitStatus status{AudioEmitStatus::disabled};
  std::optional<AudioEventIdentity> identity;
};

class AudioEventQueue {
 public:
  AudioEventQueue() = default;
  AudioEventQueue(const AudioEventQueue&) = delete;
  auto operator=(const AudioEventQueue&) -> AudioEventQueue& = delete;

  [[nodiscard]] auto try_push(AudioEvent event) noexcept -> bool;
  [[nodiscard]] auto try_pop() noexcept -> std::optional<AudioEvent>;
  [[nodiscard]] auto clear() noexcept -> std::size_t;
  [[nodiscard]] auto depth() const noexcept -> std::size_t;

 private:
  static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

  std::array<AudioEvent, kAudioEventQueueCapacity> m_events{};
  alignas(64) std::atomic<std::uint32_t> m_write_index{};
  alignas(64) std::atomic<std::uint32_t> m_read_index{};
};

enum class AudioBufferError : std::uint8_t {
  invalid_dimensions,
  stopped,
};

class AudioRenderSource {
 public:
  virtual ~AudioRenderSource() = default;
  [[nodiscard]] virtual auto render(
      std::span<float> interleaved_samples) noexcept
      -> std::optional<AudioBufferError> = 0;
};

enum class AudioBackendState : std::uint8_t {
  disabled,
  no_device,
  running,
  failed,
  stopped,
};

class AudioBackend {
 public:
  virtual ~AudioBackend() = default;
  [[nodiscard]] virtual auto name() const noexcept -> std::string_view = 0;
  [[nodiscard]] virtual auto state() const noexcept
      -> AudioBackendState = 0;
  [[nodiscard]] virtual auto start(AudioFormat format,
                                   AudioRenderSource& source) noexcept
      -> bool = 0;
  virtual auto stop() noexcept -> void = 0;
};

class NoDeviceAudioBackend final : public AudioBackend {
 public:
  [[nodiscard]] auto name() const noexcept -> std::string_view override;
  [[nodiscard]] auto state() const noexcept
      -> AudioBackendState override;
  [[nodiscard]] auto start(AudioFormat format,
                           AudioRenderSource& source) noexcept
      -> bool override;
  auto stop() noexcept -> void override;

 private:
  AudioBackendState m_state{AudioBackendState::stopped};
};

enum class AudioRuntimeMode : std::uint8_t { disabled, no_device };
enum class AudioResetReason : std::uint8_t {
  none,
  load,
  return_to_title,
  backend_loss,
  shutdown,
};

struct AudioDiagnostics {
  AudioRuntimeMode mode{AudioRuntimeMode::disabled};
  AudioBackendState backend_state{AudioBackendState::disabled};
  AudioEmitStatus last_emit_status{AudioEmitStatus::disabled};
  AudioResetReason last_reset_reason{AudioResetReason::none};
  std::uint64_t identities_assigned{};
  std::uint64_t events_queued{};
  std::uint64_t events_dequeued{};
  std::uint64_t events_dropped{};
  std::uint64_t events_rejected{};
  std::uint64_t events_discarded_on_reset{};
  std::uint64_t reset_count{};
  std::uint64_t backend_loss_count{};
  std::size_t queue_depth{};
  std::size_t maximum_queue_depth{};
};

class AudioRuntime final : public AudioRenderSource {
 public:
  explicit AudioRuntime(
      AudioRuntimeMode mode = AudioRuntimeMode::no_device,
      std::unique_ptr<AudioBackend> backend = nullptr);
  ~AudioRuntime() override;

  AudioRuntime(const AudioRuntime&) = delete;
  auto operator=(const AudioRuntime&) -> AudioRuntime& = delete;

  [[nodiscard]] auto emit(SimulationTick tick, AudioCueId cue) noexcept
      -> AudioEmitResult;
  [[nodiscard]] auto try_take_event() noexcept
      -> std::optional<AudioEvent>;
  auto service() noexcept -> void;
  auto reset(AudioResetReason reason) noexcept -> void;
  auto backend_lost() -> void;
  auto shutdown() noexcept -> void;

  [[nodiscard]] auto render(
      std::span<float> interleaved_samples) noexcept
      -> std::optional<AudioBufferError> override;
  [[nodiscard]] auto diagnostics() const noexcept -> AudioDiagnostics;

 private:
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

  auto start_backend(std::unique_ptr<AudioBackend> backend) -> void;
  auto update_maximum_depth(std::size_t depth) noexcept -> void;

  AudioRuntimeMode m_mode{AudioRuntimeMode::disabled};
  std::unique_ptr<AudioBackend> m_backend;
  AudioEventQueue m_queue;
  std::optional<SimulationTick> m_last_tick;
  std::uint32_t m_next_sequence{};
  bool m_stopped{};
  std::atomic<AudioEmitStatus> m_last_emit_status{
      AudioEmitStatus::disabled};
  std::atomic<AudioResetReason> m_last_reset_reason{
      AudioResetReason::none};
  std::atomic<std::uint64_t> m_identities_assigned{};
  std::atomic<std::uint64_t> m_events_queued{};
  std::atomic<std::uint64_t> m_events_dequeued{};
  std::atomic<std::uint64_t> m_events_dropped{};
  std::atomic<std::uint64_t> m_events_rejected{};
  std::atomic<std::uint64_t> m_events_discarded_on_reset{};
  std::atomic<std::uint64_t> m_reset_count{};
  std::atomic<std::uint64_t> m_backend_loss_count{};
  std::atomic<std::size_t> m_maximum_queue_depth{};
};

}  // namespace apsis_drift
