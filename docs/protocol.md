# Protocol — `MeshPacket` wire frame

Status: **struct layout unchanged since Phase 0 — Phase 4 is the first
phase to populate `sequence` for real and to actually construct/parse
`MSG_ACK`, and Phase 7 is the first phase with a real, live `MSG_DATA`
application payload flowing (`NODE_A` -> `NODE_S`, see below) — but adds
zero new bytes to the `MeshPacket` frame itself in either case.** `link_score` is a
purely local quantity — each node's own evaluation of its own direct radio
links, consumed only by that same node's own routing decision. Anomaly
flags are detected and logged locally, not yet transmitted anywhere. See
[decisions.md](decisions.md#no-meshpacketwire-format-changes-needed-for-phase-2)
and
[decisions.md](decisions.md#no-meshpacketwire-format-changes-for-anomaly-flags-in-phase-3)
for the Phase 2/3 reasoning, and
[decisions.md](decisions.md#packet-identity-is-source-sequence-reusing-meshpackets-existing-header-fields--no-new-wire-format)
for Phase 4's use of the existing `sequence` field. Phase 0 defined this struct
(`firmware/PredictiveMesh/src/core/packet.h`) and the ESP-NOW transport
that carries it. Phase 1 is the first phase that actually constructs and
parses one: `src/routing/routing.cpp` sends `MSG_HEARTBEAT` beacons
carrying a distance-vector payload (format below) and parses them back out
of every received frame. The struct itself is unchanged from Phase 0 — see
[decisions.md](decisions.md#no-ttlhop-count-field-added-to-meshpacket-in-phase-1)
for why no fields were added.

## Why a custom struct instead of a serialization library

ESP-NOW hands you a raw `(mac, bytes, length)` triple — there's no framing
built in. All five nodes are the same hardware (ESP32-WROOM-32) running the
same compiled struct definition, so a plain C struct sent as raw bytes is
sufficient: no endianness conversion, no schema negotiation, no
variable-length encoding needed. A serialization library (protobuf-lite,
msgpack, etc.) would add complexity this homogeneous-hardware mesh doesn't
need. See the "Practical theory notes" in
[architecture.md](architecture.md) for the underlying reasoning.

## Layout

```
Offset  Size  Field           Type       Notes
0       1     version         uint8_t    PACKET_PROTOCOL_VERSION (currently 1)
1       1     type            uint8_t    MessageType (see below)
2       1     source          uint8_t    NodeId that originated this packet
3       1     destination     uint8_t    NodeId ultimately addressed
4       1     prev_hop        uint8_t    NodeId that transmitted this copy
5       1     next_hop        uint8_t    NodeId this hop intends next (NODE_ID_UNKNOWN if undecided)
6       1     priority        uint8_t    0 = normal, 1 = priority (see below)
7       1     _reserved0      uint8_t    padding, must be 0
8       2     sequence        uint16_t   per-source counter, for future duplicate filtering
10      2     _reserved1      uint16_t   padding, must be 0
12      4     timestamp_ms    uint32_t   sender's millis() at send time
16      1     payload_len     uint8_t    valid bytes in payload[]
17      64    payload[]       uint8_t[]  PACKET_MAX_PAYLOAD = 64

Header size: 17 bytes (PACKET_HEADER_SIZE = offsetof(MeshPacket, payload))
Max total frame: 17 + 64 = 81 bytes (ESP-NOW's hard ceiling is 250 bytes)
```

The struct is `#pragma pack(push, 1)`, so these offsets are exact and
identical across all five (identical) boards. The two `_reserved` fields
are not spare capacity for casual use — they exist to keep `sequence` and
`timestamp_ms` on aligned offsets. See
[decisions.md](decisions.md#packed-struct-with-explicit-alignment-padding-never-pointer-cast-a-raw-receive-buffer)
for why that matters on real Xtensa hardware.

Only `packetWireSize(pkt)` bytes (`PACKET_HEADER_SIZE + payload_len`) need
to actually go out over ESP-NOW — there's no need to always transmit the
full 81-byte frame if the payload is smaller.

## `MessageType`

```cpp
enum MessageType : uint8_t {
  MSG_HEARTBEAT          = 0,  // periodic liveness / link-quality probe between direct neighbors
  MSG_DATA               = 1,  // application payload (sensor reading, anomaly flag, ...). Always priority=0.
  MSG_ACK                = 2,  // hop-by-hop delivery acknowledgement (§5.4) — Phase 4
  MSG_PRIORITY_BROADCAST = 3,  // opportunistic broadcast + overhear + RSSI-aware suppression (2026-08-18 milestone)
};
```

The first three values match exactly what implementation-guide.html
names: the main loop flowchart's "Build outgoing frame (heartbeat / data /
priority)" step, and §5.4's ACK. `MSG_DATA` and `MSG_ACK` are both real
and exchanged as of Phase 4 — see
[architecture.md](architecture.md#reliability-layer-phase-4). `MSG_ACK`'s
payload is `AckWire{source, sequence}` (3 bytes, packed — see
`src/reliability/reliability.cpp`), identifying which `MSG_DATA` packet is
being acknowledged; `MSG_ACK` packets are never themselves acknowledged
(fire-and-forget — see
[decisions.md](decisions.md#ack-packets-are-fire-and-forget--never-themselves-acknowledged)).

`MSG_PRIORITY_BROADCAST` is the one real deviation from
implementation-guide.html's own §5.3 architecture — see "`priority`"
below and
[decisions.md](decisions.md#priority-broadcast-milestone-opportunistic-broadcast--overhearing--rssi-aware-counter-based-suppression--replaces-the-previous-unicast-priority-override)
for the full record, including why a distinct `MessageType` (not just
`priority=1` on `MSG_DATA`) was necessary: it's what lets `main.cpp`'s
receive dispatch route it to `src/suppression/` only, never reaching
`reliability::onPacketReceived()`'s `MSG_DATA`/ACK/duplicate-filter/
forwarding pipeline at all.

## `priority`

A separate field from `type`. Per §5.3, a priority flag is checked
independently of the packet's payload type: "priority flag set? -> force
shortest-hop, ignoring link_score entirely." `MSG_HEARTBEAT`/`MSG_ACK`
traffic ignores it. See
[decisions.md](decisions.md#priority-is-a-packet-field-not-a-separate-messagetype)
for the original reasoning.

**Updated, 2026-08-18 (priority-broadcast milestone):** `priority=1` no
longer means "forced-shortest-hop unicast" in practice — that unicast
override existed only for `MSG_DATA` packets sent via
`reliability::send(..., priority=true)`, and nothing calls that anymore
(`apptraffic.cpp`'s priority branch now calls
`suppression::broadcastPriority()` instead). `priority=1` is still set on
`MSG_PRIORITY_BROADCAST` packets (semantically still true, keeps any
`pkt.priority`-reading code working unchanged), but the actual delivery
behavior (broadcast, not routed unicast) is driven entirely by
`pkt.type == MSG_PRIORITY_BROADCAST`, not by this flag. NORMAL traffic
(`MSG_DATA`, always `priority=0`) is completely unaffected — still
`reliability::send()`, still unicast, still ACK/retry/PDR exactly as
before. See the decisions.md entry above for the full guide-deviation
record (confirmed with the user before implementation, not silently
resolved).

## `prev_hop` / `next_hop`

Both are explicit fields on the packet, not left implicit in the ESP-NOW
sender MAC, because the architecture calls for hop-by-hop forwarding with
per-hop ACKs (§5.4) — a relay node needs to know who to ACK back to
(`prev_hop`) independent of who the transport layer says physically sent
the frame, and downstream reliability/predictor logic needs to know the
intended next hop to correlate a send outcome with the right link. Neither
is populated by anything yet in Phase 0 — `packetInit()` sets `prev_hop =
source` and `next_hop = NODE_ID_UNKNOWN` as safe defaults for a
single-hop, not-yet-routed packet.

## `sequence`

Per-source monotonically increasing counter, 16-bit. Exists for the
duplicate filter described in §5.4 ("a sequence-number-based duplicate
filter drops repeats caused by retransmits or multi-path delivery") — real
as of Phase 4. `packetInit()` still leaves it at 0 (structural default);
`reliability::send()` assigns a fresh value via
`reliability_core::nextSequence()` for every newly-originated `MSG_DATA`
packet, and a forwarding node never reassigns it — it rides unchanged with
`source` as the packet's identity for its entire multi-hop lifetime. See
[decisions.md](decisions.md#packet-identity-is-source-sequence-reusing-meshpackets-existing-header-fields--no-new-wire-format)
for why this is deliberately a different concept from the GUI telemetry
contract's own envelope `seq`.

## `timestamp_ms`

Sender's `millis()` at the moment of send — deliberately *not* set by
`packetInit()` (which only zero-initializes and stamps structural fields),
since the real send time should be captured as close to the actual
`transport::send()` call as possible, not at packet-construction time.
Exists to support two things named explicitly in the guide: the reroute
lead-time metric (§07 — "log the last-heartbeat timestamp ... report
`deadline - reroute_time`") and future staleness detection on a link's most
recent heartbeat.

**The reroute lead-time metric is now real** (2026-08-18 implementation
pass) — `telemetry.cpp::onRouteEvent()` populates `EVENT ROUTE_CHANGE`'s
`details.leadTimeMs`, but only for a genuine, real, proactive score-driven
reroute (`reason == LINK_DEGRADATION_R`), computed entirely from real
timestamps and one real, existing constant:
`leadTimeMs = max(0, ROUTING_ENTRY_TIMEOUT_MS - (now - routing::getNeighborLastSeenMs(oldNextHop)))`
— "how much sooner did the proactive reroute act, compared to what
routing's own independent silence-based hard fallback would eventually
have forced anyway." Never extrapolated/estimated; omitted entirely (not
fabricated as 0) for any other reroute reason. Full reasoning in
[decisions.md](decisions.md#real-measured-never-extrapolated-prediction-lead-time).

### Three (really four) distinct sequence/timestamp identities — never conflate these

This project now has multiple, genuinely different "sequence number" and
"timestamp" concepts, deliberately kept separate everywhere they could be
confused:

| Identity | Type | Meaning | Where |
|---|---|---|---|
| `MeshPacket.sequence` | uint16, per-source | wire packet identity, with `source` — see above | every hop, unchanged |
| `apptraffic_core`'s `appSeq` | uint16, `NODE_A`-only counter | which application-layer POT/LDR sample this is | encoded inside `MSG_DATA`'s payload bytes; decoded live at the sink as of this pass — see `PACKET.appSeq` in `docs/gui-compatibility-matrix.md` |
| telemetry envelope `seq` | uint32, per-node, per-boot | which telemetry JSON *message* this is (GUI sequence-gap detection) | every telemetry line's own envelope, distinct from any `MeshPacket` |
| `PACKET.appTimestampMs` | uint32 | the *originating* node's own `millis()` at the moment `apptraffic::sendOne()` called `encodeData()` | new (2026-08-18), decoded from real payload bytes, distinct from `MeshPacket.timestamp_ms` (rewritten every hop) and from telemetry's own `envelope.timestampMs` (the *reporting* node's local time) |
| `suppression_core::State::nextSeqCounter` | uint16, per-node | this node's own priority-broadcast sequence counter — a FIFTH identity axis, added 2026-08-18 | `MeshPacket.sequence` on `MSG_PRIORITY_BROADCAST` packets only; deliberately a separate counter from `reliability_core`'s own (NORMAL `MSG_DATA` traffic), even though both ultimately ride the same wire field — see [decisions.md](decisions.md#packet-identity-source-sequence-reusing-the-existing-shape-not-the-existing-counter) |

## Route-advertisement payload (Phase 1, rides inside `MSG_HEARTBEAT`)

`MSG_HEARTBEAT` doubles as both the HELLO beacon (its arrival proves the
sender is alive) and the distance-vector route advertisement (its
payload). There is no separate message type for this — see
[decisions.md](decisions.md#hello-and-route-advertisement-share-one-wire-message-msg_heartbeat).
Layout inside `MeshPacket.payload[]`:

```
Offset  Size  Field         Notes
0       1     count         number of (destination, hop_count) entries that follow
1       2*N   entries[]     N = count, each entry: { uint8_t destination; uint8_t hop_count; }
```

At most `NODE_ID_COUNT` (5) entries are ever sent — one per destination
the sender currently has a route to, including itself at `hop_count = 0`.
Max payload used: `1 + 5*2 = 11` bytes, well under `PACKET_MAX_PAYLOAD`
(64). `destination`/`hop_count` are both single bytes, so unlike
`MeshPacket`'s own header fields, no alignment padding or `memcpy()` is
needed to read them back out of the payload — see
`src/routing/routing.cpp`'s `RouteAdWire` struct.

Beacons set `destination = NODE_ID_UNKNOWN` on the outer `MeshPacket`
(reusing the same sentinel `packetInit()` already uses for "no next hop
decided yet") to mean "addressed to all direct neighbors," not one
specific node.

## Application DATA payload (Phase 7, rides inside `MSG_DATA`)

Phase 4 built the real hop-by-hop reliable-delivery mechanism
(`reliability::send()`), but nothing called it — see
[decisions.md](decisions.md#reliabilitysend-has-no-live-automatic-caller-in-phase-4--no-application-data-source-was-invented).
Phase 7 resolves that gap with the demo's minimum legitimate application
workload: `NODE_A` periodically sends its own latest POT/LDR readings to
`NODE_S`. Layout inside `MeshPacket.payload[]` (`apptraffic_core::DataWire`,
`src/apptraffic/apptraffic_core.cpp`):

```
Offset  Size  Field         Notes
0       2     appSeq        application-level packet counter (see below) — NOT MeshPacket.sequence
2       2     potValue      raw ADC reading, 0-4095 (12-bit) — anomaly::getTelemetry(POT).raw_value
4       2     ldrValue      raw ADC reading, 0-4095 (12-bit) — anomaly::getTelemetry(LDR).raw_value
8       4     timestampMs   sender's millis() at encode time
```

10 bytes total (`apptraffic_core::DATA_WIRE_SIZE`), well under
`PACKET_MAX_PAYLOAD` (64) — 54 bytes of headroom remains. Like
`RouteAdWire`'s fields, every field here is read back via `memcpy()`
(`apptraffic_core::decodeData()`), never a raw pointer cast — see this
file's "Layout notes" at the top for why that matters on real Xtensa
hardware. Sent via `reliability::send(NODE_S, payload, len, priority)` —
never a raw `transport::send()` call — so it gets a real
`MeshPacket.sequence`, hop-by-hop ACK, bounded retry, and duplicate
filtering exactly like any other `MSG_DATA` traffic.

**Three distinct, non-interchangeable sequence/counter concepts now exist
in this stack — do not conflate any of them:**

| Concept | Owner | Scope | Purpose |
|---|---|---|---|
| `MeshPacket.sequence` | `reliability_core::nextSequence()` | per-source, spans this packet's entire multi-hop lifetime | packet identity + duplicate filtering (Part 6, Phase 4) |
| GUI telemetry envelope `seq` | `telemetry.cpp`'s `g_seq` | per-node, numbers every emitted JSON line | GUI sequence-gap/reconnect detection (Phase 6) |
| `apptraffic` `appSeq` | `apptraffic_core::State.appSeqCounter` | `NODE_A`'s own application-packet count | a human/demo-readable "packet #N" counter inside the application payload itself — carried as ordinary payload bytes, invisible to routing/reliability |

`appSeq` is transported *inside* the application payload precisely because
it's an application-layer concept — `reliability_core` never reads it, and
losing/reordering it would not affect delivery, ACKing, or duplicate
detection, all of which key off `MeshPacket.sequence` alone.

**Priority trigger:** `NODE_A` reads Serial input (`apptraffic.cpp`'s
`drainPriorityTrigger()`) for a single recognized byte (`'p'`/`'P'`); on
seeing one, the *next* application packet is sent with `priority=true`
(`MeshPacket.priority = 1`), taking the existing structural priority
override (§5.3) — never re-implemented, never routed differently by
`apptraffic` itself. One keypress produces exactly one priority packet,
then reverts to `NORMAL` — see
[decisions.md](decisions.md#priority-traffic-trigger-a-single-serial-character-pp-read-on-node_a-not-a-new-command-protocol).

## What's deliberately NOT in this packet yet

- **No TTL / hop-count on `MeshPacket` itself — revisited, still not
  added, as of Phase 4.** Phase 1's original reasoning ("not needed yet...
  revisit when [the reliability/forwarding] layer is built") explicitly
  pointed at this phase as the point to reconsider. Phase 4 does now
  implement real hop-by-hop relaying of received `MSG_DATA` packets, and
  the question was revisited for real — the conclusion is still no new
  field, but now for a different, positive reason: loop prevention relies
  on `routing_core`'s already-proven correctness (a node never selects
  itself as next hop), a new `nextHop != prevHop` guard, and the Part 6
  duplicate filter as defense in depth, rather than on a TTL ceiling. See
  [decisions.md](decisions.md#forwarding-loop-prevention-relies-on-routing_core-correctness--a-next-hop-not-prev-hop-guard--the-duplicate-filter--no-new-ttl-field)
  for the full reasoning, superseding
  [decisions.md](decisions.md#no-ttlhop-count-field-added-to-meshpacket-in-phase-1)'s
  original (now resolved) open question.
- **No CRC/checksum field.** ESP-NOW/WiFi already provides frame-level
  integrity checking (FCS) at the 802.11 MAC layer below ESP-NOW; a
  duplicate application-level checksum wasn't asked for and isn't obviously
  needed. Revisit only if real hardware testing shows corrupted frames
  getting through.
- ~~No routing-table snapshot embedded in the packet~~ — **resolved in
  Phase 1**: it rides inside `MeshPacket.payload` as `MSG_HEARTBEAT`, not a
  new `MessageType`. See the route-advertisement payload section above.

## Sending/receiving pattern (real as of Phase 4)

Application-level sending goes through `reliability::send()`, not a raw
`transport::send()` call — it assigns a real sequence number, resolves the
next hop via the Phase 1/2 routing decision, and tracks the hop-
transmission for ACK/retry:

```cpp
uint8_t payload[] = { /* ... */ };
bool inFlight = reliability::send(NODE_S, payload, sizeof(payload), /*priority=*/false);
// inFlight == true means the frame is in flight and being tracked — NOT
// that delivery is confirmed. See docs/decisions.md for why the ESP-NOW
// send callback alone is never treated as delivery evidence.
```

`reliability::send()`'s own implementation (`src/reliability/reliability.cpp`)
shows the low-level pattern this builds on:

```cpp
MeshPacket pkt;
packetInit(pkt, MSG_DATA, THIS_NODE_ID, destination);
pkt.sequence = reliability_core::nextSequence(state);  // Phase 4 — see decisions.md
pkt.payload_len = /* fill payload, set len */;
pkt.timestamp_ms = millis();

transport::send(nodeInfo(nextHop).mac, reinterpret_cast<const uint8_t*>(&pkt), packetWireSize(pkt));
```

```cpp
void onTransportRx(const transport::RxEvent& evt) {
  if (evt.len < PACKET_HEADER_SIZE) return;  // too short to be a MeshPacket

  MeshPacket pkt;
  memcpy(&pkt, evt.data, min(evt.len, sizeof(MeshPacket)));  // never cast evt.data directly
  // ... hands the same (pkt, rssi) to routing::/predictor::/reliability::onPacketReceived()
}
```
