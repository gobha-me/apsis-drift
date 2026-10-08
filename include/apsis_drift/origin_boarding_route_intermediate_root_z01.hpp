#pragma once

#include "apsis_drift/origin_boarding_route_checkpoint_world_material04.hpp"
#include "apsis_drift/origin_boarding_route_intermediate_support_self01.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace apsis_drift {
namespace detail {
struct BoardingRouteIntermediateRootZ01Access;
}

inline constexpr std::uint32_t kBoardingRouteIntermediateRootZ01Version{1};
inline constexpr std::size_t kBoardingRouteIntermediateRootZ01PhaseCount{5},
    kBoardingRouteIntermediateRootZ01MaximumSourceBytes{1024},
    kBoardingRouteIntermediateRootZ01MaximumExpectedBytes{4096},
    kBoardingRouteIntermediateRootZ01MaximumCellBytes{12288},
    kBoardingRouteIntermediateRootZ01MaximumDepth{10},
    kBoardingRouteIntermediateRootZ01MaximumNodes{2047},
    kBoardingRouteIntermediateRootZ01MaximumLeaves{1024},
    kBoardingRouteIntermediateRootZ01MaximumConstructorScratchBytes{8192},
    kBoardingRouteIntermediateRootZ01MaximumWorldScratchBytes{16384},
    kBoardingRouteIntermediateRootZ01MaximumScratchBytes{49152},
    kBoardingRouteIntermediateRootZ01MaximumOutputBytes{std::size_t{16} * 1024 *
                                                        1024};

enum class BoardingRouteIntermediateRootZ01Stage : std::uint8_t {
  not_run,
  preflight,
  source,
  phase,
  contact,
  load,
  self,
  world,
  join,
  coverage
};
enum class BoardingRouteIntermediateRootZ01State : std::uint8_t {
  not_run,
  accepted,
  unresolved,
  capacity,
  unsupported,
  identity
};
enum class BoardingRouteIntermediateRootZ01Condition : std::uint8_t {
  none,
  invalid_range,
  invalid_limits,
  invalid_binding,
  source_identity,
  control_identity,
  unsupported_arithmetic,
  source_capacity,
  work_capacity,
  output_capacity,
  allocation_capacity,
  phase_refused,
  contact_refused,
  load_refused,
  self_refused,
  world_refused,
  join_refused,
  node_capacity,
  leaf_capacity,
  depth_capacity,
  unsplittable_interval,
  incomplete_cover
};
enum class BoardingRouteIntermediateRootZ01ContactEvent : std::uint8_t {
  not_run,
  upper_release,
  unloaded_motion,
  intermediate_reacquisition,
  load_acquisition,
  supported_pause,
  intermediate_unload,
  upper_reacquisition
};

struct BoardingRouteIntermediateRootZ01ConstructorEvidence {
  std::uint32_t version{kBoardingRouteIntermediateRootZ01Version};
  std::size_t required_source_bytes{}, actual_source_bytes{}, base_guards{},
      control_guards{};
  std::array<bool, 7> input_identity_checked{}, input_identity_valid{};
  std::array<bool, 5> control_identity_checked{}, control_identity_valid{};
  BoardingRouteIntermediateRootZ01Condition condition{};
  bool arithmetic_supported{}, same_base{}, controls_authenticated{},
      authenticated{};
};

// Creation binds immutable sources and the sole selected recipe. It does not
// qualify any pose, contact, load, self pair, world sweep or player action.
class OriginBoardingRouteIntermediateRootZ01 {
 public:
  struct Data;
  // Empty handles carry no source authority and refuse assessment safely.
  OriginBoardingRouteIntermediateRootZ01() = default;
  OriginBoardingRouteIntermediateRootZ01(
      const OriginBoardingRouteIntermediateRootZ01&) = default;
  OriginBoardingRouteIntermediateRootZ01(
      OriginBoardingRouteIntermediateRootZ01&&) noexcept = default;
  auto operator=(const OriginBoardingRouteIntermediateRootZ01&)
      -> OriginBoardingRouteIntermediateRootZ01& = default;
  auto operator=(OriginBoardingRouteIntermediateRootZ01&&) noexcept
      -> OriginBoardingRouteIntermediateRootZ01& = default;
  [[nodiscard]] auto summary() const
      -> const BoardingRouteIntermediateRootZ01ConstructorEvidence*;

 private:
  explicit OriginBoardingRouteIntermediateRootZ01(std::shared_ptr<const Data>);
  std::shared_ptr<const Data> data_;
  friend struct detail::BoardingRouteIntermediateRootZ01Access;
};

struct BoardingRouteIntermediateRootZ01WorldCell {
  std::array<std::uint32_t, kBoardingBodyPartCount> material_sources_closed{},
      material_triangles_closed{}, halo_triangles_closed{};
  std::array<bool, kBoardingBodyPartCount> body_identity{}, domain_complete{},
      material_complete{}, halo_complete{};
  bool arithmetic_supported{}, complete{};
};
struct BoardingRouteIntermediateRootZ01Cell {
  // Reuse numerical/evidence storage, including its ONE phase cell. No old
  // current-cell token or old successful report authenticates this storage.
  BoardingRouteIntermediateSupportSelf01Cell support_self;
  BoardingRouteIntermediateRootZ01WorldCell world;
  BoardingRouteIntermediateRootZ01ContactEvent event{};
  bool fresh_control_identity{}, contact_complete{}, load_complete{},
      self_complete{}, complete{};
};
struct BoardingRouteIntermediateRootZ01Refusal {
  BoardingRouteIntermediateRootZ01Condition condition{}, predicate_condition{};
  BoardingRouteIntermediateRootZ01Stage stage{};
  BoardingRouteIntermediateRootZ01ContactEvent event{};
  double first{}, last{}, local_first{}, local_last{};
  std::size_t depth{};
  std::optional<std::size_t> phase_index, cell;
  // These retain the original exact nested predicate/source/pair metadata.
  BoardingRouteIntermediateSupportSelf01Refusal support_self;
  BoardingRouteCheckpointWorldRefusal world;
};
struct BoardingRouteIntermediateRootZ01Counters {
  // support_self.phase is the only graph/leg/body/sector/timing ledger.
  BoardingRouteIntermediateSupportSelf01Counters support_self;
  BoardingRouteCheckpointWorldCounters world;
  BoardingRouteCheckpointBoundaryCounters boundary;
};
struct BoardingRouteIntermediateRootZ01Diagnostic {
  OriginBoardingRouteIntermediateRootZ01 source;
  std::uint32_t version{kBoardingRouteIntermediateRootZ01Version};
  std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount> parts;
  double requested_first{}, requested_last{}, reporting_elapsed_seconds{};
  BoardingRouteIntermediateRootZ01Counters work;
  std::size_t examined_nodes{}, mandatory_splits{}, maximum_depth{},
      output_capacity_bytes{};
  std::array<bool, 67> source_evaluated{};
  std::array<bool, 4> join_expression_identity{}, qualified_joins{};
  std::array<bool, 3> prefix_endpoint_scope{}, prefix_endpoint_earned{};
  std::array<BoardingRouteCheckpointWorldPart, kBoardingBodyPartCount>
      world_parts;
  std::vector<BoardingRouteIntermediateRootZ01Cell> cells;
  std::optional<BoardingRouteIntermediateRootZ01Refusal> first_refusal;
  BoardingRouteIntermediateRootZ01Condition stop_condition{};
  BoardingRouteIntermediateRootZ01Stage stop_stage{};
  BoardingRouteIntermediateRootZ01State state{};
  bool reverse{}, arithmetic_supported{}, source_enrolled{},
      kinematic_complete{}, contact_complete{}, nominal_support_complete{},
      self_complete{}, world_complete{}, complete{}, route_qualified{};
  explicit BoardingRouteIntermediateRootZ01Diagnostic(
      const OriginBoardingRouteIntermediateRootZ01& p)
      : source(p) {}
  static constexpr bool seat_qualified{false}, actor_qualified{false},
      save_qualified{false}, first_flight_qualified{false},
      dynamics_qualified{false}, strength_qualified{false},
      friction_qualified{false};
};
using BoardingRouteIntermediateRootZ01Expected =
    std::expected<BoardingRouteIntermediateRootZ01Diagnostic, std::string>;
static_assert(sizeof(BoardingRouteIntermediateRootZ01Expected) <=
              kBoardingRouteIntermediateRootZ01MaximumExpectedBytes);
static_assert(sizeof(BoardingRouteIntermediateRootZ01Cell) <=
              kBoardingRouteIntermediateRootZ01MaximumCellBytes);
static_assert(sizeof(BoardingRouteIntermediateRootZ01Refusal) <= 512);

[[nodiscard]] auto make_origin_boarding_route_intermediate_root_z01(
    const NativeCraftBinding&, const OriginBoardingBootSupport&,
    const OriginBoardingIntermediatePauseSupport&,
    const OriginBoardingInitialMaterial&,
    const OriginBoardingCheckpointMaterialExtension&,
    const OriginBoardingHatchSealMaterial&,
    const OriginBoardingSeparationRingMaterial&)
    -> std::expected<OriginBoardingRouteIntermediateRootZ01, std::string>;
[[nodiscard]] auto assess_origin_boarding_route_intermediate_root_z01(
    const OriginBoardingRouteIntermediateRootZ01&, double first = 0,
    double last = 1) -> BoardingRouteIntermediateRootZ01Expected;
// Full current/pending fixed results; dynamic cell capacity is additional.
[[nodiscard]] auto boarding_route_intermediate_root_z01_required_output_bytes()
    -> std::size_t;
} // namespace apsis_drift
