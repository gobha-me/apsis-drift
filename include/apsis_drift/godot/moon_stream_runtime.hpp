#pragma once

#include "apsis_drift/godot/moon_streaming.hpp"

#include <chrono>
#include <future>
#include <optional>
#include <stop_token>

namespace apsis_drift::godot_spike {
using MoonTileMap =
    std::map<MoonStreamKey, std::shared_ptr<const MoonMeshTile>>;
struct MoonStreamBatch {
  MoonTileMap tiles;
  unsigned generated{}, reused{};
};
// Presentation residency only. One immutable actual Moon owner, one worker and
// one bounded complete cover. No Godot objects or simulation run on the worker.
class MoonStreamRuntime {
  MoonTerrainSampler sampler;
  MoonTileMap resident;
  std::vector<MoonStreamKey> requested;
  std::future<MoonStreamBatch> future;
  std::stop_source stop;

 public:
  explicit MoonStreamRuntime(const PhysicalMoonDescriptor& owner)
      : sampler{
            moon_stream_detail::require(MoonTerrainSampler::create(owner))} {}
  MoonStreamRuntime(const MoonStreamRuntime&) = delete;
  auto operator=(const MoonStreamRuntime&) -> MoonStreamRuntime& = delete;
  ~MoonStreamRuntime() {
    stop.request_stop();
    if (future.valid()) future.wait();
  }
  [[nodiscard]] auto owner() const -> const PhysicalMoonDescriptor& {
    return sampler.owner();
  }
  [[nodiscard]] auto busy() const -> bool { return future.valid(); }
  auto request(MoonFixedPositionMetres observer) -> bool {
    // Validate before the busy/repeat fast path; a malformed request never
    // inherits a prior request's apparent acceptance.
    const auto plan = plan_moon_tiles(sampler.owner(), observer);
    if (busy() || plan == requested) return false;
    const auto token = stop.get_token();
    auto worker = std::async(
        std::launch::async, [source = sampler, old = resident, plan, token]() {
          MoonStreamBatch batch;
          for (const auto key : plan) {
            if (token.stop_requested()) return MoonStreamBatch{};
            if (const auto found = old.find(key); found != old.end()) {
              batch.tiles.emplace(key, found->second);
              ++batch.reused;
            } else {
              batch.tiles.emplace(key, make_moon_stream_tile(source, key));
              ++batch.generated;
            }
          }
          return batch;
        });
    requested = plan;
    future = std::move(worker);
    return true;
  }
  auto poll() -> std::optional<MoonStreamBatch> {
    if (!future.valid() ||
        future.wait_for(std::chrono::seconds{0}) != std::future_status::ready)
      return {};
    auto batch = future.get();
    if (batch.tiles.size() < 6 || batch.tiles.size() > kMoonStreamMaxTiles)
      throw std::runtime_error("Incomplete Moon stream cover refused");
    resident = batch.tiles;
    return batch;
  }
};
} // namespace apsis_drift::godot_spike
