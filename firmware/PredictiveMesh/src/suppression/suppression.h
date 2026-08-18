#pragma once
#include <stdint.h>
#include "../core/node_id.h"
#include "../core/packet.h"
#include "suppression_core.h"

// ============================================================
// Suppression layer — Arduino-facing adapter for opportunistic priority-
// packet broadcast + overhearing + RSSI-aware counter-based spatial
// suppression (Priority-broadcast milestone, 2026-08-18).
//
// User-confirmed replacement of the previous priority delivery mechanism
// (forced-shortest-hop unicast through reliability::send()) — see
// docs/decisions.md for the full architecture record, including the
// deliberate deviation from implementation-guide.html §5.3. NORMAL traffic
// (reliability::send(..., priority=false)) is completely unaffected —
// this module never touches reliability_core, predictor::onSendResult(),
// or PDR/STATISTICS accounting.
//
// Owns the single suppression_core::State instance, the raw MeshPacket
// bytes needed to actually (re)transmit a cached priority broadcast
// (suppression_core itself is payload-agnostic — see suppression_core.h),
// and drives real ESP-NOW broadcast sends. Mirrors reliability.cpp's own
// established adapter/core split.
//
// RX-callback discipline (Part 6): onPacketReceived() does only bounded,
// allocation-free work — identify the packet, look up/insert a cache
// entry, compute an RSSI-aware backoff deadline. It never transmits and
// never makes the suppress-or-forward decision; that is entirely tick()'s
// job, called from the main loop. This is a deliberately new pattern for
// this codebase (routing/predictor/reliability's own onPacketReceived()
// hooks already do their full processing synchronously in the ESP-NOW
// callback today) — see docs/decisions.md for why this module holds
// itself to a stricter standard.
// ============================================================

namespace suppression {

enum class SuppressionEventType : uint8_t {
  PRIORITY_BROADCAST,    // this node just originated a new priority broadcast
  PRIORITY_OVERHEARD,     // this node heard another node's rebroadcast of an already-known identity
  PRIORITY_FORWARD,        // this node's own backoff expired below threshold — it just rebroadcast
  PRIORITY_SUPPRESSED,      // this node's own backoff expired at/above threshold — it stayed silent
  PRIORITY_DELIVERED,        // this node is the real destination and just received this identity for the first time
};

struct SuppressionEvent {
  SuppressionEventType type;
  NodeId source;            // original packet source (identity) — always meaningful
  uint16_t sequence;         // original packet sequence (identity) — always meaningful
  NodeId destination;         // the packet's real final destination — always meaningful
  int8_t rssi;                  // RSSI of the reception that produced this event — meaningful for OVERHEARD/DELIVERED only; reported as 0 (not applicable) for BROADCAST (this node transmitted, it did not receive), and for FORWARD/SUPPRESSED (the RSSI that drove the original backoff decision is already reflected in backoffMs/overheardCount below, not re-reported here)
  uint8_t overheardCount;        // meaningful for OVERHEARD/FORWARD/SUPPRESSED/DELIVERED
  uint32_t backoffMs;              // meaningful for FORWARD only (the real RSSI-banded+jittered backoff this node actually waited before relaying)
  const uint8_t* payload;           // valid only for PRIORITY_DELIVERED, only for the duration of the callback
  uint8_t payloadLen;                 // valid only for PRIORITY_DELIVERED
};

typedef void (*SuppressionEventCallback)(const SuppressionEvent& event);

void init();

// Feeds every received MeshPacket whose type == MSG_PRIORITY_BROADCAST.
// Ignores any other type (that traffic is routing's/reliability's own
// concern — see main.cpp's onTransportRx, which calls this alongside the
// other three onPacketReceived() hooks for every received frame).
void onPacketReceived(const MeshPacket& pkt, int8_t rssi);

// Call from the main loop on every iteration; evaluates any cache entries
// whose scheduled backoff deadline has passed (transmit-or-suppress),
// issues the real broadcast for TRANSMIT decisions, and sweeps expired
// cache entries. Never blocks.
void tick();

// Application entry point for originating a NEW priority broadcast (Part
// "opportunistic priority-message broadcast"). Always addresses NODE_S,
// matching apptraffic's existing fixed A->S flow — the same real
// constraint reliability::send()'s previous priority path already
// operated under. Returns false if the payload is too large or the cache
// is full (mirrors reliability::send()'s own honest-failure convention —
// never fabricates an in-flight broadcast that never left the radio).
bool broadcastPriority(const uint8_t* payload, uint8_t len);

void setEventCallback(SuppressionEventCallback cb);

}  // namespace suppression
