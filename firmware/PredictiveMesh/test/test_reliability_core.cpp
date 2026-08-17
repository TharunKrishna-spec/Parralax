// Minimal host-side unit test harness for reliability_core's packet-
// identity/duplicate-filter/retry/timeout/statistics bookkeeping (Phase 4).
// Like test_routing_core.cpp/test_predictor_core.cpp/test_anomaly_core.cpp,
// this is NOT a network/radio simulator: it never simulates ESP-NOW, a real
// unicast send, or a real ACK packet arriving over the air. It only feeds
// reliability_core's pure functions hand-constructed identities/timestamps
// and checks outputs against this phase's own documented semantics (see
// docs/decisions.md for the exact attempt-vs-packet counting rules Part 9
// requires). reliability_core.h/.cpp have zero Arduino/ESP-NOW dependency
// specifically so this can compile and run with a plain host compiler. See
// docs/testing.md.
//
// Build & run (host g++ - NOT the ESP32 toolchain; run from this file's
// directory):
//   g++ -std=c++17 -Wall -Wextra -I ../src ../src/reliability/reliability_core.cpp test_reliability_core.cpp -o test_reliability_core
//   ./test_reliability_core

#include "../src/reliability/reliability_core.h"
#include <cstdio>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* description) {
  g_checks++;
  if (!condition) {
    g_failures++;
    std::printf("FAIL: %s\n", description);
  } else {
    std::printf("ok:   %s\n", description);
  }
}

using namespace reliability_core;

const NodeId SELF = NODE_A;
const NodeId NEIGHBOR = NODE_B;
const NodeId OTHER_SOURCE = NODE_C;

// ---- 1. A fresh (source, sequence) is never a duplicate on first sight ----
void test_new_identity_not_duplicate() {
  ReliabilityState s;
  init(s, SELF);

  bool dup = isDuplicateAndRecord(s, NODE_A, 1, 1000);
  check(!dup, "a never-before-seen (source, sequence) is not reported as a duplicate");
}

// ---- 2. The same (source, sequence) seen again within TTL is a duplicate ----
void test_repeat_identity_is_duplicate() {
  ReliabilityState s;
  init(s, SELF);

  check(!isDuplicateAndRecord(s, NODE_A, 5, 1000), "first sighting of (A,5) is new");
  check(isDuplicateAndRecord(s, NODE_A, 5, 1100), "second sighting of (A,5), 100ms later, is a duplicate");
  check(isDuplicateAndRecord(s, NODE_A, 5, 1200), "a third sighting is still a duplicate");
}

// ---- 3. Identity is the (source, sequence) PAIR, not sequence alone ----
void test_identity_is_source_and_sequence_pair() {
  ReliabilityState s;
  init(s, SELF);

  check(!isDuplicateAndRecord(s, NODE_A, 7, 1000), "(A,7) is new");
  check(!isDuplicateAndRecord(s, OTHER_SOURCE, 7, 1000),
        "(C,7) — same sequence number, different source — is NOT a duplicate of (A,7)");
  check(!isDuplicateAndRecord(s, NODE_A, 8, 1000),
        "(A,8) — same source, different sequence — is NOT a duplicate of (A,7)");
}

// ---- 4. Duplicate cache expiry: an old identity is treated as new again after TTL ----
void test_duplicate_cache_expiry() {
  ReliabilityState s;
  init(s, SELF);

  check(!isDuplicateAndRecord(s, NODE_A, 1, 1000), "(A,1) recorded at t=1000");
  uint32_t past_ttl = 1000 + RELIABILITY_DUP_CACHE_TTL_MS + 1;
  check(!isDuplicateAndRecord(s, NODE_A, 1, past_ttl),
        "(A,1) seen again after RELIABILITY_DUP_CACHE_TTL_MS has elapsed is treated as new, not a duplicate");
}

// ---- 5. Duplicate cache replacement: cache full forces eviction of the oldest entry ----
void test_duplicate_cache_eviction_when_full() {
  ReliabilityState s;
  init(s, SELF);

  // Fill every slot with a distinct, still-fresh identity (sequence 0..SIZE-1 from NODE_A).
  for (uint16_t i = 0; i < RELIABILITY_DUP_CACHE_SIZE; i++) {
    check(!isDuplicateAndRecord(s, NODE_A, i, 1000), "filling the duplicate cache with distinct fresh identities");
  }
  // One more distinct identity — cache is full and nothing has expired, so the
  // ring-buffer replacement policy must evict the oldest (sequence 0) slot.
  check(!isDuplicateAndRecord(s, NODE_A, RELIABILITY_DUP_CACHE_SIZE, 1001), "one more identity forces an eviction");

  // The evicted identity (A, 0) is no longer tracked — seeing it again reports "new", not a duplicate.
  check(!isDuplicateAndRecord(s, NODE_A, 0, 1002),
        "the identity evicted by the ring-buffer replacement policy is no longer recognized as a duplicate");
}

// ---- 6. beginTx reserves independent slots for concurrent hop-transmissions ----
void test_begin_tx_reserves_distinct_slots() {
  ReliabilityState s;
  init(s, SELF);

  uint8_t slot1 = beginTx(s, NODE_A, 1, NEIGHBOR, 1000);
  uint8_t slot2 = beginTx(s, NODE_C, 1, NEIGHBOR, 1000);

  check(slot1 != INVALID_SLOT && slot2 != INVALID_SLOT, "two concurrent hop-transmissions both get real slots");
  check(slot1 != slot2, "concurrent hop-transmissions occupy distinct pending slots");
  check(s.stats.packetsSent == 2, "packetsSent counts one per successful beginTx call");
}

// ---- 7. beginTx returns INVALID_SLOT once the pending pool is exhausted ----
void test_begin_tx_pool_exhaustion() {
  ReliabilityState s;
  init(s, SELF);

  for (uint16_t i = 0; i < RELIABILITY_MAX_PENDING; i++) {
    uint8_t slot = beginTx(s, NODE_A, i, NEIGHBOR, 1000);
    check(slot != INVALID_SLOT, "filling the pending pool up to RELIABILITY_MAX_PENDING");
  }
  uint8_t overflow = beginTx(s, NODE_A, RELIABILITY_MAX_PENDING, NEIGHBOR, 1000);
  check(overflow == INVALID_SLOT, "one more concurrent hop-transmission beyond the pool size is refused, not fabricated");
  check(s.stats.packetsSent == RELIABILITY_MAX_PENDING,
        "packetsSent does not count the refused hop-transmission — it never began real tracking");
}

// ---- 8. A real matching ACK clears the pending slot and records delivery ----
void test_matching_ack_resolves_pending_tx() {
  ReliabilityState s;
  init(s, SELF);

  beginTx(s, NODE_A, 3, NEIGHBOR, 1000);
  AckResult r = onAckReceived(s, NODE_A, 3, 1042);

  check(r.matched, "a real ACK for a tracked identity matches");
  check(r.nextHop == NEIGHBOR, "the matched ACK reports the correct next-hop neighbor");
  check(r.latencyMs == 42, "latency is measured from the most recent send to the matching ACK (1042 - 1000 = 42)");
  check(r.attemptCount == 1, "a match with no intervening retry reports attemptCount 1 (first-try delivery)");
  check(s.stats.packetsDelivered == 1, "packetsDelivered increments on a real match");
  check(s.stats.acknowledgements == 1, "acknowledgements increments on a real match");
  check(s.stats.lastLatencyMs == 42, "lastLatencyMs reflects the just-resolved hop-transmission");

  // The slot is now free for reuse.
  uint8_t reused = beginTx(s, NODE_C, 1, NEIGHBOR, 2000);
  check(reused != INVALID_SLOT, "a resolved pending slot becomes available for a new hop-transmission");
}

// ---- 9. An ACK for an unknown/stale identity is never fabricated as a match ----
void test_unmatched_ack_is_not_fabricated() {
  ReliabilityState s;
  init(s, SELF);

  beginTx(s, NODE_A, 1, NEIGHBOR, 1000);
  AckResult r = onAckReceived(s, NODE_A, 999, 1010);  // wrong sequence — nothing pending for this identity

  check(!r.matched, "an ACK for an identity with no pending entry does not match");
  check(s.stats.packetsDelivered == 0, "an unmatched ACK never increments packetsDelivered");
  check(s.stats.acknowledgements == 0, "an unmatched ACK never increments acknowledgements");
}

// ---- 10. tickTimeouts is silent while a pending transmission is still within its timeout window ----
void test_tick_timeouts_silent_before_deadline() {
  ReliabilityState s;
  init(s, SELF);

  beginTx(s, NODE_A, 1, NEIGHBOR, 1000);

  TimeoutEvent events[4];
  uint8_t n = tickTimeouts(s, 1000 + RELIABILITY_ACK_TIMEOUT_MS - 1, events, 4);
  check(n == 0, "no timeout event fires before RELIABILITY_ACK_TIMEOUT_MS has actually elapsed");
}

// ---- 11. tickTimeouts fires RETRY once the deadline passes, with attempts remaining ----
void test_tick_timeouts_fires_retry() {
  ReliabilityState s;
  init(s, SELF);

  beginTx(s, NODE_A, 1, NEIGHBOR, 1000);

  TimeoutEvent events[4];
  uint32_t deadline = 1000 + RELIABILITY_ACK_TIMEOUT_MS;
  uint8_t n = tickTimeouts(s, deadline, events, 4);

  check(n == 1, "exactly one timeout event fires for the one expired pending slot");
  check(events[0].action == TimeoutAction::RETRY, "a first timeout with retries remaining is a RETRY, not a FAILED");
  check(events[0].id.source == NODE_A && events[0].id.sequence == 1, "the RETRY event carries the correct packet identity");
  check(events[0].nextHop == NEIGHBOR, "the RETRY event carries the correct next-hop neighbor");
  check(events[0].attemptCount == 2, "the retried attempt becomes attempt #2");
  check(s.stats.retries == 1, "the retries statistic counts the individual resend, not the whole packet");
  check(s.stats.packetsFailed == 0, "a RETRY is not a failure — packetsFailed stays at 0");
}

// ---- 12. Exhausting all retries produces exactly one FAILED, then stops ----
void test_tick_timeouts_exhausts_retries_then_fails() {
  ReliabilityState s;
  init(s, SELF);

  beginTx(s, NODE_A, 1, NEIGHBOR, 1000);

  uint32_t now = 1000;
  TimeoutEvent events[4];
  for (uint8_t i = 0; i < RELIABILITY_MAX_RETRIES; i++) {
    now += RELIABILITY_ACK_TIMEOUT_MS;
    uint8_t n = tickTimeouts(s, now, events, 4);
    check(n == 1 && events[0].action == TimeoutAction::RETRY,
          "each of the first RELIABILITY_MAX_RETRIES timeouts is a RETRY");
  }

  now += RELIABILITY_ACK_TIMEOUT_MS;
  uint8_t n = tickTimeouts(s, now, events, 4);
  check(n == 1 && events[0].action == TimeoutAction::FAILED,
        "the timeout after RELIABILITY_MAX_RETRIES resends is FAILED, not another RETRY");
  check(s.stats.retries == RELIABILITY_MAX_RETRIES, "exactly RELIABILITY_MAX_RETRIES resends were counted");
  check(s.stats.packetsFailed == 1, "exactly one packetsFailed is recorded for the whole series, not one per attempt");
  check(s.stats.packetsDelivered == 0, "a failed hop-transmission never counts as delivered");

  // No further timeout events fire — the slot was cleared.
  now += RELIABILITY_ACK_TIMEOUT_MS;
  uint8_t nAfter = tickTimeouts(s, now, events, 4);
  check(nAfter == 0, "a FAILED, cleared slot produces no further timeout events");

  uint8_t reused = beginTx(s, NODE_C, 1, NEIGHBOR, now);
  check(reused != INVALID_SLOT, "a FAILED pending slot becomes available for a new hop-transmission");
}

// ---- 13. Part 9's exact worked example: 1 original + 2 retries + final success ----
void test_part9_one_packet_two_retries_then_success() {
  ReliabilityState s;
  init(s, SELF);

  beginTx(s, NODE_A, 1, NEIGHBOR, 1000);  // attempt #1 (the "1 original packet")

  uint32_t now = 1000;
  TimeoutEvent events[4];

  now += RELIABILITY_ACK_TIMEOUT_MS;
  tickTimeouts(s, now, events, 4);  // retry #1 -> attempt #2

  now += RELIABILITY_ACK_TIMEOUT_MS;
  tickTimeouts(s, now, events, 4);  // retry #2 -> attempt #3

  now += 5;  // the third attempt's ACK arrives quickly, well before a third timeout
  AckResult r = onAckReceived(s, NODE_A, 1, now);

  check(r.matched, "the final attempt is acknowledged before it would have timed out");
  check(r.attemptCount == 3, "attemptCount reports the real, final attempt number (1 original + 2 retries = 3) that the ACK actually resolved — lets telemetry honestly distinguish a recovered delivery from a first-try one");
  check(s.stats.packetsSent == 1, "1 original packet — packetsSent counts the whole series once, not per attempt");
  check(s.stats.retries == 2, "2 retries — matches Part 9's worked example exactly");
  check(s.stats.acknowledgements == 1, "acknowledgements counts the one real ACK that resolved the series");
  check(s.stats.packetsDelivered == 1, "packetsDelivered counts the one successful packet, not one per attempt");
  check(s.stats.packetsFailed == 0, "a packet that ultimately succeeded is never also counted as failed");
}

// ---- 14. recordImmediateFailure counts a hop-transmission that never entered tracking ----
void test_record_immediate_failure() {
  ReliabilityState s;
  init(s, SELF);

  recordImmediateFailure(s);

  check(s.stats.packetsFailed == 1, "an immediate failure (pool full / radio rejected the send outright) is counted");
  check(s.stats.packetsSent == 0, "an immediate failure never increments packetsSent — it never began real tracking");
}

// ---- 15. Multiple concurrent pending entries resolve independently ----
void test_concurrent_pending_entries_independent() {
  ReliabilityState s;
  init(s, SELF);

  beginTx(s, NODE_A, 1, NEIGHBOR, 1000);       // will be ACKed immediately
  beginTx(s, NODE_C, 1, NEIGHBOR, 1000);       // will time out and fail after exhausting retries
  beginTx(s, NODE_A, 2, NEIGHBOR, 1000);       // will retry once, then be rescued by a real ACK

  AckResult r = onAckReceived(s, NODE_A, 1, 1010);
  check(r.matched, "the first concurrent entry is acknowledged independently of the others");
  check(s.stats.packetsDelivered == 1, "the first entry's early ack is recorded immediately");

  TimeoutEvent events[4];
  uint32_t now = 1000 + RELIABILITY_ACK_TIMEOUT_MS;  // shared first deadline for (C,1) and (A,2)
  uint8_t n = tickTimeouts(s, now, events, 4);
  check(n == 2, "both still-pending concurrent entries time out together on their shared deadline");

  // Rescue (A,2) with a real ACK before its next timeout — it must not fail
  // just because (C,1), a completely different hop-transmission, keeps failing.
  AckResult r2 = onAckReceived(s, NODE_A, 2, now + 5);
  check(r2.matched, "a concurrent entry can still be acknowledged after surviving one retry cycle");
  check(s.stats.packetsDelivered == 2, "the rescued entry brings delivered count to 2, independent of (C,1)'s fate");

  // Let (C,1) exhaust its remaining retries and fail, untouched by (A,1)/(A,2)'s resolutions.
  for (uint8_t i = 0; i < RELIABILITY_MAX_RETRIES - 1; i++) {
    now += RELIABILITY_ACK_TIMEOUT_MS;
    tickTimeouts(s, now, events, 4);
  }
  now += RELIABILITY_ACK_TIMEOUT_MS;
  uint8_t nFinal = tickTimeouts(s, now, events, 4);
  check(nFinal == 1 && events[0].action == TimeoutAction::FAILED,
        "(C,1) alone reaches FAILED — its retries were never affected by the other two entries' outcomes");
  check(s.stats.packetsFailed == 1, "exactly one of the three concurrent entries ultimately failed");
  check(s.stats.packetsDelivered == 2, "the other two concurrent entries remain counted as delivered");
}

// ---- 16. nextSequence is monotonic per node, starting at 0 ----
void test_next_sequence_monotonic() {
  ReliabilityState s;
  init(s, SELF);

  check(nextSequence(s) == 0, "the first sequence number issued is 0");
  check(nextSequence(s) == 1, "sequence numbers increase monotonically");
  check(nextSequence(s) == 2, "sequence numbers increase monotonically (third call)");
}

// ---- 17b. cancelTx immediately fails a reserved slot without waiting for a timeout ----
void test_cancel_tx_immediate_failure() {
  ReliabilityState s;
  init(s, SELF);

  uint8_t slot = beginTx(s, NODE_A, 1, NEIGHBOR, 1000);
  check(slot != INVALID_SLOT, "sanity: the slot was reserved before the (synchronously failing) real send was attempted");

  cancelTx(s, slot);
  check(s.stats.packetsFailed == 1, "cancelTx counts exactly one packetsFailed, same as a timeout-exhausted failure");
  check(s.stats.packetsDelivered == 0, "a cancelled hop-transmission is never counted as delivered");

  TimeoutEvent events[4];
  uint8_t n = tickTimeouts(s, 1000 + RELIABILITY_ACK_TIMEOUT_MS, events, 4);
  check(n == 0, "a cancelled slot is already inactive — it never produces a later timeout event too");

  uint8_t reused = beginTx(s, NODE_C, 1, NEIGHBOR, 2000);
  check(reused != INVALID_SLOT, "a cancelled slot becomes available for a new hop-transmission immediately");
}

// ---- 17. tickTimeouts respects the caller's maxOut cap (Part 5: non-blocking, bounded work per tick) ----
void test_tick_timeouts_respects_max_out() {
  ReliabilityState s;
  init(s, SELF);

  for (uint16_t i = 0; i < RELIABILITY_MAX_PENDING; i++) {
    beginTx(s, NODE_A, i, NEIGHBOR, 1000);
  }

  TimeoutEvent events[1];
  uint8_t n = tickTimeouts(s, 1000 + RELIABILITY_ACK_TIMEOUT_MS, events, 1);
  check(n == 1, "tickTimeouts never writes more than maxOut events in a single call, even with more pending");
}

}  // namespace

int main() {
  test_new_identity_not_duplicate();
  test_repeat_identity_is_duplicate();
  test_identity_is_source_and_sequence_pair();
  test_duplicate_cache_expiry();
  test_duplicate_cache_eviction_when_full();
  test_begin_tx_reserves_distinct_slots();
  test_begin_tx_pool_exhaustion();
  test_matching_ack_resolves_pending_tx();
  test_unmatched_ack_is_not_fabricated();
  test_tick_timeouts_silent_before_deadline();
  test_tick_timeouts_fires_retry();
  test_tick_timeouts_exhausts_retries_then_fails();
  test_part9_one_packet_two_retries_then_success();
  test_record_immediate_failure();
  test_concurrent_pending_entries_independent();
  test_next_sequence_monotonic();
  test_cancel_tx_immediate_failure();
  test_tick_timeouts_respects_max_out();

  std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
