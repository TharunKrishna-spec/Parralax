# Architecture

Status: **Phase 6 — transport + routing + predictor + anomaly + reliability
+ telemetry all real; UCB1 adaptive routing implemented as an optional,
compile-time-gated stretch layer (disabled by default). Firmware now
serializes the frozen firmware<->GUI JSON contract for real — nothing left
in `src/` is a stub interface.** This document describes what exists now
and the shape it's built to grow into. It does not describe algorithms
that aren't implemented yet — see [known-issues.md](known-issues.md) for
those.

Source of truth for the *design* (topology, layer stack, algorithms) is
[`implementation-guide.html`](../implementation-guide.html) at the repo root.
This document explains how that design maps onto actual firmware code.

## Node topology

Five roles, one firmware image:

```
        A (source)
       / \
      /   \  A-S: weak, direct, 1 hop
     B     \  (priority path only)
     |      \
     S ------+
     |
     (via B: A -> B -> S, the normal path)

     A -> C -> D -> S  (backup path, used when B degrades)
```

Edges (who is a direct ESP-NOW neighbor of whom):

| Node | Role | Neighbors | OLED | Buzzer |
|------|------|-----------|------|--------|
| A | source | B, C, S | no | yes |
| B | relay | A, S | no | yes |
| C | relay | A, D | yes (local anomaly flags) | yes |
| D | relay | C, S | no | yes |
| S | sink/root | B, D, A | yes (mesh telemetry) | yes |

The A-S link is deliberately the weakest link in the topology despite being
the shortest path. That's not an oversight — it's what makes the priority
override (§5.3 of the implementation guide) visibly different from normal
quality-optimal routing: priority traffic takes the weak direct hop on
purpose, normal traffic avoids it.

This adjacency is encoded once, in [`core/node_id.h`](../firmware/PredictiveMesh/src/core/node_id.h)'s
`neighborsOf()` — nothing else in the firmware hardcodes topology.

## Layer stack

Bottom to top, matching implementation-guide.html §01:

| Layer | State | Lives in |
|---|---|---|
| Transport (ESP-NOW) | **Implemented** (Phase 0) | `src/transport/` |
| Reliability (hop-by-hop ACK, bounded retry, dup-filter, forwarding) | **Implemented** (Phase 4) | `src/reliability/` |
| Routing (distance-vector + priority override + link-health-aware selection) | **Implemented** (Phase 1, extended Phase 2) | `src/routing/` |
| Predictor (RSSI EWMA/slope + PDR + staleness fusion) | **Implemented** (Phase 2, PDR live-fed Phase 4) | `src/predictor/` |
| Anomaly (MAD Z-score + flatline + sensor state machine) | **Implemented** (Phase 3) | `src/anomaly/` |
| UCB1 adaptive ranking (stretch, optional) | **Implemented, disabled by default** (Phase 5) | `src/ucb1/` |
| Reporting (Serial/WebSerial JSON telemetry) | **Implemented** (Phase 6) — OLED still not wired | `src/telemetry/` |

Data is meant to flow bottom-up: raw radio -> statistics -> routing
decisions -> reliability -> reporting. Phase 0 wired the bottom layer for
real; Phase 1 wired routing on top of it, consuming
`transport::RxEvent`/`TxEvent` instead of touching `esp_now_*` APIs
directly — the receive path is: ESP-NOW callback -> `transport::RxEvent`
-> `main.cpp` parses a `MeshPacket` -> `routing::onPacketReceived()`. Phase
2 added the predictor as a peer consumer of that same parsed packet +
RSSI, not a layer routing depends on structurally — `main.cpp` hands the
same `(pkt, rssi)` to both `routing::onPacketReceived()` and
`predictor::onPacketReceived()`. Routing then closes the loop by *reading*
predictor's per-neighbor health via `predictor::isUnhealthy()` when
choosing a NORMAL next hop (see "Routing + predictor integration (Phase
2)" below). Phase 4 adds reliability as a third peer consumer of the same
`(pkt, rssi)` (`reliability::onPacketReceived()`), and closes a different
loop: reliability *writes into* the predictor via `predictor::onSendResult()`
whenever a real hop-transmission's outcome (ACK'd or timed-out) becomes
known — see "Reliability layer (Phase 4)" below. Phase 6 adds telemetry as
a consumer one level removed from the packet-receive path: it never
registers its own transport/packet callback, instead reading routing's/
predictor's/anomaly's/reliability's already-real state through their
existing read-only accessors (plus one small new accessor each on
`predictor`/`routing`) and their existing event-callback mechanism — see
"Telemetry layer (Phase 6)" below.

## Firmware layout and why it's structured this way

```
firmware/PredictiveMesh/
├── PredictiveMesh.ino      <- thin entry point (setup()/loop() only)
└── src/
    ├── main.cpp / main.h   <- app::setup() / app::loop(), wires all modules together
    ├── config.h            <- THIS_NODE_ID, MESH_WIFI_CHANNEL, pin map (the one file that
    │                          differs in *value* per board, never in code)
    ├── core/
    │   ├── node_id.h        <- NodeId/NodeRole/NodeInfo, the topology table, neighbor lookup
    │   ├── packet.h          <- MeshPacket wire struct
    │   ├── message_types.h   <- MessageType enum
    │   └── logger.h/.cpp     <- Serial logging (see docs/decisions.md for placement)
    ├── transport/
    │   └── espnow_transport.h/.cpp   <- owns all esp_now_*/WiFi calls
    ├── routing/
    │   ├── routing_core.h/.cpp   <- pure distance-vector algorithm + link-health-aware
    │   │                            selection, no Arduino dependency
    │   └── routing.h/.cpp        <- Arduino-facing adapter (millis/logger/transport::send)
    ├── predictor/
    │   ├── predictor_core.h/.cpp <- pure EWMA/slope/PDR/hysteresis algorithm, no Arduino dependency
    │   └── predictor.h/.cpp      <- Arduino-facing adapter (millis/logger)
    ├── anomaly/
    │   ├── anomaly_core.h/.cpp   <- pure median/MAD/modified-Z/flatline/state-machine algorithm, no Arduino dependency
    │   └── anomaly.h/.cpp        <- Arduino-facing adapter (analogRead/millis/logger)
    ├── reliability/
    │   ├── reliability_core.h/.cpp   <- pure packet-identity/dup-filter/retry/timeout/statistics algorithm, no Arduino dependency
    │   └── reliability.h/.cpp        <- Arduino-facing adapter (MeshPacket construction/parsing, transport::send, millis/logger)
    ├── ucb1/
    │   ├── ucb1_core.h/.cpp   <- pure bandit-statistics/UCB1-selection algorithm, no Arduino dependency, always compiled
    │   └── ucb1.h/.cpp        <- Arduino-facing adapter; .cpp body entirely `#if ENABLE_UCB1`-gated (config.h)
    └── telemetry/
        ├── telemetry_core.h/.cpp   <- pure JSON envelope/payload construction (mesh-json/v1), no Arduino dependency
        └── telemetry.h/.cpp        <- Arduino-facing adapter (reads routing/predictor/anomaly/reliability state, Serial.println())

firmware/PredictiveMesh/test/
├── test_routing_core.cpp      <- host-compiled (g++) unit tests for routing_core (incl. Phase 5's enumerateCandidates)
├── test_predictor_core.cpp    <- host-compiled (g++) unit tests for predictor_core
├── test_anomaly_core.cpp      <- host-compiled (g++) unit tests for anomaly_core
├── test_reliability_core.cpp  <- host-compiled (g++) unit tests for reliability_core
├── test_ucb1_core.cpp         <- host-compiled (g++) unit tests for ucb1_core
└── test_telemetry_core.cpp    <- host-compiled (g++) unit tests for telemetry_core
                                   see docs/testing.md for all six
```

### Why `src/` and not files flat in the sketch folder

This is a real Arduino build-system feature, not a workaround. From the
[Arduino sketch specification](https://arduino.github.io/arduino-cli/latest/sketch-specification/#src-subfolder):

- Files placed directly in the sketch's root folder (next to the `.ino`)
  are compiled, but **subfolders are ignored** — you couldn't have
  `transport/espnow_transport.cpp` sitting loose in the sketch root.
- A subfolder specifically named **`src`** is the one documented exception:
  everything under it, recursively, is compiled using the same rules as a
  *library* — subfolders included, and the folder tree is added to the
  compiler's include search path.

That's exactly the module layout implementation-guide.html's Phase 0 spec
asked for (`core/`, `transport/`, `routing/`, ...), so it maps onto this
convention directly. No custom build scripts, no PlatformIO — this compiles
in stock Arduino IDE with the ESP32 board package installed.

The root `.ino` stays a thin shim (`#include "src/main.h"` + two one-line
functions) because `.ino` files get special preprocessing (implicit
function prototyping, automatic `Arduino.h` inclusion) that plain `.cpp`
files don't — keeping real logic in `.cpp`/`.h` avoids relying on that
sketch-only magic anywhere except the two required entry points.

### Why includes are explicit relative paths

Every cross-module `#include` in this codebase uses an explicit relative
path (e.g. `"../core/logger.h"`), never a bare `"logger.h"` that relies on
`src/` being on the include search path. That implicit behavior is real (see
above) but it's an Arduino-build-system-specific side effect; relative
paths work identically under any build system and make the dependency
between files visible at the point of use.

## Node identity mechanism

`config.h` sets one macro, `THIS_NODE_ID`, to one of `NODE_A`/`NODE_B`/
`NODE_C`/`NODE_D`/`NODE_S`. Everything else — role, name, whether this board
has an OLED, its neighbor list — is looked up from that single value via
`core/node_id.h`'s `NODE_TABLE` and `neighborsOf()`. No file outside
`config.h` contains a per-node `#ifdef`/`if` branch; see
[decisions.md](decisions.md) for why this was chosen over MAC-based
auto-detection.

## Packet flow (as of Phase 4)

1. `transport::begin()` brings up WiFi in station mode, fixes the channel,
   initializes ESP-NOW, and registers the recv/send callbacks.
2. The firmware registers the ESP-NOW broadcast peer (`FF:FF:FF:FF:FF:FF`)
   so two flashed boards can immediately exchange raw ESP-NOW frames without
   needing to already know each other's MAC address — see
   [decisions.md](decisions.md).
3. Any received frame is logged (`[RX] src=... rssi=... len=...`) with a
   **real** RSSI value read from `info->rx_ctrl->rssi`, then `main.cpp`'s
   `onTransportRx()` `memcpy()`s it into a local `MeshPacket` (never
   pointer-casts the raw buffer — see `core/packet.h`) and hands it to
   **all three** `routing::onPacketReceived()`, `predictor::onPacketReceived()`,
   and `reliability::onPacketReceived()` — the same parsed `(pkt, rssi)`,
   three independent consumers.
4. `routing::onPacketReceived()` always refreshes neighbor liveness for
   `pkt.prev_hop`; if the packet is `MSG_HEARTBEAT`, it also parses the
   payload as a distance-vector advertisement and folds it into the
   routing table (`routing_core::applyRouteAdvertisement()`).
5. `predictor::onPacketReceived()` feeds `pkt.prev_hop`'s RSSI into that
   neighbor's EWMA/slope pipeline (`predictor_core::onRssiSample()`),
   recomputes `link_score`, and logs `[PREDICTOR] neighbor=... rssi_ewma=...
   slope=... pdr=... score=... health=...` — see "Routing + predictor
   integration (Phase 2)" below for what evidence feeds the score.
6. `reliability::onPacketReceived()` reacts only to `MSG_DATA`/`MSG_ACK`
   (ignoring `MSG_HEARTBEAT`, routing's own concern): an `MSG_ACK` is
   matched against a pending hop-transmission and, on a real match, fed
   into `predictor::onSendResult(neighbor, true)`; an `MSG_DATA` packet is
   always hop-ACKed back to its sender first, then checked against the
   duplicate cache, then either delivered locally or forwarded via
   `routing::selectNextHop()` — see "Reliability layer (Phase 4)" below.
7. `app::loop()` calls `routing::tick()`, `predictor::tick()`, then
   `reliability::tick()` every iteration. `routing::tick()` rate-limits
   itself: it sends this node's own HELLO/route-advertisement beacon once
   per `ROUTING_HELLO_INTERVAL_MS`, and sweeps for stale neighbor/route
   entries older than `ROUTING_ENTRY_TIMEOUT_MS`. `predictor::tick()`
   independently sweeps every direct neighbor for the staleness fast-path
   (`PREDICTOR_STALENESS_TIMEOUT_MS`, deliberately faster than routing's
   own timeout — see [parameters.md](parameters.md)). `reliability::tick()`
   sweeps every pending hop-transmission for `RELIABILITY_ACK_TIMEOUT_MS`
   expiry, resending or declaring final failure — each timed-out attempt
   also feeds `predictor::onSendResult(neighbor, false)`.
8. Whenever something needs a next hop for a destination —
   `routing::getNextHop(destination, priority)` or the packet-shaped
   wrapper `routing::selectNextHop(pkt)` — routing builds a per-neighbor
   health mask from `predictor::isUnhealthy()`, then picks the best
   surviving candidate from its table (excluding priority-only edges and,
   for NORMAL traffic only, preferring healthy candidates — see below),
   logs `[ROUTE] dst=... next=... hops=... priority=...`, and fires a
   `ROUTE_SELECTED` event to whatever's registered via
   `routing::setEventCallback()`. Both `reliability::send()` (self-
   originated) and `reliability`'s own forwarding path (Part 7) call
   through this exact same decision — reliability never re-derives a
   routing choice itself.
9. Independently of any received packet, `app::loop()` samples both
   sensors every `SENSOR_SAMPLE_INTERVAL_MS` — `anomaly::sample(POT)` then
   `anomaly::sample(LDR)`, each a real `analogRead()` fed through
   `anomaly_core::evaluate()`. `anomaly::tick()` also runs every iteration,
   independently sweeping both sensors for the staleness state (Part 4 —
   see "Anomaly / sensor-health layer (Phase 3)" below). This path is
   completely disjoint from the packet-receive path above — sensor
   readings never touch `MeshPacket`, and mesh traffic never touches
   sensor state.
10. `app::loop()`'s final call each iteration is `telemetry::tick()`, which
    rate-limits itself per `TELEMETRY_*_INTERVAL_MS` and reads real state
    from routing/predictor/anomaly/reliability to emit periodic
    HEARTBEAT/NODE_STATUS/LINK_UPDATE/PREDICTION/SENSOR_STATUS/STATISTICS
    lines over Serial. Event-driven EVENT/ERROR lines are emitted
    synchronously from `main.cpp`'s existing routing/predictor/anomaly/
    reliability event-callback bodies, not from `tick()` — see "Telemetry
    layer (Phase 6)" below.

See [protocol.md](protocol.md) for why the wire format only grew real
*use* of already-existing fields (Phase 4), not new bytes, and
[testing.md](testing.md) for how all four algorithms are validated without
hardware.

## Routing layer (Phase 1)

`src/routing/` is split into two files with different rules, on purpose:

- **`routing_core.h`/`.cpp`** — the actual distance-vector algorithm:
  neighbor table, per-(destination, via-neighbor) route candidate matrix,
  Bellman-Ford-style relaxation (`applyRouteAdvertisement`), staleness
  sweep (`expireStale`), and selection (`selectNextHop`). Zero Arduino
  dependency — no `millis()`, no `Serial`, no `esp_now_*`. Every function
  takes `now` as a parameter instead of reading the clock itself. This is
  what `firmware/PredictiveMesh/test/test_routing_core.cpp` compiles and
  runs directly with a host compiler.
- **`routing.h`/`.cpp`** — the thin Arduino-facing adapter. Owns the one
  `RoutingState` instance, builds/parses the wire format for beacons,
  calls `millis()`/`logger::*`/`transport::send()`, and is the only half
  of this module a unit test can't exercise directly.

Two topology facts drive the demo behavior (both sourced from
implementation-guide.html §01, not invented):
- The full adjacency (`neighborsOf()` in `core/node_id.h`, unchanged since
  Phase 0) determines who beacons reach directly, and therefore which
  `(destination, via-neighbor)` candidates can ever exist.
- The A↔S edge is specifically marked **priority-only**
  (`routing_core::isPriorityOnlyEdge`) because §01's own diagram labels it
  `"(priority path only)"`. NORMAL selection excludes it; PRIORITY
  selection doesn't. See
  [decisions.md](decisions.md#a↔s-edge-modeled-as-priority-only-excluded-from-normal-selection)
  for the full reasoning — this is the one place Phase 1 had to make a
  judgment call rather than just implement the spec literally.

## Predictor layer (Phase 2)

`src/predictor/` follows the exact same pure-core/adapter split as
routing:

- **`predictor_core.h`/`.cpp`** — the actual EWMA/least-squares-slope/PDR/
  hysteresis math: per-neighbor `NeighborLinkState` (RSSI EWMA + ring
  buffer, PDR EWMA, fused `link_score`, hysteresis state), `onRssiSample`,
  `onSendOutcome`, `tickStaleness`. Zero Arduino dependency. This is what
  `firmware/PredictiveMesh/test/test_predictor_core.cpp` compiles and runs
  directly with a host compiler — see [testing.md](testing.md).
- **`predictor.h`/`.cpp`** — the thin Arduino-facing adapter. Owns the one
  `PredictorState` instance, feeds it real RSSI from the same
  `main.cpp` receive dispatch routing already uses, logs the evidence
  behind every score, and fires `LINK_SCORE_UPDATED`/`LINK_DEGRADING`/
  `LINK_UNHEALTHY`/`LINK_RECOVERED` events.

The pipeline, per direct neighbor: raw RSSI → EWMA (`PREDICTOR_RSSI_EWMA_ALPHA`)
→ least-squares slope over `PREDICTOR_SLOPE_WINDOW` samples of the *smoothed*
signal → `degrade_term` → fused with PDR into `link_score` (`PREDICTOR_LINK_SCORE_W1`/`W2`)
→ a two-threshold, debounced hysteresis state machine
(`PREDICTOR_HYSTERESIS_T_LOW`/`T_HIGH`, `PREDICTOR_CONSECUTIVE_BAD_COUNT`/`GOOD_COUNT`)
classifies the link HEALTHY or UNHEALTHY. An independent staleness fast-path
(`PREDICTOR_STALENESS_TIMEOUT_MS`) can force UNHEALTHY immediately on
silence, bypassing the debounce entirely — see
[parameters.md](parameters.md) for the full formula/threshold table and
[decisions.md](decisions.md) for why each constant has the value it does.

**PDR is now real and wired, as of Phase 4** — `predictor::onSendResult()`
is called for real by `reliability`'s own ACK/retry outcomes, per attempt
(not per packet): `true` when a real `MSG_ACK` matches a pending hop-
transmission, `false` for every individually-timed-out attempt. This
resolves the Phase 2 measurement boundary
([decisions.md](decisions.md#pdr-measurement-boundary-not-wired-to-live-send-outcomes-in-phase-2))
now that real unicast `MSG_DATA` traffic — with a real MAC-layer-independent
application ACK — exists to measure. See
[decisions.md](decisions.md#pdr-is-fed-per-attempt-not-per-packet--and-never-from-the-raw-esp-now-send-callback)
for the exact attempt-vs-packet semantics and why the raw ESP-NOW send
callback is never used as delivery evidence. **What's still true:** nothing
in the current firmware automatically *calls* `reliability::send()` yet
(see [decisions.md](decisions.md#reliabilitysend-has-no-live-automatic-caller-in-phase-4--no-application-data-source-was-invented)) —
so on real hardware, PDR stays at its neutral default until something
(a future phase, or a manual trigger) actually originates `MSG_DATA`
traffic. The wiring is real; live traffic is not yet.

**Routing + predictor integration**: `routing::getNextHop()` builds a
`bool[NODE_ID_COUNT]` health mask from `predictor::isUnhealthy()` and
passes it into `routing_core::selectNextHop()`'s new optional
`neighborUnhealthy` parameter. NORMAL selection now prefers a healthy
candidate over an unhealthy one at the same or worse hop count, falling
back to the best available candidate if every eligible one is unhealthy
(health gates *preference*, never *validity* — Phase 1's staleness/
invalidity mechanism alone still controls whether a candidate exists at
all). PRIORITY selection ignores the health mask completely and
unconditionally — link_score can never suppress the priority override.
Critically, this sits **alongside**, not **instead of**, the
priority-only-edge rule above: without real hardware, `link_score` cannot
yet organically make the A-S edge look worse than A-B, so removing the
topology-level exclusion would silently break the documented "NORMAL
avoids the weak direct link" demo behavior. See
[decisions.md](decisions.md#link-health-integrated-into-routing_coreselectnexthop-alongside-not-instead-of-the-priority-only-edge-rule)
for the full reasoning, including the precise condition under which the
exclusion should eventually be removed.

## Anomaly / sensor-health layer (Phase 3)

`src/anomaly/` follows the same pure-core/adapter split as routing and
predictor:

- **`anomaly_core.h`/`.cpp`** — the real algorithm: boot-time median/MAD
  calibration (with a variance safety envelope and bounded retry), the
  modified-Z-score spike/jump detector, an independent flatline/stuck
  detector, and a 6-state, debounced, recovering state machine. Zero
  Arduino dependency — takes a generic, timestamped `SensorObservation` in,
  never touches `analogRead()`/`millis()` directly. This is what
  `firmware/PredictiveMesh/test/test_anomaly_core.cpp` compiles and runs
  directly with a host compiler.
- **`anomaly.h`/`.cpp`** — the thin Arduino-facing adapter. Owns two
  `SensorCore` instances (POT at `GPIO34`, LDR at `GPIO35`), performs the
  blocking boot-calibration sequence, calls `analogRead()` every
  `SENSOR_SAMPLE_INTERVAL_MS`, logs the evidence behind every evaluation,
  and fires `SENSOR_ANOMALY`/`SENSOR_FLATLINE`/`SENSOR_RECOVERED`/
  `SENSOR_STALE`/`SENSOR_INVALID` events.

**Sensor abstraction.** `anomaly_core` never hardcodes which physical
sensor it's evaluating — it accepts a generic
`SensorObservation{sensor_id, timestamp_ms, value, valid}`, reusable for
any future sensor, not just the potentiometer/LDR pair. `sensor_id` is an
opaque integer the adapter assigns; the core never branches on it. See
[decisions.md](decisions.md#sensor-abstraction-is-generic-sensorobservation-not-hardwired-to-the-potentiometerldr).

**State machine.** `WARMUP → NORMAL → {ANOMALY, FLATLINE}`, with `STALE`
and `INVALID` reachable independently from any state. Entry into
`ANOMALY`/exit back to `NORMAL` are debounced (`ANOMALY_CONSECUTIVE_COUNT`/
`ANOMALY_RECOVERY_COUNT`, 2 samples each — the guide's own MAD-Z pseudocode
has no such debounce, but this phase's own requirements do); `FLATLINE`'s
entry is inherently persistent via `ANOMALY_STUCK_N` and its exit is
separately debounced (`ANOMALY_FLATLINE_RECOVERY_COUNT`). When a sample
would trigger both detectors at once, FLATLINE wins — see
[decisions.md](decisions.md#one-discrete-sensorstate-not-two-independent-booleans--with-a-documented-flatline-over-anomaly-priority)
for the full reasoning and why this doesn't contradict
implementation-guide.html's "both flags reported independently" framing
(both raw evidence values stay independently exposed in telemetry
regardless of which state the top-level label picks).

**Telemetry & events.** `anomaly::getTelemetry(sensor)` exposes a complete
`SensorTelemetry` snapshot (raw value, median, MAD, modified-Z, threshold,
flatline state/duration, sensor state, validity) for any future consumer.
Events fire on real state transitions only, not every sample. **No GUI
wire-format claim is made** — this phase's task spec referenced an
external GUI telemetry contract that does not exist anywhere in this
repository; see
[decisions.md](decisions.md#gui-telemetry-contract-referenced-but-not-found-in-this-repository--flagged-not-fabricated)
and [known-issues.md](known-issues.md).

**Separation from network health.** `anomaly_core`/`anomaly.cpp` have no
reference to `routing_core`/`predictor_core` or vice versa — a sensor
anomaly never affects a routing decision. See
[decisions.md](decisions.md#sensor-health-and-networklink-health-are-separate-failure-domains--no-coupling-added).

## Reliability layer (Phase 4)

`src/reliability/` follows the same pure-core/adapter split as routing,
predictor, and anomaly:

- **`reliability_core.h`/`.cpp`** — the real algorithm: packet identity
  (`PacketId{source, sequence}`), a fixed-size pending-hop-transmission
  pool (`beginTx`/`cancelTx`/`onAckReceived`/`tickTimeouts`), a duplicate-
  detection cache with TTL-based expiry and ring-buffer replacement
  (`isDuplicateAndRecord`), and deterministic statistics counters. Zero
  Arduino dependency, zero payload storage (a resend needs the original
  bytes, which only the adapter owns — see
  [decisions.md](decisions.md#reliability_core-split-out-as-an-arduino-free-pure-module-phase-4)).
  This is what `firmware/PredictiveMesh/test/test_reliability_core.cpp`
  compiles and runs directly with a host compiler.
- **`reliability.h`/`.cpp`** — the thin Arduino-facing adapter. Owns the
  one `ReliabilityState` instance plus a parallel array of raw
  `MeshPacket` bytes for retransmission, constructs/parses `MSG_ACK`
  packets, calls `transport::send()`/`millis()`/`logger::*`, and fires
  `PACKET_TX`/`PACKET_ACK`/`PACKET_RETRY`/`PACKET_DELIVERED`/`PACKET_DROP`/
  `DUPLICATE_DROPPED`/`PACKET_RECEIVED` events.

**Packet identity (Part 1).** `(source, sequence)`, reusing `MeshPacket`'s
own existing header fields — no new wire bytes. Preserved unchanged across
every hop of a forward. Deliberately distinct from the GUI telemetry
contract's own envelope `seq` (see
[known-issues.md](known-issues.md) and
[decisions.md](decisions.md#packet-identity-is-source-sequence-reusing-meshpackets-existing-header-fields--no-new-wire-format)).

**Unicast (Part 2).** `reliability::send()` and the forwarding path both
resolve a real peer MAC via `nodeInfo(neighbor).mac` and call
`transport::send()` — never broadcast. MACs remain the Phase 0 all-zero
placeholder until hardware exists; a send to an unregistered peer is
rejected synchronously by `esp_now_send()`, which the adapter treats as an
honest, immediate failure (`reliability_core::cancelTx()`), never a
fabricated success.

**ACK (Part 3).** `MSG_ACK` is a real, distinct wire message from the raw
ESP-NOW `TxCallback` — the latter is never treated as delivery evidence
(see the dedicated decisions.md entry). An ACK identifies which `(source,
sequence)` `MSG_DATA` packet it acknowledges; ACK packets are themselves
fire-and-forget, never acknowledged.

**Retry/timeout (Parts 4/5).** `reliability_core::tickTimeouts()`, called
every `reliability::tick()`, is a single non-blocking sweep over a small
fixed array — never blocks the main loop. `RELIABILITY_MAX_RETRIES`/
`RELIABILITY_ACK_TIMEOUT_MS` bound every hop-transmission to a deterministic
worst-case time-to-failure (800ms — see [parameters.md](parameters.md)).

**Duplicate filter (Part 6).** A TTL-expiring, ring-buffer-replaced cache
of recently-seen `(source, sequence)` identities. Every received `MSG_DATA`
is hop-ACKed *before* the duplicate check — the hop transmission itself
succeeded regardless of whether the application has already seen this
packet (see the dedicated decisions.md entry).

**Forwarding (Part 7).** A packet not addressed to this node is forwarded
via the exact same `routing::selectNextHop()` decision routing already
makes for its own purposes — reliability never re-derives a routing
choice. Loop prevention relies on `routing_core`'s proven correctness, a
new `nextHop != prevHop` guard, and the duplicate filter as defense in
depth — no new TTL/hop-count field (see the dedicated decisions.md entry,
which supersedes Phase 1's original "not needed yet" TTL note).

**PDR integration (Parts 8/9).** `predictor::onSendResult()` is fed per
individual attempt, not per packet series — see the two dedicated
decisions.md entries for the exact semantics and why PDR represents
per-hop delivery only, never end-to-end. `reliability::send()` itself has
no automatic caller yet in Phase 4 (see decisions.md) — the mechanism is
real; live application traffic is a later phase's decision.

**Events (Part 10) & statistics (Part 11).** One event enum/callback,
matching the routing/predictor/anomaly convention exactly (plus
`PACKET_RECEIVED`, added so a locally-delivered `MSG_DATA` packet has a
real consequence rather than being dead code — see decisions.md).
`reliability::getStatistics()` exposes a complete counter snapshot
(`packetsSent`/`packetsDelivered`/`packetsFailed`/`retries`/
`duplicatesDropped`/`acknowledgements`/`lastLatencyMs`) — not yet
serialized over JSON, since the existing firmware telemetry architecture
doesn't support that yet (`telemetry.cpp` is still the Phase 0 stub).

**Routing interaction (Part 12).** `application -> routing::selectNextHop()
-> reliability::send()/forwarding -> transport::send()`. Reliability reads
routing's decision; it never writes to or bypasses it.

## UCB1 adaptive routing (Phase 5 — stretch, optional)

implementation-guide.html §06 labels this "[stretch, optional] UCB1
multi-armed bandit next-hop selection... only if ahead of schedule" — the
only stretch feature named in the guide's own roadmap, and the only layer
in this firmware that's disabled by default (`ENABLE_UCB1=0`, config.h).
`src/ucb1/` follows the same pure-core/adapter split as every other layer:

- **`ucb1_core.h`/`.cpp`** — the real algorithm: a fixed-size
  `[destination][nextHop]` bandit-statistics table
  (`ArmStats{everObserved, attempts, successes, failures}`), the standard
  UCB1 selection formula, health-tiering, and the loop-prevention
  exclusion. Zero Arduino dependency, and — unlike the adapter — **always
  compiled**, regardless of `ENABLE_UCB1`, exactly like every other
  `*_core` module. This is what
  `firmware/PredictiveMesh/test/test_ucb1_core.cpp` compiles and runs
  directly with a host compiler.
- **`ucb1.h`/`.cpp`** — the thin Arduino-facing adapter. `ucb1.h`'s
  declarations always exist; `ucb1.cpp`'s entire function-body content is
  wrapped in `#if ENABLE_UCB1 ... #endif`, compiling to an empty
  translation unit when disabled.

**Why UCB1 is optional and never replaces existing routing.**
implementation-guide.html's own phrase — "as an alternative to
distance-vector" — could be read as UCB1 replacing the routing table
outright. This phase's actual instructions explicitly override that
literal reading: UCB1 is "an additional adaptive ranking layer" that
"ranks only among valid candidates," never able to "create a route the
normal routing layer would consider invalid." See
[decisions.md](decisions.md#ucb1-is-an-additional-ranking-layer-never-a-replacement-for-distance-vector-routing--resolving-the-guides-alternative-to-framing)
for the full reasoning.

**The decision pipeline** (route validity → link-health → routing policy
→ UCB1 ranking → next hop), for NORMAL traffic only, with `ENABLE_UCB1=1`:

```
routing::getNextHopInternal(destination, priority=false, excludeNextHop)
  1. routing_core::selectNextHop() computes its own Phase 1/2 baseline
     pick (unchanged — this call is identical whether or not UCB1 runs).
  2. routing_core::enumerateCandidates() lists every currently-valid,
     non-priority-only-edge, non-excluded candidate, health-annotated.
  3. ucb1::selectNextHop() -> ucb1_core::selectNextHop():
       a. drop any candidate == excludeNextHop (Part 8 loop guard)
       b. if any surviving candidate is healthy, rank ONLY the healthy
          subset (Part 6 — preserves Phase 2's own philosophy exactly)
       c. any zero-observation candidate wins immediately (Part 3)
       d. otherwise: argmax(meanReward + C*sqrt(ln(N)/n)), ties -> lowest NodeId
  4. If UCB1 returned a real candidate, it REPLACES the baseline pick from
     step 1; otherwise step 1's answer stands unchanged.
  5. Absolute safety net: if the final answer == excludeNextHop for any
     reason, it becomes NODE_ID_UNKNOWN (no route) instead.
```

For PRIORITY traffic, or with `ENABLE_UCB1=0`, only step 1 runs — this is
exactly Phase 4's original `getNextHop()` body, unchanged.

**Reward definition (Part 2).** One UCB1 trial = one resolved
hop-transmission SERIES's final outcome (a real `MSG_ACK` match, or
retries genuinely exhausted / a synchronous send rejection) — never an
individual radio retry. This reuses Phase 4's own already-established
packet-series-vs-attempt distinction rather than inventing a new boundary.
`reliability.cpp` calls `ucb1::onRouteOutcome(destination, nextHop, success)`
at exactly the three points that already represent a series's final state.
See [decisions.md](decisions.md#one-ucb1-trial--one-resolved-hop-transmission-series-never-an-individual-radio-retry).

**Zero-observation handling & determinism (Part 3/9).** An untried
candidate is always chosen first (standard forced-exploration UCB1
practice), which also structurally guarantees `N` (the total-trials term)
can never be 0 when the formula is actually evaluated — no undefined
`ln(0)` or divide-by-zero is possible. No randomness anywhere — identical
inputs always produce the identical selection.

**Link health (Part 6) & priority (Part 5).** Health-tiering happens
inside the same `ucb1_core::selectNextHop()` call, before ranking — a
candidate can never win UCB1 ranking purely on historical success while
currently unhealthy. Priority traffic never reaches UCB1 at all — it's a
structural fact (the caller never invokes `ucb1::selectNextHop()` when
`priority==true`), not a runtime check that could be bypassed.

**Loop prevention (Part 8).** Two independent, redundant layers: candidate
enumeration never includes the excluded next-hop, `ucb1_core::selectNextHop()`
also refuses to return it even if it somehow appeared, and
`getNextHopInternal()` has one final unconditional check regardless of
which path produced the answer. No `MeshPacket` field was added — see
[decisions.md](decisions.md#loop-prevention-excludenexthop-threaded-through-candidate-enumeration-plus-an-unconditional-final-safety-net).

**No decay (Part 7).** `ArmStats` counters accumulate for the program's
whole lifetime; memory is bounded by structure (fixed `NODE_ID_COUNT x
NODE_ID_COUNT` array), not by discarding old observations. Not required by
the guide, and not built speculatively — see
[decisions.md](decisions.md#no-decay--fixed-unbounded-in-time-but-bounded-in-size-counters-are-sufficient).

**Known limitation.** UCB1's ranking deliberately ignores hop count
entirely (see decisions.md) — the whole point is letting learned evidence
override the static heuristic. This also means, on a fresh boot with no
history, UCB1's zero-observation forced-exploration could pick a
worse-hop-count candidate first before any evidence exists to prefer
otherwise — an expected, bounded cost of exploration, not a bug.

## Telemetry layer (Phase 6)

`src/telemetry/` follows the same pure-core/adapter split as every other
layer:

- **`telemetry_core.h`/`.cpp`** — the real JSON construction: a small,
  bounds-checked `snprintf`-based `Writer` and one `buildXxx()` function
  per contract message type (`HELLO`/`HEARTBEAT`/`NODE_STATUS`/
  `LINK_UPDATE`/`ROUTE_UPDATE`/`PREDICTION`/`SENSOR_STATUS`/`EVENT`/
  `STATISTICS`/`ERROR`), plus the Part-K enum-classification/mapping
  functions (`classifyLink`, `linkStateStr`/`predictionStateStr`,
  `hysteresisStateStr`, `roleStr`, `routeReasonStr`, `sensorHealthStr`).
  Zero Arduino dependency, zero knowledge that routing_core/predictor_core/
  anomaly_core/reliability_core/ucb1_core exist — every function takes
  already-extracted plain data (floats, ints, small enums, `const char*`
  literals) and returns a complete JSON line. This is what
  `firmware/PredictiveMesh/test/test_telemetry_core.cpp` compiles and runs
  directly with a host compiler.
- **`telemetry.h`/`.cpp`** — the thin Arduino-facing adapter. Owns the
  boot-time `bootId`/per-envelope `seq` counter, reads real state via
  `predictor::linkState()`/`routing::getCandidates()` (two small, new,
  purely-additive read-only accessors added this phase — see below),
  `anomaly::getTelemetry()`, and `reliability::getStatistics()` (all
  pre-existing), calls `telemetry_core`'s builders, and `Serial.println()`s
  the result. Event-driven messages (`EVENT`/`ERROR`) are produced from
  `telemetry::onRouteEvent()`/`onLinkEvent()`/`onAnomalyEvent()`/
  `onReliabilityEvent()`, called from inside `main.cpp`'s existing single
  event-callback bodies (see decisions.md — not a second registered
  callback, since each source module supports only one).

**Layering (Part H).** Routing/predictor/anomaly/reliability/ucb1 have
zero awareness that telemetry, JSON, or a GUI exist — confirmed by
inspection (none of their headers/`.cpp` files include anything under
`src/telemetry/`). Only `main.cpp` (the existing cross-module wiring layer)
and `telemetry.cpp` itself know both sides. Two small, purely-additive
read-only accessors were added to expose data telemetry needs that no
existing accessor covered: `predictor::linkState(NodeId)` (the full
`NeighborLinkState`, mirroring `anomaly::getTelemetry()`'s existing
pattern) and `routing::getCandidates(NodeId, CandidateInfo*, uint8_t)` (a
thin wrapper around Phase 5's already-existing
`routing_core::enumerateCandidates()`, building the same health mask
`getNextHop()` itself builds). Neither changes any existing function's
behavior.

**Envelope (Part I).** Exactly the frozen contract's 7 fields
(`protocolVersion`/`type`/`nodeId`/`bootId`/`seq`/`timestampMs`/`payload`),
no renamed fields, no invented message names. `bootId` is a fresh
`esp_random()`-derived nonce generated once per boot (no persistent
storage exists anywhere in this project — see decisions.md); `seq` is a
telemetry-owned `uint32_t` counter, structurally independent of
`MeshPacket.sequence` (reliability's own packet identity) per the
contract's explicit warning not to conflate the two.

**Rate limiting (Part N).** One `TELEMETRY_*_INTERVAL_MS` constant per
periodic message type (`config.h`), each reproducing the frozen contract's
own stated frequency exactly (not re-derived): HEARTBEAT/NODE_STATUS/
SENSOR_STATUS/STATISTICS at 1000ms, LINK_UPDATE/PREDICTION at 250ms.
EVENT/ERROR are purely event-driven, no interval. ROUTE_UPDATE fires only
on genuine routing-table changes (`ROUTE_CHANGED`/`ROUTE_INVALIDATED`), not
on every next-hop decision query (`ROUTE_SELECTED`, which fires far more
often — see decisions.md).

**Known, documented gaps (not fixed this phase, not silently worked
around):** `ROUTE_UPDATE.hops` can only ever report 2 elements
(`[thisNode, nextHop]`) since distance-vector routing never learns a
destination's full multi-hop path — this has a real, demonstrated
consequence for the GUI's topology-diagram animation (verified by running
real firmware output through the GUI's own unmodified parser — see
[testing.md](testing.md) and [decisions.md](decisions.md)); `ROUTE_UPDATE.score`
reuses the next hop's own `link_score` (no multi-hop composite score exists
anywhere in this codebase); `ROUTE_UPDATE.reason` is `UNKNOWN` outside the
priority/expiry cases `routing_core` can actually distinguish; `STATISTICS.
endToEndLatencyMs` actually reports per-hop latency; `HELLO.mac` is omitted
at boot until `transport::begin()` succeeds. See
[gui-compatibility-matrix.md](gui-compatibility-matrix.md) for the complete
field-by-field audit and [decisions.md](decisions.md) for the reasoning
behind each.

## Practical theory notes

- **ESP-NOW peer registration is required for sending, not receiving.** Any
  node on the correct WiFi channel receives ESP-NOW frames from anyone,
  peer list or not. `esp_now_send()`, however, refuses to send to a MAC
  that isn't a registered peer (with the broadcast address as a documented
  exception). This is why Phase 0 can log real RSSI on receipt before any
  peer MACs are known, but can't yet demonstrate a real two-way exchange
  requiring per-node send.
- **All five boards run identical hardware (ESP32-WROOM-32, same
  architecture, same compiler).** That means `MeshPacket` never needs
  network byte-order conversion (`htons`/`ntohs`) — every node reads the
  same in-memory layout the same way. This would NOT hold if the mesh ever
  mixed architectures (e.g. adding an ESP32-S3 node with different
  endianness/alignment rules), so don't copy this assumption if the BOM
  ever changes.
- **Struct layout and alignment matter on real hardware, not just style.**
  See the comment block at the top of `core/packet.h` for why `MeshPacket`
  is explicitly packed with deliberate padding, and why received bytes get
  `memcpy()`'d into a local struct rather than pointer-cast in place.
