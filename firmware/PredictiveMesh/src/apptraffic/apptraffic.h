#pragma once

// ============================================================
// Application traffic — Arduino-facing adapter, Phase 7.
//
// Implements the minimum legitimate demo workload resolving the
// "reliability::send() has no live automatic caller" gap open since Phase
// 4 (see docs/decisions.md): NODE_A periodically sends its latest POT/LDR
// readings to NODE_S via reliability::send() — never a raw
// transport::send() call, never a second ACK/retry/PDR/routing
// implementation (all of that stays reliability's/routing's job, unchanged
// — see apptraffic.cpp).
//
// This is one shared firmware source tree (Part "one shared firmware
// source tree, only THIS_NODE_ID differs") — this module compiles into
// every node's image, but init()/tick() are no-ops on any node other than
// NODE_A, decided via a runtime THIS_NODE_ID comparison (a compile-time
// constant the optimizer folds away), not a preprocessor #if — see
// docs/decisions.md for why #if THIS_NODE_ID==NODE_A would be silently
// wrong (NodeId enumerators aren't visible to the preprocessor).
//
// The pure "what should the next packet contain, NORMAL or PRIORITY"
// decision and the binary payload encode/decode live in
// apptraffic_core.h/.cpp (Arduino-free, host-testable); this module owns
// real timing (millis()), the real sensor read (anomaly::getTelemetry() —
// Phase 3's already-existing POT/LDR sampling, not a second/duplicate
// analogRead() path), the real Serial priority trigger, and the real
// reliability::send() call.
// ============================================================

namespace apptraffic {

void init();

// Call from the main loop on every iteration. Rate-limits itself via
// APPLICATION_TX_INTERVAL_MS; also drains any pending Serial input,
// watching for the single-character priority trigger ('p'/'P' — see
// apptraffic.cpp). No-op on any node other than NODE_A.
void tick();

}  // namespace apptraffic
