#pragma once

#include <memory>

#include "apsis_drift/audio.hpp"

namespace apsis_drift::detail {

[[nodiscard]] auto make_rtaudio_backend(AudioOutputSelection selection)
    -> std::unique_ptr<AudioBackend>;

}  // namespace apsis_drift::detail
