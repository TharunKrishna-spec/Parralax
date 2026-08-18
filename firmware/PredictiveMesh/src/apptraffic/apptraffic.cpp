#include "apptraffic.h"
#include "apptraffic_core.h"
#include "../config.h"
#include "../core/logger.h"
#include "../core/node_id.h"
#include "../anomaly/anomaly.h"
#include "../reliability/reliability.h"
#include "../suppression/suppression.h"
#include <Arduino.h>

namespace {

apptraffic_core::State g_state;
uint32_t g_lastSendMs = 0;

// Drains any bytes waiting on Serial, watching for the single-character
// priority trigger. Not a command protocol — no existing inbound-command
// mechanism exists anywhere in this project to reuse (verified before
// implementing this — see docs/decisions.md) — just one recognized byte;
// everything else (including a serial-monitor's trailing newline, or any
// telemetry-unrelated noise) is silently ignored. Only ever called on
// NODE_A (see tick()); reading Serial input here never interferes with
// telemetry's own Serial.println() output — one is RX, the other TX.
void drainPriorityTrigger() {
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c == 'p' || c == 'P') {
      apptraffic_core::requestPriority(g_state);
      logger::info("[APPTRAFFIC] priority trigger received - next packet will be PRIORITY");
    }
  }
}

void sendOne(uint32_t now) {
  apptraffic_core::SendDecision decision = apptraffic_core::buildSendDecision(g_state);
  uint16_t appSeq = apptraffic_core::nextAppSeq(g_state);

  // Reuses Phase 3's already-existing, already-sampled POT/LDR readings
  // (anomaly::sample() runs every SENSOR_SAMPLE_INTERVAL_MS from
  // main.cpp's loop) rather than a second/duplicate analogRead() path —
  // see docs/decisions.md.
  anomaly_core::SensorTelemetry pot = anomaly::getTelemetry(anomaly::SensorId::POT);
  anomaly_core::SensorTelemetry ldr = anomaly::getTelemetry(anomaly::SensorId::LDR);

  uint8_t payload[apptraffic_core::DATA_WIRE_SIZE];
  uint8_t len = apptraffic_core::encodeData(appSeq, static_cast<uint16_t>(pot.raw_value),
                                             static_cast<uint16_t>(ldr.raw_value), now, payload, sizeof(payload));
  if (len == 0) {
    logger::warn("[APPTRAFFIC] encodeData failed - payload not sent (should never happen, payload comfortably fits)");
    return;
  }

  // Priority-broadcast milestone (2026-08-18): PRIORITY traffic now goes
  // through the opportunistic broadcast + overhear + RSSI-aware
  // suppression path (src/suppression/) instead of reliability::send()'s
  // forced-unicast override - a deliberate, user-confirmed replacement,
  // not an addition alongside it (see docs/decisions.md). NORMAL traffic
  // is completely unchanged: still reliability::send(), still unicast,
  // still ACK/retry/PDR exactly as before.
  bool inFlight = decision.priority ? suppression::broadcastPriority(payload, len)
                                     : reliability::send(decision.destination, payload, len, false);
  logger::info("[APPTRAFFIC] TX appSeq=%u dest=%s class=%s pot=%u ldr=%u in_flight=%d",
               static_cast<unsigned>(appSeq), nodeName(decision.destination),
               decision.priority ? "PRIORITY" : "NORMAL", static_cast<unsigned>(pot.raw_value),
               static_cast<unsigned>(ldr.raw_value), inFlight ? 1 : 0);
}

}  // namespace

namespace apptraffic {

void init() {
  apptraffic_core::init(g_state);
  g_lastSendMs = 0;
  if (THIS_NODE_ID == NODE_A) {
    logger::info("apptraffic: init (NODE_A -> NODE_S demo workload, Phase 7, interval=%ums)",
                 static_cast<unsigned>(APPLICATION_TX_INTERVAL_MS));
  }
}

void tick() {
  if (THIS_NODE_ID != NODE_A) return;  // only the primary source originates application traffic (Part "primary source NODE_A")

  drainPriorityTrigger();

  uint32_t now = millis();
  if (now - g_lastSendMs >= APPLICATION_TX_INTERVAL_MS) {
    g_lastSendMs = now;
    sendOne(now);
  }
}

}  // namespace apptraffic
