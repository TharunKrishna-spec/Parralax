#include "ucb1_core.h"
#include <string.h>
#include <math.h>

namespace ucb1_core {

namespace {

void saturatingIncrement(uint32_t& counter) {
  if (counter < UINT32_MAX) counter++;
}

}  // namespace

void init(Ucb1State& state) {
  memset(&state, 0, sizeof(Ucb1State));
}

void recordOutcome(Ucb1State& state, NodeId destination, NodeId nextHop, bool success) {
  if (destination >= NODE_ID_COUNT || nextHop >= NODE_ID_COUNT) return;

  ArmStats& arm = state.arms[destination][nextHop];
  arm.everObserved = true;
  saturatingIncrement(arm.attempts);
  if (success) {
    saturatingIncrement(arm.successes);
  } else {
    saturatingIncrement(arm.failures);
  }
}

NodeId selectNextHop(const Ucb1State& state, NodeId destination, const Candidate* candidates,
                      uint8_t count, NodeId excludeNextHop) {
  if (destination >= NODE_ID_COUNT) return NODE_ID_UNKNOWN;

  // Step 1 (Part 8): exclude the loop-guard candidate; step 2 (Part 6):
  // prefer the healthy subset if any exists, matching routing_core's own
  // healthy-preferred/any-fallback philosophy.
  uint8_t eligible[NODE_ID_COUNT];
  uint8_t eligibleCount = 0;
  bool anyHealthy = false;

  for (uint8_t i = 0; i < count && eligibleCount < NODE_ID_COUNT; i++) {
    if (candidates[i].nextHop == excludeNextHop) continue;
    eligible[eligibleCount++] = i;
    if (candidates[i].healthy) anyHealthy = true;
  }
  if (eligibleCount == 0) return NODE_ID_UNKNOWN;

  if (anyHealthy) {
    uint8_t healthyOnly[NODE_ID_COUNT];
    uint8_t healthyCount = 0;
    for (uint8_t j = 0; j < eligibleCount; j++) {
      if (candidates[eligible[j]].healthy) healthyOnly[healthyCount++] = eligible[j];
    }
    memcpy(eligible, healthyOnly, healthyCount * sizeof(uint8_t));
    eligibleCount = healthyCount;
  }

  // Step 3 (Part 3): any never-observed candidate gets automatic priority.
  for (uint8_t j = 0; j < eligibleCount; j++) {
    NodeId nh = candidates[eligible[j]].nextHop;
    if (state.arms[destination][nh].attempts == 0) return nh;
  }

  // Step 4: N = total trials across the candidate set actually being
  // ranked right now (not a stale/cached total, and not candidates that
  // were just excluded/filtered out — see docs/decisions.md). Every
  // eligible candidate has attempts > 0 here (step 3 already returned
  // otherwise), so N > 0 and ln(N) is always well-defined — no undefined
  // logarithm is possible (Part 3).
  uint32_t N = 0;
  for (uint8_t j = 0; j < eligibleCount; j++) {
    N += state.arms[destination][candidates[eligible[j]].nextHop].attempts;
  }
  if (N == 0) return candidates[eligible[0]].nextHop;  // unreachable given step 3, kept as a defensive guard (Part 3)

  NodeId best = NODE_ID_UNKNOWN;
  float bestScore = -1.0f;
  for (uint8_t j = 0; j < eligibleCount; j++) {
    NodeId nh = candidates[eligible[j]].nextHop;
    const ArmStats& arm = state.arms[destination][nh];
    float meanReward = static_cast<float>(arm.successes) / static_cast<float>(arm.attempts);
    float bonus = UCB1_EXPLORATION_C * sqrtf(logf(static_cast<float>(N)) / static_cast<float>(arm.attempts));
    float score = meanReward + bonus;

    if (best == NODE_ID_UNKNOWN || score > bestScore || (score == bestScore && nh < best)) {
      best = nh;
      bestScore = score;
    }
  }
  return best;
}

ArmSnapshot snapshot(const Ucb1State& state, NodeId destination, NodeId nextHop) {
  if (destination >= NODE_ID_COUNT || nextHop >= NODE_ID_COUNT) {
    return ArmSnapshot{ NODE_ID_UNKNOWN, 0, 0, 0, 0.0f };
  }
  const ArmStats& arm = state.arms[destination][nextHop];
  float meanReward = (arm.attempts == 0) ? 0.0f : static_cast<float>(arm.successes) / static_cast<float>(arm.attempts);
  return ArmSnapshot{ nextHop, arm.attempts, arm.successes, arm.failures, meanReward };
}

}  // namespace ucb1_core
