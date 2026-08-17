#pragma once
#include "../core/node_id.h"

// ============================================================
// Link degradation predictor (implementation-guide.html §5.1).
//
// Target design: EWMA-smoothed RSSI, least-squares slope over a sliding
// window, fused with a PDR window into link_score = w1*(1-degrade_term) +
// w2*PDR. NOT YET IMPLEMENTED — see docs/known-issues.md.
// ============================================================

namespace predictor {

void init();

// Returns the current link_score for the link to `neighbor`, in [0, 1],
// higher = healthier.
// Phase 0 stub: always returns 1.0 (always healthy), so nothing downstream
// can mistake this for a real reading before the real algorithm lands.
float linkScore(NodeId neighbor);

}  // namespace predictor
