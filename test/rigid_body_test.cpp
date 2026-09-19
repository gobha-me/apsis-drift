#include "apsis_drift/rigid_body.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {
using namespace apsis_drift;
int failures{};

auto check(bool condition, std::string_view message) -> void {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}

struct Fixture {
  LocalSystemDescriptor system{generate_origin_system(Seed{42})};
  OriginStationDescriptor station{generate_origin_station(Seed{42})};
  RigidBodyWorldContext context{system, &station};

  auto state() const -> RigidBodyState {
    RigidBodyState value;
    value.frame.system = system.id;
    value.tick = 120;
    value.position_metres = {1.25, -2.5, 3.75};
    value.orientation = {.5, .5, -.5, .5};
    value.linear_velocity_metres_per_second = {4, -5, 6};
    value.angular_velocity_radians_per_second = {.125, -.25, .5};
    return value;
  }
};

auto doubles(const RigidBodyState& s) -> std::array<double, 13> {
  return {s.position_metres.x,
          s.position_metres.y,
          s.position_metres.z,
          s.orientation.w,
          s.orientation.x,
          s.orientation.y,
          s.orientation.z,
          s.linear_velocity_metres_per_second.x,
          s.linear_velocity_metres_per_second.y,
          s.linear_velocity_metres_per_second.z,
          s.angular_velocity_radians_per_second.x,
          s.angular_velocity_radians_per_second.y,
          s.angular_velocity_radians_per_second.z};
}

auto same_bits(const RigidBodyState& a, const RigidBodyState& b) -> bool {
  const auto av = doubles(a), bv = doubles(b);
  for (std::size_t i = 0; i < av.size(); ++i)
    if (std::bit_cast<std::uint64_t>(av[i]) !=
        std::bit_cast<std::uint64_t>(bv[i]))
      return false;
  return a.craft == b.craft && a.frame == b.frame && a.tick == b.tick;
}

auto scalar_fields(RigidBodyState& s) -> std::array<double*, 13> {
  return {&s.position_metres.x,
          &s.position_metres.y,
          &s.position_metres.z,
          &s.orientation.w,
          &s.orientation.x,
          &s.orientation.y,
          &s.orientation.z,
          &s.linear_velocity_metres_per_second.x,
          &s.linear_velocity_metres_per_second.y,
          &s.linear_velocity_metres_per_second.z,
          &s.angular_velocity_radians_per_second.x,
          &s.angular_velocity_radians_per_second.y,
          &s.angular_velocity_radians_per_second.z};
}

template <typename Mutation>
auto rejects(const Fixture& fixture, Mutation mutation,
             std::string_view message) -> void {
  auto state = fixture.state();
  mutation(state);
  const auto before = state;
  check(!validate_rigid_body_state(fixture.context, state), message);
  check(!rigid_body_state_checksum(fixture.context, state), message);
  check(!encode_rigid_body_state_json(fixture.context, state), message);
  check(same_bits(state, before),
        "failed state projection never mutates input");
}

auto golden_contract(const Fixture& fixture) -> void {
  const auto state = fixture.state();
  check(fixture.system.id.value == 677859337506523986ULL,
        "golden uses an existing generated system identity");
  check(validate_rigid_body_state(fixture.context, state).has_value(),
        "golden state is valid");
  // Independently calculated FNV-1a over the documented fixed-width LE fields.
  const auto checksum = rigid_body_state_checksum(fixture.context, state);
  check(checksum && *checksum == 11691532549348544544ULL,
        "all pose, velocity, owner, craft and tick bits match checksum golden");
  constexpr std::string_view golden =
      R"({"format":"apsis-drift-rigid-body","version":1,"craft":{"id":"1","version":1},)"
      R"("frame":{"kind":"system_inertial","system":"677859337506523986","planet":null,"station":null},)"
      R"("tick":"120","position_metres":["1.25","-2.5","3.75"],"orientation_wxyz":["0.5","0.5","-0.5","0.5"],)"
      R"("linear_velocity_metres_per_second":["4","-5","6"],"angular_velocity_radians_per_second":["0.125","-0.25","0.5"]})";
  const auto encoded = encode_rigid_body_state_json(fixture.context, state);
  check(encoded && *encoded == golden, "exact canonical native JSON golden");
  const auto loaded = decode_rigid_body_state_json(fixture.context, golden);
  check(loaded && same_bits(*loaded, state),
        "golden hydration preserves every bit");
  for (std::size_t index = 0; index < 13; ++index) {
    if (index >= 3 && index <= 6) continue;
    auto changed = state;
    *scalar_fields(changed)[index] += .125;
    check(rigid_body_state_checksum(fixture.context, changed) != checksum,
          "each vector component contributes to checksum");
  }
  auto changed = state;
  ++changed.tick;
  check(rigid_body_state_checksum(fixture.context, changed) != checksum,
        "authoritative tick contributes to checksum");
  changed = state;
  changed.orientation = {1, 0, 0, 0};
  check(rigid_body_state_checksum(fixture.context, changed) != checksum,
        "orientation contributes to checksum");
}

auto orientation_contract(const Fixture& fixture) -> void {
  auto state = fixture.state();
  const auto original = state;
  state.orientation = {-.5, -.5, .5, -.5};
  check(!validate_rigid_body_state(fixture.context, state),
        "equivalent but noncanonical quaternion is not authoritative yet");
  auto canonical = canonicalize_rigid_body_state(fixture.context, state);
  check(canonical && same_bits(*canonical, original),
        "q and -q canonicalize identically");
  for (const auto q :
       {RigidOrientation{0, -1, 0, 0}, RigidOrientation{0, 0, -1, 0},
        RigidOrientation{0, 0, 0, -1}}) {
    state.orientation = q;
    canonical = canonicalize_rigid_body_state(fixture.context, state);
    check(canonical && validate_rigid_body_state(fixture.context, *canonical),
          "first nonzero WXYZ tie-break handles 180-degree rotations");
    if (canonical) {
      const auto& c = canonical->orientation;
      check((c.x == 1 || c.y == 1 || c.z == 1) && !std::signbit(c.w),
            "180-degree rotation has positive leading component and zero");
    }
  }
  state = fixture.state();
  state.orientation = {1, -0.0, -0.0, -0.0};
  state.position_metres = {-0.0, -0.0, -0.0};
  state.linear_velocity_metres_per_second = {-0.0, -0.0, -0.0};
  state.angular_velocity_radians_per_second = {-0.0, -0.0, -0.0};
  check(!validate_rigid_body_state(fixture.context, state),
        "negative zero is not canonical");
  canonical = canonicalize_rigid_body_state(fixture.context, state);
  check(canonical.has_value(), "negative zeros explicitly canonicalize");
  if (canonical)
    for (double value : doubles(*canonical))
      check(value != 0 || !std::signbit(value),
            "all zero fields have positive sign bits");

  for (const auto q :
       {RigidOrientation{0, 0, 0, 0},
        RigidOrientation{std::numeric_limits<double>::denorm_min(), 0, 0, 0},
        RigidOrientation{2, 0, 0, 0}, RigidOrientation{1e300, 0, 0, 0}}) {
    state.orientation = q;
    check(
        !canonicalize_rigid_body_state(fixture.context, state),
        "canonicalization cannot silently repair invalid quaternion magnitude");
    check(!normalize_rigid_orientation(q),
          "impossible/unsafe normalization rejects");
  }
  const auto normalized = normalize_rigid_orientation({.31, -.47, .59, -.61});
  const auto fma_sensitive = normalize_rigid_orientation({.1, .1, .2, .8});
  check(fma_sensitive.has_value(), "FMA-sensitive provider input normalizes");
  if (fma_sensitive) {
    constexpr std::array expected{0x1.e990cdad55ed1p-4, 0x1.e990cdad55ed1p-4,
                                  0x1.e990cdad55ed1p-3, 0x1.e990cdad55ed1p-1};
    const std::array actual{fma_sensitive->w, fma_sensitive->x,
                            fma_sensitive->y, fma_sensitive->z};
    for (std::size_t index = 0; index < expected.size(); ++index)
      check(std::bit_cast<std::uint64_t>(actual[index]) ==
                std::bit_cast<std::uint64_t>(expected[index]),
            "FMA-sensitive normalization preserves separate product rounding");
  }
  check(normalized.has_value(),
        "provider explicitly normalizes an awkward quaternion");
  if (!normalized) return;
  // Independently evaluated binary64 products/sums in WXYZ order (no FMA).
  const std::array<std::uint64_t, 4> normalized_bits{
      0x3fd37b205e52a81dULL, 0xbfdd89208f01727eULL, 0x3fe289d059c24523ULL,
      0xbfe32ab05ccd31caULL};
  const std::array normalized_values{normalized->w, normalized->x,
                                     normalized->y, normalized->z};
  for (std::size_t index = 0; index < normalized_bits.size(); ++index)
    check(std::bit_cast<std::uint64_t>(normalized_values[index]) ==
              normalized_bits[index],
          "provider normalization matches unfused binary64 golden");
  state = fixture.state();
  state.orientation = *normalized;
  const auto before = state;
  const auto bytes = encode_rigid_body_state_json(fixture.context, state);
  const auto checksum = rigid_body_state_checksum(fixture.context, state);
  for (int cycle = 0; cycle < 128; ++cycle) {
    canonical = canonicalize_rigid_body_state(fixture.context, state);
    check(canonical && same_bits(*canonical, before),
          "canonicalization is bitwise idempotent");
    const auto encoded = encode_rigid_body_state_json(fixture.context, state);
    check(encoded == bytes,
          "awkward quaternion repeated save bytes never drift");
    if (!encoded) break;
    const auto loaded = decode_rigid_body_state_json(fixture.context, *encoded);
    check(loaded && same_bits(*loaded, before),
          "load never renormalizes quaternion");
    if (!loaded) break;
    state = *loaded;
    check(rigid_body_state_checksum(fixture.context, state) == checksum,
          "repeated resume checksum remains exact");
  }
  state = fixture.state();
  state.orientation = {1 + 2e-13, 0, 0, 0};
  check(validate_rigid_body_state(fixture.context, state).has_value(),
        "documented small norm error is accepted without renormalization");
  canonical = canonicalize_rigid_body_state(fixture.context, state);
  check(canonical && same_bits(*canonical, state),
        "within-tolerance norm error remains authoritative rather than "
        "silently normalized");
  const auto near_unit_json =
      encode_rigid_body_state_json(fixture.context, state);
  check(near_unit_json.has_value(), "within-tolerance orientation encodes");
  if (near_unit_json) {
    const auto loaded =
        decode_rigid_body_state_json(fixture.context, *near_unit_json);
    check(loaded && same_bits(*loaded, state),
          "hydrate preserves accepted non-unit rounding error without "
          "arithmetic");
  }
  state.orientation.w = 1 + 1e-12;
  check(!validate_rigid_body_state(fixture.context, state),
        "outside norm tolerance rejects");
  state.orientation = {0x1.83091e6a80533p-2, 0x1.83091e6a80533p-2,
                       0x1.83091e6a80533p-1, 0x1.83091e6a80532p-2};
  check(!validate_rigid_body_state(fixture.context, state),
        "unfused norm boundary rejects state that FMA would accept");
  check(normalize_rigid_orientation({.5, .5, 0, 0}).has_value() &&
            normalize_rigid_orientation({1, 1, 0, 0}).has_value(),
        "explicit normalization accepts both squared-norm bounds");
  check(!normalize_rigid_orientation({std::nextafter(.5, 0.0), .5, 0, 0}) &&
            !normalize_rigid_orientation({std::nextafter(1.0, 2.0), 1, 0, 0}),
        "explicit normalization refuses both sides beyond safety bounds");
  for (double value : {std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
    check(!normalize_rigid_orientation({value, 0, 0, 0}) &&
              !normalize_rigid_orientation({1, value, 0, 0}) &&
              !normalize_rigid_orientation({1, 0, value, 0}) &&
              !normalize_rigid_orientation({1, 0, 0, value}),
          "normalization refuses non-finite components before arithmetic");
  }
}

auto bounds_contract(const Fixture& fixture) -> void {
  for (std::size_t index = 0; index < 13; ++index) {
    for (double bad : {std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
      rejects(
          fixture, [=](auto& state) { *scalar_fields(state)[index] = bad; },
          "every non-finite scalar rejects");
    }
    if (index >= 3 && index <= 6) continue;
    const double limit = index < 3 ? kRigidBodyMaximumPositionMetres
                         : index < 10
                             ? kRigidBodyMaximumVelocityMetresPerSecond
                             : kRigidBodyMaximumAngularVelocityRadiansPerSecond;
    for (const double sign : {-1.0, 1.0}) {
      auto state = fixture.state();
      *scalar_fields(state)[index] = sign * limit;
      check(validate_rigid_body_state(fixture.context, state).has_value(),
            "each exact signed vector-component limit is accepted");
      const double beyond =
          sign * std::nextafter(limit, std::numeric_limits<double>::infinity());
      rejects(
          fixture,
          [=](auto& candidate) { *scalar_fields(candidate)[index] = beyond; },
          "next representable component beyond either limit rejects");
    }
  }
  auto state = fixture.state();
  const double subnormal = std::numeric_limits<double>::denorm_min();
  state.position_metres = {subnormal, subnormal, subnormal};
  state.linear_velocity_metres_per_second = {subnormal, subnormal, subnormal};
  state.angular_velocity_radians_per_second = {subnormal, subnormal, subnormal};
  const auto subnormal_json =
      encode_rigid_body_state_json(fixture.context, state);
  check(subnormal_json.has_value(),
        "finite positive subnormal vector values are allowed");
  if (subnormal_json) {
    const auto loaded =
        decode_rigid_body_state_json(fixture.context, *subnormal_json);
    check(loaded && same_bits(*loaded, state),
          "positive subnormal vectors survive canonical decimal round trip "
          "bit-exactly");
  }
  state = fixture.state();
  state.tick = std::numeric_limits<SimulationTick>::max() - 1;
  const auto encoded = encode_rigid_body_state_json(fixture.context, state);
  check(encoded.has_value(), "maximum tick with increment headroom is valid");
  if (encoded) {
    const auto loaded = decode_rigid_body_state_json(fixture.context, *encoded);
    check(loaded && loaded->tick == state.tick,
          "large tick avoids floating-point rounding");
  }
  rejects(
      fixture,
      [](auto& s) { s.tick = std::numeric_limits<SimulationTick>::max(); },
      "tick without overflow headroom rejects");
  rejects(
      fixture, [](auto& s) { s.craft.id.value = 0; },
      "unknown craft ID rejects");
  rejects(
      fixture, [](auto& s) { ++s.craft.version; },
      "unknown craft version rejects");
}

auto owner_contract(const Fixture& fixture) -> void {
  rejects(
      fixture, [](auto& s) { s.frame.system.value ^= 1U; },
      "wrong system identity rejects");
  rejects(
      fixture, [](auto& s) { s.frame.kind = static_cast<RigidFrameKind>(255); },
      "unknown coordinate owner kind rejects");
  rejects(
      fixture,
      [&](auto& s) {
        s.frame.planet = fixture.system.planets.front().descriptor.id;
      },
      "system owner cannot carry planet child");
  rejects(
      fixture, [&](auto& s) { s.frame.station = fixture.station.id; },
      "system owner cannot carry station child");
  const auto other = generate_origin_system(Seed{43});
  auto state = fixture.state();
  state.frame.kind = RigidFrameKind::planet_fixed;
  check(!validate_rigid_body_state(fixture.context, state),
        "planet frame requires a planet");
  state.frame.planet = fixture.system.planets.front().descriptor.id;
  check(validate_rigid_body_state(fixture.context, state).has_value(),
        "real parent-owned planet resolves");
  const auto planet_json = encode_rigid_body_state_json(fixture.context, state);
  check(planet_json && decode_rigid_body_state_json(fixture.context,
                                                    *planet_json) == state,
        "planet owner exact save round trip");
  check(rigid_body_state_checksum(fixture.context, state) !=
            rigid_body_state_checksum(fixture.context, fixture.state()),
        "coordinate owner kind and planet identity contribute to checksum");
  state.frame.planet = other.planets.front().descriptor.id;
  check(!validate_rigid_body_state(fixture.context, state),
        "real planet from wrong parent rejects");
  state.frame.planet.reset();
  state.frame.kind = RigidFrameKind::station_relative_inertial;
  check(!validate_rigid_body_state(fixture.context, state),
        "station frame requires a station");
  state.frame.station = fixture.station.id;
  check(validate_rigid_body_state(fixture.context, state).has_value(),
        "real origin station resolves");
  const auto station_json =
      encode_rigid_body_state_json(fixture.context, state);
  check(station_json && decode_rigid_body_state_json(fixture.context,
                                                     *station_json) == state,
        "station owner exact save round trip");
  auto altered_orbit = fixture.station.orbit;
  ++altered_orbit.radius_kilometres;
  const OriginStationDescriptor forged_station{
      fixture.station.universe_seed, fixture.station.home_system_seed,
      fixture.station.station_seed, fixture.station.id, altered_orbit};
  check(!validate_rigid_body_state({fixture.system, &forged_station}, state),
        "matching station identity cannot authorize forged station recipe");
  check(!validate_rigid_body_state({fixture.system, nullptr}, state),
        "unresolved station rejects");
  const auto other_station = generate_origin_station(Seed{43});
  check(!validate_rigid_body_state({fixture.system, &other_station}, state),
        "station from another universe cannot satisfy owner");
  state.frame.station = other_station.id;
  check(!validate_rigid_body_state({fixture.system, &other_station}, state),
        "matching station ID still rejects wrong parent system");
  state.frame.station = fixture.station.id;
  state.frame.planet = fixture.system.planets.front().descriptor.id;
  check(!validate_rigid_body_state(fixture.context, state),
        "mixed station/planet owner rejects");
  auto corrupt = fixture.system;
  corrupt.planets.clear();
  check(!validate_rigid_body_state({corrupt, nullptr}, fixture.state()),
        "malformed world context rejects before authoritative state creation");
}

auto replace_once(std::string text, std::string_view needle,
                  std::string_view replacement) -> std::string {
  const auto at = text.find(needle);
  check(at != std::string::npos,
        "malformed-document fixture finds exact target");
  if (at != std::string::npos) text.replace(at, needle.size(), replacement);
  return text;
}

auto escaped_excerpt(std::string_view text) -> std::string {
  constexpr std::string_view hex = "0123456789abcdef";
  std::string result;
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text.size() > 160 && index == 64) {
      result += " ... ";
      index = text.size() - 96;
    }
    const auto byte = static_cast<unsigned char>(text[index]);
    if (byte < 32 || byte >= 127) {
      result += "\\x";
      result += hex[byte >> 4U];
      result += hex[byte & 15U];
    } else {
      if (byte == '\\' || byte == '"') result += '\\';
      result += static_cast<char>(byte);
    }
  }
  return result;
}

auto document_contract(const Fixture& fixture) -> void {
  const auto encoded =
      encode_rigid_body_state_json(fixture.context, fixture.state());
  if (!encoded) {
    check(false, "document test needs valid seed state");
    return;
  }
  const auto& good = *encoded;
  auto live = fixture.state();
  const auto before = live;
  const auto refuse = [&](std::string_view text) {
    const auto candidate = decode_rigid_body_state_json(fixture.context, text);
    if (candidate)
      std::cerr << "Unexpectedly accepted malformed document (" << text.size()
                << " bytes): \"" << escaped_excerpt(text) << "\"\n";
    check(!candidate, "invalid native document rejects");
    if (candidate) live = *candidate;
    check(same_bits(live, before),
          "failed hydration transaction preserves live state");
  };
  for (std::size_t size = 0; size < good.size(); ++size)
    refuse(std::string_view(good).substr(0, size));
  for (std::string_view invalid : {"", "[]", "null", "{}", "true", "{", "NaN"})
    refuse(invalid);
  refuse(std::string(kMaximumRigidBodyDocumentBytes + 1, ' '));
  refuse(std::string(20, '[') + "0" + std::string(20, ']'));
  refuse(good + " false");
  refuse(good + std::string(1, '\0'));
  refuse(replace_once(good, "\"version\":1", "\"version\":2"));
  refuse(replace_once(good, "\"version\":1", "\"version\":4294967296"));
  refuse(replace_once(good, "\"id\":\"1\"", "\"id\":\"18446744073709551616\""));
  refuse(replace_once(good, "\"id\":\"1\"", "\"id\":\"0\""));
  refuse(replace_once(good, "\"system_inertial\"", "\"imaginary_frame\""));
  refuse(replace_once(good, "\"version\":1", "\"version\":1,\"version\":1"));
  refuse(replace_once(good, "\"id\":\"1\"", "\"id\":\"1\",\"id\":\"1\""));
  refuse(
      replace_once(good, "\"tick\":\"120\"", "\"tick\":\"120\",\"camera\":0"));
  refuse(replace_once(good, "\"id\":\"1\"", "\"id\":\"1\",\"camera\":0"));
  refuse(replace_once(good, "\"tick\":\"120\",", ""));
  for (std::string_view invalid :
       {"\"nan\"", "\"inf\"", "\"1e999\"", "\"-0\"", "\"01\"", "\"+1\"",
        "\"1.250\"", "\" 1.25\"", "\"1.25 \"", "\"1.25junk\"", "1.25", "null",
        "true"})
    refuse(replace_once(good, "\"1.25\"", invalid));
  for (std::string_view invalid :
       {"\"18446744073709551616\"", "\"18446744073709551615\"", "\"-1\"",
        "\"0120\"", "\"120.0\"", "\"1.2e2\"", "120", "null", "true"})
    refuse(replace_once(good, "\"tick\":\"120\"",
                        "\"tick\":" + std::string(invalid)));
  refuse(replace_once(good, "[\"1.25\",\"-2.5\",\"3.75\"]",
                      "[\"1.25\",\"-2.5\"]"));
  refuse(replace_once(good, "[\"1.25\",\"-2.5\",\"3.75\"]",
                      "[\"1.25\",\"-2.5\",\"3.75\",\"0\"]"));
  refuse(replace_once(good, "[\"0.5\",\"0.5\",\"-0.5\",\"0.5\"]",
                      "[\"-0.5\",\"-0.5\",\"0.5\",\"-0.5\"]"));
  check(decode_rigid_body_state_json(fixture.context, good).has_value(),
        "valid document still hydrates after malformed corpus");
}
} // namespace

auto main() -> int {
  const Fixture fixture;
  bounds_contract(fixture);
  owner_contract(fixture);
  orientation_contract(fixture);
  document_contract(fixture);
  golden_contract(fixture);
  std::cout << "Rigid-body state contracts: " << failures << " failures\n";
  return failures == 0 ? 0 : 1;
}
