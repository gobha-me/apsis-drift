#include "apsis_drift/audio.hpp"

#include <algorithm>
#include <limits>

#if defined(APSIS_DRIFT_HAS_RTAUDIO)
#include "rtaudio_backend.hpp"
#endif

namespace apsis_drift {

auto audio_backend_state_name(AudioBackendState state) noexcept
    -> std::string_view {
  switch (state) {
    case AudioBackendState::disabled: return "disabled";
    case AudioBackendState::no_device: return "no-device";
    case AudioBackendState::running: return "running";
    case AudioBackendState::failed: return "failed";
    case AudioBackendState::stopped: return "stopped";
  }
  return "unknown";
}

auto audio_backend_failure_name(AudioBackendFailure failure) noexcept
    -> std::string_view {
  switch (failure) {
    case AudioBackendFailure::none: return "none";
    case AudioBackendFailure::discovery_failed: return "discovery-failed";
    case AudioBackendFailure::no_output_device: return "no-output-device";
    case AudioBackendFailure::invalid_selected_device:
      return "invalid-selected-device";
    case AudioBackendFailure::open_failed: return "open-failed";
    case AudioBackendFailure::start_failed: return "start-failed";
    case AudioBackendFailure::callback_failed: return "callback-failed";
    case AudioBackendFailure::device_lost: return "device-lost";
  }
  return "unknown";
}

auto audio_sample_frame(AudioEventIdentity identity) noexcept
    -> std::optional<std::uint64_t> {
  if (identity.tick >
      std::numeric_limits<std::uint64_t>::max() /
          kAudioFramesPerSimulationTick) {
    return std::nullopt;
  }
  return identity.tick * kAudioFramesPerSimulationTick;
}

auto AudioEventQueue::try_push(AudioEvent event) noexcept -> bool {
  const auto write = m_write_index.load(std::memory_order_relaxed);
  const auto read = m_read_index.load(std::memory_order_acquire);
  if (write - read >= kAudioEventQueueCapacity) return false;
  m_events[write % kAudioEventQueueCapacity] = event;
  m_write_index.store(write + 1U, std::memory_order_release);
  return true;
}

auto AudioEventQueue::try_pop() noexcept -> std::optional<AudioEvent> {
  const auto read = m_read_index.load(std::memory_order_relaxed);
  const auto write = m_write_index.load(std::memory_order_acquire);
  if (read == write) return std::nullopt;
  const auto event = m_events[read % kAudioEventQueueCapacity];
  m_read_index.store(read + 1U, std::memory_order_release);
  return event;
}

auto AudioEventQueue::clear() noexcept -> std::size_t {
  std::size_t cleared{};
  while (try_pop()) ++cleared;
  return cleared;
}

auto AudioEventQueue::depth() const noexcept -> std::size_t {
  const auto write = m_write_index.load(std::memory_order_acquire);
  const auto read = m_read_index.load(std::memory_order_acquire);
  return static_cast<std::size_t>(write - read);
}

auto NoDeviceAudioBackend::name() const noexcept -> std::string_view {
  return "no-device";
}

auto NoDeviceAudioBackend::state() const noexcept -> AudioBackendState {
  return m_state;
}

auto NoDeviceAudioBackend::diagnostics() const noexcept
    -> AudioBackendDiagnostics {
  return {
      .name = name(),
      .state = state(),
  };
}

auto NoDeviceAudioBackend::start(AudioFormat format,
                                 AudioRenderSource&) noexcept -> bool {
  if (format != kAudioFormat) {
    m_state = AudioBackendState::failed;
    return false;
  }
  m_state = AudioBackendState::no_device;
  return true;
}

auto NoDeviceAudioBackend::stop() noexcept -> void {
  m_state = AudioBackendState::stopped;
}

auto rtaudio_backend_compiled() noexcept -> bool {
#if defined(APSIS_DRIFT_HAS_RTAUDIO)
  return true;
#else
  return false;
#endif
}

auto make_device_audio_backend(AudioOutputSelection selection)
    -> std::unique_ptr<AudioBackend> {
#if defined(APSIS_DRIFT_HAS_RTAUDIO)
  return detail::make_rtaudio_backend(selection);
#else
  (void)selection;
  return nullptr;
#endif
}

AudioRuntime::AudioRuntime(AudioRuntimeMode mode,
                           std::unique_ptr<AudioBackend> backend,
                           AudioOutputSelection selection)
    : m_mode(mode) {
  if (mode == AudioRuntimeMode::disabled) return;
  if (backend) {
    start_backend(std::move(backend));
  } else if (mode == AudioRuntimeMode::automatic) {
    start_backend(make_device_audio_backend(selection));
  } else {
    start_backend(std::make_unique<NoDeviceAudioBackend>());
  }
}

AudioRuntime::~AudioRuntime() { shutdown(); }

auto AudioRuntime::start_backend(
    std::unique_ptr<AudioBackend> backend) -> void {
  if (backend && backend->start(kAudioFormat, *this)) {
    m_backend = std::move(backend);
    return;
  }
  if (backend) {
    const auto failed = backend->diagnostics();
    m_last_backend_failure =
        failed.failure == AudioBackendFailure::none
            ? AudioBackendFailure::discovery_failed
            : failed.failure;
    m_retired_callback_count += failed.callback_count;
    m_retired_output_underflow_count += failed.output_underflow_count;
    m_backend_failure_count.fetch_add(1, std::memory_order_relaxed);
    backend->stop();
  }
  auto fallback = std::make_unique<NoDeviceAudioBackend>();
  (void)fallback->start(kAudioFormat, *this);
  m_backend = std::move(fallback);
}

auto AudioRuntime::emit(SimulationTick tick, AudioCueId cue) noexcept
    -> AudioEmitResult {
  if (m_stopped) {
    m_last_emit_status.store(AudioEmitStatus::stopped,
                             std::memory_order_relaxed);
    return {.status = AudioEmitStatus::stopped};
  }
  if (m_mode == AudioRuntimeMode::disabled) {
    m_last_emit_status.store(AudioEmitStatus::disabled,
                             std::memory_order_relaxed);
    return {.status = AudioEmitStatus::disabled};
  }
  if (!cue.valid()) {
    m_events_rejected.fetch_add(1, std::memory_order_relaxed);
    m_last_emit_status.store(AudioEmitStatus::rejected_invalid_cue,
                             std::memory_order_relaxed);
    return {.status = AudioEmitStatus::rejected_invalid_cue};
  }
  if (m_last_tick && tick < *m_last_tick) {
    m_events_rejected.fetch_add(1, std::memory_order_relaxed);
    m_last_emit_status.store(AudioEmitStatus::rejected_tick_regression,
                             std::memory_order_relaxed);
    return {.status = AudioEmitStatus::rejected_tick_regression};
  }
  if (!audio_sample_frame({tick, 0})) {
    m_events_rejected.fetch_add(1, std::memory_order_relaxed);
    m_last_emit_status.store(AudioEmitStatus::rejected_timestamp_overflow,
                             std::memory_order_relaxed);
    return {.status = AudioEmitStatus::rejected_timestamp_overflow};
  }
  if (!m_last_tick || tick != *m_last_tick) {
    m_last_tick = tick;
    m_next_sequence = 0;
  }
  if (m_next_sequence > std::numeric_limits<std::uint16_t>::max()) {
    m_events_rejected.fetch_add(1, std::memory_order_relaxed);
    m_last_emit_status.store(AudioEmitStatus::rejected_sequence_exhausted,
                             std::memory_order_relaxed);
    return {.status = AudioEmitStatus::rejected_sequence_exhausted};
  }

  const AudioEventIdentity identity{
      .tick = tick,
      .sequence = static_cast<std::uint16_t>(m_next_sequence++),
  };
  m_identities_assigned.fetch_add(1, std::memory_order_relaxed);
  if (!m_queue.try_push({identity, cue})) {
    m_events_dropped.fetch_add(1, std::memory_order_relaxed);
    m_last_emit_status.store(AudioEmitStatus::dropped_queue_full,
                             std::memory_order_relaxed);
    return {.status = AudioEmitStatus::dropped_queue_full,
            .identity = identity};
  }
  m_events_queued.fetch_add(1, std::memory_order_relaxed);
  update_maximum_depth(m_queue.depth());
  m_last_emit_status.store(AudioEmitStatus::queued,
                           std::memory_order_relaxed);
  return {.status = AudioEmitStatus::queued, .identity = identity};
}

auto AudioRuntime::try_take_event() noexcept -> std::optional<AudioEvent> {
  auto event = m_queue.try_pop();
  if (event) m_events_dequeued.fetch_add(1, std::memory_order_relaxed);
  return event;
}

auto AudioRuntime::service() noexcept -> void {
  if (m_stopped || m_mode == AudioRuntimeMode::disabled || !m_backend) return;
  if (m_backend->state() == AudioBackendState::failed) {
    backend_lost();
    return;
  }
  if (m_backend->state() != AudioBackendState::no_device) return;
  while (try_take_event()) {
  }
}

auto AudioRuntime::reset(AudioResetReason reason) noexcept -> void {
  if (m_backend) m_backend->stop();
  const auto cleared = m_queue.clear();
  m_events_discarded_on_reset.fetch_add(cleared,
                                        std::memory_order_relaxed);
  m_last_tick.reset();
  m_next_sequence = 0;
  m_reset_count.fetch_add(1, std::memory_order_relaxed);
  m_last_reset_reason.store(reason, std::memory_order_relaxed);
  if (reason != AudioResetReason::backend_loss &&
      reason != AudioResetReason::shutdown &&
      m_mode != AudioRuntimeMode::disabled && m_backend) {
    auto backend = std::move(m_backend);
    start_backend(std::move(backend));
  }
}

auto AudioRuntime::backend_lost() -> void {
  if (m_stopped) return;
  if (m_backend) {
    const auto failed = m_backend->diagnostics();
    m_last_backend_failure =
        failed.failure == AudioBackendFailure::none
            ? AudioBackendFailure::device_lost
            : failed.failure;
    m_retired_callback_count += failed.callback_count;
    m_retired_output_underflow_count += failed.output_underflow_count;
    m_backend_failure_count.fetch_add(1, std::memory_order_relaxed);
  }
  m_backend_loss_count.fetch_add(1, std::memory_order_relaxed);
  reset(AudioResetReason::backend_loss);
  start_backend(std::make_unique<NoDeviceAudioBackend>());
}

auto AudioRuntime::shutdown() noexcept -> void {
  if (m_stopped) return;
  reset(AudioResetReason::shutdown);
  m_stopped = true;
}

auto AudioRuntime::render(std::span<float> interleaved_samples) noexcept
    -> std::optional<AudioBufferError> {
  if (m_stopped) return AudioBufferError::stopped;
  if (interleaved_samples.empty() ||
      interleaved_samples.size() % kAudioChannelCount != 0 ||
      interleaved_samples.size() / kAudioChannelCount >
          kMaximumAudioFramesPerCallback) {
    return AudioBufferError::invalid_dimensions;
  }
  std::ranges::fill(interleaved_samples, 0.0F);
  while (try_take_event()) {
  }
  return std::nullopt;
}

auto AudioRuntime::diagnostics() const noexcept -> AudioDiagnostics {
  const auto backend = m_backend ? m_backend->diagnostics()
                                 : AudioBackendDiagnostics{};
  return {
      .mode = m_mode,
      .backend_state = backend.state,
      .backend_name = backend.name,
      .output_device_id = backend.output_device_id,
      .last_backend_failure = m_last_backend_failure,
      .last_emit_status =
          m_last_emit_status.load(std::memory_order_relaxed),
      .last_reset_reason =
          m_last_reset_reason.load(std::memory_order_relaxed),
      .identities_assigned =
          m_identities_assigned.load(std::memory_order_relaxed),
      .events_queued = m_events_queued.load(std::memory_order_relaxed),
      .events_dequeued = m_events_dequeued.load(std::memory_order_relaxed),
      .events_dropped = m_events_dropped.load(std::memory_order_relaxed),
      .events_rejected = m_events_rejected.load(std::memory_order_relaxed),
      .events_discarded_on_reset =
          m_events_discarded_on_reset.load(std::memory_order_relaxed),
      .reset_count = m_reset_count.load(std::memory_order_relaxed),
      .backend_failure_count =
          m_backend_failure_count.load(std::memory_order_relaxed),
      .backend_loss_count =
          m_backend_loss_count.load(std::memory_order_relaxed),
      .callback_count =
          m_retired_callback_count + backend.callback_count,
      .output_underflow_count =
          m_retired_output_underflow_count +
          backend.output_underflow_count,
      .queue_depth = m_queue.depth(),
      .maximum_queue_depth =
          m_maximum_queue_depth.load(std::memory_order_relaxed),
  };
}

auto AudioRuntime::update_maximum_depth(std::size_t depth) noexcept -> void {
  auto observed = m_maximum_queue_depth.load(std::memory_order_relaxed);
  while (observed < depth &&
         !m_maximum_queue_depth.compare_exchange_weak(
             observed, depth, std::memory_order_relaxed,
             std::memory_order_relaxed)) {
  }
}

}  // namespace apsis_drift
