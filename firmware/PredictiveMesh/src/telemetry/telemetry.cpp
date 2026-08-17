#include "telemetry.h"
#include "telemetry_core.h"
#include "../config.h"
#include "../core/logger.h"
#include <Arduino.h>
#include <esp_random.h>
#include <stdio.h>

namespace {

char g_bootId[16] = "";
uint32_t g_seq = 0;
char g_macStr[18] = "";
bool g_haveMac = false;
char g_line[telemetry_core::LINE_BUF_SIZE];

// Cache of the last-reported active route per destination, so a real
// ROUTE_CHANGED event can report genuine old-vs-new values (Part J's
// ROUTE_CHANGE detail example: oldHops/newHops/oldScore/newScore) instead
// of only ever knowing the new state. Real, derived, adapter-local state —
// not a second source of truth for routing (routing_core's own table
// remains authoritative; this only remembers what telemetry itself last
// told the GUI).
struct CachedRoute {
  bool valid;
  NodeId nextHop;
  uint8_t hopCount;
  float score;
};
CachedRoute g_lastRoute[NODE_ID_COUNT];

telemetry_core::Envelope envelope(uint32_t now) {
  telemetry_core::Envelope e{ thisNode().name, g_bootId, g_seq, now };
  g_seq++;
  return e;
}

void emit(size_t len) {
  if (len == 0) {
    logger::warn("[TELEMETRY] a message did not fit in the line buffer - refused, not sent truncated");
    return;
  }
  Serial.println(g_line);
}

// Builds a small JSON object literal into `out` for an EVENT's `details`
// field. Adapter-local glue (not part of telemetry_core - see
// docs/decisions.md for why each differently-shaped `details` object is
// built here rather than telemetry_core growing one struct per eventType).
void emitEvent(uint32_t now, const char* eventType, const char* severity, const char* source, const char* detailsJson) {
  telemetry_core::EventPayload p{ eventType, severity, source, detailsJson };
  emit(telemetry_core::buildEvent(envelope(now), p, g_line, sizeof(g_line)));
}

void emitRouteUpdateFor(uint32_t now, NodeId destination, NodeId activeNextHop, uint8_t activeHopCount,
                        bool priority) {
  if (activeNextHop == NODE_ID_UNKNOWN) return;  // no valid route to report - see docs/known-issues.md

  routing_core::CandidateInfo raw[NODE_ID_COUNT];
  uint8_t n = routing::getCandidates(destination, raw, NODE_ID_COUNT);

  telemetry_core::RouteEntry candidates[NODE_ID_COUNT];
  for (uint8_t i = 0; i < n; i++) {
    candidates[i].hops[0] = thisNode().name;
    candidates[i].hops[1] = nodeName(raw[i].nextHop);
    candidates[i].hopCount = raw[i].hopCount;
    candidates[i].score = predictor::linkScore(raw[i].nextHop);
    candidates[i].state = (raw[i].nextHop == activeNextHop) ? "ACTIVE" : "BACKUP";
  }

  telemetry_core::RouteUpdatePayload p{};
  p.destination = nodeName(destination);
  p.active.hops[0] = thisNode().name;
  p.active.hops[1] = nodeName(activeNextHop);
  p.active.hopCount = activeHopCount;
  p.active.score = predictor::linkScore(activeNextHop);
  p.active.state = "ACTIVE";
  p.candidates = candidates;
  p.candidateCount = n;
  p.trafficClass = priority ? "PRIORITY" : "NORMAL";
  p.reason = telemetry_core::routeReasonStr(priority, false);
  emit(telemetry_core::buildRouteUpdate(envelope(now), p, g_line, sizeof(g_line)));
}

}  // namespace

namespace telemetry {

void init(const uint8_t mac[6]) {
  snprintf(g_bootId, sizeof(g_bootId), "%s-%08x", thisNode().name, static_cast<unsigned>(esp_random()));
  g_seq = 0;
  for (uint8_t i = 0; i < NODE_ID_COUNT; i++) g_lastRoute[i].valid = false;

  if (mac != nullptr) {
    logger::macToStr(mac, g_macStr);
    g_haveMac = true;
  }

  logger::info("telemetry: init (mesh-json/v1 serialization, Phase 6, bootId=%s)", g_bootId);

  uint32_t now = millis();
  telemetry_core::HelloPayload h{};
  h.nodeName = thisNode().name;
  h.role = telemetry_core::roleStr(thisNode().role);
  h.mac = g_haveMac ? g_macStr : nullptr;
  h.firmwareVersion = FIRMWARE_VERSION;
  h.heartbeatIntervalMs = TELEMETRY_HEARTBEAT_INTERVAL_MS;
  h.offlineTimeoutMs = TELEMETRY_OFFLINE_TIMEOUT_MS;
  h.routeTimeoutMs = ROUTING_ENTRY_TIMEOUT_MS;
  h.tLow = PREDICTOR_HYSTERESIS_T_LOW;
  h.tHigh = PREDICTOR_HYSTERESIS_T_HIGH;
  h.ewmaAlpha = PREDICTOR_RSSI_EWMA_ALPHA;  // the more prominent of two real EWMA alphas - see docs/decisions.md
  h.linkRateHz = 1000.0f / TELEMETRY_LINK_INTERVAL_MS;
  h.predictionRateHz = 1000.0f / TELEMETRY_PREDICTION_INTERVAL_MS;
  h.statisticsRateHz = 1000.0f / TELEMETRY_STATISTICS_INTERVAL_MS;
  emit(telemetry_core::buildHello(envelope(now), h, g_line, sizeof(g_line)));
}

void tick() {
  uint32_t now = millis();

  static uint32_t lastHeartbeat = 0;
  if (now - lastHeartbeat >= TELEMETRY_HEARTBEAT_INTERVAL_MS) {
    lastHeartbeat = now;
    emit(telemetry_core::buildHeartbeat(envelope(now), now, g_line, sizeof(g_line)));
  }

  static uint32_t lastNodeStatus = 0;
  if (now - lastNodeStatus >= TELEMETRY_NODE_STATUS_INTERVAL_MS) {
    lastNodeStatus = now;
    telemetry_core::NodeStatusPayload p{};
    p.status = "ONLINE";  // a node can only self-report while running - see docs/decisions.md
    p.nodeName = thisNode().name;
    p.role = telemetry_core::roleStr(thisNode().role);
    p.uptimeMs = now;
    p.firmwareVersion = FIRMWARE_VERSION;
    p.reason = nullptr;
    emit(telemetry_core::buildNodeStatus(envelope(now), p, g_line, sizeof(g_line)));
  }

  static uint32_t lastLink = 0;
  if (now - lastLink >= TELEMETRY_LINK_INTERVAL_MS) {
    lastLink = now;
    uint8_t count = 0;
    const NodeId* neighbors = neighborsOf(THIS_NODE_ID, count);
    for (uint8_t i = 0; i < count; i++) {
      NodeId nb = neighbors[i];
      const predictor_core::NeighborLinkState& n = predictor::linkState(nb);
      telemetry_core::LinkClass cls =
          telemetry_core::classifyLink(n.everObserved, n.stale, !predictor::isUnhealthy(nb), n.belowCount, n.aboveCount);
      telemetry_core::LinkUpdatePayload p{};
      p.from = thisNode().name;
      p.to = nodeName(nb);
      p.rssiDbm = n.latestRssi;
      p.rssiEwmaDbm = n.ewmaRssi;
      p.rssiSlopeDbPerSec = n.slope;
      p.pdr = n.pdrEwma;
      p.pdrEwma = n.pdrEwma;
      p.stalenessMs = static_cast<uint32_t>(now - n.lastUpdateMs);
      p.linkScore = n.linkScore;
      p.state = telemetry_core::linkStateStr(cls);
      emit(telemetry_core::buildLinkUpdate(envelope(now), p, g_line, sizeof(g_line)));
    }
  }

  static uint32_t lastPrediction = 0;
  if (now - lastPrediction >= TELEMETRY_PREDICTION_INTERVAL_MS) {
    lastPrediction = now;
    uint8_t count = 0;
    const NodeId* neighbors = neighborsOf(THIS_NODE_ID, count);
    for (uint8_t i = 0; i < count; i++) {
      NodeId nb = neighbors[i];
      const predictor_core::NeighborLinkState& n = predictor::linkState(nb);
      telemetry_core::LinkClass cls =
          telemetry_core::classifyLink(n.everObserved, n.stale, !predictor::isUnhealthy(nb), n.belowCount, n.aboveCount);
      telemetry_core::PredictionPayload p{};
      p.neighborId = nodeName(nb);
      p.rssiDbm = n.latestRssi;
      p.rssiEwmaDbm = n.ewmaRssi;
      p.rssiSlopeDbPerSec = n.slope;
      p.pdr = n.pdrEwma;
      p.pdrEwma = n.pdrEwma;
      p.stalenessMs = static_cast<uint32_t>(now - n.lastUpdateMs);
      p.linkScore = n.linkScore;
      p.predictionState = telemetry_core::predictionStateStr(cls);
      p.tLow = PREDICTOR_HYSTERESIS_T_LOW;
      p.tHigh = PREDICTOR_HYSTERESIS_T_HIGH;
      p.hysteresisState = telemetry_core::hysteresisStateStr(n.linkScore, PREDICTOR_HYSTERESIS_T_LOW, PREDICTOR_HYSTERESIS_T_HIGH);
      emit(telemetry_core::buildPrediction(envelope(now), p, g_line, sizeof(g_line)));
    }
  }

  static uint32_t lastSensor = 0;
  if (now - lastSensor >= TELEMETRY_SENSOR_INTERVAL_MS) {
    lastSensor = now;
    const struct { anomaly::SensorId id; const char* sensorId; const char* sensorType; } sensors[] = {
      { anomaly::SensorId::POT, "pot", "potentiometer" },
      { anomaly::SensorId::LDR, "ldr", "photoresistor" },
    };
    for (const auto& s : sensors) {
      anomaly_core::SensorTelemetry t = anomaly::getTelemetry(s.id);
      telemetry_core::SensorStatusPayload p{};
      p.sensorId = s.sensorId;
      p.sensorType = s.sensorType;
      p.value = t.raw_value;
      p.valueValid = t.valid;
      p.healthState = telemetry_core::sensorHealthStr(static_cast<uint8_t>(t.state));
      p.durationMs = t.flatline_duration_ms;
      p.hasDurationMs = t.flatline_active;
      p.rawValue = t.raw_value;
      p.baseline = t.median;
      p.mad = t.mad;
      p.zScore = t.modified_z;
      p.threshold = t.anomaly_threshold;
      emit(telemetry_core::buildSensorStatus(envelope(now), p, g_line, sizeof(g_line)));
    }
  }

  static uint32_t lastStatistics = 0;
  if (now - lastStatistics >= TELEMETRY_STATISTICS_INTERVAL_MS) {
    lastStatistics = now;
    reliability_core::Statistics s = reliability::getStatistics();
    telemetry_core::StatisticsPayload p{};
    p.windowMs = TELEMETRY_STATISTICS_INTERVAL_MS;
    p.pdr = (s.packetsSent > 0) ? (static_cast<float>(s.packetsDelivered) / static_cast<float>(s.packetsSent)) : 1.0f;
    p.packetsTransmitted = s.packetsSent;
    p.packetsAcknowledged = s.acknowledgements;
    p.packetsDropped = s.packetsFailed;
    p.retryCount = s.retries;
    p.duplicateCount = s.duplicatesDropped;
    p.endToEndLatencyMs = static_cast<float>(s.lastLatencyMs);  // per-hop latency, not true end-to-end - see docs/decisions.md
    emit(telemetry_core::buildStatistics(envelope(now), p, g_line, sizeof(g_line)));
  }
}

void onRouteEvent(const routing::RouteEvent& evt) {
  uint32_t now = millis();

  if (evt.type == routing::RouteEventType::ROUTE_SELECTED) {
    if (evt.priority) {
      char details[96];
      snprintf(details, sizeof(details), "{\"destination\":\"%s\",\"nextHop\":\"%s\",\"hopCount\":%u}",
               nodeName(evt.destination), evt.next_hop == NODE_ID_UNKNOWN ? "NONE" : nodeName(evt.next_hop),
               static_cast<unsigned>(evt.hop_count));
      emitEvent(now, "PRIORITY_ROUTE", "INFO", thisNode().name, details);
    }
    return;  // not a table-change event - no ROUTE_UPDATE for every decision query
  }

  CachedRoute& cached = g_lastRoute[evt.destination];
  char oldHop[2] = "?";
  float oldScore = 0.0f;
  bool hadOld = cached.valid;
  if (hadOld) {
    oldHop[0] = nodeName(cached.nextHop)[0];
    oldHop[1] = '\0';
    oldScore = cached.score;
  }

  if (evt.type == routing::RouteEventType::ROUTE_INVALIDATED) {
    if (hadOld) {
      char details[192];
      snprintf(details, sizeof(details),
               "{\"oldHops\":[\"%s\",\"%s\"],\"newHops\":[],\"reason\":\"ROUTE_EXPIRED\",\"oldScore\":%.2f,\"newScore\":0.00}",
               thisNode().name, oldHop, oldScore);
      emitEvent(now, "ROUTE_CHANGE", "WARN", thisNode().name, details);
    }
    cached.valid = false;
    return;
  }

  // ROUTE_CHANGED: a genuine table mutation. Build ROUTE_UPDATE first (Part
  // J), then the EVENT with real old-vs-new values, then update the cache.
  float newScore = predictor::linkScore(evt.next_hop);
  emitRouteUpdateFor(now, evt.destination, evt.next_hop, evt.hop_count, false);

  if (hadOld && cached.nextHop != evt.next_hop) {
    char details[192];
    snprintf(details, sizeof(details),
             "{\"oldHops\":[\"%s\",\"%s\"],\"newHops\":[\"%s\",\"%s\"],\"reason\":\"UNKNOWN\",\"oldScore\":%.2f,\"newScore\":%.2f}",
             thisNode().name, oldHop, thisNode().name, nodeName(evt.next_hop), oldScore, newScore);
    emitEvent(now, "ROUTE_CHANGE", "INFO", thisNode().name, details);
  }

  cached.valid = true;
  cached.nextHop = evt.next_hop;
  cached.hopCount = evt.hop_count;
  cached.score = newScore;
}

void onLinkEvent(const predictor::LinkEvent& evt) {
  uint32_t now = millis();
  char details[48];
  snprintf(details, sizeof(details), "{\"score\":%.2f}", evt.score);

  if (evt.type == predictor::LinkEventType::LINK_DEGRADING) {
    emitEvent(now, "LINK_DEGRADING", "WARN", nodeName(evt.neighbor), details);
  } else if (evt.type == predictor::LinkEventType::LINK_UNHEALTHY) {
    emitEvent(now, "LINK_FAILURE", "ERROR", nodeName(evt.neighbor), details);
  }
  // LINK_SCORE_UPDATED / LINK_RECOVERED: no discrete EVENT - already
  // visible via LINK_UPDATE/PREDICTION's own state field. See docs/decisions.md.
}

void onAnomalyEvent(const anomaly::AnomalyEvent& evt) {
  uint32_t now = millis();
  const char* sensorName = evt.sensor == anomaly::SensorId::POT ? "pot" : "ldr";

  if (evt.type == anomaly::AnomalyEventType::SENSOR_ANOMALY) {
    char details[64];
    snprintf(details, sizeof(details), "{\"modifiedZ\":%.2f,\"threshold\":%.2f}", evt.telemetry.modified_z,
             evt.telemetry.anomaly_threshold);
    emitEvent(now, "SENSOR_ANOMALY", "WARN", sensorName, details);
  } else if (evt.type == anomaly::AnomalyEventType::SENSOR_FLATLINE) {
    char details[64];
    snprintf(details, sizeof(details), "{\"reason\":\"FLATLINE\",\"durationMs\":%lu}",
             static_cast<unsigned long>(evt.telemetry.flatline_duration_ms));
    emitEvent(now, "SENSOR_FAILURE", "ERROR", sensorName, details);
  } else if (evt.type == anomaly::AnomalyEventType::SENSOR_STALE) {
    emitEvent(now, "SENSOR_FAILURE", "ERROR", sensorName, "{\"reason\":\"STALE\"}");
  } else if (evt.type == anomaly::AnomalyEventType::SENSOR_INVALID) {
    emitEvent(now, "SENSOR_FAILURE", "ERROR", sensorName, "{\"reason\":\"INVALID\"}");
  }
  // SENSOR_RECOVERED: no discrete EVENT - already visible via
  // SENSOR_STATUS's healthState returning to NORMAL. See docs/decisions.md.
}

void onReliabilityEvent(const reliability::ReliabilityEvent& evt) {
  uint32_t now = millis();
  char details[96];
  snprintf(details, sizeof(details), "{\"sequence\":%u,\"neighbor\":\"%s\",\"attemptCount\":%u}",
           static_cast<unsigned>(evt.sequence), nodeName(evt.neighbor), static_cast<unsigned>(evt.attemptCount));

  if (evt.type == reliability::ReliabilityEventType::PACKET_RETRY) {
    emitEvent(now, "PACKET_RETRY", "WARN", nodeName(evt.source), details);
  } else if (evt.type == reliability::ReliabilityEventType::PACKET_DROP) {
    emitEvent(now, "PACKET_DROP", "ERROR", nodeName(evt.source), details);
  }
  // PACKET_TX/PACKET_ACK/PACKET_DELIVERED/PACKET_RECEIVED/DUPLICATE_DROPPED:
  // no discrete EVENT - already covered by periodic STATISTICS aggregate
  // counters, matching the contract's own EVENT-vs-STATISTICS division of
  // concerns. See docs/decisions.md.
}

void reportError(const char* code, const char* message, bool recoverable) {
  uint32_t now = millis();
  telemetry_core::ErrorPayload p{ "ERROR", code, message, recoverable };
  emit(telemetry_core::buildError(envelope(now), p, g_line, sizeof(g_line)));
}

}  // namespace telemetry
