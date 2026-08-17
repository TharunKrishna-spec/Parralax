# Phase Log

## Phase 0 — Firmware foundation / hardware-contract first
**Date:** 2026-08-17
**Status:** Complete, awaiting explicit go-ahead for Phase 1.

### Objective
Build the real ESP32/Arduino firmware foundation — not a simulator, not a
fake ESP-NOW layer — structured so that once physical hardware arrives, the
same code compiles/flashes and hardware-specific integration/calibration
can begin. Explicitly excluded from this phase: routing algorithms,
predictor math, anomaly detection, reliability (ACK/retransmit/dedup), and
any UI/dashboard beyond Serial logging.

### What was built
- Arduino sketch structured as `firmware/PredictiveMesh/` with a thin
  `.ino` entry point and all real logic under `src/` (using Arduino's
  documented `src` subfolder compilation rules — see
  [architecture.md](architecture.md)).
- Centralized node identity: `NodeId`/`NodeRole`/`NodeInfo` and a fixed
  5-node topology/adjacency table in `core/node_id.h`; role selection via a
  single `THIS_NODE_ID` define in `config.h`.
- `MeshPacket` wire struct (`core/packet.h`) with the exact field set
  specified: source, destination, prev_hop, next_hop, sequence, type,
  priority, payload, timestamp — plus alignment padding, documented.
- `MessageType` enum (`core/message_types.h`): HEARTBEAT, DATA, ACK.
- ESP-NOW transport module (`transport/espnow_transport.h/.cpp`):
  WiFi station mode, fixed-channel configuration, `esp_now_init()`, core
  3.x receive callback (`esp_now_recv_info_t*`, real RSSI from
  `info->rx_ctrl->rssi`), send callback, peer add/remove, broadcast-peer
  bootstrap.
- Structured Serial logger (`core/logger.h/.cpp`): DEBUG/INFO/WARN/ERROR
  levels plus `[RX]`/`[TX]` structured lines matching the required format.
- Clean stub interfaces for `routing/`, `predictor/`, `anomaly/`,
  `reliability/`, `telemetry/` — declarations and safe-default
  implementations only, no algorithms.
- `docs/` established with all seven required files.

### What was explicitly NOT built (by design)
Distance-vector routing, priority routing logic, EWMA, RSSI slope, PDR
calculation, link_score, staleness detection, hysteresis, MAD-Z, flatline
detection, ACK, retransmission, duplicate filtering, UCB1, dashboard,
WebSerial, final OLED UI, any sensor-reading code (`analogRead()` calls),
any OLED library dependency.

### Validation performed
See [testing.md](testing.md) for the full breakdown. Summary: static
inspection and an actual `arduino-cli compile` against the ESP32 Arduino
core (core 3.x) run from this environment — not just "should compile."
All hardware-dependent tests are `NOT RUN — HARDWARE NOT AVAILABLE`, per
[known-issues.md](known-issues.md).

### Git
No commits were made this phase (per instruction — no automatic git
operations). Working tree left as untracked/new files for the user to
review and commit.

### Next phase (not started, awaiting explicit go-ahead)
Per the roadmap in implementation-guide.html §06 ("Hours 2-6: Multi-hop
discovery, static routing, priority override"), Phase 1 would implement
neighbor discovery/peer registration for real hardware, the distance-vector
routing table, and the priority-flag routing branch (§5.3) — but only once
explicitly requested.

---

## Phase 1 — Multi-hop discovery + distance-vector routing + priority override
**Date:** 2026-08-17
**Status:** Complete, awaiting explicit go-ahead for Phase 2.

### Objective
Implement real neighbor discovery (HELLO/beacon), a distance-vector
routing table, next-hop selection for normal traffic, and the
priority-flag override forcing shortest-hop — per
implementation-guide.html §06 (Hours 2-6) and §5.3. Explicitly excluded:
the predictor (EWMA/RSSI-slope/PDR/link_score), anomaly detection, the
reliability layer (ACK/retransmit/dup-filter/actual packet relaying),
UCB1, and the dashboard.

### What was built
- `src/routing/routing_core.h/.cpp` — the real distance-vector algorithm,
  deliberately free of any Arduino/ESP-NOW dependency: neighbor table,
  a `(destination, via-neighbor)` route-candidate matrix, Bellman-Ford-style
  relaxation with validity guards, staleness expiry, and next-hop
  selection with the normal/priority split.
- `src/routing/routing.h/.cpp` — the Arduino-facing adapter: builds/sends
  the HELLO+route-advertisement beacon (riding inside `MSG_HEARTBEAT`,
  no new packet field or message type), parses received beacons, drives
  `routing_core` from `millis()`, logs `[ROUTE] dst=... next=... hops=...
  priority=...`, and exposes a `ROUTE_SELECTED`/`ROUTE_CHANGED`/
  `ROUTE_INVALIDATED` event callback.
- `src/main.cpp` updated: `onTransportRx()` now actually parses a
  `MeshPacket` (previously a no-op) and feeds `routing::onPacketReceived()`;
  `loop()` calls `routing::tick()`; a route-event logger is registered.
- `src/config.h`: `ROUTING_HELLO_INTERVAL_MS` (1000 ms),
  `ROUTING_ENTRY_TIMEOUT_MS` (3000 ms) — documented in
  [parameters.md](parameters.md) and [decisions.md](decisions.md).
- `firmware/PredictiveMesh/test/test_routing_core.cpp` — a host-compiled
  (g++, not arduino-cli) unit test harness, actually compiled and run this
  phase: **18/18 checks passed**, covering all 10 required scenarios. See
  [testing.md](testing.md) for the real output.
- One real design call, documented rather than silently made: the direct
  A↔S edge is modeled as priority-only (excluded from NORMAL selection),
  sourced from implementation-guide.html §01's own diagram label
  `"(priority path only)"` — see
  [decisions.md](decisions.md#a↔s-edge-modeled-as-priority-only-excluded-from-normal-selection).
- Resolved the Phase 0 known-issue about TTL/hop-count: not needed yet,
  because route advertisements are single-hop by construction and Phase 1
  doesn't implement actual packet relaying — see
  [known-issues.md](known-issues.md).
- `MeshPacket` (`core/packet.h`) is unchanged — no new fields, no new
  `MessageType`. `core/node_id.h`'s topology/adjacency is also unchanged.

### What was explicitly NOT built (by design)
Link-quality scoring (RSSI EWMA, slope, PDR, `link_score`), anomaly
detection (MAD Z-score, flatline), the reliability layer (hop-by-hop ACK,
retransmit, duplicate filtering) and, as a consequence, actual multi-hop
relaying of a received non-self-destined `MSG_DATA` packet (routing only
*decides* a next hop in this phase — nothing acts on that decision for
someone else's packet yet), UCB1, dashboard/WebSerial.

### Validation performed
- `firmware/PredictiveMesh/test/test_routing_core.cpp`: real, host-compiled
  (g++ 15.2.0 / MinGW-W64), actually executed — 18/18 checks passed,
  covering all 10 required test scenarios.
- Static review of `routing.cpp`/`main.cpp` against implementation-guide.html
  and Phase 1's "DO NOT" list.
- **Real `arduino-cli compile` still not performed** — no `esp32:esp32`
  core installed anywhere accessible in this environment as of this phase.
  See [testing.md](testing.md) and [known-issues.md](known-issues.md).
- No hardware-dependent validation — see
  [known-issues.md](known-issues.md#phase-1-routing--not-yet-run-on-hardware).

### Git
No commits were made this phase. Working tree left uncommitted for the
user to review.

### Next phase (not started, awaiting explicit go-ahead)
Per implementation-guide.html §06 (Hours 6-12), Phase 2 would implement
the fused link predictor: RSSI EWMA smoothing, least-squares slope over
window W, PDR sliding window, and the fused `link_score` — which is also
when the priority-only-edge special case in `routing_core` becomes
eligible to be replaced by real link-quality-aware selection.
