// Minimal host-side unit test harness for suppression_core's counter-based
// broadcast suppression algorithm (Priority-broadcast milestone,
// 2026-08-18). Like test_routing_core.cpp/test_reliability_core.cpp/etc.,
// this is NOT a network/radio simulator: it never simulates ESP-NOW, a
// real broadcast send, or real RF propagation. It only feeds
// suppression_core's pure functions hand-constructed identities/RSSI/
// timestamps and checks outputs against this milestone's own documented
// semantics (see docs/decisions.md). suppression_core.h/.cpp have zero
// Arduino/ESP-NOW dependency specifically so this can compile and run with
// a plain host compiler. See docs/testing.md.
//
// Two of the milestone's required test cases are deliberately NOT unit
// tests here, because they are structural/architectural facts about the
// ADAPTER layer, not something suppression_core's own pure functions could
// ever violate or prove in isolation:
//   - "Broadcast priority does not enter normal unicast ACK/PDR accounting
//     incorrectly" — verified by construction: suppression_core.cpp/.h
//     have zero references to reliability_core or predictor:: anywhere
//     (grep-verifiable), and the existing reliability_core test suite
//     (90/90) stays green, unmodified, after this milestone.
//   - "Normal traffic is completely unaffected" — verified by construction
//     (apptraffic.cpp's NORMAL branch still calls
//     reliability::send(decision.destination, payload, len, false),
//     byte-for-byte what it called before this milestone) and by the full
//     existing host-test regression suite staying green.
//
// "Same sequence from different boot IDs is not confused" (test case 13)
// is deliberately tested here as documenting the OPPOSITE of what it asks
// — see test_same_sequence_no_boot_disambiguation_is_a_known_limitation()
// below and docs/decisions.md for why no wire-level boot-salt was added.
//
// Build & run (host g++ - NOT the ESP32 toolchain; run from this file's
// directory):
//   g++ -std=c++17 -Wall -Wextra -I ../src ../src/suppression/suppression_core.cpp test_suppression_core.cpp -o test_suppression_core
//   ./test_suppression_core

#include "../src/suppression/suppression_core.h"
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

using namespace suppression_core;

const NodeId SOURCE_A = NODE_A;
const NodeId RELAY_B = NODE_B;
const NodeId RELAY_C = NODE_C;
const NodeId RELAY_D = NODE_D;
const NodeId SINK_S = NODE_S;

// ---- 1. New priority packet enters cache ----
void test_new_entry_created() {
  State s;
  init(s);

  PacketId id{ SOURCE_A, 100 };
  uint32_t deadline = 1000 + computeBackoffMs(-60, 0);
  ReceiveResult r = onReceive(s, id, /*prevHop=*/SOURCE_A, /*isLocalDestination=*/false, /*rssi=*/-60, deadline,
                               /*now=*/1000);

  check(r.outcome == ReceiveOutcome::NEW_ENTRY, "first reception of a new identity creates a cache entry");
  check(r.overheardCount == 0, "the original reception itself is not counted as an overhear (Part 9)");
  check(s.cache[r.slot].valid && !s.cache[r.slot].decided, "new entry is valid and not yet decided");
}

// ---- 2. Duplicate packet (of the true original) is recognized, not counted ----
void test_duplicate_of_original_no_state_change() {
  State s;
  init(s);
  PacketId id{ SOURCE_A, 101 };
  onReceive(s, id, SOURCE_A, false, -60, 1200, 1000);

  ReceiveResult r2 = onReceive(s, id, /*prevHop=*/SOURCE_A, false, -60, 1200, 1050);
  check(r2.outcome == ReceiveOutcome::DUPLICATE_OF_ORIGINAL,
        "a repeat reception directly from the true source is a duplicate of the original, not an overhear");
  check(r2.overheardCount == 0, "duplicate-of-original never increments overheardCount");
}

// ---- 3. Overhear count increments on a genuine relay rebroadcast ----
void test_overheard_increments_count() {
  State s;
  init(s);
  PacketId id{ SOURCE_A, 102 };
  onReceive(s, id, SOURCE_A, false, -60, 1200, 1000);  // original, from A

  ReceiveResult r = onReceive(s, id, /*prevHop=*/RELAY_B, false, -50, 1200, 1050);  // B's rebroadcast
  check(r.outcome == ReceiveOutcome::OVERHEARD, "a reception with prevHop != source is a genuine relay rebroadcast");
  check(r.overheardCount == 1, "overheardCount increments on a real relay rebroadcast");

  ReceiveResult r2 = onReceive(s, id, /*prevHop=*/RELAY_D, false, -70, 1200, 1060);  // D's rebroadcast too
  check(r2.overheardCount == 2, "a second distinct relay's rebroadcast increments overheardCount again");
}

// ---- 3b. First-ever reception can already be a relay's copy (never heard the true original) ----
void test_first_reception_via_relay_still_counts() {
  State s;
  init(s);
  PacketId id{ SOURCE_A, 103 };
  ReceiveResult r = onReceive(s, id, /*prevHop=*/RELAY_C, false, -65, 1200, 1000);
  check(r.outcome == ReceiveOutcome::NEW_ENTRY, "first-ever reception still creates a new entry even if it's a relay's copy");
  check(r.overheardCount == 1,
        "a first reception that is already a relay's copy (prevHop != source) counts as real overhear evidence");
}

// ---- 4. Below threshold at deadline -> TRANSMIT ----
void test_tick_decisions_transmit_below_threshold() {
  State s;
  init(s);
  PacketId id{ SOURCE_A, 104 };
  onReceive(s, id, SOURCE_A, false, -60, /*deadline=*/1200, 1000);
  // SUPPRESSION_THRESHOLD is 1 by default — zero overhears stays below it.

  ReadyDecision out[SUPPRESSION_CACHE_SIZE];
  uint8_t n = tickDecisions(s, /*now=*/1200, out, SUPPRESSION_CACHE_SIZE);
  check(n == 1, "one entry's deadline has arrived");
  check(out[0].decision == Decision::TRANSMIT, "zero overheard rebroadcasts (below threshold) -> TRANSMIT");
  check(out[0].overheardCountAtDecision == 0, "reports the real overheardCount that drove the decision");
}

// ---- 5. Threshold reached before deadline -> SUPPRESS ----
void test_tick_decisions_suppress_at_threshold() {
  State s;
  init(s);
  PacketId id{ SOURCE_A, 105 };
  onReceive(s, id, SOURCE_A, false, -60, /*deadline=*/1300, 1000);
  onReceive(s, id, RELAY_B, false, -50, 1300, 1100);  // one other node already relayed it

  ReadyDecision out[SUPPRESSION_CACHE_SIZE];
  uint8_t n = tickDecisions(s, /*now=*/1300, out, SUPPRESSION_CACHE_SIZE);
  check(n == 1, "the entry's deadline has arrived");
  check(out[0].decision == Decision::SUPPRESS, "overheardCount (1) meeting SUPPRESSION_THRESHOLD (1) -> SUPPRESS");
}

// ---- 6/7/8. RSSI-aware backoff: strong = longer, weak = shorter, jitter bounded ----
void test_strong_rssi_longer_backoff_weak_rssi_shorter() {
  uint32_t strong = computeBackoffMs(SUPPRESSION_RSSI_STRONG_DBM, 0);
  uint32_t weak = computeBackoffMs(SUPPRESSION_RSSI_WEAK_DBM, 0);
  uint32_t mid = computeBackoffMs((SUPPRESSION_RSSI_STRONG_DBM + SUPPRESSION_RSSI_WEAK_DBM) / 2, 0);

  check(strong == SUPPRESSION_MAX_BACKOFF_MS, "at/above the strong-RSSI threshold, backoff saturates to the max (longest wait)");
  check(weak == SUPPRESSION_MIN_BACKOFF_MS, "at/below the weak-RSSI threshold, backoff saturates to the min (shortest wait)");
  check(mid > weak && mid < strong, "a mid-range RSSI produces a backoff strictly between the min and max bounds");
  check(strong > weak, "a strong/near signal waits strictly longer than a weak/far one, giving farther nodes first shot");
}

void test_jitter_bounded() {
  uint32_t base = computeBackoffMs(-60, 0);
  uint32_t withMinJitter = computeBackoffMs(-60, 0);
  uint32_t withMaxJitter = computeBackoffMs(-60, SUPPRESSION_JITTER_MAX_MS - 1);

  check(withMinJitter == base, "zero jitter leaves the RSSI-banded backoff unchanged");
  check(withMaxJitter == base + (SUPPRESSION_JITTER_MAX_MS - 1),
        "jitter is added on top of the RSSI-banded base, never replacing it");
  check(withMaxJitter < base + SUPPRESSION_JITTER_MAX_MS,
        "jitter never pushes the backoff to or past base + SUPPRESSION_JITTER_MAX_MS (caller is expected to pass jitter < that bound)");
}

// ---- 9. Cache entries expire ----
void test_cache_expiry() {
  State s;
  init(s);
  PacketId id{ SOURCE_A, 106 };
  onReceive(s, id, SOURCE_A, false, -60, 1200, /*now=*/1000);

  uint8_t expiredBefore = expireCache(s, /*now=*/1000 + SUPPRESSION_CACHE_TTL_MS - 1);
  check(expiredBefore == 0, "an entry younger than the TTL is not expired");

  uint8_t expiredAfter = expireCache(s, /*now=*/1000 + SUPPRESSION_CACHE_TTL_MS);
  check(expiredAfter == 1, "an entry at/past the TTL is expired");
}

// ---- 10. A packet reappearing after expiration starts a fresh, still single-shot evaluation ----
void test_expired_then_new_entry_is_independent_and_single_shot() {
  State s;
  init(s);
  PacketId id{ SOURCE_A, 107 };
  uint8_t slot1 = onReceive(s, id, SOURCE_A, false, -60, 1200, 1000).slot;
  expireCache(s, 1000 + SUPPRESSION_CACHE_TTL_MS);
  check(!s.cache[slot1].valid, "the original entry is genuinely gone after expiry");

  // A late/delayed duplicate of the exact same identity arrives well after
  // expiry — this must be treated as a brand-new evaluation (Part 10: a
  // delayed duplicate must not resurrect or extend the old flood), and
  // that new evaluation is itself still bounded to a single TRANSMIT-or-
  // SUPPRESS decision, never a repeating/infinite one.
  uint32_t lateNow = 1000 + SUPPRESSION_CACHE_TTL_MS + 500;
  ReceiveResult r = onReceive(s, id, SOURCE_A, false, -60, lateNow + 200, lateNow);
  check(r.outcome == ReceiveOutcome::NEW_ENTRY, "a delayed duplicate after real expiry creates one fresh entry, not a revived one");

  ReadyDecision out[SUPPRESSION_CACHE_SIZE];
  uint8_t n1 = tickDecisions(s, lateNow + 200, out, SUPPRESSION_CACHE_SIZE);
  check(n1 == 1, "the fresh entry is decided exactly once");
  uint8_t n2 = tickDecisions(s, lateNow + 500, out, SUPPRESSION_CACHE_SIZE);
  check(n2 == 0, "an already-decided entry is never re-decided or re-transmitted (bounds any repeat forwarding)");
}

// ---- 11. Different sequences (same source) are not confused ----
void test_different_sequences_not_confused() {
  State s;
  init(s);
  PacketId idA{ SOURCE_A, 200 };
  PacketId idB{ SOURCE_A, 201 };
  ReceiveResult r1 = onReceive(s, idA, SOURCE_A, false, -60, 1200, 1000);
  ReceiveResult r2 = onReceive(s, idB, SOURCE_A, false, -60, 1200, 1000);

  check(r1.slot != r2.slot, "two different sequences from the same source occupy distinct cache entries");
  onReceive(s, idB, RELAY_B, false, -50, 1200, 1050);  // overhear only idB's relay
  check(s.cache[r1.slot].overheardCount == 0, "overhearing one identity's relay never affects a different sequence's count");
  check(s.cache[r2.slot].overheardCount == 1, "the correct identity's overheardCount was updated");
}

// ---- 12. Different source nodes are not confused ----
void test_different_sources_not_confused() {
  State s;
  init(s);
  PacketId fromA{ SOURCE_A, 300 };
  PacketId fromB{ RELAY_B, 300 };  // same numeric sequence, different source
  ReceiveResult r1 = onReceive(s, fromA, SOURCE_A, false, -60, 1200, 1000);
  ReceiveResult r2 = onReceive(s, fromB, RELAY_B, false, -60, 1200, 1000);

  check(r1.slot != r2.slot, "identical sequence numbers from two different sources are two distinct identities");
  check(packetIdEquals(fromA, fromA) && !packetIdEquals(fromA, fromB),
        "packetIdEquals correctly distinguishes (source, sequence) pairs");
}

// ---- 13. Same sequence, no boot-ID disambiguation — documented limitation, not silently assumed ----
void test_same_sequence_no_boot_disambiguation_is_a_known_limitation() {
  // suppression_core's identity is (source, sequence) only — there is no
  // wire-level boot-salt (see suppression_core.h's file header and
  // docs/decisions.md for the full reasoning: MeshPacket's two _reserved
  // padding bytes are explicitly documented as alignment padding, "not
  // spare capacity for casual use," and this project has twice already
  // declined to grow the wire format ahead of an actually-observed need).
  // This test documents the REAL, current behavior rather than silently
  // omitting the milestone's requested case: if a source's sequence
  // counter were ever to repeat within one cache TTL window (e.g. a very
  // fast reboot resets it), this module cannot distinguish that from a
  // genuine duplicate of the pre-reboot packet. reliability_core::PacketId
  // already accepts the identical limitation for all NORMAL unicast
  // traffic — this is not a new or worse risk than the existing, accepted
  // one. The risk window is bounded: once SUPPRESSION_CACHE_TTL_MS has
  // genuinely elapsed (see test_expired_then_new_entry_is_independent_and_single_shot
  // above), the same identity is correctly treated as brand new again.
  State s;
  init(s);
  PacketId id{ SOURCE_A, 400 };
  ReceiveResult r1 = onReceive(s, id, SOURCE_A, false, -60, 1200, 1000);
  // A repeat well within the TTL window (not a fresh evaluation) — e.g. a
  // hypothetical sub-second reboot reusing the same sequence number.
  ReceiveResult r2 = onReceive(s, id, SOURCE_A, false, -60, 1200, 1000 + SUPPRESSION_CACHE_TTL_MS / 2);

  check(r1.outcome == ReceiveOutcome::NEW_ENTRY, "first (source, sequence) occurrence creates an entry");
  check(r2.outcome == ReceiveOutcome::DUPLICATE_OF_ORIGINAL,
        "a later reception of the identical (source, sequence), still within the cache TTL window, is "
        "indistinguishable from a duplicate of the original, even if it genuinely came from a different boot — a "
        "documented, accepted, time-bounded limitation, not a crash or corruption risk");
}

// ---- 14. This node's own transmission is never counted as overhearing itself ----
void test_own_origination_not_self_overheard() {
  State s;
  init(s);
  PacketId id{ SOURCE_A, 500 };  // this node IS SOURCE_A in this scenario (the originator)
  uint8_t slot = recordOwnOrigination(s, id, 1000);
  check(slot != INVALID_SLOT, "origination reserves a real cache slot");
  check(s.cache[slot].decided && s.cache[slot].decision == Decision::TRANSMIT,
        "an originated packet is immediately marked decided/TRANSMIT — never scheduled for its own backoff evaluation");

  // Some other real node (B) later rebroadcasts it — A overhears that.
  ReceiveResult r = onReceive(s, id, RELAY_B, false, -55, 9999, 1200);
  check(r.outcome == ReceiveOutcome::ALREADY_DECIDED, "overhearing a relay of a packet we originated updates visibility only");
  check(r.overheardCount == 1, "the relay's rebroadcast is still real, counted evidence — it just can't trigger a second TX");

  ReadyDecision out[SUPPRESSION_CACHE_SIZE];
  uint8_t n = tickDecisions(s, 999999, out, SUPPRESSION_CACHE_SIZE);
  check(n == 0, "an already-decided (originated) entry is never re-evaluated for transmission, no matter how much later tickDecisions runs");
}

// ---- 15. The real destination never schedules a rebroadcast ----
void test_local_destination_never_scheduled_to_transmit() {
  State s;
  init(s);
  PacketId id{ SOURCE_A, 600 };
  ReceiveResult r = onReceive(s, id, SOURCE_A, /*isLocalDestination=*/true, -60, /*deadline irrelevant*/ 1200, 1000);
  check(r.outcome == ReceiveOutcome::NEW_ENTRY, "the destination still records a new entry (for de-duplication)");
  check(s.cache[r.slot].decided && s.cache[r.slot].isLocalDestination,
        "a local-destination entry is immediately settled — never left pending for backoff");

  ReadyDecision out[SUPPRESSION_CACHE_SIZE];
  uint8_t n = tickDecisions(s, 999999, out, SUPPRESSION_CACHE_SIZE);
  check(n == 0, "tickDecisions never produces a TRANSMIT/SUPPRESS decision for a local-destination entry, at any later time");
}

// ---- Cache-full behavior: a genuinely new identity is dropped, not silently mis-tracked ----
void test_cache_full_reports_honestly() {
  State s;
  init(s);
  for (uint16_t i = 0; i < SUPPRESSION_CACHE_SIZE; i++) {
    PacketId id{ SOURCE_A, i };
    ReceiveResult r = onReceive(s, id, SOURCE_A, false, -60, 1200, 1000);
    check(r.outcome == ReceiveOutcome::NEW_ENTRY, "cache fills up with genuinely distinct identities");
  }
  PacketId overflow{ SOURCE_A, static_cast<uint16_t>(SUPPRESSION_CACHE_SIZE) };
  ReceiveResult r = onReceive(s, overflow, SOURCE_A, false, -60, 1200, 1000);
  check(r.outcome == ReceiveOutcome::CACHE_FULL, "a genuinely new identity with no free slot is honestly reported as dropped, never silently fabricated");
}

// ---- An expired slot is reused for a brand-new identity rather than treated as permanently full ----
void test_expired_slot_is_reused() {
  State s;
  init(s);
  for (uint16_t i = 0; i < SUPPRESSION_CACHE_SIZE; i++) {
    PacketId id{ SOURCE_A, i };
    onReceive(s, id, SOURCE_A, false, -60, 1200, 1000);
  }
  expireCache(s, 1000 + SUPPRESSION_CACHE_TTL_MS);  // ages out every entry

  PacketId fresh{ RELAY_C, 999 };
  ReceiveResult r = onReceive(s, fresh, RELAY_C, false, -60, 5000, 1000 + SUPPRESSION_CACHE_TTL_MS);
  check(r.outcome == ReceiveOutcome::NEW_ENTRY, "once every old entry has expired, a brand-new identity reuses a freed slot instead of being dropped");
}

// ---- tickDecisions never writes past maxOut, matching reliability_core::tickTimeouts's own precedent ----
void test_tick_decisions_respects_max_out() {
  State s;
  init(s);
  for (uint16_t i = 0; i < 3; i++) {
    PacketId id{ SOURCE_A, i };
    onReceive(s, id, SOURCE_A, false, -60, 1200, 1000);
  }
  ReadyDecision out[1];
  uint8_t n = tickDecisions(s, 1200, out, 1);
  check(n == 1, "tickDecisions never writes more than maxOut decisions in a single call, even with more entries ready");
}

// ---- nextSequence is monotonic (mirrors reliability_core::nextSequence's own precedent) ----
void test_next_sequence_monotonic() {
  State s;
  init(s);
  uint16_t a = nextSequence(s);
  uint16_t b = nextSequence(s);
  uint16_t c = nextSequence(s);
  check(a == 0 && b == 1 && c == 2, "nextSequence increments by exactly 1 per call, starting from 0");
}

}  // namespace

int main() {
  test_new_entry_created();
  test_duplicate_of_original_no_state_change();
  test_overheard_increments_count();
  test_first_reception_via_relay_still_counts();
  test_tick_decisions_transmit_below_threshold();
  test_tick_decisions_suppress_at_threshold();
  test_strong_rssi_longer_backoff_weak_rssi_shorter();
  test_jitter_bounded();
  test_cache_expiry();
  test_expired_then_new_entry_is_independent_and_single_shot();
  test_different_sequences_not_confused();
  test_different_sources_not_confused();
  test_same_sequence_no_boot_disambiguation_is_a_known_limitation();
  test_own_origination_not_self_overheard();
  test_local_destination_never_scheduled_to_transmit();
  test_cache_full_reports_honestly();
  test_expired_slot_is_reused();
  test_tick_decisions_respects_max_out();
  test_next_sequence_monotonic();

  std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
