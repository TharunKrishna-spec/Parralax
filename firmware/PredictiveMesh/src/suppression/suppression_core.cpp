#include "suppression_core.h"
#include <string.h>

namespace suppression_core {

namespace {

// Finds the valid, non-expired entry matching `id`, or -1 if none exists.
// A "logically expired but still marked valid" entry (tick()'s
// expireCache() hasn't run since it aged out) is treated as absent here so
// a late reception after real expiry starts a fresh evaluation rather than
// reviving a stale one (Part 10: "delayed duplicates cannot restart a
// broadcast storm" — a fresh single-shot evaluation per node still caps
// it, it just isn't the *same* evaluation).
int findEntry(State& state, const PacketId& id, uint32_t now) {
  for (uint8_t i = 0; i < SUPPRESSION_CACHE_SIZE; i++) {
    CacheEntry& e = state.cache[i];
    if (!e.valid) continue;
    if (now - e.touchedAtMs >= SUPPRESSION_CACHE_TTL_MS) continue;  // logically expired
    if (packetIdEquals(e.id, id)) return i;
  }
  return -1;
}

// Finds a slot to reuse for a brand-new identity: an empty slot, or one
// holding a logically-expired entry. Returns INVALID_SLOT if none exists.
uint8_t findFreeSlot(State& state, uint32_t now) {
  for (uint8_t i = 0; i < SUPPRESSION_CACHE_SIZE; i++) {
    CacheEntry& e = state.cache[i];
    if (!e.valid) return i;
    if (now - e.touchedAtMs >= SUPPRESSION_CACHE_TTL_MS) return i;
  }
  return INVALID_SLOT;
}

}  // namespace

void init(State& state) {
  memset(&state, 0, sizeof(state));
}

uint16_t nextSequence(State& state) {
  return state.nextSeqCounter++;
}

uint32_t computeBackoffMs(int8_t rssi, uint32_t jitterMs) {
  const float weak = static_cast<float>(SUPPRESSION_RSSI_WEAK_DBM);
  const float strong = static_cast<float>(SUPPRESSION_RSSI_STRONG_DBM);
  const float r = static_cast<float>(rssi);

  float t;  // 0.0 at/below weak (far) -> shortest backoff, 1.0 at/above strong (near) -> longest
  if (r <= weak) {
    t = 0.0f;
  } else if (r >= strong) {
    t = 1.0f;
  } else {
    t = (r - weak) / (strong - weak);
  }

  const uint32_t span = SUPPRESSION_MAX_BACKOFF_MS - SUPPRESSION_MIN_BACKOFF_MS;
  const uint32_t base = SUPPRESSION_MIN_BACKOFF_MS + static_cast<uint32_t>(t * static_cast<float>(span));
  return base + jitterMs;
}

ReceiveResult onReceive(State& state, PacketId id, NodeId prevHop, bool isLocalDestination, int8_t rssi,
                         uint32_t backoffDeadlineMs, uint32_t now) {
  const bool isOriginalHop = (prevHop == id.source);

  int existing = findEntry(state, id, now);
  if (existing >= 0) {
    CacheEntry& e = state.cache[existing];
    e.touchedAtMs = now;

    if (isOriginalHop) {
      return ReceiveResult{ ReceiveOutcome::DUPLICATE_OF_ORIGINAL, static_cast<uint8_t>(existing), e.overheardCount };
    }

    // A genuine relay rebroadcast — always real evidence, whether or not
    // this entry has already been decided (Part 9: never conflate "I
    // received it" with "someone else relayed it").
    e.overheardCount++;
    ReceiveOutcome outcome = e.decided ? ReceiveOutcome::ALREADY_DECIDED : ReceiveOutcome::OVERHEARD;
    return ReceiveResult{ outcome, static_cast<uint8_t>(existing), e.overheardCount };
  }

  // Brand-new identity.
  uint8_t slot = findFreeSlot(state, now);
  if (slot == INVALID_SLOT) {
    return ReceiveResult{ ReceiveOutcome::CACHE_FULL, INVALID_SLOT, 0 };
  }

  CacheEntry fresh{};
  fresh.valid = true;
  fresh.id = id;
  fresh.rssiAtFirstHear = rssi;
  fresh.overheardCount = isOriginalHop ? 0 : 1;  // first-heard-via-relay is still real relay evidence
  fresh.isLocalDestination = isLocalDestination;
  fresh.touchedAtMs = now;

  if (isLocalDestination) {
    // The sink never schedules a rebroadcast of its own destined traffic
    // (Part 12) — settled immediately.
    fresh.decided = true;
    fresh.decision = Decision::NONE;
    fresh.deadlineMs = now;
  } else {
    fresh.decided = false;
    fresh.decision = Decision::NONE;
    fresh.deadlineMs = backoffDeadlineMs;
  }

  state.cache[slot] = fresh;
  return ReceiveResult{ ReceiveOutcome::NEW_ENTRY, slot, fresh.overheardCount };
}

uint8_t tickDecisions(State& state, uint32_t now, ReadyDecision* out, uint8_t maxOut) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < SUPPRESSION_CACHE_SIZE && count < maxOut; i++) {
    CacheEntry& e = state.cache[i];
    if (!e.valid || e.decided || e.isLocalDestination) continue;
    if (now < e.deadlineMs) continue;

    e.decision = (e.overheardCount < SUPPRESSION_THRESHOLD) ? Decision::TRANSMIT : Decision::SUPPRESS;
    e.decided = true;
    e.touchedAtMs = now;

    out[count] = ReadyDecision{ i, e.id, e.decision, e.overheardCount };
    count++;
  }
  return count;
}

uint8_t recordOwnOrigination(State& state, PacketId id, uint32_t now) {
  uint8_t slot = findFreeSlot(state, now);
  if (slot == INVALID_SLOT) return INVALID_SLOT;

  CacheEntry fresh{};
  fresh.valid = true;
  fresh.id = id;
  fresh.rssiAtFirstHear = 0;  // not applicable — this node transmitted, it did not hear this over radio
  fresh.overheardCount = 0;
  fresh.isLocalDestination = false;
  fresh.decided = true;
  fresh.decision = Decision::TRANSMIT;
  fresh.deadlineMs = now;
  fresh.touchedAtMs = now;

  state.cache[slot] = fresh;
  return slot;
}

uint8_t expireCache(State& state, uint32_t now) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < SUPPRESSION_CACHE_SIZE; i++) {
    CacheEntry& e = state.cache[i];
    if (!e.valid) continue;
    if (now - e.touchedAtMs >= SUPPRESSION_CACHE_TTL_MS) {
      e.valid = false;
      count++;
    }
  }
  return count;
}

}  // namespace suppression_core
