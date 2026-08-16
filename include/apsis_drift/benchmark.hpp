#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "apsis_drift/render_profile.hpp"

namespace apsis_drift {

enum class BenchmarkWorkload { landscape, orbital };

[[nodiscard]] auto workload_name(BenchmarkWorkload workload) noexcept
    -> std::string_view;
[[nodiscard]] auto workload_identifier(BenchmarkWorkload workload) noexcept
    -> std::string_view;
[[nodiscard]] auto parse_benchmark_workload(std::string_view text) noexcept
    -> std::optional<BenchmarkWorkload>;

struct BenchmarkSummary {
  std::size_t frames{};
  double elapsed_seconds{};
  double achieved_fps{};
  double render_avg_ms{};
  double render_p95_ms{};
  double work_avg_ms{};
  double work_p95_ms{};
  double bytes_per_frame{};
  double mebibytes_per_second{};
  std::uint64_t total_bytes{};
  std::uint64_t checksum{};
};

struct BenchmarkMeasurement {
  RenderConfiguration configuration{};
  BenchmarkSummary summary{};
};

struct CadenceAssessment {
  std::uint32_t target_fps{};
  double deadline_budget_ms{};
  double renderer_p95_headroom_ms{};
  double frame_work_p95_headroom_ms{};
  double required_mebibytes_per_second{};
};

[[nodiscard]] auto default_sweep_viewports()
    -> std::vector<RenderConfiguration>;
[[nodiscard]] auto default_sweep_fps() -> std::vector<std::uint32_t>;

[[nodiscard]] auto parse_sweep_viewports(std::string_view text)
    -> std::expected<std::vector<RenderConfiguration>, std::string>;
[[nodiscard]] auto parse_sweep_fps(std::string_view text)
    -> std::expected<std::vector<std::uint32_t>, std::string>;

[[nodiscard]] auto assess_cadence(const BenchmarkSummary& summary,
                                  std::uint32_t target_fps) noexcept
    -> CadenceAssessment;
[[nodiscard]] auto sweep_table(
    const std::vector<BenchmarkMeasurement>& measurements,
    const std::vector<std::uint32_t>& target_fps) -> std::string;
[[nodiscard]] auto sweep_json(
    const std::vector<BenchmarkMeasurement>& measurements,
    const std::vector<std::uint32_t>& target_fps, std::uint32_t seed,
    std::size_t frames_per_viewport,
    BenchmarkWorkload workload = BenchmarkWorkload::landscape) -> std::string;

}  // namespace apsis_drift
