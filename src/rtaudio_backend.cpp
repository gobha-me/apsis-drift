#include "rtaudio_backend.hpp"

#include <RtAudio.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>

#include "audio_callback.hpp"

namespace apsis_drift::detail {
namespace {

class RtAudioBackend final : public AudioBackend {
 public:
  explicit RtAudioBackend(AudioOutputSelection selection)
      : m_selection(selection) {}

  ~RtAudioBackend() override { stop(); }

  [[nodiscard]] auto name() const noexcept -> std::string_view override {
    return "rtaudio";
  }

  [[nodiscard]] auto state() const noexcept
      -> AudioBackendState override {
    return m_state.load(std::memory_order_acquire);
  }

  [[nodiscard]] auto diagnostics() const noexcept
      -> AudioBackendDiagnostics override {
    return {
        .name = name(),
        .state = state(),
        .failure = m_failure.load(std::memory_order_relaxed),
        .output_device_id = m_output_device_id,
        .callback_count = m_callback.callback_count(),
        .output_underflow_count = m_callback.output_underflow_count(),
    };
  }

  [[nodiscard]] auto start(AudioFormat format,
                           AudioRenderSource& source) noexcept
      -> bool override {
    stop();
    m_failure.store(AudioBackendFailure::none, std::memory_order_relaxed);
    m_output_device_id.reset();
    if (format != kAudioFormat) {
      fail(AudioBackendFailure::open_failed);
      return false;
    }

    try {
#if defined(__linux__)
      if (!std::filesystem::is_directory("/dev/snd")) {
        fail(AudioBackendFailure::no_output_device);
        return false;
      }
#endif
      m_audio = std::make_unique<RtAudio>(
          RtAudio::UNSPECIFIED,
          [this](RtAudioErrorType type, const std::string&) {
            if (type == RTAUDIO_WARNING ||
                state() != AudioBackendState::running) {
              return;
            }
            fail(type == RTAUDIO_DEVICE_DISCONNECT
                     ? AudioBackendFailure::device_lost
                     : AudioBackendFailure::callback_failed);
            m_callback.fail(
                type == RTAUDIO_DEVICE_DISCONNECT
                    ? AudioBackendFailure::device_lost
                    : AudioBackendFailure::callback_failed);
          });

      const auto device_ids = m_audio->getDeviceIds();
      if (device_ids.empty()) {
        fail(AudioBackendFailure::no_output_device);
        return false;
      }

      const auto requested =
          m_selection.device_id.value_or(m_audio->getDefaultOutputDevice());
      if (requested == 0 ||
          std::ranges::find(device_ids, requested) == device_ids.end()) {
        fail(m_selection.device_id
                 ? AudioBackendFailure::invalid_selected_device
                 : AudioBackendFailure::no_output_device);
        return false;
      }

      const auto device = m_audio->getDeviceInfo(requested);
      if (device.ID != requested ||
          device.outputChannels < kAudioChannelCount) {
        fail(m_selection.device_id
                 ? AudioBackendFailure::invalid_selected_device
                 : AudioBackendFailure::no_output_device);
        return false;
      }

      RtAudio::StreamParameters output{
          .deviceId = requested,
          .nChannels = kAudioChannelCount,
          .firstChannel = 0,
      };
      unsigned int frames{512};
      m_callback.activate(source);
      const auto opened = m_audio->openStream(
          &output, nullptr, RTAUDIO_FLOAT32, kAudioSampleRate, &frames,
          &RtAudioBackend::callback, this);
      if (opened != RTAUDIO_NO_ERROR || frames == 0 ||
          frames > kMaximumAudioFramesPerCallback) {
        m_callback.deactivate();
        if (m_audio->isStreamOpen()) m_audio->closeStream();
        fail(AudioBackendFailure::open_failed);
        return false;
      }
      m_output_device_id = requested;
      if (m_audio->startStream() != RTAUDIO_NO_ERROR) {
        m_callback.deactivate();
        if (m_audio->isStreamOpen()) m_audio->closeStream();
        fail(AudioBackendFailure::start_failed);
        return false;
      }
      m_state.store(AudioBackendState::running, std::memory_order_release);
      return true;
    } catch (...) {
      m_callback.deactivate();
      m_audio.reset();
      fail(AudioBackendFailure::discovery_failed);
      return false;
    }
  }

  auto stop() noexcept -> void override {
    if (m_audio) {
      if (m_audio->isStreamRunning()) (void)m_audio->abortStream();
      if (m_audio->isStreamOpen()) m_audio->closeStream();
    }
    m_callback.deactivate();
    m_audio.reset();
    m_state.store(AudioBackendState::stopped, std::memory_order_release);
  }

 private:
  static auto callback(void* output, void*, unsigned int frames, double,
                       RtAudioStreamStatus status, void* user_data) noexcept
      -> int {
    auto& backend = *static_cast<RtAudioBackend*>(user_data);
    const auto action = backend.m_callback.render(
        static_cast<float*>(output), frames,
        (status & RTAUDIO_OUTPUT_UNDERFLOW) != 0);
    if (action == AudioCallbackAction::abort_stream) {
      backend.fail(backend.m_callback.failure());
      return 2;
    }
    return 0;
  }

  auto fail(AudioBackendFailure failure) noexcept -> void {
    if (failure == AudioBackendFailure::none) return;
    m_failure.store(failure, std::memory_order_relaxed);
    m_state.store(AudioBackendState::failed, std::memory_order_release);
  }

  AudioOutputSelection m_selection;
  std::unique_ptr<RtAudio> m_audio;
  AudioCallbackBridge m_callback;
  std::atomic<AudioBackendState> m_state{AudioBackendState::stopped};
  std::atomic<AudioBackendFailure> m_failure{AudioBackendFailure::none};
  std::optional<std::uint32_t> m_output_device_id;
};

}  // namespace

auto make_rtaudio_backend(AudioOutputSelection selection)
    -> std::unique_ptr<AudioBackend> {
  return std::make_unique<RtAudioBackend>(selection);
}

}  // namespace apsis_drift::detail
