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
// PREDICTOR (Phase 2)
// ============================================================
// RSSI EWMA smoothing factor. Higher = more reactive to each new sample,
// lower = smoother/slower. Per implementation-guide.html §5.1 ("alpha ~
// 0.3") - used exactly as given, not re-derived.
#define PREDICTOR_RSSI_EWMA_ALPHA 0.3f

// Number of EWMA-smoothed RSSI samples the least-squares slope is fit
// over. implementation-guide.html's own reference value (15-20 samples,
// ~2-4s) assumes a 100-200ms heartbeat. Phase 2 deliberately reuses the
// existing ~1s distance-vector beacon (ROUTING_HELLO_INTERVAL_MS) as its
// RSSI sample source instead of adding a second, faster wire message (Part
// 1 of the Phase 2 spec: "do not duplicate the radio reception
// mechanism") - so the window is scaled down to keep the real-world
// reaction time in the same single-digit-second range the guide intends,
// rather than literally reusing "15-20" samples at ~1 Hz (15-20 seconds -
// far slower than the guide's own intent). See docs/decisions.md.
#define PREDICTOR_SLOPE_WINDOW 8

// SLOPE_REF from implementation-guide.html's degrade_term formula
// (degrade_term = clamp(-slope/SLOPE_REF, 0, 1)). The guide names this
// constant but gives no starting numeric value. Chosen here so a
// sustained ~1.5 dBm-per-sample downward trend fully saturates
// degrade_term to 1.0 - a starting/placeholder figure, expected to be
// re-tuned once real hardware attenuation testing (the guide's own
// "Faraday bag on Node B" demo, §06) is possible. See docs/decisions.md.
#define PREDICTOR_SLOPE_REF_DBM_PER_SAMPLE 1.5f

// link_score fusion weights - per implementation-guide.html §5.1 exactly
// ("w1 = w2 = 0.5 to start").
#define PREDICTOR_LINK_SCORE_W1 0.5f
#define PREDICTOR_LINK_SCORE_W2 0.5f

// PDR EWMA smoothing factor, derived (not guessed) from
// implementation-guide.html's stated 20-frame PDR window using the
// standard EWMA/simple-moving-average equivalence alpha = 2/(N+1):
// 2/(20+1) ~= 0.0952, rounded to 0.1 for a clean, documented constant. See
// docs/decisions.md.
#define PREDICTOR_PDR_EWMA_ALPHA 0.1f

// Hysteresis thresholds on link_score [0,1] (higher = healthier). A single
// threshold, as implementation-guide.html's own pseudocode uses, would let
// a score oscillating right around the cutoff flap the routing decision
// back and forth - the Phase 2 task spec explicitly requires two
// thresholds instead. T_LOW plays the role the guide calls THRESHOLD;
// T_HIGH is new, requiring the score to clear a meaningfully higher bar
// before a link is trusted as healthy again. See docs/decisions.md.
#define PREDICTOR_HYSTERESIS_T_LOW 0.5f
#define PREDICTOR_HYSTERESIS_T_HIGH 0.7f

// Consecutive-evaluation debounce, per implementation-guide.html §5.1's own
// pseudocode ("reroute if below threshold for 3 consecutive evaluations").
// Applied symmetrically to the recovery direction (T_HIGH) too - the guide
// only specifies the degrade direction, but nothing suggests recovering
// should be easier to trigger than degrading was to detect, so the same
// count is reused rather than inventing an unstated asymmetric value.
#define PREDICTOR_CONSECUTIVE_BAD_COUNT 3
#define PREDICTOR_CONSECUTIVE_GOOD_COUNT 3

// Independent staleness fast-path timeout - deliberately faster than
// ROUTING_ENTRY_TIMEOUT_MS (3000ms, 3x the beacon interval) so the
// predictor's silence-detection can flag a dying link BEFORE routing's own
// hard fallback expires it, matching implementation-guide.html's stated
// intent ("heartbeat timeout stays armed regardless, as a hard fallback" -
// implying the proactive path should normally act first). 2x the beacon
// interval - one less than routing's 3x - is the smallest change that
// keeps the same "tolerate one dropped beacon, not two" derivation while
// staying strictly faster than ROUTING_ENTRY_TIMEOUT_MS. See
// docs/decisions.md and docs/parameters.md for the full relationship
// between HELLO interval / route timeout / predictor staleness timeout.
#define PREDICTOR_STALENESS_TIMEOUT_MS 2000

// ============================================================
// SERIAL
// ============================================================
#define SERIAL_BAUD_RATE 115200

// Convenience accessor for this board's own node metadata.
inline const NodeInfo& thisNode() { return nodeInfo(THIS_NODE_ID); }
