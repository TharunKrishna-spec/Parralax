# Architecture

Status: **Phase 1 — transport + routing real, predictor/anomaly/reliability/telemetry
still stubs.** This document describes what exists now and the shape it's
built to grow into. It does not describe algorithms that aren't
implemented yet — see [known-issues.md](known-issues.md) for those.

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
| Routing (distance-vector + priority override) | **Implemented** (Phase 1) | `src/routing/` |
| Predictor (RSSI slope + PDR fusion) | Stub interface only | `src/predictor/` |
| Anomaly (MAD Z-score + flatline) | Stub interface only | `src/anomaly/` |
| Reporting (OLED + Serial/WebSerial) | Stub interface only | `src/telemetry/` |

Data is meant to flow bottom-up: raw radio -> statistics -> routing
decisions -> reliability -> reporting. Phase 0 wired the bottom layer for
real; Phase 1 wired routing on top of it, consuming
`transport::RxEvent`/`TxEvent` instead of touching `esp_now_*` APIs
directly — the receive path is now: ESP-NOW callback -> `transport::RxEvent`
-> `main.cpp` parses a `MeshPacket` -> `routing::onPacketReceived()`.
Reliability/predictor/anomaly/telemetry remain clean call-through stubs
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
    │   ├── routing_core.h/.cpp   <- pure distance-vector algorithm, no Arduino dependency
    │   └── routing.h/.cpp        <- Arduino-facing adapter (millis/logger/transport::send)
    ├── predictor/     <- stub
    ├── anomaly/       <- stub
    ├── reliability/   <- stub
    └── telemetry/     <- stub

firmware/PredictiveMesh/test/
└── test_routing_core.cpp   <- host-compiled (g++) unit tests for routing_core;
                                see docs/testing.md
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

## Packet flow (as of Phase 1)

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
   `routing::onPacketReceived()`.
4. `routing::onPacketReceived()` always refreshes neighbor liveness for
   `pkt.prev_hop`; if the packet is `MSG_HEARTBEAT`, it also parses the
   payload as a distance-vector advertisement and folds it into the
   routing table (`routing_core::applyRouteAdvertisement()`).
5. `app::loop()` calls `routing::tick()` every iteration. `tick()`
   rate-limits itself: it sends this node's own HELLO/route-advertisement
   beacon once per `ROUTING_HELLO_INTERVAL_MS`, and sweeps for stale
   neighbor/route entries older than `ROUTING_ENTRY_TIMEOUT_MS`.
6. Whenever something needs a next hop for a destination —
   `routing::getNextHop(destination, priority)` or the packet-shaped
   wrapper `routing::selectNextHop(pkt)` — routing picks the best surviving
   candidate from its table (excluding priority-only edges unless
   `priority == true`), logs `[ROUTE] dst=... next=... hops=... priority=...`,
   and fires a `ROUTE_SELECTED` event to whatever's registered via
   `routing::setEventCallback()`.

See [protocol.md](protocol.md) for the route-advertisement payload layout
and [testing.md](testing.md) for how the algorithm is validated without
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
