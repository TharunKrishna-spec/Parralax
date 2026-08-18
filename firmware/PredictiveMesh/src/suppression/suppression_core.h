#pragma once
#include <stdint.h>
#include "../core/node_id.h"
#include "../config.h"

// ============================================================
// suppression_core — pure counter-based broadcast suppression algorithm
// for opportunistic priority-packet relaying, deliberately free of any
// Arduino/ESP-NOW/Serial dependency (no millis(), no logger::*, no
// transport::*, no MeshPacket bytes) — mirrors the routing_core/
// predictor_core/anomaly_core/reliability_core split from Phases 1-4 for
// the same reason: this is real algorithmic bookkeeping worth verifying on
// its own, host-testable outside the ESP32 toolchain. The adapter
// (suppression.cpp) drives this from real ESP-NOW receives/sends and owns
// the raw MeshPacket bytes needed to actually (re)transmit — exactly
// reliability_core's own established division of responsibility (a resend
// needs the original bytes, which only the Arduino-facing adapter has any
// business owning).
//
// Packet identity (deliberately reusing, not reinventing): a priority
// broadcast is identified by (source, sequence) — the exact same identity
// shape as reliability_core::PacketId ("packet identity" per Part 4 of
// this milestone's own spec: "if the existing MeshPacket sequence + source
// ... is sufficient, reuse it"). A separate local `PacketId` struct is
// defined here (not a #include of reliability_core.h) to keep every
// *_core module mutually independent, matching this project's existing
// convention (routing_core/predictor_core/anomaly_core never include each
// other either).
//
// Sequence numbers for priority broadcasts come from THIS module's own
// nextSequence() — a fourth, deliberately separate identity axis from
// reliability_core's own per-source counter (used for NORMAL MSG_DATA),
// apptraffic_core's application-level appSeq, and the GUI telemetry
// envelope's own seq. None of the four may be conflated (Part 4). Since
// priority broadcasts (MSG_PRIORITY_BROADCAST) and normal unicast data
// (MSG_DATA) are now two fully disjoint MessageTypes/pipelines, their
// sequence numbers are never compared against each other, so two separate
// counters cannot collide in any way that matters.
//
// Cross-reboot identity: NOT disambiguated at the wire level (no boot-salt
// field). This is a deliberate choice, not an oversight — see
// docs/decisions.md for the full reasoning: docs/protocol.md explicitly
// documents MeshPacket's two `_reserved` padding bytes as alignment
// padding, "not spare capacity for casual use," and this project has twice
// already (Phase 1, Phase 4) declined to grow MeshPacket's wire format
// ahead of an actually-observed need. reliability_core::PacketId already
// accepts the identical limitation for all normal traffic. Documented, not
// silently assumed away.
// ============================================================

namespace suppression_core {

// (source, sequence) — see the file header. Identifies one priority
// broadcast for its entire propagation lifetime, independent of which
// node currently holds/relays it.
struct PacketId {
  NodeId source;
  uint16_t sequence;
};

inline bool packetIdEquals(const PacketId& a, const PacketId& b) {
  return a.source == b.source && a.sequence == b.sequence;
}

enum class Decision : uint8_t {
  NONE,       // not yet decided (deadline hasn't arrived)
  TRANSMIT,   // this node's backoff expired before the suppression threshold was reached — rebroadcast
  SUPPRESS,   // the suppression threshold was reached before this node's own deadline — stay silent
};

// One recently-observed priority-broadcast identity. `rssiAtFirstHear` is
// the RSSI of whichever reception created this entry (the true original
// direct from `source`, or an already-relayed copy if that's genuinely
// the first copy this node heard) — see onReceive()'s doc comment for why
// that reception's RSSI, not necessarily the true original's, is the
// correct backoff input.
struct CacheEntry {
  bool valid;
  PacketId id;
  int8_t rssiAtFirstHear;
  uint8_t overheardCount;     // distinct OTHER-node rebroadcasts observed (never the original reception, never this node's own transmission)
  uint32_t deadlineMs;        // this node's own scheduled transmit-or-suppress evaluation time; meaningless when isLocalDestination
  bool decided;               // true once tickDecisions() (or origination, or local-destination delivery) has settled this entry's fate
  Decision decision;
  bool isLocalDestination;    // true if this node is the packet's real destination — never scheduled to transmit (Part 12)
  uint32_t touchedAtMs;       // last time this entry was created/overheard/decided — TTL is measured from here
};

struct State {
  CacheEntry cache[SUPPRESSION_CACHE_SIZE];
  uint16_t nextSeqCounter;   // this node's own priority-broadcast sequence counter — see file header
};

void init(State& state);

// The next sequence number for a NEW priority broadcast THIS node
// originates. Never called for a relayed/forwarded copy — those keep
// their original (source, sequence) unchanged, exactly like reliability's
// own forwarding convention.
uint16_t nextSequence(State& state);

// RSSI-aware backoff (Part 8): bounded linear interpolation between
// SUPPRESSION_MIN_BACKOFF_MS (weak/far RSSI, fires soonest) and
// SUPPRESSION_MAX_BACKOFF_MS (strong/near RSSI, waits longest), banded by
// SUPPRESSION_RSSI_WEAK_DBM/SUPPRESSION_RSSI_STRONG_DBM, plus caller-
// supplied jitter added on top. A spatial heuristic only — never claimed
// to mathematically guarantee optimal coverage. `jitterMs` is computed by
// the adapter (esp_random()-based) and passed in so this function stays
// pure/deterministic and host-testable with hand-picked jitter values.
uint32_t computeBackoffMs(int8_t rssi, uint32_t jitterMs);

enum class ReceiveOutcome : uint8_t {
  NEW_ENTRY,              // first time this identity has been seen — a cache entry was created and (unless isLocalDestination) a backoff scheduled
  OVERHEARD,               // an existing, not-yet-decided entry's overheardCount just incremented (a genuine relay rebroadcast, not the original)
  DUPLICATE_OF_ORIGINAL,    // a stray repeat reception directly from the true source (prevHop == id.source) for an identity already cached — no state change
  ALREADY_DECIDED,          // this identity was already decided (transmitted/suppressed/delivered) — overheardCount still updates for telemetry visibility, but no scheduling changes
  CACHE_FULL,               // a genuinely new identity arrived but no free slot exists — dropped, not tracked
};

struct ReceiveResult {
  ReceiveOutcome outcome;
  uint8_t slot;             // meaningful for every outcome except CACHE_FULL
  uint8_t overheardCount;    // post-update count, meaningful for NEW_ENTRY/OVERHEARD/ALREADY_DECIDED
};

// Called for every genuinely received MSG_PRIORITY_BROADCAST packet this
// node did NOT itself just transmit (the adapter must filter out
// `prevHop == THIS_NODE_ID` before ever calling this — see suppression.cpp
// — so this function never needs to know its own identity).
//
// `prevHop == id.source` means this reception is the true original
// transmission (or a stray repeat of it) — Part 9 explicitly requires this
// NOT be counted as evidence of a relay rebroadcast. Any other prevHop
// means a real relay already rebroadcast this identity — genuine evidence,
// always counted, even if it's this node's very first reception of the
// identity (a node can easily hear a relay's copy before/instead of ever
// hearing the true original directly).
//
// `backoffDeadlineMs` (already computed by the caller via
// computeBackoffMs() + a real `now`) is only consumed when this reception
// creates a brand-new, non-local-destination entry.
ReceiveResult onReceive(State& state, PacketId id, NodeId prevHop, bool isLocalDestination,
                         int8_t rssi, uint32_t backoffDeadlineMs, uint32_t now);

struct ReadyDecision {
  uint8_t slot;
  PacketId id;
  Decision decision;
  uint8_t overheardCountAtDecision;  // real overheardCount at the moment the deadline fired — the "why" behind TRANSMIT/SUPPRESS
};

// Sweeps the cache for entries whose deadlineMs has passed and are not yet
// decided (excludes isLocalDestination entries, which never transmit).
// TRANSMIT when overheardCount < SUPPRESSION_THRESHOLD at the moment the
// deadline arrives, SUPPRESS otherwise. Writes up to maxOut ready
// decisions into `out`, marking each entry `decided = true` before
// returning — never re-evaluates the same entry twice. Non-blocking,
// intended for one call per suppression::tick().
uint8_t tickDecisions(State& state, uint32_t now, ReadyDecision* out, uint8_t maxOut);

// Records that THIS node has just originated (not relayed) a brand-new
// priority broadcast — reserves a cache entry already marked
// `decided = true, decision = TRANSMIT` so any later overhearing of a
// relay's copy of this same identity only ever increments overheardCount,
// never schedules a second transmission for a packet this node already
// sent. Returns INVALID_SLOT if the cache is full.
static const uint8_t INVALID_SLOT = 0xFF;
uint8_t recordOwnOrigination(State& state, PacketId id, uint32_t now);

// Cache TTL sweep — invalidates entries untouched for longer than
// SUPPRESSION_CACHE_TTL_MS. Returns how many entries were invalidated.
uint8_t expireCache(State& state, uint32_t now);

}  // namespace suppression_core
