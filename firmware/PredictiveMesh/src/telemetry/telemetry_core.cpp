#include "telemetry_core.h"
#include <stdio.h>
#include <stdarg.h>

namespace telemetry_core {

namespace {

// Minimal append-only JSON line writer. Every append is bounds-checked
// against the caller's buffer; once a write would truncate, `ok` latches
// false and every subsequent append becomes a no-op — the builder functions
// below check `ok` exactly once at the end and return 0 (refuse to emit a
// truncated/invalid line) rather than ever handing the caller a partial
// JSON string. See telemetry_core.h's file header.
struct Writer {
  char* buf;
  size_t cap;
  size_t pos;
  bool ok;
};

void wInit(Writer& w, char* buf, size_t cap) {
  w.buf = buf;
  w.cap = cap;
  w.pos = 0;
  w.ok = (buf != nullptr && cap > 0);
  if (w.ok) w.buf[0] = '\0';
}

void wPrintf(Writer& w, const char* fmt, ...) {
  if (!w.ok) return;
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(w.buf + w.pos, w.cap - w.pos, fmt, args);
  va_end(args);
  if (n < 0 || static_cast<size_t>(n) >= (w.cap - w.pos)) {
    w.ok = false;
    return;
  }
  w.pos += static_cast<size_t>(n);
}

// Every string field this module ever writes is a fixed literal chosen by
// firmware itself (see telemetry_core.h's file header) — this only guards
// against a null pointer being passed for an optional field, never against
// untrusted content.
const char* orEmpty(const char* s) {
  return s != nullptr ? s : "";
}

void wEnvelopeOpen(Writer& w, const Envelope& env, const char* type) {
  wPrintf(w, "{\"protocolVersion\":\"mesh-json/v1\",\"type\":\"%s\",\"nodeId\":\"%s\",\"bootId\":\"%s\","
             "\"seq\":%lu,\"timestampMs\":%lu,\"payload\":",
          type, orEmpty(env.nodeId), orEmpty(env.bootId), static_cast<unsigned long>(env.seq),
          static_cast<unsigned long>(env.timestampMs));
}

// Writes a JSON string array of `len` canonical node-letter strings —
// shared by ROUTE_UPDATE's `active.hops`/`candidates[].hops` (Phase 7.1,
// red-team Finding 5) so both use exactly the same array-formatting logic.
void wHopsArray(Writer& w, const char* const* hops, uint8_t len) {
  wPrintf(w, "[");
  for (uint8_t i = 0; i < len; i++) {
    wPrintf(w, "%s\"%s\"", i == 0 ? "" : ",", orEmpty(hops[i]));
  }
  wPrintf(w, "]");
}

// hopCount is deliberately derived here, never accepted as a separately-
// trusted field — see telemetry_core.h's RouteEntry comment for why this
// is what makes the contract's `hopCount == hops.length - 1` invariant
// impossible to violate by construction.
uint8_t derivedHopCount(uint8_t hopsLen) {
  return hopsLen > 0 ? static_cast<uint8_t>(hopsLen - 1) : 0;
}

size_t finish(Writer& w) {
  if (!w.ok) return 0;
  wPrintf(w, "}");  // closes the envelope object opened by wEnvelopeOpen
  return w.ok ? w.pos : 0;
}

}  // namespace

// ---- Part K enum classification/mapping ----

LinkClass classifyLink(bool everObserved, bool stale, bool healthy, uint8_t belowCount, uint8_t aboveCount) {
  if (!everObserved) return LinkClass::UNKNOWN_C;
  if (stale) return LinkClass::STALE_C;
  if (!healthy && aboveCount > 0) return LinkClass::RECOVERING_C;
  if (!healthy) return LinkClass::UNHEALTHY_C;
  if (healthy && belowCount > 0) return LinkClass::DEGRADING_C;
  return LinkClass::HEALTHY_C;
}

const char* linkStateStr(LinkClass c) {
  switch (c) {
    case LinkClass::UNKNOWN_C:    return "UNKNOWN";
    case LinkClass::HEALTHY_C:    return "HEALTHY";
    case LinkClass::DEGRADING_C:  return "DEGRADING";
    case LinkClass::UNHEALTHY_C:  return "UNHEALTHY";
    case LinkClass::RECOVERING_C: return "RECOVERING";
    case LinkClass::STALE_C:      return "STALE";
    default:                       return "UNKNOWN";
  }
}

const char* predictionStateStr(LinkClass c) {
  switch (c) {
    case LinkClass::UNKNOWN_C:    return "UNKNOWN";
    case LinkClass::HEALTHY_C:    return "STABLE";
    case LinkClass::DEGRADING_C:  return "DEGRADING";
    case LinkClass::UNHEALTHY_C:  return "UNHEALTHY";
    case LinkClass::RECOVERING_C: return "RECOVERING";
    case LinkClass::STALE_C:      return "TIMEOUT";
    default:                       return "UNKNOWN";
  }
}

const char* hysteresisStateStr(float linkScore, float tLow, float tHigh) {
  if (linkScore < tLow) return "BELOW_LOW";
  if (linkScore >= tHigh) return "ABOVE_HIGH";
  return "BETWEEN_THRESHOLDS";
}

const char* roleStr(NodeRole role) {
  switch (role) {
    case ROLE_SOURCE: return "SOURCE";
    case ROLE_RELAY:  return "RELAY";
    case ROLE_SINK:   return "SINK";
    default:           return "RELAY";
  }
}

const char* routeReasonStr(RouteReason reason) {
  switch (reason) {
    case RouteReason::PRIORITY_OVERRIDE_R: return "PRIORITY_OVERRIDE";
    case RouteReason::ROUTE_EXPIRED_R:     return "ROUTE_EXPIRED";
    case RouteReason::LINK_DEGRADATION_R:  return "LINK_DEGRADATION";
    case RouteReason::ROUTE_RECOVERY_R:    return "ROUTE_RECOVERY";
    case RouteReason::UNKNOWN_R:           return "UNKNOWN";
    default:                                return "UNKNOWN";
  }
}

const char* sensorHealthStr(uint8_t anomalySensorState) {
  // Mirrors anomaly_core::SensorState's declaration order exactly
  // (WARMUP=0, NORMAL=1, ANOMALY=2, FLATLINE=3, STALE=4, INVALID=5) —
  // passed as uint8_t rather than the real enum so this module stays free
  // of any dependency on anomaly_core.h. See docs/decisions.md for the
  // WARMUP->SUSPECT / INVALID->OUT_OF_RANGE mapping rationale.
  switch (anomalySensorState) {
    case 0: return "SUSPECT";       // WARMUP
    case 1: return "NORMAL";        // NORMAL
    case 2: return "ANOMALY";       // ANOMALY
    case 3: return "FLATLINE";      // FLATLINE
    case 4: return "STALE";         // STALE
    case 5: return "OUT_OF_RANGE";  // INVALID
    default: return "SUSPECT";
  }
}

// ---- message builders ----

size_t buildHello(const Envelope& env, const HelloPayload& p, char* buf, size_t bufSize) {
  Writer w;
  wInit(w, buf, bufSize);
  wEnvelopeOpen(w, env, "HELLO");
  wPrintf(w,
          "{\"nodeName\":\"%s\",\"role\":\"%s\",", orEmpty(p.nodeName), orEmpty(p.role));
  if (p.mac != nullptr) {
    wPrintf(w, "\"mac\":\"%s\",", p.mac);
  }
  wPrintf(w,
          "\"firmwareVersion\":\"%s\",\"config\":{\"heartbeatIntervalMs\":%lu,\"offlineTimeoutMs\":%lu,"
          "\"routeTimeoutMs\":%lu,\"tLow\":%.2f,\"tHigh\":%.2f,\"ewmaAlpha\":%.2f,"
          "\"telemetryRatesHz\":{\"link\":%.2f,\"prediction\":%.2f,\"statistics\":%.2f}}}",
          orEmpty(p.firmwareVersion), static_cast<unsigned long>(p.heartbeatIntervalMs),
          static_cast<unsigned long>(p.offlineTimeoutMs), static_cast<unsigned long>(p.routeTimeoutMs), p.tLow,
          p.tHigh, p.ewmaAlpha, p.linkRateHz, p.predictionRateHz, p.statisticsRateHz);
  return finish(w);
}

size_t buildHeartbeat(const Envelope& env, uint32_t uptimeMs, char* buf, size_t bufSize) {
  Writer w;
  wInit(w, buf, bufSize);
  wEnvelopeOpen(w, env, "HEARTBEAT");
  wPrintf(w, "{\"uptimeMs\":%lu}", static_cast<unsigned long>(uptimeMs));
  return finish(w);
}

size_t buildNodeStatus(const Envelope& env, const NodeStatusPayload& p, char* buf, size_t bufSize) {
  Writer w;
  wInit(w, buf, bufSize);
  wEnvelopeOpen(w, env, "NODE_STATUS");
  wPrintf(w, "{\"status\":\"%s\",\"nodeName\":\"%s\",\"role\":\"%s\",\"uptimeMs\":%lu,\"firmwareVersion\":\"%s\"",
          orEmpty(p.status), orEmpty(p.nodeName), orEmpty(p.role), static_cast<unsigned long>(p.uptimeMs),
          orEmpty(p.firmwareVersion));
  if (p.reason != nullptr) {
    wPrintf(w, ",\"reason\":\"%s\"", p.reason);
  }
  wPrintf(w, "}");
  return finish(w);
}

size_t buildLinkUpdate(const Envelope& env, const LinkUpdatePayload& p, char* buf, size_t bufSize) {
  Writer w;
  wInit(w, buf, bufSize);
  wEnvelopeOpen(w, env, "LINK_UPDATE");
  wPrintf(w,
          "{\"from\":\"%s\",\"to\":\"%s\",\"rssiDbm\":%d,\"rssiEwmaDbm\":%.1f,\"rssiSlopeDbPerSec\":%.2f,"
          "\"pdr\":%.2f,\"pdrEwma\":%.2f,\"stalenessMs\":%lu,\"linkScore\":%.2f,\"state\":\"%s\"}",
          orEmpty(p.from), orEmpty(p.to), static_cast<int>(p.rssiDbm), p.rssiEwmaDbm, p.rssiSlopeDbPerSec, p.pdr,
          p.pdrEwma, static_cast<unsigned long>(p.stalenessMs), p.linkScore, orEmpty(p.state));
  return finish(w);
}

size_t buildRouteUpdate(const Envelope& env, const RouteUpdatePayload& p, char* buf, size_t bufSize) {
  Writer w;
  wInit(w, buf, bufSize);
  wEnvelopeOpen(w, env, "ROUTE_UPDATE");
  wPrintf(w, "{\"destination\":\"%s\",\"active\":{\"hops\":", orEmpty(p.destination));
  wHopsArray(w, p.active.hops, p.active.hopsLen);
  wPrintf(w, ",\"hopCount\":%u,\"score\":%.2f,\"state\":\"%s\"},\"candidates\":[",
          static_cast<unsigned>(derivedHopCount(p.active.hopsLen)), p.active.score, orEmpty(p.active.state));
  for (uint8_t i = 0; i < p.candidateCount; i++) {
    const RouteEntry& c = p.candidates[i];
    wPrintf(w, "%s{\"hops\":", i == 0 ? "" : ",");
    wHopsArray(w, c.hops, c.hopsLen);
    wPrintf(w, ",\"hopCount\":%u,\"score\":%.2f,\"state\":\"%s\"}",
            static_cast<unsigned>(derivedHopCount(c.hopsLen)), c.score, orEmpty(c.state));
  }
  wPrintf(w, "],\"trafficClass\":\"%s\",\"reason\":\"%s\"}", orEmpty(p.trafficClass), orEmpty(p.reason));
  return finish(w);
}

size_t buildPrediction(const Envelope& env, const PredictionPayload& p, char* buf, size_t bufSize) {
  Writer w;
  wInit(w, buf, bufSize);
  wEnvelopeOpen(w, env, "PREDICTION");
  wPrintf(w,
          "{\"neighborId\":\"%s\",\"rssiDbm\":%.1f,\"rssiEwmaDbm\":%.1f,\"rssiSlopeDbPerSec\":%.2f,\"pdr\":%.2f,"
          "\"pdrEwma\":%.2f,\"stalenessMs\":%lu,\"linkScore\":%.2f,\"predictionState\":\"%s\",\"tLow\":%.2f,"
          "\"tHigh\":%.2f,\"hysteresisState\":\"%s\"}",
          orEmpty(p.neighborId), p.rssiDbm, p.rssiEwmaDbm, p.rssiSlopeDbPerSec, p.pdr, p.pdrEwma,
          static_cast<unsigned long>(p.stalenessMs), p.linkScore, orEmpty(p.predictionState), p.tLow, p.tHigh,
          orEmpty(p.hysteresisState));
  return finish(w);
}

size_t buildSensorStatus(const Envelope& env, const SensorStatusPayload& p, char* buf, size_t bufSize) {
  Writer w;
  wInit(w, buf, bufSize);
  wEnvelopeOpen(w, env, "SENSOR_STATUS");
  wPrintf(w, "{\"sensorId\":\"%s\",\"sensorType\":\"%s\",", orEmpty(p.sensorId), orEmpty(p.sensorType));
  if (p.valueValid) {
    wPrintf(w, "\"value\":%.2f,", p.value);
  }
  wPrintf(w, "\"healthState\":\"%s\"", orEmpty(p.healthState));
  if (p.hasDurationMs) {
    wPrintf(w, ",\"durationMs\":%lu", static_cast<unsigned long>(p.durationMs));
  }
  wPrintf(w, ",\"rawValue\":%.2f,\"baseline\":%.2f,\"mad\":%.2f,\"zScore\":%.2f,\"threshold\":%.2f}", p.rawValue,
          p.baseline, p.mad, p.zScore, p.threshold);
  return finish(w);
}

size_t buildEvent(const Envelope& env, const EventPayload& p, char* buf, size_t bufSize) {
  Writer w;
  wInit(w, buf, bufSize);
  wEnvelopeOpen(w, env, "EVENT");
  wPrintf(w, "{\"eventType\":\"%s\",\"severity\":\"%s\",\"source\":\"%s\",\"details\":%s}", orEmpty(p.eventType),
          orEmpty(p.severity), orEmpty(p.source), p.detailsJson != nullptr ? p.detailsJson : "{}");
  return finish(w);
}

size_t buildStatistics(const Envelope& env, const StatisticsPayload& p, char* buf, size_t bufSize) {
  Writer w;
  wInit(w, buf, bufSize);
  wEnvelopeOpen(w, env, "STATISTICS");
  wPrintf(w,
          "{\"windowMs\":%lu,\"pdr\":%.2f,\"packetsTransmitted\":%lu,\"packetsAcknowledged\":%lu,"
          "\"packetsDropped\":%lu,\"retryCount\":%lu,\"duplicateCount\":%lu,\"endToEndLatencyMs\":%.1f}",
          static_cast<unsigned long>(p.windowMs), p.pdr, static_cast<unsigned long>(p.packetsTransmitted),
          static_cast<unsigned long>(p.packetsAcknowledged), static_cast<unsigned long>(p.packetsDropped),
          static_cast<unsigned long>(p.retryCount), static_cast<unsigned long>(p.duplicateCount),
          p.endToEndLatencyMs);
  return finish(w);
}

size_t buildPacket(const Envelope& env, const PacketPayload& p, char* buf, size_t bufSize) {
  Writer w;
  wInit(w, buf, bufSize);
  wEnvelopeOpen(w, env, "PACKET");
  wPrintf(w, "{\"meshSequence\":%u,\"seq\":%u,", static_cast<unsigned>(p.meshSequence), static_cast<unsigned>(p.meshSequence));
  if (p.hasAppSeq) {
    wPrintf(w, "\"appSeq\":%u,", static_cast<unsigned>(p.appSeq));
  }
  if (p.hasSensorValues) {
    wPrintf(w, "\"potValue\":%u,\"ldrValue\":%u,\"appTimestampMs\":%lu,", static_cast<unsigned>(p.potValue),
            static_cast<unsigned>(p.ldrValue), static_cast<unsigned long>(p.appTimestampMs));
  }
  wPrintf(w, "\"src\":\"%s\",\"dst\":\"%s\",\"currentNode\":\"%s\",", orEmpty(p.source), orEmpty(p.destination),
          orEmpty(p.currentNode));
  if (p.nextHop != nullptr) {
    wPrintf(w, "\"nextHop\":\"%s\",", p.nextHop);
  }
  if (p.path != nullptr && p.pathLen > 0) {
    wPrintf(w, "\"path\":");
    wHopsArray(w, p.path, p.pathLen);
    wPrintf(w, ",");
  }
  wPrintf(w, "\"trafficClass\":\"%s\",\"priority\":%s,\"status\":\"%s\",\"attemptCount\":%u}",
          orEmpty(p.trafficClass), p.priority ? "true" : "false", orEmpty(p.status),
          static_cast<unsigned>(p.attemptCount));
  return finish(w);
}

size_t buildError(const Envelope& env, const ErrorPayload& p, char* buf, size_t bufSize) {
  Writer w;
  wInit(w, buf, bufSize);
  wEnvelopeOpen(w, env, "ERROR");
  wPrintf(w, "{\"severity\":\"%s\",\"code\":\"%s\",\"message\":\"%s\",\"recoverable\":%s,\"details\":{}}",
          orEmpty(p.severity), orEmpty(p.code), orEmpty(p.message), p.recoverable ? "true" : "false");
  return finish(w);
}

}  // namespace telemetry_core
