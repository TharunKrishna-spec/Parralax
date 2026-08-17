#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../core/node_id.h"

// ============================================================
// telemetry_core — pure JSON envelope/payload construction for the frozen
// firmware<->GUI contract (gui-main/gui-main/docs/gui-telemetry-contract.md,
// "mesh-json/v1"), deliberately free of any Arduino/Serial dependency (no
// millis(), no Serial.print(), no WiFi.*). Mirrors the routing_core/
// predictor_core/anomaly_core/reliability_core/ucb1_core split from Phases
// 1-5 for the same reason: JSON string construction is real, order/format-
// sensitive logic worth verifying on its own, and "verified on its own"
// only means something if it runs outside the ESP32 toolchain. This is
// what firmware/PredictiveMesh/test/test_telemetry_core.cpp compiles and
// runs directly with a host compiler.
//
// Ownership split (Phase 6 task Part H): this module only turns already-
// extracted plain data (floats, ints, NodeIds, small enums) into a
// contract-exact JSON line. It has no idea routing_core/predictor_core/
// anomaly_core/reliability_core/ucb1_core exist - the thin Arduino adapter
// (telemetry.cpp) is the only place that reads their real state and maps it
// into the plain structs below. See docs/decisions.md for the full Part K
// enum-mapping table (why each GUI enum value was chosen from what
// firmware-internal evidence).
//
// No JSON string-escaping is implemented deliberately: every string field
// this module ever writes is a fixed C-string literal chosen by firmware
// itself (an enum name, a node letter, a sensor id) - never user-supplied,
// never dynamic free text. See docs/decisions.md.
// ============================================================

namespace telemetry_core {

// Comfortably covers the largest real message (ROUTE_UPDATE with a full
// candidate set) with headroom to spare, well under the contract's own
// 4096-byte per-line ceiling. An algorithm-intrinsic buffer bound, not a
// deployment tunable - lives here, not config.h, matching
// routing_core::MAX_HOP_COUNT's own precedent (see docs/parameters.md).
static const size_t LINE_BUF_SIZE = 768;

// Every envelope field but `type`/`payload`, which each builder function
// supplies itself (Part I: exact contract fields, nothing renamed/added).
struct Envelope {
  const char* nodeId;     // canonical single-letter id, e.g. "A" — see core/node_id.h::nodeName()
  const char* bootId;
  uint32_t seq;
  uint32_t timestampMs;
};

// ---- Part K: enum classification shared between LINK_UPDATE/PREDICTION ----
// predictor_core::NeighborLinkState has no discrete DEGRADING/RECOVERING
// state field of its own (those are momentary RecomputeResult flags, never
// persisted) - this classification derives them from real, already-stored
// evidence (the hysteresis debounce counters belowCount/aboveCount) rather
// than inventing a new state field. See docs/decisions.md.
enum class LinkClass : uint8_t { UNKNOWN_C, HEALTHY_C, DEGRADING_C, UNHEALTHY_C, RECOVERING_C, STALE_C };

LinkClass classifyLink(bool everObserved, bool stale, bool healthy, uint8_t belowCount, uint8_t aboveCount);
const char* linkStateStr(LinkClass c);        // LINK_UPDATE's `state` vocabulary
const char* predictionStateStr(LinkClass c);  // PREDICTION's `predictionState` vocabulary (STABLE/TIMEOUT instead of HEALTHY/STALE)
const char* hysteresisStateStr(float linkScore, float tLow, float tHigh);
const char* roleStr(NodeRole role);

// Phase 7.1 (red-team Finding 6): the contract's routeReason vocabulary has
// 8 values; only these 5 are ever derivable from real firmware state
// without inventing a signal firmware doesn't actually have — see
// docs/decisions.md for why LINK_FAILURE/STALE_NEIGHBOR/MANUAL are never
// produced (routing's health gate is binary, so it can't distinguish
// "degrading" from "failed", and nothing in this firmware issues a
// route change on human command).
enum class RouteReason : uint8_t { PRIORITY_OVERRIDE_R, ROUTE_EXPIRED_R, LINK_DEGRADATION_R, ROUTE_RECOVERY_R, UNKNOWN_R };
const char* routeReasonStr(RouteReason reason);

const char* sensorHealthStr(uint8_t anomalySensorState);  // takes anomaly_core::SensorState as uint8_t to stay decoupled from that header

// ---- 0x01 HELLO ----
struct HelloPayload {
  const char* nodeName;
  const char* role;               // already-mapped via roleStr()
  const char* mac;                // "XX:XX:XX:XX:XX:XX", or nullptr if not yet available
  const char* firmwareVersion;
  uint32_t heartbeatIntervalMs;
  uint32_t offlineTimeoutMs;
  uint32_t routeTimeoutMs;
  float tLow;
  float tHigh;
  float ewmaAlpha;
  float linkRateHz;
  float predictionRateHz;
  float statisticsRateHz;
};
size_t buildHello(const Envelope& env, const HelloPayload& p, char* buf, size_t bufSize);

// ---- 0x02 HEARTBEAT ----
size_t buildHeartbeat(const Envelope& env, uint32_t uptimeMs, char* buf, size_t bufSize);

// ---- 0x03 NODE_STATUS ----
struct NodeStatusPayload {
  const char* status;    // ONLINE/STALE/OFFLINE/ERROR
  const char* nodeName;
  const char* role;
  uint32_t uptimeMs;
  const char* firmwareVersion;
  const char* reason;    // may be nullptr (optional)
};
size_t buildNodeStatus(const Envelope& env, const NodeStatusPayload& p, char* buf, size_t bufSize);

// ---- 0x04 LINK_UPDATE ----
struct LinkUpdatePayload {
  const char* from;
  const char* to;
  int8_t rssiDbm;
  float rssiEwmaDbm;
  float rssiSlopeDbPerSec;
  float pdr;
  float pdrEwma;
  uint32_t stalenessMs;
  float linkScore;
  const char* state;   // already-mapped via linkStateStr()
};
size_t buildLinkUpdate(const Envelope& env, const LinkUpdatePayload& p, char* buf, size_t bufSize);

// ---- 0x05 ROUTE_UPDATE ----
// Phase 7.1 (red-team Finding 5): `hops` is now the real, ordered node
// sequence [self, ..., destination] whenever the adapter could legitimately
// reconstruct it (routing_core::reconstructPath() — real graph search over
// the compiled-in static topology, never fabricated), falling back to the
// honest minimal [self, nextHop] pair when it couldn't (ambiguous or no
// matching graph path — see docs/decisions.md). `hopCount` is deliberately
// NOT a separately-stored field here: buildRouteUpdate() derives it as
// `hopsLen - 1` at serialization time, which makes the contract's own
// stated invariant (`hopCount == hops.length - 1`) impossible to violate by
// construction — the previous [2]-element-hops-with-a-independently-passed-
// hopCount shape is exactly what let that invariant drift out of sync
// (a real, demonstrated bug — see docs/known-issues.md).
struct RouteEntry {
  const char* hops[NODE_ID_COUNT];  // ordered [self, ..., destination]; only hops[0..hopsLen-1] are valid
  uint8_t hopsLen;                   // number of valid hops[] entries; always >= 1 in a well-formed instance
  float score;
  const char* state;     // ACTIVE or BACKUP
};
struct RouteUpdatePayload {
  const char* destination;
  RouteEntry active;
  const RouteEntry* candidates;
  uint8_t candidateCount;
  const char* trafficClass;  // NORMAL/PRIORITY
  const char* reason;        // already-mapped via routeReasonStr()
};
size_t buildRouteUpdate(const Envelope& env, const RouteUpdatePayload& p, char* buf, size_t bufSize);

// ---- 0x06 PREDICTION ----
struct PredictionPayload {
  const char* neighborId;
  float rssiDbm;
  float rssiEwmaDbm;
  float rssiSlopeDbPerSec;
  float pdr;
  float pdrEwma;
  uint32_t stalenessMs;
  float linkScore;
  const char* predictionState;  // already-mapped via predictionStateStr()
  float tLow;
  float tHigh;
  const char* hysteresisState;  // already-mapped via hysteresisStateStr()
};
size_t buildPrediction(const Envelope& env, const PredictionPayload& p, char* buf, size_t bufSize);

// ---- 0x07 SENSOR_STATUS ----
struct SensorStatusPayload {
  const char* sensorId;
  const char* sensorType;
  float value;
  bool valueValid;         // false -> value field omitted entirely (contract: "required when valid")
  const char* healthState; // already-mapped via sensorHealthStr()
  uint32_t durationMs;
  bool hasDurationMs;
  float rawValue;
  float baseline;
  float mad;
  float zScore;
  float threshold;
};
size_t buildSensorStatus(const Envelope& env, const SensorStatusPayload& p, char* buf, size_t bufSize);

// ---- 0x08 EVENT ----
// `detailsJson` is a caller-pre-built JSON object literal (e.g.
// "{\"oldScore\":0.54,\"newScore\":0.91}") - the adapter builds it with the
// same JsonWriter primitives this module uses internally, kept as a raw
// string parameter here so this one builder covers every eventType's
// differently-shaped `details` object without a combinatorial explosion of
// per-event-type structs.
struct EventPayload {
  const char* eventType;
  const char* severity;
  const char* source;
  const char* detailsJson;  // must be a valid JSON object literal, e.g. "{}" if empty
};
size_t buildEvent(const Envelope& env, const EventPayload& p, char* buf, size_t bufSize);

// ---- 0x09 STATISTICS ----
struct StatisticsPayload {
  uint32_t windowMs;
  float pdr;
  uint32_t packetsTransmitted;
  uint32_t packetsAcknowledged;
  uint32_t packetsDropped;
  uint32_t retryCount;
  uint32_t duplicateCount;
  float endToEndLatencyMs;
};
size_t buildStatistics(const Envelope& env, const StatisticsPayload& p, char* buf, size_t bufSize);

// ---- 0x0A ERROR ----
struct ErrorPayload {
  const char* severity;  // ERROR/CRITICAL
  const char* code;
  const char* message;
  bool recoverable;
};
size_t buildError(const Envelope& env, const ErrorPayload& p, char* buf, size_t bufSize);

}  // namespace telemetry_core
