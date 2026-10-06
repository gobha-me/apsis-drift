#pragma once
// Private effective-source traversal for the historical reservations and the
// same-stance static checkpoint. No geometry is copied or caller-admitted.
#include "apsis_drift/origin_lower_cockpit_contact.hpp"
#include "origin_cabin_contact_internal.hpp"
#include <functional>

namespace apsis_drift::detail {
struct LowerCockpitEffectiveTriangle {
  const CabinContactObstacle* obstacle{};
  LowerCockpitTriangleKey key;
  std::uint32_t object{};
  std::string_view source_object;
  std::optional<std::uint32_t> evaluated_source_triangle;
};
struct LowerCockpitEffectiveVisit {
  std::size_t total_triangles{}, visited_triangles{};
  bool complete{};
};
struct LowerCockpitContactAccess {
  static auto data(const OriginLowerCockpitContact&)
      -> const OriginLowerCockpitContact::Data*;
};
// Source order: retained originals, additive halo, selected replacement.
// The stopping callback's triangle counts as visited. Total remains complete.
[[nodiscard]] auto visit_effective_lower_cockpit_contact(
    const OriginLowerCockpitContact&,
    const std::function<bool(const LowerCockpitEffectiveTriangle&)>&)
    -> std::expected<LowerCockpitEffectiveVisit, std::string>;
// Closed declared crop, with its unchanged lower-front notch. Bounds must be
// finite and ordered. A moved-from provider refuses, even for empty traversal.
[[nodiscard]] auto covers_lower_cockpit_bounds(const OriginLowerCockpitContact&,
                                               CabinContactBox)
    -> std::expected<bool, std::string>;
} // namespace apsis_drift::detail

namespace apsis_drift::detail {
enum class LowerCockpitHaloVisitCondition : std::uint8_t {
  none,
  metadata_capacity,
  triangle_capacity,
  callback_stopped
};
struct LowerCockpitHaloVisit {
  std::size_t total_metadata{}, metadata_examined{}, total_triangles{},
      visited_triangles{};
  bool metadata_complete{}, complete{};
  LowerCockpitHaloVisitCondition condition{};
  std::optional<std::size_t> next_metadata;
  std::optional<LowerCockpitTriangleKey> next_key;
};
// Narrow immutable HALO traversal: no original count/removal scan or
// replacement visit. Stopping callbacks count their actual face; capacity names
// NEXT work.
[[nodiscard]] auto visit_lower_cockpit_halo(
    const OriginLowerCockpitContact&, void* context,
    bool (*visit)(void*, const LowerCockpitEffectiveTriangle&),
    std::size_t max_metadata = 75, std::size_t max_triangles = 8100)
    -> std::expected<LowerCockpitHaloVisit, std::string>;
} // namespace apsis_drift::detail
