#include "routing_core.h"

namespace routing_core {

void init(RoutingState& state, NodeId self) {
  state.self = self;
  for (uint8_t n = 0; n < NODE_ID_COUNT; n++) {
    state.neighbors[n].valid = false;
    state.neighbors[n].last_rssi = 0;
    state.neighbors[n].last_seen_ms = 0;
    for (uint8_t via = 0; via < NODE_ID_COUNT; via++) {
      state.candidates[n][via].valid = false;
      state.candidates[n][via].hop_count = MAX_HOP_COUNT;
      state.candidates[n][via].last_update_ms = 0;
    }
  }
}

void noteNeighborSeen(RoutingState& state, NodeId from, int8_t rssi, uint32_t now) {
  if (from >= NODE_ID_COUNT || from == state.self) return;
  state.neighbors[from].valid = true;
  state.neighbors[from].last_rssi = rssi;
  state.neighbors[from].last_seen_ms = now;
}

bool applyRouteAdvertisement(RoutingState& state, NodeId from,
                              const RouteAdEntry* entries, uint8_t count,
                              uint32_t now) {
  if (from >= NODE_ID_COUNT || from == state.self) return false;

  bool changed = false;
  for (uint8_t i = 0; i < count; i++) {
    NodeId dest = entries[i].destination;
    uint8_t advertised = entries[i].hop_count;

    if (dest >= NODE_ID_COUNT) continue;  // malformed entry, ignore
    if (dest == state.self) continue;     // guard: our own distance to ourselves is never learned from a neighbor
    if (dest == from && advertised != 0) continue;  // guard: a neighbor must claim distance 0 to itself, or the entry is invalid

    // Distance via `from` is always its claimed distance plus exactly one
    // more hop — this is the bound test #9 requires: no advertisement can
    // make a destination look closer than "one hop past whatever the
    // neighbor itself claims."
    uint16_t computed = static_cast<uint16_t>(advertised) + 1;
    uint8_t newHop = (computed >= MAX_HOP_COUNT) ? MAX_HOP_COUNT : static_cast<uint8_t>(computed);

    RouteCandidate& cand = state.candidates[dest][from];
    bool wasValid = cand.valid;
    uint8_t oldHop = cand.hop_count;

    cand.last_update_ms = now;
    if (newHop >= MAX_HOP_COUNT) {
      cand.valid = false;
      cand.hop_count = MAX_HOP_COUNT;
      if (wasValid) changed = true;
    } else {
      cand.valid = true;
      cand.hop_count = newHop;
      if (!wasValid || oldHop != newHop) changed = true;
    }
  }
  return changed;
}

uint8_t expireStale(RoutingState& state, uint32_t now, uint32_t timeoutMs) {
  uint8_t invalidated = 0;

  for (uint8_t n = 0; n < NODE_ID_COUNT; n++) {
    NeighborEntry& nb = state.neighbors[n];
    if (nb.valid && (now - nb.last_seen_ms) > timeoutMs) {
      nb.valid = false;
      invalidated++;
    }
  }

  for (uint8_t dest = 0; dest < NODE_ID_COUNT; dest++) {
    for (uint8_t via = 0; via < NODE_ID_COUNT; via++) {
      RouteCandidate& cand = state.candidates[dest][via];
      if (cand.valid && (now - cand.last_update_ms) > timeoutMs) {
        cand.valid = false;
        invalidated++;
      }
    }
  }

  return invalidated;
}

bool isPriorityOnlyEdge(NodeId a, NodeId b) {
  return (a == NODE_A && b == NODE_S) || (a == NODE_S && b == NODE_A);
}

NodeId selectNextHop(const RoutingState& state, NodeId destination, bool priority,
                      uint8_t* outHopCount) {
  if (outHopCount) *outHopCount = 0;
  if (destination >= NODE_ID_COUNT || destination == state.self) return NODE_ID_UNKNOWN;

  NodeId best = NODE_ID_UNKNOWN;
  uint8_t bestHop = MAX_HOP_COUNT;

  for (uint8_t via = 0; via < NODE_ID_COUNT; via++) {
    const RouteCandidate& cand = state.candidates[destination][via];
    if (!cand.valid) continue;
    if (!priority && isPriorityOnlyEdge(state.self, static_cast<NodeId>(via))) continue;

    // Ascending iteration order means the first strict improvement found
    // is always the lowest-NodeId candidate at that hop count, so this
    // also implements the documented tie-break with no extra branch.
    if (cand.hop_count < bestHop) {
      bestHop = cand.hop_count;
      best = static_cast<NodeId>(via);
    }
  }

  if (outHopCount) *outHopCount = (best == NODE_ID_UNKNOWN) ? 0 : bestHop;
  return best;
}

uint8_t buildAdvertisement(const RoutingState& state, RouteAdEntry* out, uint8_t maxEntries) {
  uint8_t n = 0;
  if (n < maxEntries) {
    out[n].destination = state.self;
    out[n].hop_count = 0;
    n++;
  }

  for (uint8_t dest = 0; dest < NODE_ID_COUNT; dest++) {
    if (dest == state.self) continue;
    uint8_t hop = 0;
    // priority=true here means "consider every candidate, including
    // priority-only edges" so the advertised distance is our objectively
    // best known cost, not policy-filtered. Normal-vs-priority is a
    // selection-time decision, not a fact about the topology.
    NodeId via = selectNextHop(state, static_cast<NodeId>(dest), /*priority=*/true, &hop);
    if (via != NODE_ID_UNKNOWN && n < maxEntries) {
      out[n].destination = static_cast<NodeId>(dest);
      out[n].hop_count = hop;
      n++;
    }
  }
  return n;
}

}  // namespace routing_core
