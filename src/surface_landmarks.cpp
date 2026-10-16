#include "apsis_drift/surface_landmarks.hpp"

#include "apsis_drift/godot/saved_contact_geometry.hpp"
#include "apsis_drift/terrain_sea_surface.hpp"
#include "godot/contact_geometry_internal.hpp"

#include <algorithm>
#include <cmath>

namespace apsis_drift {
namespace {
using Error = SurfaceLandmarkError;
using Point = PlanetFixedPositionMetres;
constexpr std::int32_t kCellMinimum{-32768}, kCellMaximum{32767};
constexpr double kMaximumQueryComponentMetres{1e12};
// Existing raw terrain is bounded below17070m (terrain_impact.cpp). Keep a
// conservative extra shell without making distant orbital queries sample it.
constexpr double kQueryShellMetres{32768};
constexpr double kMinimumWalkingAlignment{.8191520442889918};

auto valid_cell(SurfaceLandmarkCell c) -> bool {
  return c.x >= kCellMinimum && c.x <= kCellMaximum && c.y >= kCellMinimum &&
         c.y <= kCellMaximum && c.z >= kCellMinimum && c.z <= kCellMaximum;
}
auto cell_at(Point p) -> SurfaceLandmarkCell {
  return {
      static_cast<std::int32_t>(std::floor(p.x / kSurfaceLandmarkCellMetres)),
      static_cast<std::int32_t>(std::floor(p.y / kSurfaceLandmarkCellMetres)),
      static_cast<std::int32_t>(std::floor(p.z / kSurfaceLandmarkCellMetres))};
}
auto cell_ordinal(SurfaceLandmarkCell c) -> std::uint64_t {
  const auto coordinate = [](std::int32_t value) {
    return static_cast<std::uint64_t>(value - kCellMinimum);
  };
  return coordinate(c.x) | (coordinate(c.y) << 16U) | (coordinate(c.z) << 32U);
}
auto distance(Point a, Point b) -> double {
  return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}
auto dot(Point a, Point b) -> double {
  return (a.x * b.x + a.y * b.y) + a.z * b.z;
}
} // namespace

auto identify_surface_landmark(const PlanetDescriptor& planet,
                               SurfaceLandmarkCell cell, std::uint32_t version)
    -> std::expected<std::optional<SurfaceLandmarkIdentity>, Error> {
  if (version != kSurfaceLandmarkVersion)
    return std::unexpected{Error::unsupported_version};
  if (planet.radius.value < PlanetRadiusKm::min ||
      planet.radius.value > PlanetRadiusKm::max)
    return std::unexpected{Error::invalid_body};
  if (!valid_cell(cell)) return std::unexpected{Error::invalid_cell};
  // Independent version/cell/attribute children. No terrain or encounter
  // stream is consumed; rejection never changes another cell's attributes.
  const auto recipe_seed = derive_seed(
      planet.seed, SeedDomain::surface_landmark, kSurfaceLandmarkVersion);
  const auto seed = derive_seed(recipe_seed, SeedDomain::surface_landmark,
                                cell_ordinal(cell));
  const auto child = [&](std::uint64_t ordinal) {
    return derive_seed(seed, SeedDomain::surface_landmark, ordinal).value;
  };
  // Integer metre jitter lies in the closed middle half [128,384] of512m.
  const Point candidate{
      static_cast<double>(cell.x) * kSurfaceLandmarkCellMetres + 128 +
          static_cast<double>(child(0) % 257),
      static_cast<double>(cell.y) * kSurfaceLandmarkCellMetres + 128 +
          static_cast<double>(child(1) % 257),
      static_cast<double>(cell.z) * kSurfaceLandmarkCellMetres + 128 +
          static_cast<double>(child(2) % 257)};
  const auto magnitude = std::hypot(candidate.x, candidate.y, candidate.z);
  if (!std::isfinite(magnitude) || magnitude <= 0)
    return std::unexpected{Error::unsafe_arithmetic};
  const double radius = static_cast<double>(planet.radius.value) * 1000;
  const double scale = radius / magnitude;
  const Point reference{candidate.x * scale, candidate.y * scale,
                        candidate.z * scale};
  if (cell_at(reference) != cell)
    return std::optional<SurfaceLandmarkIdentity>{};
  const auto kind = static_cast<SurfaceLandmarkKind>(child(3) % 3);
  constexpr std::array<Rgb8, 3> tints{Rgb8{230, 222, 183}, Rgb8{195, 106, 38},
                                      Rgb8{38, 45, 54}};
  return SurfaceLandmarkIdentity{{seed.value},
                                 cell,
                                 kind,
                                 reference,
                                 28 + static_cast<double>(child(4) % 25),
                                 tints[static_cast<std::size_t>(kind)]};
}

auto SurfaceLandmarkField::create(const PhysicalLocalSystem& owner, BodyId body,
                                  std::uint32_t terrain_policy,
                                  std::uint32_t version)
    -> std::expected<SurfaceLandmarkField, Error> {
  if (version != kSurfaceLandmarkVersion)
    return std::unexpected{Error::unsupported_version};
  if (!godot_spike::saved_contact_recipe(terrain_policy))
    return std::unexpected{Error::invalid_policy};
  if (!validate_local_system(owner))
    return std::unexpected{Error::invalid_owner};
  const auto id = planet_id(body);
  if (!id) return std::unexpected{Error::invalid_body};
  const auto found = std::ranges::find_if(
      owner.catalog.planets,
      [&](const LocalSystemPlanet& p) { return p.descriptor.id == *id; });
  if (found == owner.catalog.planets.end())
    return std::unexpected{Error::invalid_body};
  auto cache = TerrainTileCache::create(4);
  if (!cache) return std::unexpected{Error::terrain_failure};
  return SurfaceLandmarkField{
      owner, static_cast<std::size_t>(found - owner.catalog.planets.begin()),
      terrain_policy, version, std::move(*cache)};
}

auto SurfaceLandmarkField::build_site(SurfaceLandmarkCell cell)
    -> std::expected<std::optional<SurfaceLandmarkSite>, Error> {
  const auto& planet = owner_.catalog.planets[planet_index_].descriptor;
  const auto identity = identify_surface_landmark(planet, cell, version_);
  if (!identity) return std::unexpected{identity.error()};
  if (!*identity) return std::optional<SurfaceLandmarkSite>{};
  const auto& reference = (*identity)->reference_position_metres;
  const auto recipe = godot_spike::saved_contact_recipe(terrain_policy_);
  if (!recipe) return std::unexpected{Error::invalid_policy};
  const auto ground = godot_spike::detail::query_selected_contact_surface(
      owner_, planet, *recipe, {reference.x, reference.y, reference.z}, cache_);
  if (!ground) return std::unexpected{Error::terrain_failure};
  if (terrain_policy_ == kSeaDatumTerrainPolicy) {
    const auto datum = classify_terrain_datum_mask(ground->triangle.datum_mask);
    if (!datum) return std::unexpected{Error::terrain_failure};
    if (*datum != TerrainDatumClass::dry)
      return std::optional<SurfaceLandmarkSite>{};
  }
  const auto length = std::hypot(reference.x, reference.y, reference.z);
  const Point up{reference.x / length, reference.y / length,
                 reference.z / length};
  const auto& n = ground->triangle.outward_normal;
  const auto alignment = dot({n.x, n.y, n.z}, up);
  const double radius = static_cast<double>(planet.radius.value) * 1000;
  if (!std::isfinite(alignment))
    return std::unexpected{Error::unsafe_arithmetic};
  if (alignment < kMinimumWalkingAlignment ||
      ground->radial_distance_metres <= radius)
    return std::optional<SurfaceLandmarkSite>{};
  // Match the walker's dry-facet test, including the shared triangle's chord.
  const auto& first = ground->triangle.vertices.front();
  const auto first_length = std::hypot(first.x, first.y, first.z);
  const Point radial{first.x / first_length, first.y / first_length,
                     first.z / first_length};
  for (const auto& vertex : ground->triangle.vertices)
    if (dot(radial, vertex) <= radius + .0001)
      return std::optional<SurfaceLandmarkSite>{};
  return SurfaceLandmarkSite{**identity, ground->position};
}

auto SurfaceLandmarkField::site(SurfaceLandmarkCell cell)
    -> std::expected<std::optional<SurfaceLandmarkSite>, Error> {
  if (!valid_cell(cell)) return std::unexpected{Error::invalid_cell};
  for (const auto& memo : memo_)
    if (memo.used && memo.cell == cell) return memo.result;
  const auto result = build_site(cell);
  if (!result) return std::unexpected{result.error()};
  memo_[next_memo_] = {true, cell, *result};
  next_memo_ = (next_memo_ + 1) % memo_.size();
  memo_count_ = std::min(memo_count_ + 1, memo_.size());
  return *result;
}

auto SurfaceLandmarkField::nearby(Point query)
    -> std::expected<std::vector<SurfaceLandmarkSite>, Error> {
  if (!std::isfinite(query.x) || !std::isfinite(query.y) ||
      !std::isfinite(query.z) ||
      std::max({std::abs(query.x), std::abs(query.y), std::abs(query.z)}) >
          kMaximumQueryComponentMetres)
    return std::unexpected{Error::invalid_query};
  const auto length = std::hypot(query.x, query.y, query.z);
  if (!std::isfinite(length) || length < 1)
    return std::unexpected{Error::invalid_query};
  const auto& planet = owner_.catalog.planets[planet_index_].descriptor;
  const double radius = static_cast<double>(planet.radius.value) * 1000;
  if (std::abs(length - radius) > kQueryShellMetres)
    return std::vector<SurfaceLandmarkSite>{};
  const double scale = radius / length;
  const auto cell =
      cell_at({query.x * scale, query.y * scale, query.z * scale});
  std::array<SurfaceLandmarkSite, 27> candidates{};
  std::size_t count{};
  for (std::int32_t x = -1; x <= 1; ++x)
    for (std::int32_t y = -1; y <= 1; ++y)
      for (std::int32_t z = -1; z <= 1; ++z) {
        const auto found = site({cell.x + x, cell.y + y, cell.z + z});
        if (!found) return std::unexpected{found.error()};
        if (*found && distance((*found)->position_metres, query) <=
                          kSurfaceLandmarkNearbyMetres)
          candidates[count++] = **found;
      }
  const auto end = candidates.begin() + static_cast<std::ptrdiff_t>(count);
  std::sort(candidates.begin(), end, [&](const auto& a, const auto& b) {
    const auto da = distance(a.position_metres, query),
               db = distance(b.position_metres, query);
    return da < db || (da == db && a.identity.id < b.identity.id);
  });
  const auto kept = std::min(count, kMaximumNearbySurfaceLandmarks);
  return std::vector<SurfaceLandmarkSite>{
      candidates.begin(),
      candidates.begin() + static_cast<std::ptrdiff_t>(kept)};
}
} // namespace apsis_drift
