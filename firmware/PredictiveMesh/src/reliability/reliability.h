#pragma once
#include "../core/node_id.h"

// ============================================================
// Reliability layer (implementation-guide.html §5.4).
//
// Target design: hop-by-hop ACK on every forwarded frame, bounded
// retransmit on a missing ACK, sequence-number duplicate filtering.
// NOT YET IMPLEMENTED — see docs/known-issues.md. The transport layer's
// TxCallback already carries per-send success/fail from the ESP-NOW send
// callback, which is what a future retransmit policy and the predictor's
// PDR window will both consume.
// ============================================================

namespace reliability {

void init();

// Records the outcome of a send attempt so a future retransmit policy can
// use it. Phase 0 stub: logs only, no retry/ACK logic yet.
void onSendResult(NodeId dst, bool success);

}  // namespace reliability
