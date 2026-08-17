#pragma once

// ============================================================
// OLED adapter — local display, Nodes S and C only
// (core/node_id.h::NodeInfo::hasOled). Thin Arduino-facing layer over
// oled_core's pure screen-scheduling logic: owns the real display object,
// the I2C bus, and every Adafruit_GFX drawing call.
//
// implementation-guide.html §03's BOM/role table gives S and C genuinely
// different OLED content, not one shared layout:
//   - Node S: "mesh telemetry (reroute events, link scores)" / bring-up
//     checklist: "Node S OLED ... show live link_score for the current
//     best path".
//   - Node C: "local anomaly flag" / "SPIKE/JUMP vs STUCK, shown
//     independently" / checklist: "Node C OLED is blank/idle ... at rest".
// oled.cpp implements that split directly rather than reusing one generic
// 4-screen layout on both nodes. See docs/decisions.md.
//
// Safety: init()/tick() are the ONLY entry points, both called exclusively
// from app::setup()/app::loop() — never from inside the ESP-NOW receive
// callback, an ACK callback, or a routing callback. tick() reads already-
// computed state via existing side-effect-free accessors
// (predictor::linkState()/isUnhealthy(), anomaly::getTelemetry()) and its
// own internal edge-detection; it never calls a mutating/event-emitting
// function like routing::getNextHop()/selectNextHop(). Every real I2C push
// is rate-limited by oled_core (OLED_REFRESH_MIN_INTERVAL_MS), so a slow
// I2C transaction happens at most once per that interval, from loop()
// only — it can never stall the radio path.
// ============================================================

namespace oled {

// No-ops (after a single [OLED] log line) on any node whose
// core/node_id.h::NodeInfo::hasOled is false. On a node that does have an
// OLED, a display.begin() failure is logged and handled the same way — a
// real hardware fault here degrades to "no local display", never a
// blocking for(;;) hang like the hardware team's own bench sketches.
void init();

// Call once per app::loop() iteration, alongside the other module ticks.
// A single boolean check and an immediate return on any node without an
// active display.
void tick();

}  // namespace oled
