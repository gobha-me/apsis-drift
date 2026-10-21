#include "apsis_drift/freedom_recovery_save.hpp"
#include "apsis_drift/native_startup.hpp"
#include "apsis_drift/origin_docking.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace {
using namespace apsis_drift;
using Json = nlohmann::ordered_json;
int checks{}, failures{};
auto check(bool value, std::string_view message) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) {
    if constexpr (std::is_same_v<E, SaveSchemaError>)
      throw std::runtime_error(value.error().detail);
    else
      throw std::runtime_error("Orbital survey fixture refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*value);
}
auto known_planet(const FreedomKnowledge& k) -> KnowledgeSubject {
  const auto found = std::ranges::find_if(k.entries, [](const auto& entry) {
    return entry.subject.kind == KnowledgeSubjectKind::planet;
  });
  if (found == k.entries.end()) throw std::runtime_error("No known planet");
  return found->subject;
}
auto resolve_landing(const FreedomKnowledge& knowledge, KnowledgeSubject target,
                     StarterCraftId craft) -> FreedomKnowledge {
  const KnowledgeEvidence evidence{target,
                                   KnowledgeFact::landing,
                                   {NavigationKnowledgeLevel::resolved,
                                    KnowledgeSource::local_ship, craft.value,
                                    4000, craft}};
  return need(apply_freedom_knowledge(knowledge, evidence, 5000));
}
auto codec() -> void {
  const Seed seed{42};
  auto k = need(
      make_freedom_starting_knowledge(seed, kFreedomObservedKnowledgeVersion));
  const auto subject = known_planet(k);
  const auto craft = make_freedom_new_game_document(seed).state.craft;
  const auto historical = need(encode_freedom_knowledge_json(k, 5000));
  check(!Json::parse(historical).contains("orbital_surveys") &&
            need(encode_freedom_knowledge_json(
                need(decode_freedom_knowledge_json(historical, 5000)), 5000)) ==
                historical,
        "Absent extension preserves exact historical knowledge bytes");
  k.orbital_surveys = FreedomOrbitalSurveyState{};
  const auto empty = need(encode_freedom_knowledge_json(k, 5000));
  check(need(decode_freedom_knowledge_json(empty, 5000)) == k &&
            empty != historical,
        "Explicit empty survey selection survives independently of absence");
  k.orbital_surveys->active = FreedomOrbitalSurveyAcquisition{
      subject.system, {subject.identity}, craft, 100, 3599};
  const auto active = need(encode_freedom_knowledge_json(k, 5000));
  check(need(decode_freedom_knowledge_json(active, 5000)) == k,
        "Accumulated progress survives roundtrip with nonqualifying held time");
  for (unsigned fault = 0; fault < 11; ++fault) {
    auto bad = Json::parse(active);
    auto& state = bad["orbital_surveys"];
    auto& item = state["active"];
    switch (fault) {
      case 0: state["version"] = 2; break;
      case 1: state["unexpected"] = 0; break;
      case 2: item["acquired_ticks"] = 3600; break;
      case 3:
        item["acquired_ticks"] = std::numeric_limits<std::uint64_t>::max();
        break;
      case 4: item["started_tick"] = 5001; break;
      case 5: item["started_tick"] = 4999; break;
      case 6: item["craft"] = 0; break;
      case 7: item["planet"] = 0; break;
      case 8: item["system"] = -1; break;
      case 9: item["acquired_ticks"] = 12.0; break;
      case 10: item["extra"] = 0; break;
    }
    check(!decode_freedom_knowledge_json(bad.dump(), 5000),
          "Malformed, future, foreign or narrowing acquisition is rejected");
  }
  k.orbital_surveys->active.reset();
  k = resolve_landing(k, subject, craft);
  k.orbital_surveys->completed.push_back(
      {subject.system, {subject.identity}, craft, 100, 4000});
  const auto complete = need(encode_freedom_knowledge_json(k, 5000));
  check(need(decode_freedom_knowledge_json(complete, 5000)) == k,
        "Completion retains exact source/start/completion provenance");
  for (unsigned fault = 0; fault < 3; ++fault) {
    auto bad = Json::parse(complete);
    auto& entries = bad["entries"];
    const auto landing =
        std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
          return entry["fact"] ==
                     static_cast<unsigned>(KnowledgeFact::landing) &&
                 entry["identity"] == subject.identity;
        });
    if (landing == entries.end())
      throw std::runtime_error("No landing evidence");
    switch (fault) {
      case 0: entries.erase(landing); break;
      case 1:
        (*landing)["transitions"][0]["level"] =
            static_cast<unsigned>(NavigationKnowledgeLevel::probable);
        break;
      case 2: (*landing)["transitions"][0]["tick"] = 4001; break;
    }
    check(
        !decode_freedom_knowledge_json(bad.dump(), 5000),
        "Completed survey rejects missing, probable or later landing evidence");
  }
  auto earlier = Json::parse(complete);
  for (auto& entry : earlier["entries"])
    if (entry["fact"] == static_cast<unsigned>(KnowledgeFact::landing))
      entry["transitions"][0]["tick"] = 3999;
  const auto already_resolved =
      need(decode_freedom_knowledge_json(earlier.dump(), 5000));
  check(
      need(decode_freedom_knowledge_json(
          need(encode_freedom_knowledge_json(already_resolved, 5000)), 5000)) ==
          already_resolved,
      "Previously resolved landing remains valid for later survey completion");
  for (unsigned fault = 0; fault < 7; ++fault) {
    auto bad = Json::parse(complete);
    auto& state = bad["orbital_surveys"];
    auto& records = state["completed"];
    switch (fault) {
      case 0: records[0]["completed_tick"] = 3699; break;
      case 1: records[0]["completed_tick"] = 5001; break;
      case 2: records.push_back(records[0]); break;
      case 3:
        state["active"] = Json::parse(active)["orbital_surveys"]["active"];
        break;
      case 4:
        while (records.size() <= kMaximumFreedomOrbitalSurveys)
          records.push_back(records[0]);
        break;
      case 5: records[0]["extra"] = true; break;
      case 6:
        records[0]["started_tick"] = std::numeric_limits<std::uint64_t>::max();
        break;
    }
    check(!decode_freedom_knowledge_json(bad.dump(), 5000),
          "Invalid duration/clock/shape/duplicate/cap/completed acquisition "
          "rejected");
  }
  auto old = need(make_freedom_starting_knowledge(seed));
  old.orbital_surveys = FreedomOrbitalSurveyState{};
  check(!validate_freedom_knowledge(old, 5000),
        "Extension never silently selects sensors for historical chart-only "
        "recipe");
  const auto truth = need(generate_physical_origin_system(seed, 2));
  const auto hidden =
      std::ranges::find_if(truth.catalog.planets, [&](const auto& planet) {
        return std::ranges::none_of(k.entries, [&](const auto& entry) {
          return entry.subject.kind == KnowledgeSubjectKind::planet &&
                 entry.subject.identity == planet.descriptor.id.value;
        });
      });
  if (hidden == truth.catalog.planets.end())
    throw std::runtime_error("No hidden planet");
  k.orbital_surveys->completed[0].planet = hidden->descriptor.id;
  check(!validate_freedom_knowledge(k, 5000),
        "Registered but unknown planets cannot appear in survey history");
  k.orbital_surveys->completed[0].planet = {subject.identity};
  const KnowledgeSubject discovered{KnowledgeSubjectKind::planet,
                                    truth.catalog.id,
                                    hidden->descriptor.id.value};
  k = need(apply_freedom_knowledge(
      k,
      {discovered,
       KnowledgeFact::identity_type,
       {NavigationKnowledgeLevel::contact, KnowledgeSource::local_ship,
        craft.value, 1, craft}},
      5000));
  k = resolve_landing(k, discovered, craft);
  k.orbital_surveys->completed.push_back(
      {discovered.system, {discovered.identity}, craft, 100, 4000});
  std::ranges::sort(k.orbital_surveys->completed,
                    [](const auto& a, const auto& b) {
                      return std::tuple{a.system.value, a.planet.value} <
                             std::tuple{b.system.value, b.planet.value};
                    });
  check(validate_freedom_knowledge(k, 5000).has_value(),
        "Unique completed targets have canonical system/planet order");
  std::swap(k.orbital_surveys->completed[0], k.orbital_surveys->completed[1]);
  check(!validate_freedom_knowledge(k, 5000),
        "Out-of-order completion history is rejected rather than normalized");
}
auto recovery() -> void {
  const Seed seed{42};
  auto selected = need(native_new_game(seed));
  auto voyage = std::get<FreedomKnowledgeSaveDocument>(selected.document);
  auto& assembly =
      std::get<FreedomStartingAssemblySaveDocument>(voyage.voyage.voyage.base);
  auto& flight = assembly.journey.voyage.flight;
  const auto system = need(generate_physical_origin_system(seed, 2));
  const auto station = generate_origin_station(seed);
  const auto geometry = need(origin_station_geometry(station));
  flight.flight = need(release_origin_port(
      system, station, geometry,
      {geometry.version, assembly.journey.voyage.docking.target,
       flight.flight.craft, 5000}));
  flight.origin.state.tick = 5000;
  voyage.voyage.resources.tick = 5000;
  const auto craft = flight.origin.state.craft;
  const auto planet = *flight.flight.frame.planet;
  const auto other =
      std::ranges::find_if(system.catalog.planets, [&](const auto& body) {
        return body.descriptor.id != planet;
      });
  if (other == system.catalog.planets.end())
    throw std::runtime_error("No second planet");
  voyage.knowledge = need(apply_freedom_knowledge(
      voyage.knowledge,
      {{KnowledgeSubjectKind::planet, system.catalog.id,
        other->descriptor.id.value},
       KnowledgeFact::identity_type,
       {NavigationKnowledgeLevel::contact, KnowledgeSource::local_ship,
        craft.value, 1, craft}},
      5000));
  voyage.knowledge = resolve_landing(
      voyage.knowledge,
      {KnowledgeSubjectKind::planet, system.catalog.id, planet.value}, craft);
  voyage.knowledge =
      resolve_landing(voyage.knowledge,
                      {KnowledgeSubjectKind::planet, system.catalog.id,
                       other->descriptor.id.value},
                      craft);
  voyage.knowledge.orbital_surveys = FreedomOrbitalSurveyState{};
  auto& surveys = *voyage.knowledge.orbital_surveys;
  surveys.active = FreedomOrbitalSurveyAcquisition{system.catalog.id, planet,
                                                   craft, 100, 3599};
  need(validate_freedom_knowledge_document(voyage));
  auto foreign = voyage;
  foreign.knowledge.orbital_surveys->active->craft =
      need(derive_freedom_craft_identity(seed, 1));
  check(!validate_freedom_knowledge_document(foreign),
        "Active acquisition must belong to the current actual craft");
  foreign = voyage;
  foreign.knowledge.orbital_surveys->active->planet = other->descriptor.id;
  check(validate_freedom_knowledge(foreign.knowledge, 5000).has_value() &&
            !validate_freedom_knowledge_document(foreign),
        "Known foreign planet cannot own this craft's active acquisition");
  const auto replace = [&](const FreedomKnowledgeSaveDocument& original) {
    FreedomRecoverySaveDocument before{original, {}};
    const auto checksum = need(freedom_recovery_source_checksum(before.voyage));
    return need(complete_freedom_recovery(need(
        record_freedom_loss(before, FreedomLossCause::irrecoverable_destruction,
                            craft, 5000, checksum))));
  };
  const auto unfinished = replace(voyage);
  check(recovery_knowledge(unfinished.voyage).knowledge.orbital_surveys &&
            !recovery_knowledge(unfinished.voyage)
                 .knowledge.orbital_surveys->active,
        "Replacement retains selected survey state but clears unfinished work");
  surveys.active.reset();
  surveys.completed.push_back({system.catalog.id, planet, craft, 100, 4000});
  const auto completed = replace(voyage);
  const auto text = need(encode_freedom_recovery_document_json(completed));
  check(need(decode_freedom_recovery_document_json(text)) == completed &&
            recovery_knowledge(completed.voyage).knowledge.orbital_surveys ==
                voyage.knowledge.orbital_surveys,
        "Recovered craft retains completed personal survey provenance exactly");
  for (unsigned fault = 0; fault < 4; ++fault) {
    auto bad = completed;
    auto& knowledge =
        std::get<FreedomKnowledgeSaveDocument>(bad.voyage).knowledge;
    switch (fault) {
      case 0: knowledge.orbital_surveys.reset(); break;
      case 1: knowledge.orbital_surveys->completed.clear(); break;
      case 2:
        knowledge.orbital_surveys->completed[0].completed_tick = 4001;
        break;
      case 3:
        knowledge.orbital_surveys->completed[0].craft =
            recovery_flight(bad.voyage).origin.state.craft;
        break;
    }
    check(!validate_freedom_recovery_document(bad),
          "Replacement cannot erase or rewrite completed survey history");
  }
  auto backdated = completed;
  auto& added =
      std::get<FreedomKnowledgeSaveDocument>(backdated.voyage).knowledge;
  added.orbital_surveys->completed.push_back(
      {system.catalog.id, other->descriptor.id,
       recovery_flight(backdated.voyage).origin.state.craft, 100, 4000});
  std::ranges::sort(added.orbital_surveys->completed,
                    [](const auto& a, const auto& b) {
                      return std::tuple{a.system.value, a.planet.value} <
                             std::tuple{b.system.value, b.planet.value};
                    });
  check(
      validate_freedom_knowledge(added, 5000).has_value() &&
          !validate_freedom_recovery_document(backdated),
      "Replacement cannot backdate newly invented survey history before loss");
}
} // namespace
int main() {
  try {
    codec();
    recovery();
  } catch (const std::exception& error) {
    std::cerr << "Orbital survey save fixture: " << error.what() << '\n';
    return 1;
  }
  std::cout << "Orbital survey save contracts: " << checks << " checks, "
            << failures << " failures\n";
  return failures ? 1 : 0;
}
