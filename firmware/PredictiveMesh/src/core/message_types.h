#pragma once
#include <stdint.h>

// Wire-level message types. Kept deliberately small for Phase 0 — only the
// types the architecture in implementation-guide.html §01/§04/§5.4 actually
// names. New types (e.g. explicit priority-ack) get added when the layer
// that needs them is implemented, not preemptively.
enum MessageType : uint8_t {
  MSG_HEARTBEAT = 0,  // periodic liveness / link-quality probe between direct neighbors
  MSG_DATA      = 1,  // application payload (sensor reading, anomaly flag, ...). May carry priority=1.
  MSG_ACK       = 2,  // hop-by-hop delivery acknowledgement (§5.4) — reliability layer, not yet implemented
};

inline const char* messageTypeName(MessageType type) {
  switch (type) {
    case MSG_HEARTBEAT: return "HEARTBEAT";
    case MSG_DATA:       return "DATA";
    case MSG_ACK:         return "ACK";
    default:              return "UNKNOWN";
  }
}
