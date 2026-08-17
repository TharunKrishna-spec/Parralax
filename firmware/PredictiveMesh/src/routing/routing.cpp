#include "routing.h"
#include "routing_core.h"
#include "../config.h"
#include "../core/logger.h"
#include "../core/message_types.h"
#include "../transport/espnow_transport.h"
#include <Arduino.h>

namespace {

routing_core::RoutingState g_state;
routing::RouteEventCallback g_eventCallback = nullptr;
uint32_t g_lastBeaconMs = 0;

// Wire format for one distance-vector entry inside a MSG_HEARTBEAT
// payload: [count:1][ (destination:1, hop_count:1) x count ]. Both fields
// are single bytes, so there's no multi-byte alignment concern reading
// this back out of pkt.payload the way there is for MeshPacket's own
// header fields (see core/packet.h) - no memcpy needed here.
#pragma pack(push, 1)
struct RouteAdWire {
  uint8_t destination;
  uint8_t hop_count;
};
#pragma pack(pop)

void fireEvent(routing::RouteEventType type, NodeId dest, NodeId nextHop, uint8_t hops, bool priority) {
  if (g_eventCallback == nullptr) return;
  routing::RouteEvent evt{ type, dest, nextHop, hops, priority };
  g_eventCallback(evt);
}

// Broadcasts this node's current distance vector as a MSG_HEARTBEAT. This
// single beacon serves as both the HELLO (its mere arrival proves the
// sender is alive - see routing::onPacketReceived) and the route
// advertisement (its payload). Destination is NODE_ID_UNKNOWN because a
// beacon addresses all direct neighbors at once, not one specific node -
// the same sentinel packetInit() already uses for "no specific next hop
// decided yet", reused here for "no specific destination", both meaning
// "not one particular node."
void sendBeacon() {
  routing_core::RouteAdEntry entries[NODE_ID_COUNT];
  uint8_t count = routing_core::buildAdvertisement(g_state, entries, NODE_ID_COUNT);

  MeshPacket pkt;
  packetInit(pkt, MSG_HEARTBEAT, THIS_NODE_ID, NODE_ID_UNKNOWN);
  pkt.timestamp_ms = millis();

  pkt.payload[0] = count;
  RouteAdWire* wire = reinterpret_cast<RouteAdWire*>(pkt.payload + 1);
  for (uint8_t i = 0; i < count; i++) {
    wire[i].destination = static_cast<uint8_t>(entries[i].destination);
    wire[i].hop_count = entries[i].hop_count;
  }
  pkt.payload_len = static_cast<uint8_t>(1 + count * sizeof(RouteAdWire));

  static const uint8_t BROADCAST_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  transport::send(BROADCAST_MAC, reinterpret_cast<const uint8_t*>(&pkt), packetWireSize(pkt));
}

// Fires ROUTE_CHANGED for every destination after an advertisement altered
// the table, so a later phase can react without routing.cpp knowing who's
// listening. Uses priority=true (all candidates) to report this node's
// objectively best route, not a normal/priority-filtered one.
void announceChangedRoutes() {
  for (uint8_t d = 0; d < NODE_ID_COUNT; d++) {
    if (d == THIS_NODE_ID) continue;
    uint8_t hop = 0;
    NodeId via = routing_core::selectNextHop(g_state, static_cast<NodeId>(d), true, &hop);
    fireEvent(routing::RouteEventType::ROUTE_CHANGED, static_cast<NodeId>(d), via, hop, false);
  }
}

void processRouteUpdate(const MeshPacket& pkt, uint32_t now) {
  if (pkt.payload_len < 1) return;

  uint8_t count = pkt.payload[0];
  uint8_t maxFit = static_cast<uint8_t>((pkt.payload_len - 1) / sizeof(RouteAdWire));
  if (count > maxFit) count = maxFit;          // never read past a short/corrupt payload
  if (count > NODE_ID_COUNT) count = NODE_ID_COUNT;

  const RouteAdWire* wire = reinterpret_cast<const RouteAdWire*>(pkt.payload + 1);
  routing_core::RouteAdEntry entries[NODE_ID_COUNT];
  for (uint8_t i = 0; i < count; i++) {
    entries[i].destination = static_cast<NodeId>(wire[i].destination);
    entries[i].hop_count = wire[i].hop_count;
  }

  NodeId from = static_cast<NodeId>(pkt.prev_hop);
  bool changed = routing_core::applyRouteAdvertisement(g_state, from, entries, count, now);
  if (changed) {
    logger::debug("routing: table changed after advertisement from %s", nodeName(from));
    announceChangedRoutes();
  }
}

}  // namespace

namespace routing {

void init() {
  routing_core::init(g_state, THIS_NODE_ID);
  g_lastBeaconMs = 0;
  logger::info("routing: init (distance-vector + priority override, Phase 1)");
}

void onPacketReceived(const MeshPacket& pkt, int8_t rssi) {
  NodeId from = static_cast<NodeId>(pkt.prev_hop);
  uint32_t now = millis();

  routing_core::noteNeighborSeen(g_state, from, rssi, now);

  if (pkt.type == MSG_HEARTBEAT) {
    processRouteUpdate(pkt, now);
  }
}

void tick() {
  uint32_t now = millis();

  if (now - g_lastBeaconMs >= ROUTING_HELLO_INTERVAL_MS) {
    g_lastBeaconMs = now;
    sendBeacon();
  }

  uint8_t invalidated = routing_core::expireStale(g_state, now, ROUTING_ENTRY_TIMEOUT_MS);
  if (invalidated > 0) {
    logger::warn("routing: expired %u stale neighbor/route entry(ies)", static_cast<unsigned>(invalidated));
    for (uint8_t d = 0; d < NODE_ID_COUNT; d++) {
      if (d == THIS_NODE_ID) continue;
      uint8_t hop = 0;
      NodeId via = routing_core::selectNextHop(g_state, static_cast<NodeId>(d), true, &hop);
      if (via == NODE_ID_UNKNOWN) {
        fireEvent(RouteEventType::ROUTE_INVALIDATED, static_cast<NodeId>(d), NODE_ID_UNKNOWN, 0, false);
      }
    }
  }
}

NodeId getNextHop(NodeId destination, bool priority) {
  uint8_t hop = 0;
  NodeId next = routing_core::selectNextHop(g_state, destination, priority, &hop);

  logger::info("[ROUTE] dst=%s next=%s hops=%u priority=%d",
               nodeName(destination),
               next == NODE_ID_UNKNOWN ? "NONE" : nodeName(next),
               static_cast<unsigned>(hop), priority ? 1 : 0);

  fireEvent(RouteEventType::ROUTE_SELECTED, destination, next, hop, priority);
  return next;
}

NodeId selectNextHop(const MeshPacket& pkt) {
  if (pkt.destination >= NODE_ID_COUNT) {
    logger::warn("routing::selectNextHop: invalid destination %u", static_cast<unsigned>(pkt.destination));
    return NODE_ID_UNKNOWN;
  }
  return getNextHop(static_cast<NodeId>(pkt.destination), pkt.priority != 0);
}

void setEventCallback(RouteEventCallback cb) {
  g_eventCallback = cb;
}

}  // namespace routing
