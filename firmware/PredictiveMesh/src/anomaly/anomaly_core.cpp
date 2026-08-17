#include "anomaly_core.h"
#include <math.h>

namespace anomaly_core {

namespace {

// Plain insertion sort - fine for a fixed, small
// (ANOMALY_CALIBRATION_SAMPLE_COUNT, 100) array, and avoids pulling in
// <algorithm> to match routing_core/predictor_core's minimal-STL embedded
// style.
void insertionSort(float* arr, uint16_t n) {
  for (uint16_t i = 1; i < n; i++) {
    float key = arr[i];
    int32_t j = static_cast<int32_t>(i) - 1;
    while (j >= 0 && arr[j] > key) {
      arr[j + 1] = arr[j];
      j--;
    }
    arr[j + 1] = key;
  }
}

// Standard median definition (average of the two middle elements for an
// even-length array - ANOMALY_CALIBRATION_SAMPLE_COUNT is even, so this
// branch is the one actually exercised).
float medianOf(float* arr, uint16_t n) {
  insertionSort(arr, n);
  if (n % 2 == 1) return arr[n / 2];
  return (arr[n / 2 - 1] + arr[n / 2]) / 2.0f;
}

void resetStreaks(SensorCore& core) {
  core.anomalyStreak = 0;
  core.normalStreak = 0;
  core.flatlineClearStreak = 0;
}

// Computes variance/median/MAD from a full calibration buffer.
// implementation-guide.html's boot-sequence diagram: reject (reset the
// buffer) if variance exceeds the safety envelope, unless `forceAccept` is
// set - see docs/decisions.md for when that happens
// (ANOMALY_CALIBRATION_MAX_RETRIES exhausted). Returns true if a baseline
// was frozen (core.calibrated becomes true) this call.
bool tryFinalizeCalibration(SensorCore& core, bool forceAccept) {
  float samples[ANOMALY_CALIBRATION_SAMPLE_COUNT];
  float sum = 0.0f;
  for (uint16_t i = 0; i < ANOMALY_CALIBRATION_SAMPLE_COUNT; i++) {
    samples[i] = core.calBuffer[i];
    sum += samples[i];
  }
  float mean = sum / ANOMALY_CALIBRATION_SAMPLE_COUNT;

  float varSum = 0.0f;
  for (uint16_t i = 0; i < ANOMALY_CALIBRATION_SAMPLE_COUNT; i++) {
    float d = samples[i] - mean;
    varSum += d * d;
  }
  float variance = varSum / ANOMALY_CALIBRATION_SAMPLE_COUNT;

  if (variance > ANOMALY_MAX_CALIBRATION_VARIANCE && !forceAccept) {
    core.calCount = 0;
    core.warmupRetryCount++;
    return false;
  }

  float forMedian[ANOMALY_CALIBRATION_SAMPLE_COUNT];
  for (uint16_t i = 0; i < ANOMALY_CALIBRATION_SAMPLE_COUNT; i++) forMedian[i] = samples[i];
  float median = medianOf(forMedian, ANOMALY_CALIBRATION_SAMPLE_COUNT);

  float absDev[ANOMALY_CALIBRATION_SAMPLE_COUNT];
  for (uint16_t i = 0; i < ANOMALY_CALIBRATION_SAMPLE_COUNT; i++) {
    absDev[i] = fabsf(samples[i] - median);
  }
  float mad = medianOf(absDev, ANOMALY_CALIBRATION_SAMPLE_COUNT);
  if (mad < ANOMALY_MAD_FLOOR) mad = ANOMALY_MAD_FLOOR;

  core.median = median;
  core.mad = mad;
  core.calibrated = true;
  return true;
}

// Warmup-phase handling: accumulate one sample; once the buffer fills,
// attempt to finalize (retrying on unsafe variance, force-accepting past
// ANOMALY_CALIBRATION_MAX_RETRIES - see docs/decisions.md). Returns the
// resulting SensorState (WARMUP if still accumulating/retrying, NORMAL
// once a baseline is frozen).
SensorState advanceWarmup(SensorCore& core, float value) {
  if (core.calCount < ANOMALY_CALIBRATION_SAMPLE_COUNT) {
    core.calBuffer[core.calCount] = value;
    core.calCount++;
  }
  if (core.calCount < ANOMALY_CALIBRATION_SAMPLE_COUNT) {
    return SensorState::WARMUP;
  }

  bool forceAccept = core.warmupRetryCount >= ANOMALY_CALIBRATION_MAX_RETRIES;
  bool ok = tryFinalizeCalibration(core, forceAccept);
  if (ok) {
    resetStreaks(core);
    return SensorState::NORMAL;
  }
  return SensorState::WARMUP;  // buffer was reset for retry inside tryFinalizeCalibration
}

// Steady-state per-sample evaluation once a baseline exists: MAD-Z +
// flatline evidence, then the debounced state-machine transition (Part
// 5/6). `core.state` on entry is guaranteed to be NORMAL, ANOMALY, or
// FLATLINE - WARMUP/STALE/INVALID are handled by the caller before this is
// reached.
SensorState advanceSteadyState(SensorCore& core, float value) {
  float mad = core.mad < ANOMALY_MAD_FLOOR ? ANOMALY_MAD_FLOOR : core.mad;
  float modifiedZ = 0.6745f * fabsf(value - core.median) / mad;
  core.lastModifiedZ = modifiedZ;
  bool isAnomalousSample = modifiedZ > ANOMALY_MODIFIED_Z_THRESHOLD;

  bool isFlatSample = core.hasLastValue && fabsf(value - core.lastValue) < ANOMALY_FLATLINE_EPS;
  if (isFlatSample) {
    core.stuckCount++;
  } else {
    core.stuckCount = 0;
  }
  core.lastValue = value;
  core.hasLastValue = true;
  bool isFlatlined = core.stuckCount >= ANOMALY_STUCK_N;

  // FLATLINE takes priority over ANOMALY when both would otherwise fire -
  // see docs/decisions.md. Flatline's own persistence (ANOMALY_STUCK_N) is
  // already a sustained signal, so no extra entry debounce is needed here.
  if (isFlatlined) {
    core.anomalyStreak = 0;
    core.normalStreak = 0;
    core.flatlineClearStreak = 0;
    if (core.state != SensorState::FLATLINE) core.flatlineStartMs = 0;  // set for real by the caller, which has the timestamp
    return SensorState::FLATLINE;
  }

  if (core.state == SensorState::FLATLINE) {
    // Recovering from flatline requires ANOMALY_FLATLINE_RECOVERY_COUNT
    // consecutive non-flat samples (Part 6) - one changed sample alone
    // isn't enough.
    core.flatlineClearStreak++;
    if (core.flatlineClearStreak < ANOMALY_FLATLINE_RECOVERY_COUNT) {
      return SensorState::FLATLINE;
    }
    core.flatlineClearStreak = 0;
    core.anomalyStreak = 0;
    core.normalStreak = 0;
    // Falls through to normal/anomaly evaluation below using this same sample.
  }

  if (isAnomalousSample) {
    core.anomalyStreak++;
    if (core.state == SensorState::ANOMALY) {
      core.normalStreak = 0;
      return SensorState::ANOMALY;
    }
    if (core.anomalyStreak >= ANOMALY_CONSECUTIVE_COUNT) {
      core.anomalyStreak = 0;
      return SensorState::ANOMALY;
    }
    return SensorState::NORMAL;  // not yet enough consecutive evidence (Part 5 debounce)
  }

  core.anomalyStreak = 0;
  if (core.state == SensorState::ANOMALY) {
    core.normalStreak++;
    if (core.normalStreak >= ANOMALY_RECOVERY_COUNT) {
      core.normalStreak = 0;
      return SensorState::NORMAL;
    }
    return SensorState::ANOMALY;  // not yet enough consecutive recovery evidence (Part 6)
  }

  return SensorState::NORMAL;
}

}  // namespace

void init(SensorCore& core, uint8_t sensor_id) {
  core.sensor_id = sensor_id;
  core.state = SensorState::WARMUP;
  core.calibrated = false;
  core.calCount = 0;
  core.warmupRetryCount = 0;
  core.median = 0.0f;
  core.mad = ANOMALY_MAD_FLOOR;
  core.lastValue = 0.0f;
  core.hasLastValue = false;
  core.stuckCount = 0;
  core.flatlineStartMs = 0;
  resetStreaks(core);
  core.lastObservationMs = 0;
  core.everObserved = false;
  core.lastRawValue = 0.0f;
  core.lastModifiedZ = 0.0f;
}

EvalResult evaluate(SensorCore& core, const SensorObservation& obs) {
  SensorState prev = core.state;
  core.lastObservationMs = obs.timestamp_ms;
  core.everObserved = true;

  if (!obs.valid) {
    core.state = SensorState::INVALID;
    return EvalResult{ core.state, prev, prev != core.state, 0.0f };
  }

  core.lastRawValue = obs.value;

  if (!core.calibrated) {
    core.state = advanceWarmup(core, obs.value);
    return EvalResult{ core.state, prev, prev != core.state, 0.0f };
  }

  SensorState wasState = core.state;
  core.state = advanceSteadyState(core, obs.value);
  if (core.state == SensorState::FLATLINE && wasState != SensorState::FLATLINE) {
    core.flatlineStartMs = obs.timestamp_ms;
  }

  return EvalResult{ core.state, prev, prev != core.state, core.lastModifiedZ };
}

EvalResult tickStaleness(SensorCore& core, uint32_t now) {
  SensorState prev = core.state;
  bool isStale = core.everObserved && (now - core.lastObservationMs) > ANOMALY_STALE_TIMEOUT_MS;

  if (isStale && core.state != SensorState::STALE) {
    core.state = SensorState::STALE;
    return EvalResult{ core.state, prev, true, 0.0f };
  }
  return EvalResult{ core.state, prev, false, core.lastModifiedZ };
}

SensorTelemetry snapshot(const SensorCore& core) {
  SensorTelemetry t{};
  t.sensor_id = core.sensor_id;
  t.timestamp_ms = core.lastObservationMs;
  t.raw_value = core.lastRawValue;
  t.median = core.median;
  t.mad = core.mad;
  t.modified_z = core.lastModifiedZ;
  t.anomaly_threshold = ANOMALY_MODIFIED_Z_THRESHOLD;
  t.flatline_active = (core.state == SensorState::FLATLINE);
  t.flatline_duration_ms = t.flatline_active ? (core.lastObservationMs - core.flatlineStartMs) : 0;
  t.state = core.state;
  t.valid = (core.state != SensorState::INVALID);
  return t;
}

}  // namespace anomaly_core
