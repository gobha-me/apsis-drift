#include "audio_callback.hpp"

#include <algorithm>
#include <span>

namespace apsis_drift::detail {

auto AudioCallbackBridge::activate(AudioRenderSource& source) noexcept -> void {
  m_source = &source;
  m_failure.store(AudioBackendFailure::none, std::memory_order_relaxed);
  m_active.store(true, std::memory_order_release);
}

auto AudioCallbackBridge::deactivate() noexcept -> void {
  m_active.store(false, std::memory_order_release);
  m_source = nullptr;
}

auto AudioCallbackBridge::fail(AudioBackendFailure failure) noexcept -> void {
  if (failure == AudioBackendFailure::none) return;
  auto expected = AudioBackendFailure::none;
  (void)m_failure.compare_exchange_strong(
      expected, failure, std::memory_order_relaxed, std::memory_order_relaxed);
}

auto AudioCallbackBridge::render(float* output, std::size_t frames,
                                 bool output_underflow) noexcept
    -> AudioCallbackAction {
  if (m_failure.load(std::memory_order_relaxed) != AudioBackendFailure::none ||
      !m_active.load(std::memory_order_acquire) || m_source == nullptr ||
      output == nullptr || frames == 0 ||
      frames > kMaximumAudioFramesPerCallback) {
    fail(AudioBackendFailure::callback_failed);
    return AudioCallbackAction::abort_stream;
  }

  m_callback_count.fetch_add(1, std::memory_order_relaxed);
  if (output_underflow) {
    m_output_underflow_count.fetch_add(1, std::memory_order_relaxed);
  }

  const auto samples = std::span<float>{output, frames * kAudioChannelCount};
  if (m_source->render(samples)) {
    std::ranges::fill(samples, 0.0F);
    fail(AudioBackendFailure::callback_failed);
    return AudioCallbackAction::abort_stream;
  }
  return AudioCallbackAction::continue_stream;
}

auto AudioCallbackBridge::failure() const noexcept -> AudioBackendFailure {
  return m_failure.load(std::memory_order_relaxed);
}

auto AudioCallbackBridge::callback_count() const noexcept -> std::uint64_t {
  return m_callback_count.load(std::memory_order_relaxed);
}

auto AudioCallbackBridge::output_underflow_count() const noexcept
    -> std::uint64_t {
  return m_output_underflow_count.load(std::memory_order_relaxed);
}

} // namespace apsis_drift::detail
