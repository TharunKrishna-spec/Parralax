#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "message_types.h"
#include "node_id.h"

// ============================================================
// MeshPacket — the wire frame for every ESP-NOW payload this project sends.
//
// Field set matches implementation-guide.html's "PACKET / FRAME FOUNDATION"
// requirement exactly: source, destination, previous hop, next hop,
// sequence number, message type, priority, payload, timestamp. Nothing
// beyond that list is added speculatively — see docs/decisions.md for why
// each field is here and docs/protocol.md for the full layout.
//
// Layout notes (this matters on real hardware, not just style):
//  - `#pragma pack(push, 1)` fixes the wire layout deterministically so
//    every node (all identical ESP32 hardware, same compiled struct) agrees
//    on byte offsets without relying on default compiler padding rules.
//  - The `_reserved*` bytes are NOT wasted: they keep `sequence` on a
//    2-byte boundary and `timestamp_ms` on a 4-byte boundary *within* the
//    packed layout. On classic ESP32 (Xtensa LX6), dereferencing a
//    misaligned 16/32-bit field can raise a LoadStoreError exception —
//    padding avoids ever constructing one.
//  - Even so, never cast a raw received `uint8_t*` buffer straight to
//    `MeshPacket*` and dereference through it. Always memcpy() the bytes
//    into a locally declared MeshPacket first (see transport layer). The
//    local copy is naturally stack-aligned by the compiler; the incoming
//    radio buffer is not guaranteed to be.
// ============================================================

#define PACKET_PROTOCOL_VERSION 1

// Payload capacity in bytes. ESP-NOW's hard per-frame ceiling is
// ESP_NOW_MAX_DATA_LEN (250 bytes) for the *entire* frame, header included.
// 64 bytes was picked as comfortably covering the small structured
// telemetry this project sends (a couple of 12-bit ADC readings, a link
// score, a few flags) while leaving 3x headroom under that ceiling. It's a
// single #define — raise it later if a real payload needs more, no
// protocol redesign required.
#define PACKET_MAX_PAYLOAD 64

#pragma pack(push, 1)
struct MeshPacket {
  uint8_t  version;         // protocol version; bump if this layout changes
  uint8_t  type;             // MessageType
  uint8_t  source;           // NodeId that originated this packet
  uint8_t  destination;      // NodeId this packet is ultimately addressed to
  uint8_t  prev_hop;         // NodeId that transmitted this copy (this hop's sender)
  uint8_t  next_hop;         // NodeId this hop's sender intends it to reach next
  uint8_t  priority;         // 0 = normal (quality-optimal routing), 1 = priority (forces shortest-hop, §5.3)
  uint8_t  _reserved0;       // padding — keeps `sequence` 2-byte aligned. Must be 0.
  uint16_t sequence;         // per-source monotonically increasing counter (future duplicate filtering, §5.4)
  uint16_t _reserved1;       // padding — keeps `timestamp_ms` 4-byte aligned. Must be 0.
  uint32_t timestamp_ms;     // sender's millis() at send time (future reroute-lead-time / staleness use, §07)
  uint8_t  payload_len;      // number of valid bytes in payload[] (<= PACKET_MAX_PAYLOAD)
  uint8_t  payload[PACKET_MAX_PAYLOAD];
};
#pragma pack(pop)

#define PACKET_HEADER_SIZE offsetof(MeshPacket, payload)

// Bytes actually worth putting on the radio for this packet: header plus
// only the valid payload bytes, not the full fixed-size buffer.
inline size_t packetWireSize(const MeshPacket& pkt) {
  return PACKET_HEADER_SIZE + pkt.payload_len;
}

// Zero-initializes a packet and fills in the fields Phase 0 already knows
// how to set. Does NOT set sequence (reliability layer's job, not yet
// implemented) or timestamp_ms (caller should stamp it with millis() at
// the actual moment of transport::send(), not at construction time).
inline void packetInit(MeshPacket& pkt, MessageType type, NodeId source, NodeId destination) {
  memset(&pkt, 0, sizeof(pkt));
  pkt.version = PACKET_PROTOCOL_VERSION;
  pkt.type = static_cast<uint8_t>(type);
  pkt.source = static_cast<uint8_t>(source);
  pkt.destination = static_cast<uint8_t>(destination);
  pkt.prev_hop = static_cast<uint8_t>(source);
  pkt.next_hop = NODE_ID_UNKNOWN;  // routing layer decides this — not implemented yet
  pkt.priority = 0;
  pkt.payload_len = 0;
}
