#include "suppression.h"
#include "../config.h"
#include "../core/logger.h"
#include "../core/message_types.h"
#include "../transport/espnow_transport.h"
#include <Arduino.h>
#include <esp_random.h>
#include <string.h>

namespace {

suppression_core::State g_state;

// Adapter-owned raw packet bytes for retransmission — suppression_core
// itself never stores a payload (see suppression_core.h's file header for
// why), mirroring reliability.cpp's g_pendingPackets[] exactly. Parallel-
// indexed to g_state.cache[] by slot.
MeshPacket g_cachedPackets[SUPPRESSION_CACHE_SIZE];

// Adapter-owned per-slot bookkeeping suppression_core doesn't need for its
// own algorithm but telemetry needs to report accurately: the backoff
// duration actually computed at entry-creation time (deadlineMs alone
// doesn't recover this once time has passed).
uint32_t g_scheduledBackoffMs[SUPPRESSION_CACHE_SIZE];

const uint8_t BROADCAST_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

suppression::SuppressionEventCallback g_eventCallback = nullptr;

void fireEvent(suppression::SuppressionEventType type, NodeId source, uint16_t sequence, NodeId destination,
               int8_t rssi, uint8_t overheardCount, uint32_t backoffMs, const uint8_t* payload = nullptr,
               uint8_t payloadLen = 0) {
  if (g_eventCallback == nullptr) return;
  suppression::SuppressionEvent evt{ type, source, sequence, destination, rssi, overheardCount,
                                      backoffMs, payload, payloadLen };
  g_eventCallback(evt);
}

}  // namespace

namespace suppression {

void init() {
  suppression_core::init(g_state);
  memset(g_cachedPackets, 0, sizeof(g_cachedPackets));
  memset(g_scheduledBackoffMs, 0, sizeof(g_scheduledBackoffMs));
  logger::info("suppression: init (opportunistic priority broadcast + overhear + RSSI-aware counter-based suppression)");
}

void onPacketReceived(const MeshPacket& pkt, int8_t rssi) {
  if (pkt.type != MSG_PRIORITY_BROADCAST) return;

  NodeId prevHop = static_cast<NodeId>(pkt.prev_hop);
  NodeId source = static_cast<NodeId>(pkt.source);

  // Never process a reception of our own transmission (real ESP-NOW
  // broadcast does not loop back to the sender, but this guard is cheap
  // and makes that assumption explicit rather than silently relied upon —
  // see docs/decisions.md). Checked on both prevHop and source: prevHop
  // catches "this exact copy passed through us most recently"; source
  // catches "we are the original author of this identity", which matters
  // even if some future relay ever rewrote prevHop unexpectedly.
  if (prevHop == THIS_NODE_ID || source == THIS_NODE_ID) return;

  bool isLocalDestination = (static_cast<NodeId>(pkt.destination) == THIS_NODE_ID);
  suppression_core::PacketId id{ source, pkt.sequence };

  uint32_t now = millis();
  uint32_t jitterMs = esp_random() % SUPPRESSION_JITTER_MAX_MS;
  uint32_t backoffMs = suppression_core::computeBackoffMs(rssi, jitterMs);
  uint32_t deadlineMs = now + backoffMs;

  suppression_core::ReceiveResult r =
      suppression_core::onReceive(g_state, id, prevHop, isLocalDestination, rssi, deadlineMs, now);

  switch (r.outcome) {
    case suppression_core::ReceiveOutcome::NEW_ENTRY:
      g_cachedPackets[r.slot] = pkt;
      g_scheduledBackoffMs[r.slot] = backoffMs;
      if (isLocalDestination) {
        logger::info("[SUPPRESSION] PRIORITY delivered source=%s seq=%u len=%u", nodeName(source),
                     static_cast<unsigned>(pkt.sequence), static_cast<unsigned>(pkt.payload_len));
        fireEvent(SuppressionEventType::PRIORITY_DELIVERED, source, pkt.sequence,
                  static_cast<NodeId>(pkt.destination), rssi, r.overheardCount, 0, pkt.payload, pkt.payload_len);
      } else {
        logger::info("[SUPPRESSION] new priority identity source=%s seq=%u rssi=%d backoff=%ums", nodeName(source),
                     static_cast<unsigned>(pkt.sequence), rssi, static_cast<unsigned>(backoffMs));
      }
      break;

    case suppression_core::ReceiveOutcome::OVERHEARD:
    case suppression_core::ReceiveOutcome::ALREADY_DECIDED:
      logger::debug("[SUPPRESSION] overheard relay source=%s seq=%u count=%u", nodeName(source),
                    static_cast<unsigned>(pkt.sequence), static_cast<unsigned>(r.overheardCount));
      fireEvent(SuppressionEventType::PRIORITY_OVERHEARD, source, pkt.sequence,
                static_cast<NodeId>(pkt.destination), rssi, r.overheardCount, 0);
      break;

    case suppression_core::ReceiveOutcome::DUPLICATE_OF_ORIGINAL:
      logger::debug("[SUPPRESSION] duplicate of original source=%s seq=%u ignored", nodeName(source),
                    static_cast<unsigned>(pkt.sequence));
      break;

    case suppression_core::ReceiveOutcome::CACHE_FULL:
      logger::warn("[SUPPRESSION] cache full - dropping new priority identity source=%s seq=%u", nodeName(source),
                   static_cast<unsigned>(pkt.sequence));
      break;
  }
}

void tick() {
  suppression_core::ReadyDecision decisions[SUPPRESSION_CACHE_SIZE];
  uint32_t now = millis();
  uint8_t n = suppression_core::tickDecisions(g_state, now, decisions, SUPPRESSION_CACHE_SIZE);

  for (uint8_t i = 0; i < n; i++) {
    const suppression_core::ReadyDecision& d = decisions[i];
    MeshPacket& pkt = g_cachedPackets[d.slot];
    NodeId destination = static_cast<NodeId>(pkt.destination);

    if (d.decision == suppression_core::Decision::TRANSMIT) {
      pkt.prev_hop = THIS_NODE_ID;
      pkt.timestamp_ms = now;
      bool sent = transport::send(BROADCAST_MAC, reinterpret_cast<const uint8_t*>(&pkt), packetWireSize(pkt));
      logger::info("[SUPPRESSION] PRIORITY forward source=%s seq=%u overheard=%u backoff=%ums sent=%d",
                   nodeName(d.id.source), static_cast<unsigned>(d.id.sequence),
                   static_cast<unsigned>(d.overheardCountAtDecision), static_cast<unsigned>(g_scheduledBackoffMs[d.slot]),
                   sent ? 1 : 0);
      if (sent) {
        fireEvent(SuppressionEventType::PRIORITY_FORWARD, d.id.source, d.id.sequence, destination, 0,
                  d.overheardCountAtDecision, g_scheduledBackoffMs[d.slot]);
      }
    } else {
      logger::info("[SUPPRESSION] PRIORITY suppressed source=%s seq=%u overheard=%u (threshold=%u)",
                   nodeName(d.id.source), static_cast<unsigned>(d.id.sequence),
                   static_cast<unsigned>(d.overheardCountAtDecision), static_cast<unsigned>(SUPPRESSION_THRESHOLD));
      fireEvent(SuppressionEventType::PRIORITY_SUPPRESSED, d.id.source, d.id.sequence, destination, 0,
                d.overheardCountAtDecision, 0);
    }
  }

  suppression_core::expireCache(g_state, now);
}

bool broadcastPriority(const uint8_t* payload, uint8_t len) {
  if (len > PACKET_MAX_PAYLOAD) {
    logger::warn("suppression::broadcastPriority: payload too large (%u > %u)", static_cast<unsigned>(len),
                 static_cast<unsigned>(PACKET_MAX_PAYLOAD));
    return false;
  }

  uint32_t now = millis();
  uint16_t sequence = suppression_core::nextSequence(g_state);
  suppression_core::PacketId id{ THIS_NODE_ID, sequence };

  uint8_t slot = suppression_core::recordOwnOrigination(g_state, id, now);
  if (slot == suppression_core::INVALID_SLOT) {
    logger::warn("suppression::broadcastPriority: cache full - priority broadcast not sent source=%s seq=%u",
                 nodeName(THIS_NODE_ID), static_cast<unsigned>(sequence));
    return false;
  }

  MeshPacket pkt;
  packetInit(pkt, MSG_PRIORITY_BROADCAST, THIS_NODE_ID, NODE_S);
  pkt.priority = 1;
  pkt.sequence = sequence;
  pkt.timestamp_ms = now;
  if (payload != nullptr && len > 0) {
    memcpy(pkt.payload, payload, len);
  }
  pkt.payload_len = len;

  g_cachedPackets[slot] = pkt;
  g_scheduledBackoffMs[slot] = 0;  // originator never waited a backoff — it transmitted immediately

  bool sent = transport::send(BROADCAST_MAC, reinterpret_cast<const uint8_t*>(&pkt), packetWireSize(pkt));
  logger::info("[SUPPRESSION] PRIORITY broadcast originated seq=%u len=%u sent=%d", static_cast<unsigned>(sequence),
               static_cast<unsigned>(len), sent ? 1 : 0);
  if (sent) {
    fireEvent(SuppressionEventType::PRIORITY_BROADCAST, THIS_NODE_ID, sequence, NODE_S, 0, 0, 0);
  }
  return sent;
}

void setEventCallback(SuppressionEventCallback cb) {
  g_eventCallback = cb;
}

}  // namespace suppression
