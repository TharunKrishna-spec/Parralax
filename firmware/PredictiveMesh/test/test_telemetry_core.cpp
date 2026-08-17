// Minimal host-side unit test harness for telemetry_core's pure JSON
// envelope/payload construction (Phase 6). Like every other *_core test
// suite in this project, this is NOT a network/hardware simulator and does
// NOT pull in a JSON parsing library (matching this project's own stated
// avoidance of serialization-library dependencies, see docs/protocol.md) —
// it checks the hand-built JSON text directly: substring presence for every
// required field/value, and brace/bracket balance as a structural
// well-formedness proxy. telemetry_core.h/.cpp have zero Arduino/Serial
// dependency specifically so this can compile and run with a plain host
// compiler. See docs/testing.md.
//
// Build & run (host g++ — NOT the ESP32 toolchain; run from this file's
// directory):
//   g++ -std=c++17 -Wall -Wextra -I ../src ../src/telemetry/telemetry_core.cpp test_telemetry_core.cpp -o test_telemetry_core
//   ./test_telemetry_core

#include "../src/telemetry/telemetry_core.h"
#include <cstdio>
#include <cstring>

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

using namespace telemetry_core;

bool contains(const char* hay, const char* needle) {
  return hay != nullptr && std::strstr(hay, needle) != nullptr;
}

// Structural well-formedness proxy: every '{'/'[' must be closed by a
// matching '}'/']' in the correct order, and the count must return to zero
// exactly at the end of the string (no unclosed or over-closed objects).
bool balanced(const char* s) {
  int depth = 0;
  for (const char* c = s; *c; c++) {
    if (*c == '{' || *c == '[') depth++;
    else if (*c == '}' || *c == ']') {
      depth--;
      if (depth < 0) return false;
    }
  }
  return depth == 0;
}

Envelope testEnvelope() {
  return Envelope{ "A", "a-3f9c21a4", 42, 123456 };
}

// ---- envelope correctness, shared shape across every message type ----
void test_envelope_fields_present() {
  char buf[LINE_BUF_SIZE];
  size_t n = buildHeartbeat(testEnvelope(), 5000, buf, sizeof(buf));
  check(n > 0, "buildHeartbeat succeeds with a normal-size buffer");
  check(contains(buf, "\"protocolVersion\":\"mesh-json/v1\""), "envelope carries the exact frozen protocol version string");
  check(contains(buf, "\"type\":\"HEARTBEAT\""), "envelope type matches the builder");
  check(contains(buf, "\"nodeId\":\"A\""), "envelope nodeId matches the caller-supplied Envelope");
  check(contains(buf, "\"bootId\":\"a-3f9c21a4\""), "envelope bootId matches the caller-supplied Envelope");
  check(contains(buf, "\"seq\":42"), "envelope seq matches the caller-supplied Envelope");
  check(contains(buf, "\"timestampMs\":123456"), "envelope timestampMs matches the caller-supplied Envelope");
  check(contains(buf, "\"payload\":{\"uptimeMs\":5000}"), "HEARTBEAT payload carries the real uptimeMs value");
  check(balanced(buf), "HEARTBEAT line is a structurally balanced JSON object");
}

// ---- 0x01 HELLO ----
void test_hello_with_and_without_mac() {
  char buf[LINE_BUF_SIZE];
  HelloPayload p{};
  p.nodeName = "Node A";
  p.role = "SOURCE";
  p.mac = "24:6F:28:AA:BB:01";
  p.firmwareVersion = "1.0.0";
  p.heartbeatIntervalMs = 1000;
  p.offlineTimeoutMs = 3000;
  p.routeTimeoutMs = 3000;
  p.tLow = 0.5f;
  p.tHigh = 0.7f;
  p.ewmaAlpha = 0.3f;
  p.linkRateHz = 4.0f;
  p.predictionRateHz = 4.0f;
  p.statisticsRateHz = 1.0f;

  size_t n = buildHello(testEnvelope(), p, buf, sizeof(buf));
  check(n > 0, "buildHello succeeds with a real mac present");
  check(contains(buf, "\"type\":\"HELLO\""), "HELLO envelope type is correct");
  check(contains(buf, "\"mac\":\"24:6F:28:AA:BB:01\""), "HELLO includes mac when available");
  check(contains(buf, "\"config\":{\"heartbeatIntervalMs\":1000"), "HELLO nests config as a sub-object, matching the frozen contract exactly");
  check(contains(buf, "\"telemetryRatesHz\":{\"link\":4.00"), "HELLO nests telemetryRatesHz two levels deep inside config");
  check(balanced(buf), "HELLO (with mac) line is structurally balanced");

  p.mac = nullptr;
  n = buildHello(testEnvelope(), p, buf, sizeof(buf));
  check(n > 0, "buildHello succeeds with mac unavailable (nullptr)");
  check(!contains(buf, "\"mac\":"), "HELLO omits the mac field entirely when unavailable, rather than emitting a fabricated placeholder");
  check(balanced(buf), "HELLO (without mac) line is structurally balanced");
}

// ---- 0x03 NODE_STATUS ----
void test_node_status_optional_reason() {
  char buf[LINE_BUF_SIZE];
  NodeStatusPayload p{ "ONLINE", "Node A", "SOURCE", 9000, "1.0.0", nullptr };
  size_t n = buildNodeStatus(testEnvelope(), p, buf, sizeof(buf));
  check(n > 0, "buildNodeStatus succeeds without a reason");
  check(!contains(buf, "\"reason\""), "NODE_STATUS omits the optional reason field when not given");
  check(balanced(buf), "NODE_STATUS (no reason) line is structurally balanced");

  p.reason = "manual override";
  n = buildNodeStatus(testEnvelope(), p, buf, sizeof(buf));
  check(n > 0, "buildNodeStatus succeeds with a reason");
  check(contains(buf, "\"reason\":\"manual override\""), "NODE_STATUS includes the reason field when given");
  check(balanced(buf), "NODE_STATUS (with reason) line is structurally balanced");
}

// ---- 0x04 LINK_UPDATE ----
void test_link_update_fields() {
  char buf[LINE_BUF_SIZE];
  LinkUpdatePayload p{};
  p.from = "A";
  p.to = "B";
  p.rssiDbm = -64;
  p.rssiEwmaDbm = -62.0f;
  p.rssiSlopeDbPerSec = -1.8f;
  p.pdr = 0.91f;
  p.pdrEwma = 0.92f;
  p.stalenessMs = 120;
  p.linkScore = 0.78f;
  p.state = "DEGRADING";
  size_t n = buildLinkUpdate(testEnvelope(), p, buf, sizeof(buf));
  check(n > 0, "buildLinkUpdate succeeds");
  check(contains(buf, "\"from\":\"A\",\"to\":\"B\""), "LINK_UPDATE reports the correct directed link endpoints");
  check(contains(buf, "\"rssiDbm\":-64"), "LINK_UPDATE reports the real raw RSSI as an integer, no decimal point");
  check(contains(buf, "\"linkScore\":0.78"), "LINK_UPDATE reports the real fused link score");
  check(contains(buf, "\"state\":\"DEGRADING\""), "LINK_UPDATE reports the already-classified state string as-is");
  check(balanced(buf), "LINK_UPDATE line is structurally balanced");
}

// ---- 0x05 ROUTE_UPDATE ----
void test_route_update_with_and_without_candidates() {
  char buf[LINE_BUF_SIZE];
  RouteEntry candidates[2];
  candidates[0] = RouteEntry{ { "A", "B" }, 2, 0.82f, "ACTIVE" };
  candidates[1] = RouteEntry{ { "A", "C" }, 3, 0.91f, "BACKUP" };

  RouteUpdatePayload p{};
  p.destination = "S";
  p.active = candidates[0];
  p.candidates = candidates;
  p.candidateCount = 2;
  p.trafficClass = "NORMAL";
  p.reason = "UNKNOWN";

  size_t n = buildRouteUpdate(testEnvelope(), p, buf, sizeof(buf));
  check(n > 0, "buildRouteUpdate succeeds with two candidates");
  check(contains(buf, "\"destination\":\"S\""), "ROUTE_UPDATE reports the correct destination");
  check(contains(buf, "\"active\":{\"hops\":[\"A\",\"B\"]"), "ROUTE_UPDATE's active route carries the real 2-element hops known locally");
  check(contains(buf, "\"candidates\":[{"), "ROUTE_UPDATE's candidates array is populated when candidates exist");
  check(contains(buf, "\"hopCount\":3"), "ROUTE_UPDATE preserves each candidate's own real routing_core hop count");
  check(balanced(buf), "ROUTE_UPDATE (2 candidates) line is structurally balanced");

  p.candidateCount = 0;
  n = buildRouteUpdate(testEnvelope(), p, buf, sizeof(buf));
  check(n > 0, "buildRouteUpdate succeeds with zero candidates");
  check(contains(buf, "\"candidates\":[]"), "ROUTE_UPDATE emits a valid empty array, not a malformed comma, when there are no candidates");
  check(balanced(buf), "ROUTE_UPDATE (0 candidates) line is structurally balanced");
}

// ---- 0x06 PREDICTION ----
void test_prediction_fields() {
  char buf[LINE_BUF_SIZE];
  PredictionPayload p{};
  p.neighborId = "A";
  p.rssiDbm = -61.0f;
  p.rssiEwmaDbm = -59.0f;
  p.rssiSlopeDbPerSec = -1.7f;
  p.pdr = 0.89f;
  p.pdrEwma = 0.91f;
  p.stalenessMs = 120;
  p.linkScore = 0.54f;
  p.predictionState = "DEGRADING";
  p.tLow = 0.55f;
  p.tHigh = 0.75f;
  p.hysteresisState = "BELOW_LOW";
  size_t n = buildPrediction(testEnvelope(), p, buf, sizeof(buf));
  check(n > 0, "buildPrediction succeeds");
  check(contains(buf, "\"neighborId\":\"A\""), "PREDICTION reports the correct neighbor id");
  check(contains(buf, "\"predictionState\":\"DEGRADING\""), "PREDICTION reports the already-classified prediction state as-is");
  check(contains(buf, "\"hysteresisState\":\"BELOW_LOW\""), "PREDICTION reports the already-classified hysteresis state as-is");
  check(balanced(buf), "PREDICTION line is structurally balanced");
}

// ---- 0x07 SENSOR_STATUS ----
void test_sensor_status_optional_fields() {
  char buf[LINE_BUF_SIZE];
  SensorStatusPayload p{};
  p.sensorId = "pot";
  p.sensorType = "potentiometer";
  p.value = 2048.0f;
  p.valueValid = true;
  p.healthState = "NORMAL";
  p.hasDurationMs = false;
  p.rawValue = 2048.0f;
  p.baseline = 2050.0f;
  p.mad = 3.0f;
  p.zScore = 0.1f;
  p.threshold = 3.5f;

  size_t n = buildSensorStatus(testEnvelope(), p, buf, sizeof(buf));
  check(n > 0, "buildSensorStatus succeeds without durationMs");
  check(contains(buf, "\"value\":2048.00"), "SENSOR_STATUS includes value when valid");
  check(!contains(buf, "\"durationMs\""), "SENSOR_STATUS omits durationMs when not flatlined");
  check(balanced(buf), "SENSOR_STATUS (no duration) line is structurally balanced");

  p.hasDurationMs = true;
  p.durationMs = 8200;
  p.healthState = "FLATLINE";
  n = buildSensorStatus(testEnvelope(), p, buf, sizeof(buf));
  check(n > 0, "buildSensorStatus succeeds with durationMs");
  check(contains(buf, "\"durationMs\":8200"), "SENSOR_STATUS includes durationMs when flatlined, per the contract's 'required for FLATLINE' rule");
  check(balanced(buf), "SENSOR_STATUS (with duration) line is structurally balanced");

  p.valueValid = false;
  n = buildSensorStatus(testEnvelope(), p, buf, sizeof(buf));
  check(n > 0, "buildSensorStatus succeeds with an invalid value");
  check(!contains(buf, "\"value\":"), "SENSOR_STATUS omits value entirely when invalid, rather than fabricating a reading");
  check(balanced(buf), "SENSOR_STATUS (invalid value) line is structurally balanced");
}

// ---- 0x08 EVENT ----
void test_event_details_passthrough() {
  char buf[LINE_BUF_SIZE];
  EventPayload p{ "ROUTE_CHANGE", "INFO", "A", "{\"oldScore\":0.54,\"newScore\":0.91}" };
  size_t n = buildEvent(testEnvelope(), p, buf, sizeof(buf));
  check(n > 0, "buildEvent succeeds");
  check(contains(buf, "\"eventType\":\"ROUTE_CHANGE\""), "EVENT reports the correct eventType");
  check(contains(buf, "\"details\":{\"oldScore\":0.54,\"newScore\":0.91}"), "EVENT embeds the caller's pre-built details object as a raw nested object, not a quoted string");
  check(balanced(buf), "EVENT line is structurally balanced");

  EventPayload empty{ "PACKET_DROP", "ERROR", "B", nullptr };
  n = buildEvent(testEnvelope(), empty, buf, sizeof(buf));
  check(n > 0, "buildEvent succeeds with a null detailsJson");
  check(contains(buf, "\"details\":{}"), "EVENT defaults to an empty object, never a null/missing details field, when the caller passes nullptr");
  check(balanced(buf), "EVENT (default details) line is structurally balanced");
}

// ---- 0x09 STATISTICS ----
void test_statistics_fields() {
  char buf[LINE_BUF_SIZE];
  StatisticsPayload p{ 1000, 0.98f, 1042, 1021, 21, 34, 2, 38.0f };
  size_t n = buildStatistics(testEnvelope(), p, buf, sizeof(buf));
  check(n > 0, "buildStatistics succeeds");
  check(contains(buf, "\"packetsTransmitted\":1042"), "STATISTICS reports the real cumulative counters, not re-derived values");
  check(contains(buf, "\"pdr\":0.98"), "STATISTICS reports the real pdr ratio");
  check(balanced(buf), "STATISTICS line is structurally balanced");
}

// ---- 0x0A ERROR ----
void test_error_fields() {
  char buf[LINE_BUF_SIZE];
  ErrorPayload p{ "ERROR", "ROUTE_TABLE_FULL", "No candidate route slot available", true };
  size_t n = buildError(testEnvelope(), p, buf, sizeof(buf));
  check(n > 0, "buildError succeeds");
  check(contains(buf, "\"code\":\"ROUTE_TABLE_FULL\""), "ERROR reports the real code, matching the contract's own worked example");
  check(contains(buf, "\"recoverable\":true"), "ERROR serializes a real JSON boolean literal, not a quoted string");
  check(balanced(buf), "ERROR line is structurally balanced");
}

// ---- truncation safety ----
void test_truncation_is_refused_not_partial() {
  char tiny[8];
  size_t n = buildHeartbeat(testEnvelope(), 5000, tiny, sizeof(tiny));
  check(n == 0, "a message that cannot fit the caller's buffer is refused entirely (returns 0), never emitted truncated");
}

// ---- Part K enum classification (LinkClass, derived from real evidence) ----
void test_classify_link_all_branches() {
  check(classifyLink(false, false, true, 0, 0) == LinkClass::UNKNOWN_C,
        "a neighbor never observed classifies as UNKNOWN regardless of any other flag");
  check(classifyLink(true, true, true, 0, 0) == LinkClass::STALE_C,
        "a stale neighbor classifies as STALE even if the hysteresis state machine still says healthy");
  check(classifyLink(true, false, false, 0, 3) == LinkClass::RECOVERING_C,
        "unhealthy with a nonzero aboveCount (real debounce evidence of accumulating good evaluations) classifies as RECOVERING");
  check(classifyLink(true, false, false, 0, 0) == LinkClass::UNHEALTHY_C,
        "unhealthy with no recovery evidence yet classifies as plain UNHEALTHY");
  check(classifyLink(true, false, true, 2, 0) == LinkClass::DEGRADING_C,
        "healthy with a nonzero belowCount (real debounce evidence of accumulating bad evaluations) classifies as DEGRADING");
  check(classifyLink(true, false, true, 0, 0) == LinkClass::HEALTHY_C,
        "healthy with no degradation evidence classifies as plain HEALTHY");
}

void test_link_and_prediction_state_vocabularies() {
  check(std::strcmp(linkStateStr(LinkClass::HEALTHY_C), "HEALTHY") == 0, "LINK_UPDATE vocabulary uses HEALTHY");
  check(std::strcmp(predictionStateStr(LinkClass::HEALTHY_C), "STABLE") == 0,
        "PREDICTION vocabulary uses STABLE for the identical underlying classification — the two message types use different words for the same real state, per the frozen contract's own two enums");
  check(std::strcmp(linkStateStr(LinkClass::STALE_C), "STALE") == 0, "LINK_UPDATE vocabulary uses STALE");
  check(std::strcmp(predictionStateStr(LinkClass::STALE_C), "TIMEOUT") == 0,
        "PREDICTION vocabulary uses TIMEOUT for the identical underlying classification");
}

void test_hysteresis_state_boundaries() {
  check(std::strcmp(hysteresisStateStr(0.4f, 0.5f, 0.7f), "BELOW_LOW") == 0, "below tLow classifies BELOW_LOW");
  check(std::strcmp(hysteresisStateStr(0.6f, 0.5f, 0.7f), "BETWEEN_THRESHOLDS") == 0, "between tLow and tHigh classifies BETWEEN_THRESHOLDS");
  check(std::strcmp(hysteresisStateStr(0.7f, 0.5f, 0.7f), "ABOVE_HIGH") == 0, "exactly at tHigh classifies ABOVE_HIGH (contract: tHigh is the crossing point, not an open bound)");
  check(std::strcmp(hysteresisStateStr(0.9f, 0.5f, 0.7f), "ABOVE_HIGH") == 0, "above tHigh classifies ABOVE_HIGH");
}

void test_sensor_health_mapping_covers_every_state() {
  check(std::strcmp(sensorHealthStr(0), "SUSPECT") == 0, "anomaly_core WARMUP maps to the contract's SUSPECT");
  check(std::strcmp(sensorHealthStr(1), "NORMAL") == 0, "anomaly_core NORMAL maps to the contract's NORMAL");
  check(std::strcmp(sensorHealthStr(2), "ANOMALY") == 0, "anomaly_core ANOMALY maps to the contract's ANOMALY");
  check(std::strcmp(sensorHealthStr(3), "FLATLINE") == 0, "anomaly_core FLATLINE maps to the contract's FLATLINE");
  check(std::strcmp(sensorHealthStr(4), "STALE") == 0, "anomaly_core STALE maps to the contract's STALE");
  check(std::strcmp(sensorHealthStr(5), "OUT_OF_RANGE") == 0, "anomaly_core INVALID maps to the contract's OUT_OF_RANGE");
}

void test_route_reason_mapping() {
  check(std::strcmp(routeReasonStr(true, false), "PRIORITY_OVERRIDE") == 0, "a priority decision always maps to PRIORITY_OVERRIDE, regardless of invalidation");
  check(std::strcmp(routeReasonStr(true, true), "PRIORITY_OVERRIDE") == 0, "priority takes precedence over the invalidated flag");
  check(std::strcmp(routeReasonStr(false, true), "ROUTE_EXPIRED") == 0, "a non-priority invalidated route maps to ROUTE_EXPIRED");
  check(std::strcmp(routeReasonStr(false, false), "UNKNOWN") == 0,
        "a non-priority, non-invalidated change maps to UNKNOWN — routing_core has no finer-grained reason to report honestly (see docs/decisions.md)");
}

}  // namespace

int main() {
  test_envelope_fields_present();
  test_hello_with_and_without_mac();
  test_node_status_optional_reason();
  test_link_update_fields();
  test_route_update_with_and_without_candidates();
  test_prediction_fields();
  test_sensor_status_optional_fields();
  test_event_details_passthrough();
  test_statistics_fields();
  test_error_fields();
  test_truncation_is_refused_not_partial();
  test_classify_link_all_branches();
  test_link_and_prediction_state_vocabularies();
  test_hysteresis_state_boundaries();
  test_sensor_health_mapping_covers_every_state();
  test_route_reason_mapping();

  std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
