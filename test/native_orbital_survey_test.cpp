#include "apsis_drift/native_flight_session.hpp"
#include "apsis_drift/rigid_frame_handoff.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace apsis_drift {
auto run_native_survey_commission_scenario(
    const std::optional<std::filesystem::path>& output) -> void;
auto run_native_specimen_sale_scenario(
    const std::optional<std::filesystem::path>& output) -> void;
auto run_native_charted_arrivals_scenario(
    const std::optional<std::filesystem::path>& output) -> void;
} // namespace apsis_drift

namespace {
using namespace apsis_drift;
unsigned checks{};
auto check(bool condition, const char* message) -> void {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template <class T, class E> auto need(std::expected<T, E> result) -> T {
  if (!result) {
    if constexpr (std::is_same_v<E, std::string>)
      throw std::runtime_error(result.error());
    else
      throw std::runtime_error("Survey fixture or document refused");
  }
  if constexpr (!std::is_void_v<T>) return std::move(*result);
}
auto bytes(const NativeFreedomFlightSession& session) -> std::string {
  return need(encode_native_save_document_json(need(session.save_document())));
}
auto resume(const NativeFreedomFlightSession& session)
    -> NativeFreedomFlightSession {
  return need(NativeFreedomFlightSession::open(need(native_select_save_document(
      need(decode_native_save_document_json(bytes(session)))))));
}
// Initial-condition fixture, not a claim that a pilot flew a scripted route.
// Every acquired interval after opening is an ordinary committed flight tick.
auto fixture(double radii = 1.5, bool system_frame = false,
             double speed_fraction = 1) -> NativeFreedomFlightSession {
  const Seed seed{42};
  const auto system = need(generate_physical_origin_system(seed, 2));
  const auto& planet = system.catalog.planets[0].descriptor;
  auto origin = make_freedom_new_game_document(seed);
  origin.state.tick = 240;
  FreedomFlightSaveDocument flight{origin, {}, {}};
  flight.flight.craft = {kWayfarerFrameId, 1};
  flight.flight.frame = {RigidFrameKind::planet_relative_inertial,
                         system.catalog.id,
                         planet.id,
                         {}};
  flight.flight.tick = origin.state.tick;
  flight.flight.position_metres = {planet.radius.value * 1000.0 * radii, 0, 0};
  const auto gravity = need(evaluate_central_body_gravity(
      RigidBodyWorldContext{system}, flight.flight));
  flight.flight.linear_velocity_metres_per_second = {
      0, 0,
      speed_fraction *
          std::sqrt(
              gravity.gravitational_parameter_metres_cubed_per_second_squared /
              gravity.distance_metres)};
  flight.model.physical_catalog = 2;
  flight.model.physical_ephemeris = 2;
  flight.model.assistance = false;
  flight.world = FreedomActiveWorldSelection{1, system.catalog.id, planet.id};
  if (system_frame) {
    flight.model.frame_policy = kFreedomReleaseSystemFramePolicy;
    flight.flight = need(reframe_rigid_body(
        RigidBodyWorldContext{system}, flight.flight,
        {{RigidFrameKind::system_inertial, system.catalog.id, {}, {}},
         flight.flight.tick}));
  }
  FreedomResources resources{1, origin.state.craft, flight.flight.craft,
                             origin.state.tick};
  FreedomSurfaceSaveDocument surface{flight, {}};
  FreedomResourceSaveDocument fueled{std::move(surface), resources};
  FreedomKnowledgeSaveDocument mapped{
      std::move(fueled), need(make_freedom_starting_knowledge(seed, 3))};
  FreedomTravelSaveDocument document{
      std::move(mapped), {}, NativeStartingAssemblySelection{}, {}};
  return need(NativeFreedomFlightSession::open(
      need(native_select_save_document(document))));
}
auto acquisition(const NativeFreedomFlightSession& session) -> std::uint32_t {
  return session.knowledge()->orbital_surveys->active->acquired_ticks;
}
auto core(const std::optional<std::filesystem::path>& output) -> void {
  auto station =
      need(NativeFreedomFlightSession::open(need(native_new_game(Seed{42}))));
  const auto station_before = bytes(station);
  check(!station.begin_orbital_survey() && bytes(station) == station_before,
        "Station start cannot start a scan or select a save extension");
  for (const auto& [radii, speed] : {std::pair{2.5, 1.0}, std::pair{1.5, .2}}) {
    auto refused = fixture(radii, false, speed);
    const auto before = bytes(refused);
    check(!refused.begin_orbital_survey() && bytes(refused) == before,
          "Distant or unstable flight cannot start a survey");
  }
  auto session = fixture();
  check(!session.knowledge()->orbital_surveys &&
            session.orbital_survey_view().can_begin,
        "Historical save stays absent until explicit opt-in");
  if (output) need(session.save_as(*output / "survey-start.json"));
  const auto initial_tick = session.document().flight.tick;
  need(session.begin_orbital_survey());
  check(acquisition(session) == 0 &&
            session.document().flight.tick == initial_tick,
        "Start selects the acquisition without moving time or physics");
  const auto selected = bytes(session);
  for (unsigned i = 0; i < 4; ++i) {
    (void)session.orbital_survey_view();
    need(session.observe());
  }
  check(bytes(session) == selected && bytes(resume(session)) == selected,
        "Read, Save and Continue cannot acquire or discard progress");
  NativeFlightControls invalid{};
  invalid.positive_rotation.x = std::numeric_limits<double>::quiet_NaN();
  check(!session.advance(invalid) && bytes(session) == selected,
        "Refused flight input preserves acquisition and complete save");
  check(!session.advance({}, SimulationSeconds{0}) &&
            bytes(session) == selected,
        "Rejected fixed step cannot acquire time");
  auto active_step = resume(session);
  auto passive_step = fixture();
  need(active_step.advance({}));
  need(passive_step.advance({}));
  check(
      active_step.document() == passive_step.document() &&
          active_step.resources() == passive_step.resources(),
      "Survey acquires evidence without changing physical flight or resources");
  for (unsigned tick = 1; tick < kFreedomOrbitalSurveyTicks; ++tick)
    need(session.advance({}));
  check(acquisition(session) == kFreedomOrbitalSurveyTicks - 1 &&
            session.knowledge()->orbital_surveys->completed.empty(),
        "3599 genuine qualifying intervals are not a completed survey");
  if (output) need(session.save_as(*output / "survey-near.json"));
  auto continued = resume(session);
  need(continued.advance({}));
  need(session.advance({}));
  check(bytes(session) == bytes(continued),
        "Continue at completion boundary converges on complete Save bytes");
  const auto view = session.orbital_survey_view();
  check(!view.active && view.completed && !view.can_begin &&
            view.result.has_value() &&
            session.knowledge()->orbital_surveys->completed.size() == 1 &&
            session.knowledge()->orbital_surveys->completed[0].completed_tick ==
                initial_tick + kFreedomOrbitalSurveyTicks,
        "3600th interval records one survey and resolves a readable terrain "
        "model");
  if (output) need(session.save_as(*output / "survey-complete.json"));
  need(session.advance({}));
  check(session.knowledge()->orbital_surveys->completed.size() == 1,
        "Further flight cannot duplicate completed survey history");
  auto system = fixture(1.5, true);
  need(system.begin_orbital_survey());
  need(system.advance({}));
  check(
      acquisition(system) == 1,
      "Recursive system-to-planet integration counts the outer interval once");
  auto impact = fixture();
  need(impact.enable_impact_consequences());
  need(impact.begin_orbital_survey());
  need(impact.advance({}));
  check(acquisition(impact) == 1,
        "Condition integration counts only the final outer committed interval");
  auto held = fixture(2.5);
  auto document = need(held.travel_document());
  document.voyage.knowledge.orbital_surveys = FreedomOrbitalSurveyState{};
  document.voyage.knowledge.orbital_surveys->active =
      FreedomOrbitalSurveyAcquisition{
          held.system().catalog.id, held.rotation().rotation.planet,
          held.document().origin.state.craft, 100, 30};
  held = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(document))));
  need(held.advance({}));
  check(acquisition(held) == 30 && !held.orbital_survey_view().note.empty(),
        "Out-of-range committed flight holds acquired work");
  auto returned = need(held.travel_document());
  auto& returned_body =
      std::get<FreedomFlightSaveDocument>(returned.voyage.voyage.voyage.base)
          .flight;
  const auto return_radius =
      held.system().catalog.planets[0].descriptor.radius.value * 1500.0;
  returned_body.position_metres = {return_radius, 0, 0};
  const auto return_gravity = need(evaluate_central_body_gravity(
      RigidBodyWorldContext{held.system()}, returned_body));
  returned_body.linear_velocity_metres_per_second = {
      0, 0,
      std::sqrt(return_gravity
                    .gravitational_parameter_metres_cubed_per_second_squared /
                return_radius)};
  held = need(NativeFreedomFlightSession::open(
      need(native_select_save_document(returned))));
  need(held.advance({}));
  check(acquisition(held) == 31,
        "Returning to an eligible orbit continues held work");
  need(held.cancel_orbital_survey());
  check(!held.knowledge()->orbital_surveys->active &&
            held.knowledge()->orbital_surveys->completed.empty(),
        "Cancel discards only the unfinished acquisition");
  auto jump = fixture();
  need(jump.begin_orbital_survey());
  const auto identities = generate_first_intersystem_identities(Seed{42});
  need(jump.select_jump(identities.target_system));
  need(jump.begin_jump());
  check(!jump.knowledge()->orbital_surveys->active,
        "Accepted jump commitment discards unfinished survey");
}
} // namespace
int main(int argc, char** argv) {
  try {
    std::optional<std::filesystem::path> output;
    if (argc == 3 && std::string_view{argv[1]} == "--fixtures") {
      output = std::filesystem::absolute(argv[2]);
      std::filesystem::create_directories(*output);
    } else if (argc != 1) {
      throw std::runtime_error("Expected --fixtures DIRECTORY or no arguments");
    }
    core(output);
    apsis_drift::run_native_survey_commission_scenario(output);
    apsis_drift::run_native_specimen_sale_scenario(output);
    apsis_drift::run_native_charted_arrivals_scenario(output);
    std::cout << "Orbital survey: " << checks << " checks, 0 failures\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Orbital survey failed: " << error.what() << '\n';
    return 1;
  }
}
