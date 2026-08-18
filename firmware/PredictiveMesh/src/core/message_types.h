#pragma once
#include <stdint.h>

// Wire-level message types. Kept deliberately small for Phase 0 — only the
// types the architecture in implementation-guide.html §01/§04/§5.4 actually
// names. New types (e.g. explicit priority-ack) get added when the layer
// that needs them is implemented, not preemptively.
enum MessageType : uint8_t {
  MSG_HEARTBEAT         = 0,  // periodic liveness / link-quality probe between direct neighbors
  MSG_DATA              = 1,  // application payload (sensor reading, anomaly flag, ...). Always priority=0 — see MSG_PRIORITY_BROADCAST.
  MSG_ACK               = 2,  // hop-by-hop delivery acknowledgement (§5.4) — reliability layer, Phase 4
  // Opportunistic broadcast + overhear + RSSI-aware counter-based
  // suppression (Priority-broadcast milestone, 2026-08-18). A deliberate,
  // real deviation from implementation-guide.html §5.3 (which specifies
  // priority traffic as a forced-shortest-hop *unicast* override, never
  // broadcast) — user-confirmed replacement of that delivery mechanism,
  // not a guide gap-fill. Never dispatched through reliability::
  // onPacketReceived()'s unicast/ACK/duplicate-filter pipeline (that would
  // create a second, competing priority mechanism and incorrectly count a
  // broadcast delivery as a unicast ACK) — a distinct MessageType, not
  // just MeshPacket.priority=1 on MSG_DATA, is what lets main.cpp's
  // dispatch route it to src/suppression/ instead. See docs/decisions.md.
  MSG_PRIORITY_BROADCAST = 3,
};

inline const char* messageTypeName(MessageType type) {
  switch (type) {
    case MSG_HEARTBEAT:          return "HEARTBEAT";
    case MSG_DATA:                return "DATA";
    case MSG_ACK:                  return "ACK";
    case MSG_PRIORITY_BROADCAST:   return "PRIORITY_BROADCAST";
    default:                       return "UNKNOWN";
  }
}
