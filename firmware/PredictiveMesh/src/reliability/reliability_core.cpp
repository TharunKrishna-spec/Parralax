#include "reliability_core.h"
#include <string.h>

namespace reliability_core {

namespace {

bool idEquals(const PacketId& a, const PacketId& b) {
  return a.source == b.source && a.sequence == b.sequence;
}

}  // namespace

void init(ReliabilityState& state, NodeId self) {
  memset(&state, 0, sizeof(ReliabilityState));
  state.self = self;
}

uint16_t nextSequence(ReliabilityState& state) {
  return state.nextSeqCounter++;
}

bool isDuplicateAndRecord(ReliabilityState& state, NodeId source, uint16_t sequence, uint32_t now) {
  PacketId id{ source, sequence };
  int freeSlot = -1;

  for (uint8_t i = 0; i < RELIABILITY_DUP_CACHE_SIZE; i++) {
    DupEntry& e = state.dupCache[i];
    if (e.valid && (now - e.seenAtMs) > RELIABILITY_DUP_CACHE_TTL_MS) {
      e.valid = false;  // lazy expiry — also frees this slot for reuse below
    }
    if (e.valid) {
      if (idEquals(e.id, id)) return true;  // real, unexpired match — duplicate
    } else if (freeSlot < 0) {
      freeSlot = i;
    }
  }

  uint8_t insertAt = (freeSlot >= 0) ? static_cast<uint8_t>(freeSlot) : state.dupCacheNext;
  if (freeSlot < 0) {
    state.dupCacheNext = static_cast<uint8_t>((state.dupCacheNext + 1) % RELIABILITY_DUP_CACHE_SIZE);
  }
  state.dupCache[insertAt] = DupEntry{ true, id, now };
  return false;
}

uint8_t beginTx(ReliabilityState& state, NodeId source, uint16_t sequence, NodeId nextHop, uint32_t now) {
  for (uint8_t i = 0; i < RELIABILITY_MAX_PENDING; i++) {
    if (!state.pending[i].active) {
      state.pending[i] = PendingTx{ true, PacketId{ source, sequence }, nextHop, 1, now };
      state.stats.packetsSent++;
      return i;
    }
  }
  return INVALID_SLOT;
}

void cancelTx(ReliabilityState& state, uint8_t slot) {
  if (slot >= RELIABILITY_MAX_PENDING) return;
  PendingTx& p = state.pending[slot];
  if (!p.active) return;
  p.active = false;
  state.stats.packetsFailed++;
}

void recordImmediateFailure(ReliabilityState& state) {
  state.stats.packetsFailed++;
}

AckResult onAckReceived(ReliabilityState& state, NodeId source, uint16_t sequence, uint32_t now) {
  PacketId id{ source, sequence };
  for (uint8_t i = 0; i < RELIABILITY_MAX_PENDING; i++) {
    PendingTx& p = state.pending[i];
    if (p.active && idEquals(p.id, id)) {
      uint32_t latency = now - p.lastSendMs;
      NodeId nextHop = p.nextHop;
      p.active = false;

      state.stats.packetsDelivered++;
      state.stats.acknowledgements++;
      state.stats.lastLatencyMs = latency;

      return AckResult{ true, nextHop, latency };
    }
  }
  // No matching pending entry — a stale, duplicate, or unknown ACK. Never
  // fabricate a match; state is left untouched (Part 3).
  return AckResult{ false, NODE_ID_UNKNOWN, 0 };
}

uint8_t tickTimeouts(ReliabilityState& state, uint32_t now, TimeoutEvent* out, uint8_t maxOut) {
  uint8_t written = 0;

  for (uint8_t i = 0; i < RELIABILITY_MAX_PENDING && written < maxOut; i++) {
    PendingTx& p = state.pending[i];
    if (!p.active) continue;
    if ((now - p.lastSendMs) < RELIABILITY_ACK_TIMEOUT_MS) continue;

    uint8_t retryCount = static_cast<uint8_t>(p.attemptCount - 1);
    if (retryCount < RELIABILITY_MAX_RETRIES) {
      p.attemptCount++;
      p.lastSendMs = now;
      state.stats.retries++;

      out[written++] = TimeoutEvent{ TimeoutAction::RETRY, p.id, p.nextHop, p.attemptCount, i };
    } else {
      out[written++] = TimeoutEvent{ TimeoutAction::FAILED, p.id, p.nextHop, p.attemptCount, i };
      state.stats.packetsFailed++;
      p.active = false;
    }
  }

  return written;
}

}  // namespace reliability_core
