#pragma once
#include <stdint.h>
#include "../core/node_id.h"
#include "../core/packet.h"
#include "reliability_core.h"

// ============================================================
// Reliability layer (implementation-guide.html §5.4) — Arduino-facing
// adapter, Phase 4.
//
// Owns the single reliability_core::ReliabilityState instance, the raw
// MeshPacket bytes needed to actually retransmit a pending hop-
// transmission (reliability_core itself is payload-agnostic — see
// reliability_core.h), and drives real unicast ESP-NOW sends/ACKs/
// retries/forwarding. The actual retry/timeout/duplicate-filter/
// statistics math lives in reliability_core.h/.cpp (Arduino-free,
// host-testable) — see docs/decisions.md, matching the routing_core/
// predictor_core/anomaly_core splits from Phases 1-3.
//
// Part 12's architecture: application -> routing::selectNextHop() ->
// reliability::send() -> transport::send(). This module never re-derives
// a routing decision itself (Part 12: "do not move routing logic into
// reliability") — it always asks routing:: for the next hop, for both
// self-originated sends (send()) and forwarded packets (Part 7,
// implemented inside onPacketReceived()).
// ============================================================

namespace reliability {

// Part 10 events, plus PACKET_RECEIVED (not in the task spec's example
// list, added so Part 7's "deliver" branch has a real, non-dead-code
// consequence — see docs/decisions.md).
enum class ReliabilityEventType : uint8_t {
  PACKET_TX,
  PACKET_ACK,
  PACKET_RETRY,
  PACKET_DELIVERED,
  PACKET_DROP,
  DUPLICATE_DROPPED,
  PACKET_RECEIVED,
};

struct ReliabilityEvent {
  ReliabilityEventType type;
  NodeId source;          // original packet source (Part 1 identity) — always meaningful
  uint16_t sequence;       // original packet sequence (Part 1 identity) — always meaningful
  NodeId neighbor;         // direct neighbor this hop concerns: next_hop for TX-side events, prev_hop for RX-side events
  NodeId destination;      // the packet's real final destination (Part 1 identity's third field) — always meaningful; added so a telemetry consumer can report a real PACKET path without re-deriving it from routing state
  bool priority;            // the packet's real MeshPacket.priority flag — always meaningful; lets a telemetry consumer report real trafficClass (NORMAL/PRIORITY) instead of guessing
  uint8_t attemptCount;     // meaningful for PACKET_TX/PACKET_ACK/PACKET_RETRY/PACKET_DROP; for PACKET_ACK/PACKET_DELIVERED this is the real, final attempt count reliability_core::AckResult reported (1 = first-try, >1 = recovered after real retries) — never hardcoded; 0 for RX-side events
  const uint8_t* payload;   // valid only for PACKET_RECEIVED, and only for the duration of the callback — copy if needed after it returns
  uint8_t payloadLen;       // valid only for PACKET_RECEIVED
};

typedef void (*ReliabilityEventCallback)(const ReliabilityEvent& event);

void init();

// Feeds every received MeshPacket whose type is MSG_DATA or MSG_ACK.
// Ignores any other type (MSG_HEARTBEAT is routing's own concern — see
// main.cpp's onTransportRx, which calls both routing::onPacketReceived()
// and this function for every received frame).
//
// MSG_ACK: matches against a pending hop-transmission (Part 3) via
// reliability_core::onAckReceived(), feeding the real outcome into
// predictor::onSendResult() (Part 8) through a clean call, never by
// reaching into predictor internals.
//
// MSG_DATA: Part 7's full pipeline — always hop-ACKs the sender first
// (the hop transmission genuinely succeeded, regardless of what happens
// next), then checks the duplicate cache (Part 6); a duplicate is dropped
// without being delivered or forwarded again; a new packet is either
// delivered locally (pkt.destination == this node — fires PACKET_RECEIVED)
// or forwarded via routing::selectNextHop(), preserving the original
// source/sequence unchanged (Part 6/7).
void onPacketReceived(const MeshPacket& pkt, int8_t rssi);

// Call from the main loop on every iteration; sweeps pending hop-
// transmissions for RELIABILITY_ACK_TIMEOUT_MS expiry (Part 5) and
// resends or declares final failure. Never blocks (Part 5).
void tick();

// Part 12's "application" entry point: sends `len` bytes of application
// payload to `destination`, honoring the Phase 1/2 routing decision
// (priority or normal). Returns false immediately if there's no valid
// route or the very first radio send is rejected synchronously (Part 2:
// never fabricates an in-flight transmission for a frame that never left
// the radio) — true means the frame is in flight and being tracked for
// ACK/retry, NOT that delivery is confirmed yet (Part 3).
bool send(NodeId destination, const uint8_t* payload, uint8_t len, bool priority = false);

// Part 11: read-only snapshot of this node's own reliability statistics.
// Not serialized over JSON yet — see docs/known-issues.md's GUI-telemetry-
// contract entry; the existing firmware telemetry architecture doesn't
// support that yet (telemetry.cpp is still the Phase 0 stub).
reliability_core::Statistics getStatistics();

void setEventCallback(ReliabilityEventCallback cb);

}  // namespace reliability
