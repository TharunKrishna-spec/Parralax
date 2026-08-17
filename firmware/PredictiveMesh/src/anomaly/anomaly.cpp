#include "anomaly.h"
#include "../core/logger.h"

namespace anomaly {

void init() {
  logger::info("anomaly: init (Phase 0 stub - MAD Z-score + flatline detector not yet implemented)");
}

Flag evaluate(uint16_t rawAdcValue) {
  (void)rawAdcValue;
  return Flag::NONE;
}

}  // namespace anomaly
