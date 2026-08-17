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
// priority == false (NORMAL): minimum hop-count candidate, excluding any
//   candidate reached via a priority-only edge.
// priority == true (PRIORITY): minimum hop-count candidate over ALL
//   candidates — will prefer a priority-only edge whenever it's shorter.
// Ties break toward the lowest NodeId value (deterministic, arbitrary,
// documented — not a quality judgment).
// Returns NODE_ID_UNKNOWN if destination is out of range, is this node
// itself, or has no valid candidate. Never returns `state.self`.
NodeId selectNextHop(const RoutingState& state, NodeId destination, bool priority,
                      uint8_t* outHopCount = nullptr);

}  // namespace routing_core
