#include "apsis_drift/operating_motion.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

#include <nlohmann/json.hpp>

#include "apsis_drift/operating_motion_recipe.hpp"

namespace apsis_drift {
namespace {
using Json = nlohmann::json;
constexpr double source_tolerance{5e-6};
constexpr std::string_view schema{"apsis.operating-motion/1"};
constexpr std::string_view layout{
    "columns basis_x/basis_y/basis_z/origin; metres; XYZ radians"};
constexpr std::array<std::string_view, 7> identity_values{
    "87f4a1f0c584223aaec9b902f236ca9a9ea53924ae7bce4413cf150f61bad677",
    "6a3d4cf56af54b8b4d1cc1e344f32651609022280f1c3fb0a9109bf86dfa4fb6",
    "6a12e1e6846be4de6c89ce0c65b154567a8a07818d37bf574de2a319109319b4",
    "a9a8104a0ea8b5c22e4149861a76b5ab3911a9f77ba871b446c08bf9ed56621c",
    "79c8303ebe362a619f58a931cf97102cf63cd7bdd36f46a86152368107e0be80",
    "109e3f140f6865612118b2712021a71c0732200f6adc14ac2d4a1ce1d749657a",
    "662287856666bc9f3bffdd6ccda3fce2a2c2b55fd393998f4cf65597c3ffeada"};
constexpr std::array<std::string_view, 7> identity_keys{
    "craft_source_sha256",   "station_source_sha256", "closure_source_sha256",
    "craft_model_sha256",    "station_model_sha256",  "contact_sha256",
    "station_closure_sha256"};

auto add(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
auto scale(RigidVector3 value, double amount) -> RigidVector3 {
  return {value.x * amount, value.y * amount, value.z * amount};
}
auto dot(RigidVector3 a, RigidVector3 b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
auto cross(RigidVector3 a, RigidVector3 b) -> RigidVector3 {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
auto finite(RigidVector3 value, double bound = 128) -> bool {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z) && std::abs(value.x) <= bound &&
         std::abs(value.y) <= bound && std::abs(value.z) <= bound;
}
auto component(RigidVector3 value, std::size_t axis) -> double {
  return axis == 0 ? value.x : axis == 1 ? value.y : value.z;
}
auto assign(RigidVector3& value, std::size_t axis, double number) -> void {
  if (axis == 0)
    value.x = number;
  else if (axis == 1)
    value.y = number;
  else
    value.z = number;
}
auto rotate(const OperatingTransform& transform, RigidVector3 value)
    -> RigidVector3 {
  return add(add(scale(transform.columns[0], value.x),
                 scale(transform.columns[1], value.y)),
             scale(transform.columns[2], value.z));
}
auto compose(const OperatingTransform& parent, const OperatingTransform& local)
    -> OperatingTransform {
  return {{rotate(parent, local.columns[0]), rotate(parent, local.columns[1]),
           rotate(parent, local.columns[2]),
           add(rotate(parent, local.columns[3]), parent.columns[3])}};
}
auto inverse(const OperatingTransform& value) -> OperatingTransform {
  const auto& c = value.columns;
  OperatingTransform result{{RigidVector3{c[0].x, c[1].x, c[2].x},
                             RigidVector3{c[0].y, c[1].y, c[2].y},
                             RigidVector3{c[0].z, c[1].z, c[2].z},
                             {}}};
  result.columns[3] = scale(rotate(result, c[3]), -1);
  return result;
}
auto local(RigidVector3 position, RigidVector3 angle) -> OperatingTransform {
  const auto cx = std::cos(angle.x), sx = std::sin(angle.x);
  const auto cy = std::cos(angle.y), sy = std::sin(angle.y);
  const auto cz = std::cos(angle.z), sz = std::sin(angle.z);
  // Active XYZ Euler: Rz * Ry * Rx, with source local translation.
  return {
      {RigidVector3{cz * cy, sz * cy, -sy},
       RigidVector3{cz * sy * sx - sz * cx, sz * sy * sx + cz * cx, cy * sx},
       RigidVector3{cz * sy * cx + sz * sx, sz * sy * cx - cz * sx, cy * cx},
       position}};
}
auto rigid(const OperatingTransform& value) -> bool {
  if (!std::ranges::all_of(value.columns,
                           [](RigidVector3 v) { return finite(v); }))
    return false;
  for (std::size_t i = 0; i < 3; ++i)
    for (std::size_t j = 0; j < 3; ++j)
      if (std::abs(dot(value.columns[i], value.columns[j]) -
                   (i == j ? 1.0 : 0.0)) > 1e-6)
        return false;
  return std::abs(
             dot(cross(value.columns[0], value.columns[1]), value.columns[2]) -
             1) <= 1e-6;
}
auto close(const OperatingTransform& a, const OperatingTransform& b) -> bool {
  for (std::size_t i = 0; i < 4; ++i)
    for (std::size_t axis = 0; axis < 3; ++axis)
      if (std::abs(component(a.columns[i], axis) -
                   component(b.columns[i], axis)) > source_tolerance)
        return false;
  return true;
}
auto object_keys(const Json& object,
                 std::initializer_list<std::string_view> keys) -> bool {
  return object.is_object() && object.size() == keys.size() &&
         std::ranges::all_of(keys, [&](std::string_view key) {
           return object.contains(std::string(key));
         });
}
auto number(const Json& value, double bound = 128) -> double {
  if (!value.is_number()) throw std::runtime_error("Operating scalar type");
  const auto result = value.get<double>();
  if (!std::isfinite(result) || std::abs(result) > bound)
    throw std::runtime_error("Operating scalar bound");
  return result;
}
auto index(const Json& value, std::size_t bound) -> std::size_t {
  if (!value.is_number_unsigned() || value.get<std::uint64_t>() >= bound)
    throw std::runtime_error("Operating index type/bound");
  return value.get<std::size_t>();
}
auto vector(const Json& value) -> RigidVector3 {
  if (!value.is_array() || value.size() != 3)
    throw std::runtime_error("Operating vector dimensions");
  return {number(value[0]), number(value[1]), number(value[2])};
}
auto transform(const Json& value) -> OperatingTransform {
  if (!value.is_array() || value.size() != 4)
    throw std::runtime_error("Operating transform dimensions");
  return {
      {vector(value[0]), vector(value[1]), vector(value[2]), vector(value[3])}};
}
auto channel(const Json& value) -> OperatingChannel {
  if (value == "roof_transfer") return OperatingChannel::roof_transfer;
  if (value == "inner_door") return OperatingChannel::inner_door;
  if (value == "seat_boarding") return OperatingChannel::seat_boarding;
  if (value == "station_closure") return OperatingChannel::station_closure;
  throw std::runtime_error("Unknown operating channel");
}
template <std::size_t N, std::size_t G>
auto section(const Json& value, std::array<OperatingMotionNode, N>& nodes,
             std::vector<OperatingMotionTrack>& tracks,
             const std::array<std::string_view, G>& ids,
             std::size_t track_count) -> void {
  if (!object_keys(value, {"nodes", "tracks"}) || !value["nodes"].is_array() ||
      value["nodes"].size() != N || !value["tracks"].is_array() ||
      value["tracks"].size() != track_count)
    throw std::runtime_error("Operating section dimensions/schema");
  for (std::size_t i = 0; i < N; ++i) {
    const auto& node = value["nodes"][i];
    if (!object_keys(node,
                     {"source_object", "parent", "group", "location_metres",
                      "euler_xyz_radians", "rest_world"}) ||
        !node["source_object"].is_string())
      throw std::runtime_error("Operating node schema");
    auto& result = nodes[i];
    result.source_object = node["source_object"].get<std::string>();
    if (!node["parent"].is_null()) result.parent = index(node["parent"], i);
    if (!node["group"].is_null()) {
      if (!node["group"].is_string())
        throw std::runtime_error("Operating group type");
      const auto found =
          std::ranges::find(ids, node["group"].get<std::string>());
      if (found == ids.end())
        throw std::runtime_error("Unknown operating group");
      result.output_group = static_cast<std::size_t>(found - ids.begin());
    }
    result.location_metres = vector(node["location_metres"]);
    result.euler_xyz_radians = vector(node["euler_xyz_radians"]);
    result.rest_world = transform(node["rest_world"]);
  }
  tracks.reserve(track_count);
  for (const auto& track : value["tracks"]) {
    if (!object_keys(track, {"channel", "node", "property", "axis", "knots"}) ||
        !track["knots"].is_array() || track["knots"].size() < 2 ||
        track["knots"].size() > 16)
      throw std::runtime_error("Operating track schema/knots");
    OperatingMotionTrack result;
    result.channel = channel(track["channel"]);
    result.node = index(track["node"], N);
    result.axis = index(track["axis"], 3);
    if (track["property"] == "location")
      result.property = OperatingProperty::location;
    else if (track["property"] == "rotation_euler")
      result.property = OperatingProperty::rotation_euler;
    else
      throw std::runtime_error("Unknown operating property");
    result.knots.reserve(track["knots"].size());
    for (const auto& knot : track["knots"]) {
      if (!knot.is_array() || knot.size() != 2)
        throw std::runtime_error("Operating knot dimensions");
      result.knots.push_back({number(knot[0], 1), number(knot[1])});
    }
    tracks.push_back(std::move(result));
  }
}
auto parse(std::string_view text)
    -> std::expected<OperatingMotionRecipe, std::string> {
  if (text.empty() || text.size() > kOperatingMotionMaximumDocumentBytes)
    return std::unexpected("Operating recipe byte limit");
  std::size_t depth{};
  bool quoted{}, escaped{};
  for (const auto character : text) {
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (character == '\\')
        escaped = true;
      else if (character == '"')
        quoted = false;
    } else if (character == '"')
      quoted = true;
    else if (character == '{' || character == '[') {
      if (++depth > 16)
        return std::unexpected("Operating recipe nesting limit");
    } else if (character == '}' || character == ']') {
      if (depth == 0)
        return std::unexpected("Operating recipe unbalanced nesting");
      --depth;
    }
  }
  try {
    bool duplicate{};
    std::vector<std::set<std::string>> keys;
    auto callback = [&](int, Json::parse_event_t event, Json& value) {
      if (event == Json::parse_event_t::object_start)
        keys.emplace_back();
      else if (event == Json::parse_event_t::object_end)
        keys.pop_back();
      else if (event == Json::parse_event_t::key && !keys.empty())
        duplicate =
            !keys.back().insert(value.get<std::string>()).second || duplicate;
      return true;
    };
    const auto json = Json::parse(text, callback);
    if (duplicate ||
        !object_keys(json, {"schema", "version", "matrix_layout", "identities",
                            "craft", "station"}) ||
        json["schema"] != schema || json["matrix_layout"] != layout ||
        !json["version"].is_number_unsigned() || json["version"] != 1 ||
        !object_keys(json["identities"],
                     {"craft_source_sha256", "station_source_sha256",
                      "closure_source_sha256", "craft_model_sha256",
                      "station_model_sha256", "contact_sha256",
                      "station_closure_sha256"}))
      return std::unexpected("Operating recipe identity/schema mismatch");
    OperatingMotionRecipe result;
    auto& identities = json["identities"];
    for (std::size_t i = 0; i < identity_keys.size(); ++i)
      if (identities[std::string(identity_keys[i])] != identity_values[i])
        return std::unexpected(
            "Operating recipe source/model identity mismatch");
    result.craft_source_sha256 = identities["craft_source_sha256"];
    result.station_source_sha256 = identities["station_source_sha256"];
    result.closure_source_sha256 = identities["closure_source_sha256"];
    result.craft_model_sha256 = identities["craft_model_sha256"];
    result.station_model_sha256 = identities["station_model_sha256"];
    result.contact_sha256 = identities["contact_sha256"];
    result.station_closure_sha256 = identities["station_closure_sha256"];
    section(json["craft"], result.craft_nodes, result.craft_tracks,
            kOperatingCraftGroupIds, 12);
    section(json["station"], result.station_nodes, result.station_tracks,
            kOperatingStationGroupIds, 51);
    return result;
  } catch (const std::exception& error) {
    return std::unexpected(std::string("Operating recipe malformed: ") +
                           error.what());
  }
}
template <std::size_t N, std::size_t G>
auto structural(const std::array<OperatingMotionNode, N>& nodes,
                const std::vector<OperatingMotionTrack>& tracks,
                const std::array<std::string_view, G>& ids, bool station)
    -> std::expected<void, std::string> {
  std::set<std::string> source_objects;
  std::array<bool, G> outputs{};
  std::array<OperatingTransform, N> worlds;
  for (std::size_t i = 0; i < N; ++i) {
    const auto& node = nodes[i];
    if (node.source_object.empty() || node.source_object.size() > 160 ||
        !source_objects.insert(node.source_object).second ||
        (node.parent && *node.parent >= i) || !finite(node.location_metres) ||
        !finite(node.euler_xyz_radians, 6.4) || !rigid(node.rest_world))
      return std::unexpected("Operating node ancestry/finite/rest bounds");
    if (node.output_group) {
      if (*node.output_group >= ids.size() || outputs[*node.output_group])
        return std::unexpected("Operating group duplicate/bound");
      outputs[*node.output_group] = true;
    }
    const auto basis = local(node.location_metres, node.euler_xyz_radians);
    worlds[i] = node.parent ? compose(worlds[*node.parent], basis) : basis;
    if (!close(worlds[i], node.rest_world))
      return std::unexpected("Operating source rest composition mismatch");
  }
  if (!std::ranges::all_of(outputs, [](bool present) { return present; }))
    return std::unexpected("Operating missing group");
  if (tracks.size() != (station ? std::size_t{51} : std::size_t{12}))
    return std::unexpected("Operating track roster size");
  std::set<
      std::tuple<OperatingChannel, std::size_t, OperatingProperty, std::size_t>>
      targets;
  for (const auto& track : tracks) {
    const auto known_channel =
        track.channel == OperatingChannel::roof_transfer ||
        track.channel == OperatingChannel::inner_door ||
        track.channel == OperatingChannel::seat_boarding ||
        track.channel == OperatingChannel::station_closure;
    if (!known_channel ||
        station != (track.channel == OperatingChannel::station_closure) ||
        track.node >= N || track.axis >= 3 ||
        (track.property != OperatingProperty::location &&
         track.property != OperatingProperty::rotation_euler) ||
        !nodes[track.node].output_group || track.knots.size() < 2 ||
        track.knots.size() > 16 || track.knots.front().progress != 0 ||
        track.knots.back().progress != 1 ||
        !targets.emplace(track.channel, track.node, track.property, track.axis)
             .second)
      return std::unexpected("Operating track target/phase/domain mismatch");
    double previous{-1};
    for (const auto& knot : track.knots) {
      if (!std::isfinite(knot.progress) || !std::isfinite(knot.value) ||
          knot.progress < 0 || knot.progress > 1 || knot.progress <= previous ||
          std::abs(knot.value) >
              (track.property == OperatingProperty::location ? 128 : 6.4))
        return std::unexpected("Operating knot finite/order/phase bounds");
      previous = knot.progress;
    }
    const auto& node = nodes[track.node];
    const auto rest = track.property == OperatingProperty::location
                          ? node.location_metres
                          : node.euler_xyz_radians;
    if (std::abs(track.knots.front().value - component(rest, track.axis)) >
        source_tolerance)
      return std::unexpected("Operating first knot/source rest mismatch");
  }
  return {};
}
auto approved() -> const std::expected<OperatingMotionRecipe, std::string>& {
  // This is the repository-selected build input, never a receipt-supplied file.
  static const auto result =
      []() -> std::expected<OperatingMotionRecipe, std::string> {
    auto recipe = parse(detail::kOperatingMotionRecipeJson);
    if (!recipe) return recipe;
    if (const auto valid = structural(recipe->craft_nodes, recipe->craft_tracks,
                                      kOperatingCraftGroupIds, false);
        !valid)
      return std::unexpected(valid.error());
    if (const auto valid =
            structural(recipe->station_nodes, recipe->station_tracks,
                       kOperatingStationGroupIds, true);
        !valid)
      return std::unexpected(valid.error());
    return recipe;
  }();
  return result;
}
auto scalar(const OperatingMotionTrack& track, double progress) -> double {
  for (std::size_t i = 1; i < track.knots.size(); ++i) {
    const auto& a = track.knots[i - 1];
    const auto& b = track.knots[i];
    if (progress <= b.progress) {
      if (progress == a.progress) return a.value;
      if (progress == b.progress) return b.value;
      return a.value + (b.value - a.value) * ((progress - a.progress) /
                                              (b.progress - a.progress));
    }
  }
  return track.knots.back().value;
}
auto progress_for(OperatingChannel channel, const OperatingProgress& progress)
    -> double {
  switch (channel) {
    case OperatingChannel::roof_transfer: return progress.roof_transfer;
    case OperatingChannel::inner_door: return progress.inner_door;
    case OperatingChannel::seat_boarding: return progress.seat_boarding;
    case OperatingChannel::station_closure: return progress.station_closure;
  }
  return 0; // Validated roster contains only the four named channels.
}
constexpr OperatingTransform convert{
    {RigidVector3{1, 0, 0}, RigidVector3{0, 0, -1}, RigidVector3{0, 1, 0}, {}}};
constexpr OperatingTransform station_convert{
    {RigidVector3{1, 0, 0}, RigidVector3{0, 0, -1}, RigidVector3{0, 1, 0},
     RigidVector3{-.97, 0, .978}}};
template <std::size_t N, std::size_t G>
auto evaluate_section(const std::array<OperatingMotionNode, N>& nodes,
                      const std::vector<OperatingMotionTrack>& tracks,
                      const OperatingProgress& progress,
                      std::array<OperatingTransform, G>& deltas,
                      std::array<OperatingTransform, G>* native_local,
                      const OperatingTransform& conversion) -> void {
  std::array<OperatingTransform, N> worlds;
  for (std::size_t i = 0; i < N; ++i) {
    const auto& node = nodes[i];
    auto position = node.location_metres;
    auto angle = node.euler_xyz_radians;
    for (const auto& track : tracks)
      if (track.node == i)
        assign(track.property == OperatingProperty::location ? position : angle,
               track.axis,
               scalar(track, progress_for(track.channel, progress)));
    const auto basis = local(position, angle);
    worlds[i] = node.parent ? compose(worlds[*node.parent], basis) : basis;
    if (!node.output_group) continue;
    const auto group = *node.output_group;
    deltas[group] = compose(
        compose(compose(conversion, worlds[i]), inverse(node.rest_world)),
        inverse(conversion));
    if (native_local)
      (*native_local)[group] =
          compose(compose(convert, basis), inverse(convert));
  }
}
} // namespace

auto decode_operating_motion_recipe(std::string_view text)
    -> std::expected<OperatingMotionRecipe, std::string> {
  auto recipe = parse(text);
  if (!recipe) return recipe;
  if (const auto valid = validate_operating_motion_recipe(*recipe); !valid)
    return std::unexpected(valid.error());
  return recipe;
}
auto validate_operating_motion_recipe(const OperatingMotionRecipe& recipe)
    -> std::expected<void, std::string> {
  if (recipe.version != kOperatingMotionRecipeVersion ||
      std::array<std::string_view, 7>{
          recipe.craft_source_sha256, recipe.station_source_sha256,
          recipe.closure_source_sha256, recipe.craft_model_sha256,
          recipe.station_model_sha256, recipe.contact_sha256,
          recipe.station_closure_sha256} != identity_values)
    return std::unexpected(
        "Operating recipe version/source/model identity mismatch");
  if (const auto valid = structural(recipe.craft_nodes, recipe.craft_tracks,
                                    kOperatingCraftGroupIds, false);
      !valid)
    return valid;
  if (const auto valid = structural(recipe.station_nodes, recipe.station_tracks,
                                    kOperatingStationGroupIds, true);
      !valid)
    return valid;
  const auto& reference = approved();
  if (!reference) return std::unexpected(reference.error());
  if (recipe != *reference)
    return std::unexpected(
        "Operating source roster/rest/approved scalar phases differ");
  return {};
}
auto evaluate_operating_motion(const OperatingMotionRecipe& recipe,
                               const OperatingProgress& progress)
    -> std::expected<OperatingPose, std::string> {
  for (const auto value : {progress.roof_transfer, progress.inner_door,
                           progress.seat_boarding, progress.station_closure})
    if (!std::isfinite(value) || value < 0 || value > 1)
      return std::unexpected("Operating progress must be finite in [0,1]");
  if (const auto valid = validate_operating_motion_recipe(recipe); !valid)
    return std::unexpected(valid.error());
  OperatingPose result;
  evaluate_section(recipe.craft_nodes, recipe.craft_tracks, progress,
                   result.craft_world_deltas,
                   static_cast<std::array<OperatingTransform, 13>*>(nullptr),
                   convert);
  evaluate_section(recipe.station_nodes, recipe.station_tracks, progress,
                   result.station_contact_deltas, &result.station_node_local,
                   station_convert);
  const auto valid = [](const auto& transforms) {
    return std::ranges::all_of(transforms,
                               [](const auto& value) { return rigid(value); });
  };
  if (!valid(result.craft_world_deltas) || !valid(result.station_node_local) ||
      !valid(result.station_contact_deltas))
    return std::unexpected("Operating evaluated transform finite/rigid bound");
  return result;
}
} // namespace apsis_drift
