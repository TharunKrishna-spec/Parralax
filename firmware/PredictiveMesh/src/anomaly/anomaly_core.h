#pragma once
#include <stdint.h>
#include "../config.h"

// ============================================================
// anomaly_core — pure sensor-health/anomaly algorithm, deliberately free
// of any Arduino/ADC/Serial dependency (no analogRead(), no logger::*).
// Mirrors the routing_core/predictor_core split from Phases 1-2. See
// docs/decisions.md.
//
// Reusable across sensors by design (implementation-guide.html's pot +
// LDR, and generically any future sensor): the core never branches on
// `sensor_id`, never assumes ADC-specific units, and accepts a generic
// SensorObservation{sensor_id, timestamp_ms, value, valid} - not a
// hardwired uint16_t ADC reading. `src/anomaly/anomaly.cpp` is the thin
// adapter that constructs one of these from a real `analogRead()` +
// `millis()` per physical sensor.
//
// implementation-guide.html §5.2 requires two independent detectors (MAD-Z
// spike/jump, flatline/stuck) that are never merged into one score. This
// module honors that at the evidence level - `EvalResult` exposes both
// `modifiedZ` and the flatline duration independently - while still
// collapsing them into one discrete `SensorState` for the state machine
// Part 4 of this phase's task spec requires; see docs/decisions.md for the
// FLATLINE-over-ANOMALY priority rule used when both would otherwise fire
// on the same sample.
// ============================================================

namespace anomaly_core {

// ---- Part 1: sensor abstraction ----
// `sensor_id` is an opaque small integer the adapter assigns per physical
// sensor (e.g. 0=POT, 1=LDR) purely for the adapter's/caller's own
// bookkeeping - anomaly_core itself never inspects or branches on it.
struct SensorObservation {
  uint8_t sensor_id;
  uint32_t timestamp_ms;
  float value;
  bool valid;  // false = caller-flagged invalid reading (e.g. an ADC fault upstream) - never folded into calibration or evidence
};

// ---- Part 4: sensor state machine ----
//
//   WARMUP -> NORMAL -> ANOMALY (debounced entry/exit)
//                    \-> FLATLINE (debounced exit; entry is inherently
//                        persistent via ANOMALY_STUCK_N)
//   Any state -> STALE (independent, time-driven, checked by tickStaleness)
//   Any state -> INVALID (immediate, whenever the caller flags an
//                observation invalid)
//   STALE/INVALID -> resumes whatever WARMUP/NORMAL/ANOMALY/FLATLINE logic
//                     applies once real, valid observations resume.
enum class SensorState : uint8_t { WARMUP, NORMAL, ANOMALY, FLATLINE, STALE, INVALID };

struct SensorCore {
  uint8_t sensor_id;
  SensorState state;
  bool calibrated;  // true once a median/MAD baseline has ever been frozen - distinct from `state`, since state can visit STALE/INVALID without losing the baseline

  // Warmup / calibration (Part 2's boot-calibration + variance safety check)
  float calBuffer[ANOMALY_CALIBRATION_SAMPLE_COUNT];
  uint16_t calCount;
  uint16_t warmupRetryCount;  // how many times the variance safety check has rejected a full buffer this boot

  // Frozen baseline - meaningful once `calibrated == true`
  float median;
  float mad;

  // Flatline tracking
  float lastValue;
  bool hasLastValue;
  uint16_t stuckCount;
  uint32_t flatlineStartMs;  // timestamp FLATLINE was entered; meaningful only while state == FLATLINE

  // Debounce/persistence (Part 5/6)
  uint16_t anomalyStreak;        // consecutive over-threshold samples while not yet ANOMALY
  uint16_t normalStreak;         // consecutive under-threshold samples while ANOMALY (recovery)
  uint16_t flatlineClearStreak;  // consecutive non-flat samples while FLATLINE (recovery)

  // Staleness (Part 4 STALE)
  uint32_t lastObservationMs;
  bool everObserved;

  // Latest evidence, for telemetry (Part 7)
  float lastRawValue;
  float lastModifiedZ;
};

// One evaluation's outcome - `changed` lets a caller fire a state-
// transition event without re-deriving it from before/after snapshots.
struct EvalResult {
  SensorState state;
  SensorState previousState;
  bool changed;
  float modifiedZ;  // 0 while WARMUP/INVALID/STALE - no baseline evidence exists yet for those
};

// Snapshot of everything Part 7 asks to expose for telemetry, read-only.
struct SensorTelemetry {
  uint8_t sensor_id;
  uint32_t timestamp_ms;
  float raw_value;
  float median;
  float mad;
  float modified_z;
  float anomaly_threshold;   // ANOMALY_MODIFIED_Z_THRESHOLD, echoed for self-describing payloads
  bool flatline_active;
  uint32_t flatline_duration_ms;
  SensorState state;
  bool valid;
};

void init(SensorCore& core, uint8_t sensor_id);

// Feeds one observation through the full pipeline: validity check first,
// then either warmup accumulation (with the variance safety check once the
// buffer fills, bounded-retry per ANOMALY_CALIBRATION_MAX_RETRIES - see
// docs/decisions.md) or steady-state MAD-Z + flatline evaluation with
// debounced state transitions. Never touches "now" except via
// obs.timestamp_ms.
EvalResult evaluate(SensorCore& core, const SensorObservation& obs);

// Independent, time-driven staleness check (Part 4 STALE) - mirrors
// predictor_core::tickStaleness. Call periodically regardless of whether
// new observations are arriving; a sensor that has simply stopped
// producing observations would otherwise never re-evaluate on its own.
EvalResult tickStaleness(SensorCore& core, uint32_t now);

SensorTelemetry snapshot(const SensorCore& core);

}  // namespace anomaly_core
