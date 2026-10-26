#pragma once

#include "apsis_drift/body_ephemeris.hpp"
#include "apsis_drift/freedom_save.hpp"
#include "apsis_drift/simulation.hpp"
#include "apsis_drift/surface_landmarks.hpp"

#include <cstdint>

namespace apsis_drift {
// Physical pouch contents and first-collection notes are separate. Source
// authentication is not a composition assay or a navigation fact.
struct FreedomFieldSampleRecord {
  BodyTarget target;
  SurfaceLandmarkCell cell;
  SurfaceLandmarkId site;
  std::uint32_t terrain_recipe{};
  SurfaceLandmarkKind kind{};
  StarterCraftId craft;
  SimulationTick tick{};
  friend auto operator==(const FreedomFieldSampleRecord&,
                         const FreedomFieldSampleRecord&) -> bool = default;
};
} // namespace apsis_drift
