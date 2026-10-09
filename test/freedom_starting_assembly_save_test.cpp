#include "apsis_drift/freedom_starting_assembly_save.hpp"
#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/origin_lower_cockpit_contact.hpp"

#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
std::size_t checks{};
auto check(bool condition, std::string_view label) -> void {
  ++checks;
  if (!condition) throw std::runtime_error(std::string(label));
}
template <class T, class E> auto require(std::expected<T, E> value) -> T {
  if (!value)
    throw std::runtime_error("Starting assembly prerequisite refused");
  return std::move(*value);
}
template <class E> auto require(std::expected<void, E> value) -> void {
  if (!value)
    throw std::runtime_error("Starting assembly prerequisite refused");
}
auto contents(const std::filesystem::path& path) -> std::string {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Missing temporary save fixture");
  return {std::istreambuf_iterator<char>(input), {}};
}
auto put(const std::filesystem::path& path, std::string_view text) -> void {
  std::ofstream output(path, std::ios::binary);
  output << text;
  if (!output) throw std::runtime_error("Temporary save write failed");
}
struct TemporaryDirectory {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("apsis-starting-assembly-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  TemporaryDirectory() { std::filesystem::create_directory(path); }
  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
  TemporaryDirectory(const TemporaryDirectory&) = delete;
  auto operator=(const TemporaryDirectory&) -> TemporaryDirectory& = delete;
};
auto journey(const NativeFreedomFlightSession& session)
    -> FreedomJourneySaveDocument {
  check(session.docking().has_value() && session.walker().has_value(),
        "Supported station actor and docking retained");
  return {{session.document(), *session.docking()}, *session.walker()};
}
auto state_bits(const RigidBodyState& state) -> std::array<std::uint64_t, 13> {
  const std::array<double, 13> values{
      state.position_metres.x,
      state.position_metres.y,
      state.position_metres.z,
      state.orientation.w,
      state.orientation.x,
      state.orientation.y,
      state.orientation.z,
      state.linear_velocity_metres_per_second.x,
      state.linear_velocity_metres_per_second.y,
      state.linear_velocity_metres_per_second.z,
      state.angular_velocity_radians_per_second.x,
      state.angular_velocity_radians_per_second.y,
      state.angular_velocity_radians_per_second.z};
  std::array<std::uint64_t, 13> bits{};
  for (std::size_t i = 0; i < bits.size(); ++i)
    bits[i] = std::bit_cast<std::uint64_t>(values[i]);
  return bits;
}
auto legacy_versions(const std::filesystem::path& directory) -> void {
  const Seed seed{42};
  const auto old = require(make_freedom_journey_new_game_document(seed));
  const auto career = make_new_game_document(seed);
  const auto career_path = directory / "legacy16.json";
  const auto station_path = directory / "legacy17.json";
  const auto flight_path = directory / "legacy18.json";
  const auto docking_path = directory / "legacy19.json";
  const auto journey_path = directory / "legacy20.json";
  require(write_save_file_atomically(career_path, career));
  require(write_freedom_save_file_atomically(station_path,
                                             old.voyage.flight.origin));
  require(write_freedom_flight_file_atomically(flight_path, old.voyage.flight));
  require(write_freedom_docking_file_atomically(docking_path, old.voyage));
  require(write_freedom_journey_file_atomically(journey_path, old));
  check(std::get<SaveDocument>(
            require(native_continue(career_path)).document) == career,
        "Legacy16 career remains career, not inferred equipment or actor");
  check(std::get<FreedomSaveDocument>(
            require(native_continue(station_path)).document) ==
            old.voyage.flight.origin,
        "Legacy17 station retains its original owner and history");
  check(!NativeFreedomFlightSession::open(
            require(native_continue(career_path))) &&
            !NativeFreedomFlightSession::open(
                require(native_continue(station_path))),
        "Historical career/station do not invent a flight session or actor");
  const std::array<std::filesystem::path, 3> supported{
      flight_path, docking_path, journey_path};
  for (std::size_t i = 0; i < supported.size(); ++i) {
    const auto before = contents(supported[i]);
    const auto selection = require(native_continue(supported[i]));
    auto session = require(NativeFreedomFlightSession::open(selection));
    check(session.document() == old.voyage.flight &&
              session.source_save() == supported[i] &&
              session.walker().has_value() == (i == 2) &&
              session.docking().has_value() == (i != 0),
          "Legacy18/19/20 retained fields without adding actor/docking");
    check(!session.starting_assembly() &&
              session.craft_binding().selection() == nullptr &&
              session.craft_binding().pose() == nullptr &&
              session.craft_binding().contact() == nullptr,
          "Legacy hardware/contact selection remains unknown, never inferred");
    const auto destination =
        directory / ("legacy-copy-" + std::to_string(18 + i) + ".json");
    require(session.save_as(destination));
    check(contents(destination) == before &&
              Json::parse(contents(destination))["format_version"] == 18 + i &&
              session.source_save() == supported[i] &&
              contents(supported[i]) == before,
          "Legacy SaveAs keeps exact old version bytes and original source");
  }
  auto released = old.voyage;
  released.docking.attached = false;
  const auto released_path = directory / "legacy19-released.json";
  require(write_freedom_docking_file_atomically(released_path, released));
  const auto released_session = require(NativeFreedomFlightSession::open(
      require(native_continue(released_path))));
  check(released_session.docking() && !released_session.docking()->attached &&
            !released_session.walker(),
        "Legacy released docking is not inferred parked equipment or actor");
}
auto fixed_selection(const FreedomStartingAssemblySaveDocument& document)
    -> void {
  const auto encoded =
      require(encode_freedom_starting_assembly_document_json(document));
  const auto root = Json::parse(encoded);
  check(root["format_version"] == 21 && root.contains("starting_assembly"),
        "New explicit version21 selection serialized");
  for (const auto pin :
       {"d286dfd5174940ccd7e3db0cb023d3571ad460c6b22609c6f81208e16f06e2fc",
        "62b4d493f2d74f59b81fd089873360b99e3bae9d17a6d90a00066e9440e44c4a",
        "f98c50f69d71ecd38ca1d43d010f7b43ca300178dc25d40e170fd45fe5bc88a6"})
    check(root["starting_assembly"].dump().find(pin) != std::string::npos,
          "Saved selection includes independently known model/contact/frame");
  check(require(decode_freedom_starting_assembly_document_json(encoded)) ==
            document,
        "Version21 complete document roundtrips");
}
auto fresh_starters(const std::filesystem::path& directory) -> void {
  for (const Seed seed :
       {Seed{0}, Seed{42}, Seed{std::numeric_limits<std::uint64_t>::max()}}) {
    const auto selection = require(native_new_game(seed));
    check(selection.mode == NativeStartup::Mode::freedom &&
              !selection.source_save &&
              std::holds_alternative<FreedomKnowledgeSaveDocument>(
                  selection.document),
          "Ordinary New Game explicitly selects25 without a source save");
    const auto document = std::get<FreedomStartingAssemblySaveDocument>(
        std::get<FreedomKnowledgeSaveDocument>(selection.document)
            .voyage.voyage.base);
    const auto old = require(make_freedom_journey_new_game_document(seed));
    check(document.journey == old &&
              require(encode_freedom_journey_document_json(document.journey)) ==
                  require(encode_freedom_journey_document_json(old)) &&
              document.journey.voyage.flight.origin.recipe.universe_seed ==
                  seed,
          "Selected assembly changes no seed, generation, body, actor or "
          "history");
    fixed_selection(document);
    auto session = require(NativeFreedomFlightSession::open(selection));
    const auto before = journey(session);
    const auto body_bits = state_bits(session.document().flight);
    const auto observation = require(session.observe());
    check(
        before == old && session.starting_assembly() &&
            *session.starting_assembly() == document.starting_assembly &&
            session.craft_binding().selection() != nullptr &&
            session.craft_binding().contact() != nullptr &&
            session.craft_binding().pose() != nullptr,
        "Session owns exact immutable selected binding and effective contact");
    check(session.starting_assembly()->version == 1 &&
              session.starting_assembly()->preset == "wayfarer-stowed-01" &&
              session.starting_assembly()->operating_model_sha256 ==
                  "a9a8104a0ea8b5c22e4149861a76b5ab3911a9f77ba871b446c08bf9ed56"
                  "621c" &&
              session.starting_assembly()->hardware ==
                  OperatingProgress{1, 1, 1, 0} &&
              session.craft_binding().contact()->replacement_objects().size() ==
                  13 &&
              session.craft_binding()
                      .contact()
                      ->stowed_partition()
                      ->removed()
                      .size() == 8,
          "Selected fixed hardware and catalog removal are application owned");
    check(require(session.observe()) == observation &&
              journey(session) == before &&
              state_bits(session.document().flight) == body_bits &&
              session.document().flight.tick == 0,
          "Opening and projecting candidate leaves it unstepped");
    const auto path = directory / (std::to_string(seed.value) + "-new21.json");
    require(session.save_as(path));
    const auto raw = contents(path);
    check(Json::parse(raw)["format_version"] == 25 &&
              std::get<FreedomStartingAssemblySaveDocument>(
                  std::get<FreedomKnowledgeSaveDocument>(
                      require(load_native_save_file(path)))
                      .voyage.voyage.base) == document &&
              !session.source_save(),
          "New session SaveAs persists25 and exact nested21 without claiming a "
          "source");
    const auto continued = require(native_continue(path));
    auto reopened = require(NativeFreedomFlightSession::open(continued));
    check(journey(reopened) == before &&
              reopened.starting_assembly() == session.starting_assembly() &&
              reopened.source_save() == path && contents(path) == raw,
          "Continue retains selection, original owner and untouched source");
  }
}
auto binding_lifetime() -> void {
  NativeCraftBinding sharing;
  {
    auto session = require(
        NativeFreedomFlightSession::open(require(native_new_game(Seed{42}))));
    sharing = session.craft_binding();
  }
  check(sharing.selection() && sharing.pose() && sharing.contact(),
        "Binding outlives originating session through shared immutable "
        "ownership");
  const auto objects = sharing.contact()->replacement_objects();
  auto moved = std::move(sharing);
  check(moved.contact()->replacement_objects().data() == objects.data() &&
            objects.size() == 13 &&
            objects[0].source_object == "Shoulder restraint",
        "Binding move retains contact inventory/name view lifetime");
  // NOLINTBEGIN(bugprone-use-after-move) -- shared immutable binding exposes
  // documented empty accessors after moving its only shared_ptr member.
  check(!sharing.selection() && !sharing.pose() && !sharing.contact(),
        "Moved-from binding cannot expose a partial selected model");
  // NOLINTEND(bugprone-use-after-move) -- end documented moved-from accessors
}
auto bounded_continuation(const std::filesystem::path& directory) -> void {
  auto document =
      require(make_freedom_starting_assembly_new_game_document(Seed{42}));
  document.journey.voyage.flight.origin.state.discoveries.push_back({{77}, 0});
  document.journey.voyage.flight.origin.state.world_deltas.push_back(
      {"signal:77", SaveWorldDeltaKind::discovered, 0});
  document.journey.voyage.flight.model.assistance = false;
  const auto path = directory / "history-source21.json";
  require(write_freedom_starting_assembly_file_atomically(path, document));
  const auto source = contents(path);
  auto selected = require(native_continue(path));
  auto current = require(NativeFreedomFlightSession::open(selected));
  auto direct = current;
  auto legacy_selection = selected;
  legacy_selection.document = document.journey;
  auto legacy = require(NativeFreedomFlightSession::open(legacy_selection));
  const auto original_binding = current.starting_assembly();
  for (int n = 0; n < 12; ++n) {
    const OriginWalkControls controls{n < 6 ? 0.25 : 0.0, n < 6 ? 0.0 : 0.25,
                                      0.1};
    require(current.advance_walk(controls));
    require(direct.advance_walk(controls));
    require(legacy.advance_walk(controls));
    check(
        journey(current) == journey(direct) &&
            journey(current) == journey(legacy) &&
            state_bits(current.document().flight) ==
                state_bits(direct.document().flight) &&
            require(current.observe()) == require(direct.observe()) &&
            current.starting_assembly() == original_binding,
        "Selected/reopened/legacy nested owner has exact bounded continuation");
    if (n == 5) {
      const auto copy = directory / "history-copy21.json";
      const auto before_save = journey(current);
      require(current.save_as(copy));
      check(current.source_save() == path && contents(path) == source,
            "SaveAs leaves selected source bytes/path unchanged");
      current = require(
          NativeFreedomFlightSession::open(require(native_continue(copy))));
      check(journey(current) == before_save &&
                current.starting_assembly() == original_binding,
            "Reopened21 preserves progressed shared clock and history");
    }
  }
  check(current.document().origin.state.discoveries ==
                document.journey.voyage.flight.origin.state.discoveries &&
            current.document().origin.state.world_deltas ==
                document.journey.voyage.flight.origin.state.world_deltas &&
            !current.document().model.assistance &&
            current.document().flight.tick == 12 && contents(path) == source,
        "No reset or invented mutable history, clock or assistance");
  const auto before = journey(current);
  check(!current.advance({}) && !current.release_port() &&
            !current.capture_port() && !current.select_port(2) &&
            !current.set_assistance(true) && !current.set_hold({}) &&
            journey(current) == before &&
            current.starting_assembly() == original_binding,
        "Parked starter does not infer seating or authorize craft controls");
}
auto malformed_saves(const std::filesystem::path& directory) -> void {
  const auto good =
      require(make_freedom_starting_assembly_new_game_document(Seed{42}));
  const auto text =
      require(encode_freedom_starting_assembly_document_json(good));
  const auto root = Json::parse(text);
  const auto bad_path = directory / "malformed21.json";
  const auto sentinel_path = directory / "unchanged-destination.json";
  put(sentinel_path, "preserved destination bytes\n");
  auto deny = [&](std::string_view bytes) {
    put(bad_path, bytes);
    const auto decoded = decode_freedom_starting_assembly_document_json(bytes);
    const auto loaded = native_continue(bad_path);
    check(!decoded && !loaded && !loaded.error().empty() &&
              contents(bad_path) == bytes &&
              contents(sentinel_path) == "preserved destination bytes\n",
          "Corrupt21 refuses without legacy fallback or any source rewrite");
  };
  auto mutate = [&](const Json& value) { deny(value.dump()); };
  for (const auto& version :
       {Json(true), Json(21.0), Json("21"), Json(22), Json(0)}) {
    auto bad = root;
    bad["format_version"] = version;
    mutate(bad);
  }
  for (const auto& substitute :
       {Json(nullptr), Json(true), Json::array(), Json("legacy")}) {
    auto bad = root;
    bad["starting_assembly"] = substitute;
    mutate(bad);
  }
  auto bad = root;
  bad.erase("starting_assembly");
  mutate(bad);
  bad = root;
  bad["unexpected"] = false;
  mutate(bad);
  bad = root;
  bad["starting_assembly"]["unexpected"] = true;
  mutate(bad);
  std::size_t digest_fields{};
  for (auto it = root["starting_assembly"].begin();
       it != root["starting_assembly"].end(); ++it) {
    if (!it.value().is_string()) continue;
    const auto value = it.value().get<std::string>();
    if (value.size() != 64) continue;
    ++digest_fields;
    for (const auto& replacement :
         {Json(std::string(64, '0')), Json("abc"), Json(true)}) {
      bad = root;
      bad["starting_assembly"][it.key()] = replacement;
      mutate(bad);
    }
  }
  check(digest_fields >= 3, "All saved independent digest fields exercised");
  bad = root;
  bad["journey"]["state"]["location"] = "planetary_flight";
  mutate(bad);
  bad = root;
  bad["journey"].erase("actor");
  mutate(bad);
  bad = root;
  bad["journey"]["docking"]["attached"] = false;
  mutate(bad);
  bad = root;
  bad["journey"]["actor"]["actor_id"] = "0";
  mutate(bad);
  auto duplicate = text;
  const auto position = duplicate.find("\"starting_assembly\":");
  check(position != std::string::npos, "Selection fixture member present");
  duplicate.insert(position, "\"starting_assembly\": {},");
  deny(duplicate);
  deny("{broken json");
  deny(std::string(65, '[') + "0" + std::string(65, ']'));
  const auto at_cap =
      text + std::string(kMaximumSaveDocumentBytes - text.size(), ' ');
  check(require(decode_freedom_starting_assembly_document_json(at_cap)) == good,
        "Exact save byte boundary accepts genuine bounded document");
  deny(at_cap + " ");
  const auto source_path = directory / "valid-source21.json";
  require(write_freedom_starting_assembly_file_atomically(source_path, good));
  const auto original = contents(source_path);
  auto session = require(
      NativeFreedomFlightSession::open(require(native_continue(source_path))));
  const auto before = journey(session);
  for (const auto& path :
       {std::filesystem::path{}, std::filesystem::path{"relative.json"},
        directory, directory / "missing-parent" / "save.json"}) {
    check(!session.save_as(path) && journey(session) == before &&
              session.source_save() == source_path &&
              contents(source_path) == original,
          "Invalid SaveAs destination leaves session and source unchanged");
  }
  for (const auto& entry : std::filesystem::directory_iterator(directory))
    check(entry.path().filename().string().find(".tmp.") == std::string::npos,
          "Failed saves leave no temporary residue");
}
auto invalid_selections(const std::filesystem::path& directory) -> void {
  const auto good =
      require(make_freedom_starting_assembly_new_game_document(Seed{42}));
  const auto good_path = directory / "good-for-refusal21.json";
  const auto destination = directory / "existing-for-refusal21.json";
  require(write_freedom_starting_assembly_file_atomically(good_path, good));
  require(write_freedom_starting_assembly_file_atomically(destination, good));
  const auto original = contents(good_path),
             original_destination = contents(destination);
  const auto selected = require(native_continue(good_path));
  const auto stable = require(NativeFreedomFlightSession::open(selected));
  auto denied = [&](const FreedomStartingAssemblySaveDocument& document) {
    auto candidate = selected;
    candidate.document = document;
    check(!validate_freedom_starting_assembly_document(document) &&
              !encode_freedom_starting_assembly_document_json(document) &&
              !NativeFreedomFlightSession::open(candidate) &&
              !prepare_native_freedom_station_start(candidate) &&
              !write_freedom_starting_assembly_file_atomically(destination,
                                                               document) &&
              !native_save_freedom(candidate, destination) &&
              contents(destination) == original_destination &&
              contents(good_path) == original &&
              journey(stable) == good.journey,
          "Invalid selection fails across save/start/session without mutation");
  };
  auto bad = good;
  bad.starting_assembly.version = 2;
  denied(bad);
  bad = good;
  bad.starting_assembly.preset = "legacy";
  denied(bad);
  for (std::string NativeStartingAssemblySelection::* member :
       {&NativeStartingAssemblySelection::operating_model_sha256,
        &NativeStartingAssemblySelection::stowed_model_sha256,
        &NativeStartingAssemblySelection::frame_sha256,
        &NativeStartingAssemblySelection::contact_sha256}) {
    bad = good;
    bad.starting_assembly.*member = std::string(64, '0');
    denied(bad);
  }
  for (double OperatingProgress::* member :
       {&OperatingProgress::roof_transfer, &OperatingProgress::inner_door,
        &OperatingProgress::seat_boarding,
        &OperatingProgress::station_closure}) {
    for (const double value :
         {0.5, -1.0, 2.0, std::numeric_limits<double>::quiet_NaN(),
          std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity()}) {
      bad = good;
      bad.starting_assembly.hardware.*member = value;
      denied(bad);
    }
  }
  for (const double value : {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()}) {
    bad = good;
    bad.journey.actor.heading_radians = value;
    denied(bad);
    bad = good;
    bad.journey.voyage.flight.flight.position_metres.x = value;
    denied(bad);
  }
  bad = good;
  bad.journey.voyage.docking.attached = false;
  denied(bad);
  bad = good;
  bad.journey.voyage.docking.target.ordinal = 2;
  denied(bad);
  bad = good;
  bad.journey.actor.actor_id = 0;
  denied(bad);
  auto wrong_mode = selected;
  wrong_mode.mode = NativeStartup::Mode::legacy_career;
  check(!NativeFreedomFlightSession::open(wrong_mode) &&
            !prepare_native_freedom_station_start(wrong_mode),
        "Mode cannot relabel selected parked assembly");
  NativeStartup wrong_home{selected.mode, selected.document,
                           generate_planet_descriptor(Seed{99}),
                           selected.source_save};
  check(!NativeFreedomFlightSession::open(wrong_home) &&
            !prepare_native_freedom_station_start(wrong_home) &&
            contents(good_path) == original,
        "Foreign home descriptor cannot select another universe");
  const auto root = Json::parse(
      require(encode_freedom_starting_assembly_document_json(good)));
  for (const auto& version : {Json(true), Json(1.0), Json("1"), Json(2)}) {
    auto altered = root;
    altered["starting_assembly"]["version"] = version;
    check(!decode_freedom_starting_assembly_document_json(altered.dump()),
          "Selection version is a true fixed unsigned integer");
  }
  for (const auto channel :
       {"roof_transfer", "inner_door", "seat_boarding", "station_closure"}) {
    for (const auto& value :
         {Json(true), Json("1"), Json(nullptr), Json(0.5)}) {
      auto altered = root;
      altered["starting_assembly"]["hardware"][channel] = value;
      check(!decode_freedom_starting_assembly_document_json(altered.dump()),
            "Hardware channel requires supported numeric value, never bool");
    }
    auto altered = root;
    altered["starting_assembly"]["hardware"].erase(channel);
    check(!decode_freedom_starting_assembly_document_json(altered.dump()),
          "All four hardware channels required");
  }
  for (const auto field :
       {"preset", "version", "hardware", "operating_model_sha256",
        "stowed_model_sha256", "frame_sha256", "contact_sha256"}) {
    auto altered = root;
    altered["starting_assembly"].erase(field);
    check(!decode_freedom_starting_assembly_document_json(altered.dump()),
          "No missing starting-assembly field defaults into approval");
  }
  auto altered = root;
  altered["starting_assembly"]["hardware"]["extra"] = 0;
  check(!decode_freedom_starting_assembly_document_json(altered.dump()),
        "Hardware contract is closed");
}
} // namespace
int main() {
  try {
    const TemporaryDirectory directory;
    legacy_versions(directory.path);
    fresh_starters(directory.path);
    binding_lifetime();
    bounded_continuation(directory.path);
    malformed_saves(directory.path);
    invalid_selections(directory.path);
    std::cout << "Starting assembly save/session contract: " << checks
              << " checks passed\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
