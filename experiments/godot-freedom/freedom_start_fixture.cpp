#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>

#include "apsis_drift/save_file.hpp"

namespace {

[[nodiscard]] auto decimal(std::string_view text)
    -> std::optional<std::uint64_t> {
  if (text.empty() || text.size() > 20 ||
      (text.size() > 1 && text.front() == '0'))
    return std::nullopt;
  std::uint64_t value{};
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    return std::nullopt;
  return value;
}

} // namespace

auto main(int argc, char** argv) -> int {
  using namespace apsis_drift;
  if (argc != 5) {
    std::cerr << "usage: freedom-start-fixture ABSOLUTE_PATH SEED TICK "
                 "freedom|career\n";
    return 2;
  }
  const auto path = std::filesystem::path{argv[1]};
  const auto seed = decimal(argv[2]);
  const auto tick = decimal(argv[3]);
  const std::string_view mode{argv[4]};
  if (!path.is_absolute() || !seed || !tick ||
      *tick == std::numeric_limits<SimulationTick>::max()) {
    std::cerr << "invalid fixture path, seed or tick\n";
    return 2;
  }
  if (mode == "freedom") {
    auto save = make_freedom_new_game_document(Seed{*seed});
    save.state.tick = *tick;
    if (*tick >= 9) {
      save.state.discoveries.push_back({SurfaceSignalId{77}, 8});
      save.state.world_deltas.push_back(
          {"signal:77", SaveWorldDeltaKind::discovered, 9});
    }
    const auto written = write_freedom_save_file_atomically(path, save);
    if (!written) {
      std::cerr << save_file_error_message(written.error()) << '\n';
      return 1;
    }
  } else if (mode == "career" && *tick == 0) {
    const auto written =
        write_save_file_atomically(path, make_new_game_document(Seed{*seed}));
    if (!written) {
      std::cerr << save_file_error_message(written.error()) << '\n';
      return 1;
    }
  } else {
    std::cerr << "invalid fixture mode or career tick\n";
    return 2;
  }
  return 0;
}
