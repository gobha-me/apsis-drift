#include "apsis_drift/audio.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <format>
#include <limits>
#include <vector>

#include "apsis_drift/origin_return.hpp"
#include "apsis_drift/planetary_flight.hpp"
#include "apsis_drift/system_flight.hpp"

#if defined(APSIS_DRIFT_HAS_RTAUDIO)
#include "rtaudio_backend.hpp"
#endif

namespace apsis_drift {
namespace {

inline constexpr std::int32_t kSynthUnit{32'767};
inline constexpr std::int32_t kParameterSlewPerFrame{96};
inline constexpr std::int32_t kWarningSlewPerFrame{128};
inline constexpr std::uint32_t kWarningDurationFrames{kAudioSampleRate * 2U /
                                                      5U};

[[nodiscard]] auto valid_mode(FlightMode mode) noexcept -> bool {
  return mode == FlightMode::manual || mode == FlightMode::autopilot;
}

[[nodiscard]] auto engine_demand(FlightMode mode,
                                 const FlightControls& controls) noexcept
    -> float {
  if (mode == FlightMode::autopilot) return 0.72F;
  if (controls.forward || controls.backward) return 1.0F;
  if (controls.strafe_left || controls.strafe_right || controls.rise ||
      controls.fall) {
    return 0.68F;
  }
  if (controls.turn_left || controls.turn_right) return 0.35F;
  return 0.0F;
}

[[nodiscard]] auto normalized_speed(double speed, double maximum) noexcept
    -> float {
  if (!std::isfinite(speed) || !std::isfinite(maximum) || speed < 0.0 ||
      maximum <= 0.0) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  return static_cast<float>(std::clamp(speed / maximum, 0.0, 1.0));
}

[[nodiscard]] auto valid_parameters(FlightAudioParameters parameters) noexcept
    -> bool {
  const auto bounded = [](float value) {
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
  };
  return bounded(parameters.engine_demand) && bounded(parameters.speed) &&
         bounded(parameters.atmosphere);
}

[[nodiscard]] auto to_synth(float value) noexcept -> std::int32_t {
  return static_cast<std::int32_t>(
      std::lround(std::clamp(value, 0.0F, 1.0F) * kSynthUnit));
}

auto approach(std::int32_t& value, std::int32_t target,
              std::int32_t step) noexcept -> void {
  if (value < target)
    value += std::min(step, target - value);
  else if (value > target)
    value -= std::min(step, value - target);
}

[[nodiscard]] auto triangle(std::uint32_t phase) noexcept -> std::int32_t {
  const auto half = static_cast<std::int32_t>(phase >> 16U);
  const auto folded = half < 32'768 ? half : 65'535 - half;
  return folded * 2 - kSynthUnit;
}

auto hash_sample(std::uint64_t& hash, float sample) noexcept -> void {
  constexpr std::uint64_t prime{1'099'511'628'211ULL};
  const auto word = std::bit_cast<std::uint32_t>(sample);
  for (unsigned byte = 0; byte < 4; ++byte) {
    hash ^= (word >> (byte * 8U)) & 0xFFU;
    hash *= prime;
  }
}

} // namespace

auto resolve_flight_audio(const FlightState& state) noexcept
    -> std::expected<FlightAudioTelemetry, FlightAudioError> {
  const double speed = std::hypot(static_cast<double>(state.velocity.x),
                                  static_cast<double>(state.velocity.y),
                                  static_cast<double>(state.velocity.vertical));
  if (!valid_mode(state.mode) || !std::isfinite(state.pose.altitude) ||
      !std::isfinite(state.clearance) || state.clearance < 0.0F ||
      !std::isfinite(speed)) {
    return std::unexpected{FlightAudioError::invalid_state};
  }
  return FlightAudioTelemetry{
      .tick = state.tick,
      .parameters = {.active = true,
                     .engine_demand = engine_demand(state.mode, state.controls),
                     .speed = normalized_speed(speed, 80.0),
                     .atmosphere = 1.0F},
      .low_clearance = state.clearance <= kLowClearanceWarningMetres,
  };
}

auto resolve_flight_audio(const PlanetDescriptor& planet,
                          const PlanetaryFlightState& state) noexcept
    -> std::expected<FlightAudioTelemetry, FlightAudioError> {
  if (state.planet != planet.id) {
    return std::unexpected{FlightAudioError::invalid_planet};
  }
  const auto performance = flight_performance(planet, state.regime);
  const auto bands = flight_regime_bands(planet);
  const double speed = std::hypot(state.velocity.east_metres_per_second,
                                  state.velocity.north_metres_per_second,
                                  state.velocity.up_metres_per_second);
  if (!performance || !bands || !valid_mode(state.mode) ||
      !std::isfinite(state.pose.position.altitude_metres) ||
      !std::isfinite(state.clearance_metres) ||
      state.clearance_metres < kMinimumFlightClearanceMetres ||
      !std::isfinite(speed)) {
    return std::unexpected{FlightAudioError::invalid_state};
  }

  double atmosphere{};
  if (planet.atmosphere_class != AtmosphereClass::airless) {
    const double ceiling = bands->atmosphere_enter_altitude_metres;
    const double depth = std::clamp(
        1.0 - state.pose.position.altitude_metres / ceiling, 0.0, 1.0);
    const double pressure =
        static_cast<double>(planet.atmosphere_pressure.value) /
        static_cast<double>(AtmospherePressureMillibars::max);
    atmosphere = std::clamp(pressure * depth * depth, 0.0, 1.0);
  }
  const double maximum_speed = std::hypot(performance->maximum_horizontal_speed,
                                          performance->maximum_vertical_speed);
  const FlightAudioParameters parameters{
      .active = true,
      .engine_demand = engine_demand(state.mode, state.controls),
      .speed = normalized_speed(speed, maximum_speed),
      .atmosphere = static_cast<float>(atmosphere),
  };
  if (!valid_parameters(parameters)) {
    return std::unexpected{FlightAudioError::invalid_state};
  }
  return FlightAudioTelemetry{
      .tick = state.tick,
      .parameters = parameters,
      .low_clearance = state.clearance_metres <= kLowClearanceWarningMetres,
  };
}

auto resolve_flight_audio(const SystemFlightState& state) noexcept
    -> std::expected<FlightAudioTelemetry, FlightAudioError> {
  const double speed =
      std::hypot(state.velocity.x, state.velocity.y, state.velocity.z);
  if (!valid_mode(state.mode) || !std::isfinite(speed)) {
    return std::unexpected{FlightAudioError::invalid_state};
  }
  return FlightAudioTelemetry{
      .tick = state.tick,
      .parameters = {.active = true,
                     .engine_demand = engine_demand(state.mode, state.controls),
                     .speed = normalized_speed(
                         speed, kSystemFlightMaximumRelativeSpeed),
                     .atmosphere = 0.0F},
  };
}

auto resolve_flight_audio(const OriginStationFlightState& state) noexcept
    -> std::expected<FlightAudioTelemetry, FlightAudioError> {
  const double speed =
      std::hypot(state.relative_velocity.x, state.relative_velocity.y,
                 state.relative_velocity.z);
  if (!valid_mode(state.mode) || !std::isfinite(speed)) {
    return std::unexpected{FlightAudioError::invalid_state};
  }
  return FlightAudioTelemetry{
      .tick = state.tick,
      .parameters = {.active = true,
                     .engine_demand = engine_demand(state.mode, state.controls),
                     .speed = normalized_speed(
                         speed, kHomeSignalStationFlightMaximumSpeed),
                     .atmosphere = 0.0F},
  };
}

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
  if (identity.tick > std::numeric_limits<std::uint64_t>::max() /
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
  while (try_pop())
    ++cleared;
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
                           AudioOutputSelection selection,
                           std::unique_ptr<FirstLightAudioPack> asset_pack)
    : m_mode(mode), m_asset_pack{std::move(asset_pack)} {
  if (mode == AudioRuntimeMode::disabled) return;
  if (backend) {
    start_backend(std::move(backend));
  } else if (mode == AudioRuntimeMode::automatic) {
    start_backend(make_device_audio_backend(selection));
  } else {
    start_backend(std::make_unique<NoDeviceAudioBackend>());
  }
}

AudioRuntime::~AudioRuntime() {
  shutdown();
}

auto AudioRuntime::start_backend(std::unique_ptr<AudioBackend> backend)
    -> void {
  if (backend && backend->start(kAudioFormat, *this)) {
    m_backend = std::move(backend);
    return;
  }
  if (backend) {
    const auto failed = backend->diagnostics();
    m_last_backend_failure = failed.failure == AudioBackendFailure::none
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
  if (!cue.valid()) {
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
    m_events_rejected.fetch_add(1, std::memory_order_relaxed);
    m_last_emit_status.store(AudioEmitStatus::rejected_invalid_cue,
                             std::memory_order_relaxed);
    return {.status = AudioEmitStatus::rejected_invalid_cue};
  }
  return emit_event({.identity = {.tick = tick}, .cue = cue});
}

auto AudioRuntime::emit_flight_parameters(
    SimulationTick tick, FlightAudioParameters parameters) noexcept
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
  if (!valid_parameters(parameters)) {
    m_events_rejected.fetch_add(1, std::memory_order_relaxed);
    m_last_emit_status.store(AudioEmitStatus::rejected_invalid_parameters,
                             std::memory_order_relaxed);
    return {.status = AudioEmitStatus::rejected_invalid_parameters};
  }
  if (m_last_parameter_tick && tick == *m_last_parameter_tick &&
      (!m_last_tick || tick == *m_last_tick)) {
    m_parameter_updates_coalesced.fetch_add(1, std::memory_order_relaxed);
    m_last_emit_status.store(AudioEmitStatus::coalesced_same_tick,
                             std::memory_order_relaxed);
    return {.status = AudioEmitStatus::coalesced_same_tick};
  }
  auto result = emit_event({.identity = {.tick = tick},
                            .kind = AudioEventKind::flight_parameters,
                            .parameters = parameters});
  if (result.identity) m_last_parameter_tick = tick;
  if (result.status == AudioEmitStatus::queued) {
    m_parameter_updates_queued.fetch_add(1, std::memory_order_relaxed);
  }
  return result;
}

auto AudioRuntime::emit_event(AudioEvent event) noexcept -> AudioEmitResult {
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
  const auto tick = event.identity.tick;
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
  event.identity = identity;
  m_identities_assigned.fetch_add(1, std::memory_order_relaxed);
  if (!m_queue.try_push(event)) {
    m_events_dropped.fetch_add(1, std::memory_order_relaxed);
    m_last_emit_status.store(AudioEmitStatus::dropped_queue_full,
                             std::memory_order_relaxed);
    return {.status = AudioEmitStatus::dropped_queue_full,
            .identity = identity};
  }
  m_events_queued.fetch_add(1, std::memory_order_relaxed);
  update_maximum_depth(m_queue.depth());
  m_last_emit_status.store(AudioEmitStatus::queued, std::memory_order_relaxed);
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
  m_events_discarded_on_reset.fetch_add(cleared, std::memory_order_relaxed);
  m_last_tick.reset();
  m_last_parameter_tick.reset();
  m_next_sequence = 0;
  reset_synth();
  if (m_asset_pack) m_asset_pack->reset();
  m_music_state.store(MusicState::silent, std::memory_order_relaxed);
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
    m_last_backend_failure = failed.failure == AudioBackendFailure::none
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
  if (m_mode == AudioRuntimeMode::disabled) {
    std::ranges::fill(interleaved_samples, 0.0F);
    return std::nullopt;
  }
  while (auto event = try_take_event())
    apply_event(*event);
  const auto frames = interleaved_samples.size() / kAudioChannelCount;
  if (!m_asset_pack || !m_asset_pack->render(interleaved_samples)) {
    std::ranges::fill(interleaved_samples, 0.0F);
  }
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const float sample = render_sample();
    for (std::size_t channel = 0; channel < kAudioChannelCount; ++channel) {
      auto& output = interleaved_samples[frame * kAudioChannelCount + channel];
      output = std::clamp(output + sample, -1.0F, 1.0F);
    }
  }
  m_waveform_frames_generated.fetch_add(frames, std::memory_order_relaxed);
  return std::nullopt;
}

auto AudioRuntime::apply_event(const AudioEvent& event) noexcept -> void {
  if (event.kind == AudioEventKind::flight_parameters) {
    m_engine_target =
        event.parameters.active ? to_synth(event.parameters.engine_demand) : 0;
    m_speed_target =
        event.parameters.active ? to_synth(event.parameters.speed) : 0;
    m_atmosphere_target =
        event.parameters.active ? to_synth(event.parameters.atmosphere) : 0;
    return;
  }
  if (event.cue == kLowClearanceAudioCue) {
    m_warning_frames_remaining = kWarningDurationFrames;
  } else if (event.cue == kStopFlightAudioCue) {
    m_engine_target = 0;
    m_speed_target = 0;
    m_atmosphere_target = 0;
    m_warning_frames_remaining = 0;
  } else if (m_asset_pack) {
    m_asset_pack->cue(event.cue);
  }
}

auto AudioRuntime::set_music_state(MusicState state) noexcept -> void {
  m_music_state.store(state, std::memory_order_relaxed);
  if (m_asset_pack) m_asset_pack->set_music_state(state);
}

auto AudioRuntime::pause_music() noexcept -> void {
  if (m_asset_pack) m_asset_pack->pause_music();
}

auto AudioRuntime::resume_music() noexcept -> void {
  if (m_asset_pack) m_asset_pack->resume_music();
}

auto AudioRuntime::render_sample() noexcept -> float {
  approach(m_engine_level, m_engine_target, kParameterSlewPerFrame);
  approach(m_speed_level, m_speed_target, kParameterSlewPerFrame);
  approach(m_atmosphere_level, m_atmosphere_target, kParameterSlewPerFrame);

  std::int32_t sample{};
  if (!m_asset_pack) {
    const auto pitch =
        static_cast<std::uint32_t>((m_engine_level * 2 + m_speed_level) / 3);
    const auto engine_hz =
        42U + static_cast<std::uint32_t>(138ULL * pitch / kSynthUnit);
    m_engine_phase += static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(engine_hz) << 32U) / kAudioSampleRate);
    const auto engine_amplitude = m_engine_level / 6 + m_speed_level / 12;
    sample = triangle(m_engine_phase) * engine_amplitude / kSynthUnit;

    m_noise_state ^= m_noise_state << 13U;
    m_noise_state ^= m_noise_state >> 17U;
    m_noise_state ^= m_noise_state << 5U;
    const auto noise = static_cast<std::int32_t>(m_noise_state >> 16U) - 32'768;
    m_wind_filter += (noise - m_wind_filter) / 16;
    const auto wind_amplitude = static_cast<std::int32_t>(
        static_cast<std::int64_t>(m_atmosphere_level) * m_speed_level /
        kSynthUnit / 7);
    sample += m_wind_filter * wind_amplitude / kSynthUnit;
  }

  const auto warning_target =
      m_warning_frames_remaining > 0 ? kSynthUnit / 5 : 0;
  approach(m_warning_level, warning_target, kWarningSlewPerFrame);
  if (m_warning_frames_remaining > 0) --m_warning_frames_remaining;
  constexpr std::uint32_t warning_hz{720};
  m_warning_phase += static_cast<std::uint32_t>(
      (static_cast<std::uint64_t>(warning_hz) << 32U) / kAudioSampleRate);
  sample += triangle(m_warning_phase) * m_warning_level / kSynthUnit;

  sample = std::clamp(sample, -kSynthUnit, kSynthUnit);
  return static_cast<float>(sample) / 32'768.0F;
}

auto AudioRuntime::reset_synth() noexcept -> void {
  m_engine_level = 0;
  m_engine_target = 0;
  m_speed_level = 0;
  m_speed_target = 0;
  m_atmosphere_level = 0;
  m_atmosphere_target = 0;
  m_wind_filter = 0;
  m_engine_phase = 0;
  m_warning_phase = 0;
  m_noise_state = 0xA51D'5EEDU;
  m_warning_frames_remaining = 0;
  m_warning_level = 0;
}

auto AudioRuntime::diagnostics() const noexcept -> AudioDiagnostics {
  const auto backend =
      m_backend ? m_backend->diagnostics() : AudioBackendDiagnostics{};
  return {
      .mode = m_mode,
      .backend_state = backend.state,
      .backend_name = backend.name,
      .output_device_id = backend.output_device_id,
      .last_backend_failure = m_last_backend_failure,
      .last_emit_status = m_last_emit_status.load(std::memory_order_relaxed),
      .last_reset_reason = m_last_reset_reason.load(std::memory_order_relaxed),
      .identities_assigned =
          m_identities_assigned.load(std::memory_order_relaxed),
      .events_queued = m_events_queued.load(std::memory_order_relaxed),
      .events_dequeued = m_events_dequeued.load(std::memory_order_relaxed),
      .events_dropped = m_events_dropped.load(std::memory_order_relaxed),
      .events_rejected = m_events_rejected.load(std::memory_order_relaxed),
      .events_discarded_on_reset =
          m_events_discarded_on_reset.load(std::memory_order_relaxed),
      .parameter_updates_queued =
          m_parameter_updates_queued.load(std::memory_order_relaxed),
      .parameter_updates_coalesced =
          m_parameter_updates_coalesced.load(std::memory_order_relaxed),
      .reset_count = m_reset_count.load(std::memory_order_relaxed),
      .backend_failure_count =
          m_backend_failure_count.load(std::memory_order_relaxed),
      .backend_loss_count =
          m_backend_loss_count.load(std::memory_order_relaxed),
      .callback_count = m_retired_callback_count + backend.callback_count,
      .output_underflow_count =
          m_retired_output_underflow_count + backend.output_underflow_count,
      .queue_depth = m_queue.depth(),
      .maximum_queue_depth =
          m_maximum_queue_depth.load(std::memory_order_relaxed),
      .waveform_frames_generated =
          m_waveform_frames_generated.load(std::memory_order_relaxed),
      .asset_pack_loaded = m_asset_pack != nullptr,
      .music_state = m_music_state.load(std::memory_order_relaxed),
      .active_sfx_voices =
          m_asset_pack ? m_asset_pack->active_sfx_voices() : 0U,
      .dropped_sfx_voices =
          m_asset_pack ? m_asset_pack->dropped_sfx_voices() : 0U,
      .asset_packaged_bytes =
          m_asset_pack ? m_asset_pack->packaged_bytes() : 0U,
      .asset_decoded_bytes = m_asset_pack ? m_asset_pack->decoded_bytes() : 0U,
  };
}

auto AudioRuntime::update_maximum_depth(std::size_t depth) noexcept -> void {
  auto observed = m_maximum_queue_depth.load(std::memory_order_relaxed);
  while (observed < depth && !m_maximum_queue_depth.compare_exchange_weak(
                                 observed, depth, std::memory_order_relaxed,
                                 std::memory_order_relaxed)) {
  }
}

auto MusicDirector::update(MusicState state) noexcept -> void {
  if (state == m_state) return;
  m_state = state;
  m_runtime.set_music_state(state);
}

auto MusicDirector::pause() noexcept -> void {
  if (m_paused) return;
  m_paused = true;
  m_runtime.pause_music();
}

auto MusicDirector::resume() noexcept -> void {
  if (!m_paused) return;
  m_paused = false;
  m_runtime.resume_music();
}

auto MusicDirector::reset() noexcept -> void {
  m_paused = false;
  m_state = MusicState::silent;
  m_runtime.set_music_state(MusicState::silent);
}

auto benchmark_flight_audio(std::uint64_t ticks) -> AudioSynthesisBenchmark {
  AudioSynthesisBenchmark result{.ticks = ticks};
  if (ticks == 0 || ticks > std::numeric_limits<std::uint64_t>::max() /
                                kAudioFramesPerSimulationTick) {
    return result;
  }
  AudioRuntime audio;
  std::vector<float> buffer(kAudioFramesPerSimulationTick * kAudioChannelCount);
  std::uint64_t checksum{1'469'598'103'934'665'603ULL};
  const auto started = std::chrono::steady_clock::now();
  for (std::uint64_t tick = 0; tick < ticks; ++tick) {
    const auto cycle = static_cast<float>(tick % 240U) / 239.0F;
    const float ramp = cycle <= 0.5F ? cycle * 2.0F : (1.0F - cycle) * 2.0F;
    (void)audio.emit_flight_parameters(
        tick, {.active = true,
               .engine_demand = 0.2F + ramp * 0.8F,
               .speed = ramp,
               .atmosphere = static_cast<float>(tick % 120U) / 119.0F});
    if (tick % 240U == 120U) {
      (void)audio.emit(tick, kLowClearanceAudioCue);
    }
    if (audio.render(buffer)) return result;
    for (const float sample : buffer)
      hash_sample(checksum, sample);
  }
  const auto finished = std::chrono::steady_clock::now();
  result.sample_frames = ticks * kAudioFramesPerSimulationTick;
  result.audio_seconds = static_cast<double>(result.sample_frames) /
                         static_cast<double>(kAudioSampleRate);
  result.elapsed_seconds =
      std::chrono::duration<double>(finished - started).count();
  result.realtime_factor = result.elapsed_seconds > 0.0
                               ? result.audio_seconds / result.elapsed_seconds
                               : 0.0;
  result.checksum = checksum;
  const auto diagnostics = audio.diagnostics();
  result.maximum_queue_depth = diagnostics.maximum_queue_depth;
  result.events_dropped = diagnostics.events_dropped;
  return result;
}

auto audio_benchmark_json(const AudioSynthesisBenchmark& benchmark)
    -> std::string {
  return std::format("{{\n"
                     "  \"schema_version\": 1,\n"
                     "  \"workload\": \"procedural-flight-audio\",\n"
                     "  \"ticks\": \"{}\",\n"
                     "  \"sample_frames\": \"{}\",\n"
                     "  \"audio_seconds\": {:.6f},\n"
                     "  \"elapsed_seconds\": {:.6f},\n"
                     "  \"realtime_factor\": {:.6f},\n"
                     "  \"checksum\": \"{}\",\n"
                     "  \"maximum_queue_depth\": {},\n"
                     "  \"events_dropped\": \"{}\"\n"
                     "}}\n",
                     benchmark.ticks, benchmark.sample_frames,
                     benchmark.audio_seconds, benchmark.elapsed_seconds,
                     benchmark.realtime_factor, benchmark.checksum,
                     benchmark.maximum_queue_depth, benchmark.events_dropped);
}

} // namespace apsis_drift
