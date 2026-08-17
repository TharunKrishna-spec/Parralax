// Minimal host-side unit test harness for routing_core's distance-vector
// math (Phase 1). This is NOT a network/radio simulator: it never
// simulates ESP-NOW, timing, or a packet actually moving between nodes
// over the air. It only feeds routing_core's pure functions
// hand-computed inputs - the same numbers a real 5-node convergence would
// produce, walked through by hand in the Phase 1 report - and checks the
// outputs. routing_core.h/.cpp have zero Arduino/ESP-NOW dependency
// specifically so this can compile and run with a plain host compiler,
// not arduino-cli. See docs/testing.md.
//
// Build & run (host g++ - NOT the ESP32 toolchain, no arduino-cli
// involved; run from this file's directory):
//   g++ -std=c++17 -I ../src ../src/routing/routing_core.cpp test_routing_core.cpp -o test_routing_core
//   ./test_routing_core

#include "../src/routing/routing_core.h"
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

using namespace routing_core;

// Mirrors config.h's ROUTING_ENTRY_TIMEOUT_MS. Not #included directly -
// config.h pins THIS_NODE_ID to one specific node, which has no meaning
// for a generic multi-node test.
const uint32_t TEST_TIMEOUT_MS = 3000;

// ---- 1. S knows itself at distance 0 ----
void test_self_distance_zero() {
  RoutingState s;
  init(s, NODE_S);

  RouteAdEntry entries[NODE_ID_COUNT];
  uint8_t n = buildAdvertisement(s, entries, NODE_ID_COUNT);

  check(n >= 1, "S advertises at least one entry");
  check(entries[0].destination == NODE_S && entries[0].hop_count == 0,
        "S's first advertised entry is itself at distance 0");
}

// ---- 2. B can learn S at distance 1 ----
void test_b_learns_s_at_one_hop() {
  RoutingState b;
  init(b, NODE_B);

  RouteAdEntry fromS[] = { { NODE_S, 0 } };
  bool changed = applyRouteAdvertisement(b, NODE_S, fromS, 1, /*now=*/1000);
  check(changed, "B's table changes after hearing S's advertisement");

  uint8_t hop = 0;
  NodeId via = selectNextHop(b, NODE_S, /*priority=*/false, &hop);
  check(via == NODE_S && hop == 1, "B's route to S is direct, 1 hop");
}

// ---- 3. A can learn S through B at distance 2 ----
void test_a_learns_s_via_b_at_two_hops() {
  RoutingState a;
  init(a, NODE_A);

  // B's advertisement carries B's own best-known distance to S (1),
  // independently re-derived here so this test doesn't depend on
  // execution order of the other tests.
  RouteAdEntry fromB[] = { { NODE_B, 0 }, { NODE_S, 1 } };
  applyRouteAdvertisement(a, NODE_B, fromB, 2, 1000);

  uint8_t hop = 0;
  NodeId via = selectNextHop(a, NODE_S, /*priority=*/false, &hop);
  check(via == NODE_B && hop == 2, "A's route to S via B is 2 hops (A->B->S)");
}

// ---- 4. A can learn S through C/D as an alternate route ----
void test_a_learns_alternate_via_c() {
  RoutingState a;
  init(a, NODE_A);

  RouteAdEntry fromB[] = { { NODE_B, 0 }, { NODE_S, 1 } };
  applyRouteAdvertisement(a, NODE_B, fromB, 2, 1000);

  // C's distance to S is 2 (C->D->S), derived the same way B's was.
  RouteAdEntry fromC[] = { { NODE_C, 0 }, { NODE_S, 2 } };
  applyRouteAdvertisement(a, NODE_C, fromC, 2, 1000);

  uint8_t hopViaB = 0;
  check(selectNextHop(a, NODE_S, false, &hopViaB) == NODE_B && hopViaB == 2,
        "A still prefers B (2 hops) as the primary route to S once C's route also exists");

  RoutingState aOnlyC;
  init(aOnlyC, NODE_A);
  applyRouteAdvertisement(aOnlyC, NODE_C, fromC, 2, 1000);
  uint8_t hopViaC = 0;
  NodeId viaCOnly = selectNextHop(aOnlyC, NODE_S, false, &hopViaC);
  check(viaCOnly == NODE_C && hopViaC == 3,
        "With no route via B, A falls back to C (3 hops, A->C->D->S)");
}

// ---- 5. Invalid/stale routes are removed or marked invalid ----
void test_stale_routes_invalidated() {
  RoutingState b;
  init(b, NODE_B);

  RouteAdEntry fromS[] = { { NODE_S, 0 } };
  applyRouteAdvertisement(b, NODE_S, fromS, 1, /*now=*/1000);

  uint8_t hop = 0;
  check(selectNextHop(b, NODE_S, false, &hop) == NODE_S, "route to S is valid before timeout");

  uint32_t pastTimeout = 1000 + TEST_TIMEOUT_MS + 1;
  uint8_t invalidated = expireStale(b, pastTimeout, TEST_TIMEOUT_MS);
  check(invalidated > 0, "expireStale reports at least one invalidated entry past the timeout");
  check(selectNextHop(b, NODE_S, false, &hop) == NODE_ID_UNKNOWN,
        "route to S is gone after the timeout elapses with no refresh");
}

// ---- 6. Normal traffic selects the intended baseline route ----
void test_normal_selects_b_not_direct_s() {
  RoutingState a;
  init(a, NODE_A);

  RouteAdEntry fromB[] = { { NODE_B, 0 }, { NODE_S, 1 } };
  applyRouteAdvertisement(a, NODE_B, fromB, 2, 1000);

  // S is also a direct neighbor of A - the weak, priority-only edge per
  // implementation-guide.html §01. S's own distance(S,S)=0 reaches A directly.
  RouteAdEntry fromS[] = { { NODE_S, 0 } };
  applyRouteAdvertisement(a, NODE_S, fromS, 1, 1000);

  uint8_t hop = 0;
  NodeId via = selectNextHop(a, NODE_S, /*priority=*/false, &hop);
  check(via == NODE_B && hop == 2,
        "NORMAL routing picks B (2 hops), not the shorter direct A-S edge");
}

// ---- 7. Priority traffic selects the shortest-hop route ----
void test_priority_selects_direct_s() {
  RoutingState a;
  init(a, NODE_A);

  RouteAdEntry fromB[] = { { NODE_B, 0 }, { NODE_S, 1 } };
  applyRouteAdvertisement(a, NODE_B, fromB, 2, 1000);
  RouteAdEntry fromS[] = { { NODE_S, 0 } };
  applyRouteAdvertisement(a, NODE_S, fromS, 1, 1000);

  uint8_t hop = 0;
  NodeId via = selectNextHop(a, NODE_S, /*priority=*/true, &hop);
  check(via == NODE_S && hop == 1,
        "PRIORITY routing forces the direct A-S edge (1 hop), overriding NORMAL's choice");
}

// ---- 8. A cannot select itself as its own next hop ----
void test_cannot_select_self() {
  RoutingState a;
  init(a, NODE_A);

  // B claims to know a route back to A - even so, A's route "to A" must
  // never resolve to a next hop at all.
  RouteAdEntry fromB[] = { { NODE_B, 0 }, { NODE_A, 5 } };
  applyRouteAdvertisement(a, NODE_B, fromB, 2, 1000);

  uint8_t hop = 0;
  NodeId via = selectNextHop(a, NODE_A, false, &hop);
  check(via == NODE_ID_UNKNOWN,
        "A's route 'to A' is always NODE_ID_UNKNOWN - destination==self is rejected structurally");
}

// ---- 9. A route update cannot incorrectly reduce a distance below valid bounds ----
void test_invalid_advertisement_rejected() {
  RoutingState a;
  init(a, NODE_A);

  // C falsely claims a nonzero distance to itself (must always be 0) -
  // the whole entry must be rejected, not stored as a real candidate.
  RouteAdEntry badFromC[] = { { NODE_C, 4 } };
  bool changed = applyRouteAdvertisement(a, NODE_C, badFromC, 1, 1000);
  check(!changed, "a neighbor's false self-distance claim is rejected outright");

  uint8_t hop = 0;
  check(selectNextHop(a, NODE_C, false, &hop) == NODE_ID_UNKNOWN,
        "no candidate is created from the rejected entry");

  // B's advertisement describes A's own distance to itself - must be
  // rejected regardless of the claimed value, since a node's distance to
  // itself is never learned from a neighbor.
  RouteAdEntry aboutSelf[] = { { NODE_A, 0 } };
  bool changed2 = applyRouteAdvertisement(a, NODE_B, aboutSelf, 1, 1000);
  check(!changed2, "an advertisement describing this node's own distance to itself is rejected");
}

// ---- 10. A route advertisement is associated with the neighbor that advertised it ----
void test_route_associated_with_neighbor() {
  RoutingState a;
  init(a, NODE_A);

  RouteAdEntry fromB[] = { { NODE_S, 1 } };
  RouteAdEntry fromC[] = { { NODE_S, 2 } };
  applyRouteAdvertisement(a, NODE_B, fromB, 1, 1000);
  applyRouteAdvertisement(a, NODE_C, fromC, 1, 1000);

  // Applying C's advertisement must not overwrite or remove B's -
  // they're stored in independent (destination, via-neighbor) slots.
  uint8_t hop = 0;
  NodeId best = selectNextHop(a, NODE_S, false, &hop);
  check(best == NODE_B && hop == 2, "B's candidate (via B, 2 hops) survives after C also advertises S");

  RoutingState aOnlyC;
  init(aOnlyC, NODE_A);
  applyRouteAdvertisement(aOnlyC, NODE_C, fromC, 1, 1000);
  NodeId viaCOnly = selectNextHop(aOnlyC, NODE_S, true, &hop);
  check(viaCOnly == NODE_C && hop == 3, "C's candidate (via C, 3 hops) was stored independently, keyed by neighbor C");
}

// ---- 13. Priority routing ignores link health (Phase 2) ----
void test_priority_ignores_unhealthy_link() {
  RoutingState a;
  init(a, NODE_A);

  RouteAdEntry fromB[] = { { NODE_B, 0 }, { NODE_S, 1 } };
  applyRouteAdvertisement(a, NODE_B, fromB, 2, 1000);
  RouteAdEntry fromS[] = { { NODE_S, 0 } };
  applyRouteAdvertisement(a, NODE_S, fromS, 1, 1000);

  bool unhealthy[NODE_ID_COUNT] = { false, false, false, false, false };
  unhealthy[NODE_S] = true;  // mark the direct A-S link unhealthy

  uint8_t hop = 0;
  NodeId via = selectNextHop(a, NODE_S, /*priority=*/true, &hop, unhealthy);
  check(via == NODE_S && hop == 1,
        "PRIORITY routing still forces the direct A-S edge even when it's marked unhealthy");
}

// ---- 14. Normal routing routes around an unhealthy B toward the surviving C candidate (Phase 2) ----
void test_normal_avoids_unhealthy_b() {
  RoutingState a;
  init(a, NODE_A);

  RouteAdEntry fromB[] = { { NODE_B, 0 }, { NODE_S, 1 } };
  applyRouteAdvertisement(a, NODE_B, fromB, 2, 1000);
  RouteAdEntry fromC[] = { { NODE_C, 0 }, { NODE_S, 2 } };
  applyRouteAdvertisement(a, NODE_C, fromC, 2, 1000);

  uint8_t hopHealthy = 0;
  check(selectNextHop(a, NODE_S, false, &hopHealthy) == NODE_B,
        "sanity: with both healthy, NORMAL still prefers B (2 hops) over C (3 hops)");

  bool unhealthy[NODE_ID_COUNT] = { false, false, false, false, false };
  unhealthy[NODE_B] = true;

  uint8_t hop = 0;
  NodeId via = selectNextHop(a, NODE_S, /*priority=*/false, &hop, unhealthy);
  check(via == NODE_C && hop == 3,
        "NORMAL routing: unhealthy B allows the surviving C candidate (3 hops) to become preferred");
}

// ---- 15. enumerateCandidates lists every valid NORMAL candidate, excluding priority-only edges, with health annotated (Phase 5) ----
void test_enumerate_candidates_lists_valid_normal_candidates() {
  RoutingState a;
  init(a, NODE_A);

  RouteAdEntry fromB[] = { { NODE_B, 0 }, { NODE_S, 1 } };
  applyRouteAdvertisement(a, NODE_B, fromB, 2, 1000);
  RouteAdEntry fromC[] = { { NODE_C, 0 }, { NODE_S, 2 } };
  applyRouteAdvertisement(a, NODE_C, fromC, 2, 1000);
  RouteAdEntry fromS[] = { { NODE_S, 0 } };
  applyRouteAdvertisement(a, NODE_S, fromS, 1, 1000);  // direct A-S edge — priority-only, must NOT be enumerated

  bool unhealthy[NODE_ID_COUNT] = { false, false, false, false, false };
  unhealthy[NODE_C] = true;

  CandidateInfo candidates[NODE_ID_COUNT];
  uint8_t n = enumerateCandidates(a, NODE_S, unhealthy, NODE_ID_UNKNOWN, candidates, NODE_ID_COUNT);

  check(n == 2, "enumerateCandidates finds exactly 2 valid NORMAL candidates (via B and via C, not the priority-only S edge)");
  bool sawB = false, sawC = false;
  for (uint8_t i = 0; i < n; i++) {
    if (candidates[i].nextHop == NODE_B) { sawB = true; check(candidates[i].hopCount == 2 && candidates[i].healthy, "via-B candidate reports 2 hops and healthy=true"); }
    if (candidates[i].nextHop == NODE_C) { sawC = true; check(candidates[i].hopCount == 3 && !candidates[i].healthy, "via-C candidate reports 3 hops and healthy=false (marked unhealthy)"); }
    check(candidates[i].nextHop != NODE_S, "the priority-only A-S edge never appears in NORMAL candidate enumeration");
  }
  check(sawB && sawC, "both real candidates were found");
}

// ---- 16. enumerateCandidates respects excludeNextHop — the Phase 5 loop-prevention guard (Part 8) ----
void test_enumerate_candidates_excludes_given_next_hop() {
  RoutingState a;
  init(a, NODE_A);

  RouteAdEntry fromB[] = { { NODE_B, 0 }, { NODE_S, 1 } };
  applyRouteAdvertisement(a, NODE_B, fromB, 2, 1000);
  RouteAdEntry fromC[] = { { NODE_C, 0 }, { NODE_S, 2 } };
  applyRouteAdvertisement(a, NODE_C, fromC, 2, 1000);

  CandidateInfo candidates[NODE_ID_COUNT];
  uint8_t n = enumerateCandidates(a, NODE_S, nullptr, /*excludeNextHop=*/NODE_B, candidates, NODE_ID_COUNT);

  check(n == 1 && candidates[0].nextHop == NODE_C,
        "excludeNextHop=B removes B from the candidate list entirely, leaving only C — "
        "this is the guard against bouncing a packet back to whoever just sent it (Part 8)");
}

}  // namespace

int main() {
  test_self_distance_zero();
  test_b_learns_s_at_one_hop();
  test_a_learns_s_via_b_at_two_hops();
  test_a_learns_alternate_via_c();
  test_stale_routes_invalidated();
  test_normal_selects_b_not_direct_s();
  test_priority_selects_direct_s();
  test_cannot_select_self();
  test_invalid_advertisement_rejected();
  test_route_associated_with_neighbor();
  test_priority_ignores_unhealthy_link();
  test_normal_avoids_unhealthy_b();
  test_enumerate_candidates_lists_valid_normal_candidates();
  test_enumerate_candidates_excludes_given_next_hop();

  std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
