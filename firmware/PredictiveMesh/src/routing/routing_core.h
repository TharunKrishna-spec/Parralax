#pragma once
#include <stdint.h>
#include "../core/node_id.h"

// ============================================================
// routing_core — pure distance-vector algorithm, deliberately free of any
// Arduino/ESP-NOW/Serial dependency (no millis(), no logger::*, no
// transport::*).
//
// Why this is split out from routing.cpp: it's the first module in this
// project with real algorithmic logic worth verifying by itself, and
// "verified by itself" only means something if it can run outside the
// Arduino toolchain. Every function here takes "now" as an explicit
// parameter instead of calling millis() internally, so the exact same code
// that runs on-device is what firmware/PredictiveMesh/test/test_routing_core.cpp
// exercises on a host compiler (g++) with hand-constructed inputs. This is
// NOT a network/radio simulator — it never fakes a packet exchange between
// nodes; it only unit-tests the table math routing.cpp drives for real.
// See docs/decisions.md and docs/testing.md.
// ============================================================

namespace routing_core {

// Hop counts at or above this are "unreachable" (RIP-style infinity
// sentinel), not a real distance. Bounds a single corrupt/absurd
// advertisement from being stored as a plausible-looking finite distance.
// 5 real nodes means any genuine route is at most 4 hops; 15 leaves
// generous headroom without being large enough to matter for overflow.
static const uint8_t MAX_HOP_COUNT = 15;

// One direct-neighbor liveness record. RSSI is recorded as transport
// metadata only — Phase 1 does not use it to make any decision. That's
// Phase 2 (predictor / link_score).
struct NeighborEntry {
  bool valid;
  int8_t last_rssi;
  uint32_t last_seen_ms;
};

// One candidate route to a destination, reached via one specific neighbor.
// Candidates are retained per (destination, via-neighbor) pair rather than
// collapsed to a single "best" route per destination, so alternate paths
// (e.g. A's route to S via C/D) stay available for fallback and for a
// future link-quality-aware selector to weigh, without changing this
// table's shape.
struct RouteCandidate {
  bool valid;
  uint8_t hop_count;       // meaningful only when valid == true
  uint32_t last_update_ms;
};

struct RoutingState {
  NodeId self;
  NeighborEntry neighbors[NODE_ID_COUNT];
  // candidates[destination][via_neighbor]
  RouteCandidate candidates[NODE_ID_COUNT][NODE_ID_COUNT];
};

// One (destination, hop_count) fact, either parsed from a received
// advertisement or produced by buildAdvertisement() for sending.
struct RouteAdEntry {
  NodeId destination;
  uint8_t hop_count;
};

void init(RoutingState& state, NodeId self);

// Records that `from` was heard directly, just now. Called for every
// received packet regardless of type (any traffic from a neighbor implies
// liveness), using the packet's prev_hop as the identity.
void noteNeighborSeen(RoutingState& state, NodeId from, int8_t rssi, uint32_t now);

// Applies one neighbor's advertised distance vector to our candidate
// table. `from` must be a direct neighbor (see node_id.h::neighborsOf());
// entries claiming an impossible distance are rejected rather than stored
// (see the Route Advertisement Validity Guards note in docs/decisions.md).
// Returns true if any candidate's validity or hop_count actually changed
// (callers use this to decide whether a ROUTE_CHANGED event is warranted).
bool applyRouteAdvertisement(RoutingState& state, NodeId from,
                              const RouteAdEntry* entries, uint8_t count,
                              uint32_t now);

// Read-only: real last_seen_ms timestamp for `neighbor` (see
// noteNeighborSeen()), 0 if out-of-range or never observed. For
// diagnostic/telemetry consumers only — never used by any routing
// decision. See routing.h::getNeighborLastSeenMs() for the adapter
// wrapper.
uint32_t neighborLastSeenMs(const RoutingState& state, NodeId neighbor);

// Sweeps neighbor and route-candidate entries older than timeoutMs and
// marks them invalid (never silently deletes/reuses the slot — it stays
// present but invalid, satisfying "route expiry/invalidation implemented"
// without needing a separate removal path). Returns how many entries were
// invalidated this sweep.
uint8_t expireStale(RoutingState& state, uint32_t now, uint32_t timeoutMs);

// Builds this node's own vector to advertise to its neighbors: distance 0
// for self, plus the best currently-known hop_count for every other
// reachable destination (evaluated across ALL candidates, priority-only
// edges included — advertised distance is an objective fact about what
// this node knows, not a statement about routing policy). Returns the
// number of entries written (<= NODE_ID_COUNT).
uint8_t buildAdvertisement(const RoutingState& state, RouteAdEntry* out, uint8_t maxEntries);

// True for the one edge the topology diagram in implementation-guide.html
// §01 labels "(priority path only)" — currently just A<->S. See
// docs/decisions.md for why this edge is excluded from NORMAL selection.
bool isPriorityOnlyEdge(NodeId a, NodeId b);

// Selects the next hop for `destination`.
// priority == false (NORMAL): prefers the minimum hop-count candidate
//   among those NOT reached via a priority-only edge AND not flagged
//   unhealthy in `neighborUnhealthy` (Phase 2 — see docs/decisions.md for
//   why this sits alongside, not instead of, the priority-only-edge
//   exclusion). If every remaining eligible candidate is unhealthy, falls
//   back to the minimum hop-count one anyway — a degraded link is still
//   better than no link, so link health gates *preference*, not
//   *validity*; only Phase 1's staleness/invalidity mechanism controls
//   whether a candidate exists at all.
// priority == true (PRIORITY): minimum hop-count candidate over ALL
//   candidates, completely ignoring `neighborUnhealthy` — will prefer a
//   priority-only edge whenever it's shorter, regardless of link health.
// `neighborUnhealthy`, if non-null, is a NODE_ID_COUNT-length array
// indexed by via-neighbor NodeId; a Phase-1-only caller (or a Phase 1
// regression test) can omit it entirely (nullptr, the default) and get
// exactly Phase 1's original behavior back.
// Ties break toward the lowest NodeId value (deterministic, arbitrary,
// documented — not a quality judgment).
// Returns NODE_ID_UNKNOWN if destination is out of range, is this node
// itself, or has no valid candidate. Never returns `state.self`.
NodeId selectNextHop(const RoutingState& state, NodeId destination, bool priority,
                      uint8_t* outHopCount = nullptr,
                      const bool* neighborUnhealthy = nullptr);

// ============================================================
// Phase 5 (UCB1, stretch/optional) — purely additive. Does not change
// selectNextHop() or any existing behavior above; always compiled and
// host-testable regardless of ENABLE_UCB1 (see config.h), exactly like
// every other function in this file. Exists so a higher adaptive-ranking
// layer (src/ucb1/) can enumerate what routing already considers valid,
// rather than re-deriving routing's own validity/health/priority-only-edge
// rules — "UCB1 ranks only among valid candidates ... must never create a
// route the normal routing layer would consider invalid" (Phase 5 task
// spec, Part 4). See docs/decisions.md.
// ============================================================

// One NORMAL-eligible candidate for a destination: a real, non-stale,
// non-priority-only-edge route, annotated with its current health.
struct CandidateInfo {
  NodeId nextHop;
  uint8_t hopCount;
  bool healthy;  // true if `neighborUnhealthy` was null or didn't flag this via-neighbor — mirrors selectNextHop()'s own convention
};

// Enumerates every currently-valid NORMAL candidate for `destination` —
// the exact same validity rule selectNextHop() applies in its own NORMAL
// (priority=false) branch (non-stale, excludes priority-only edges), plus
// an optional `excludeNextHop` (NODE_ID_UNKNOWN = no exclusion) for the
// UCB1 loop-prevention guard (Part 8) — never enumerates a candidate equal
// to `excludeNextHop`. Returns the count written (<= maxOut, itself
// <= NODE_ID_COUNT). Order is ascending NodeId (via 0..NODE_ID_COUNT-1),
// matching selectNextHop()'s own tie-break convention.
uint8_t enumerateCandidates(const RoutingState& state, NodeId destination,
                             const bool* neighborUnhealthy, NodeId excludeNextHop,
                             CandidateInfo* out, uint8_t maxOut);

// ============================================================
// Phase 7.1 (red-team finding: GUI contract's ROUTE_UPDATE.hops must be an
// ordered full path, with hopCount == hops.length-1 — a real, provable
// invariant firmware was violating by reporting only [self, nextHop] next
// to the real, longer routing_core hop count). Purely additive, read-only,
// stateless (no RoutingState needed) — for TELEMETRY REPORTING ONLY. Never
// called by, and never influences, selectNextHop()/getNextHop()/any
// routing decision. See docs/decisions.md.
// ============================================================

// Attempts to reconstruct the full node sequence from `self` to
// `destination` via direct neighbor `via`, given a real `hopCount`
// routing_core already computed for that (destination, via) candidate.
// Searches ONLY the fixed, compiled-in static adjacency graph
// (core/node_id.h::neighborsOf() — real structural data every node's
// firmware already has, identical on all five boards, not fabricated) for
// a loop-free path of EXACTLY `hopCount` edges from `via` to `destination`
// that never revisits `self` (a real simple path can never pass back
// through its own origin). Writes `[self, via, ..., destination]` into
// `out` (length hopCount+1) and returns that length ONLY when such a path
// exists AND is the UNIQUE one the static graph admits at that exact
// length — if the graph allows zero or more than one distinct path of
// that length (a real possibility for some (self, destination) pairs in
// this topology — see docs/decisions.md), this returns 0 rather than ever
// guessing which one actually carried the traffic. Bounded by `maxOut`
// (must be >= 2) and by NODE_ID_COUNT — a real loop-free path can never
// have more nodes than exist in the whole topology.
uint8_t reconstructPath(NodeId self, NodeId destination, NodeId via, uint8_t hopCount,
                         NodeId* out, uint8_t maxOut);

}  // namespace routing_core
