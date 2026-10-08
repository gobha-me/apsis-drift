#pragma once

#include "apsis_drift/origin_boarding_route_intermediate_root_z01.hpp"
#include "origin_boarding_route_intermediate_support_self01_internal.hpp"

namespace apsis_drift::detail {
struct BoardingRouteCheckpointWorldProxy;
struct BoardingRouteIntermediateRootZ01ConstructorLimits {
  std::size_t source_bytes{kBoardingRouteIntermediateRootZ01MaximumSourceBytes},
      base_guards{64}, control_guards{64};
};
// WORLD04 maxima, without its old checkpoint child or a second phase ledger.
struct BoardingRouteIntermediateRootZ01WorldLimits {
  std::size_t pair_axes{16};
  std::uint64_t roster_entries{1759}, effective_sources{1751},
      union_envelope_pairs{26265}, union_proxy_preparations{15360},
      domain_union_checks{15}, domain_cell_checks{15360},
      proxy_preparations{2496512}, refined_envelope_pairs{1048576},
      material_triangle_visits{354786}, halo_metadata_visits{75},
      halo_triangle_visits{8100}, triangle_union_pairs{5443290},
      refined_triangle_pairs{1048576}, base_enclosure_relations{480},
      refined_enclosure_relations{491520}, axes{16777216},
      direction_entries_prepared{20217856}, sole_vertex_guards{192},
      boundary_witness_attempts{15360}, boundary_signed_support_calls{30720},
      boundary_width_attempts{15360};
};
struct BoardingRouteIntermediateRootZ01Limits {
  BoardingRouteIntermediateSupportSelf01Limits support_self;
  BoardingRouteIntermediateRootZ01WorldLimits world;
  std::size_t source_bytes{kBoardingRouteIntermediateRootZ01MaximumSourceBytes},
      output_bytes{kBoardingRouteIntermediateRootZ01MaximumOutputBytes};
};
static_assert(sizeof(BoardingRouteIntermediateRootZ01Limits) <= 1024);

struct BoardingRouteIntermediateRootZ01Access {
  static auto make(
      const NativeCraftBinding&, const OriginBoardingBootSupport&,
      const OriginBoardingIntermediatePauseSupport&,
      const OriginBoardingInitialMaterial&,
      const OriginBoardingCheckpointMaterialExtension&,
      const OriginBoardingHatchSealMaterial&,
      const OriginBoardingSeparationRingMaterial&,
      BoardingRouteIntermediateRootZ01ConstructorLimits = {},
      BoardingRouteIntermediateRootZ01ConstructorEvidence* = nullptr)
      -> std::expected<OriginBoardingRouteIntermediateRootZ01, std::string>;
  static auto valid(const OriginBoardingRouteIntermediateRootZ01&) -> bool;
  static auto data(const OriginBoardingRouteIntermediateRootZ01&)
      -> const OriginBoardingRouteIntermediateRootZ01::Data*;
  static auto binding(const OriginBoardingRouteIntermediateRootZ01&)
      -> const NativeCraftBinding*;
  static auto boots(const OriginBoardingRouteIntermediateRootZ01&)
      -> const OriginBoardingBootSupport*;
  static auto pause(const OriginBoardingRouteIntermediateRootZ01&)
      -> const OriginBoardingIntermediatePauseSupport*;
  static auto material(const OriginBoardingRouteIntermediateRootZ01&)
      -> const OriginBoardingInitialMaterial*;
  static auto extension(const OriginBoardingRouteIntermediateRootZ01&)
      -> const OriginBoardingCheckpointMaterialExtension*;
  static auto seal(const OriginBoardingRouteIntermediateRootZ01&)
      -> const OriginBoardingHatchSealMaterial*;
  static auto ring(const OriginBoardingRouteIntermediateRootZ01&)
      -> const OriginBoardingSeparationRingMaterial*;
};

[[nodiscard]] auto boarding_route_intermediate_root_z01_controls(std::size_t)
    -> std::optional<BoardingRouteFootPhaseRequest>;
[[nodiscard]] auto boarding_route_intermediate_root_z01_phase(double, double)
    -> std::optional<std::size_t>;
[[nodiscard]] auto boarding_route_intermediate_root_z01_local(std::size_t,
                                                              double) -> double;
[[nodiscard]] auto boarding_route_intermediate_root_z01_clock(double) -> double;
[[nodiscard]] auto boarding_route_intermediate_root_z01_bounded(
    const OriginBoardingRouteIntermediateRootZ01&, double, double,
    BoardingRouteIntermediateRootZ01Limits = {})
    -> BoardingRouteIntermediateRootZ01Expected;

class BoardingRouteIntermediateRootZ01Context {
 public:
  BoardingRouteIntermediateRootZ01Context(
      const BoardingRouteIntermediateRootZ01Context&) = delete;
  auto operator=(const BoardingRouteIntermediateRootZ01Context&)
      -> BoardingRouteIntermediateRootZ01Context& = delete;

 private:
  BoardingRouteIntermediateRootZ01Context(
      const BoardingRouteIntermediateRootZ01Diagnostic& owner,
      const BoardingRouteFootPhaseRequest& request,
      const std::size_t& current_epoch)
      : owner_(&owner),
        data_(BoardingRouteIntermediateRootZ01Access::data(owner.source)),
        parts_(&owner.parts), request_(&request), epoch_(&current_epoch),
        expected_epoch_(current_epoch) {}
  const BoardingRouteIntermediateRootZ01Diagnostic* owner_;
  const OriginBoardingRouteIntermediateRootZ01::Data* data_;
  const std::array<BoardingRoutePhasePartBinding, kBoardingBodyPartCount>*
      parts_;
  const BoardingRouteFootPhaseRequest* request_;
  const std::size_t* epoch_;
  std::size_t expected_epoch_;
  friend auto boarding_route_intermediate_root_z01_bounded(
      const OriginBoardingRouteIntermediateRootZ01&, double, double,
      BoardingRouteIntermediateRootZ01Limits)
      -> BoardingRouteIntermediateRootZ01Expected;
  friend auto boarding_route_intermediate_root_z01_current_cell(
      const BoardingRouteIntermediateRootZ01Context&,
      BoardingRouteIntermediateRootZ01Diagnostic&,
      BoardingRouteIntermediateRootZ01Cell&,
      const BoardingRouteIntermediateRootZ01Limits&,
      BoardingRouteIntermediateRootZ01Refusal&)
      -> BoardingRouteIntermediateRootZ01State;
  friend struct BoardingRouteIntermediateRootZ01CurrentAccess;
};

class BoardingRouteIntermediateRootZ01CurrentToken {
 public:
  BoardingRouteIntermediateRootZ01CurrentToken(
      const BoardingRouteIntermediateRootZ01CurrentToken&) = delete;
  auto operator=(const BoardingRouteIntermediateRootZ01CurrentToken&)
      -> BoardingRouteIntermediateRootZ01CurrentToken& = delete;

 private:
  BoardingRouteIntermediateRootZ01CurrentToken(
      const BoardingRouteIntermediateRootZ01Context& context,
      const BoardingRouteIntermediateRootZ01Diagnostic& owner,
      const BoardingRouteIntermediateRootZ01Cell& cell,
      const BoardingRouteFootPhaseRequest& request)
      : context_(&context), owner_(&owner), cell_(&cell), request_(&request),
        data_(BoardingRouteIntermediateRootZ01Access::data(owner.source)) {}
  const BoardingRouteIntermediateRootZ01Context* context_;
  const BoardingRouteIntermediateRootZ01Diagnostic* owner_;
  const BoardingRouteIntermediateRootZ01Cell* cell_;
  const BoardingRouteFootPhaseRequest* request_;
  const OriginBoardingRouteIntermediateRootZ01::Data* data_;
  friend auto boarding_route_intermediate_root_z01_current_cell(
      const BoardingRouteIntermediateRootZ01Context&,
      BoardingRouteIntermediateRootZ01Diagnostic&,
      BoardingRouteIntermediateRootZ01Cell&,
      const BoardingRouteIntermediateRootZ01Limits&,
      BoardingRouteIntermediateRootZ01Refusal&)
      -> BoardingRouteIntermediateRootZ01State;
  friend auto boarding_route_intermediate_root_z01_current_proxy(
      const BoardingRouteIntermediateRootZ01Context&,
      BoardingRouteIntermediateRootZ01Diagnostic&,
      BoardingRouteIntermediateRootZ01Cell&, std::size_t,
      const BoardingRouteIntermediateRootZ01Limits&,
      BoardingRouteIntermediateRootZ01Refusal&)
      -> std::expected<BoardingRouteCheckpointWorldProxy, std::string>;
  friend struct BoardingRouteIntermediateRootZ01CurrentAccess;
};

// The validating borrow exposes no token constructor or accepted-state setter.
struct BoardingRouteIntermediateRootZ01CurrentAccess {
  static auto valid(const BoardingRouteIntermediateRootZ01Context&,
                    const BoardingRouteIntermediateRootZ01Diagnostic&,
                    const BoardingRouteFootPhaseRequest&) -> bool;
  static auto valid(const BoardingRouteIntermediateRootZ01CurrentToken&,
                    const BoardingRouteIntermediateRootZ01Diagnostic&,
                    const BoardingRouteIntermediateRootZ01Cell&) -> bool;
  static auto request(const BoardingRouteIntermediateRootZ01Context&)
      -> const BoardingRouteFootPhaseRequest*;
  static auto request(const BoardingRouteIntermediateRootZ01CurrentToken&)
      -> const BoardingRouteFootPhaseRequest*;
};

// Renew a fresh token only for the supplied owned cell during staged WORLD.
// The returned proxy is numerical layout, without old issuer permissions.
auto boarding_route_intermediate_root_z01_current_proxy(
    const BoardingRouteIntermediateRootZ01Context&,
    BoardingRouteIntermediateRootZ01Diagnostic&,
    BoardingRouteIntermediateRootZ01Cell&, std::size_t part,
    const BoardingRouteIntermediateRootZ01Limits&,
    BoardingRouteIntermediateRootZ01Refusal&)
    -> std::expected<BoardingRouteCheckpointWorldProxy, std::string>;

auto boarding_route_intermediate_root_z01_current_cell(
    const BoardingRouteIntermediateRootZ01Context&,
    BoardingRouteIntermediateRootZ01Diagnostic&,
    BoardingRouteIntermediateRootZ01Cell&,
    const BoardingRouteIntermediateRootZ01Limits&,
    BoardingRouteIntermediateRootZ01Refusal&)
    -> BoardingRouteIntermediateRootZ01State;
auto boarding_route_intermediate_root_z01_support_bridge(
    const BoardingRouteIntermediateRootZ01CurrentToken&,
    BoardingRouteIntermediateRootZ01Diagnostic&,
    BoardingRouteIntermediateRootZ01Cell&,
    const BoardingRouteIntermediateRootZ01Limits&,
    BoardingRouteIntermediateRootZ01Refusal&)
    -> BoardingRouteIntermediateRootZ01State;
auto boarding_route_intermediate_root_z01_self_bridge(
    const BoardingRouteIntermediateRootZ01CurrentToken&,
    BoardingRouteIntermediateRootZ01Diagnostic&,
    BoardingRouteIntermediateRootZ01Cell&,
    const BoardingRouteIntermediateRootZ01Limits&,
    BoardingRouteIntermediateRootZ01Refusal&)
    -> BoardingRouteIntermediateRootZ01State;
auto boarding_route_intermediate_root_z01_world_bridge(
    const BoardingRouteIntermediateRootZ01CurrentToken&,
    BoardingRouteIntermediateRootZ01Diagnostic&,
    BoardingRouteIntermediateRootZ01Cell&,
    const BoardingRouteIntermediateRootZ01Limits&,
    BoardingRouteIntermediateRootZ01Refusal&)
    -> BoardingRouteIntermediateRootZ01State;
} // namespace apsis_drift::detail
