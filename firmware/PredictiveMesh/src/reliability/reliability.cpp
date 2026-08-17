#include "reliability.h"
#include "../core/logger.h"

namespace reliability {

void init() {
  logger::info("reliability: init (Phase 0 stub - ACK/retransmit/dup-filter not yet implemented)");
}

void onSendResult(NodeId dst, bool success) {
  logger::debug("reliability::onSendResult stub: dst=%s success=%d (no retry logic yet)",
                nodeName(dst), success ? 1 : 0);
}

}  // namespace reliability
