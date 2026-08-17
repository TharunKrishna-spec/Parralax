#include "main.h"
#include "config.h"
#include "core/logger.h"
#include "core/node_id.h"
#include "core/packet.h"
#include "transport/espnow_transport.h"
#include "routing/routing.h"
#include "predictor/predictor.h"
#include "anomaly/anomaly.h"
#include "reliability/reliability.h"
#include "telemetry/telemetry.h"
#include "ucb1/ucb1.h"

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>

namespace {

uint32_t g_lastAliveLog = 0;
uint32_t g_lastSensorSample = 0;

// Transport already logs every [RX]/[TX] line itself (see
// espnow_transport.cpp); this hook's job is to turn a raw received frame
// into a MeshPacket and hand it to routing. Never pointer-cast evt.data
// directly (see core/packet.h) - memcpy into a locally declared,
// compiler-aligned MeshPacket first.
void onTransportRx(const transport::RxEvent& evt) {
  if (evt.len < PACKET_HEADER_SIZE) return;  // too short to be a MeshPacket, ignore

  MeshPacket pkt;
  size_t copyLen = evt.len < sizeof(MeshPacket) ? evt.len : sizeof(MeshPacket);
  memcpy(&pkt, evt.data, copyLen);

  routing::onPacketReceived(pkt, evt.rssi);
  predictor::onPacketReceived(pkt, evt.rssi);
  reliability::onPacketReceived(pkt, evt.rssi);
}

void onTransportTx(const transport::TxEvent& evt) {
  (void)evt;
}

// Phase 1's route-change/invalidation event stream - logged for now.
// Later phases (predictor-triggered rerouting, telemetry/dashboard)
// subscribe to the same routing::setEventCallback() instead of adding a
// new hook here.
void onRouteEvent(const routing::RouteEvent& evt) {
  const char* typeStr =
      evt.type == routing::RouteEventType::ROUTE_SELECTED    ? "SELECTED"
      : evt.type == routing::RouteEventType::ROUTE_CHANGED   ? "CHANGED"
                                                               : "INVALIDATED";
  logger::debug("[ROUTE-EVENT] %s dst=%s next=%s hops=%u priority=%d", typeStr,
                nodeName(evt.destination),
                evt.next_hop == NODE_ID_UNKNOWN ? "NONE" : nodeName(evt.next_hop),
                static_cast<unsigned>(evt.hop_count), evt.priority ? 1 : 0);
  telemetry::onRouteEvent(evt);
}

// Phase 2's link-health event stream - logged for now, same pattern as
// onRouteEvent above. A later phase (anomaly/telemetry) can subscribe via
// predictor::setEventCallback() instead of adding a new hook here.
void onLinkEvent(const predictor::LinkEvent& evt) {
  const char* typeStr =
      evt.type == predictor::LinkEventType::LINK_SCORE_UPDATED ? "SCORE_UPDATED"
      : evt.type == predictor::LinkEventType::LINK_DEGRADING   ? "DEGRADING"
      : evt.type == predictor::LinkEventType::LINK_UNHEALTHY   ? "UNHEALTHY"
                                                                 : "RECOVERED";
  logger::debug("[PREDICTOR-EVENT] %s neighbor=%s score=%.2f", typeStr, nodeName(evt.neighbor), evt.score);
  telemetry::onLinkEvent(evt);
}

// Phase 3's sensor-health event stream (state-machine transitions only,
// not every sample) - logged for now, same pattern as onRouteEvent/
// onLinkEvent above. A later phase (OLED/telemetry) subscribes via
// anomaly::setEventCallback() instead of adding a new hook here.
//
// Deliberately does NOT touch routing/predictor state - a sensor anomaly
// and a network/link anomaly are separate failure domains (Part 9 of the
// Phase 3 task spec). See docs/decisions.md.
void onAnomalyEvent(const anomaly::AnomalyEvent& evt) {
  const char* sensorStr = evt.sensor == anomaly::SensorId::POT ? "POT" : "LDR";
  const char* typeStr =
      evt.type == anomaly::AnomalyEventType::SENSOR_ANOMALY    ? "SENSOR_ANOMALY"
      : evt.type == anomaly::AnomalyEventType::SENSOR_FLATLINE ? "SENSOR_FLATLINE"
      : evt.type == anomaly::AnomalyEventType::SENSOR_STALE    ? "SENSOR_STALE"
      : evt.type == anomaly::AnomalyEventType::SENSOR_INVALID  ? "SENSOR_INVALID"
                                                                  : "SENSOR_RECOVERED";
  logger::debug("[ANOMALY-EVENT] %s sensor=%s raw=%.0f modified_z=%.2f",
                typeStr, sensorStr, evt.telemetry.raw_value, evt.telemetry.modified_z);
  telemetry::onAnomalyEvent(evt);
}

// Phase 4's reliability event stream (hop-by-hop TX/ACK/retry/deliver/
// drop/duplicate, plus PACKET_RECEIVED for a locally-delivered DATA
// packet) - logged for now, same pattern as onRouteEvent/onLinkEvent/
// onAnomalyEvent above. A later phase (telemetry/STATISTICS wiring)
// subscribes via reliability::setEventCallback() instead of adding a new
// hook here.
void onReliabilityEvent(const reliability::ReliabilityEvent& evt) {
  const char* typeStr =
      evt.type == reliability::ReliabilityEventType::PACKET_TX         ? "PACKET_TX"
      : evt.type == reliability::ReliabilityEventType::PACKET_ACK      ? "PACKET_ACK"
      : evt.type == reliability::ReliabilityEventType::PACKET_RETRY    ? "PACKET_RETRY"
      : evt.type == reliability::ReliabilityEventType::PACKET_DELIVERED ? "PACKET_DELIVERED"
      : evt.type == reliability::ReliabilityEventType::PACKET_DROP     ? "PACKET_DROP"
      : evt.type == reliability::ReliabilityEventType::DUPLICATE_DROPPED ? "DUPLICATE_DROPPED"
                                                                           : "PACKET_RECEIVED";
  logger::debug("[RELIABILITY-EVENT] %s source=%s seq=%u neighbor=%s attempt=%u", typeStr, nodeName(evt.source),
                static_cast<unsigned>(evt.sequence), nodeName(evt.neighbor), static_cast<unsigned>(evt.attemptCount));
  telemetry::onReliabilityEvent(evt);
}

// Registers ESP-NOW peers for this node's direct topology neighbors (see
// neighborsOf() in core/node_id.h), skipping any whose MAC hasn't been
// filled in yet. Real MACs don't exist until hardware is flashed — see
// docs/known-issues.md.
void registerConfiguredPeers() {
  uint8_t count = 0;
  const NodeId* neighbors = neighborsOf(THIS_NODE_ID, count);
  static const uint8_t zeroMac[6] = { 0, 0, 0, 0, 0, 0 };

  for (uint8_t i = 0; i < count; i++) {
    const NodeInfo& n = nodeInfo(neighbors[i]);
    if (memcmp(n.mac, zeroMac, 6) == 0) {
      logger::warn("Peer MAC not yet configured for node %s - see docs/known-issues.md", n.name);
      continue;
    }
    transport::addPeer(n.mac);
  }
}

}  // namespace

namespace app {

void setup() {
  logger::begin(SERIAL_BAUD_RATE);
  logger::info("========================================");
  logger::info("Predictive Self-Healing IoT Mesh - Phase 6 firmware");
  logger::info("UCB1 adaptive routing: %s", ENABLE_UCB1 ? "ENABLED" : "disabled (default)");
  logger::info("Node %s initialized (role=%s)", thisNode().name, roleName(thisNode().role));

  // Telemetry initializes before transport so a bootId/HELLO exist before
  // anything that could fail (Part J) - the real MAC genuinely isn't known
  // yet at this point (WiFi.macAddress() needs transport::begin() to have
  // at least set WiFi mode first, matching the existing code below), so
  // HELLO's optional `mac` field is honestly omitted here rather than
  // delayed. See docs/decisions.md.
  telemetry::init(nullptr);

  transport::Status status = transport::begin(onTransportRx, onTransportTx);
  if (status != transport::Status::OK) {
    logger::error("Transport init failed (code=%d) - halting", static_cast<int>(status));
    telemetry::reportError("TRANSPORT_INIT_FAILED", "ESP-NOW/WiFi transport initialization failed", false);
    while (true) {
      delay(1000);  // Phase 0: no recovery strategy yet, fail loud and stop
    }
  }

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[18];
  logger::macToStr(mac, macStr);
  logger::info("Own MAC address: %s (record this in core/node_id.h's NODE_TABLE once hardware exists)", macStr);

  transport::addBroadcastPeer();
  registerConfiguredPeers();

  routing::init();
  routing::setEventCallback(onRouteEvent);
  predictor::init();
  predictor::setEventCallback(onLinkEvent);
  anomaly::init();
  anomaly::setEventCallback(onAnomalyEvent);
  reliability::init();
  reliability::setEventCallback(onReliabilityEvent);
#if ENABLE_UCB1
  ucb1::init();
#endif

  logger::info("Phase 6 firmware ready - entering main loop");
}

void loop() {
  uint32_t now = millis();
  if (now - g_lastAliveLog >= 5000) {
    g_lastAliveLog = now;
    logger::debug("alive uptime_ms=%lu free_heap=%u", static_cast<unsigned long>(now),
                  static_cast<unsigned>(ESP.getFreeHeap()));
  }

  routing::tick();
  predictor::tick();
  anomaly::tick();
  reliability::tick();
  telemetry::tick();

  if (now - g_lastSensorSample >= SENSOR_SAMPLE_INTERVAL_MS) {
    g_lastSensorSample = now;
    anomaly::sample(anomaly::SensorId::POT);
    anomaly::sample(anomaly::SensorId::LDR);
  }

  delay(10);
}

}  // namespace app
