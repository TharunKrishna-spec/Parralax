#pragma once
#include <stdint.h>
#include "../core/node_id.h"
#include "../config.h"

// ============================================================
// reliability_core — pure hop-by-hop reliable-delivery algorithm,
// deliberately free of any Arduino/ESP-NOW/Serial dependency (no millis(),
// no logger::*, no transport::*, no MeshPacket bytes). Mirrors the
// routing_core/predictor_core/anomaly_core split from Phases 1-3 (see
// docs/decisions.md) for the same reason: retry/timeout/duplicate-filter
// bookkeeping is real algorithmic logic worth verifying on its own, and
// "verified on its own" only means something if it runs outside the ESP32
// toolchain. Every function takes `now` as an explicit uint32_t parameter.
//
// This is NOT a network/radio simulator - it never fakes a send, an ACK
// arriving, or a MAC address; it only tracks the bookkeeping (pending-
// transmission slots, duplicate cache, statistics) that a real adapter
// (reliability.cpp) drives from real ESP-NOW sends/receives. The actual
// bytes-on-the-radio (MeshPacket construction, transport::send(), ACK
// packet parsing) live entirely in reliability.cpp — see docs/decisions.md
// for why payload storage deliberately does NOT live here (a resend needs
// the original bytes, which only the Arduino-facing adapter has any
// business owning).
//
// Packet identity (Part 1): a packet is identified by (source, sequence) -
// NOT next_hop, NOT prev_hop. `source` is who originally created the
// packet; `sequence` is that source's own per-packet monotonic counter,
// preserved unchanged across every hop of a forward (Part 6). This reuses
// MeshPacket's own `source`/`sequence` header fields exactly - no new wire
// format. This is deliberately a different concept from the GUI telemetry
// contract's per-envelope `seq` (gui-main/gui-main/docs/gui-telemetry-
// contract.md) - that field doesn't exist in firmware at all yet (see
// docs/known-issues.md), and even once it does, it will number GUI
// telemetry *messages*, not mesh *packets*. Do not conflate the two.
// ============================================================

namespace reliability_core {

// Sentinel returned by beginTx() when the pending pool is full.
static const uint8_t INVALID_SLOT = 0xFF;

// (source, sequence) — see the file header. Identifies one application-
// level packet for its entire multi-hop lifetime, independent of which
// node currently holds it or which hop is being attempted.
struct PacketId {
  NodeId source;
  uint16_t sequence;
};

// One outgoing hop-transmission this node is currently waiting on an ACK
// for — either something this node originated (reliability::send()) or
// something it is forwarding (Part 7). Exactly one slot per concurrently
// in-flight hop-transmission; a node never has two pending slots for the
// same (source, sequence) at once (Part 6/7: forwarding preserves identity,
// but a given node only forwards a given packet once, since repeats are
// caught by the duplicate filter before ever reaching beginTx()).
struct PendingTx {
  bool active;
  PacketId id;
  NodeId nextHop;         // the direct neighbor this hop-transmission is addressed to
  uint8_t attemptCount;    // real unicast sends issued so far for this hop-transmission, >=1 once active
  uint32_t lastSendMs;     // timestamp of the most recent (re)send — what ACK_TIMEOUT_MS is measured from
};

// One recently-seen (source, sequence) identity, for Part 6's duplicate
// filter. See dupCache's size/expiry/replacement policy in config.h's
// RELIABILITY_DUP_CACHE_SIZE/RELIABILITY_DUP_CACHE_TTL_MS comments.
struct DupEntry {
  bool valid;
  PacketId id;
  uint32_t seenAtMs;
};

// Deterministic counters (Part 11). See docs/decisions.md for the exact
// numerator/denominator semantics (Part 9) each field uses — in short:
// packets_* count at application-hop-transmission granularity (one count
// per (source,sequence,nextHop) series, however many attempts it took);
// retries/acknowledgements count at individual-attempt granularity.
struct Statistics {
  uint32_t packetsSent;         // hop-transmission series begun (beginTx calls that got a slot)
  uint32_t packetsDelivered;    // hop-transmission series that ended in a matched ACK (per-hop, not end-to-end — see docs/decisions.md)
  uint32_t packetsFailed;       // hop-transmission series that exhausted RELIABILITY_MAX_RETRIES with no ACK
  uint32_t retries;             // individual re-send attempts (attempt #2 and beyond) issued
  uint32_t duplicatesDropped;   // receive-side (source,sequence) identities recognized as already-seen
  uint32_t acknowledgements;    // real MSG_ACK arrivals that matched a pending slot
  uint32_t lastLatencyMs;       // now - lastSendMs at the moment of the most recent successful match; 0 if none yet
};

struct ReliabilityState {
  NodeId self;
  PendingTx pending[RELIABILITY_MAX_PENDING];
  DupEntry dupCache[RELIABILITY_DUP_CACHE_SIZE];
  uint8_t dupCacheNext;    // ring-buffer write cursor, used only when no invalid/expired slot is available to reuse
  uint16_t nextSeqCounter; // this node's own per-source sequence counter (Part 1/6) for packets it originates
  Statistics stats;
};

void init(ReliabilityState& state, NodeId self);

// Part 1/6: the next sequence number for a NEW packet this node originates.
// Never called when forwarding — forwarded packets keep their original
// (source, sequence) unchanged (Part 6).
uint16_t nextSequence(ReliabilityState& state);

// Part 6: true if (source, sequence) was already seen within
// RELIABILITY_DUP_CACHE_TTL_MS; if not (or expired), records it as newly
// seen and returns false. This is the single authoritative check-and-
// record call — callers must not separately "insert" after checking.
bool isDuplicateAndRecord(ReliabilityState& state, NodeId source, uint16_t sequence, uint32_t now);

// Part 3/4: reserves a pending slot for one new outgoing hop-transmission,
// BEFORE the caller/adapter attempts the actual radio send — so a full
// pool is discovered without ever launching an untracked frame (Part 2/3:
// no fabricated delivery expectation for a frame nothing is watching).
// Sets attemptCount=1 and lastSendMs=now, anticipating that the adapter's
// real transport::send() for attempt #1 happens immediately after this
// call succeeds. If that real send is then rejected synchronously (e.g. an
// unregistered peer — see docs/known-issues.md), the caller must call
// cancelTx() on the returned slot rather than leave it to time out for a
// failure that is already known. Returns INVALID_SLOT if the pending pool
// (RELIABILITY_MAX_PENDING) is already full.
uint8_t beginTx(ReliabilityState& state, NodeId source, uint16_t sequence, NodeId nextHop, uint32_t now);

// Immediately fails a just-reserved slot whose actual first radio send was
// rejected synchronously — avoids waiting RELIABILITY_ACK_TIMEOUT_MS to
// declare a failure that is already known for certain (Part 5: "do not
// block... indefinitely", read as "do not needlessly delay a known
// outcome" too). Converges on the same documented statistics outcome as a
// slot that instead fails via tickTimeouts' FAILED branch (Part 9). A
// no-op if `slot` is out of range or already inactive.
void cancelTx(ReliabilityState& state, uint8_t slot);

// Records a hop-transmission that was declared failed WITHOUT ever
// reserving a pending slot at all — beginTx() itself found the pool full.
// Kept as its own function (never a direct field write) so every
// Statistics mutation stays funneled through reliability_core's own API,
// matching routing_core/predictor_core/anomaly_core's established
// discipline.
void recordImmediateFailure(ReliabilityState& state);

// Part 3: a real MSG_ACK arrived carrying (source, sequence). `matched` is
// true only if a pending slot for that identity is currently being
// tracked — a stale/unknown/already-cleared identity leaves state
// untouched and returns matched=false (never fabricate a match). Clears
// the slot and records latency/statistics on a real match.
struct AckResult {
  bool matched;
  NodeId nextHop;
  uint32_t latencyMs;
  uint8_t slot;  // which pending[] slot this concerns — meaningful only when matched; lets the adapter recover its own parallel packet-byte storage (e.g. the original packet's destination, for Phase 5's UCB1 reward) without re-deriving it (Part 2 of Phase 5's task spec)
  uint8_t attemptCount;  // real, final attempt count this hop-transmission needed before the ACK matched (1 = succeeded on the first try, >1 = succeeded only after real retries) — captured from the pending slot before it's cleared, so telemetry can honestly distinguish a first-try delivery from a recovered one, matching the project's existing "one series, however many attempts" accounting (see Statistics's own field comments). Meaningless when matched == false.
};
AckResult onAckReceived(ReliabilityState& state, NodeId source, uint16_t sequence, uint32_t now);

// Part 4/5: what the adapter should do about one pending slot that has just
// timed out. Both actions represent one failed attempt (see
// docs/decisions.md#reliability-retries-feed-predictor-per-attempt-not-per-
// packet for why both are fed to predictor::onSendResult(neighbor, false)):
//   RETRY  - attempts remain; adapter must resend the stored packet bytes
//            and the slot stays active with attemptCount incremented.
//   FAILED - RELIABILITY_MAX_RETRIES already reached; the slot is cleared
//            and this hop-transmission is a final, declared failure.
enum class TimeoutAction : uint8_t { RETRY, FAILED };

struct TimeoutEvent {
  TimeoutAction action;
  PacketId id;
  NodeId nextHop;
  uint8_t attemptCount;  // the attempt number that just timed out
  uint8_t slot;          // which pending[] slot this concerns — the adapter's own parallel packet-byte storage is indexed the same way
};

// Sweeps every active pending slot for ACK_TIMEOUT_MS expiry. Writes up to
// maxOut TimeoutEvent entries into `out` and returns how many were
// written. Intended to be called once per reliability::tick() (Part 5:
// "must remain compatible with the existing main loop" — this never
// blocks; it is a single non-blocking sweep over a small fixed array).
uint8_t tickTimeouts(ReliabilityState& state, uint32_t now, TimeoutEvent* out, uint8_t maxOut);

}  // namespace reliability_core
