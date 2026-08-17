#include "predictor.h"
#include "predictor_core.h"
#include "../config.h"
#include "../core/logger.h"
#include <Arduino.h>

namespace {

predictor_core::PredictorState g_state;
predictor::LinkEventCallback g_eventCallback = nullptr;

void fireEvent(predictor::LinkEventType type, NodeId neighbor, float score) {
  if (g_eventCallback == nullptr) return;
  predictor::LinkEvent evt{ type, neighbor, score };
  g_eventCallback(evt);
}

// Logs the evidence behind the latest score (Part 6: "make it obvious
// which evidence contributed to the score") and translates a
// predictor_core::RecomputeResult into the ordered event callbacks Part 10
// asks for: LINK_SCORE_UPDATED first (if real evidence changed this
// call), then LINK_DEGRADING (soft warning), then whichever hysteresis
// transition (if any) actually happened.
void handleResult(NodeId neighbor, const predictor_core::RecomputeResult& r) {
  if (r.scoreUpdated) {
    const predictor_core::NeighborLinkState& n = predictor_core::linkState(g_state, neighbor);
    logger::debug("[PREDICTOR] neighbor=%s rssi_ewma=%.2f slope=%.3f pdr=%.2f score=%.2f health=%s",
                  nodeName(neighbor), n.ewmaRssi, n.slope, n.pdrEwma, n.linkScore,
                  n.health == predictor_core::LinkHealth::HEALTHY ? "HEALTHY" : "UNHEALTHY");
    fireEvent(predictor::LinkEventType::LINK_SCORE_UPDATED, neighbor, r.score);
  }
  if (r.degrading) {
    fireEvent(predictor::LinkEventType::LINK_DEGRADING, neighbor, r.score);
  }
  if (r.becameUnhealthy) {
    logger::warn("[PREDICTOR] neighbor=%s link now UNHEALTHY (score=%.2f)", nodeName(neighbor), r.score);
    fireEvent(predictor::LinkEventType::LINK_UNHEALTHY, neighbor, r.score);
  }
  if (r.becameHealthy) {
    logger::info("[PREDICTOR] neighbor=%s link RECOVERED (score=%.2f)", nodeName(neighbor), r.score);
    fireEvent(predictor::LinkEventType::LINK_RECOVERED, neighbor, r.score);
  }
}

}  // namespace

namespace predictor {

void init() {
  predictor_core::init(g_state, THIS_NODE_ID);
  logger::info("predictor: init (RSSI EWMA/slope + PDR + staleness fusion, Phase 2)");
}

void onPacketReceived(const MeshPacket& pkt, int8_t rssi) {
  NodeId from = static_cast<NodeId>(pkt.prev_hop);
  predictor_core::RecomputeResult r = predictor_core::onRssiSample(g_state, from, rssi, millis());
  handleResult(from, r);
}

void onSendResult(NodeId neighbor, bool success) {
  predictor_core::RecomputeResult r = predictor_core::onSendOutcome(g_state, neighbor, success, millis());
  handleResult(neighbor, r);
}

void tick() {
  uint32_t now = millis();
  for (uint8_t n = 0; n < NODE_ID_COUNT; n++) {
    if (n == THIS_NODE_ID) continue;
    predictor_core::RecomputeResult r = predictor_core::tickStaleness(g_state, static_cast<NodeId>(n), now);
    if (r.becameUnhealthy) {
      logger::warn("[PREDICTOR] neighbor=%s gone stale - link forced UNHEALTHY", nodeName(static_cast<NodeId>(n)));
      fireEvent(LinkEventType::LINK_UNHEALTHY, static_cast<NodeId>(n), r.score);
    }
  }
}

float linkScore(NodeId neighbor) {
  return predictor_core::linkScore(g_state, neighbor);
}

bool isUnhealthy(NodeId neighbor) {
  return predictor_core::isUnhealthy(g_state, neighbor);
}

const predictor_core::NeighborLinkState& linkState(NodeId neighbor) {
  return predictor_core::linkState(g_state, neighbor);
}

void setEventCallback(LinkEventCallback cb) {
  g_eventCallback = cb;
}

}  // namespace predictor
