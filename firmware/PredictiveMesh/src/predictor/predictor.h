#pragma once
#include "../core/node_id.h"
#include "../core/packet.h"
#include "predictor_core.h"

// ============================================================
// Link degradation predictor (implementation-guide.html §5.1) - Arduino-
// facing adapter, Phase 2.
//
// Owns the single predictor_core::PredictorState instance, feeds it real
// RSSI samples (from received packets, mirroring routing.cpp's own
// onPacketReceived hook) and drives its periodic staleness fast-path from
// tick(). The actual EWMA/slope/PDR/hysteresis math lives in
// predictor_core.h/.cpp (Arduino-free, host-testable) - see
// docs/decisions.md, matching the routing_core/routing split from Phase 1.
// ============================================================

namespace predictor {

enum class LinkEventType : uint8_t { LINK_SCORE_UPDATED, LINK_DEGRADING, LINK_UNHEALTHY, LINK_RECOVERED };

struct LinkEvent {
  LinkEventType type;
  NodeId neighbor;
  float score;
};

typedef void (*LinkEventCallback)(const LinkEvent& event);

void init();

// Feeds a real RSSI observation into the predictor for pkt.prev_hop.
// Called from the same receive dispatch point as routing::onPacketReceived
// (see main.cpp) - does not register its own transport callback, per Part
// 1's "do not duplicate the radio reception mechanism."
void onPacketReceived(const MeshPacket& pkt, int8_t rssi);

// Feeds a real send-outcome observation into the PDR evidence stream for
// `neighbor`. A real, tested entry point with no live caller yet in Phase
// 2 - see
// docs/decisions.md#pdr-measurement-boundary-not-wired-to-live-send-outcomes-in-phase-2.
void onSendResult(NodeId neighbor, bool success);

// Drives the independent staleness fast-path (Part 5) for every direct
// neighbor. Call once per app::loop() iteration, alongside routing::tick().
void tick();

// Returns the current link_score for the link to `neighbor`, in [0, 1],
// higher = healthier. Real as of Phase 2 (was a Phase 0/1 stub always
// returning 1.0).
float linkScore(NodeId neighbor);

// True if `neighbor`'s link is currently classified UNHEALTHY by the
// hysteresis state machine. Consumed by routing::getNextHop() to build the
// health mask passed into routing_core::selectNextHop() - see
// docs/decisions.md for how link health integrates with Phase 1's routing
// table without replacing it.
bool isUnhealthy(NodeId neighbor);

// Read-only access to one neighbor's full evidence state (RSSI/EWMA/slope/
// PDR/hysteresis-debounce counters) - a thin pass-through to
// predictor_core::linkState() for a consumer that needs more than the
// single fused score (Phase 6 telemetry's LINK_UPDATE/PREDICTION payloads).
// Mirrors anomaly::getTelemetry()'s existing read-only-snapshot pattern.
const predictor_core::NeighborLinkState& linkState(NodeId neighbor);

void setEventCallback(LinkEventCallback cb);

}  // namespace predictor
