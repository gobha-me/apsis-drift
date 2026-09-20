#include "snapshot.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string_view>

template <typename T> auto number(std::string_view text) -> T {
  T value{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size())
    throw std::invalid_argument("invalid numeric argument");
  return value;
}

auto main(int argc, char** argv) -> int {
  try {
    if (argc < 2) {
      std::cerr << "usage: apsis-drift-godot-snapshot OUTPUT.json [planet-seed "
                   "[samples [span-metres [experimental-relief-version]]]]\n"
                   "   or: apsis-drift-godot-snapshot OUTPUT.json "
                   "--physical-origin=SEED "
                   "[--samples=N --span-metres=M --relief-version=V "
                   "--latitude=RADIANS --longitude=RADIANS]\n";
      return 2;
    }
    apsis_drift::godot_spike::Request request;
    if (argc > 2 &&
        std::string_view{argv[2]}.starts_with("--physical-origin=")) {
      if (argc > 8)
        throw std::invalid_argument("too many physical origin options");
      std::set<std::string_view> seen;
      for (int i = 2; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        const auto equal = argument.find('=');
        if (equal == std::string_view::npos)
          throw std::invalid_argument(
              "physical origin options require --name=value");
        const auto key = argument.substr(0, equal);
        const auto value = argument.substr(equal + 1);
        if (!seen.insert(key).second)
          throw std::invalid_argument("duplicate physical origin option");
        if (key == "--physical-origin")
          request.physical_origin_seed =
              apsis_drift::Seed{number<std::uint64_t>(value)};
        else if (key == "--samples")
          request.samples = number<unsigned>(value);
        else if (key == "--span-metres")
          request.span_metres = number<double>(value);
        else if (key == "--relief-version")
          request.relief_version = number<unsigned>(value);
        else if (key == "--latitude")
          request.latitude = number<double>(value);
        else if (key == "--longitude")
          request.longitude = number<double>(value);
        else
          throw std::invalid_argument("unknown physical origin option");
      }
    } else {
      if (argc > 6)
        throw std::invalid_argument("too many standalone snapshot arguments");
      if (argc > 2) request.planet_seed.value = number<std::uint64_t>(argv[2]);
      if (argc > 3) request.samples = number<unsigned>(argv[3]);
      if (argc > 4) request.span_metres = number<double>(argv[4]);
      if (argc > 5) request.relief_version = number<unsigned>(argv[5]);
    }
    apsis_drift::godot_spike::validate(request);
    if (std::filesystem::exists(argv[1]))
      throw std::runtime_error(
          "output already exists; choose a new snapshot path");
    const auto data = apsis_drift::godot_spike::snapshot(request);
    std::ofstream output{argv[1], std::ios::binary};
    output << data.dump() << '\n';
    output.close();
    if (!output) throw std::runtime_error("could not write snapshot");
    std::cout << "Exported " << request.samples * request.samples
              << " vertices; " << data.at("tiles_touched")
              << " C++ tiles; elevation " << data.at("elevation_min_metres")
              << ".." << data.at("elevation_max_metres") << " m; "
              << data.at("replay").size() << " replay poses\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
