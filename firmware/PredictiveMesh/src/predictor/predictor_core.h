#pragma once
#include <stdint.h>
#include "../core/node_id.h"
#include "../config.h"

// ============================================================
// predictor_core — pure link-degradation predictor algorithm, deliberately
// free of any Arduino/ESP-NOW/Serial dependency (no millis(), no logger::*,
// no transport::*). Mirrors the routing_core / routing split from Phase 1
// (see docs/decisions.md#routing_core-split-out-as-an-arduino-free-pure-module)
// for the same reason: this is real algorithmic math (EWMA, least-squares
// slope, hysteresis state machine) worth verifying on its own, and
// "verified on its own" only means something if it runs outside the ESP32
// toolchain. Every function takes `now` as an explicit uint32_t parameter.
// This is NOT a network/radio simulator - it never fakes RSSI, a packet
// exchange, or send outcomes; it only unit-tests the math
// firmware/PredictiveMesh/test/test_predictor_core.cpp feeds it by hand.
//
// Unlike routing_core (whose one internal constant, MAX_HOP_COUNT, is an
// algorithm-intrinsic safety bound, not a deployment tunable), every
// numeric constant this module uses (EWMA alphas, SLOPE_REF, fusion
// weights, hysteresis thresholds, debounce counts, staleness timeout) is a
// real tuning parameter implementation-guide.html §5.1 and the Phase 2 task
// spec both require to be centralized in one place - so, unlike
// routing_core.h, this header includes ../config.h directly rather than
// duplicating or hardcoding any of them. config.h has no Arduino
// dependency itself, so this doesn't compromise host-testability. See
// docs/decisions.md and docs/parameters.md.
// ============================================================

namespace predictor_core {

// Two-state hysteresis classification (see docs/decisions.md for why two
// states + two thresholds, not the implementation guide's single
// THRESHOLD). A neighbor that has never been observed at all starts
// HEALTHY - an optimistic default that can't cause a false "avoid this
// link" outcome, matching predictor.h's pre-Phase-2 stub behavior of
// always returning a healthy 1.0 score until real evidence says otherwise.
enum class LinkHealth : uint8_t { HEALTHY, UNHEALTHY };

// One EWMA-smoothed-RSSI ring buffer, sized PREDICTOR_SLOPE_WINDOW
// (config.h). Stores rssi_ewma values (NOT raw RSSI - see decisions.md for
// why the slope is fit over the smoothed signal), in a fixed-size ring so
// no dynamic allocation is ever needed for a 5-node embedded target.
struct RssiWindow {
  float samples[PREDICTOR_SLOPE_WINDOW];
  uint8_t count;  // valid entries so far, <= PREDICTOR_SLOPE_WINDOW
  uint8_t next;   // ring-buffer write cursor
};

// Everything predictor_core tracks for one direct neighbor's link. "Direct"
// matters: link_score is always a statement about THIS node's own radio
// link to that neighbor, never a multi-hop/end-to-end quantity - see
// docs/decisions.md for how this composes with routing_core's per-via-
// neighbor candidate table.
struct NeighborLinkState {
  // --- RSSI evidence (Part 1/2/3) ---
  bool everObserved;     // has at least one real RSSI sample arrived?
  int8_t latestRssi;      // most recent raw sample, dBm
  float ewmaRssi;         // current EWMA-smoothed RSSI
  float prevEwmaRssi;     // EWMA value before the latest update (meaningful from the 2nd sample on)
  uint32_t lastUpdateMs;  // last time a fresh RSSI observation arrived - the staleness fast-path's only input
  RssiWindow window;      // feeds the least-squares slope
  float slope;            // last computed least-squares slope, dBm per sample step; negative = degrading
  bool stale;             // true when lastUpdateMs is older than PREDICTOR_STALENESS_TIMEOUT_MS

  // --- PDR evidence (Part 4) ---
  uint16_t pdrAttempts;  // count of real send-outcome observations folded in so far (0 = no data yet)
  float pdrEwma;          // EWMA-smoothed delivery ratio, [0,1]; 1.0 until the first real attempt (documented neutral default, not a real reading)

  // --- Fused score + hysteresis state machine (Part 6/8/9) ---
  float linkScore;      // last computed fused score, [0,1], higher = healthier
  LinkHealth health;     // hysteresis-gated classification consumed by routing
  uint8_t belowCount;    // consecutive evaluations below T_LOW while HEALTHY (Part 9 debounce)
  uint8_t aboveCount;    // consecutive evaluations above T_HIGH while UNHEALTHY (Part 9 debounce)
};

struct PredictorState {
  NodeId self;
  NeighborLinkState neighbors[NODE_ID_COUNT];
};

// What happened during one recompute, for the Arduino adapter to translate
// into routing::RouteEvent-style callbacks (Part 10) without predictor_core
// itself knowing anything about a callback mechanism. `score` is always the
// neighbor's current link_score after this call, valid regardless of which
// flags are set.
struct RecomputeResult {
  bool scoreUpdated;     // true if real evidence was recomputed this call (false during a stale early-return - see docs/decisions.md)
  bool degrading;        // true if HEALTHY but currently sitting below T_HIGH (LINK_DEGRADING)
  bool becameUnhealthy;  // true exactly on a HEALTHY -> UNHEALTHY transition this call (LINK_UNHEALTHY)
  bool becameHealthy;    // true exactly on an UNHEALTHY -> HEALTHY transition this call (LINK_RECOVERED)
  float score;
};

void init(PredictorState& state, NodeId self);

// Feeds one fresh RSSI observation for direct neighbor `from` (mirrors
// routing_core::noteNeighborSeen's call site/trigger - see Part 1: "do not
// duplicate the radio reception mechanism"). Updates the EWMA/slope
// pipeline, then folds the result into link_score/health.
RecomputeResult onRssiSample(PredictorState& state, NodeId from, int8_t rssi, uint32_t now);

// Feeds one real send-outcome observation (success/fail) for `neighbor`
// into the PDR EWMA, then folds the result into link_score/health. Not
// wired to any live caller in Phase 2 - see
// docs/decisions.md#pdr-measurement-boundary-not-wired-to-live-send-outcomes-in-phase-2
// - but the math itself is real and independently tested.
RecomputeResult onSendOutcome(PredictorState& state, NodeId neighbor, bool success, uint32_t now);

// The staleness fast-path (Part 5), independent of any new sample: checks
// whether `neighbor` has gone quiet longer than PREDICTOR_STALENESS_TIMEOUT_MS
// and, if so, forces UNHEALTHY immediately - deliberately bypassing the
// normal consecutive-sample debounce, because there IS no new sample to
// evaluate; that's the whole point of a fast path for sudden silence
// (Part 5's own framing: "slope-only predictor can miss the failure").
// Intended to be called once per neighbor per predictor::tick().
RecomputeResult tickStaleness(PredictorState& state, NodeId neighbor, uint32_t now);

float linkScore(const PredictorState& state, NodeId neighbor);
bool isUnhealthy(const PredictorState& state, NodeId neighbor);
LinkHealth healthState(const PredictorState& state, NodeId neighbor);

// Read-only access to one neighbor's full evidence state, for diagnostic
// logging only (Part 6: "make it obvious which evidence contributed to the
// score"). Falls back to index 0 for an out-of-range/self id, mirroring
// node_id.h's nodeInfo() defensive-fallback convention - callers are
// expected to pass a valid direct-neighbor id.
const NeighborLinkState& linkState(const PredictorState& state, NodeId neighbor);

}  // namespace predictor_core
