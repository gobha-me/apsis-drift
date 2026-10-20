#include "apsis_drift/rigid_frame_handoff.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

namespace {
using namespace apsis_drift;
int checks{}, failures{};
auto check(bool value, std::string_view message) -> void {
  ++checks;
  if (!value) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}
template <class T, class E> auto need(std::expected<T, E> value) -> T {
  if (!value) throw std::runtime_error("Minor frame fixture refused");
  return std::move(*value);
}
auto close(double a, double b, double tolerance) -> bool {
  return std::abs(a - b) <= tolerance;
}
auto run() -> void {
  const auto system = need(generate_physical_origin_system(Seed{42}, 2));
  const auto catalog = need(generate_secondary_body_catalog(system));
  const auto minor = need(
      generate_physical_minor_descriptor(catalog, catalog.bodies[1].node.id));
  const auto moon = need(
      generate_physical_moon_descriptor(catalog, catalog.bodies[0].node.id));
  const RigidBodyWorldContext context{minor}, moon_context{moon},
      planet_context{system};
  RigidBodyState state;
  state.craft = {kWayfarerFrameId, kWayfarerFrameVersion};
  state.frame = {RigidFrameKind::body_relative_inertial,
                 system.catalog.id,
                 {},
                 {},
                 minor.body};
  state.tick = 12345;
  state.position_metres = {minor.radius_metres + 20000.0, 321.5, -123.25};
  state.linear_velocity_metres_per_second = {2, -3, 4};
  state.orientation = need(normalize_rigid_orientation({.9, .1, .2, .3}));
  state.angular_velocity_radians_per_second = {.01, -.02, .03};
  check(context.physical_minor_owner() == &minor &&
            context.physical_moon_owner() == nullptr &&
            context.physical_owner() == &minor.catalog.owner,
        "Exact explicitly owned context");
  const auto original = state;
  const auto encoded = need(encode_rigid_body_state_json(context, state));
  check(need(decode_rigid_body_state_json(context, encoded)) == state &&
            state == original,
        "Minor codec exact roundtrip and immutable source");
  const auto doc = nlohmann::ordered_json::parse(encoded);
  check(doc.at("version") == 5 &&
            doc.at("frame").at("body").at("kind") == "minor" &&
            doc.at("minor_owner").at("family") == "physical_minor" &&
            !doc.contains("moon_owner"),
        "Minor v5 and owner cannot masquerade as Moon v4");
  check(!decode_rigid_body_state_json(moon_context, encoded) &&
            !decode_rigid_body_state_json(planet_context, encoded),
        "Wrong physical family refuses minor codec");
  for (const auto field : {"kind", "version", "owner", "unknown"}) {
    auto bad = doc;
    if (std::string_view{field} == "kind")
      bad["frame"]["body"]["kind"] = "moon";
    else if (std::string_view{field} == "version")
      bad["version"] = 4;
    else if (std::string_view{field} == "owner")
      bad["minor_owner"]["body"]["value"] = "1";
    else
      bad["unexpected"] = true;
    check(!decode_rigid_body_state_json(context, bad.dump()),
          "Closed minor codec rejects substituted identity/version/unknown "
          "field");
  }
  check(!decode_rigid_body_state_json(context, encoded + "x") &&
            !decode_rigid_body_state_json(
                context, std::string(kMaximumRigidBodyDocumentBytes + 1, ' ')),
        "Bounded complete JSON only");
  {
    auto bad = state;
    bad.frame.body = moon.body;
    check(!validate_rigid_body_state(context, bad), "Wrong body owner refuses");
  }
  {
    auto bad = state;
    bad.frame.planet = system.catalog.planets.front().descriptor.id;
    check(!validate_rigid_body_state(context, bad),
          "No mixed PlanetId carrier");
  }
  {
    auto bad = state;
    bad.position_metres.x = std::numeric_limits<double>::infinity();
    check(!validate_rigid_body_state(context, bad), "Nonfinite state refuses");
  }
  {
    auto bad = state;
    bad.position_metres.x = -0.0;
    check(!encode_rigid_body_state_json(context, bad),
          "Decode/encode never silently normalize unknown zeros");
  }
  const auto hierarchy = need(make_body_hierarchy(catalog));
  const auto bodies =
      need(resolve_body_ephemerides(hierarchy, {state.tick, 0}));
  const auto body =
      std::find_if(bodies.begin(), bodies.end(),
                   [&](const auto& b) { return b.body == minor.body; });
  check(body != bodies.end(), "Actual minor ephemeris found");
  const auto inertial = need(reframe_rigid_body(
      context, state,
      {{RigidFrameKind::system_inertial, system.catalog.id, {}, {}},
       state.tick}));
  check(inertial.position_metres.x ==
                state.position_metres.x + body->position.x &&
            inertial.linear_velocity_metres_per_second.y ==
                state.linear_velocity_metres_per_second.y + body->velocity.y &&
            inertial.orientation == state.orientation &&
            inertial.angular_velocity_radians_per_second ==
                state.angular_velocity_radians_per_second,
        "System translation uses actual star-parent hierarchy, no attitude "
        "change");
  const auto returned =
      need(reframe_rigid_body(context, inertial, {state.frame, state.tick}));
  const double tolerance =
      8 * std::numeric_limits<double>::epsilon() *
      std::max({1.0, std::abs(body->position.x), std::abs(body->position.y),
                std::abs(body->position.z)});
  check(
      close(returned.position_metres.x, state.position_metres.x, tolerance) &&
          close(returned.position_metres.y, state.position_metres.y,
                tolerance) &&
          close(returned.position_metres.z, state.position_metres.z, tolerance),
      "Translation roundtrip respects astronomical binary64 precision");
  const RigidCoordinateFrame fixed{
      RigidFrameKind::body_fixed, system.catalog.id, {}, {}, minor.body};
  const auto rotated =
      need(reframe_rigid_body(context, state, {fixed, state.tick}, minor));
  const auto back = need(
      reframe_rigid_body(context, rotated, {state.frame, state.tick}, minor));
  check(
      close(back.position_metres.x, state.position_metres.x, 1e-7) &&
          close(back.position_metres.y, state.position_metres.y, 1e-7) &&
          close(back.linear_velocity_metres_per_second.z,
                state.linear_velocity_metres_per_second.z, 1e-9),
      "Fixed/inertial spin transform reuses complete p/v/q/angular semantics");
  check(need(reframe_rigid_body(context, rotated, {fixed, state.tick},
                                minor)) == rotated,
        "Authenticated fixed identity remains exact");
  check(need(decode_rigid_body_state_json(
            context, need(encode_rigid_body_state_json(context, rotated)))) ==
            rotated,
        "Actual fixed frame also has exact minor-v5 codec");
  check(!reframe_rigid_body(context, state, {fixed, state.tick + 1}, minor) &&
            !reframe_rigid_body(context, state, {fixed, state.tick}, moon),
        "Stale tick and wrong rotation owner refuse");
  auto moon_state = state;
  moon_state.frame.body = moon.body;
  const auto moon_encoded =
      need(encode_rigid_body_state_json(moon_context, moon_state));
  const auto moon_doc = nlohmann::ordered_json::parse(moon_encoded);
  check(moon_doc.at("version") == 4 && moon_doc.contains("moon_owner") &&
            !moon_doc.contains("minor_owner") &&
            need(decode_rigid_body_state_json(moon_context, moon_encoded)) ==
                moon_state &&
            !decode_rigid_body_state_json(context, moon_encoded),
        "Moon v4 remains independent and exact");
  check(need(rigid_body_state_checksum(context, state)) !=
            need(rigid_body_state_checksum(moon_context, moon_state)),
        "Checksum families remain distinct");
  check(state == original,
        "All pure frame/codec queries leave source unchanged");
}
} // namespace
int main() {
  try {
    run();
  } catch (const std::exception& e) {
    check(false, e.what());
  }
  std::cout << checks << " checks, " << failures << " failures\n";
  return failures ? 1 : 0;
}
