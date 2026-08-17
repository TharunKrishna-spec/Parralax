#pragma once
#include "core/node_id.h"

// ============================================================
// NODE IDENTITY
//
// Set to exactly the role this physical board should run, then compile and
// flash. This is meant to be the ONLY line that differs between the five
// boards' compiled images — see docs/decisions.md for why a compile-time
// define was chosen (of the two options implementation-guide.html §04
// allows: "compile-time flag or a MAC-address lookup table") over runtime
// MAC-address auto-detection for Phase 0.
//
// Valid values: NODE_A, NODE_B, NODE_C, NODE_D, NODE_S
// ============================================================
#define THIS_NODE_ID NODE_S

// ============================================================
// RADIO
// ============================================================
// All five nodes MUST use the same WiFi channel — ESP-NOW peers can only
// hear each other while on matching channels, and nothing at runtime
// negotiates or verifies this. One centralized value, documented in
// docs/parameters.md, referenced everywhere instead of hardcoded per file.
#define MESH_WIFI_CHANNEL 6

// ============================================================
// HARDWARE PINS
// Fixed hardware contract from implementation-guide.html §03. Full
// rationale (ADC1 vs ADC2, strapping pins, etc.) in docs/parameters.md.
// ============================================================
#define PIN_SENSOR_POT 34    // ADC1_CH6 — potentiometer wiper (Channel A). Input-only pin, ADC1 only (WiFi/ESP-NOW kills ADC2).
#define PIN_SENSOR_LDR 35    // ADC1_CH7 — LDR divider midpoint (Channel B). Input-only pin, ADC1 only.
#define PIN_BUZZER 25        // digital out — piezo buzzer signal. Never 34/35/36/39 (input-only, can't drive an output).
#define PIN_OLED_SDA 21      // I2C SDA — default ESP32 pin. Nodes S and C only.
#define PIN_OLED_SCL 22      // I2C SCL — default ESP32 pin. Nodes S and C only.
#define OLED_I2C_ADDRESS 0x3C

// ============================================================
// ROUTING (Phase 1)
// ============================================================
// How often each node broadcasts its distance-vector beacon (HELLO +
// route advertisement combined - see src/routing/routing.cpp). 1 second
// is fast enough for a 5-node static topology to converge in a couple of
// beacon cycles, slow enough not to spam the channel/log. Deliberately
// decoupled from the *predictor's* future heartbeat cadence (100-200ms,
// documented in docs/parameters.md) - routing convergence doesn't need
// that resolution, and reusing one constant would prematurely couple two
// layers' timing before the predictor layer exists. See docs/decisions.md.
#define ROUTING_HELLO_INTERVAL_MS 1000

// How long a neighbor or route candidate can go without being refreshed
// before it's treated as stale and invalidated. Both neighbor liveness and
// route freshness are learned from the same beacon (see
// ROUTING_HELLO_INTERVAL_MS), so one shared timeout keeps the model
// simple. Set to 3x the beacon interval - tolerates a couple of dropped
// beacons (real radio conditions, not just clean delivery) before
// declaring a link down, matching the "3-5x reaction time" safety-net
// convention already used for the predictor's future heartbeat timeout in
// docs/parameters.md.
#define ROUTING_ENTRY_TIMEOUT_MS 3000

// ============================================================
// SERIAL
// ============================================================
#define SERIAL_BAUD_RATE 115200

// Convenience accessor for this board's own node metadata.
inline const NodeInfo& thisNode() { return nodeInfo(THIS_NODE_ID); }
