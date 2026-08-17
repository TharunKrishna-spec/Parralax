#include "reliability.h"
#include "../config.h"
#include "../core/logger.h"
#include "../core/message_types.h"
#include "../routing/routing.h"
#include "../predictor/predictor.h"
#include "../transport/espnow_transport.h"
#include <Arduino.h>
#include <string.h>

namespace {

reliability_core::ReliabilityState g_state;

// Adapter-owned raw packet bytes for retransmission — reliability_core
// itself never stores a payload (see reliability_core.h's file header for
// why), so a resend needs the original MeshPacket bytes from somewhere.
// Parallel-indexed to g_state.pending[] by slot; a slot's bytes are only
// ever read after reliability_core reports that slot as RETRY, at which
// point beginTx() guarantees the bytes were written before this slot could
// have become active.
MeshPacket g_pendingPackets[RELIABILITY_MAX_PENDING];

reliability::ReliabilityEventCallback g_eventCallback = nullptr;

// Wire format for one MSG_ACK payload: which (source, sequence) identity
// is being acknowledged (Part 1/3). Packed for the same reason routing.cpp's
// RouteAdWire is — a MeshPacket's payload[] is a byte array with no
// alignment guarantee beyond 1 byte at any given offset; `packed` forces
// the compiler to emit unaligned-safe loads/stores for the uint16_t field
// instead of risking a LoadStoreError on real Xtensa hardware (see
// core/packet.h's own layout-notes comment).
#pragma pack(push, 1)
struct AckWire {
  uint8_t source;
  uint16_t sequence;
};
#pragma pack(pop)

void fireEvent(reliability::ReliabilityEventType type, NodeId source, uint16_t sequence, NodeId neighbor,
               uint8_t attemptCount, const uint8_t* payload = nullptr, uint8_t payloadLen = 0) {
  if (g_eventCallback == nullptr) return;
  reliability::ReliabilityEvent evt{ type, source, sequence, neighbor, attemptCount, payload, payloadLen };
  g_eventCallback(evt);
}

// ACK packets are fire-and-forget — they are never themselves acknowledged
// (an ACK-of-an-ACK would recurse forever). See docs/decisions.md.
void sendAck(NodeId to, NodeId ackedSource, uint16_t ackedSequence) {
  MeshPacket ack;
  packetInit(ack, MSG_ACK, THIS_NODE_ID, to);
  ack.next_hop = to;
  ack.timestamp_ms = millis();

  AckWire* wire = reinterpret_cast<AckWire*>(ack.payload);
  wire->source = static_cast<uint8_t>(ackedSource);
  wire->sequence = ackedSequence;
  ack.payload_len = sizeof(AckWire);

  bool sent = transport::send(nodeInfo(to).mac, reinterpret_cast<const uint8_t*>(&ack), packetWireSize(ack));
  if (!sent) {
    logger::warn("[RELIABILITY] ACK to %s for source=%s seq=%u could not be sent (peer not registered?)",
                 nodeName(to), nodeName(ackedSource), static_cast<unsigned>(ackedSequence));
  }
}

// Reserves tracking for one new outgoing hop-transmission and issues its
// first real radio send. Shared by reliability::send() (Part 12, self-
// originated) and the forwarding path (Part 7) below — the "unicast this
// MeshPacket to `nextHop`, then track it" mechanics are identical either
// way; only how `pkt` was constructed differs.
bool transmitHop(MeshPacket& pkt, NodeId nextHop) {
  uint32_t now = millis();
  NodeId source = static_cast<NodeId>(pkt.source);

  uint8_t slot = reliability_core::beginTx(g_state, source, pkt.sequence, nextHop, now);
  if (slot == reliability_core::INVALID_SLOT) {
    logger::warn("[RELIABILITY] pending pool full - dropping hop-transmission source=%s seq=%u, never sent",
                 nodeName(source), static_cast<unsigned>(pkt.sequence));
    reliability_core::recordImmediateFailure(g_state);
    fireEvent(reliability::ReliabilityEventType::PACKET_DROP, source, pkt.sequence, nextHop, 0);
    return false;
  }

  pkt.prev_hop = THIS_NODE_ID;
  pkt.next_hop = nextHop;
  pkt.timestamp_ms = now;
  g_pendingPackets[slot] = pkt;

  bool sent = transport::send(nodeInfo(nextHop).mac, reinterpret_cast<const uint8_t*>(&pkt), packetWireSize(pkt));
  if (!sent) {
    logger::warn("[RELIABILITY] unicast to %s rejected immediately (peer not registered - see docs/known-issues.md) "
                 "source=%s seq=%u",
                 nodeName(nextHop), nodeName(source), static_cast<unsigned>(pkt.sequence));
    reliability_core::cancelTx(g_state, slot);
    fireEvent(reliability::ReliabilityEventType::PACKET_DROP, source, pkt.sequence, nextHop, 1);
    return false;
  }

  logger::info("[RELIABILITY] TX source=%s seq=%u next=%s attempt=1", nodeName(source),
               static_cast<unsigned>(pkt.sequence), nodeName(nextHop));
  fireEvent(reliability::ReliabilityEventType::PACKET_TX, source, pkt.sequence, nextHop, 1);
  return true;
}

void handleAck(const MeshPacket& pkt) {
  if (pkt.payload_len < sizeof(AckWire)) {
    logger::warn("[RELIABILITY] MSG_ACK payload too short (%u bytes) - dropped", static_cast<unsigned>(pkt.payload_len));
    return;
  }
  const AckWire* wire = reinterpret_cast<const AckWire*>(pkt.payload);
  NodeId ackedSource = static_cast<NodeId>(wire->source);
  uint16_t ackedSequence = wire->sequence;

  reliability_core::AckResult r = reliability_core::onAckReceived(g_state, ackedSource, ackedSequence, millis());
  if (!r.matched) {
    logger::debug("[RELIABILITY] ACK for unknown/stale identity source=%s seq=%u ignored", nodeName(ackedSource),
                  static_cast<unsigned>(ackedSequence));
    return;
  }

  logger::info("[RELIABILITY] ACK matched source=%s seq=%u neighbor=%s latency_ms=%u", nodeName(ackedSource),
               static_cast<unsigned>(ackedSequence), nodeName(r.nextHop), static_cast<unsigned>(r.latencyMs));
  fireEvent(reliability::ReliabilityEventType::PACKET_ACK, ackedSource, ackedSequence, r.nextHop, 0);
  fireEvent(reliability::ReliabilityEventType::PACKET_DELIVERED, ackedSource, ackedSequence, r.nextHop, 0);

  // Part 8: the real PDR observation source — per-attempt, per-hop, via
  // the predictor's own clean API. See docs/decisions.md.
  predictor::onSendResult(r.nextHop, true);
}

void handleData(const MeshPacket& pkt, int8_t rssi) {
  (void)rssi;  // predictor::onPacketReceived() (main.cpp) already samples RSSI for every received packet, regardless of type
  NodeId source = static_cast<NodeId>(pkt.source);
  NodeId prevHop = static_cast<NodeId>(pkt.prev_hop);

  // Part 6/7: this specific hop transmission genuinely succeeded — ACK the
  // sender first, unconditionally, before any duplicate/forward decision.
  // A duplicate or an unforwardable packet is still real evidence that
  // `prevHop`'s transmission to us worked; withholding the ACK would just
  // make `prevHop` retry a hop that already succeeded.
  sendAck(prevHop, source, pkt.sequence);

  bool duplicate = reliability_core::isDuplicateAndRecord(g_state, source, pkt.sequence, millis());
  if (duplicate) {
    logger::debug("[RELIABILITY] duplicate dropped source=%s seq=%u (already seen)", nodeName(source),
                  static_cast<unsigned>(pkt.sequence));
    fireEvent(reliability::ReliabilityEventType::DUPLICATE_DROPPED, source, pkt.sequence, prevHop, 0);
    return;
  }

  if (pkt.destination == THIS_NODE_ID) {
    logger::info("[RELIABILITY] DATA delivered source=%s seq=%u len=%u", nodeName(source),
                 static_cast<unsigned>(pkt.sequence), static_cast<unsigned>(pkt.payload_len));
    fireEvent(reliability::ReliabilityEventType::PACKET_RECEIVED, source, pkt.sequence, prevHop, 0, pkt.payload,
              pkt.payload_len);
    return;
  }

  // Part 7: not for us — forward it, reusing the exact Phase 1/2 routing
  // decision (Part 12: "do not move routing logic into reliability").
  NodeId nextHop = routing::selectNextHop(pkt);
  if (nextHop == NODE_ID_UNKNOWN || nextHop == prevHop) {
    // No valid route, or routing would bounce it straight back to whoever
    // just sent it to us — either way forwarding here would fail outright
    // or create an immediate one-hop loop (Part 7: "do not allow
    // forwarding to create loops"). See docs/decisions.md.
    logger::warn("[RELIABILITY] cannot forward source=%s seq=%u dest=%s - no safe next hop (routing=%s)",
                 nodeName(source), static_cast<unsigned>(pkt.sequence), nodeName(static_cast<NodeId>(pkt.destination)),
                 nextHop == NODE_ID_UNKNOWN ? "NONE" : nodeName(nextHop));
    fireEvent(reliability::ReliabilityEventType::PACKET_DROP, source, pkt.sequence, prevHop, 0);
    return;
  }

  MeshPacket forwarded = pkt;  // preserves source/sequence/type/priority/payload unchanged (Part 6)
  transmitHop(forwarded, nextHop);
}

}  // namespace

namespace reliability {

void init() {
  reliability_core::init(g_state, THIS_NODE_ID);
  memset(g_pendingPackets, 0, sizeof(g_pendingPackets));
  logger::info("reliability: init (hop-by-hop ACK + bounded retry + duplicate filter + forwarding, Phase 4)");
}

void onPacketReceived(const MeshPacket& pkt, int8_t rssi) {
  if (pkt.type == MSG_ACK) {
    handleAck(pkt);
  } else if (pkt.type == MSG_DATA) {
    handleData(pkt, rssi);
  }
  // MSG_HEARTBEAT is routing's own concern (see main.cpp) — reliability
  // does not react to it.
}

void tick() {
  reliability_core::TimeoutEvent events[RELIABILITY_MAX_PENDING];
  uint8_t n = reliability_core::tickTimeouts(g_state, millis(), events, RELIABILITY_MAX_PENDING);

  for (uint8_t i = 0; i < n; i++) {
    const reliability_core::TimeoutEvent& te = events[i];

    if (te.action == reliability_core::TimeoutAction::RETRY) {
      MeshPacket& pkt = g_pendingPackets[te.slot];
      logger::warn("[RELIABILITY] ACK timeout - retrying source=%s seq=%u next=%s attempt=%u",
                   nodeName(te.id.source), static_cast<unsigned>(te.id.sequence), nodeName(te.nextHop),
                   static_cast<unsigned>(te.attemptCount));
      transport::send(nodeInfo(te.nextHop).mac, reinterpret_cast<const uint8_t*>(&pkt), packetWireSize(pkt));
      fireEvent(ReliabilityEventType::PACKET_RETRY, te.id.source, te.id.sequence, te.nextHop, te.attemptCount);
      // Part 8/9: each individually-timed-out attempt is one failed PDR
      // observation, whether or not more retries remain — see
      // docs/decisions.md.
      predictor::onSendResult(te.nextHop, false);
    } else {
      logger::warn("[RELIABILITY] delivery FAILED after %u attempts - source=%s seq=%u next=%s",
                   static_cast<unsigned>(te.attemptCount), nodeName(te.id.source),
                   static_cast<unsigned>(te.id.sequence), nodeName(te.nextHop));
      fireEvent(ReliabilityEventType::PACKET_DROP, te.id.source, te.id.sequence, te.nextHop, te.attemptCount);
      predictor::onSendResult(te.nextHop, false);
    }
  }
}

bool send(NodeId destination, const uint8_t* payload, uint8_t len, bool priority) {
  if (destination >= NODE_ID_COUNT || destination == THIS_NODE_ID) {
    logger::warn("reliability::send: invalid destination %u", static_cast<unsigned>(destination));
    return false;
  }
  if (len > PACKET_MAX_PAYLOAD) {
    logger::warn("reliability::send: payload too large (%u > %u)", static_cast<unsigned>(len),
                 static_cast<unsigned>(PACKET_MAX_PAYLOAD));
    return false;
  }

  NodeId nextHop = routing::getNextHop(destination, priority);
  if (nextHop == NODE_ID_UNKNOWN) {
    logger::warn("reliability::send: no route to %s", nodeName(destination));
    return false;
  }

  MeshPacket pkt;
  packetInit(pkt, MSG_DATA, THIS_NODE_ID, destination);
  pkt.priority = priority ? 1 : 0;
  pkt.sequence = reliability_core::nextSequence(g_state);
  if (payload != nullptr && len > 0) {
    memcpy(pkt.payload, payload, len);
  }
  pkt.payload_len = len;

  return transmitHop(pkt, nextHop);
}

reliability_core::Statistics getStatistics() {
  return g_state.stats;
}

void setEventCallback(ReliabilityEventCallback cb) {
  g_eventCallback = cb;
}

}  // namespace reliability
