#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/native_profile.hpp"
#include "apsis_drift/native_startup.hpp"

#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <source_location>
#include <sys/file.h>
#include <unistd.h>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
int failures{};
auto check(bool value, std::string_view message,
           std::source_location where = std::source_location::current())
    -> void {
  if (value) return;
  ++failures;
  std::cerr << "FAIL line " << where.line() << ": " << message << '\n';
}
template <typename T, typename E>
auto need(const std::expected<T, E>& value,
          std::source_location where = std::source_location::current()) -> T {
  if (value) return *value;
  std::cerr << "Fixture refused at line " << where.line();
  if constexpr (requires { value.error().detail; })
    std::cerr << ": " << value.error().detail;
  std::cerr << '\n';
  std::exit(1);
}
struct TemporaryDirectory {
  std::filesystem::path path;
  TemporaryDirectory() {
    auto pattern =
        (std::filesystem::temp_directory_path() / "apsis-native-profile-XXXXXX")
            .string();
    const auto* created = ::mkdtemp(pattern.data());
    if (!created) std::exit(1);
    path = created;
  }
  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};
auto read_bytes(const std::filesystem::path& path) -> std::string {
  std::ifstream input{path, std::ios::binary};
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}
auto write_bytes(const std::filesystem::path& path, std::string_view bytes)
    -> void {
  std::ofstream output{path, std::ios::binary | std::ios::trunc};
  output << bytes;
  check(output.good(), "Cannot write test fixture");
}
auto storage_contract(const NativeSaveDocument& document) -> void {
  TemporaryDirectory temporary;
  const auto directory = temporary.path / "profiles";
  const auto empty = scan_native_profile_catalog(directory);
  check(empty.writable && empty.entries.empty() && !empty.continue_index &&
            !std::filesystem::exists(directory),
        "Empty catalog scan created storage or selected a slot");
  const auto first = need(create_native_catalog_profile(directory, document));
  check(first.profile.header.id.value == 1 &&
            first.profile.header.save_sequence == 1 &&
            first.profile.document == document,
        "First slot identity/sequence/world differs");
  const auto first_bytes = read_bytes(first.path);
  const auto legacy = need(create_catalog_profile(
      directory,
      make_new_game_document(NewGameOptions{.universe_seed = Seed{42}})));
  check(legacy.metadata.id.value == 2 && legacy.metadata.save_sequence == 2,
        "Legacy allocator ignored native identity or sequence");
  const auto legacy_bytes = read_bytes(legacy.path);
  const auto second = need(create_native_catalog_profile(directory, document));
  check(second.profile.header.id.value == 3 &&
            second.profile.header.save_sequence == 3 &&
            read_bytes(first.path) == first_bytes &&
            read_bytes(legacy.path) == legacy_bytes,
        "Native Save As ignored legacy ordering or overwrote a slot");
  const auto scanned = scan_native_profile_catalog(directory);
  check(scanned.entries.size() == 3 && scanned.continue_index == 0 &&
            scanned.entries[0].header == second.profile.header &&
            !scanned.entries.back().activatable() &&
            !scanned.entries.back().diagnostic.empty(),
        "Mixed catalog failed ordering or reinterpreted legacy career");
  const auto loaded = need(load_native_catalog_profile(scanned.entries[0]));
  check(loaded.profile.document == document &&
            loaded.source_bytes == second.source_bytes,
        "Loaded profile differs from fully validated scan");
  const auto replaced = need(replace_native_catalog_profile(first, document));
  check(replaced.profile.header.id == first.profile.header.id &&
            replaced.profile.header.save_sequence == 4 &&
            read_bytes(first.path) == replaced.source_bytes &&
            read_bytes(second.path) == second.source_bytes &&
            read_bytes(legacy.path) == legacy_bytes,
        "Save replaced a different slot or failed to adopt durable ordering");
  check(!replace_native_catalog_profile(first, document),
        "Stale active source overwrote newer save");
  const auto saved_scan = scan_native_profile_catalog(directory);
  write_bytes(first.path, "corrupt");
  check(!load_native_catalog_profile(saved_scan.entries[0]),
        "Changed scan source loaded");
  check(!replace_native_catalog_profile(replaced, document) &&
            read_bytes(first.path) == "corrupt",
        "Failed Save changed conflicted source");
  write_bytes(first.path, replaced.source_bytes);
  auto forged = replaced;
  forged.path = second.path;
  check(!replace_native_catalog_profile(forged, document) &&
            read_bytes(second.path) == second.source_bytes,
        "Forged active identity overwrote another slot");
  const int lock =
      ::open((directory / ".profiles.lock").c_str(), O_RDWR | O_CLOEXEC);
  check(lock >= 0 && ::flock(lock, LOCK_EX | LOCK_NB) == 0,
        "Cannot lock test catalog");
  check(!create_native_catalog_profile(directory, document) &&
            !replace_native_catalog_profile(replaced, document),
        "Concurrent writer lock ignored");
  if (lock >= 0) ::close(lock);
  auto duplicate = second.profile;
  duplicate.header.id = ProfileId{4};
  const auto fourth = directory / "profile-0000000000000004.json";
  write_bytes(fourth, need(encode_native_profile_document_json(duplicate)));
  const auto ambiguous = scan_native_profile_catalog(directory);
  std::size_t selectable{};
  for (const auto& entry : ambiguous.entries)
    selectable += entry.activatable() ? 1U : 0U;
  check(selectable == 1 &&
            ambiguous.entries[0].header == replaced.profile.header,
        "Duplicate native sequences remained activatable");
  std::filesystem::remove(fourth);
  auto exhausted = second.profile;
  exhausted.header.save_sequence = std::numeric_limits<std::uint64_t>::max();
  write_bytes(second.path,
              need(encode_native_profile_document_json(exhausted)));
  const auto overflow = create_native_catalog_profile(directory, document);
  const auto save_overflow = replace_native_catalog_profile(replaced, document);
  check(!overflow &&
            overflow.error().code ==
                ProfileCatalogErrorCode::sequence_overflow &&
            !save_overflow &&
            save_overflow.error().code ==
                ProfileCatalogErrorCode::sequence_overflow &&
            read_bytes(first.path) == replaced.source_bytes,
        "Sequence overflow mutated storage");
  write_bytes(second.path, second.source_bytes);
  const auto alias = temporary.path / "alias";
  std::filesystem::create_directory_symlink(directory, alias);
  check(!scan_native_profile_catalog(alias).writable &&
            !scan_native_profile_catalog(alias / "absent").writable &&
            !create_native_catalog_profile(alias, document),
        "Catalog followed a final or ancestor directory link");
  std::filesystem::remove(first.path);
  std::filesystem::create_symlink(second.path, first.path);
  check(!load_native_catalog_profile(saved_scan.entries[0]) &&
            !replace_native_catalog_profile(replaced, document) &&
            !create_native_catalog_profile(directory, document) &&
            read_bytes(second.path) == second.source_bytes,
        "Catalog followed or overwrote a linked slot");
  std::filesystem::remove(first.path);
  write_bytes(first.path, replaced.source_bytes);
  for (std::uint64_t id = 4; id <= kMaximumLocalProfiles; ++id)
    write_bytes(directory / std::format("profile-{:016x}.json", id), "invalid");
  const auto full = scan_native_profile_catalog(directory);
  const auto refused = create_native_catalog_profile(directory, document);
  check(full.entries.size() == kMaximumLocalProfiles && !full.overflow &&
            !refused &&
            refused.error().code == ProfileCatalogErrorCode::catalog_full,
        "Invalid and legacy canonical slots did not share the 64-slot bound");
  const auto full_saved =
      need(replace_native_catalog_profile(replaced, document));
  check(full_saved.profile.header.save_sequence == 5,
        "Full catalog incorrectly blocked Save");
  write_bytes(directory / "profile-0000000000000041.json", "invalid");
  check(scan_native_profile_catalog(directory).overflow &&
            !create_native_catalog_profile(directory, document) &&
            !replace_native_catalog_profile(full_saved, document),
        "Overflow catalog offered an arbitrary writable subset");
  check(!scan_native_profile_catalog("relative").writable &&
            !create_native_catalog_profile("relative", document),
        "Relative storage path accepted");
}
auto session_contract(const NativeSaveDocument& document) -> void {
  TemporaryDirectory temporary;
  const auto directory = temporary.path / "profiles";
  NativeProfileSession session;
  check(need(session.dirty(document)) && !session.active() &&
            !session.save(directory, document, false) &&
            !std::filesystem::exists(directory),
        "Unslotted session silently saved or became clean");
  check(session.save(directory, document, true).has_value(),
        "Initial Save As refused");
  const auto first = *session.active();
  check(!need(session.dirty(document)) && first.profile.header.id.value == 1,
        "Successful Save As did not adopt clean slot");
  const NativeSaveDocument changed{make_freedom_new_game_document(Seed{43})};
  check(need(session.dirty(changed)), "Changed world remained clean");
  check(
      !session.save(temporary.path / "missing" / ".." / "bad", changed, true) &&
          session.active()->source_bytes == first.source_bytes &&
          need(session.dirty(changed)) &&
          !std::filesystem::exists(temporary.path / "missing"),
      "Failed Save As changed active metadata, dirty state or unsafe storage");
  check(session.save(directory, changed, true).has_value() &&
            session.active()->profile.header.id.value == 2 &&
            !need(session.dirty(changed)) &&
            read_bytes(first.path) == first.source_bytes,
        "Second Save As changed former slot or failed adoption");
  const auto second = *session.active();
  write_bytes(second.path, "changed by another writer");
  check(!session.save(directory, document, false) &&
            session.active()->source_bytes == second.source_bytes &&
            need(session.dirty(document)) &&
            read_bytes(second.path) == "changed by another writer",
        "Failed Save cleared dirty state or replaced conflicting bytes");
  write_bytes(second.path, second.source_bytes);
  check(session.save(directory, document, false).has_value() &&
            !need(session.dirty(document)) &&
            session.active()->profile.header.id.value == 2,
        "Successful Save changed slot or remained dirty");
  const auto resumed =
      need(NativeProfileSession::from_catalog(*session.active()));
  check(!need(resumed.dirty(document)) &&
            resumed.active()->source_bytes == session.active()->source_bytes,
        "Catalog admission lost clean/source metadata");
  auto false_source = *session.active();
  false_source.source_bytes = first.source_bytes;
  check(!NativeProfileSession::from_catalog(false_source),
        "Mismatched loaded source adopted");
  auto explicit_session =
      need(NativeProfileSession::from_explicit_path(document));
  check(explicit_session.explicit_path() &&
            !need(explicit_session.dirty(document)) &&
            need(explicit_session.dirty(changed)) &&
            !explicit_session.active() &&
            !explicit_session.save(directory, document, false) &&
            !explicit_session.save(directory, document, true),
        "Explicit-path session entered catalog persistence");
}
auto round_trip(const NativeSaveDocument& document,
                NativeProfileLocation location) -> void {
  const auto baseline = need(encode_native_save_document_json(document));
  for (const auto id :
       {std::uint64_t{1}, std::numeric_limits<std::uint64_t>::max()}) {
    for (const auto sequence :
         {std::uint64_t{1}, std::numeric_limits<std::uint64_t>::max()}) {
      auto profile =
          need(make_native_profile_document({id}, sequence, document));
      check(profile.header.summary.location == location,
            "Wrong actual phase summary (a selected suit without a ground "
            "actor is still indoors)");
      const auto encoded = need(encode_native_profile_document_json(profile));
      const auto decoded = need(decode_native_profile_document_json(encoded));
      check(decoded.header == profile.header && decoded.document == document,
            "Profile header/world did not round trip");
      check(need(encode_native_save_document_json(decoded.document)) ==
                baseline,
            "Metadata changed authoritative canonical bytes");
      check(need(encode_native_profile_document_json(decoded)) == encoded,
            "Profile canonical encoding drifted");
      profile.header.summary.tick ^= 1;
      check(!encode_native_profile_document_json(profile),
            "Forged tick encoded");
    }
  }
  check(!make_native_profile_document({0}, 1, document), "Zero ID accepted");
  check(!make_native_profile_document({1}, 0, document),
        "Zero sequence accepted");
}
auto supported_versions(const NativeStartup& selected) -> void {
  const auto& knowledge =
      std::get<FreedomKnowledgeSaveDocument>(selected.document);
  const auto& resources = knowledge.voyage;
  const auto& surface = resources.voyage;
  const auto& assembly =
      std::get<FreedomStartingAssemblySaveDocument>(surface.base);
  round_trip(resources, NativeProfileLocation::station_walk);
  round_trip(surface, NativeProfileLocation::station_walk);
  round_trip(assembly, NativeProfileLocation::station_walk);
  round_trip(assembly.journey, NativeProfileLocation::station_walk);
  round_trip(assembly.journey.voyage, NativeProfileLocation::attached);
  round_trip(assembly.journey.voyage.flight, NativeProfileLocation::flight);
  auto live = need(NativeFreedomFlightSession::open(selected));
  const auto source = need(live.recovery_document());
  round_trip(source, NativeProfileLocation::station_walk);
  round_trip(FreedomSurfaceWalkSaveDocument{source, 1, std::nullopt},
             NativeProfileLocation::station_walk);
  const auto checksum = need(freedom_recovery_source_checksum(source.voyage));
  check(live.record_loss(FreedomLossCause::irrecoverable_destruction,
                         live.document().origin.state.craft,
                         live.document().flight.tick, checksum)
            .has_value(),
        "Recorded-loss fixture refused");
  round_trip(need(live.recovery_document()),
             NativeProfileLocation::recorded_loss);
}
auto jump_phases(Seed seed) -> void {
  // The existing first-jump contract's valid physical spaceflight fixture,
  // with real session transitions rather than forged spool/transit fields.
  const auto system = need(generate_physical_origin_system(seed, 2));
  const auto& body = system.catalog.planets[0].descriptor;
  FreedomFlightSaveDocument flight;
  flight.origin = make_freedom_new_game_document(seed);
  flight.origin.state.tick = 240;
  flight.model.physical_catalog = 2;
  flight.model.physical_ephemeris = 2;
  flight.flight.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  flight.flight.frame = {
      RigidFrameKind::planet_relative_inertial, system.catalog.id, body.id, {}};
  flight.flight.tick = 240;
  flight.flight.position_metres = {0, 0, body.radius.value * 10000.0};
  flight.world = FreedomActiveWorldSelection{1, system.catalog.id, body.id};
  const FreedomResources resources{1, flight.origin.state.craft,
                                   flight.flight.craft, 240};
  const auto knowledge = need(
      make_freedom_starting_knowledge(seed, kFreedomTravelKnowledgeVersion));
  const FreedomKnowledgeSaveDocument mapped{{{flight, {}}, resources},
                                            knowledge};
  const FreedomTravelSaveDocument travel{
      mapped, {}, NativeStartingAssemblySelection{}, std::nullopt};
  round_trip(travel, NativeProfileLocation::flight);
  auto live = need(NativeFreedomFlightSession::open(
      {NativeStartup::Mode::freedom, travel, body, {}}));
  const auto ids = generate_first_intersystem_identities(seed);
  check(live.select_jump(ids.target_system).has_value() &&
            live.begin_jump().has_value(),
        "Actual jump spool fixture refused");
  round_trip(need(live.travel_document()), NativeProfileLocation::jump_spool);
  for (unsigned tick = 0;
       tick < 1200 && live.travel()->phase == FreedomJumpPhase::spool; ++tick)
    check(live.advance({}).has_value(), "Actual jump advancement refused");
  check(live.travel()->phase == FreedomJumpPhase::transit,
        "Bounded spool did not reach actual commitment");
  round_trip(need(live.travel_document()), NativeProfileLocation::jump_transit);
}
auto malformed(const NativeSaveDocument& document) -> void {
  const auto good = need(encode_native_profile_document_json(
      need(make_native_profile_document({1}, 1, document))));
  const auto mutate = [&](const auto& action) {
    auto root = Json::parse(good);
    action(root);
    check(!decode_native_profile_document_json(root.dump()),
          "Malformed header admitted");
  };
  for (const auto& value :
       {Json(true), Json(nullptr), Json("1"), Json(0), Json(2), Json(1.0)})
    mutate([&](Json& root) { root["profile"]["version"] = value; });
  for (const auto text :
       {"", "0", "+1", "-1", "01", " 1", "1x", "18446744073709551616"}) {
    mutate([&](Json& root) { root["profile"]["id"] = text; });
    mutate([&](Json& root) { root["profile"]["save_sequence"] = text; });
  }
  for (const auto text :
       {"", "+1", "-1", "01", " 1", "1x", "18446744073709551616"}) {
    mutate([&](Json& root) { root["profile"]["summary"]["tick"] = text; });
    mutate([&](Json& root) {
      root["profile"]["summary"]["universe_seed"] = text;
    });
  }
  mutate([](Json& root) { root.erase("profile"); });
  mutate([](Json& root) { root["profile"]["unknown"] = 1; });
  mutate([](Json& root) { root["profile"]["kind"] = "guided"; });
  mutate(
      [](Json& root) { root["profile"]["summary"]["location"] = "unknown"; });
  mutate([](Json& root) {
    root["profile"]["summary"]["native_format"] = 4294967296ULL;
  });
  mutate(
      [](Json& root) { root["profile"]["summary"]["native_format"] = 25.0; });
  mutate([](Json& root) { root["profile"]["summary"]["native_format"] = 0; });
  mutate([](Json& root) { root["profile"]["summary"]["tick"] = "9999"; });
  mutate(
      [](Json& root) { root["profile"]["summary"]["universe_seed"] = "9999"; });
  mutate([](Json& root) {
    root["profile"]["summary"]["universe_seed"] = std::string(2049, '1');
  });
  mutate([](Json& root) { root["voyage"] = nullptr; });
  for (const auto text : {"", "{", "{}", "[]", "null"})
    check(!decode_native_profile_document_json(text),
          "Malformed root admitted");
  const std::string deep = std::string(65, '[') + "0" + std::string(65, ']');
  check(!decode_native_profile_document_json(deep), "Excess nesting admitted");
  check(!decode_native_profile_document_json(
            std::string(kMaximumSaveDocumentBytes + 1, ' ')),
        "Excess bytes admitted");
  auto duplicate = good;
  const auto at = duplicate.find("\"save_sequence\"");
  check(at != std::string::npos, "Duplicate-key fixture absent");
  duplicate.insert(at, "\"save_sequence\": \"2\", ");
  check(!decode_native_profile_document_json(duplicate),
        "Duplicate header key admitted");
  duplicate = good;
  duplicate.insert(1, "\"profile\": {},");
  check(!decode_native_profile_document_json(duplicate),
        "Duplicate root profile admitted");
}
} // namespace

auto main() -> int {
  using namespace apsis_drift;
  for (const auto seed :
       {Seed{0}, Seed{42}, Seed{std::numeric_limits<std::uint64_t>::max()}}) {
    const auto selected = need(native_new_game(seed));
    round_trip(selected.document, NativeProfileLocation::station_walk);
    supported_versions(selected);
    jump_phases(seed);
    const auto frozen = make_freedom_new_game_document(seed);
    round_trip(NativeSaveDocument{frozen},
               NativeProfileLocation::legacy_station);
  }
  const auto selected = need(native_new_game({42}));
  malformed(selected.document);
  storage_contract(selected.document);
  session_contract(selected.document);
  const NativeSaveDocument legacy{make_new_game_document(Seed{42})};
  check(!make_native_profile_document({1}, 1, legacy),
        "Legacy career reinterpreted as Freedom");
  auto bad = need(make_native_profile_document({1}, 1, selected.document));
  bad.header.summary.location = static_cast<NativeProfileLocation>(255);
  check(!encode_native_profile_document_json(bad), "Invalid location encoded");
  std::cout << "Native profile: " << failures << " failures\n";
  return failures ? 1 : 0;
}
