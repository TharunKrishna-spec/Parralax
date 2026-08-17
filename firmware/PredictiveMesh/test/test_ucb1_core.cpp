// Minimal host-side unit test harness for ucb1_core's bandit-statistics/
// UCB1-selection math (Phase 5, stretch/optional). Like every other
// *_core test suite in this project, this is NOT a network simulator — it
// never simulates real delivery outcomes; it only feeds ucb1_core's pure
// functions hand-constructed candidate lists/outcomes and checks the
// selection against the actual UCB1 formula (see the worked arithmetic in
// comments below). ucb1_core.h/.cpp have zero Arduino/ESP-NOW dependency
// specifically so this can compile and run with a plain host compiler.
// See docs/testing.md.
//
// What this file does NOT test (by design — these are adapter/compile-
// time-level guarantees, not pure-math properties; see docs/testing.md
// for how each is actually verified):
//   - Part 10 test 1 ("UCB1 disabled preserves existing routing"): ucb1_core
//     is a structurally separate module from routing_core with zero
//     dependency in either direction — when ENABLE_UCB1=0, nothing in the
//     compiled firmware even references this code at all (see
//     routing.cpp's #if ENABLE_UCB1 gating). The unchanged, still-passing
//     test_routing_core.cpp suite is the actual evidence for this.
//   - Part 10 test 8 ("priority traffic ignores UCB1"): ucb1_core's API has
//     no concept of "priority" at all — routing::getNextHop() simply never
//     calls into this module when priority==true. Structurally impossible
//     to exercise from here; verified by code review + the real ESP32
//     compile.
//
// Build & run (host g++ - NOT the ESP32 toolchain; run from this file's
// directory):
//   g++ -std=c++17 -Wall -Wextra -I ../src ../src/ucb1/ucb1_core.cpp test_ucb1_core.cpp -o test_ucb1_core
//   ./test_ucb1_core

#include "../src/ucb1/ucb1_core.h"
#include <cstdio>
#include <cmath>

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

using namespace ucb1_core;

const NodeId DEST_S = NODE_S;
const NodeId DEST_D = NODE_D;
const NodeId CAND_B = NODE_B;
const NodeId CAND_C = NODE_C;

// ---- 1. First observation handled correctly ----
void test_first_observation_recorded_correctly() {
  Ucb1State s;
  init(s);

  recordOutcome(s, DEST_S, CAND_B, true);

  ArmSnapshot snap = snapshot(s, DEST_S, CAND_B);
  check(snap.attempts == 1, "one recordOutcome call produces exactly one attempt");
  check(snap.successes == 1 && snap.failures == 0, "a successful first observation is recorded as one success, zero failures");
  check(std::fabs(snap.meanReward - 1.0f) < 0.001f, "mean reward after one success is 1.0");
}

// ---- 2. Zero-observation candidate receives exploration priority ----
void test_zero_observation_gets_priority() {
  Ucb1State s;
  init(s);

  // B has a long, strong success history; C has never been tried.
  for (int i = 0; i < 20; i++) recordOutcome(s, DEST_S, CAND_B, true);

  Candidate candidates[] = { { CAND_B, 2, true }, { CAND_C, 3, true } };
  NodeId chosen = selectNextHop(s, DEST_S, candidates, 2);

  check(chosen == CAND_C, "an unobserved candidate is selected over a heavily-proven one — standard UCB1 forced exploration");
}

// ---- 3. High-success candidate eventually dominates ----
void test_high_success_candidate_dominates() {
  Ucb1State s;
  init(s);

  // Give both candidates equal opportunity (10 attempts each) so neither
  // gets a zero-observation exemption, then let their true reward rates
  // separate the outcome.
  for (int i = 0; i < 10; i++) recordOutcome(s, DEST_S, CAND_B, true);   // B: 10/10 -> mean 1.0
  for (int i = 0; i < 9; i++) recordOutcome(s, DEST_S, CAND_C, false);
  recordOutcome(s, DEST_S, CAND_C, true);                                // C: 1/10 -> mean 0.1

  // Worked arithmetic: N = 20, C_explore = sqrt(2).
  // B: 1.0 + sqrt(2)*sqrt(ln(20)/10) = 1.0 + 1.41421*sqrt(0.29957) = 1.0 + 0.7739 = 1.7739
  // C: 0.1 + sqrt(2)*sqrt(ln(20)/10) = 0.1 + 0.7739 = 0.8739
  // Equal exploration bonus (equal attempts) means the mean-reward gap alone decides it.
  Candidate candidates[] = { { CAND_B, 2, true }, { CAND_C, 3, true } };
  NodeId chosen = selectNextHop(s, DEST_S, candidates, 2);

  check(chosen == CAND_B, "with equal attempts, the candidate with the far higher success rate wins");

  // Confirm this isn't a one-off — it holds across repeated selections
  // once the reward gap is established (Part 10 test 4: "eventually
  // dominates", not just "wins once").
  bool dominatedEveryTime = true;
  for (int i = 0; i < 5; i++) {
    if (selectNextHop(s, DEST_S, candidates, 2) != CAND_B) dominatedEveryTime = false;
  }
  check(dominatedEveryTime, "the high-success candidate continues to be selected across repeated queries, not just once");
}

// ---- 4. Poor candidate does not dominate indefinitely ----
void test_poor_candidate_does_not_dominate_indefinitely() {
  Ucb1State s;
  init(s);

  // C gets a lucky early run (small sample, looks perfect); B has a much
  // larger, honestly mediocre track record.
  for (int i = 0; i < 2; i++) recordOutcome(s, DEST_S, CAND_C, true);    // C: 2/2 -> mean 1.0, but tiny sample
  for (int i = 0; i < 15; i++) recordOutcome(s, DEST_S, CAND_B, true);
  for (int i = 0; i < 5; i++) recordOutcome(s, DEST_S, CAND_B, false);  // B: 15/20 -> mean 0.75, large sample

  Candidate candidates[] = { { CAND_B, 2, true }, { CAND_C, 3, true } };

  // Now give C a long run of real failures, revealing its true (poor) rate.
  for (int i = 0; i < 30; i++) recordOutcome(s, DEST_S, CAND_C, false);  // C: 2/32 -> mean 0.0625

  bool poorCandidateEverWon = false;
  for (int i = 0; i < 10; i++) {
    if (selectNextHop(s, DEST_S, candidates, 2) == CAND_C) poorCandidateEverWon = true;
  }
  check(!poorCandidateEverWon,
        "once a candidate's true poor delivery rate is revealed by enough trials, it stops being selected — "
        "an early lucky streak does not let it dominate indefinitely");
}

// ---- 5. Historical success influences selection ----
void test_historical_success_influences_selection() {
  Ucb1State fresh;
  init(fresh);
  Ucb1State favored;
  init(favored);
  for (int i = 0; i < 8; i++) recordOutcome(favored, DEST_S, CAND_C, true);
  for (int i = 0; i < 8; i++) recordOutcome(favored, DEST_S, CAND_B, false);

  Candidate candidates[] = { { CAND_B, 2, true }, { CAND_C, 3, true } };

  // In `favored`, C's real recorded history should win despite B's better
  // static hop count (2 vs 3) — UCB1 deliberately ignores hop count (see
  // ucb1_core.h).
  check(selectNextHop(favored, DEST_S, candidates, 2) == CAND_C,
        "recorded delivery history overrides the static hop-count heuristic when the evidence supports it");
}

// ---- 6. A current unhealthy link is not selected merely because of historical success ----
void test_unhealthy_link_not_selected_despite_history() {
  Ucb1State s;
  init(s);
  for (int i = 0; i < 50; i++) recordOutcome(s, DEST_S, CAND_B, true);  // B: overwhelming proven success

  Candidate candidates[] = { { CAND_B, 2, /*healthy=*/false }, { CAND_C, 3, /*healthy=*/true } };
  NodeId chosen = selectNextHop(s, DEST_S, candidates, 2);

  check(chosen == CAND_C,
        "a currently-unhealthy candidate is excluded from ranking regardless of how strong its historical "
        "success record is — link health is checked before UCB1 ranking, not blended into the score");
}

// ---- 7. Only candidates actually passed in are ever selected (no invalid/stale candidate ever appears) ----
void test_never_selects_a_candidate_not_provided() {
  Ucb1State s;
  init(s);
  for (int i = 0; i < 5; i++) recordOutcome(s, DEST_S, CAND_B, true);
  for (int i = 0; i < 5; i++) recordOutcome(s, DEST_S, CAND_C, true);
  for (int i = 0; i < 5; i++) recordOutcome(s, DEST_S, NODE_A, true);  // strong history for a candidate NOT offered below

  Candidate candidates[] = { { CAND_B, 2, true }, { CAND_C, 3, true } };
  bool onlyOfferedCandidatesReturned = true;
  for (int i = 0; i < 10; i++) {
    NodeId chosen = selectNextHop(s, DEST_S, candidates, 2);
    if (chosen != CAND_B && chosen != CAND_C) onlyOfferedCandidatesReturned = false;
  }
  check(onlyOfferedCandidatesReturned,
        "selectNextHop only ever returns a candidate from the list it was given — routing_core's own "
        "validity/staleness filtering (which built that list) is never bypassed or second-guessed");
}

// ---- 8. nextHop == prevHop is rejected via excludeNextHop ----
void test_exclude_next_hop_rejects_prev_hop() {
  Ucb1State s;
  init(s);
  for (int i = 0; i < 50; i++) recordOutcome(s, DEST_S, CAND_B, true);  // B: overwhelming history, would normally win easily

  Candidate candidates[] = { { CAND_B, 2, true }, { CAND_C, 3, true } };
  NodeId chosen = selectNextHop(s, DEST_S, candidates, 2, /*excludeNextHop=*/CAND_B);

  check(chosen == CAND_C, "excludeNextHop removes the candidate even when it would otherwise win overwhelmingly");
}

// ---- 9. Two-node loop scenario is prevented ----
void test_two_node_loop_prevented() {
  // Simulates: A received this packet from B (prevHop=B) and is deciding
  // where to forward it for destination S. Even if B has a flawless
  // recorded delivery history as a candidate for S, A must never pick B —
  // that would bounce the packet straight back to where it just came from.
  Ucb1State s;
  init(s);
  for (int i = 0; i < 100; i++) recordOutcome(s, DEST_S, CAND_B, true);

  Candidate candidates[] = { { CAND_B, 1, true }, { CAND_C, 3, true } };
  NodeId chosen = selectNextHop(s, DEST_S, candidates, 2, /*excludeNextHop=*/CAND_B);

  check(chosen != CAND_B, "the two-node bounce-back candidate is never selected, regardless of its recorded history");
  check(chosen == CAND_C, "the only remaining valid candidate is selected instead");
}

// ---- 10. Retry attempts do not incorrectly inflate UCB1 trials ----
void test_retries_do_not_inflate_trials() {
  Ucb1State s;
  init(s);

  // recordOutcome() has no concept of "retry" at all — every call is
  // exactly one trial by construction. This documents that contract
  // directly: the caller (reliability.cpp) is responsible for calling
  // this exactly once per resolved hop-transmission SERIES, not once per
  // radio attempt — see docs/decisions.md.
  recordOutcome(s, DEST_S, CAND_B, true);  // represents: 1 original attempt + 2 retries, resolved as ONE success

  ArmSnapshot snap = snapshot(s, DEST_S, CAND_B);
  check(snap.attempts == 1, "a hop-transmission series that took multiple radio retries before succeeding is still exactly one recorded trial");
}

// ---- 11. Successful delivery produces the correct reward ----
void test_successful_delivery_correct_reward() {
  Ucb1State s;
  init(s);
  recordOutcome(s, DEST_S, CAND_B, true);
  recordOutcome(s, DEST_S, CAND_B, true);
  recordOutcome(s, DEST_S, CAND_B, false);

  ArmSnapshot snap = snapshot(s, DEST_S, CAND_B);
  check(snap.successes == 2 && snap.failures == 1, "successes and failures are tallied independently and correctly");
  check(std::fabs(snap.meanReward - (2.0f / 3.0f)) < 0.001f, "mean reward is successes/attempts = 2/3");
}

// ---- 12. Failed delivery produces the correct reward ----
void test_failed_delivery_correct_reward() {
  Ucb1State s;
  init(s);
  recordOutcome(s, DEST_S, CAND_C, false);
  recordOutcome(s, DEST_S, CAND_C, false);

  ArmSnapshot snap = snapshot(s, DEST_S, CAND_C);
  check(snap.successes == 0 && snap.failures == 2, "an all-failure history is recorded with zero successes");
  check(std::fabs(snap.meanReward - 0.0f) < 0.001f, "mean reward after only failures is exactly 0.0");
}

// ---- 13. Multiple destinations maintain independent learning state ----
void test_independent_state_per_destination() {
  Ucb1State s;
  init(s);
  for (int i = 0; i < 10; i++) recordOutcome(s, DEST_S, CAND_B, true);
  for (int i = 0; i < 10; i++) recordOutcome(s, DEST_D, CAND_B, false);

  ArmSnapshot toS = snapshot(s, DEST_S, CAND_B);
  ArmSnapshot toD = snapshot(s, DEST_D, CAND_B);
  check(std::fabs(toS.meanReward - 1.0f) < 0.001f, "the same next-hop candidate's record toward destination S is unaffected by its record toward D");
  check(std::fabs(toD.meanReward - 0.0f) < 0.001f, "the same next-hop candidate's record toward destination D is unaffected by its record toward S");
}

// ---- 14. Multiple next hops maintain independent statistics ----
void test_independent_state_per_next_hop() {
  Ucb1State s;
  init(s);
  for (int i = 0; i < 10; i++) recordOutcome(s, DEST_S, CAND_B, true);
  for (int i = 0; i < 10; i++) recordOutcome(s, DEST_S, CAND_C, false);

  ArmSnapshot b = snapshot(s, DEST_S, CAND_B);
  ArmSnapshot c = snapshot(s, DEST_S, CAND_C);
  check(std::fabs(b.meanReward - 1.0f) < 0.001f && std::fabs(c.meanReward - 0.0f) < 0.001f,
        "two different next-hop candidates toward the same destination maintain fully independent statistics");
}

// ---- 15. Counter overflow is safely handled ----
void test_counter_overflow_saturates() {
  Ucb1State s;
  init(s);
  s.arms[DEST_S][CAND_B].attempts = UINT32_MAX;
  s.arms[DEST_S][CAND_B].successes = UINT32_MAX;

  recordOutcome(s, DEST_S, CAND_B, true);

  check(s.arms[DEST_S][CAND_B].attempts == UINT32_MAX, "attempts saturates at UINT32_MAX rather than wrapping to 0");
  check(s.arms[DEST_S][CAND_B].successes == UINT32_MAX, "successes saturates at UINT32_MAX rather than wrapping to 0");
}

// ---- 16. Fixed-size memory remains bounded, and out-of-range access is a safe no-op ----
void test_fixed_size_memory_and_bounds_safety() {
  check(sizeof(Ucb1State) == sizeof(ArmStats) * NODE_ID_COUNT * NODE_ID_COUNT,
        "Ucb1State's size is exactly NODE_ID_COUNT x NODE_ID_COUNT arms — fixed at compile time, no dynamic allocation");

  Ucb1State s;
  init(s);
  recordOutcome(s, static_cast<NodeId>(NODE_ID_COUNT + 10), CAND_B, true);  // out-of-range destination
  recordOutcome(s, DEST_S, static_cast<NodeId>(NODE_ID_COUNT + 10), true);   // out-of-range next-hop

  ArmSnapshot snap = snapshot(s, DEST_S, CAND_B);
  check(snap.attempts == 0, "an out-of-range recordOutcome() call is a safe no-op — it never corrupts a real, in-range arm");

  ArmSnapshot oob = snapshot(s, static_cast<NodeId>(NODE_ID_COUNT + 10), CAND_B);
  check(oob.nextHop == NODE_ID_UNKNOWN, "snapshot() on an out-of-range destination returns a safe, clearly-invalid sentinel, not garbage");
}

}  // namespace

int main() {
  test_first_observation_recorded_correctly();
  test_zero_observation_gets_priority();
  test_high_success_candidate_dominates();
  test_poor_candidate_does_not_dominate_indefinitely();
  test_historical_success_influences_selection();
  test_unhealthy_link_not_selected_despite_history();
  test_never_selects_a_candidate_not_provided();
  test_exclude_next_hop_rejects_prev_hop();
  test_two_node_loop_prevented();
  test_retries_do_not_inflate_trials();
  test_successful_delivery_correct_reward();
  test_failed_delivery_correct_reward();
  test_independent_state_per_destination();
  test_independent_state_per_next_hop();
  test_counter_overflow_saturates();
  test_fixed_size_memory_and_bounds_safety();

  std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
