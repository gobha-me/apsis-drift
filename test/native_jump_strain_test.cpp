#include "jump_strain_fixture.hpp"

#include <limits>

namespace {
using namespace jump_strain_fixture;
auto starts() -> void {
  for (std::uint32_t chart = 0; chart < 3; ++chart) {
    const auto old = need(native_jump_recovery_new_game({42}, chart));
    const auto selected = need(native_jump_strain_new_game({42}, chart));
    const auto& a = std::get<FreedomKnowledgeSaveDocument>(old.document);
    const auto& b = std::get<FreedomKnowledgeSaveDocument>(selected.document);
    check(a.knowledge == b.knowledge && a.voyage.voyage == b.voyage.voyage &&
              a.voyage.resources.version == kFreedomRecoveryResourceVersion &&
              a.voyage.resources.drive->version ==
                  kFreedomDriveRecoveryVersion &&
              b.voyage.resources.version == kFreedomStrainedResourceVersion &&
              b.voyage.resources.drive->version ==
                  kFreedomStrainedDriveRecoveryVersion &&
              !b.voyage.resources.drive->last_use,
          "Explicit strain selection preserves each chart and historical "
          "recovery factory");
    auto s = need(NativeFreedomFlightSession::open(selected));
    s = continued(s);
    check(reading(s).ready && reading(s).next_recovery_ticks == 5400,
          "Every new strain craft starts ready for its first45-second cycle");
  }
  check(!native_jump_strain_new_game({42}, 3),
        "Unsupported chart selection refuses");
}
auto cancel_and_cool() -> void {
  auto s = source();
  const auto route = generate_first_universe_route({42});
  need(s.select_jump(route.destination));
  check(s.travel()->version == kFreedomStrainedTravelStateVersion,
        "First travel explicitly selects logical7");
  leg(s, FreedomJumpRecoveryKind::system);
  need(s.select_jump(route.origin));
  const auto state = *s.resources()->drive;
  const auto charges = s.resources()->jump_charges;
  const auto text = bytes(s);
  for (unsigned query = 0; query < 3; ++query) {
    need(s.jump_preview());
    need(s.jump_available());
    reading(s);
  }
  check(bytes(s) == text,
        "Paused previews/save cannot age or increase temporary strain");
  need(s.begin_jump());
  for (unsigned i = 0; i < 17; ++i)
    need(s.advance({}));
  need(s.cancel_jump());
  check(*s.resources()->drive == state &&
            s.resources()->jump_charges == charges &&
            reading(s).remaining_ticks == 5383,
        "Cancellation retains strain/charge while actual spool ticks cool it");
  s = continued(s);
  for (SimulationTick i = 0; i < 5383; ++i)
    need(s.advance({}));
  check(reading(s).ready && reading(s).next_recovery_ticks == 5400 &&
            *s.resources()->drive == state,
        "Actual committed cooling restores45-second next use without erasing "
        "history");
  leg(s, FreedomJumpRecoveryKind::system);
  check(reading(s).cycle_ticks == 5400,
        "A cooled real repeat starts a fresh45-second cycle");
}
auto mixed(bool minor) -> void {
  auto s = source();
  const auto home = current(s);
  const auto kind =
      minor ? FreedomJumpRecoveryKind::minor : FreedomJumpRecoveryKind::moon;
  need(s.select_local_jump(companion(minor)));
  leg(s, kind);
  need(s.select_local_jump(home));
  leg(s, kind);
  check(reading(s).cycle_ticks == 10440,
        "Three actual seconds of spool reduce carried debt to87seconds");
  if (minor) {
    need(s.select_planet_jump(planet(s)));
    leg(s, FreedomJumpRecoveryKind::planet);
  } else {
    need(s.select_jump(
        generate_first_intersystem_identities({42}).target_system));
    leg(s, FreedomJumpRecoveryKind::system);
  }
  check(
      reading(s).cycle_ticks == 10800 &&
          reading(s).next_recovery_ticks == 10800,
      "Early mixed-route third commitment reaches the disclosed90-second cap");
  const auto frozen = bytes(s);
  NativeFlightControls bad;
  bad.positive_rotation.x = std::numeric_limits<double>::quiet_NaN();
  check(!s.advance(bad) && !s.begin_jump() && bytes(s) == frozen,
        "Invalid controls and empty charges leave the complete strained save "
        "unchanged");
}
auto pair_chart() -> void {
  auto s = source(0);
  const auto route = generate_first_universe_route({42});
  need(s.select_jump(route.destination));
  leg(s, FreedomJumpRecoveryKind::system);
  need(s.select_jump(route.origin));
  leg(s, FreedomJumpRecoveryKind::system);
  check(s.knowledge()->recipe.version == kFreedomTravelKnowledgeVersion &&
            reading(s).cycle_ticks == 10440,
        "Pair chart strain trips preserve pair knowledge and actual "
        "accumulated cycle");
}
} // namespace
int main() {
  try {
    starts();
    cancel_and_cool();
    mixed(false);
    mixed(true);
    pair_chart();
    std::cout << checks << " native jump strain checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
