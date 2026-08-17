// Minimal host-side unit test harness for predictor_core's EWMA/slope/PDR/
// hysteresis math (Phase 2). Like test_routing_core.cpp, this is NOT a
// network/radio simulator: it never simulates ESP-NOW, real RSSI hardware,
// or a live send outcome. It only feeds predictor_core's pure functions
// hand-computed inputs and checks outputs against the same formulas
// implementation-guide.html §5.1 and docs/decisions.md specify - the
// expected values in the comments below are worked by hand, not guessed.
// predictor_core.h/.cpp have zero Arduino/ESP-NOW dependency specifically
// so this can compile and run with a plain host compiler. See
// docs/testing.md.
//
// Build & run (host g++ - NOT the ESP32 toolchain; run from this file's
// directory):
//   g++ -std=c++17 -I ../src ../src/predictor/predictor_core.cpp test_predictor_core.cpp -o test_predictor_core
//   ./test_predictor_core

#include "../src/predictor/predictor_core.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>

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

using namespace predictor_core;

const NodeId SELF = NODE_A;
const NodeId NEIGHBOR = NODE_B;

// ---- 1. Stable RSSI -> slope approximately stable -> healthy link ----
void test_stable_rssi_is_healthy() {
  PredictorState s;
  init(s, SELF);

  uint32_t now = 1000;
  for (int i = 0; i < 10; i++) {
    onRssiSample(s, NEIGHBOR, -50, now);
    now += 1000;
  }

  const NeighborLinkState& n = linkState(s, NEIGHBOR);
  check(std::fabs(n.slope) < 0.01f, "stable RSSI produces an approximately-zero slope");
  check(healthState(s, NEIGHBOR) == LinkHealth::HEALTHY, "stable RSSI keeps the link HEALTHY");
  check(linkScore(s, NEIGHBOR) > 0.95f, "stable RSSI with default PDR yields a near-1.0 link_score");
}

// ---- 2. Improving RSSI -> slope indicates improvement ----
void test_improving_rssi_positive_slope() {
  PredictorState s;
  init(s, SELF);

  uint32_t now = 1000;
  int8_t rssi[] = { -70, -65, -60, -55, -50 };
  for (int8_t r : rssi) {
    onRssiSample(s, NEIGHBOR, r, now);
    now += 1000;
  }

  const NeighborLinkState& n = linkState(s, NEIGHBOR);
  check(n.slope > 0.0f, "a steadily improving (less negative) RSSI trend produces a positive slope");
  check(healthState(s, NEIGHBOR) == LinkHealth::HEALTHY, "an improving link never becomes unhealthy");
}

// ---- 3. Degrading RSSI -> EWMA follows the trend, slope indicates degradation ----
void test_degrading_rssi_negative_slope() {
  PredictorState s;
  init(s, SELF);

  uint32_t now = 1000;
  int8_t rssi[] = { -50, -55, -60, -65, -70 };
  for (int8_t r : rssi) {
    onRssiSample(s, NEIGHBOR, r, now);
    now += 1000;
  }

  const NeighborLinkState& n = linkState(s, NEIGHBOR);
  // Hand-computed EWMA (alpha=0.3): -50, -51.5, -54.05, -57.335, -61.1345
  check(n.ewmaRssi < -55.0f, "EWMA follows the raw RSSI's downward trend (lagged, not instantaneous)");
  check(n.slope < 0.0f, "a steadily degrading RSSI trend produces a negative (signed correctly) slope");
}

// ---- 4. Noisy RSSI -> EWMA is smoother than raw values ----
void test_noisy_rssi_ewma_smoother_than_raw() {
  PredictorState s;
  init(s, SELF);

  uint32_t now = 1000;
  onRssiSample(s, NEIGHBOR, -50, now);
  now += 1000;
  float ewmaBefore = linkState(s, NEIGHBOR).ewmaRssi;

  onRssiSample(s, NEIGHBOR, -70, now);  // a sharp, noisy 20 dBm swing
  const NeighborLinkState& n = linkState(s, NEIGHBOR);
  float rawSwing = std::fabs(-70.0f - (-50.0f));       // 20
  float ewmaSwing = std::fabs(n.ewmaRssi - ewmaBefore);  // alpha * rawSwing = 0.3 * 20 = 6

  check(ewmaSwing < rawSwing, "one noisy raw sample moves the EWMA by less than the raw swing itself");
  check(ewmaSwing < 10.0f, "EWMA swing matches the expected alpha-scaled magnitude (~6, well under raw's 20)");
}

// ---- 5. PDR degradation -> PDR metric decreases ----
void test_pdr_degrades_on_failures() {
  PredictorState s;
  init(s, SELF);

  onSendOutcome(s, NEIGHBOR, true, 1000);
  onSendOutcome(s, NEIGHBOR, true, 2000);
  onSendOutcome(s, NEIGHBOR, true, 3000);
  float pdrBefore = linkState(s, NEIGHBOR).pdrEwma;

  onSendOutcome(s, NEIGHBOR, false, 4000);
  onSendOutcome(s, NEIGHBOR, false, 5000);
  onSendOutcome(s, NEIGHBOR, false, 6000);
  float pdrAfter = linkState(s, NEIGHBOR).pdrEwma;

  check(pdrBefore > 0.99f, "an all-success run keeps PDR at (essentially) 1.0");
  check(pdrAfter < pdrBefore, "a run of failed sends drives the PDR EWMA down");
}

// ---- 6. Stable good PDR -> healthy PDR evidence ----
void test_stable_good_pdr_is_healthy() {
  PredictorState s;
  init(s, SELF);

  for (int i = 0; i < 5; i++) onSendOutcome(s, NEIGHBOR, true, 1000 + i * 1000);

  check(linkState(s, NEIGHBOR).pdrEwma > 0.99f, "an all-success PDR history stays at (essentially) 1.0");
  check(healthState(s, NEIGHBOR) == LinkHealth::HEALTHY, "stable good PDR alone keeps the link HEALTHY");
  check(linkScore(s, NEIGHBOR) > 0.95f, "stable good PDR with no RSSI evidence yields a near-1.0 score (degrade_term defaults to 0)");
}

// ---- 7. Sudden silence -> staleness fast-path activates ----
void test_staleness_fast_path() {
  PredictorState s;
  init(s, SELF);

  uint32_t now = 1000;
  onRssiSample(s, NEIGHBOR, -50, now);
  onRssiSample(s, NEIGHBOR, -50, now + 500);

  check(healthState(s, NEIGHBOR) == LinkHealth::HEALTHY, "sanity: healthy immediately after a couple of good samples");

  // No further samples arrive at all - slope/PDR evidence is frozen and
  // would report nothing wrong by itself (Part 5's own point: "slope-only
  // predictor can miss the failure"). Only the independent staleness
  // check, driven by elapsed time, can catch this.
  uint32_t silentNow = (now + 500) + PREDICTOR_STALENESS_TIMEOUT_MS + 1;
  RecomputeResult r = tickStaleness(s, NEIGHBOR, silentNow);

  check(r.becameUnhealthy, "the staleness fast-path fires becameUnhealthy with zero new RSSI samples");
  check(isUnhealthy(s, NEIGHBOR), "a gone-silent neighbor is flagged unhealthy purely from elapsed time");
}

// ---- 8. Combined RSSI + PDR degradation -> link_score decreases ----
void test_combined_degradation_lowers_score() {
  PredictorState stable;
  init(stable, SELF);
  onSendOutcome(stable, NEIGHBOR, true, 1000);
  onRssiSample(stable, NEIGHBOR, -50, 2000);
  onRssiSample(stable, NEIGHBOR, -50, 3000);

  PredictorState degraded;
  init(degraded, SELF);
  onSendOutcome(degraded, NEIGHBOR, false, 1000);
  onRssiSample(degraded, NEIGHBOR, -50, 2000);
  onRssiSample(degraded, NEIGHBOR, -55, 3000);

  check(linkScore(degraded, NEIGHBOR) < linkScore(stable, NEIGHBOR),
        "combined RSSI degradation + PDR loss produces a strictly lower link_score than an all-healthy baseline");
}

// ---- 9. Hysteresis: score crosses T_LOW -> unhealthy (with debounce satisfied) ----
void test_hysteresis_crosses_t_low_to_unhealthy() {
  PredictorState s;
  init(s, SELF);

  onSendOutcome(s, NEIGHBOR, false, 1000);  // pdr -> 0.0 (bootstrap)

  uint32_t now = 2000;
  int8_t rssi[] = { -50, -55, -60, -65 };  // -5 dBm/sample decline, well past SLOPE_REF
  RecomputeResult last{};
  for (int8_t r : rssi) {
    last = onRssiSample(s, NEIGHBOR, r, now);
    now += 1000;
  }

  check(last.becameUnhealthy, "3 consecutive below-T_LOW evaluations (score=0.0 the whole way) trip the HEALTHY->UNHEALTHY transition");
  check(isUnhealthy(s, NEIGHBOR), "the neighbor is UNHEALTHY after the debounced transition");
}

// ---- 10. Score remains between thresholds -> state does not flap ----
void test_midband_score_does_not_flap() {
  PredictorState s;
  init(s, SELF);

  onSendOutcome(s, NEIGHBOR, false, 1000);  // pdr -> 0.0 (bootstrap)
  onSendOutcome(s, NEIGHBOR, true, 2000);   // pdr -> 0.1
  onSendOutcome(s, NEIGHBOR, true, 3000);   // pdr -> 0.19
  // score so far (degrade_term=0, no RSSI yet): 0.5 + 0.5*0.19 = 0.595 - inside (T_LOW, T_HIGH)

  uint32_t now = 4000;
  for (int i = 0; i < 5; i++) {
    RecomputeResult r = onRssiSample(s, NEIGHBOR, -50, now);  // flat RSSI -> slope stays 0 -> score stays 0.595
    check(!r.becameUnhealthy && !r.becameHealthy, "a score sitting between T_LOW and T_HIGH never fires a transition");
    now += 1000;
  }

  check(healthState(s, NEIGHBOR) == LinkHealth::HEALTHY, "state remains HEALTHY throughout a mid-band score run - no flapping");
}

// ---- 11. Recovery: score crosses T_HIGH -> healthy/rejoin eligible ----
void test_recovery_crosses_t_high_to_healthy() {
  PredictorState s;
  init(s, SELF);

  // Drive to UNHEALTHY first, exactly as in test 9.
  onSendOutcome(s, NEIGHBOR, false, 1000);
  uint32_t now = 2000;
  for (int8_t r : { -50, -55, -60, -65 }) {
    onRssiSample(s, NEIGHBOR, r, now);
    now += 1000;
  }
  check(isUnhealthy(s, NEIGHBOR), "sanity: neighbor is UNHEALTHY before the recovery sequence begins");

  // Recover PDR with a run of real successes (pdr -> 1 - 0.9^6 ~= 0.469).
  for (int i = 0; i < 6; i++) {
    onSendOutcome(s, NEIGHBOR, true, now);
    now += 1000;
  }

  // Flush the RSSI window with flat, healthy samples - PREDICTOR_SLOPE_WINDOW
  // pushes fully overwrite the old declining window, driving slope back to
  // exactly 0 (degrade_term=0) for several consecutive evaluations.
  bool sawRecovery = false;
  for (int i = 0; i < 12; i++) {
    RecomputeResult r = onRssiSample(s, NEIGHBOR, -50, now);
    if (r.becameHealthy) sawRecovery = true;
    now += 1000;
  }

  check(sawRecovery, "recovery (UNHEALTHY -> HEALTHY) fires once the score clears T_HIGH for enough consecutive evaluations");
  check(healthState(s, NEIGHBOR) == LinkHealth::HEALTHY, "the neighbor is HEALTHY again after the recovery sequence");
}

// ---- 12. Single noisy bad sample does not immediately trigger reroute ----
void test_single_bad_sample_does_not_immediately_reroute() {
  PredictorState s;
  init(s, SELF);

  onSendOutcome(s, NEIGHBOR, false, 1000);  // pdr -> 0.0

  // Only 2 RSSI samples -> exactly 1 qualifying below-T_LOW evaluation
  // (the very first sample has no slope yet - see leastSquaresSlope's
  // n<2 guard - so it doesn't count). PREDICTOR_CONSECUTIVE_BAD_COUNT is 3.
  RecomputeResult r1 = onRssiSample(s, NEIGHBOR, -50, 2000);
  RecomputeResult r2 = onRssiSample(s, NEIGHBOR, -55, 3000);

  check(!r1.becameUnhealthy && !r2.becameUnhealthy,
        "a single below-threshold evaluation does not by itself flip the link unhealthy");
  check(!isUnhealthy(s, NEIGHBOR),
        "the consecutive-sample debounce (count=3) gates against one noisy bad sample");
}

}  // namespace

int main() {
  test_stable_rssi_is_healthy();
  test_improving_rssi_positive_slope();
  test_degrading_rssi_negative_slope();
  test_noisy_rssi_ewma_smoother_than_raw();
  test_pdr_degrades_on_failures();
  test_stable_good_pdr_is_healthy();
  test_staleness_fast_path();
  test_combined_degradation_lowers_score();
  test_hysteresis_crosses_t_low_to_unhealthy();
  test_midband_score_does_not_flap();
  test_recovery_crosses_t_high_to_healthy();
  test_single_bad_sample_does_not_immediately_reroute();

  std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
