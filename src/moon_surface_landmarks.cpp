#include "apsis_drift/moon_surface_landmarks.hpp"

#include "apsis_drift/moon_surface_contact.hpp"

#include <algorithm>
#include <cmath>

namespace apsis_drift {
namespace {
using Error = SurfaceLandmarkError;
using Point = MoonFixedPositionMetres;
constexpr std::int32_t kCellMinimum{-32768}, kCellMaximum{32767};
constexpr double kMaximumQueryComponentMetres{1e12};
// Exceeds unchanged4545m relief plus contact chord/384m query range. This
// short-circuits distant space queries, never clamps the actual dry surface.
constexpr double kQueryShellMetres{8192};
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
auto validate_owner(const PhysicalMoonDescriptor& owner)
    -> std::expected<void, Error> {
  if (owner.body.version != kBodyIdentityVersion ||
      owner.body.kind != BodyKind::moon)
    return std::unexpected{Error::invalid_body};
  // The complete source-bound descriptor authenticates both procedural and
  // Origin catalogs; terrain/identity arithmetic below remains recipe1.
  if (!validate_physical_moon_descriptor(owner))
    return std::unexpected{Error::invalid_owner};
  return {};
}
} // namespace

auto identify_moon_surface_landmark(const PhysicalMoonDescriptor& owner,
                                    SurfaceLandmarkCell cell,
                                    std::uint32_t version)
    -> std::expected<std::optional<MoonSurfaceLandmarkIdentity>, Error> {
  if (version != kMoonSurfaceLandmarkVersion)
    return std::unexpected{Error::unsupported_version};
  if (const auto valid = validate_owner(owner); !valid)
    return std::unexpected{valid.error()};
  if (!valid_cell(cell)) return std::unexpected{Error::invalid_cell};
  const auto recipe = derive_seed(owner.terrain_seed,
                                  SeedDomain::moon_surface_landmark, version);
  const auto seed = derive_seed(recipe, SeedDomain::moon_surface_landmark,
                                cell_ordinal(cell));
  const auto child = [&](std::uint64_t ordinal) {
    return derive_seed(seed, SeedDomain::moon_surface_landmark, ordinal).value;
  };
  const Point candidate{
      static_cast<double>(cell.x) * kSurfaceLandmarkCellMetres + 128 +
          static_cast<double>(child(0) % 257),
      static_cast<double>(cell.y) * kSurfaceLandmarkCellMetres + 128 +
          static_cast<double>(child(1) % 257),
      static_cast<double>(cell.z) * kSurfaceLandmarkCellMetres + 128 +
          static_cast<double>(child(2) % 257)};
  const double length = std::hypot(candidate.x, candidate.y, candidate.z);
  if (!std::isfinite(length) || length <= 0)
    return std::unexpected{Error::unsafe_arithmetic};
  const double scale = owner.radius_metres / length;
  const Point reference{candidate.x * scale, candidate.y * scale,
                        candidate.z * scale};
  if (cell_at(reference) != cell)
    return std::optional<MoonSurfaceLandmarkIdentity>{};
  const auto kind = static_cast<SurfaceLandmarkKind>(child(3) % 3);
  constexpr std::array<Rgb8, 3> tints{Rgb8{230, 222, 183}, Rgb8{195, 106, 38},
                                      Rgb8{38, 45, 54}};
  return MoonSurfaceLandmarkIdentity{{seed.value},
                                     cell,
                                     kind,
                                     reference,
                                     28 + static_cast<double>(child(4) % 25),
                                     tints[static_cast<std::size_t>(kind)]};
}

auto MoonSurfaceLandmarkField::create(const PhysicalMoonDescriptor& owner,
                                      std::uint32_t terrain_recipe,
                                      std::uint32_t version)
    -> std::expected<MoonSurfaceLandmarkField, Error> {
  if (version != kMoonSurfaceLandmarkVersion)
    return std::unexpected{Error::unsupported_version};
  if (terrain_recipe != kMoonTerrainRecipeVersion)
    return std::unexpected{Error::invalid_policy};
  if (const auto valid = validate_owner(owner); !valid)
    return std::unexpected{valid.error()};
  auto sampler = MoonTerrainSampler::create(owner, terrain_recipe);
  if (!sampler) return std::unexpected{Error::terrain_failure};
  return MoonSurfaceLandmarkField{std::move(*sampler), version};
}

auto MoonSurfaceLandmarkField::build_site(SurfaceLandmarkCell cell)
    -> std::expected<std::optional<MoonSurfaceLandmarkSite>, Error> {
  const auto identity = identify_moon_surface_landmark(owner(), cell, version_);
  if (!identity) return std::unexpected{identity.error()};
  if (!*identity) return std::optional<MoonSurfaceLandmarkSite>{};
  const auto& p = (*identity)->reference_position_metres;
  const auto ground = query_moon_contact_surface(sampler_, {p.x, p.y, p.z});
  if (!ground) return std::unexpected{Error::terrain_failure};
  const auto length = std::hypot(p.x, p.y, p.z);
  const auto& n = ground->triangle.outward_normal;
  const double alignment =
      (n.x * (p.x / length) + n.y * (p.y / length)) + n.z * (p.z / length);
  const auto& actual = ground->position;
  if (!std::isfinite(alignment) || !std::isfinite(actual.x) ||
      !std::isfinite(actual.y) || !std::isfinite(actual.z) ||
      !std::isfinite(ground->radial_distance_metres) ||
      ground->radial_distance_metres <= 1 ||
      ground->radial_distance_metres > 1e8)
    return std::unexpected{Error::unsafe_arithmetic};
  if (alignment < kMinimumWalkingAlignment)
    return std::optional<MoonSurfaceLandmarkSite>{};
  // The exact authenticated Moon has no water; negative relief is dry rock.
  // This is the same actual top and slope admission as MoonSurfaceWalkTerrain.
  return MoonSurfaceLandmarkSite{**identity, actual};
}

auto MoonSurfaceLandmarkField::site(SurfaceLandmarkCell cell)
    -> std::expected<std::optional<MoonSurfaceLandmarkSite>, Error> {
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

auto MoonSurfaceLandmarkField::nearby(Point query)
    -> std::expected<std::vector<MoonSurfaceLandmarkSite>, Error> {
  if (!std::isfinite(query.x) || !std::isfinite(query.y) ||
      !std::isfinite(query.z) ||
      std::max({std::abs(query.x), std::abs(query.y), std::abs(query.z)}) >
          kMaximumQueryComponentMetres)
    return std::unexpected{Error::invalid_query};
  const double length = std::hypot(query.x, query.y, query.z);
  if (!std::isfinite(length) || length < 1)
    return std::unexpected{Error::invalid_query};
  const double radius = owner().radius_metres;
  if (std::abs(length - radius) > kQueryShellMetres)
    return std::vector<MoonSurfaceLandmarkSite>{};
  const double scale = radius / length;
  const auto cell =
      cell_at({query.x * scale, query.y * scale, query.z * scale});
  std::array<MoonSurfaceLandmarkSite, 27> candidates{};
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
  return std::vector<MoonSurfaceLandmarkSite>{
      candidates.begin(),
      candidates.begin() + static_cast<std::ptrdiff_t>(kept)};
}
} // namespace apsis_drift
