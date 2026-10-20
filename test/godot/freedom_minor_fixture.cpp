#include "../native_local_route_reuse_fixture.hpp"
#include "../native_minor_route_fixture.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {
using namespace apsis_drift;
using namespace apsis_drift::minor_route_fixture;
using Json = nlohmann::ordered_json;
auto phase(const NativeFreedomFlightSession& s,
           const std::filesystem::path& output, const char* name, Json& report)
    -> void {
  need(s.save_as(output / (std::string{name} + ".json")));
  ensure(bytes(continued(s)) == bytes(s), "Export phase changed on Continue");
  report["phases"][name] = {
      {"tick", std::to_string(s.document().flight.tick)},
      {"flight_quanta", std::to_string(s.resources()->flight_quanta)},
      {"jump_charges", s.resources()->jump_charges}};
  std::ofstream stream{output / "report.json"};
  stream << report.dump(2) << '\n';
  if (!stream) throw std::runtime_error("Minor phase report write failed");
  std::cout << "phase=" << name << " tick=" << s.document().flight.tick << '\n';
}
auto segment(const char* from, const char* to, unsigned ticks, bool inward)
    -> Json {
  return {{"from", from},
          {"to", to},
          {"ticks", ticks},
          {"positive_translation", Json::array({0.0, inward ? 0.0 : 1.0, 0.0})},
          {"negative_translation", Json::array({0.0, inward ? 1.0 : 0.0, 0.0})},
          {"positive_rotation", Json::array({0.0, 0.0, 0.0})},
          {"negative_rotation", Json::array({0.0, 0.0, 0.0})}};
}
} // namespace
int main(int argc, char** argv) {
  try {
    if (argc != 2)
      throw std::runtime_error("Expected one absolute output directory");
    const std::filesystem::path output{argv[1]};
    if (!output.is_absolute() || output.native().size() > 4000 ||
        output == output.root_path() || std::filesystem::is_symlink(output) ||
        (std::filesystem::exists(output) &&
         (!std::filesystem::is_directory(output) ||
          !std::filesystem::is_empty(output))))
      throw std::runtime_error(
          "Expected a bounded empty absolute output directory");
    std::filesystem::create_directories(output);
    Json report{{"version", 1},
                {"seed", "42"},
                {"phases", Json::object()},
                {"segments", Json::array()},
                {"approach_limited", false}};
    auto s = departure();
    need(s.select_local_jump(minor_target(s)));
    leg(s);
    phase(s, output, "arrival", report);
    const auto down = approach(s);
    report["segments"].push_back(segment("arrival", "approach", down, true));
    report["approach_limited"] = true;
    phase(s, output, "approach", report);
    const auto up = withdraw(s);
    report["segments"].push_back(segment("approach", "withdraw", up, false));
    phase(s, output, "withdraw", report);
    need(s.select_local_jump(home_target(s)));
    leg(s);
    phase(s, output, "home", report);
    std::ofstream stream{output / "report.json"};
    stream << report.dump(2) << '\n';
    if (!stream) throw std::runtime_error("Minor fixture report write failed");
    // The original four phases/report stay unchanged. This extra genuine
    // captured return lets the native case use the service UI and revisit the
    // Moon without duplicating the C++ test guidance in presentation code.
    const auto rendezvous_ticks = origin_route_reuse_fixture::capture_home(s);
    need(s.save_as(output / "reuse-captured.json"));
    std::cout << "reuse_capture_ticks=" << rendezvous_ticks << '\n';
    std::cout
        << "Actual station-to-minor approach/withdraw/home fixture passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
