#include "apsis_drift/rigid_body.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <span>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>

namespace apsis_drift {
namespace {
using Json = nlohmann::ordered_json;
constexpr std::string_view format_name{"apsis-drift-rigid-body"};
constexpr int maximum_json_depth{8};
static_assert(sizeof(double) == sizeof(std::uint64_t));
static_assert(std::numeric_limits<double>::is_iec559);

auto components(const RigidOrientation& q) -> std::array<double, 4> {
  return {q.w, q.x, q.y, q.z};
}
auto components(const RigidVector3& v) -> std::array<double, 3> {
  return {v.x, v.y, v.z};
}
auto squared_norm(const RigidOrientation& q) -> double {
  // Explicit evaluation order, with no hypot/libm scaling or matrix conversion.
  const double w2 = q.w * q.w;
  const double x2 = q.x * q.x;
  const double y2 = q.y * q.y;
  const double z2 = q.z * q.z;
  return ((w2 + x2) + y2) + z2;
}
auto positive_zero(double value) -> double {
  return value == 0.0 ? 0.0 : value;
}
auto canonicalize_zeros(RigidVector3& value) -> void {
  value.x = positive_zero(value.x);
  value.y = positive_zero(value.y);
  value.z = positive_zero(value.z);
}
auto canonicalize_sign(RigidOrientation& q) -> void {
  for (const auto value : components(q)) {
    if (value == 0.0) continue;
    if (value < 0.0) {
      q.w = -q.w;
      q.x = -q.x;
      q.y = -q.y;
      q.z = -q.z;
    }
    break;
  }
  q.w = positive_zero(q.w);
  q.x = positive_zero(q.x);
  q.y = positive_zero(q.y);
  q.z = positive_zero(q.z);
}
auto validate_orientation(RigidOrientation q)
    -> std::expected<void, RigidBodyError> {
  for (const auto value : components(q)) {
    if (!std::isfinite(value))
      return std::unexpected{RigidBodyError::non_finite_state};
    if (std::abs(value) > 2.0)
      return std::unexpected{RigidBodyError::invalid_orientation};
  }
  const double norm2 = squared_norm(q);
  if (std::abs(norm2 - 1.0) > kRigidBodyOrientationSquaredNormTolerance)
    return std::unexpected{RigidBodyError::invalid_orientation};
  bool first = true;
  for (const auto value : components(q)) {
    if (value == 0.0) {
      if (std::signbit(value))
        return std::unexpected{RigidBodyError::noncanonical_state};
    } else if (first) {
      if (value < 0.0)
        return std::unexpected{RigidBodyError::noncanonical_state};
      first = false;
    }
  }
  return {};
}
auto station_matches(const OriginStationDescriptor& actual,
                     const OriginStationDescriptor& expected) -> bool {
  const auto fields = [](const OriginStationDescriptor& s) {
    return std::tie(s.universe_seed.value, s.home_system_seed.value,
                    s.station_seed.value, s.id.value, s.orbit.host_planet.value,
                    s.orbit.radius_kilometres, s.orbit.period_ticks,
                    s.orbit.epoch_phase_turns, s.orbit.inclination_microdegrees,
                    s.orbit.ascending_node_turns);
  };
  return fields(actual) == fields(expected);
}
auto validate_frame(const RigidBodyWorldContext& context,
                    const RigidCoordinateFrame& frame)
    -> std::expected<void, RigidBodyError> {
  if (!validate_local_system(context.system))
    return std::unexpected{RigidBodyError::invalid_world_context};
  if (frame.system != context.system.id)
    return std::unexpected{RigidBodyError::unknown_system};
  switch (frame.kind) {
    case RigidFrameKind::system_inertial:
      if (frame.planet || frame.station)
        return std::unexpected{RigidBodyError::invalid_coordinate_frame};
      return {};
    case RigidFrameKind::planet_fixed:
      if (!frame.planet || frame.station)
        return std::unexpected{RigidBodyError::invalid_coordinate_frame};
      if (!std::ranges::any_of(context.system.planets, [&](const auto& body) {
            return body.descriptor.id == *frame.planet;
          }))
        return std::unexpected{RigidBodyError::unknown_planet};
      return {};
    case RigidFrameKind::station_relative_inertial:
      if (!frame.station || frame.planet)
        return std::unexpected{RigidBodyError::invalid_coordinate_frame};
      if (context.station == nullptr || *frame.station != context.station->id)
        return std::unexpected{RigidBodyError::unknown_station};
      if (context.system.kind != LocalSystemKind::origin_home ||
          context.station->home_system_seed != context.system.seed ||
          !station_matches(
              *context.station,
              generate_origin_station(context.station->universe_seed)) ||
          !std::ranges::any_of(context.system.planets, [&](const auto& body) {
            return body.descriptor.id == context.station->orbit.host_planet;
          }))
        return std::unexpected{RigidBodyError::invalid_world_context};
      return {};
  }
  return std::unexpected{RigidBodyError::invalid_coordinate_frame};
}
auto vector_valid(RigidVector3 vector, double bound)
    -> std::expected<void, RigidBodyError> {
  for (const auto value : components(vector)) {
    if (!std::isfinite(value))
      return std::unexpected{RigidBodyError::non_finite_state};
    if (std::abs(value) > bound)
      return std::unexpected{RigidBodyError::excessive_magnitude};
    if (value == 0.0 && std::signbit(value))
      return std::unexpected{RigidBodyError::noncanonical_state};
  }
  return {};
}
auto decimal(double value) -> std::string {
  std::array<char, 64> buffer{};
  const auto encoded = std::to_chars(
      buffer.data(), buffer.data() + buffer.size(), value,
      std::chars_format::general, std::numeric_limits<double>::max_digits10);
  if (encoded.ec != std::errc{}) throw RigidBodyError::invalid_decimal;
  return {buffer.data(), encoded.ptr};
}
auto frame_kind_name(RigidFrameKind kind) -> std::string_view {
  switch (kind) {
    case RigidFrameKind::system_inertial: return "system_inertial";
    case RigidFrameKind::planet_fixed: return "planet_fixed";
    case RigidFrameKind::station_relative_inertial:
      return "station_relative_inertial";
  }
  throw RigidBodyError::invalid_coordinate_frame;
}
auto require_keys(const Json& object,
                  std::initializer_list<std::string_view> keys) -> void {
  if (!object.is_object()) throw RigidBodyError::invalid_type;
  for (const auto key : keys)
    if (!object.contains(key)) throw RigidBodyError::missing_field;
  for (auto it = object.begin(); it != object.end(); ++it)
    if (std::ranges::find(keys, it.key()) == keys.end())
      throw RigidBodyError::unknown_field;
}
auto text(const Json& value) -> const std::string& {
  if (!value.is_string()) throw RigidBodyError::invalid_type;
  const auto& result = value.get_ref<const std::string&>();
  if (result.empty() || result.size() > 64)
    throw RigidBodyError::invalid_decimal;
  return result;
}
auto read_u64(const Json& value) -> std::uint64_t {
  const auto& source = text(value);
  std::uint64_t result{};
  const auto parsed =
      std::from_chars(source.data(), source.data() + source.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != source.data() + source.size() ||
      std::to_string(result) != source)
    throw RigidBodyError::invalid_decimal;
  return result;
}
auto read_version(const Json& value) -> std::uint32_t {
  if (!value.is_number_unsigned() && !value.is_number_integer())
    throw RigidBodyError::invalid_type;
  if (value.is_number_integer() && !value.is_number_unsigned() &&
      value.get<std::int64_t>() < 0)
    throw RigidBodyError::unsupported_version;
  const auto version = value.get<std::uint64_t>();
  if (version > std::numeric_limits<std::uint32_t>::max())
    throw RigidBodyError::unsupported_version;
  return static_cast<std::uint32_t>(version);
}
auto read_double(const Json& value) -> double {
  const auto& source = text(value);
  double result{};
  const auto parsed =
      std::from_chars(source.data(), source.data() + source.size(), result,
                      std::chars_format::general);
  if (parsed.ec != std::errc{} || parsed.ptr != source.data() + source.size() ||
      !std::isfinite(result) || decimal(result) != source)
    throw RigidBodyError::invalid_decimal;
  return result;
}
template <std::size_t Count>
auto read_doubles(const Json& values) -> std::array<double, Count> {
  if (!values.is_array() || values.size() != Count)
    throw RigidBodyError::invalid_type;
  std::array<double, Count> result{};
  for (std::size_t i = 0; i < Count; ++i)
    result[i] = read_double(values[i]);
  return result;
}
auto read_vector(const Json& values) -> RigidVector3 {
  const auto v = read_doubles<3>(values);
  return {v[0], v[1], v[2]};
}
auto vector_json(RigidVector3 vector) -> Json {
  return Json::array({decimal(vector.x), decimal(vector.y), decimal(vector.z)});
}
} // namespace

auto normalize_rigid_orientation(RigidOrientation orientation)
    -> std::expected<RigidOrientation, RigidBodyError> {
  for (const auto value : components(orientation)) {
    if (!std::isfinite(value))
      return std::unexpected{RigidBodyError::non_finite_state};
    if (std::abs(value) > 2.0)
      return std::unexpected{RigidBodyError::invalid_orientation};
  }
  const double norm2 = squared_norm(orientation);
  if (norm2 < 0.5 || norm2 > 2.0)
    return std::unexpected{RigidBodyError::invalid_orientation};
  const double norm = std::sqrt(norm2);
  orientation.w /= norm;
  orientation.x /= norm;
  orientation.y /= norm;
  orientation.z /= norm;
  canonicalize_sign(orientation);
  if (const auto valid = validate_orientation(orientation); !valid)
    return std::unexpected{valid.error()};
  return orientation;
}

auto canonicalize_rigid_body_state(const RigidBodyWorldContext& context,
                                   RigidBodyState candidate)
    -> std::expected<RigidBodyState, RigidBodyError> {
  canonicalize_sign(candidate.orientation);
  canonicalize_zeros(candidate.position_metres);
  canonicalize_zeros(candidate.linear_velocity_metres_per_second);
  canonicalize_zeros(candidate.angular_velocity_radians_per_second);
  if (const auto valid = validate_rigid_body_state(context, candidate); !valid)
    return std::unexpected{valid.error()};
  return candidate;
}

auto validate_rigid_body_state(const RigidBodyWorldContext& context,
                               const RigidBodyState& state)
    -> std::expected<void, RigidBodyError> {
  if (!resolve_craft_frame(state.craft))
    return std::unexpected{RigidBodyError::invalid_craft_frame};
  if (const auto frame = validate_frame(context, state.frame); !frame)
    return frame;
  if (state.tick == std::numeric_limits<SimulationTick>::max())
    return std::unexpected{RigidBodyError::tick_overflow};
  for (const auto& [vector, bound] :
       {std::pair{state.position_metres, kRigidBodyMaximumPositionMetres},
        std::pair{state.linear_velocity_metres_per_second,
                  kRigidBodyMaximumVelocityMetresPerSecond},
        std::pair{state.angular_velocity_radians_per_second,
                  kRigidBodyMaximumAngularVelocityRadiansPerSecond}}) {
    if (const auto valid = vector_valid(vector, bound); !valid) return valid;
  }
  return validate_orientation(state.orientation);
}

auto rigid_body_state_checksum(const RigidBodyWorldContext& context,
                               const RigidBodyState& state)
    -> std::expected<std::uint64_t, RigidBodyError> {
  if (const auto valid = validate_rigid_body_state(context, state); !valid)
    return std::unexpected{valid.error()};
  std::uint64_t hash{14695981039346656037ULL};
  const auto integer = [&hash](std::uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) {
      hash ^= value & 255U;
      hash *= 1099511628211ULL;
      value >>= 8U;
    }
  };
  for (const char c : std::string_view{"apsis-rigid-body-v1"})
    integer(static_cast<unsigned char>(c), 1);
  integer(state.craft.id.value, 8);
  integer(state.craft.version, 4);
  integer(static_cast<std::uint8_t>(state.frame.kind), 1);
  integer(state.frame.system.value, 8);
  integer(state.frame.planet.has_value() ? 1U : 0U, 1);
  integer(state.frame.planet ? state.frame.planet->value : 0U, 8);
  integer(state.frame.station.has_value() ? 1U : 0U, 1);
  integer(state.frame.station ? state.frame.station->value : 0U, 8);
  integer(state.tick, 8);
  for (const auto value : components(state.position_metres))
    integer(std::bit_cast<std::uint64_t>(value), 8);
  for (const auto value : components(state.orientation))
    integer(std::bit_cast<std::uint64_t>(value), 8);
  for (const auto vector : {state.linear_velocity_metres_per_second,
                            state.angular_velocity_radians_per_second})
    for (const auto value : components(vector))
      integer(std::bit_cast<std::uint64_t>(value), 8);
  return hash;
}

auto encode_rigid_body_state_json(const RigidBodyWorldContext& context,
                                  const RigidBodyState& state)
    -> std::expected<std::string, RigidBodyError> {
  if (const auto valid = validate_rigid_body_state(context, state); !valid)
    return std::unexpected{valid.error()};
  try {
    const Json planet = state.frame.planet
                            ? Json(std::to_string(state.frame.planet->value))
                            : Json(nullptr);
    const Json station = state.frame.station
                             ? Json(std::to_string(state.frame.station->value))
                             : Json(nullptr);
    const Json result{
        {"format", format_name},
        {"version", kRigidBodyStateVersion},
        {"craft",
         {{"id", std::to_string(state.craft.id.value)},
          {"version", state.craft.version}}},
        {"frame",
         {{"kind", frame_kind_name(state.frame.kind)},
          {"system", std::to_string(state.frame.system.value)},
          {"planet", planet},
          {"station", station}}},
        {"tick", std::to_string(state.tick)},
        {"position_metres", vector_json(state.position_metres)},
        {"orientation_wxyz",
         Json::array(
             {decimal(state.orientation.w), decimal(state.orientation.x),
              decimal(state.orientation.y), decimal(state.orientation.z)})},
        {"linear_velocity_metres_per_second",
         vector_json(state.linear_velocity_metres_per_second)},
        {"angular_velocity_radians_per_second",
         vector_json(state.angular_velocity_radians_per_second)}};
    auto encoded = result.dump();
    if (encoded.size() > kMaximumRigidBodyDocumentBytes)
      return std::unexpected{RigidBodyError::document_too_large};
    return encoded;
  } catch (RigidBodyError error) {
    return std::unexpected{error};
  }
}

auto decode_rigid_body_state_json(const RigidBodyWorldContext& context,
                                  std::string_view document)
    -> std::expected<RigidBodyState, RigidBodyError> {
  if (document.size() > kMaximumRigidBodyDocumentBytes)
    return std::unexpected{RigidBodyError::document_too_large};
  // The JSON parser treats raw NUL as an end-of-input sentinel. JSON forbids
  // that byte, so reject it explicitly instead of accepting a hidden suffix.
  if (document.find('\0') != std::string_view::npos)
    return std::unexpected{RigidBodyError::malformed_json};
  try {
    std::vector<std::set<std::string>> object_keys;
    const Json::parser_callback_t callback =
        [&](int depth, Json::parse_event_t event, Json& parsed) {
          if (depth > maximum_json_depth) throw RigidBodyError::malformed_json;
          if (event == Json::parse_event_t::object_start)
            object_keys.emplace_back();
          else if (event == Json::parse_event_t::key) {
            if (object_keys.empty() ||
                !object_keys.back().insert(parsed.get<std::string>()).second)
              throw RigidBodyError::duplicate_key;
          } else if (event == Json::parse_event_t::object_end)
            object_keys.pop_back();
          return true;
        };
    const Json data = Json::parse(document, callback);
    require_keys(data, {"format", "version", "craft", "frame", "tick",
                        "position_metres", "orientation_wxyz",
                        "linear_velocity_metres_per_second",
                        "angular_velocity_radians_per_second"});
    if (text(data.at("format")) != format_name ||
        read_version(data.at("version")) != kRigidBodyStateVersion)
      throw RigidBodyError::unsupported_version;
    const auto& craft = data.at("craft");
    require_keys(craft, {"id", "version"});
    const auto& frame = data.at("frame");
    require_keys(frame, {"kind", "system", "planet", "station"});
    RigidBodyState candidate;
    candidate.craft = {{read_u64(craft.at("id"))},
                       read_version(craft.at("version"))};
    const auto& kind = text(frame.at("kind"));
    if (kind == "system_inertial")
      candidate.frame.kind = RigidFrameKind::system_inertial;
    else if (kind == "planet_fixed")
      candidate.frame.kind = RigidFrameKind::planet_fixed;
    else if (kind == "station_relative_inertial")
      candidate.frame.kind = RigidFrameKind::station_relative_inertial;
    else
      throw RigidBodyError::invalid_coordinate_frame;
    candidate.frame.system = SystemId{read_u64(frame.at("system"))};
    if (!frame.at("planet").is_null())
      candidate.frame.planet = PlanetId{read_u64(frame.at("planet"))};
    if (!frame.at("station").is_null())
      candidate.frame.station = OriginStationId{read_u64(frame.at("station"))};
    candidate.tick = read_u64(data.at("tick"));
    candidate.position_metres = read_vector(data.at("position_metres"));
    const auto q = read_doubles<4>(data.at("orientation_wxyz"));
    candidate.orientation = {q[0], q[1], q[2], q[3]};
    candidate.linear_velocity_metres_per_second =
        read_vector(data.at("linear_velocity_metres_per_second"));
    candidate.angular_velocity_radians_per_second =
        read_vector(data.at("angular_velocity_radians_per_second"));
    // Never normalize or canonicalize during hydration: preserve exact state or
    // refuse it. Assignment to the live session remains the caller's commit.
    if (const auto valid = validate_rigid_body_state(context, candidate);
        !valid)
      return std::unexpected{valid.error()};
    return candidate;
  } catch (RigidBodyError error) {
    return std::unexpected{error};
  } catch (const Json::exception&) {
    return std::unexpected{RigidBodyError::malformed_json};
  }
}

} // namespace apsis_drift
