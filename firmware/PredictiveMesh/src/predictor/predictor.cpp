#include "predictor.h"
#include "../core/logger.h"

namespace predictor {

void init() {
  logger::info("predictor: init (Phase 0 stub - EWMA/slope/PDR fusion not yet implemented)");
}

float linkScore(NodeId neighbor) {
  (void)neighbor;
  return 1.0f;
}

}  // namespace predictor
