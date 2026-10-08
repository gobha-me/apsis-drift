#include "origin_boarding_intermediate_pause_support_internal.hpp"
#include "origin_boarding_route_intermediate_step02_internal.hpp"
#include "origin_lower_cockpit_contact_internal.hpp"
#include <algorithm>
#include <cmath>
#include <new>
namespace apsis_drift {
namespace {
using Access = detail::BoardingIntermediatePauseSupportAccess;
using CtorLimits = detail::BoardingIntermediatePauseConstructorLimits;
using CtorEvidence = BoardingIntermediatePauseConstructorEvidence;
using Limits = detail::BoardingIntermediatePauseLimits;
using Diagnostic = BoardingIntermediatePauseSupportDiagnostic;
using Expected = std::expected<Diagnostic, std::string>;
using Why = BoardingIntermediatePauseCondition;
using Part = BoardingBodyPartId;
using PointId = BoardingPlantedBodyPointId;
constexpr auto port_name =
    std::string_view{"CRAFT | pilot transition intermediate step"};
constexpr auto star_name = std::string_view{"CABIN | cockpit transition step"};
constexpr double port_plane = static_cast<double>(-230000) * 1e-6;
constexpr double star_plane = static_cast<double>(-160000) * 1e-6;
auto coordinate(RigidVector3 v, std::size_t axis) -> double {
  return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}
auto finite(double x) -> bool {
  return std::isfinite(x) && std::abs(x) <= 8;
}
auto ctor_fail(CtorEvidence& e, Why w) -> bool {
  if (e.condition == Why::none) e.condition = w;
  return false;
}
auto base_charge(CtorEvidence& e, const CtorLimits& l) -> bool {
  if (e.work.base_guards >= l.base_guards)
    return ctor_fail(e, Why::base_capacity);
  ++e.work.base_guards;
  return true;
}
auto valid_limits(const CtorLimits& l) -> bool {
  const CtorLimits d;
  return l.source_bytes <= d.source_bytes && l.base_guards <= d.base_guards &&
         l.metadata_rows <= d.metadata_rows && l.face_reads <= d.face_reads &&
         l.quad_records <= d.quad_records && l.quad_edges <= d.quad_edges &&
         l.quad_sides <= d.quad_sides && l.face_vertices <= d.face_vertices &&
         l.corner_matches <= d.corner_matches &&
         l.face_windings <= d.face_windings && l.incidence <= d.incidence &&
         l.diagonals <= d.diagonals;
}
auto valid_limits(const Limits& l) -> bool {
  const Limits d;
  return l.phase.graphs <= d.phase.graphs && l.phase.legs <= d.phase.legs &&
         l.phase.bodies <= d.phase.bodies &&
         l.phase.sectors <= d.phase.sectors &&
         l.phase.timing <= d.phase.timing &&
         l.projection_guards <= d.projection_guards &&
         l.pressure_candidates <= d.pressure_candidates &&
         l.disk_edges <= d.disk_edges && l.output_bytes <= d.output_bytes;
}
auto port_perimeter() -> std::array<RigidVector3, 4> {
  const auto p = [](int x, int z) {
    return RigidVector3{static_cast<double>(x) * 1e-6, port_plane,
                        static_cast<double>(z) * 1e-6};
  };
  return {p(550000, -1346840), p(-550000, -1346840), p(-550000, -1183160),
          p(550000, -1183160)};
}
auto star_perimeter() -> std::array<RigidVector3, 4> {
  const auto p = [](int x, int z) {
    return RigidVector3{static_cast<double>(x) * 1e-6, star_plane,
                        static_cast<double>(z) * 1e-6};
  };
  return {p(498841, -988000), p(-498841, -988000), p(-500881, -612000),
          p(500881, -612000)};
}
auto expected_identity(std::size_t side)
    -> BoardingIntermediatePauseSourceIdentity {
  BoardingIntermediatePauseSourceIdentity r;
  r.name = side == 0 ? port_name : star_name;
  r.object = side == 0 ? 10 : 124;
  if (side == 0) r.inventory_provenance = 735;
  r.plane = side == 0 ? port_plane : star_plane;
  for (std::size_t f = 0; f < 2; ++f) {
    r.keys[f] = {side == 0 ? LowerCockpitContactBuffer::halo
                           : LowerCockpitContactBuffer::original,
                 0, static_cast<std::uint32_t>((side == 0 ? 662 : 62119) + f)};
    if (side == 0) r.evaluated_faces[f] = static_cast<std::uint32_t>(182 + f);
  }
  return r;
}
auto identity(const BoardingBootSourcePartition& p,
              const BoardingIntermediatePauseSourceIdentity& i,
              std::size_t side, const CtorLimits& l, CtorEvidence& e) -> bool {
  e.side = side;
  e.identity_checked[side] = true;
  const auto required = expected_identity(side);
  if (!base_charge(e, l)) return false;
  bool good = i.name == required.name && i.object == required.object &&
              i.inventory_provenance == required.inventory_provenance &&
              i.keys == required.keys &&
              i.evaluated_faces == required.evaluated_faces;
  for (std::size_t f = 0; f < 2; ++f) {
    if (!base_charge(e, l)) return false;
    const auto& face = p.faces[f];
    good = good && face.key == required.keys[f] &&
           face.object == required.object &&
           face.source_object == required.name &&
           face.evaluated_source_triangle == required.evaluated_faces[f] &&
           finite(face.unit_normal_current.x) &&
           finite(face.unit_normal_current.y) &&
           finite(face.unit_normal_current.z) && face.unit_normal_current.y > 0;
  }
  if (!base_charge(e, l)) return false;
  good =
      good && i.plane == required.plane && p.plane_metres == required.plane &&
      p.perimeter_metres == (side == 0 ? port_perimeter() : star_perimeter());
  e.identity_valid[side] = good;
  if (!good) ctor_fail(e, Why::source_identity);
  return good;
}
auto fail(Diagnostic& d, Why why, std::optional<std::size_t> side = {})
    -> bool {
  if (why == Why::projection_capacity || why == Why::unsupported_arithmetic)
    d.stop_condition = why;
  if (!d.first_refusal) {
    BoardingIntermediatePauseRefusal r;
    r.condition = why;
    r.side = side;
    d.first_refusal = r;
  }
  return false;
}
auto projection_charge(Diagnostic& d, const Limits& l) -> bool {
  if (d.work.projection_guards >= l.projection_guards)
    return fail(d, Why::projection_capacity);
  ++d.work.projection_guards;
  return true;
}
auto projected_carrier(const BoardingPlantedBodyPointEvidence& p, Diagnostic& d,
                       const Limits& l) -> bool {
  // Six scalar endpoint checks per carrier value/velocity/acceleration. Jets
  // need only enclose zero: exact constancy comes from the authentic request.
  for (std::size_t kind = 0; kind < 3; ++kind) {
    const auto& b = kind == 0   ? p.value
                    : kind == 1 ? p.derivatives.velocity
                                : p.derivatives.acceleration;
    for (std::size_t axis = 0; axis < 3; ++axis) {
      if (!projection_charge(d, l)) return false;
      const auto low = coordinate(b.lower, axis);
      if (!finite(low) || (kind != 0 && low > 0))
        return fail(d, Why::projection_identity);
      if (!projection_charge(d, l)) return false;
      const auto high = coordinate(b.upper, axis);
      if (!finite(high) || low > high || (kind != 0 && high < 0))
        return fail(d, Why::projection_identity);
    }
  }
  return true;
}
auto same_constant(const BoardingRoutePhaseConstant& a,
                   const BoardingRoutePhaseConstant& b) -> bool {
  return a.count == b.count && a.terms == b.terms;
}
} // namespace
struct OriginBoardingIntermediatePauseSupport::Data {
  NativeCraftBinding binding;
  OriginBoardingBootSupport boots;
  const OriginLowerCockpitContact::Data* lower{};
  BoardingBootSourcePartition port;
  CtorEvidence summary;
  Data(const NativeCraftBinding& b, const OriginBoardingBootSupport& s)
      : binding(b), boots(s) {}
};
namespace {
// One stateless allocator-owned arena is the ACTUAL requested allocation.
// The rebound STL control block must fit before any allocation is attempted.
template <class T> struct PauseArenaAllocator {
  using value_type = T;
  PauseArenaAllocator() = default;
  template <class U> PauseArenaAllocator(const PauseArenaAllocator<U>&) {}
  [[nodiscard]] auto allocate(std::size_t n) -> T* {
    static_assert(alignof(T) <= alignof(std::max_align_t));
    if (n > 4096 / sizeof(T)) throw std::bad_alloc();
    return static_cast<T*>(::operator new(4096));
  }
  auto deallocate(T* pointer, std::size_t) -> void {
    ::operator delete(pointer);
  }
  template <class U>
  auto operator==(const PauseArenaAllocator<U>&) const -> bool {
    return true;
  }
};
constexpr std::size_t required_source_bytes = 4096;
static_assert(sizeof(OriginBoardingIntermediatePauseSupport::Data) <=
              required_source_bytes);
static_assert(required_source_bytes <= kBoardingIntermediatePauseSourceBytes);
static_assert(sizeof(Expected) <= kBoardingIntermediatePauseOutputBytes);
// Retain the WHOLE old compiler proof; the additional source/report/control
// records are not hidden in its old caller reserve. Pressure is staged later.
constexpr std::size_t consumer_source_bound =
    kBoardingRouteFootPhaseMaximumScratchBytes + 2 * sizeof(Expected) +
    required_source_bytes + 4 * sizeof(Limits) +
    sizeof(detail::BoardingRouteFootPhaseLimits) +
    sizeof(BoardingRouteFootPhaseRefusal) + 2048;
static_assert(consumer_source_bound <= kBoardingIntermediatePauseScratchBytes);
constexpr std::size_t constructor_source_bound = std::max(
    3 * sizeof(CtorEvidence) + 4 * sizeof(CtorLimits) +
        sizeof(BoardingBootSourcePartition) + 2048 + 1024,
    required_source_bytes + 2 * sizeof(CtorEvidence) + 4 * sizeof(CtorLimits) +
        sizeof(BoardingBootSourcePartition) + 80 + 1024);
static_assert(constructor_source_bound <=
              kBoardingIntermediatePauseConstructorScratchBytes);
[[gnu::noinline]] auto authenticate(const NativeCraftBinding& binding,
                                    const OriginBoardingBootSupport& boots,
                                    const CtorLimits& l, CtorEvidence& e,
                                    BoardingBootSourcePartition& port) -> bool {
  if (!base_charge(e, l)) return false;
  if (!detail::boarding_route_foot_phase_environment())
    return ctor_fail(e, Why::unsupported_arithmetic);
  e.arithmetic_supported = true;
  if (!base_charge(e, l)) return false;
  const auto* lower = binding.contact();
  const auto* old = boots.contact();
  if (!lower || !binding.selection() || !binding.pose())
    return ctor_fail(e, Why::invalid_binding);
  if (!base_charge(e, l)) return false;
  const auto& selection = *binding.selection();
  if (selection.version != 1 || selection.preset != "wayfarer-stowed-01" ||
      selection.hardware != OperatingProgress{1, 1, 1, 0})
    return ctor_fail(e, Why::invalid_binding);
  if (!base_charge(e, l)) return false;
  const auto partitions = boots.selected_partitions();
  if (!old || partitions.size() != 10)
    return ctor_fail(e, Why::invalid_binding);
  if (!base_charge(e, l)) return false;
  if (!lower->original_geometry() ||
      lower->original_geometry() != old->original_geometry())
    return ctor_fail(e, Why::invalid_binding);
  if (!base_charge(e, l)) return false;
  if (!lower->stowed_partition() ||
      lower->stowed_partition() != old->stowed_partition())
    return ctor_fail(e, Why::invalid_binding);
  if (!base_charge(e, l)) return false;
  if (detail::LowerCockpitContactAccess::data(*lower) !=
      detail::LowerCockpitContactAccess::data(*old))
    return ctor_fail(e, Why::invalid_binding);
  if (!base_charge(e, l)) return false;
  if (selection.operating_model_sha256 !=
          "a9a8104a0ea8b5c22e4149861a76b5ab3911a9f77ba871b446c08bf9ed56621c" ||
      selection.stowed_model_sha256 !=
          "d286dfd5174940ccd7e3db0cb023d3571ad460c6b22609c6f81208e16f06e2fc" ||
      selection.frame_sha256 !=
          "f98c50f69d71ecd38ca1d43d010f7b43ca300178dc25d40e170fd45fe5bc88a6" ||
      selection.contact_sha256 !=
          "62b4d493f2d74f59b81fd089873360b99e3bae9d17a6d90a00066e9440e44c4a")
    return ctor_fail(e, Why::invalid_binding);
  if (e.work.metadata_rows >= l.metadata_rows)
    return ctor_fail(e, Why::metadata_capacity);
  ++e.work.metadata_rows;
  if (!base_charge(e, l)) return false;
  const auto objects = lower->objects();
  if (objects.size() <= 10) return ctor_fail(e, Why::source_identity);
  const auto& row = objects[10];
  // original_object is deliberately not inventory735. The complete retained
  // evaluated roster is owned by the genuine lower handle; named reads only.
  if (row.source_object != port_name || row.triangle_start != 480 ||
      row.triangle_count != 188 || row.source_evaluated_triangles != 188 ||
      row.evaluated_source_triangles.size() != 188 ||
      row.evaluated_source_triangles[182] != 182 ||
      row.evaluated_source_triangles[183] != 183)
    return ctor_fail(e, Why::source_identity);
  port.plane_metres = port_plane;
  port.perimeter_metres = port_perimeter();
  for (std::size_t f = 0; f < 2; ++f) {
    if (e.work.face_reads >= l.face_reads)
      return ctor_fail(e, Why::face_capacity);
    ++e.work.face_reads;
    const auto face =
        lookup_lower_cockpit_face(*lower, expected_identity(0).keys[f]);
    if (!face) return ctor_fail(e, Why::source_identity);
    port.faces[f] = *face;
  }
  e.sources = {expected_identity(0), expected_identity(1)};
  if (!identity(port, e.sources[0], 0, l, e) ||
      !identity(partitions[9], e.sources[1], 1, l, e))
    return false;
  for (std::size_t side = 0; side < 2; ++side)
    if (!detail::intermediate_pause_quad_bridge(
            side == 0 ? port : partitions[9], l, e, side))
      return false;
  e.side.reset();
  return e.condition == Why::none;
}
[[gnu::noinline]] auto initialize(
    OriginBoardingIntermediatePauseSupport::Data& data, const CtorEvidence& e)
    -> void {
  data.summary = e;
  data.lower = detail::LowerCockpitContactAccess::data(*data.binding.contact());
}
[[gnu::noinline]] auto projection(
    const OriginBoardingIntermediatePauseSupport& provider,
    const BoardingRouteFootPhaseCell& c, Diagnostic& d, const Limits& l)
    -> bool {
  if (!projection_charge(d, l)) return false;
  const auto* data = Access::data(provider);
  if (!Access::valid(provider) || !data) return fail(d, Why::invalid_binding);
  for (const auto* flag :
       std::array{&c.complete, &c.arithmetic_supported, &c.nominal_links,
                  &c.target_sole_identities, &c.joint_sectors,
                  &c.derivative_domains, &c.timing_complete}) {
    if (!projection_charge(d, l)) return false;
    if (!*flag) return fail(d, Why::phase_prerequisite);
  }
  if (!projection_charge(d, l)) return false;
  if (c.first != 0) return fail(d, Why::projection_identity);
  if (!projection_charge(d, l)) return false;
  if (c.last != 1) return fail(d, Why::projection_identity);
  const auto& r = d.request;
  for (std::size_t a = 0; a < 3; ++a) {
    if (!projection_charge(d, l)) return false;
    if (!same_constant(r.root[0].coordinates[a], r.root[1].coordinates[a]))
      return fail(d, Why::projection_identity);
  }
  for (std::size_t side = 0; side < 2; ++side) {
    for (std::size_t a = 0; a < 3; ++a) {
      if (!projection_charge(d, l)) return false;
      if (!same_constant(r.feet[side].sole[0].coordinates[a],
                         r.feet[side].sole[1].coordinates[a]))
        return fail(d, Why::projection_identity, side);
    }
    if (!projection_charge(d, l)) return false;
    if (r.feet[side].yaw_half != std::array<double, 2>{0, 0})
      return fail(d, Why::projection_identity, side);
    if (!projection_charge(d, l)) return false;
    if (r.feet[side].swing_height_metres != 0)
      return fail(d, Why::projection_identity, side);
  }
  for (std::size_t check = 0; check < 4; ++check) {
    if (!projection_charge(d, l)) return false;
    const auto equal =
        check == 0   ? r.root_yaw_half[0] == r.root_yaw_half[1]
        : check == 1 ? r.torso_lean_half[0] == r.torso_lean_half[1]
        : check == 2
            ? r.port_reaction_fraction == std::array<double, 2>{.25, .25}
            : r.seconds_per_parameter == 2;
    if (!equal) return fail(d, Why::projection_identity);
  }
  const auto parts = detail::boarding_route_foot_phase_parts();
  for (std::size_t p = 0; p < parts.size(); ++p) {
    if (!projection_charge(d, l)) return false;
    if (static_cast<std::size_t>(parts[p].id) != p)
      return fail(d, Why::projection_identity);
  }
  for (std::size_t side = 0; side < 2; ++side) {
    const auto index = static_cast<std::size_t>(
        side == 0 ? Part::port_boot : Part::starboard_boot);
    if (!projection_charge(d, l)) return false;
    const auto* b =
        std::get_if<BoardingRoutePhaseBoxBinding>(&parts[index].reservation);
    if (!b) return fail(d, Why::projection_identity, side);
    if (!projection_charge(d, l)) return false;
    if (b->center != (side == 0 ? PointId::port_boot_center
                                : PointId::starboard_boot_center))
      return fail(d, Why::projection_identity, side);
    if (!projection_charge(d, l)) return false;
    if (b->half_size_metres != RigidVector3{.06, .05, .14})
      return fail(d, Why::projection_identity, side);
    if (!projection_charge(d, l)) return false;
    if (b->frame != (side == 0 ? BoardingRoutePhaseFrame::port_sole
                               : BoardingRoutePhaseFrame::starboard_sole))
      return fail(d, Why::projection_identity, side);
    if (!projection_charge(d, l)) return false;
    const auto* source = Access::partition(provider, side);
    const auto& y = r.feet[side].sole[0].coordinates[1];
    if (!source || y.count != 1 ||
        y.terms != std::array<double, 3>{source->plane_metres, 0, 0})
      return fail(d, Why::sole_plane, side);
    d.source_planes[side] = source->plane_metres;
    d.sites[side].plane_identity = true;
  }
  if (!projection_charge(d, l)) return false;
  if (c.port_reaction_fraction.lower != .25 ||
      c.port_reaction_fraction.upper != .25)
    return fail(d, Why::projection_identity);
  for (const auto* carrier : std::array{
           &c.center_of_mass,
           &c.points[static_cast<std::size_t>(PointId::port_boot_center)],
           &c.points[static_cast<std::size_t>(PointId::starboard_boot_center)],
           &c.points[static_cast<std::size_t>(PointId::port_ankle)],
           &c.points[static_cast<std::size_t>(PointId::starboard_ankle)]})
    if (!projected_carrier(*carrier, d, l)) return false;
  for (std::size_t side = 0; side < 2; ++side)
    for (const auto& column : c.frames[side + 2].columns)
      if (!projected_carrier(column, d, l)) return false;
  d.constant_state = d.projection_complete = true;
  return true;
}
} // namespace
OriginBoardingIntermediatePauseSupport::OriginBoardingIntermediatePauseSupport(
    std::shared_ptr<const Data> d)
    : data_(std::move(d)) {
}
auto OriginBoardingIntermediatePauseSupport::summary() const
    -> const CtorEvidence* {
  return data_ ? &data_->summary : nullptr;
}
auto Access::data(const OriginBoardingIntermediatePauseSupport& p)
    -> const OriginBoardingIntermediatePauseSupport::Data* {
  return p.data_.get();
}
auto Access::valid(const OriginBoardingIntermediatePauseSupport& p) -> bool {
  const auto* d = data(p);
  if (!d || !d->summary.complete || d->summary.version != 1) return false;
  const auto* a = d->binding.contact();
  const auto* b = d->boots.contact();
  return a && b && d->lower &&
         detail::LowerCockpitContactAccess::data(*a) == d->lower &&
         detail::LowerCockpitContactAccess::data(*b) == d->lower &&
         d->boots.selected_partitions().size() == 10;
}
auto Access::partition(const OriginBoardingIntermediatePauseSupport& p,
                       std::size_t side) -> const BoardingBootSourcePartition* {
  if (side > 1 || !valid(p)) return nullptr;
  return side == 0 ? &data(p)->port : &data(p)->boots.selected_partitions()[9];
}
auto Access::fixture(const OriginBoardingIntermediatePauseSupport& p)
    -> std::optional<detail::BoardingIntermediatePauseSourceFixture> {
  if (!valid(p)) return {};
  return detail::BoardingIntermediatePauseSourceFixture{
      {*partition(p, 0), *partition(p, 1)}, data(p)->summary.sources};
}
auto Access::make(const NativeCraftBinding& b,
                  const OriginBoardingBootSupport& boots, CtorLimits l,
                  CtorEvidence* supplied)
    -> std::expected<OriginBoardingIntermediatePauseSupport, std::string> {
  CtorEvidence local;
  auto& e = supplied ? *supplied : local;
  e = {};
  e.required_source_bytes = required_source_bytes;
  if (!valid_limits(l)) {
    e.condition = Why::invalid_limits;
    return std::unexpected("Pause source lowered caps required");
  }
  if (!base_charge(e, l)) return std::unexpected("Pause source base capacity");
  if (l.source_bytes < required_source_bytes) {
    e.condition = Why::source_capacity;
    return std::unexpected("Pause source bytes refused");
  }
  BoardingBootSourcePartition port;
  if (!authenticate(b, boots, l, e, port))
    return std::unexpected("Pause source authentication refused");
  if (!base_charge(e, l))
    return std::unexpected("Pause source allocation guard refused");
  try {
    auto d = std::allocate_shared<OriginBoardingIntermediatePauseSupport::Data>(
        PauseArenaAllocator<OriginBoardingIntermediatePauseSupport::Data>{}, b,
        boots);
    e.actual_source_bytes = e.work.source_bytes = required_source_bytes;
    e.complete = true;
    d->port = port;
    initialize(*d, e);
    return OriginBoardingIntermediatePauseSupport(std::move(d));
  } catch (const std::bad_alloc&) {
    e.complete = false;
    e.condition = Why::allocation;
    return std::unexpected("Pause source allocation failed");
  }
}
auto make_origin_boarding_intermediate_pause_support(
    const NativeCraftBinding& b, const OriginBoardingBootSupport& boots)
    -> std::expected<OriginBoardingIntermediatePauseSupport, std::string> {
  return Access::make(b, boots);
}
auto detail::intermediate_pause_source_math(
    const BoardingIntermediatePauseSourceFixture& f, CtorLimits l)
    -> BoardingIntermediatePauseSourceMath {
  BoardingIntermediatePauseSourceMath r;
  auto& e = r.evidence;
  if (!valid_limits(l)) {
    e.condition = Why::invalid_limits;
    return r;
  }
  if (!boarding_route_foot_phase_environment()) {
    e.condition = Why::unsupported_arithmetic;
    return r;
  }
  e.arithmetic_supported = true;
  e.sources = f.identities;
  for (std::size_t side = 0; side < 2; ++side) {
    identity(f.partitions[side], f.identities[side], side, l, e);
    if (e.condition == Why::base_capacity) return r;
    // No source admission here: changed pins deliberately do not prevent the
    // arithmetic coverage kernel's distinct evaluated evidence.
    const auto saved = e.condition;
    e.condition = Why::none;
    const auto good =
        intermediate_pause_quad_bridge(f.partitions[side], l, e, side);
    const auto geometry_condition = e.condition;
    e.condition = saved != Why::none ? saved : geometry_condition;
    if (!good && geometry_condition != Why::source_geometry) return r;
  }
  e.side.reset();
  e.complete = e.identity_valid[0] && e.identity_valid[1] &&
               e.quads[0].complete && e.quads[1].complete;
  return r;
}
auto detail::intermediate_pause_support_bounded(
    const OriginBoardingIntermediatePauseSupport& p, bool reverse, Limits l)
    -> Expected {
  if (!valid_limits(l))
    return std::unexpected("Pause assessment lowered caps required");
  if (l.output_bytes < sizeof(Expected))
    return std::unexpected("Pause fixed output capacity refused");
  Expected result(std::in_place, p);
  auto& d = *result;
  d.reverse = reverse;
  d.output_bytes = sizeof(Expected);
  if (!Access::valid(p)) {
    fail(d, Why::invalid_binding);
    return result;
  }
  if (!boarding_route_foot_phase_environment()) {
    fail(d, Why::unsupported_arithmetic);
    return result;
  }
  d.arithmetic_supported = true;
  d.request = *boarding_route_intermediate_step02_controls(4);
  BoardingRouteFootPhaseLimits phase_caps;
  phase_caps.graphs = l.phase.graphs;
  phase_caps.legs = l.phase.legs;
  phase_caps.bodies = l.phase.bodies;
  phase_caps.sectors = l.phase.sectors;
  phase_caps.timing = l.phase.timing;
  BoardingRouteFootPhaseCell cell;
  BoardingRouteFootPhaseRefusal reason;
  ++d.work.phase_calls;
  const auto state = boarding_route_foot_phase_cell(
      d.request, 0, 1, reverse, phase_caps, d.work.phase, cell, reason);
  if (state != BoardingRouteFootPhaseCellResult::accepted) {
    fail(d, Why::phase_prerequisite);
    d.first_refusal->phase = reason;
    d.arithmetic_supported =
        state != BoardingRouteFootPhaseCellResult::unsupported;
    return result;
  }
  d.kinematic_complete = true;
  if (!projection(p, cell, d, l)) return result;
  const BoardingIntermediatePauseProjectionToken token(p, cell);
  intermediate_pause_pressure_bridge(token, l, d);
  return result;
}
auto assess_origin_boarding_intermediate_pause_support(
    const OriginBoardingIntermediatePauseSupport& p, bool reverse) -> Expected {
  return detail::intermediate_pause_support_bounded(p, reverse);
}
} // namespace apsis_drift

#include "origin_boarding_route_intermediate_unloaded01_internal.hpp"
namespace apsis_drift::detail {
auto boarding_route_intermediate_unloaded01_upper_partition(
    const OriginBoardingIntermediatePauseSupport& provider)
    -> const BoardingBootSourcePartition* {
  if (!BoardingIntermediatePauseSupportAccess::valid(provider)) return nullptr;
  const auto* data = BoardingIntermediatePauseSupportAccess::data(provider);
  if (!data || !data->boots.contact() ||
      LowerCockpitContactAccess::data(*data->boots.contact()) != data->lower)
    return nullptr;
  const auto partitions = data->boots.selected_partitions();
  return partitions.size() == 10 ? &partitions[8] : nullptr;
}
} // namespace apsis_drift::detail

namespace apsis_drift::detail {
auto root_z01_pause_base_matches(
    const OriginBoardingIntermediatePauseSupport& pause,
    const NativeCraftBinding& binding, const OriginBoardingBootSupport& boots)
    -> bool {
  const auto* d = BoardingIntermediatePauseSupportAccess::data(pause);
  if (!d || !BoardingIntermediatePauseSupportAccess::valid(pause)) return false;
  const auto* a = d->binding.contact();
  const auto* b = binding.contact();
  const auto* sa = d->binding.selection();
  const auto* sb = binding.selection();
  const auto retained = d->boots.selected_partitions();
  const auto incoming = boots.selected_partitions();
  return a && b && sa && sb && a == b && sa == sb &&
         d->binding.pose() == binding.pose() &&
         a->original_geometry() == b->original_geometry() &&
         a->stowed_partition() == b->stowed_partition() &&
         d->lower == LowerCockpitContactAccess::data(*b) &&
         sa->hardware == OperatingProgress{1, 1, 1, 0} &&
         sb->hardware == OperatingProgress{1, 1, 1, 0} &&
         retained.size() == kBoardingBootSourcePartitionCount &&
         retained.data() == incoming.data() &&
         d->boots.contact() == boots.contact();
}
} // namespace apsis_drift::detail
