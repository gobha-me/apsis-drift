#pragma once

#include "apsis_drift/freedom_resources.hpp"
#include "apsis_drift/universe_navigation.hpp"

namespace apsis_drift {

// Explicit baseline chart grant for the unchanged origin/neighbor recipe.
// Both anchors are RESOLVED; no actual visit or observation is synthesized.
// This read-only projection is not a mutable discovery ledger or jump owner.
// Legacy callers retain starter-only identity checks. A replacement projection
// must explicitly borrow its validated same-seed/craft/tick origin owner.
[[nodiscard]] auto resolve_freedom_starting_chart(
    const FirstUniverseRoute& route, SystemId current_system,
    const FreedomResources& resources, bool selection_open = true,
    const FreedomSaveDocument* instance_owner = nullptr)
    -> std::expected<UniverseNavigationView, UniverseNavigationError>;

} // namespace apsis_drift
