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
// ANOMALY (Phase 3)
// ============================================================
// Boot-time calibration sample count - implementation-guide.html §5.2 /
// boot-sequence diagram: "Buffer ~100 raw ADC samples per sensor (boot
// calibration window)".
#define ANOMALY_CALIBRATION_SAMPLE_COUNT 100

// MAD floor, in ADC LSB - guide: "~3 ADC LSB on a 12-bit ADC - real noise
// floor". ESP32 analogRead() defaults to 12-bit (0-4095); see
// anomaly::init()'s explicit analogReadResolution(12) call.
#define ANOMALY_MAD_FLOOR 3.0f

// Modified Z-score flag threshold - Iglewicz & Hoaglin's standard
// recommendation, and the guide's own stated value ("|z| > 3.5").
#define ANOMALY_MODIFIED_Z_THRESHOLD 3.5f

// Flatline "unchanged" tolerance, in ADC LSB. The guide names EPS in its
// pseudocode but gives no numeric value - a starting/placeholder figure,
// chosen just above the expected ADC quantization noise floor so genuine
// channel dither doesn't itself count as "changing" (which would mask a
// truly stuck sensor), while a real slow analog change still registers.
// Expected to be re-tuned once real hardware is available. See
// docs/decisions.md.
#define ANOMALY_FLATLINE_EPS 2.0f

// Consecutive-unchanged-sample count before flagging STUCK - guide:
// "STUCK_N ~ 50 samples".
#define ANOMALY_STUCK_N 50

// Upper bound on raw-sample variance (LSB^2) during boot calibration - the
// guide's own boot-sequence diagram and Q&A describe a "variance within
// safety envelope?" check that restarts calibration if unsafe, but gives
// no numeric envelope. A stable resting analog input's variance is
// dominated by ADC quantization/thermal noise (single-to-low-double-digit
// LSB^2, well under this); a value in the hundreds or more likely means
// active manipulation, a floating/disconnected pin, or interference during
// what's supposed to be a stable baseline window. Starting/placeholder
// figure, expected to be re-tuned once real hardware exists. See
// docs/decisions.md.
#define ANOMALY_MAX_CALIBRATION_VARIANCE 400.0f

// Bounds the guide's "restart calibration" loop, which its own diagram
// draws as an unconditional retry with no escape. An unbounded retry could
// hang boot forever on hardware that's inherently noisy or has a floating/
// unwired sensor pin - past this many failed attempts, calibration
// proceeds anyway using the last (unsafe-but-only-available) sample set,
// loudly logged rather than silently accepted. See docs/decisions.md.
#define ANOMALY_CALIBRATION_MAX_RETRIES 10

// How often each sensor is sampled and evaluated - matches
// implementation-guide.html's stated main-loop cadence ("every evaluation
// cycle (~100-200 ms)").
#define SENSOR_SAMPLE_INTERVAL_MS 150

// Sampling interval used only during the one-time boot calibration window,
// deliberately faster than SENSOR_SAMPLE_INTERVAL_MS. At the steady-state
// rate, buffering ANOMALY_CALIBRATION_SAMPLE_COUNT (100) samples per
// sensor would add ~15 seconds of boot delay per sensor - a real cost for
// a live hackathon demo reboot. 10ms keeps each sensor's calibration to
// roughly 1 second while still taking genuinely time-spaced (not a single
// instant burst) real samples. See docs/decisions.md.
#define ANOMALY_CALIBRATION_SAMPLE_INTERVAL_MS 10

// Consecutive over-threshold samples required before the sensor STATE
// (not the raw per-sample modified-Z evidence, which is always computed
// and reported instantly) actually transitions to ANOMALY. The guide's own
// MAD-Z pseudocode has no such debounce - it's designed to flag a single
// spike immediately - but this phase's task spec explicitly requires that
// no state be classified as failed from one noisy sample. Kept small
// (smaller than the predictor's 3-sample debounce) to stay close to the
// guide's "catch it fast" intent for a genuine spike while still refusing
// to act on a single sample. See docs/decisions.md.
#define ANOMALY_CONSECUTIVE_COUNT 2

// Consecutive under-threshold samples required to recover from ANOMALY
// back to NORMAL. Symmetric with ANOMALY_CONSECUTIVE_COUNT for the same
// "recovering shouldn't be easier to trigger than degrading was to detect"
// reasoning already used for the predictor's hysteresis (Phase 2).
#define ANOMALY_RECOVERY_COUNT 2

// Consecutive non-flat samples required to exit FLATLINE back to NORMAL -
// this phase's task spec explicitly requires that a flatlined sensor not
// instantly report NORMAL from a single changed sample.
#define ANOMALY_FLATLINE_RECOVERY_COUNT 2

// Independent staleness timeout for a sensor observation stream - mirrors
// the predictor's staleness fast-path (Phase 2) applied to sensors instead
// of mesh neighbors. 3x the steady-state sample interval, matching the
// project's established "tolerate a couple of missed samples before
// declaring something down" convention (see ROUTING_ENTRY_TIMEOUT_MS).
// In this phase's actual wiring (a locally, synchronously polled ADC) this
// will rarely if ever fire - it exists because anomaly_core is designed to
// be reusable for a sensor whose observations could genuinely stop
// arriving (e.g. a future relayed/remote sensor), not because local ADC
// polling is expected to go stale. See docs/decisions.md.
#define ANOMALY_STALE_TIMEOUT_MS (3 * SENSOR_SAMPLE_INTERVAL_MS)

// ============================================================
// RELIABILITY (Phase 4)
// ============================================================
// Bounded retransmit ceiling for one hop-transmission (implementation-
// guide.html §5.4: "bounded retransmit", no numeric value given). 3 matches
// this project's already-established "tolerate up to 3" convention
// (PREDICTOR_CONSECUTIVE_BAD_COUNT/GOOD_COUNT, and ROUTING_ENTRY_TIMEOUT_MS's
// "3x the beacon interval" derivation) rather than inventing an unrelated
// number. See docs/decisions.md.
#define RELIABILITY_MAX_RETRIES 3

// How long one hop-transmission attempt waits for its application-level
// MSG_ACK before being declared timed-out (Part 5). No numeric value is
// given by the guide - a starting/placeholder figure, expected to be
// re-tuned once real hardware round-trip timing exists. RELIABILITY_MAX_RETRIES
// bounds RESENDS (beyond the original attempt), so the worst case is
// (1 + RELIABILITY_MAX_RETRIES) attempts = 4 * 200ms = 800ms before a
// hop-transmission is declared FAILED - resolving well before
// PREDICTOR_STALENESS_TIMEOUT_MS (2000ms), so a truly dead link's PDR
// degrades (via real onAckReceived/tickTimeouts failures) before the
// predictor's independent staleness fast-path would otherwise have to
// catch it from silence alone. See docs/decisions.md.
#define RELIABILITY_ACK_TIMEOUT_MS 200

// Fixed-size pool of concurrently-tracked outgoing hop-transmissions (this
// node's own originated sends plus anything it is currently forwarding).
// NODE_ID_COUNT-1 (4) is the maximum number of distinct hop-transmissions
// this 5-node topology could plausibly have in flight from one node at
// once - a defensible fixed bound, no dynamic allocation, matching this
// project's established fixed-array convention (RssiWindow, calBuffer,
// candidates[][]). See docs/decisions.md.
#define RELIABILITY_MAX_PENDING 4

// Duplicate-detection cache size (Part 6) - recently-seen (source,
// sequence) identities. Larger than NODE_ID_COUNT because each of the
// other 4 nodes can have more than one recent sequence in flight
// (original + retries, or original + one in-progress forward). A
// starting/placeholder figure, expected to be re-tuned once real hardware
// traffic patterns exist. See docs/decisions.md.
#define RELIABILITY_DUP_CACHE_SIZE 16

// How long a recorded (source, sequence) identity stays "seen" for
// duplicate-detection purposes. Set comfortably above the worst-case
// in-flight window for one hop-transmission's own retries
// (RELIABILITY_MAX_RETRIES * RELIABILITY_ACK_TIMEOUT_MS = 600ms), so a
// legitimate retransmission of the same packet is still recognized as a
// duplicate for its whole real lifetime, while old entries eventually free
// up for reuse. See docs/decisions.md.
#define RELIABILITY_DUP_CACHE_TTL_MS 2000

// ============================================================
// UCB1 ADAPTIVE ROUTING (Phase 5 — stretch, optional)
// ============================================================
// Compile-time feature flag. implementation-guide.html §06 frames this as
// "[stretch, optional] ... only if ahead of schedule" and explicitly says
// to "gate it behind a compile flag so it can be disabled instantly if
// unstable." 0 (disabled) is the required default — with it at 0, every
// call site that would touch UCB1 is compiled out entirely (not just
// skipped at runtime), so Phase 1/2/4's routing/reliability behavior is
// byte-for-byte what it was before this phase. Flip to 1 to compile UCB1
// in. See docs/decisions.md.
#define ENABLE_UCB1 0

// UCB1 exploration coefficient (the `C` in `meanReward + C * sqrt(ln(N)/n)`).
// implementation-guide.html names UCB1 only as a stretch-phase label with
// no formula or coefficient given. `sqrt(2)` is the standard textbook
// value (Auer, Cesa-Bianchi & Fischer 2002's original UCB1 derivation,
// also the framing of this project's own cited reference [10]) — used
// as-is rather than inventing a different constant with no basis. See
// docs/decisions.md.
#define UCB1_EXPLORATION_C 1.41421356f

// ============================================================
// TELEMETRY (Phase 6 — firmware<->GUI wire serialization)
// ============================================================
// Firmware version string reported in HELLO/NODE_STATUS. No release/version
// process existed anywhere in this project before this phase (see
// docs/known-issues.md's pre-Phase-6 "fields with no firmware source"
// note) - introduced here specifically because the frozen GUI contract
// requires it as a non-empty string. Bump by convention only; not tied to
// git tags or any external process.
#define FIRMWARE_VERSION "1.0.0"

// Telemetry envelope emission cadence, one constant per message type,
// matching gui-main/gui-main/docs/gui-telemetry-contract.md's own "Exact
// frequency" column exactly (not invented - the frozen contract already
// specifies these numbers, so they're reproduced here, not re-derived).
// HELLO fires once at boot (+ once after reconnect - N/A over a plain
// Serial UART with no reconnect handshake, so just once at boot here);
// EVENT/ERROR are purely event-driven (Part 10/Part N), no interval.
#define TELEMETRY_HEARTBEAT_INTERVAL_MS 1000
#define TELEMETRY_NODE_STATUS_INTERVAL_MS 1000
#define TELEMETRY_LINK_INTERVAL_MS 250
#define TELEMETRY_PREDICTION_INTERVAL_MS 250
#define TELEMETRY_SENSOR_INTERVAL_MS 1000
#define TELEMETRY_STATISTICS_INTERVAL_MS 1000

// HELLO's config.offlineTimeoutMs - the GUI's own client-side staleness
// clock (mesh-command-console.html's refreshFirmwareStaleness()), driven
// entirely by this declared value, independent of any firmware-side
// timeout. 3x the heartbeat interval, matching this project's established
// "tolerate a couple of missed beacons" convention (see
// ROUTING_ENTRY_TIMEOUT_MS/PREDICTOR_STALENESS_TIMEOUT_MS) - a new,
// telemetry-specific constant rather than reusing ROUTING_ENTRY_TIMEOUT_MS,
// since it answers a different question (when should the GUI distrust this
// node's telemetry) even though the two happen to share a derivation.
#define TELEMETRY_OFFLINE_TIMEOUT_MS (3 * TELEMETRY_HEARTBEAT_INTERVAL_MS)

// ============================================================
// APPLICATION TRAFFIC (Phase 7 — demo workload for reliability::send())
// ============================================================
// Periodic interval between application DATA packets NODE_A sends to
// NODE_S. No numeric value is given by implementation-guide.html or any
// existing spec (see docs/decisions.md — this whole module resolves a gap
// Phase 4 explicitly declined to guess at) - a starting/placeholder
// figure, chosen deliberately relative to timing this project has already
// established rather than picked arbitrarily: comfortably above one
// hop-transmission's own worst-case retry window
// ((1 + RELIABILITY_MAX_RETRIES) * RELIABILITY_ACK_TIMEOUT_MS = 800ms), so
// in the common case a new send never overlaps the previous series' own
// retries; well under a level that would compete for airtime with
// ROUTING_HELLO_INTERVAL_MS's (1000ms) beacon traffic every single cycle;
// and still frequent enough to give PREDICTOR_PDR_EWMA_ALPHA's ~20-sample
// window, and the GUI's STATISTICS panel, real, visibly-moving numbers
// over a multi-minute demo. See docs/decisions.md.
#define APPLICATION_TX_INTERVAL_MS 2000

// ============================================================
// SERIAL
// ============================================================
#define SERIAL_BAUD_RATE 115200

// Convenience accessor for this board's own node metadata.
inline const NodeInfo& thisNode() { return nodeInfo(THIS_NODE_ID); }
