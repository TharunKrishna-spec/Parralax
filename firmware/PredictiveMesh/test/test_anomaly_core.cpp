// Minimal host-side unit test harness for anomaly_core's sensor-health
// state machine (Phase 3): median/MAD boot calibration + modified Z-score
// + flatline detector + debounce/recovery + staleness. Like
// test_routing_core.cpp/test_predictor_core.cpp, this is NOT a hardware
// simulator - it never simulates a real ADC or a real potentiometer/LDR.
// It only feeds anomaly_core's pure functions hand-constructed
// observations and checks outputs against the guide's own formulas plus
// this phase's explicit state-machine/debounce requirements - the expected
// values below are worked by hand, not guessed. anomaly_core.h/.cpp have
// zero Arduino/ADC dependency specifically so this can compile and run
// with a plain host compiler. See docs/testing.md.
//
// Build & run (host g++ - NOT the ESP32 toolchain; run from this file's
// directory):
//   g++ -std=c++17 -I ../src ../src/anomaly/anomaly_core.cpp test_anomaly_core.cpp -o test_anomaly_core
//   ./test_anomaly_core

#include "../src/anomaly/anomaly_core.h"
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

using namespace anomaly_core;

const uint32_t STEP_MS = SENSOR_SAMPLE_INTERVAL_MS;

SensorObservation obsOf(uint8_t sensorId, uint32_t t, float value, bool valid = true) {
  return SensorObservation{ sensorId, t, value, valid };
}

// Feeds ANOMALY_CALIBRATION_SAMPLE_COUNT identical observations, leaving
// `core` calibrated and (assuming a safe/flat variance) in NORMAL.
// Returns the timestamp of the last sample fed.
uint32_t calibrateFlat(SensorCore& core, uint8_t sensorId, float value, uint32_t startMs = 1000) {
  uint32_t t = startMs;
  for (uint16_t i = 0; i < ANOMALY_CALIBRATION_SAMPLE_COUNT; i++) {
    evaluate(core, obsOf(sensorId, t, value));
    t += STEP_MS;
  }
  return t;
}

// ---- 1. Warmup with insufficient history ----
void test_warmup_insufficient_history() {
  SensorCore core;
  init(core, 0);

  uint32_t t = 1000;
  for (uint16_t i = 0; i < ANOMALY_CALIBRATION_SAMPLE_COUNT / 2; i++) {
    evaluate(core, obsOf(0, t, 2000));
    t += STEP_MS;
  }

  check(core.state == SensorState::WARMUP, "state remains WARMUP before the calibration buffer fills");
  check(!core.calibrated, "no baseline is frozen before ANOMALY_CALIBRATION_SAMPLE_COUNT samples arrive");
}

// ---- 2. Stable normal signal ----
void test_stable_normal_signal() {
  SensorCore core;
  init(core, 0);
  uint32_t t = calibrateFlat(core, 0, 2000);

  check(core.calibrated, "sanity: calibration completed");
  check(core.state == SensorState::NORMAL, "sanity: state is NORMAL immediately after calibration");

  // Alternates 2000/2005 - each step exceeds ANOMALY_FLATLINE_EPS (2.0, so
  // never flatlines) while staying well under the modified-Z threshold
  // (mad is floored to ANOMALY_MAD_FLOOR=3.0 after a flat calibration; z
  // for a 5 LSB deviation = 0.6745*5/3.0 ~= 1.12, well under 3.5).
  for (int i = 0; i < 20; i++) {
    EvalResult r = evaluate(core, obsOf(0, t, (i % 2 == 0) ? 2000.0f : 2005.0f));
    check(r.state == SensorState::NORMAL, "a realistic, mildly-varying stable signal stays NORMAL");
    t += STEP_MS;
  }
}

// ---- 3. Single statistical outlier does not immediately flip to ANOMALY ----
void test_single_outlier_debounced() {
  SensorCore core;
  init(core, 0);
  uint32_t t = calibrateFlat(core, 0, 2000);

  // mad=3.0 after flat calibration; a 30 LSB deviation gives
  // z = 0.6745*30/3.0 = 6.745, well over the 3.5 threshold.
  EvalResult r = evaluate(core, obsOf(0, t, 2030));
  t += STEP_MS;
  check(r.modifiedZ > ANOMALY_MODIFIED_Z_THRESHOLD, "sanity: the single sample's raw modified_z evidence does exceed the threshold");
  check(r.state == SensorState::NORMAL, "one anomalous sample alone does not flip the state to ANOMALY (ANOMALY_CONSECUTIVE_COUNT debounce)");
}

// ---- 4. Repeated statistical anomalies trigger ANOMALY ----
void test_repeated_anomalies_trigger_state() {
  SensorCore core;
  init(core, 0);
  uint32_t t = calibrateFlat(core, 0, 2000);

  EvalResult r{};
  for (uint16_t i = 0; i < ANOMALY_CONSECUTIVE_COUNT; i++) {
    r = evaluate(core, obsOf(0, t, 2030));
    t += STEP_MS;
  }
  check(r.state == SensorState::ANOMALY, "ANOMALY_CONSECUTIVE_COUNT consecutive over-threshold samples transition the state to ANOMALY");
  check(r.changed, "the transition into ANOMALY is reported as a real state change");
}

// ---- 5. Flatline within tolerance ----
void test_flatline_within_tolerance() {
  SensorCore core;
  init(core, 0);
  uint32_t t = calibrateFlat(core, 0, 2000);

  // The first post-calibration sample only establishes lastValue (there is
  // no prior sample to compare against yet), so ANOMALY_STUCK_N + 1 total
  // samples are needed to observe ANOMALY_STUCK_N consecutive unchanged
  // deltas.
  EvalResult r{};
  for (uint16_t i = 0; i < ANOMALY_STUCK_N + 1; i++) {
    r = evaluate(core, obsOf(0, t, 2000));  // identical to the calibrated value and to itself, every step
    t += STEP_MS;
  }
  check(r.state == SensorState::FLATLINE, "ANOMALY_STUCK_N consecutive within-tolerance samples transition the state to FLATLINE");
}

// ---- 6. Flatline outside tolerance never triggers ----
void test_flatline_outside_tolerance_never_triggers() {
  SensorCore core;
  init(core, 0);
  uint32_t t = calibrateFlat(core, 0, 2000);

  bool sawFlatline = false;
  for (uint16_t i = 0; i < ANOMALY_STUCK_N + 20; i++) {
    float v = (i % 2 == 0) ? 2000.0f : 2005.0f;  // each step exceeds ANOMALY_FLATLINE_EPS
    EvalResult r = evaluate(core, obsOf(0, t, v));
    if (r.state == SensorState::FLATLINE) sawFlatline = true;
    t += STEP_MS;
  }
  check(!sawFlatline, "a genuinely changing signal (every step beyond ANOMALY_FLATLINE_EPS) never triggers FLATLINE");
}

// ---- 7. Recovery after anomaly ----
void test_recovery_after_anomaly() {
  SensorCore core;
  init(core, 0);
  uint32_t t = calibrateFlat(core, 0, 2000);

  for (uint16_t i = 0; i < ANOMALY_CONSECUTIVE_COUNT; i++) {
    evaluate(core, obsOf(0, t, 2030));
    t += STEP_MS;
  }
  check(core.state == SensorState::ANOMALY, "sanity: state is ANOMALY before the recovery sequence");

  EvalResult r{};
  for (uint16_t i = 0; i < ANOMALY_RECOVERY_COUNT; i++) {
    r = evaluate(core, obsOf(0, t, 2000));  // back at the calibrated baseline
    t += STEP_MS;
  }
  check(r.state == SensorState::NORMAL, "ANOMALY_RECOVERY_COUNT consecutive normal samples recover the state to NORMAL");
}

// ---- 8. Recovery after flatline ----
void test_recovery_after_flatline() {
  SensorCore core;
  init(core, 0);
  uint32_t t = calibrateFlat(core, 0, 2000);

  for (uint16_t i = 0; i < ANOMALY_STUCK_N + 1; i++) {
    evaluate(core, obsOf(0, t, 2000));
    t += STEP_MS;
  }
  check(core.state == SensorState::FLATLINE, "sanity: state is FLATLINE before the recovery sequence");

  // A single changed sample alone must not instantly clear FLATLINE.
  EvalResult single = evaluate(core, obsOf(0, t, 2010));
  t += STEP_MS;
  check(single.state == SensorState::FLATLINE, "one changed sample alone does not instantly recover from FLATLINE (Part 6 persistence)");

  EvalResult r{};
  for (uint16_t i = 1; i < ANOMALY_FLATLINE_RECOVERY_COUNT; i++) {
    r = evaluate(core, obsOf(0, t, 2010 + i));
    t += STEP_MS;
  }
  check(r.state == SensorState::NORMAL, "ANOMALY_FLATLINE_RECOVERY_COUNT consecutive non-flat samples recover the state to NORMAL");
}

// ---- 9. Invalid sample ----
void test_invalid_sample() {
  SensorCore core;
  init(core, 0);
  uint32_t t = calibrateFlat(core, 0, 2000);

  EvalResult r = evaluate(core, obsOf(0, t, 0, /*valid=*/false));
  t += STEP_MS;
  check(r.state == SensorState::INVALID, "a caller-flagged invalid observation immediately reports INVALID");
  check(r.changed, "the transition into INVALID is reported as a real state change");

  EvalResult recovered = evaluate(core, obsOf(0, t, 2000));
  check(recovered.state == SensorState::NORMAL, "a valid observation right after one invalid sample resumes normal evaluation cleanly (the invalid sample did not corrupt ongoing evidence)");
}

// ---- 10. Stale sensor ----
void test_stale_sensor() {
  SensorCore core;
  init(core, 0);
  uint32_t t = calibrateFlat(core, 0, 2000);
  evaluate(core, obsOf(0, t, 2000));

  check(core.state == SensorState::NORMAL, "sanity: state is NORMAL immediately before going silent");

  uint32_t silentNow = t + ANOMALY_STALE_TIMEOUT_MS + 1;
  EvalResult r = tickStaleness(core, silentNow);

  check(r.state == SensorState::STALE, "tickStaleness reports STALE once ANOMALY_STALE_TIMEOUT_MS elapses with no new observation");
  check(r.changed, "the transition into STALE is reported as a real state change");
}

// ---- 11. No false anomaly from normal noise ----
void test_no_false_anomaly_from_noise() {
  SensorCore core;
  init(core, 0);
  uint32_t t = calibrateFlat(core, 0, 2000);

  bool everLeftNormal = false;
  for (int i = 0; i < 40; i++) {
    float v = 2000.0f + static_cast<float>((i % 3) - 1) * 4.0f;  // cycles 1996, 2000, 2004 - low modified_z, and consecutive values always differ by >= 4 (never flatlines)
    EvalResult r = evaluate(core, obsOf(0, t, v));
    if (r.state != SensorState::NORMAL) everLeftNormal = true;
    t += STEP_MS;
  }
  check(!everLeftNormal, "realistic small jitter around the calibrated median never triggers ANOMALY or FLATLINE");
}

// ---- 12. MAD robustness against an isolated extreme value ----
void test_mad_robust_to_isolated_outlier() {
  SensorCore core;
  init(core, 0);

  // One 150 LSB outlier among 99 identical samples. Chosen deliberately:
  // large enough to clearly demonstrate median/MAD's robustness (a naive
  // mean would shift by 1.5 LSB, a naive stddev would be ~14.9), but still
  // small enough that the *calibration step's own separate* variance
  // safety envelope (ANOMALY_MAX_CALIBRATION_VARIANCE, ordinary variance -
  // deliberately NOT the MAD-Z detector itself, see docs/decisions.md)
  // still accepts it (variance here ~= 222.75, comfortably under 400) -
  // an outlier extreme enough to blow past that gate would never reach
  // median/MAD computation at all, since the whole calibration attempt
  // would be rejected and retried first.
  uint32_t t = 1000;
  for (uint16_t i = 0; i < ANOMALY_CALIBRATION_SAMPLE_COUNT; i++) {
    float v = (i == 0) ? 2150.0f : 2000.0f;
    evaluate(core, obsOf(0, t, v));
    t += STEP_MS;
  }

  check(core.calibrated, "sanity: calibration completed - the outlier's magnitude was chosen to pass the (separate, ordinary-variance-based) calibration safety gate");
  check(core.median == 2000.0f, "the median is completely unmoved by a single isolated extreme value (99 of 100 samples agree)");
  check(core.mad == ANOMALY_MAD_FLOOR, "the MAD is still floored - a single outlier among 100 samples cannot itself inflate the median absolute deviation of the other 99");
}

// ---- 13. Multiple sensor IDs maintain independent state ----
void test_independent_sensor_state() {
  SensorCore potCore, ldrCore;
  init(potCore, 0);
  init(ldrCore, 1);

  uint32_t t1 = calibrateFlat(potCore, 0, 1500);
  uint32_t t2 = calibrateFlat(ldrCore, 1, 3000);

  check(potCore.median == 1500.0f && ldrCore.median == 3000.0f,
        "two independent SensorCore instances calibrate to their own, unrelated baselines");

  // Drive only the POT sensor into ANOMALY.
  for (uint16_t i = 0; i < ANOMALY_CONSECUTIVE_COUNT; i++) {
    evaluate(potCore, obsOf(0, t1, 1530));
    t1 += STEP_MS;
  }
  // Feed the LDR sensor only normal samples in the meantime.
  for (uint16_t i = 0; i < ANOMALY_CONSECUTIVE_COUNT; i++) {
    evaluate(ldrCore, obsOf(1, t2, 3000));
    t2 += STEP_MS;
  }

  check(potCore.state == SensorState::ANOMALY, "driving the POT sensor's state to ANOMALY only affects the POT core");
  check(ldrCore.state == SensorState::NORMAL, "the independently-updated LDR core is unaffected by the POT core's state - no shared/leaked state");
}

// ---- 14. Sensor anomaly does not automatically become a link anomaly ----
void test_sensor_anomaly_does_not_affect_routing() {
  routing_core::RoutingState rstate;
  routing_core::init(rstate, NODE_A);
  routing_core::RouteAdEntry fromB[] = { { NODE_B, 0 }, { NODE_S, 1 } };
  routing_core::applyRouteAdvertisement(rstate, NODE_B, fromB, 2, 1000);

  uint8_t hopBefore = 0;
  NodeId beforeSensorAnomaly = routing_core::selectNextHop(rstate, NODE_S, /*priority=*/false, &hopBefore);

  // Drive a completely separate sensor into ANOMALY - anomaly_core has no
  // reference to routing_core anywhere, and routing_core's own selection
  // takes no sensor-health input at all (see routing_core.h's
  // selectNextHop signature) - this is a structural guarantee, not just a
  // convention, but demonstrated here as a real regression check too.
  SensorCore sensor;
  init(sensor, 0);
  uint32_t t = calibrateFlat(sensor, 0, 2000);
  for (uint16_t i = 0; i < ANOMALY_CONSECUTIVE_COUNT; i++) {
    evaluate(sensor, obsOf(0, t, 2030));
    t += STEP_MS;
  }
  check(sensor.state == SensorState::ANOMALY, "sanity: the sensor really did transition to ANOMALY");

  uint8_t hopAfter = 0;
  NodeId afterSensorAnomaly = routing_core::selectNextHop(rstate, NODE_S, /*priority=*/false, &hopAfter);

  check(beforeSensorAnomaly == afterSensorAnomaly && hopBefore == hopAfter,
        "a sensor entering ANOMALY has zero effect on routing's next-hop decision - sensor health and network health are separate failure domains");
}

}  // namespace

int main() {
  test_warmup_insufficient_history();
  test_stable_normal_signal();
  test_single_outlier_debounced();
  test_repeated_anomalies_trigger_state();
  test_flatline_within_tolerance();
  test_flatline_outside_tolerance_never_triggers();
  test_recovery_after_anomaly();
  test_recovery_after_flatline();
  test_invalid_sample();
  test_stale_sensor();
  test_no_false_anomaly_from_noise();
  test_mad_robust_to_isolated_outlier();
  test_independent_sensor_state();
  test_sensor_anomaly_does_not_affect_routing();

  std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
