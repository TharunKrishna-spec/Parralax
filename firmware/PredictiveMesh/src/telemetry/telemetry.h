#pragma once
#include <stdint.h>
#include "../routing/routing.h"
#include "../predictor/predictor.h"
#include "../anomaly/anomaly.h"
#include "../reliability/reliability.h"

// ============================================================
// Reporting layer (implementation-guide.html §01, §07 "Reporting Layer") —
// Arduino-facing adapter, Phase 6.
//
// Real as of Phase 6: serializes the frozen firmware<->GUI contract
// (gui-main/gui-main/docs/gui-telemetry-contract.md, "mesh-json/v1") to
// Serial. Reads authoritative state from routing/predictor/anomaly/
// reliability via their existing read-only accessors and their existing
// event-callback mechanism — none of those four modules (or ucb1) knows
// telemetry/GUI/JSON exists, satisfying Part H's layering requirement.
// The actual JSON string construction lives in telemetry_core.h/.cpp
// (Arduino-free, host-testable) — see docs/decisions.md, matching the
// pure-core/adapter split used by every other layer since Phase 1.
//
// Still NOT implemented: OLED wiring (deferred since Phase 0, unrelated to
// this phase). UCB1 has no dedicated message in the frozen contract and is
// not separately serialized — see docs/decisions.md.
// ============================================================

namespace telemetry {

void init(const uint8_t mac[6]);

// Call from the main loop on every iteration; rate-limits itself per
// config.h's TELEMETRY_*_INTERVAL_MS constants (Part N).
void tick();

// Event-forwarding hooks. Each source module supports exactly one
// registered callback (see e.g. routing.h's setEventCallback doc comment) —
// main.cpp's existing single callback for each module calls these
// alongside its existing logger::debug() line, rather than telemetry
// registering a second, competing callback. See docs/decisions.md.
void onRouteEvent(const routing::RouteEvent& evt);
void onLinkEvent(const predictor::LinkEvent& evt);
void onAnomalyEvent(const anomaly::AnomalyEvent& evt);
void onReliabilityEvent(const reliability::ReliabilityEvent& evt);

// Emits a real ERROR message (Part J: no fabricated error taxonomy) for a
// genuine firmware fault. `code` and `message` must be fixed string
// literals (see telemetry_core.h's file header — no string escaping is
// implemented). Currently only called from main.cpp's transport::begin()
// failure path.
void reportError(const char* code, const char* message, bool recoverable);

}  // namespace telemetry
