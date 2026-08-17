# Architecture

Status: **Phase 3 — transport + routing + predictor + anomaly real,
reliability/telemetry still stubs.** This document describes what exists
now and the shape it's built to grow into. It does not describe algorithms
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
| Reliability (ACK, retransmit, dup-filter) | Stub interface only | `src/reliability/` |
| Routing (distance-vector + priority override + link-health-aware selection) | **Implemented** (Phase 1, extended Phase 2) | `src/routing/` |
| Predictor (RSSI EWMA/slope + PDR + staleness fusion) | **Implemented** (Phase 2) | `src/predictor/` |
| Anomaly (MAD Z-score + flatline + sensor state machine) | **Implemented** (Phase 3) | `src/anomaly/` |
| Reporting (OLED + Serial/WebSerial) | Stub interface only | `src/telemetry/` |

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
2)" below). Reliability/anomaly/telemetry remain clean call-through stubs
for later phases.

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
    ├── reliability/   <- stub
    └── telemetry/     <- stub

firmware/PredictiveMesh/test/
├── test_routing_core.cpp     <- host-compiled (g++) unit tests for routing_core
├── test_predictor_core.cpp   <- host-compiled (g++) unit tests for predictor_core
└── test_anomaly_core.cpp     <- host-compiled (g++) unit tests for anomaly_core
                                  see docs/testing.md for all three
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

## Packet flow (as of Phase 3)

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
   **both** `routing::onPacketReceived()` **and** `predictor::onPacketReceived()`
   — the same parsed `(pkt, rssi)`, two independent consumers.
4. `routing::onPacketReceived()` always refreshes neighbor liveness for
   `pkt.prev_hop`; if the packet is `MSG_HEARTBEAT`, it also parses the
   payload as a distance-vector advertisement and folds it into the
   routing table (`routing_core::applyRouteAdvertisement()`).
5. `predictor::onPacketReceived()` feeds `pkt.prev_hop`'s RSSI into that
   neighbor's EWMA/slope pipeline (`predictor_core::onRssiSample()`),
   recomputes `link_score`, and logs `[PREDICTOR] neighbor=... rssi_ewma=...
   slope=... pdr=... score=... health=...` — see "Routing + predictor
   integration (Phase 2)" below for what evidence feeds the score.
6. `app::loop()` calls `routing::tick()` then `predictor::tick()` every
   iteration. `routing::tick()` rate-limits itself: it sends this node's
   own HELLO/route-advertisement beacon once per `ROUTING_HELLO_INTERVAL_MS`,
   and sweeps for stale neighbor/route entries older than
   `ROUTING_ENTRY_TIMEOUT_MS`. `predictor::tick()` independently sweeps
   every direct neighbor for the staleness fast-path
   (`PREDICTOR_STALENESS_TIMEOUT_MS`, deliberately faster than routing's
   own timeout — see [parameters.md](parameters.md)).
7. Whenever something needs a next hop for a destination —
   `routing::getNextHop(destination, priority)` or the packet-shaped
   wrapper `routing::selectNextHop(pkt)` — routing builds a per-neighbor
   health mask from `predictor::isUnhealthy()`, then picks the best
   surviving candidate from its table (excluding priority-only edges and,
   for NORMAL traffic only, preferring healthy candidates — see below),
   logs `[ROUTE] dst=... next=... hops=... priority=...`, and fires a
   `ROUTE_SELECTED` event to whatever's registered via
   `routing::setEventCallback()`.
8. Independently of any received packet, `app::loop()` samples both
   sensors every `SENSOR_SAMPLE_INTERVAL_MS` — `anomaly::sample(POT)` then
   `anomaly::sample(LDR)`, each a real `analogRead()` fed through
   `anomaly_core::evaluate()`. `anomaly::tick()` also runs every iteration,
   independently sweeping both sensors for the staleness state (Part 4 —
   see "Anomaly / sensor-health layer (Phase 3)" below). This path is
   completely disjoint from the packet-receive path above — sensor
   readings never touch `MeshPacket`, and mesh traffic never touches
   sensor state.

See [protocol.md](protocol.md) for why no wire format changed in Phase 2
or Phase 3, and [testing.md](testing.md) for how all three algorithms are
validated without
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

**PDR is real but not yet live-fed**: the math and API
(`predictor::onSendResult()`) are complete and independently tested, but
nothing in the current firmware calls it — every send this firmware
performs today is a broadcast `MSG_HEARTBEAT` beacon, and ESP-NOW broadcast
has no MAC-layer delivery ACK to measure, so there is no real per-neighbor
unicast send outcome to observe yet. See
[decisions.md](decisions.md#pdr-measurement-boundary-not-wired-to-live-send-outcomes-in-phase-2)
for the full reasoning. Until then, PDR evidence sits at its documented
neutral default (1.0) and `link_score` is driven by RSSI evidence + the
staleness fast-path.

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
