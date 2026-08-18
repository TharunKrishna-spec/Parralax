#include "telemetry.h"
#include "telemetry_core.h"
#include "../config.h"
#include "../core/logger.h"
#include "../apptraffic/apptraffic_core.h"
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
//
// Phase 7.1 (red-team Finding 5/6): also remembers the last REAL,
// reconstructed hops path (not just the next-hop letter), so a ROUTE_CHANGE
// EVENT's oldHops/newHops can report genuine multi-hop paths too, and
// hopCount, so a genuine health-driven reroute (hop count got longer or
// shorter) can be told apart from an opaque table mutation — see
// reasonForChange() below.
struct CachedRoute {
  bool valid;
  NodeId nextHop;
  uint8_t hopCount;
  float score;
  const char* hopsPath[NODE_ID_COUNT];
  uint8_t hopsPathLen;
};
CachedRoute g_lastRoute[NODE_ID_COUNT];

// Reconstructs the real, full node-letter path for (destination, nextHop,
// hopCount) via routing_core::reconstructPath() — a real graph search over
// the compiled-in static topology, never fabricated. Falls back to the
// honest minimal [self, nextHop] pair (2 elements) when the graph search
// can't legitimately determine a unique full path (see routing_core.h for
// exactly when that happens — a real, provable ambiguity, not a cop-out).
// Shared by emitRouteUpdateFor() (active + every candidate) and
// onRouteEvent()'s ROUTE_CHANGE EVENT (old + new), so both use the exact
// same reconstruction, never two different notions of "the route".
uint8_t reconstructHopsPath(NodeId destination, NodeId nextHop, uint8_t hopCount, const char** out, uint8_t maxOut) {
  NodeId path[NODE_ID_COUNT];
  uint8_t n = routing_core::reconstructPath(THIS_NODE_ID, destination, nextHop, hopCount, path, NODE_ID_COUNT);
  if (n >= 2 && n <= maxOut) {
    for (uint8_t i = 0; i < n; i++) out[i] = nodeName(path[i]);
    return n;
  }
  out[0] = thisNode().name;
  out[1] = nodeName(nextHop);
  return 2;
}

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
// Builds a JSON array literal (e.g. "[\"A\",\"C\",\"D\",\"S\"]") from `len`
// node-letter strings into `out` — adapter-local glue for the ROUTE_CHANGE
// EVENT's oldHops/newHops (mirrors telemetry_core::wHopsArray's own
// array-writing discipline, kept here since telemetry_core only ever
// receives a pre-assembled `details` string, never composes one itself —
// see docs/decisions.md). Returns 0 (leaves `out` unspecified) if `out`
// would truncate, never a partial array.
size_t formatHopsJson(char* out, size_t outCap, const char* const* hops, uint8_t len) {
  size_t pos = 0;
  int n = snprintf(out + pos, outCap - pos, "[");
  if (n < 0 || static_cast<size_t>(n) >= outCap - pos) return 0;
  pos += static_cast<size_t>(n);
  for (uint8_t i = 0; i < len; i++) {
    n = snprintf(out + pos, outCap - pos, "%s\"%s\"", i == 0 ? "" : ",", hops[i]);
    if (n < 0 || static_cast<size_t>(n) >= outCap - pos) return 0;
    pos += static_cast<size_t>(n);
  }
  n = snprintf(out + pos, outCap - pos, "]");
  if (n < 0 || static_cast<size_t>(n) >= outCap - pos) return 0;
  pos += static_cast<size_t>(n);
  return pos;
}

void emitEvent(uint32_t now, const char* eventType, const char* severity, const char* source, const char* detailsJson) {
  telemetry_core::EventPayload p{ eventType, severity, source, detailsJson };
  emit(telemetry_core::buildEvent(envelope(now), p, g_line, sizeof(g_line)));
}

void emitRouteUpdateFor(uint32_t now, NodeId destination, NodeId activeNextHop, uint8_t activeHopCount,
                        bool priority, telemetry_core::RouteReason reason) {
  if (activeNextHop == NODE_ID_UNKNOWN) return;  // no valid route to report - see docs/known-issues.md

  routing_core::CandidateInfo raw[NODE_ID_COUNT];
  uint8_t n = routing::getCandidates(destination, raw, NODE_ID_COUNT);

  telemetry_core::RouteEntry candidates[NODE_ID_COUNT];
  for (uint8_t i = 0; i < n; i++) {
    candidates[i].hopsLen = reconstructHopsPath(destination, raw[i].nextHop, raw[i].hopCount, candidates[i].hops, NODE_ID_COUNT);
    candidates[i].score = predictor::linkScore(raw[i].nextHop);
    candidates[i].state = (raw[i].nextHop == activeNextHop) ? "ACTIVE" : "BACKUP";
  }

  telemetry_core::RouteUpdatePayload p{};
  p.destination = nodeName(destination);
  p.active.hopsLen = reconstructHopsPath(destination, activeNextHop, activeHopCount, p.active.hops, NODE_ID_COUNT);
  p.active.score = predictor::linkScore(activeNextHop);
  p.active.state = "ACTIVE";
  p.candidates = candidates;
  p.candidateCount = n;
  p.trafficClass = priority ? "PRIORITY" : "NORMAL";
  p.reason = telemetry_core::routeReasonStr(reason);
  emit(telemetry_core::buildRouteUpdate(envelope(now), p, g_line, sizeof(g_line)));
}

// Part 3/12: emits real, per-hop application/mesh packet movement, driven
// entirely by reliability::ReliabilityEvent — never a parallel simulator.
// One PACKET message per real hop-transmission event (TX/RETRY/DELIVERED/
// DROP/RECEIVED), matching the project's "one reliability trial, however
// many attempts" accounting elsewhere (attemptCount is always the real,
// current one, never re-derived). `path` is deliberately the minimal real
// [currentNode, neighbor] pair for THIS hop, not a full multi-hop
// reconstruction — ROUTE_UPDATE already owns the full-path concept; a
// live-mode GUI packet animation naturally builds up the visual impression
// of end-to-end movement from a real sequence of these per-hop events, the
// same way physically watching a packet cross the mesh one radio hop at a
// time would.
void emitPacket(uint32_t now, const reliability::ReliabilityEvent& evt, const char* status, bool hasDecoded,
                 const apptraffic_core::DecodedData& decoded) {
  const char* path[2];
  uint8_t pathLen = 0;
  const char* nextHopStr = nullptr;

  if (evt.type == reliability::ReliabilityEventType::PACKET_RECEIVED) {
    // The just-completed hop: whoever sent it to us, then us.
    if (evt.neighbor != NODE_ID_UNKNOWN) {
      path[0] = nodeName(evt.neighbor);
      path[1] = thisNode().name;
      pathLen = 2;
    }
  } else if (evt.neighbor != NODE_ID_UNKNOWN) {
    path[0] = thisNode().name;
    path[1] = nodeName(evt.neighbor);
    pathLen = 2;
    nextHopStr = nodeName(evt.neighbor);
  }

  telemetry_core::PacketPayload p{};
  p.meshSequence = evt.sequence;
  p.hasAppSeq = hasDecoded;
  p.appSeq = decoded.appSeq;
  p.hasSensorValues = hasDecoded;
  p.potValue = decoded.potValue;
  p.ldrValue = decoded.ldrValue;
  p.appTimestampMs = decoded.timestampMs;
  p.source = nodeName(evt.source);
  p.destination = nodeName(evt.destination);
  p.currentNode = thisNode().name;
  p.nextHop = nextHopStr;
  p.path = pathLen > 0 ? path : nullptr;
  p.pathLen = pathLen;
  p.trafficClass = evt.priority ? "PRIORITY" : "NORMAL";
  p.priority = evt.priority;
  p.status = status;
  p.attemptCount = evt.attemptCount;
  emit(telemetry_core::buildPacket(envelope(now), p, g_line, sizeof(g_line)));
}

}  // namespace

namespace telemetry {

void init(const uint8_t mac[6]) {
  snprintf(g_bootId, sizeof(g_bootId), "%s-%08x", thisNode().name, static_cast<unsigned>(esp_random()));
  g_seq = 0;
  for (uint8_t i = 0; i < NODE_ID_COUNT; i++) {
    g_lastRoute[i].valid = false;
    g_lastRoute[i].hopsPathLen = 0;
  }

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

  if (evt.type == routing::RouteEventType::NEIGHBOR_SILENT) {
    // Part 5: a real direct-neighbor liveness transition, detected by
    // routing.cpp's own existing ROUTING_ENTRY_TIMEOUT_MS sweep (no second,
    // independent timeout system) - distinct from a route-to-a-destination
    // event, so handled before ever touching g_lastRoute[]. `destination`
    // carries the silent neighbor's real NodeId for this event type (see
    // routing.h's RouteEventType::NEIGHBOR_SILENT doc comment).
    uint32_t lastSeenMs = routing::getNeighborLastSeenMs(evt.destination);
    uint32_t silentForMs = static_cast<uint32_t>(now - lastSeenMs);
    char details[80];
    snprintf(details, sizeof(details), "{\"neighbor\":\"%s\",\"silentForMs\":%lu}", nodeName(evt.destination),
             static_cast<unsigned long>(silentForMs));
    emitEvent(now, "NODE_SILENT", "ERROR", nodeName(evt.destination), details);
    return;
  }

  CachedRoute& cached = g_lastRoute[evt.destination];

  if (evt.type == routing::RouteEventType::ROUTE_SELECTED) {
    if (evt.priority) {
      char details[96];
      snprintf(details, sizeof(details), "{\"destination\":\"%s\",\"nextHop\":\"%s\",\"hopCount\":%u}",
               nodeName(evt.destination), evt.next_hop == NODE_ID_UNKNOWN ? "NONE" : nodeName(evt.next_hop),
               static_cast<unsigned>(evt.hop_count));
      emitEvent(now, "PRIORITY_ROUTE", "INFO", thisNode().name, details);
      return;
    }
    // Phase 7.1 (red-team Finding 6): NORMAL selection fires on EVERY
    // decision query (e.g. apptraffic's own periodic reliability::send()),
    // not just table mutations - a health-driven reroute (the demo's
    // headline "B degrades -> A reroutes via C-D" scenario) changes the
    // WINNING candidate without ever mutating routing_core's table, so the
    // ROUTE_CHANGED path below would never see it on its own. Detect a
    // real change here by comparing against the same cache ROUTE_CHANGED
    // uses, and fall through to the identical shared handling below when
    // one is found — see docs/decisions.md.
    if (evt.next_hop == NODE_ID_UNKNOWN) return;                  // no valid route - ROUTE_INVALIDATED covers this
    if (cached.valid && cached.nextHop == evt.next_hop) return;   // unchanged, nothing real to report
  } else if (evt.type == routing::RouteEventType::ROUTE_INVALIDATED) {
    if (cached.valid) {
      char oldHopsJson[64];
      formatHopsJson(oldHopsJson, sizeof(oldHopsJson), cached.hopsPath, cached.hopsPathLen);
      char details[192];
      snprintf(details, sizeof(details),
               "{\"oldHops\":%s,\"newHops\":[],\"reason\":\"ROUTE_EXPIRED\",\"oldScore\":%.2f,\"newScore\":0.00}",
               oldHopsJson, cached.score);
      emitEvent(now, "ROUTE_CHANGE", "WARN", thisNode().name, details);
    }
    cached.valid = false;
    return;
  }

  // Reaches here for a genuine route change: either a real ROUTE_CHANGED
  // table mutation, or a ROUTE_SELECTED that just proved a real
  // health-driven change above. Both share identical "build ROUTE_UPDATE +
  // EVENT from real old-vs-new state" handling — snapshot the OLD cached
  // state first, since `cached` is overwritten at the end of this function.
  bool hadOld = cached.valid;
  NodeId oldNextHop = cached.nextHop;
  uint8_t oldHopCount = cached.hopCount;
  float oldScore = cached.score;
  const char* oldHopsPath[NODE_ID_COUNT];
  uint8_t oldHopsPathLen = cached.hopsPathLen;
  for (uint8_t i = 0; i < oldHopsPathLen; i++) oldHopsPath[i] = cached.hopsPath[i];

  // Real, derivable reason (Finding 6): only a health-gated ROUTE_SELECTED
  // change (the table itself is unchanged) can be legitimately attributed
  // to link health at all — a genuine table mutation (ROUTE_CHANGED) could
  // be caused by many things routing doesn't distinguish (topology change,
  // neighbor restart, ...), so that always stays UNKNOWN, never guessed.
  // Comparing real hop counts (never fabricated) tells degradation and
  // recovery apart: moving onto a LONGER path means the previously
  // preferred shorter one was excluded by health; moving onto a SHORTER
  // one means a previously excluded path is preferred again.
  telemetry_core::RouteReason reason = telemetry_core::RouteReason::UNKNOWN_R;
  if (hadOld && evt.type == routing::RouteEventType::ROUTE_SELECTED) {
    if (evt.hop_count > oldHopCount) {
      reason = telemetry_core::RouteReason::LINK_DEGRADATION_R;
    } else if (evt.hop_count < oldHopCount) {
      reason = telemetry_core::RouteReason::ROUTE_RECOVERY_R;
    }
  }

  float newScore = predictor::linkScore(evt.next_hop);
  emitRouteUpdateFor(now, evt.destination, evt.next_hop, evt.hop_count, false, reason);

  const char* newHopsPath[NODE_ID_COUNT];
  uint8_t newHopsPathLen = reconstructHopsPath(evt.destination, evt.next_hop, evt.hop_count, newHopsPath, NODE_ID_COUNT);

  if (hadOld && oldNextHop != evt.next_hop) {
    char oldHopsJson[64];
    char newHopsJson[64];
    formatHopsJson(oldHopsJson, sizeof(oldHopsJson), oldHopsPath, oldHopsPathLen);
    formatHopsJson(newHopsJson, sizeof(newHopsJson), newHopsPath, newHopsPathLen);
    char details[320];

    // Part 13: a real, MEASURED (never extrapolated/estimated/fabricated)
    // prediction lead-time — see docs/decisions.md for the full defensible-
    // definition writeup. Only meaningful for a genuine proactive,
    // score-driven reroute (LINK_DEGRADATION_R): how much sooner this node
    // acted compared to what routing's own independent, silence-based hard
    // fallback (ROUTING_ENTRY_TIMEOUT_MS) would eventually have forced
    // anyway, using the real last-seen timestamp for the neighbor being
    // moved away from. Omitted entirely — never fabricated as 0 or a guess
    // — for any other reason: ROUTE_RECOVERY_R/UNKNOWN_R have no defensible
    // "what deadline did we beat" question to answer.
    if (reason == telemetry_core::RouteReason::LINK_DEGRADATION_R) {
      uint32_t oldNextHopLastSeenMs = routing::getNeighborLastSeenMs(oldNextHop);
      uint32_t stalenessOfOldMs = static_cast<uint32_t>(now - oldNextHopLastSeenMs);
      int32_t leadTimeMs = static_cast<int32_t>(ROUTING_ENTRY_TIMEOUT_MS) - static_cast<int32_t>(stalenessOfOldMs);
      if (leadTimeMs < 0) leadTimeMs = 0;
      snprintf(details, sizeof(details),
               "{\"oldHops\":%s,\"newHops\":%s,\"reason\":\"%s\",\"oldScore\":%.2f,\"newScore\":%.2f,\"leadTimeMs\":%ld}",
               oldHopsJson, newHopsJson, telemetry_core::routeReasonStr(reason), oldScore, newScore,
               static_cast<long>(leadTimeMs));
    } else {
      snprintf(details, sizeof(details),
               "{\"oldHops\":%s,\"newHops\":%s,\"reason\":\"%s\",\"oldScore\":%.2f,\"newScore\":%.2f}",
               oldHopsJson, newHopsJson, telemetry_core::routeReasonStr(reason), oldScore, newScore);
    }
    emitEvent(now, "ROUTE_CHANGE", "INFO", thisNode().name, details);
  }

  cached.valid = true;
  cached.nextHop = evt.next_hop;
  cached.hopCount = evt.hop_count;
  cached.score = newScore;
  cached.hopsPathLen = newHopsPathLen;
  for (uint8_t i = 0; i < newHopsPathLen; i++) cached.hopsPath[i] = newHopsPath[i];
}

void onLinkEvent(const predictor::LinkEvent& evt) {
  uint32_t now = millis();
  char details[48];
  snprintf(details, sizeof(details), "{\"score\":%.2f}", evt.score);

  if (evt.type == predictor::LinkEventType::LINK_DEGRADING) {
    emitEvent(now, "LINK_DEGRADING", "WARN", nodeName(evt.neighbor), details);

    // Part 7: REROUTE_PROPOSED only when routing has actually identified a
    // real, different viable candidate — never merely because a link score
    // changed. NODE_S is this project's one real destination (apptraffic's
    // fixed A->S flow); routing_core::enumerateCandidates() already returns
    // 0 when queried by NODE_S about itself, so this self-excludes with no
    // special-casing needed.
    routing_core::CandidateInfo candidates[NODE_ID_COUNT];
    uint8_t n = routing::getCandidates(NODE_S, candidates, NODE_ID_COUNT);
    bool degradingNeighborIsCandidate = false;
    for (uint8_t i = 0; i < n; i++) {
      if (candidates[i].nextHop == evt.neighbor) degradingNeighborIsCandidate = true;
    }
    if (degradingNeighborIsCandidate) {
      for (uint8_t i = 0; i < n; i++) {
        if (candidates[i].nextHop == evt.neighbor) continue;
        char proposedDetails[144];
        snprintf(proposedDetails, sizeof(proposedDetails),
                 "{\"degradingNeighbor\":\"%s\",\"alternateNextHop\":\"%s\",\"alternateHopCount\":%u,\"alternateScore\":%.2f}",
                 nodeName(evt.neighbor), nodeName(candidates[i].nextHop), static_cast<unsigned>(candidates[i].hopCount),
                 predictor::linkScore(candidates[i].nextHop));
        emitEvent(now, "REROUTE_PROPOSED", "WARN", thisNode().name, proposedDetails);
        break;  // one real alternate proves a reroute is genuinely viable — never enumerate as if ranking (that's REROUTE_PROPOSED's job to signal, not to replace ROUTE_UPDATE's own candidate list)
      }
    }
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

  const char* status = nullptr;
  bool hasDecoded = false;
  apptraffic_core::DecodedData decoded{};

  switch (evt.type) {
    case reliability::ReliabilityEventType::PACKET_TX:
      status = "SENT";
      break;

    case reliability::ReliabilityEventType::PACKET_RETRY:
      status = "RETRIED";
      emitEvent(now, "PACKET_RETRY", "WARN", nodeName(evt.source), details);
      break;

    case reliability::ReliabilityEventType::PACKET_DELIVERED:
      status = "DELIVERED";
      if (evt.attemptCount > 1) {
        // Part 8: a delivery that needed real retries, distinct from a
        // first-try success — reuses reliability_core::AckResult's own
        // real, final attemptCount (never re-derived or guessed). One
        // application delivery series still counts as one series either
        // way (reliability_core's own accounting, unchanged) — this only
        // adds a distinguishing EVENT for the recovered case.
        emitEvent(now, "PACKET_RECOVERED", "INFO", nodeName(evt.source), details);
      }
      break;

    case reliability::ReliabilityEventType::PACKET_DROP:
      status = "FAILED";
      emitEvent(now, "PACKET_DROP", "ERROR", nodeName(evt.source), details);
      // Part 6: attemptCount > 1 here is only ever true for a genuine
      // retry-exhaustion FAILED outcome (reliability_core::tickTimeouts's
      // own FAILED branch, attemptCount == 1 + RELIABILITY_MAX_RETRIES) —
      // the pool-full and synchronous-send-rejection PACKET_DROP call
      // sites in reliability.cpp always report attemptCount 0 or 1
      // respectively, never more. This real, already-existing field
      // distinguishes the cases without inventing a new one.
      if (evt.attemptCount > 1) {
        emitEvent(now, "TIMEOUT_FALLBACK", "ERROR", nodeName(evt.source), details);
      }
      break;

    case reliability::ReliabilityEventType::DUPLICATE_DROPPED:
      emitEvent(now, "DUPLICATE_SUPPRESSED", "INFO", nodeName(evt.source), details);
      break;

    case reliability::ReliabilityEventType::PACKET_RECEIVED: {
      status = "RECEIVED";
      // Part 17: the real, live sink-side decode path. apptraffic_core's
      // own encode/decode pair is reused exactly as-is (no second decoder)
      // — a decode failure (e.g. a payload too short to be real apptraffic
      // DATA) simply leaves hasDecoded false; nothing about it is ever
      // fabricated.
      if (evt.payload != nullptr && apptraffic_core::decodeData(evt.payload, evt.payloadLen, &decoded)) {
        hasDecoded = true;
        logger::info("[TELEMETRY] decoded application DATA appSeq=%u pot=%u ldr=%u source=%s",
                      static_cast<unsigned>(decoded.appSeq), static_cast<unsigned>(decoded.potValue),
                      static_cast<unsigned>(decoded.ldrValue), nodeName(evt.source));
      }
      break;
    }

    case reliability::ReliabilityEventType::PACKET_ACK:
      // No discrete EVENT or PACKET emission here — PACKET_DELIVERED
      // (fired immediately alongside it, from the same real ACK match)
      // already reports this exact moment; emitting both would report one
      // real event as two.
      return;
  }

  if (status != nullptr) {
    emitPacket(now, evt, status, hasDecoded, decoded);
  }
}

// Priority-broadcast milestone (2026-08-18). Reuses the existing, generic
// 0x08 EVENT message (no new message type, no GUI contract change) — the
// GUI's own applyTelemetryCore() already has a generic fallback that logs
// any unrecognized eventType safely (see docs/gui-compatibility-matrix.md).
// Every field here is real: never fabricated because a function merely
// ran. PRIORITY_DELIVERED reuses apptraffic_core::decodeData() exactly
// like PACKET_RECEIVED above — no second decoder.
void onSuppressionEvent(const suppression::SuppressionEvent& evt) {
  uint32_t now = millis();
  char details[192];

  switch (evt.type) {
    case suppression::SuppressionEventType::PRIORITY_BROADCAST:
      snprintf(details, sizeof(details), "{\"sequence\":%u,\"destination\":\"%s\"}",
               static_cast<unsigned>(evt.sequence), nodeName(evt.destination));
      emitEvent(now, "PRIORITY_BROADCAST", "INFO", nodeName(evt.source), details);
      break;

    case suppression::SuppressionEventType::PRIORITY_OVERHEARD:
      snprintf(details, sizeof(details), "{\"sequence\":%u,\"rssi\":%d,\"overheardCount\":%u,\"currentNode\":\"%s\"}",
               static_cast<unsigned>(evt.sequence), evt.rssi, static_cast<unsigned>(evt.overheardCount),
               thisNode().name);
      emitEvent(now, "PRIORITY_OVERHEARD", "INFO", nodeName(evt.source), details);
      break;

    case suppression::SuppressionEventType::PRIORITY_FORWARD:
      snprintf(details, sizeof(details),
               "{\"sequence\":%u,\"overheardCount\":%u,\"threshold\":%u,\"backoffMs\":%u,\"currentNode\":\"%s\"}",
               static_cast<unsigned>(evt.sequence), static_cast<unsigned>(evt.overheardCount),
               static_cast<unsigned>(SUPPRESSION_THRESHOLD), static_cast<unsigned>(evt.backoffMs), thisNode().name);
      emitEvent(now, "PRIORITY_FORWARD", "INFO", nodeName(evt.source), details);
      break;

    case suppression::SuppressionEventType::PRIORITY_SUPPRESSED:
      snprintf(details, sizeof(details), "{\"sequence\":%u,\"overheardCount\":%u,\"threshold\":%u,\"currentNode\":\"%s\"}",
               static_cast<unsigned>(evt.sequence), static_cast<unsigned>(evt.overheardCount),
               static_cast<unsigned>(SUPPRESSION_THRESHOLD), thisNode().name);
      emitEvent(now, "PRIORITY_SUPPRESSED", "INFO", nodeName(evt.source), details);
      break;

    case suppression::SuppressionEventType::PRIORITY_DELIVERED: {
      apptraffic_core::DecodedData decoded{};
      bool hasDecoded = evt.payload != nullptr && apptraffic_core::decodeData(evt.payload, evt.payloadLen, &decoded);
      if (hasDecoded) {
        logger::info("[TELEMETRY] decoded PRIORITY application DATA appSeq=%u pot=%u ldr=%u source=%s",
                      static_cast<unsigned>(decoded.appSeq), static_cast<unsigned>(decoded.potValue),
                      static_cast<unsigned>(decoded.ldrValue), nodeName(evt.source));
        snprintf(details, sizeof(details), "{\"sequence\":%u,\"appSeq\":%u,\"potValue\":%u,\"ldrValue\":%u}",
                 static_cast<unsigned>(evt.sequence), static_cast<unsigned>(decoded.appSeq),
                 static_cast<unsigned>(decoded.potValue), static_cast<unsigned>(decoded.ldrValue));
      } else {
        snprintf(details, sizeof(details), "{\"sequence\":%u}", static_cast<unsigned>(evt.sequence));
      }
      emitEvent(now, "PRIORITY_DELIVERED", "INFO", nodeName(evt.source), details);
      break;
    }
  }
}

void reportError(const char* code, const char* message, bool recoverable) {
  uint32_t now = millis();
  telemetry_core::ErrorPayload p{ "ERROR", code, message, recoverable };
  emit(telemetry_core::buildError(envelope(now), p, g_line, sizeof(g_line)));
}

}  // namespace telemetry
