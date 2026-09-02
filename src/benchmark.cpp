#include "apsis_drift/benchmark.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <system_error>

namespace apsis_drift {
namespace {

template <typename Function>
[[nodiscard]] auto for_each_csv_token(std::string_view text,
                                      Function&& function)
    -> std::expected<void, std::string> {
  if (text.empty()) return std::unexpected{"sweep list must not be empty"};

  std::size_t offset{};
  while (offset <= text.size()) {
    const auto separator = text.find(',', offset);
    const auto end =
        separator == std::string_view::npos ? text.size() : separator;
    const auto token = text.substr(offset, end - offset);
    if (token.empty()) {
      return std::unexpected{"sweep list entries must not be empty"};
    }
    if (auto result = function(token); !result) return result;
    if (separator == std::string_view::npos) break;
    offset = separator + 1;
  }
  return {};
}

[[nodiscard]] auto same_viewport(const RenderConfiguration& left,
                                 const RenderConfiguration& right) noexcept
    -> bool {
  return left.viewport == right.viewport;
}

auto append_summary_json(std::string& output, const BenchmarkSummary& summary)
    -> void {
  output += std::format(
      "        \"frames\": {},\n"
      "        \"elapsed_seconds\": {:.6f},\n"
      "        \"achieved_fps\": {:.6f},\n"
      "        \"renderer_avg_ms\": {:.6f},\n"
      "        \"renderer_p95_ms\": {:.6f},\n"
      "        \"frame_work_avg_ms\": {:.6f},\n"
      "        \"frame_work_p95_ms\": {:.6f},\n"
      "        \"bytes_per_frame\": {:.6f},\n"
      "        \"mebibytes_per_second\": {:.6f},\n"
      "        \"total_bytes\": \"{}\",\n"
      "        \"checksum\": \"{}\"",
      summary.frames, summary.elapsed_seconds, summary.achieved_fps,
      summary.render_avg_ms, summary.render_p95_ms, summary.work_avg_ms,
      summary.work_p95_ms, summary.bytes_per_frame,
      summary.mebibytes_per_second, summary.total_bytes, summary.checksum);
  if (summary.planetary_presentation) {
    const auto& value = *summary.planetary_presentation;
    output +=
        std::format(",\n"
                    "        \"planetary_presentation\": {{\n"
                    "          \"mode_frames\": {{\"orbital\": {}, "
                    "\"atmospheric\": {}, \"terrain_blend\": {}, "
                    "\"local_terrain\": {}}},\n"
                    "          \"orbital_render_avg_ms\": {:.6f},\n"
                    "          \"local_render_avg_ms\": {:.6f},\n"
                    "          \"composite_avg_ms\": {:.6f},\n"
                    "          \"total_avg_ms\": {:.6f},\n"
                    "          \"total_p95_ms\": {:.6f},\n"
                    "          \"maximum_tiles_touched\": {}\n"
                    "        }}\n",
                    value.orbital_frames, value.atmospheric_frames,
                    value.terrain_blend_frames, value.local_terrain_frames,
                    value.orbital_render_avg_ms, value.local_render_avg_ms,
                    value.composite_avg_ms, value.total_avg_ms,
                    value.total_p95_ms, value.maximum_tiles_touched);
  } else {
    output += '\n';
  }
}

} // namespace

auto workload_name(BenchmarkWorkload workload) noexcept -> std::string_view {
  switch (workload) {
    case BenchmarkWorkload::landscape: return "landscape";
    case BenchmarkWorkload::orbital: return "orbital";
    case BenchmarkWorkload::planetary: return "planetary";
    case BenchmarkWorkload::system: return "system";
  }
  return "landscape";
}

auto workload_identifier(BenchmarkWorkload workload) noexcept
    -> std::string_view {
  switch (workload) {
    case BenchmarkWorkload::landscape: return "voxel-landscape-rgba";
    case BenchmarkWorkload::orbital: return "orbital-planet-rgba";
    case BenchmarkWorkload::planetary: return "planetary-presentation-rgba";
    case BenchmarkWorkload::system: return "local-system-rgba";
  }
  return "voxel-landscape-rgba";
}

auto parse_benchmark_workload(std::string_view text) noexcept
    -> std::optional<BenchmarkWorkload> {
  if (text == "landscape") return BenchmarkWorkload::landscape;
  if (text == "orbital") return BenchmarkWorkload::orbital;
  if (text == "planetary") return BenchmarkWorkload::planetary;
  if (text == "system") return BenchmarkWorkload::system;
  return std::nullopt;
}

auto default_sweep_viewports() -> std::vector<RenderConfiguration> {
  return {resolve_render_configuration(RenderProfile::remote),
          resolve_render_configuration(RenderProfile::balanced),
          resolve_render_configuration(RenderProfile::local)};
}

auto default_sweep_fps() -> std::vector<std::uint32_t> {
  return {30, 60};
}

auto parse_sweep_viewports(std::string_view text)
    -> std::expected<std::vector<RenderConfiguration>, std::string> {
  std::vector<RenderConfiguration> result;
  const auto parsed = for_each_csv_token(
      text,
      [&result](std::string_view token) -> std::expected<void, std::string> {
        RenderConfiguration configuration;
        if (const auto profile = parse_render_profile(token)) {
          configuration = resolve_render_configuration(*profile);
        } else {
          const auto viewport = parse_viewport(token);
          if (!viewport) {
            return std::unexpected{
                std::format("invalid sweep viewport '{}': {}", token,
                            viewport_error_message(viewport.error()))};
          }
          configuration =
              resolve_render_configuration(RenderProfile::local, *viewport);
        }
        if (std::ranges::any_of(result, [&](const auto& existing) {
              return same_viewport(existing, configuration);
            })) {
          return std::unexpected{std::format("duplicate sweep viewport {}x{}",
                                             configuration.viewport.width,
                                             configuration.viewport.height)};
        }
        result.push_back(configuration);
        return {};
      });
  if (!parsed) return std::unexpected{parsed.error()};
  return result;
}

auto parse_sweep_fps(std::string_view text)
    -> std::expected<std::vector<std::uint32_t>, std::string> {
  std::vector<std::uint32_t> result;
  const auto parsed = for_each_csv_token(
      text,
      [&result](std::string_view token) -> std::expected<void, std::string> {
        std::uint32_t value{};
        const auto [end, error] =
            std::from_chars(token.data(), token.data() + token.size(), value);
        if (error != std::errc{} || end != token.data() + token.size() ||
            value == 0) {
          return std::unexpected{std::format(
              "invalid sweep FPS '{}': expected a positive 32-bit integer",
              token)};
        }
        if (std::ranges::find(result, value) != result.end()) {
          return std::unexpected{
              std::format("duplicate sweep FPS target {}", value)};
        }
        result.push_back(value);
        return {};
      });
  if (!parsed) return std::unexpected{parsed.error()};
  return result;
}

auto assess_cadence(const BenchmarkSummary& summary,
                    std::uint32_t target_fps) noexcept -> CadenceAssessment {
  CadenceAssessment result;
  result.target_fps = target_fps;
  if (target_fps == 0) return result;
  result.deadline_budget_ms = 1000.0 / static_cast<double>(target_fps);
  result.renderer_p95_headroom_ms =
      result.deadline_budget_ms - summary.render_p95_ms;
  result.frame_work_p95_headroom_ms =
      result.deadline_budget_ms - summary.work_p95_ms;
  result.required_mebibytes_per_second = summary.bytes_per_frame *
                                         static_cast<double>(target_fps) /
                                         (1024.0 * 1024.0);
  return result;
}

auto sweep_table(const std::vector<BenchmarkMeasurement>& measurements,
                 const std::vector<std::uint32_t>& target_fps) -> std::string {
  std::string output =
      "PROFILE    VIEWPORT    FPS  BUDGET  RENDER AVG/P95  FRAME AVG/P95  "
      "KiB/FRAME  TARGET MiB/s  FRAME P95 HEADROOM\n";
  for (const auto& measurement : measurements) {
    for (const auto fps : target_fps) {
      const auto cadence = assess_cadence(measurement.summary, fps);
      output += std::format(
          "{:<10} {:>4}x{:<4} {:>4} {:>7.2f}  {:>6.2f}/{:<6.2f}  "
          "{:>6.2f}/{:<6.2f}  {:>9.1f}  {:>12.2f}  {:>12.2f}\n",
          profile_name(measurement.configuration),
          measurement.configuration.viewport.width,
          measurement.configuration.viewport.height, fps,
          cadence.deadline_budget_ms, measurement.summary.render_avg_ms,
          measurement.summary.render_p95_ms, measurement.summary.work_avg_ms,
          measurement.summary.work_p95_ms,
          measurement.summary.bytes_per_frame / 1024.0,
          cadence.required_mebibytes_per_second,
          cadence.frame_work_p95_headroom_ms);
    }
  }
  return output;
}

auto sweep_json(const std::vector<BenchmarkMeasurement>& measurements,
                const std::vector<std::uint32_t>& target_fps,
                std::uint32_t seed, std::size_t frames_per_viewport,
                BenchmarkWorkload workload) -> std::string {
  std::string output =
      std::format("{{\n"
                  "  \"schema_version\": 2,\n"
                  "  \"workload\": \"{}\",\n"
                  "  \"seed\": {},\n"
                  "  \"frames_per_viewport\": {},\n"
                  "  \"measurements\": [\n",
                  workload_identifier(workload), seed, frames_per_viewport);

  for (std::size_t index = 0; index < measurements.size(); ++index) {
    const auto& measurement = measurements[index];
    output += std::format("    {{\n"
                          "      \"render_profile\": \"{}\",\n"
                          "      \"viewport_width\": {},\n"
                          "      \"viewport_height\": {},\n"
                          "      \"summary\": {{\n",
                          profile_name(measurement.configuration),
                          measurement.configuration.viewport.width,
                          measurement.configuration.viewport.height);
    append_summary_json(output, measurement.summary);
    output += "      },\n      \"targets\": [\n";
    for (std::size_t target_index = 0; target_index < target_fps.size();
         ++target_index) {
      const auto cadence =
          assess_cadence(measurement.summary, target_fps[target_index]);
      output += std::format(
          "        {{\"target_fps\": {}, \"deadline_budget_ms\": {:.6f}, "
          "\"renderer_p95_headroom_ms\": {:.6f}, "
          "\"frame_work_p95_headroom_ms\": {:.6f}, "
          "\"required_mebibytes_per_second\": {:.6f}}}{}\n",
          cadence.target_fps, cadence.deadline_budget_ms,
          cadence.renderer_p95_headroom_ms, cadence.frame_work_p95_headroom_ms,
          cadence.required_mebibytes_per_second,
          target_index + 1 == target_fps.size() ? "" : ",");
    }
    output += "      ]\n    }";
    output += index + 1 == measurements.size() ? "\n" : ",\n";
  }
  output += "  ]\n}\n";
  return output;
}

} // namespace apsis_drift
