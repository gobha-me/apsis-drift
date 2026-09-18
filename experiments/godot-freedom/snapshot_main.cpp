#include "snapshot.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

template <typename T>
auto number(std::string_view text) -> T {
  T value{};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size())
    throw std::invalid_argument("invalid numeric argument");
  return value;
}

auto main(int argc, char** argv) -> int {
  try {
    if (argc < 2 || argc > 6) {
      std::cerr << "usage: apsis-drift-godot-snapshot OUTPUT.json [planet-seed [samples [span-metres [experimental-relief-version]]]]\n";
      return 2;
    }
    apsis_drift::godot_spike::Request request;
    if (argc > 2) request.planet_seed.value = number<std::uint64_t>(argv[2]);
    if (argc > 3) request.samples = number<unsigned>(argv[3]);
    if (argc > 4) request.span_metres = number<double>(argv[4]);
    if (argc > 5) request.relief_version = number<unsigned>(argv[5]);
    apsis_drift::godot_spike::validate(request);
    if (std::filesystem::exists(argv[1]))
      throw std::runtime_error("output already exists; choose a new snapshot path");
    const auto data = apsis_drift::godot_spike::snapshot(request);
    std::ofstream output{argv[1], std::ios::binary};
    output << data.dump() << '\n';
    output.close();
    if (!output) throw std::runtime_error("could not write snapshot");
    std::cout << "Exported " << request.samples * request.samples << " vertices; "
              << data.at("tiles_touched") << " C++ tiles; elevation "
              << data.at("elevation_min_metres") << ".."
              << data.at("elevation_max_metres") << " m; "
              << data.at("replay").size() << " replay poses\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
