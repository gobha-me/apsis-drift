#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "apsis_drift/audio.hpp"

namespace apsis_drift::detail {

enum class AudioCallbackAction : std::uint8_t {
  continue_stream,
  abort_stream,
};

class AudioCallbackBridge {
 public:
  auto activate(AudioRenderSource& source) noexcept -> void;
  auto deactivate() noexcept -> void;
  auto fail(AudioBackendFailure failure) noexcept -> void;

  [[nodiscard]] auto render(float* output, std::size_t frames,
                            bool output_underflow) noexcept
      -> AudioCallbackAction;
  [[nodiscard]] auto failure() const noexcept -> AudioBackendFailure;
  [[nodiscard]] auto callback_count() const noexcept -> std::uint64_t;
  [[nodiscard]] auto output_underflow_count() const noexcept
      -> std::uint64_t;

 private:
  AudioRenderSource* m_source{};
  std::atomic<bool> m_active{};
  std::atomic<AudioBackendFailure> m_failure{AudioBackendFailure::none};
  std::atomic<std::uint64_t> m_callback_count{};
  std::atomic<std::uint64_t> m_output_underflow_count{};
};

}  // namespace apsis_drift::detail
