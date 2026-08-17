#pragma once
#include "../core/node_id.h"
#include "../core/packet.h"

// ============================================================
// Routing layer (implementation-guide.html §5.3, §01, §06 Hours 2-6).
//
// Phase 1: real distance-vector routing table, HELLO/route-advertisement
// beacons, staleness expiry, and the priority-flag override. The actual
// table math lives in routing_core.h/.cpp (Arduino-free, unit-testable);
// this module is the thin Arduino-facing adapter — millis()/logger::*/
// transport::send() live here, nowhere in routing_core.
//
// Still NOT implemented (later phases): link_score/RSSI-based selection
// (Phase 2), UCB1 (stretch phase), hop-by-hop ACK/retransmit/dup-filter
// (§5.4 reliability layer), any real packet relaying of non-self-destined
// DATA traffic (that's the reliability layer's job once it exists — this
// module only decides next_hop, it doesn't act on it for anyone else's
// packets).
// ============================================================

namespace routing {

enum class RouteEventType : uint8_t {
  ROUTE_SELECTED,     // a next-hop decision was just made for an outgoing packet
  ROUTE_CHANGED,      // an incoming advertisement changed a stored candidate
  ROUTE_INVALIDATED,  // staleness expiry left a destination with no valid route
};

struct RouteEvent {
  RouteEventType type;
  NodeId destination;
  NodeId next_hop;   // NODE_ID_UNKNOWN if no valid route exists
  uint8_t hop_count; // 0 when next_hop == NODE_ID_UNKNOWN
  bool priority;
};

typedef void (*RouteEventCallback)(const RouteEvent& event);

void init();

// Feed every received MeshPacket here, regardless of type. Always
// refreshes neighbor liveness for pkt.prev_hop (the "HELLO" half); when
// pkt.type == MSG_HEARTBEAT, additionally parses the payload as a
// distance-vector advertisement and applies it (the "route update" half).
// See docs/decisions.md for why these two conceptual steps are one wire
// message instead of two.
void onPacketReceived(const MeshPacket& pkt, int8_t rssi);

// Call from the main loop on every iteration; it rate-limits itself.
// Sends this node's own beacon when ROUTING_HELLO_INTERVAL_MS has elapsed,
// and sweeps neighbor/route tables for entries older than
// ROUTING_ENTRY_TIMEOUT_MS.
void tick();

// Lower-level next-hop decision, independent of any specific packet. Logs
// the decision as `[ROUTE] dst=... next=... hops=... priority=...` and
// fires a ROUTE_SELECTED event every time it's called.
NodeId getNextHop(NodeId destination, bool priority);

// Convenience wrapper reading destination/priority out of `pkt`.
NodeId selectNextHop(const MeshPacket& pkt);

// Registers a callback for ROUTE_SELECTED/ROUTE_CHANGED/ROUTE_INVALIDATED
// events. Later phases (predictor-triggered rerouting, telemetry/dashboard)
// consume this instead of routing.cpp growing new callers. At most one
// callback is supported (Phase 0's transport module uses the same
// single-callback pattern).
void setEventCallback(RouteEventCallback cb);

}  // namespace routing
