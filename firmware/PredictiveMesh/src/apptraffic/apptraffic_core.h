#pragma once
#include <stdint.h>
#include "../core/node_id.h"

// ============================================================
// apptraffic_core — pure application-traffic decision/payload logic,
// deliberately free of any Arduino/ESP-NOW/Serial dependency (no millis(),
// no logger::*, no reliability::*, no analogRead()). Mirrors the
// routing_core/predictor_core/anomaly_core/reliability_core split from
// Phases 1-4 (see docs/decisions.md) for the same reason: "what should the
// next application packet contain, and is it NORMAL or PRIORITY" is real
// decision logic worth verifying on its own.
//
// Phase 7 (implementation-guide.html's "define real application traffic"
// gap, open since Phase 4 — see
// docs/decisions.md#reliabilitysend-has-no-live-automatic-caller-in-phase-4--no-application-data-source-was-invented).
// This module is the minimum legitimate demo workload: NODE_A periodically
// sends a small binary payload (its own latest POT/LDR readings) to NODE_S
// through the existing reliability::send() -> routing::selectNextHop()
// pipeline. It does NOT implement its own ACK, retry, PDR, or routing —
// see src/apptraffic/apptraffic.cpp (the thin Arduino adapter) and
// docs/decisions.md for why every one of those stays reliability's/
// routing's job.
//
// Wire format note: unlike routing.cpp's RouteAdWire / reliability.cpp's
// AckWire (which live in their adapter .cpp, not the *_core layer — see
// those files), this module's packed wire struct lives inside
// apptraffic_core.cpp instead, specifically so encode/decode is itself
// host-testable pure logic (Phase 7's task spec explicitly requires tests
// for "sensor values encoded/decoded correctly" and "payload size
// bounds") — a deliberate, documented exception to that precedent, not an
// inconsistency. See docs/decisions.md.
// ============================================================

namespace apptraffic_core {

// Fixed wire size of one encoded DATA payload, in bytes: appSeq(2) +
// potValue(2) + ldrValue(2) + timestampMs(4) = 10. Exposed as a constant
// (not just "whatever encodeData() happens to write") so callers/tests can
// reason about payload-capacity bounds without decoding a real buffer
// first. See docs/protocol.md.
static const uint8_t DATA_WIRE_SIZE = 10;

// Semantic (non-wire-packed) view of a decoded DATA payload — mirrors
// reliability_core::PacketId's "identity struct, not wire bytes" pattern.
struct DecodedData {
  uint16_t appSeq;
  uint16_t potValue;
  uint16_t ldrValue;
  uint32_t timestampMs;
};

// Encodes one application DATA payload into `out` (which must be at least
// DATA_WIRE_SIZE bytes). Returns the number of bytes written (always
// DATA_WIRE_SIZE on success), or 0 if outCap is too small — never writes a
// partial/truncated payload, matching telemetry_core::Writer's "ok latch"
// discipline for the same reason (a caller must never send bytes it
// thinks are DATA_WIRE_SIZE-but-aren't).
uint8_t encodeData(uint16_t appSeq, uint16_t potValue, uint16_t ldrValue, uint32_t timestampMs, uint8_t* out,
                    uint8_t outCap);

// Decodes a received application DATA payload. Returns false (leaves
// *decoded untouched) if `len` < DATA_WIRE_SIZE — never reads past a
// short/corrupt payload, matching routing.cpp's processRouteUpdate()
// convention for the same reason.
bool decodeData(const uint8_t* payload, uint8_t len, DecodedData* decoded);

// ---- send-decision logic ----

// Traffic classification the application layer assigns BEFORE calling
// reliability::send() — never a third value, never invented per-packet.
// `priority` on SendDecision mirrors this directly so a caller can pass it
// straight through to reliability::send()'s own bool parameter.
enum class TrafficClass : uint8_t { NORMAL, PRIORITY };

struct SendDecision {
  NodeId destination;        // always NODE_S — see buildSendDecision()
  TrafficClass trafficClass;
  bool priority;              // == (trafficClass == TrafficClass::PRIORITY)
};

// Adapter-owned state across calls: the application-level packet counter
// (a THIRD distinct identity axis alongside MeshPacket.sequence and the
// GUI telemetry envelope's own seq — see docs/decisions.md; none of the
// three may be conflated) and a one-shot "a priority packet was
// requested" latch.
struct State {
  uint16_t appSeqCounter;
  bool priorityPending;
};

void init(State& state);

// Called by the adapter when its deterministic priority trigger fires (a
// Serial command byte on NODE_A — see apptraffic.cpp). Idempotent: calling
// this any number of times before the next buildSendDecision() still
// produces exactly one PRIORITY packet, never a flood of them.
void requestPriority(State& state);

// Decides what the next application packet should be and consumes any
// pending priority request (one-shot: the very next call after this one
// reverts to NORMAL unless requestPriority() fires again). Always
// addresses NODE_S — implementation-guide.html's own topology names NODE_S
// as the mesh's sink/root, and Phase 7's task spec fixes NODE_A -> NODE_S
// as the demo's application flow; nothing about a legitimate call site can
// construct a different destination.
SendDecision buildSendDecision(State& state);

// Returns the next per-application-packet sequence number, advancing
// `state`. Wraps at 65536, same as MeshPacket.sequence itself
// (reliability_core::nextSequence) — a normal, expected event for a demo
// running past 65535 packets, not an error condition.
uint16_t nextAppSeq(State& state);

}  // namespace apptraffic_core
