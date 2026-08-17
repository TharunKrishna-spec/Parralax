#pragma once
#include <stdint.h>
#include "../core/node_id.h"
#include "../config.h"

// ============================================================
// ucb1_core — pure UCB1 multi-armed-bandit next-hop ranking algorithm,
// deliberately free of any Arduino/ESP-NOW/Serial dependency (no
// millis(), no logger::*, no transport::*, no routing_core::RoutingState).
// Mirrors the routing_core/predictor_core/anomaly_core/reliability_core
// split from Phases 1-4 — this is real algorithmic logic (bandit
// statistics + the UCB1 selection formula) worth verifying on its own,
// and "verified on its own" only means something if it runs outside the
// ESP32 toolchain.
//
// implementation-guide.html §06 frames this whole feature as
// "[stretch, optional] UCB1 multi-armed bandit next-hop selection" with
// no formula or exploration coefficient given — this module implements
// the textbook UCB1 formula (Auer, Cesa-Bianchi & Fischer, 2002):
//   score(arm) = meanReward(arm) + C * sqrt(ln(N) / n(arm))
// where n(arm) is that arm's own trial count, N is the total trial count
// summed across the candidate set currently being ranked, and C is
// UCB1_EXPLORATION_C (config.h) — the standard sqrt(2) value, since the
// guide specifies none. See docs/decisions.md.
//
// This module is deliberately decoupled from routing_core: it never sees
// a RoutingState, MeshPacket, or anything Arduino-facing. The caller
// (src/ucb1/ucb1.cpp, the Arduino adapter) is responsible for building the
// `Candidate` list from routing_core::enumerateCandidates() first — by the
// time ucb1_core ever ranks a candidate, routing_core has already decided
// it's valid (not stale, not a priority-only edge). ucb1_core's own job is
// ranking ONLY — it can never invent a candidate that wasn't handed to it,
// which is what guarantees Phase 5's Part 4 requirement ("must never
// create a route the normal routing layer would consider invalid") by
// construction, not by ucb1_core re-deriving routing correctness itself.
// ============================================================

namespace ucb1_core {

// Part 1: one (destination, next-hop) "arm"'s bandit statistics.
// `attempts` is the exploration count (n_i in the UCB1 formula) — the
// number of times this specific candidate has actually been chosen AND
// resolved to a final outcome. Deliberately NOT incremented per radio
// retry (Part 2/13 — see recordOutcome()'s own doc comment). All three
// counters saturate at UINT32_MAX rather than wrapping (Part 10 test 18)
// — at real mesh traffic rates this would take longer than the hackathon,
// or this project, will ever run.
struct ArmStats {
  bool everObserved;
  uint32_t attempts;
  uint32_t successes;
  uint32_t failures;
};

// Fixed-size, indexed [destination][nextHop] — NODE_ID_COUNT x
// NODE_ID_COUNT arms, bounded at compile time, no dynamic allocation
// (Part 1: "do not create unbounded memory growth. Use a fixed-size
// structure appropriate for the known node count"). Deliberately no
// timestamp/decay field — see the "no decay" decision in docs/decisions.md
// (Part 7: fixed counters are acceptable when the guide doesn't require
// decay, and it doesn't).
struct Ucb1State {
  ArmStats arms[NODE_ID_COUNT][NODE_ID_COUNT];
};

void init(Ucb1State& state);

// Part 2: records ONE resolved hop-transmission SERIES's final outcome —
// never an individual radio retry. A series that took 1 original attempt
// + 2 retries before finally succeeding is exactly one call here
// (success=true), matching Phase 4's own established packet-series-vs-
// attempt distinction (reliability_core::Statistics.packetsDelivered), not
// a new invented boundary. See docs/decisions.md.
void recordOutcome(Ucb1State& state, NodeId destination, NodeId nextHop, bool success);

// One candidate as the caller already knows it: already validity- and
// priority-only-edge-filtered (routing_core::enumerateCandidates()),
// annotated with current link health (Part 6). `hopCount` is carried for
// diagnostics/logging only — the ranking formula below deliberately never
// uses it (the whole point of this stretch feature is letting learned
// delivery history override the static hop-count heuristic when real
// evidence supports it — see docs/decisions.md).
struct Candidate {
  NodeId nextHop;
  uint8_t hopCount;
  bool healthy;
};

// Part 3/4/6/8: the fused, safety-first selection.
//   1. Excludes any candidate equal to `excludeNextHop` (NODE_ID_UNKNOWN =
//      no exclusion) — the loop-prevention guard (Part 8): this node must
//      never pick, as its next hop, whoever it just received this
//      decision's packet from.
//   2. If any surviving candidate is healthy, ranks ONLY among the
//      healthy subset (Part 6: link-health constraints apply before UCB1
//      ranking, preserving Phase 2's own healthy-preferred/any-fallback
//      philosophy exactly). Falls back to the full surviving set only if
//      none are healthy.
//   3. Any candidate with zero observations (`attempts == 0`) is returned
//      immediately (Part 3: "handle zero-observation candidates
//      explicitly" — standard UCB1 practice of trying every untried arm
//      before the formula is well-defined; also directly guarantees no
//      division by zero or ln(0), since N can only be 0 when every
//      candidate has 0 attempts, which this step always catches first).
//   4. Otherwise computes UCB1's score for each and returns the argmax,
//      ties broken toward the lowest NodeId — deterministic (Part 9): no
//      randomness anywhere in this function.
// Returns NODE_ID_UNKNOWN if no candidate survives exclusion (mirrors
// routing_core::selectNextHop()'s own "no route" contract).
// Never called for priority traffic (Part 5) — the caller (routing::
// getNextHop()) simply never invokes this function when priority==true;
// the existing priority code path is untouched and doesn't know ucb1_core
// exists.
NodeId selectNextHop(const Ucb1State& state, NodeId destination, const Candidate* candidates,
                      uint8_t count, NodeId excludeNextHop = NODE_ID_UNKNOWN);

// Read-only diagnostic snapshot of one arm — for logging today, ready for
// a future telemetry consumer (Part 13: "internal UCB1 state may be
// documented for future telemetry" — no GUI protocol invented here, see
// docs/known-issues.md).
struct ArmSnapshot {
  NodeId nextHop;
  uint32_t attempts;
  uint32_t successes;
  uint32_t failures;
  float meanReward;  // 0.0 if attempts == 0 — never a divide-by-zero
};
ArmSnapshot snapshot(const Ucb1State& state, NodeId destination, NodeId nextHop);

}  // namespace ucb1_core
