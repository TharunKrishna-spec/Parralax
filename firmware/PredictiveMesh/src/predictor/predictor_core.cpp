#include "predictor_core.h"

namespace predictor_core {

namespace {

float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void pushWindow(RssiWindow& w, float value) {
  w.samples[w.next] = value;
  w.next = static_cast<uint8_t>((w.next + 1) % PREDICTOR_SLOPE_WINDOW);
  if (w.count < PREDICTOR_SLOPE_WINDOW) w.count++;
}

// Least-squares slope of the window's values against sample index
// (0..count-1, oldest to newest) - see docs/decisions.md for why sample
// index is used instead of real timestamps (samples arrive on a fixed
// beacon cadence, so this is a deliberate simplification, not an
// oversight). Returns 0 when fewer than 2 samples exist - "not enough
// evidence yet" is reported honestly as no trend, never fabricated.
float leastSquaresSlope(const RssiWindow& w) {
  uint8_t n = w.count;
  if (n < 2) return 0.0f;

  float sumX = 0.0f, sumY = 0.0f, sumXY = 0.0f, sumX2 = 0.0f;
  for (uint8_t i = 0; i < n; i++) {
    // Chronological (oldest-to-newest) index: while the ring hasn't
    // wrapped yet (n < PREDICTOR_SLOPE_WINDOW), storage order already IS
    // chronological order; once wrapped, the oldest entry sits at `next`.
    uint8_t idx = (n < PREDICTOR_SLOPE_WINDOW) ? i
                                                : static_cast<uint8_t>((w.next + i) % PREDICTOR_SLOPE_WINDOW);
    float x = static_cast<float>(i);
    float y = w.samples[idx];
    sumX += x;
    sumY += y;
    sumXY += x * y;
    sumX2 += x * x;
  }

  float denom = static_cast<float>(n) * sumX2 - sumX * sumX;
  if (denom == 0.0f) return 0.0f;  // degenerate (shouldn't happen for n >= 2 with unit-spaced x), guard anyway
  return (static_cast<float>(n) * sumXY - sumX * sumY) / denom;
}

// Forces UNHEALTHY the instant staleness is newly detected, bypassing the
// normal debounce counters entirely (see predictor_core.h). Returns true
// only on a fresh HEALTHY -> UNHEALTHY transition caused by staleness, so
// callers don't re-fire LINK_UNHEALTHY every call while remaining stale.
bool applyStalenessCheck(NeighborLinkState& n, uint32_t now) {
  n.stale = n.everObserved && (now - n.lastUpdateMs) > PREDICTOR_STALENESS_TIMEOUT_MS;
  if (!n.stale) return false;
  if (n.health == LinkHealth::HEALTHY) {
    n.health = LinkHealth::UNHEALTHY;
    n.belowCount = 0;
    n.aboveCount = 0;
    return true;
  }
  return false;
}

// Recomputes slope -> degrade_term -> link_score -> hysteresis/debounce
// from the neighbor's current evidence. Shared by onRssiSample and
// onSendOutcome so both evidence channels feed the exact same fusion and
// state machine. Assumes the caller has already applied any relevant
// staleness check; if the neighbor is (still or newly) stale, this skips
// straight to reporting that instead of computing a score from stale data.
RecomputeResult recomputeLocked(NeighborLinkState& n, uint32_t now, bool justWentStale) {
  RecomputeResult result{};

  if (n.stale) {
    result.scoreUpdated = false;
    result.becameUnhealthy = justWentStale;
    result.score = n.linkScore;
    return result;
  }

  n.slope = leastSquaresSlope(n.window);
  float degradeTerm = clampf(-n.slope / PREDICTOR_SLOPE_REF_DBM_PER_SAMPLE, 0.0f, 1.0f);
  n.linkScore = PREDICTOR_LINK_SCORE_W1 * (1.0f - degradeTerm) + PREDICTOR_LINK_SCORE_W2 * n.pdrEwma;

  if (n.health == LinkHealth::HEALTHY) {
    if (n.linkScore < PREDICTOR_HYSTERESIS_T_LOW) {
      n.belowCount++;
      if (n.belowCount >= PREDICTOR_CONSECUTIVE_BAD_COUNT) {
        n.health = LinkHealth::UNHEALTHY;
        n.belowCount = 0;
        n.aboveCount = 0;
        result.becameUnhealthy = true;
      }
    } else {
      n.belowCount = 0;
    }
    result.degrading = (n.health == LinkHealth::HEALTHY) && (n.linkScore < PREDICTOR_HYSTERESIS_T_HIGH);
  } else {  // UNHEALTHY
    if (n.linkScore > PREDICTOR_HYSTERESIS_T_HIGH) {
      n.aboveCount++;
      if (n.aboveCount >= PREDICTOR_CONSECUTIVE_GOOD_COUNT) {
        n.health = LinkHealth::HEALTHY;
        n.aboveCount = 0;
        n.belowCount = 0;
        result.becameHealthy = true;
      }
    } else {
      n.aboveCount = 0;
    }
  }

  result.scoreUpdated = true;
  result.score = n.linkScore;
  (void)now;
  return result;
}

}  // namespace

void init(PredictorState& state, NodeId self) {
  state.self = self;
  for (uint8_t i = 0; i < NODE_ID_COUNT; i++) {
    NeighborLinkState& n = state.neighbors[i];
    n.everObserved = false;
    n.latestRssi = 0;
    n.ewmaRssi = 0.0f;
    n.prevEwmaRssi = 0.0f;
    n.lastUpdateMs = 0;
    n.window = RssiWindow{};
    n.slope = 0.0f;
    n.stale = false;
    n.pdrAttempts = 0;
    n.pdrEwma = 1.0f;       // neutral "no data yet" default, not a real reading - see predictor_core.h
    n.linkScore = 1.0f;     // matches the pre-Phase-2 stub's "always healthy until proven otherwise"
    n.health = LinkHealth::HEALTHY;
    n.belowCount = 0;
    n.aboveCount = 0;
  }
}

RecomputeResult onRssiSample(PredictorState& state, NodeId from, int8_t rssi, uint32_t now) {
  if (from >= NODE_ID_COUNT || from == state.self) return RecomputeResult{};
  NeighborLinkState& n = state.neighbors[from];

  n.latestRssi = rssi;
  n.prevEwmaRssi = n.ewmaRssi;  // meaningful from the 2nd sample on - see predictor_core.h
  if (!n.everObserved) {
    n.ewmaRssi = static_cast<float>(rssi);  // bootstrap with the first real sample, never a fabricated default
    n.everObserved = true;
  } else {
    n.ewmaRssi = PREDICTOR_RSSI_EWMA_ALPHA * static_cast<float>(rssi) + (1.0f - PREDICTOR_RSSI_EWMA_ALPHA) * n.ewmaRssi;
  }
  n.lastUpdateMs = now;
  pushWindow(n.window, n.ewmaRssi);

  // A fresh sample can never itself be "stale" (now - lastUpdateMs == 0
  // immediately above), so this only ever clears a previously-stale state.
  n.stale = false;
  return recomputeLocked(n, now, /*justWentStale=*/false);
}

RecomputeResult onSendOutcome(PredictorState& state, NodeId neighbor, bool success, uint32_t now) {
  if (neighbor >= NODE_ID_COUNT || neighbor == state.self) return RecomputeResult{};
  NeighborLinkState& n = state.neighbors[neighbor];

  float outcome = success ? 1.0f : 0.0f;
  if (n.pdrAttempts == 0) {
    n.pdrEwma = outcome;  // bootstrap with the first real attempt, same principle as RSSI EWMA
  } else {
    n.pdrEwma = PREDICTOR_PDR_EWMA_ALPHA * outcome + (1.0f - PREDICTOR_PDR_EWMA_ALPHA) * n.pdrEwma;
  }
  if (n.pdrAttempts < UINT16_MAX) n.pdrAttempts++;

  bool justWentStale = applyStalenessCheck(n, now);
  return recomputeLocked(n, now, justWentStale);
}

RecomputeResult tickStaleness(PredictorState& state, NodeId neighbor, uint32_t now) {
  if (neighbor >= NODE_ID_COUNT || neighbor == state.self) return RecomputeResult{};
  NeighborLinkState& n = state.neighbors[neighbor];

  bool wasStale = n.stale;
  bool justWentStale = applyStalenessCheck(n, now);

  RecomputeResult result{};
  if (justWentStale) {
    result.becameUnhealthy = true;
    result.score = n.linkScore;
  } else if (!n.stale && wasStale) {
    // Timestamp-wise no longer stale (shouldn't normally happen without a
    // fresh sample, since lastUpdateMs doesn't move on its own - guarded
    // for correctness/documentation, not a path expected in practice).
    result.score = n.linkScore;
  } else {
    result.score = n.linkScore;
  }
  return result;
}

float linkScore(const PredictorState& state, NodeId neighbor) {
  if (neighbor >= NODE_ID_COUNT || neighbor == state.self) return 1.0f;
  return state.neighbors[neighbor].linkScore;
}

bool isUnhealthy(const PredictorState& state, NodeId neighbor) {
  if (neighbor >= NODE_ID_COUNT || neighbor == state.self) return false;
  return state.neighbors[neighbor].health == LinkHealth::UNHEALTHY;
}

LinkHealth healthState(const PredictorState& state, NodeId neighbor) {
  if (neighbor >= NODE_ID_COUNT || neighbor == state.self) return LinkHealth::HEALTHY;
  return state.neighbors[neighbor].health;
}

const NeighborLinkState& linkState(const PredictorState& state, NodeId neighbor) {
  if (neighbor >= NODE_ID_COUNT || neighbor == state.self) return state.neighbors[0];
  return state.neighbors[neighbor];
}

}  // namespace predictor_core
