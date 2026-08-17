#pragma once
#include "ucb1_core.h"

// ============================================================
// UCB1 adaptive next-hop ranking (implementation-guide.html §06,
// "[stretch, optional]") — Arduino-facing adapter, Phase 5.
//
// Owns the single ucb1_core::Ucb1State instance. Deliberately decoupled
// from routing_core::RoutingState — routing.cpp (which already owns the
// one real RoutingState) is responsible for calling
// routing_core::enumerateCandidates() itself and handing the resulting
// list here for ranking, exactly mirroring how routing.cpp already reads
// predictor::isUnhealthy() to build its own health mask. See
// docs/decisions.md.
//
// This header's declarations always exist, regardless of ENABLE_UCB1
// (config.h) — but ucb1.cpp's function BODIES are entirely wrapped in
// `#if ENABLE_UCB1`, and every call site into this module (routing.cpp,
// reliability.cpp) is wrapped the same way. With ENABLE_UCB1=0, nothing
// in the compiled firmware ever references this module at all — Phase
// 1/2/4's routing/reliability behavior is byte-for-byte unchanged. See
// config.h and docs/decisions.md.
// ============================================================

namespace ucb1 {

void init();

// Part 2: records one resolved hop-transmission SERIES's final outcome
// (a real MSG_ACK match, or retries genuinely exhausted / a synchronous
// send rejection) — never an individual radio retry. Called from
// reliability.cpp at exactly the same three points reliability already
// treats as a hop-transmission's FINAL state (see docs/decisions.md).
void onRouteOutcome(NodeId destination, NodeId nextHop, bool success);

// Part 3/4/5/6/8: ranks `candidates` (already validity + health-annotated
// by routing_core::enumerateCandidates(), called by routing.cpp) via
// UCB1, honoring `excludeNextHop` (the loop-prevention guard, Part 8).
// Returns NODE_ID_UNKNOWN if no candidate survives — the caller
// (routing::getNextHop()) then falls back to routing_core's own baseline
// pick, never fabricating a route UCB1 didn't actually choose. Never
// called for priority traffic (Part 5) — routing.cpp simply never
// reaches this function when priority==true.
NodeId selectNextHop(NodeId destination, const ucb1_core::Candidate* candidates, uint8_t count,
                      NodeId excludeNextHop = NODE_ID_UNKNOWN);

}  // namespace ucb1
