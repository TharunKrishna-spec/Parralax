#pragma once
#include <stdint.h>
#include "anomaly_core.h"

// ============================================================
// Anomaly / sensor-health engine (implementation-guide.html §5.2) -
// Arduino-facing adapter, Phase 3.
//
// Owns two anomaly_core::SensorCore instances (POT, LDR), performs the
// blocking boot-time calibration implementation-guide.html's boot-sequence
// diagram describes, evaluates real analogRead() samples against it during
// app::loop(), and drives the independent staleness check from tick().
// See docs/decisions.md for the deliberate Phase 3 scope boundary:
// detection + local telemetry/events only - no OLED wiring, no assumed
// wire-protocol/GUI contract (none exists in this repo to match against).
// ============================================================

namespace anomaly {

enum class SensorId : uint8_t { POT, LDR, SENSOR_COUNT };

// Part 8 event types.
enum class AnomalyEventType : uint8_t {
  SENSOR_ANOMALY,
  SENSOR_FLATLINE,
  SENSOR_RECOVERED,
  SENSOR_STALE,
  SENSOR_INVALID
};

struct AnomalyEvent {
  AnomalyEventType type;
  SensorId sensor;
  anomaly_core::SensorTelemetry telemetry;
};

typedef void (*AnomalyEventCallback)(const AnomalyEvent& event);

// Performs blocking boot-time calibration for both sensors (~1s each in
// the common case - see ANOMALY_CALIBRATION_SAMPLE_INTERVAL_MS), matching
// implementation-guide.html's boot-sequence diagram: buffer samples, check
// the variance safety envelope, restart on failure (bounded by
// ANOMALY_CALIBRATION_MAX_RETRIES), then freeze median/MAD.
void init();

// Reads the real ADC pin for `sensor`, evaluates it, logs the evidence,
// fires an event on any state transition, and returns the resulting
// telemetry snapshot (Part 7). Called from app::loop() at
// SENSOR_SAMPLE_INTERVAL_MS.
anomaly_core::SensorTelemetry sample(SensorId sensor);

// Drives the independent staleness check (Part 4 STALE) for both sensors.
// Call once per app::loop() iteration, alongside routing::tick()/
// predictor::tick().
void tick();

// Read-only telemetry accessor (Part 7), independent of sample() - lets a
// caller (e.g. a future reporting layer) read the latest snapshot without
// forcing a new ADC read.
anomaly_core::SensorTelemetry getTelemetry(SensorId sensor);

void setEventCallback(AnomalyEventCallback cb);

}  // namespace anomaly
