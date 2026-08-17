#include "ucb1.h"
#include "../config.h"

#if ENABLE_UCB1

#include "../core/logger.h"

namespace {

ucb1_core::Ucb1State g_state;

}  // namespace

namespace ucb1 {

void init() {
  ucb1_core::init(g_state);
  logger::info("ucb1: init (UCB1 adaptive next-hop ranking, Phase 5 — ENABLED)");
}

void onRouteOutcome(NodeId destination, NodeId nextHop, bool success) {
  ucb1_core::recordOutcome(g_state, destination, nextHop, success);
  logger::debug("[UCB1] outcome dest=%s next=%s success=%d", nodeName(destination), nodeName(nextHop),
                success ? 1 : 0);
}

NodeId selectNextHop(NodeId destination, const ucb1_core::Candidate* candidates, uint8_t count,
                      NodeId excludeNextHop) {
  NodeId chosen = ucb1_core::selectNextHop(g_state, destination, candidates, count, excludeNextHop);
  if (chosen != NODE_ID_UNKNOWN) {
    ucb1_core::ArmSnapshot snap = ucb1_core::snapshot(g_state, destination, chosen);
    logger::debug("[UCB1] dest=%s chose next=%s attempts=%lu mean_reward=%.2f", nodeName(destination),
                  nodeName(chosen), static_cast<unsigned long>(snap.attempts), snap.meanReward);
  }
  return chosen;
}

}  // namespace ucb1

#endif  // ENABLE_UCB1
