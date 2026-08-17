#include "anomaly.h"
#include "../config.h"
#include "../core/logger.h"
#include <Arduino.h>

namespace anomaly {

namespace {

anomaly_core::SensorCore g_core[static_cast<uint8_t>(SensorId::SENSOR_COUNT)];
AnomalyEventCallback g_eventCallback = nullptr;

const char* sensorName(SensorId sensor) {
  return sensor == SensorId::POT ? "POT" : "LDR";
}

uint8_t sensorPin(SensorId sensor) {
  return sensor == SensorId::POT ? PIN_SENSOR_POT : PIN_SENSOR_LDR;
}

const char* stateName(anomaly_core::SensorState state) {
  switch (state) {
    case anomaly_core::SensorState::WARMUP:   return "WARMUP";
    case anomaly_core::SensorState::NORMAL:   return "NORMAL";
    case anomaly_core::SensorState::ANOMALY:  return "ANOMALY";
    case anomaly_core::SensorState::FLATLINE: return "FLATLINE";
    case anomaly_core::SensorState::STALE:    return "STALE";
    case anomaly_core::SensorState::INVALID:  return "INVALID";
    default:                                  return "?";
  }
}

// Translates a state-machine transition into the Part 8 event set. Never
// fires on WARMUP entry/exit-to-NORMAL (calibration completing isn't a
// health event) and only fires SENSOR_RECOVERED when the *previous* state
// was a real degraded one (ANOMALY/FLATLINE/STALE/INVALID) - not merely
// "calibration finished."
void fireTransitionEvent(SensorId sensor, const anomaly_core::EvalResult& r, const anomaly_core::SensorTelemetry& telemetry) {
  if (!r.changed || g_eventCallback == nullptr) return;

  using S = anomaly_core::SensorState;
  AnomalyEventType type;
  switch (r.state) {
    case S::ANOMALY:  type = AnomalyEventType::SENSOR_ANOMALY;  break;
    case S::FLATLINE: type = AnomalyEventType::SENSOR_FLATLINE; break;
    case S::STALE:    type = AnomalyEventType::SENSOR_STALE;    break;
    case S::INVALID:  type = AnomalyEventType::SENSOR_INVALID;  break;
    case S::NORMAL: {
      bool realRecovery = r.previousState == S::ANOMALY || r.previousState == S::FLATLINE ||
                           r.previousState == S::STALE || r.previousState == S::INVALID;
      if (!realRecovery) return;  // WARMUP -> NORMAL is calibration completing, not a health event
      type = AnomalyEventType::SENSOR_RECOVERED;
      break;
    }
    default:
      return;  // WARMUP entry - not a Part 8 event
  }

  AnomalyEvent evt{ type, sensor, telemetry };
  g_eventCallback(evt);
}

// Blocking boot calibration for one sensor - implementation-guide.html's
// boot-sequence diagram: buffer ANOMALY_CALIBRATION_SAMPLE_COUNT samples,
// check the variance safety envelope, restart on failure. The bounded
// retry / force-accept fallback lives inside anomaly_core itself
// (SensorCore::warmupRetryCount), so this loop simply keeps feeding real
// samples until the core reports it's left WARMUP.
void calibrateSensor(SensorId sensor) {
  anomaly_core::SensorCore& core = g_core[static_cast<uint8_t>(sensor)];
  uint8_t pin = sensorPin(sensor);

  anomaly_core::init(core, static_cast<uint8_t>(sensor));

  while (core.state == anomaly_core::SensorState::WARMUP) {
    anomaly_core::SensorObservation obs{ static_cast<uint8_t>(sensor), millis(), static_cast<float>(analogRead(pin)), true };
    anomaly_core::evaluate(core, obs);
    if (core.state == anomaly_core::SensorState::WARMUP) {
      delay(ANOMALY_CALIBRATION_SAMPLE_INTERVAL_MS);
    }
  }

  anomaly_core::SensorTelemetry t = anomaly_core::snapshot(core);
  if (core.warmupRetryCount >= ANOMALY_CALIBRATION_MAX_RETRIES) {
    logger::error("anomaly: %s calibration retries exhausted (%u attempts) - forced accept, baseline may be unreliable (median=%.1f mad=%.1f)",
                  sensorName(sensor), static_cast<unsigned>(core.warmupRetryCount), t.median, t.mad);
  } else {
    logger::info("anomaly: %s calibrated (median=%.1f mad=%.1f, %u restart(s))",
                 sensorName(sensor), t.median, t.mad, static_cast<unsigned>(core.warmupRetryCount));
  }
}

}  // namespace

void init() {
  analogReadResolution(12);  // explicit, not relied on as an implicit core default - see config.h
  pinMode(PIN_SENSOR_POT, INPUT);
  pinMode(PIN_SENSOR_LDR, INPUT);

  logger::info("anomaly: init (boot calibration starting - median/MAD + flatline detector, Phase 3)");
  calibrateSensor(SensorId::POT);
  calibrateSensor(SensorId::LDR);
  logger::info("anomaly: boot calibration complete for both sensors");
}

anomaly_core::SensorTelemetry sample(SensorId sensor) {
  uint8_t idx = static_cast<uint8_t>(sensor);
  anomaly_core::SensorCore& core = g_core[idx];

  anomaly_core::SensorObservation obs{ static_cast<uint8_t>(sensor), millis(), static_cast<float>(analogRead(sensorPin(sensor))), true };
  anomaly_core::EvalResult r = anomaly_core::evaluate(core, obs);
  anomaly_core::SensorTelemetry t = anomaly_core::snapshot(core);

  logger::debug("[ANOMALY] sensor=%s raw=%.0f state=%s modified_z=%.2f flatline_ms=%u",
                sensorName(sensor), t.raw_value, stateName(t.state), t.modified_z,
                static_cast<unsigned>(t.flatline_duration_ms));

  if (r.changed) {
    logger::warn("[ANOMALY] sensor=%s state %s -> %s", sensorName(sensor), stateName(r.previousState), stateName(r.state));
  }

  fireTransitionEvent(sensor, r, t);
  return t;
}

void tick() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < static_cast<uint8_t>(SensorId::SENSOR_COUNT); i++) {
    SensorId sensor = static_cast<SensorId>(i);
    anomaly_core::EvalResult r = anomaly_core::tickStaleness(g_core[i], now);
    if (r.changed) {
      anomaly_core::SensorTelemetry t = anomaly_core::snapshot(g_core[i]);
      logger::warn("[ANOMALY] sensor=%s gone stale (no observation for > %u ms)",
                    sensorName(sensor), static_cast<unsigned>(ANOMALY_STALE_TIMEOUT_MS));
      fireTransitionEvent(sensor, r, t);
    }
  }
}

anomaly_core::SensorTelemetry getTelemetry(SensorId sensor) {
  return anomaly_core::snapshot(g_core[static_cast<uint8_t>(sensor)]);
}

void setEventCallback(AnomalyEventCallback cb) {
  g_eventCallback = cb;
}

}  // namespace anomaly
