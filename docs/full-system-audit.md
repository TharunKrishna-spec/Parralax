# Full System Architecture Audit

**Date:** 2026-08-18
**Method:** Every claim below is grounded in a direct read of the file cited next to it, this pass, in this session — not inherited from a prior audit, a comment, or GUI simulation behavior. Where a finding matches something already in `docs/decisions.md`/`docs/gui-compatibility-matrix.md`/`docs/hardware-readiness.md`, that's noted as "carried forward, re-confirmed" rather than re-derived from nothing. Where this pass found something those documents don't mention, it's marked **NEW THIS PASS**. Nothing here was fabricated to fill a gap — anywhere the honest answer is "unverified" or "unclear," it says so.
**No code was modified to produce this document.**

> **✅ RESOLVED, 2026-08-18 — Phase 12 below has been rebuilt against the
> real replacement GUI.** `gui-main/gui-main/mesh-command-console.html`
> (the file Phase 12 originally audited) was deleted from the working
> tree and replaced with `gui-main/gui-main/mesh-command-console (1).html`
> (116KB vs. the original's 36KB — DEMO/EXPLAIN/EXPERT modes, a
> conceptual Data Center view, a Bridge view, replay, an event timeline,
> a tech inspector, and a "learn" walkthrough, none of which existed
> before), alongside a new `gui-main/gui-main/docs/manual.md` (604 lines).
> Every claim in this rebuilt Phase 12 is grounded in a fresh, direct read
> of the new HTML's actual `<script>` content this pass — not the manual,
> and not inherited from the original Phase 12. Where the manual's own
> documented examples/vocabulary diverge from what the GUI's real code
> checks for, or from what firmware actually emits, that's called out
> explicitly rather than assumed consistent. The file is still named with
> a stray `(1)` suffix in the working tree — rename it to
> `mesh-command-console.html` before treating it as final.

Verification-status vocabulary used throughout:
- **CODE-IMPLEMENTED** — the logic exists and was read.
- **HOST-TESTED** — a host g++ suite exercises it (see `docs/testing.md` for exact counts).
- **ESP32-COMPILE-VERIFIED** — the whole sketch compiles clean on real `arduino-cli`/core 3.3.11.
- **SIMULATION-VERIFIED** — only the GUI's in-browser JS or the Python mock produces this behavior; no firmware code path does.
- **GUI-VERIFIED** — a real GUI parser run (this project's own harness, see `docs/testing.md`) proved firmware JSON parses correctly.
- **PHYSICAL-HARDWARE-VERIFIED** — actually run on a flashed board. **As of this document, nothing in this project holds this status** — see `docs/hardware-readiness.md`.
- **END-TO-END-VERIFIED** — the full physical input → firmware → mesh → sink → telemetry → serial → GUI parser → GUI state → display chain, all confirmed. **Nothing in this project holds this status either** — the physical-input and physical-mesh legs have never run.

---

## Phase 0 — Repository Map

| Subsystem | File(s) | Responsibility | Callers | Callees | Produces | Consumes |
|---|---|---|---|---|---|---|
| Entry point | `firmware/PredictiveMesh/PredictiveMesh.ino` | thin `.ino` shell | Arduino runtime | `app::setup()`/`app::loop()` | — | — |
| Boot/loop orchestration | `src/main.cpp`, `src/main.h` | wires every module's init/tick, owns the 5 event-fan-out functions | `.ino` | every module's `init()`/`tick()` | — | — |
| Config | `src/config.h` | every tunable constant, `THIS_NODE_ID` | everything | — | — | — |
| Node identity | `src/core/node_id.h` | `NodeId`/`NodeRole`/`NodeInfo`, MAC table, static adjacency (`neighborsOf()`) | everything | — | node/MAC/adjacency facts | — |
| Packet format | `src/core/packet.h` | `MeshPacket` wire struct, `packetInit()`/`packetWireSize()` | transport, routing, reliability, apptraffic | — | — | — |
| Message types | `src/core/message_types.h` | `MessageType` enum (HEARTBEAT/DATA/ACK) | routing, reliability | — | — | — |
| Logger | `src/core/logger.h/.cpp` | Serial log lines, MAC formatting | everything | `Serial.print*` | `[INFO]`/`[RX]`/`[TX]` lines | — |
| Transport | `src/transport/espnow_transport.h/.cpp` | WiFi STA + ESP-NOW init, peer table, real `esp_now_send`/recv callbacks | `main.cpp` | `esp_now_*`, `esp_wifi_*` | `RxEvent`/`TxEvent` | `MeshPacket` bytes |
| Routing (core) | `src/routing/routing_core.h/.cpp` | distance-vector table, `selectNextHop()`, `reconstructPath()` | `routing.cpp` | — | route decisions, candidates | neighbor/advertisement facts |
| Routing (adapter) | `src/routing/routing.h/.cpp` | beacons, wire (de)serialization, event fan-out | `main.cpp`, `reliability.cpp`, `apptraffic` (via `reliability::send`) | `routing_core`, `transport::send`, `predictor::isUnhealthy` | `MSG_HEARTBEAT`, `RouteEvent` | received `MeshPacket` |
| Predictor (core) | `src/predictor/predictor_core.h/.cpp` | RSSI EWMA/slope, PDR EWMA, fused `link_score`, hysteresis | `predictor.cpp` | — | `NeighborLinkState` | RSSI samples, send outcomes |
| Predictor (adapter) | `src/predictor/predictor.h/.cpp` | wraps core with `millis()`, event fan-out | `main.cpp`, `routing.cpp`, `reliability.cpp`, `oled.cpp` | `predictor_core` | `LinkEvent` | `MeshPacket`+RSSI, ACK outcomes |
| Anomaly (core) | `src/anomaly/anomaly_core.h/.cpp` | calibration, MAD/modified-Z, flatline, state machine | `anomaly.cpp` | — | `SensorTelemetry` | `SensorObservation` |
| Anomaly (adapter) | `src/anomaly/anomaly.h/.cpp` | real `analogRead()`, blocking boot calibration, event fan-out | `main.cpp`, `apptraffic.cpp`, `oled.cpp` | `anomaly_core` | `AnomalyEvent` | ADC reads |
| Reliability (core) | `src/reliability/reliability_core.h/.cpp` | pending-slot bookkeeping, dup filter, timeouts, statistics | `reliability.cpp` | — | `AckResult`/`TimeoutEvent` | send/ACK/timeout facts |
| Reliability (adapter) | `src/reliability/reliability.h/.cpp` | real sends, ACK construction, forwarding | `main.cpp`, `apptraffic.cpp` | `reliability_core`, `routing::selectNextHop`, `transport::send`, `predictor::onSendResult`, `ucb1::onRouteOutcome` | `MSG_ACK`, forwarded `MSG_DATA`, `ReliabilityEvent` | received `MeshPacket` |
| UCB1 (core) | `src/ucb1/ucb1_core.h/.cpp` | UCB1 arm stats, ranking | `ucb1.cpp` | — | ranked next-hop | candidate list |
| UCB1 (adapter) | `src/ucb1/ucb1.h/.cpp` | thin wrapper, **entirely `#if ENABLE_UCB1`** | `routing.cpp`, `reliability.cpp` | `ucb1_core` | — | routing outcomes |
| Application traffic | `src/apptraffic/apptraffic_core.h/.cpp`, `apptraffic.h/.cpp` | `NODE_A`-only DATA generation, priority trigger, wire encode/decode | `main.cpp` | `reliability::send`, `anomaly::getTelemetry` | `MSG_DATA` payload (encode) | Serial `'p'` byte |
| Telemetry (core) | `src/telemetry/telemetry_core.h/.cpp` | JSON line builders for all 10 message types, enum classification | `telemetry.cpp` | — | JSON strings | struct payloads |
| Telemetry (adapter) | `src/telemetry/telemetry.h/.cpp` | envelope/seq/bootId, route-change caching+reason derivation, rate limiting | `main.cpp` (5 event hooks + `tick()`) | `telemetry_core`, `Serial.println` | Serial JSON lines | every other module's state |
| OLED (core) | `src/oled/oled_core.h/.cpp` | screen-cycle/override/rate-limit state machine | `oled.cpp` | — | which screen, redraw y/n | `now` |
| OLED (adapter) | `src/oled/oled.h/.cpp` | per-node Adafruit driver selection, drawing | `main.cpp` | `oled_core`, `predictor::linkScore/isUnhealthy`, `anomaly::getTelemetry`, Adafruit_GFX | pixels | polled state |
| GUI | `gui-main/gui-main/mesh-command-console.html` | single-file HTML/CSS/JS dashboard | browser | WebSerial API, WebSocket | DOM updates | Serial/WS JSON lines |
| GUI bridge | `gui-main/gui-main/serial-bridge.py` | relays one real serial port OR the mock generator to WebSocket clients | operator | `pyserial` (optional), `websockets` | JSON lines over WS | one serial port |
| GUI mock | `gui-main/gui-main/serial-mock.py` | scripted flat-schema demo cycle over a PTY | operator, `serial-bridge.py` | `pty`, `os.write` | flat-schema JSON | — |
| GUI contract | `gui-main/gui-main/docs/gui-telemetry-contract.md` | frozen wire contract, v1 | — | — | — | — |
| Host tests | `firmware/PredictiveMesh/test/test_*_core.cpp` | 8 suites, g++, 382 checks total | operator (`g++`) | each `*_core.cpp` | pass/fail | hand-constructed inputs |
| Hardware bench sketches | `hardware code/0.96esp32node/`, `hardware code/1.3esp32node/` | hardware team's own OLED/sensor bring-up sketches | — | Adafruit libs | — | — |
| Docs | `docs/*.md`, `CLAUDE.md` | architecture, decisions, parameters, testing, phase log, known issues, hardware readiness, GUI compat matrix, system map, hardware bring-up | — | — | — | — |

No simulator exists inside `firmware/PredictiveMesh/src/` — confirmed by this pass's own reading, not just a repeated grep claim.

---

## Phase 1 — Actual Runtime Flow

### Boot (`src/main.cpp::app::setup()`, read in full this pass)

```
logger::begin()
  -> WiFi.mode(WIFI_STA); WiFi.macAddress(mac); telemetry::init(mac)   [emits HELLO — see known caveat below]
  -> transport::begin(onTransportRx, onTransportTx)                   [ESP-NOW recv callback becomes LIVE here]
  -> transport::addBroadcastPeer(); registerConfiguredPeers()
  -> routing::init(); routing::setEventCallback(onRouteEvent)
  -> predictor::init(); predictor::setEventCallback(onLinkEvent)
  -> anomaly::init()  [BLOCKING: ~1s/sensor boot calibration]; anomaly::setEventCallback(onAnomalyEvent)
  -> reliability::init(); reliability::setEventCallback(onReliabilityEvent)
  -> #if ENABLE_UCB1: ucb1::init()
  -> apptraffic::init()
  -> oled::init()
```

**Confirmed exactly as coded** — this is not the generic order the task prompt suggested; it's what `main.cpp` actually does. One confirmed gap, carried forward from the prior forensic-audit pass in this same session and re-verified here: `transport::begin()` (which arms the real receive callback) runs **before** `routing::init()`/`predictor::init()`/`reliability::init()`. C++ static zero-init makes most of that window harmless by coincidence, but `routing_core::RoutingState.self` would read `0` (=`NODE_A`) until `routing::init()` actually executes. Real but narrow (microseconds).

### Loop (`app::loop()`, read in full)

```
alive log (every 5s, DEBUG)
routing::tick()
predictor::tick()
anomaly::tick()
reliability::tick()
apptraffic::tick()
telemetry::tick()
oled::tick()
sensor sampling (every SENSOR_SAMPLE_INTERVAL_MS = 150ms)
delay(10)
```

Confirmed exact order, no assumptions. `oled::tick()` deliberately runs last (reads already-updated state; never mutates anything upstream).

### The one thing NOT in the diagram above

`onTransportRx()` (the real ESP-NOW receive callback, registered via `esp_now_register_recv_cb`) synchronously calls `routing::onPacketReceived()` / `predictor::onPacketReceived()` / `reliability::onPacketReceived()` **outside the loop() cadence entirely** — this is real, radio-driven, interrupt-adjacent execution, not just "the loop() list above." `reliability::onPacketReceived()`'s `MSG_DATA` path issues a real `transport::send()` (the ACK, and potentially a forward) from inside this same callback. Carried forward from this session's prior bug-audit pass, re-confirmed by re-reading `espnow_transport.cpp`/`main.cpp`/`reliability.cpp` this pass.

---

## Phase 2 — Hardware Layer

| Interface | Pin | Device | Direction | Init owner | Verification status |
|---|---|---|---|---|---|
| ADC1_CH6 | GPIO34 | Potentiometer | in | `anomaly::init()` (`pinMode`+`analogReadResolution(12)`) | ESP32-compile-verified; NOT hardware-verified |
| ADC1_CH7 | GPIO35 | LDR divider | in | same | same |
| digital out | GPIO25 | Piezo buzzer | out | **nothing** — `PIN_BUZZER` is `#define`d in `config.h` but grepped this pass: **zero references anywhere in `src/`** other than the constant itself. No code drives it. | CODE-IMPLEMENTED: NO. Constant only. |
| I2C SDA | GPIO21 | OLED (S: SSD1306, C: SH1106) | bidir | `oled::init()` (`Wire.begin()`) | ESP32-compile-verified; NOT hardware-verified |
| I2C SCL | GPIO22 | OLED | bidir | same | same |
| I2C addr | `0x3C` (S) / `0x78` (C) — **corrected 2026-08-18, was wrongly assumed `0x3C` for both** | per-driver | — | `config.h`: `OLED_I2C_ADDRESS_S`/`OLED_I2C_ADDRESS_C` | differ by node, team-confirmed |
| WiFi/ESP-NOW | — | radio | bidir | `transport::begin()` | ESP32-compile-verified; NOT hardware-verified |
| Channel | 6 (`MESH_WIFI_CHANNEL`) | — | — | `transport::begin()`, `addPeer()` | placeholder value, no RF survey done — carried forward from `hardware-readiness.md` |
| MAC/node mapping | — | — | — | `core/node_id.h::nodeTable()` | real MACs populated (Phase 7); **firmware never programmatically verifies `WiFi.macAddress()` against this table** — confirmed gap, carried forward |
| UART/USB | fixed by board | — | — | N/A | **UNKNOWN — TEAM INPUT REQUIRED**, carried forward from `hardware-readiness.md`, unchanged |
| RESET/BOOT | fixed by board | — | — | N/A | same, unchanged |

**NEW THIS PASS:** the buzzer pin is defined but has no driver anywhere. Not a bug (implementation-guide.html marks it "Optional" for every node), but the feature matrix should not claim buzzer feedback exists in any form — it doesn't, at all, in code.

---

## Phase 3 — Packet Architecture

`MeshPacket` (`src/core/packet.h`), `#pragma pack(push,1)`:

| Field | Type | Bytes |
|---|---|---|
| `version` | uint8 | 1 |
| `type` | uint8 | 1 |
| `source` | uint8 | 1 |
| `destination` | uint8 | 1 |
| `prev_hop` | uint8 | 1 |
| `next_hop` | uint8 | 1 |
| `priority` | uint8 | 1 |
| `_reserved0` | uint8 | 1 |
| `sequence` | uint16 | 2 |
| `_reserved1` | uint16 | 2 |
| `timestamp_ms` | uint32 | 4 |
| `payload_len` | uint8 | 1 |
| `payload[64]` | uint8[] | 64 |

`PACKET_HEADER_SIZE` = `offsetof(payload)` = **16 bytes**. Max wire size = 16 + 64 = **80 bytes** — comfortably under ESP-NOW's 250-byte frame ceiling (confirmed against the header's own comment, not re-measured against real ESP-IDF constants this pass, since that figure hasn't changed).

Actual wire sizes by message, computed from real code (not assumed):
- **HEARTBEAT**: 16 + `1 + count*2` (count ≤ 5) = 16 + up to 11 = **≤27 bytes**
- **ACK**: 16 + `sizeof(AckWire)` (1+2, packed) = **19 bytes**
- **DATA (apptraffic)**: 16 + `DATA_WIRE_SIZE`(10) = **26 bytes**
- **DATA (max, protocol ceiling)**: 16 + 64 = **80 bytes**

All well within limits — no wire-size risk anywhere in this project.

**Validation gap, carried forward and re-confirmed this pass:** `onTransportRx()` checks `evt.len >= PACKET_HEADER_SIZE` but nothing anywhere clamps the wire-received `pkt.payload_len` to `PACKET_MAX_PAYLOAD` (64) before `routing.cpp::processRouteUpdate()`'s `maxFit` arithmetic reads `pkt.payload+1` — a corrupt `payload_len` can walk past the 64-byte array within the packet's own stack frame. Not triggerable by this project's own trusted senders in normal operation (every real sender here always writes a valid `payload_len`); a real robustness gap, not an active bug given only this project's own 5 firmware images ever transmit on this channel today.

Packet meaning changes at exactly one point worth naming: **forwarding preserves `source`/`sequence`/`type`/`priority`/`payload` unchanged** (`reliability.cpp::handleData()`: `MeshPacket forwarded = pkt;`) — only `prev_hop`/`next_hop`/`timestamp_ms` are rewritten per hop (inside `transmitHop()`). Confirmed, matches the documented design.

---

## Phase 4 — Basic Network Features

| Feature | Where | Trigger | Produces | Consumer | Tested | Physical |
|---|---|---|---|---|---|---|
| Node identity | `config.h`+`node_id.h` | compile-time | `THIS_NODE_ID` | everything | N/A (constant) | not verified |
| Peer registration | `main.cpp::registerConfiguredPeers()` | boot | ESP-NOW peer table | `transport::send` | ESP32-compile | not verified |
| Broadcast | `routing.cpp::sendBeacon()` | every 1000ms | `MSG_HEARTBEAT` | all neighbors | host (routing_core) | not verified |
| Unicast | `reliability.cpp::transmitHop()`/`sendAck()` | send/ACK/forward | `MSG_DATA`/`MSG_ACK` | one neighbor | host | not verified |
| Neighbor discovery | `routing_core::noteNeighborSeen()` | any received packet | neighbor liveness | `expireStale()` | host, 37/37 | not verified |
| RSSI | `espnow_transport.cpp` (`info->rx_ctrl->rssi`) | every recv | int8 dBm | predictor | ESP32-compile (real API) | not verified |
| Route advertisements | `routing_core::applyRouteAdvertisement()` | HEARTBEAT recv | candidate table update | `selectNextHop` | host | not verified |
| Route tables/candidates | `routing_core::RoutingState.candidates[dest][via]` | — | — | selection, telemetry | host | not verified |
| Next-hop selection | `routing_core::selectNextHop()` | every send/forward | `NodeId` | reliability | host | not verified |
| Multi-hop forwarding | `reliability.cpp::handleData()` | non-self-destined DATA | forwarded packet | next hop | host (reliability_core bookkeeping) | not verified |
| Route expiry | `routing_core::expireStale()` | every `tick()` | invalidated entries | `ROUTE_INVALIDATED` event | host | not verified |
| Route recovery | same table, fresh advertisement | new HEARTBEAT | restored candidate | selection | host | not verified |
| Route-change events | `routing.cpp` fan-out, `telemetry.cpp::onRouteEvent` | selection/table change | `ROUTE_UPDATE`/`EVENT` | GUI (if connected to the right node — see Phase 12) | host+real-JSON-parse (Phase 6/7.1) | not verified |
| Alternate path | `A-C-D-S` candidate | B degraded | second candidate row | selection | host | not verified |
| Node disappearance | staleness (routing 3000ms, predictor 2000ms) | silence | UNHEALTHY/invalidated | selection, telemetry | host | not verified |
| Node recovery | hysteresis `aboveCount>=3` | fresh good samples | HEALTHY again | selection | host | not verified |

Every row above is CODE-IMPLEMENTED + HOST-TESTED; **zero rows are PHYSICAL-HARDWARE-VERIFIED.**

---

## Phase 5 — Link Quality Pipeline (actual equations, from `predictor_core.cpp`, read this pass)

```
RSSI EWMA:      ewma[t] = α·rssi[t] + (1-α)·ewma[t-1]        α = PREDICTOR_RSSI_EWMA_ALPHA = 0.3
                bootstrap: ewma[0] = rssi[0] (never fabricated)

Slope:          ordinary least squares of ewma values vs SAMPLE INDEX (0..n-1),
                n ≤ PREDICTOR_SLOPE_WINDOW = 8
                slope = (n·ΣXY - ΣX·ΣY) / (n·ΣX² - (ΣX)²)
                units: dBm per SAMPLE STEP, not per second (samples arrive on the
                ~1000ms beacon cadence, not a guaranteed fixed wall-clock interval)

degrade_term:   clamp(-slope / SLOPE_REF, 0, 1)               SLOPE_REF = 1.5

link_score:     W1·(1-degrade_term) + W2·pdrEwma               W1 = W2 = 0.5

PDR EWMA:       pdr[t] = β·outcome[t] + (1-β)·pdr[t-1]        β = PREDICTOR_PDR_EWMA_ALPHA = 0.1
                outcome ∈ {0,1} per real ACK-match/timeout attempt
                bootstrap: pdr[0] = outcome[0]; before any attempt, pdr = 1.0 (documented
                neutral default, never a real reading)

Hysteresis:     HEALTHY -> UNHEALTHY: belowCount >= 3 consecutive evals with
                linkScore < T_LOW(0.5)
                UNHEALTHY -> HEALTHY: aboveCount >= 3 consecutive evals with
                linkScore > T_HIGH(0.7)

Staleness fast-path (INDEPENDENT of the above, bypasses debounce entirely):
                now - lastUpdateMs > PREDICTOR_STALENESS_TIMEOUT_MS(2000ms)
                  -> forced UNHEALTHY immediately
```

**This is exactly what the code does — not a textbook substitution.** Every constant is a real `config.h` value, cited above.

Evidence sources feeding this pipeline: `predictor::onPacketReceived()` is called for **every** received packet regardless of `MessageType` (HEARTBEAT, DATA, or ACK) — RSSI is a physical-layer property of the frame, not the message content, so this is correct, not a mixing bug (matches this session's prior bug-audit conclusion, re-confirmed). PDR evidence comes only from `reliability.cpp`'s real ACK-match (`predictor::onSendResult(neighbor, true)`) and real timeout events (`..., false`) — never fabricated, never from HEARTBEAT/RSSI.

**GUI cross-check:** the GUI's `LINK_UPDATE`/`PREDICTION` panels render exactly these fields (`rssiDbm`, `rssiEwmaDbm`, `rssiSlopeDbPerSec`, `pdr`, `pdrEwma`, `stalenessMs`, `linkScore`) — same meaning as firmware, **except** `rssiSlopeDbPerSec`'s name claims per-second units for a value that's actually per-sample-index — a real, now-documented (this pass) mismatch; see `docs/gui-compatibility-matrix.md`'s updated `LINK_UPDATE` row. The GUI never recomputes any of these — it only ever displays what firmware sent, matching the contract's "firmware is authoritative" rule.

---

## Phase 6 — Predictive Novelty (critical section, answered directly)

1. **What is predicted?** A binary health classification (HEALTHY/UNHEALTHY) for each direct neighbor link, derived from a leading indicator (RSSI trend) fused with a lagging one (PDR) — not a forecast of a future numeric value. "Predictive" here means "reacts to a *worsening trend*, not just an outright failure," not "forecasts time-to-failure."
2. **Variable:** `link_score` (see Phase 5 equation).
3. **Degradation:** `linkScore < T_HIGH` while still classified HEALTHY (the `degrading` soft-warning flag).
4. **Unhealthy:** 3 consecutive low evaluations, OR the independent staleness fast-path.
5. **Hysteresis:** Real — two distinct thresholds (0.5/0.7) plus a 3-evaluation debounce each direction, not a single flip-flop-prone cutoff.
6. **Ahead of failure?** By design, yes — the staleness fast-path (2000ms) is deliberately faster than routing's own hard timeout (3000ms) specifically so predictive degradation can act first. **Never measured on real RF** — this is a design property, not an empirical one yet.
7. **Is lead time measured?** **No. Confirmed by grep: no code path anywhere computes or emits a `leadTimeMs` value.** This is a genuine, deliberate, honestly-undocumented-as-fabricated gap — matches this project's own "don't invent leadTimeMs" rule. **Consequence (NEW THIS PASS):** the GUI's own "Latest reroute lead-time" headline metric can *only* ever be populated by the JS simulation's `Math.random()` or the Python mock — there is no firmware code path that could fill it with a real number, ever, as the code stands today.
8. **Is route switching caused by prediction?** Yes, genuinely — `predictor::isUnhealthy()` feeds `routing_core::selectNextHop()`'s health mask directly; host-tested, not just documented.
9. **Physically demonstrable?** Not yet — no hardware flashed.
10. **Does the GUI show the causal chain?** Partially, and *only if the physically-connected node is the one that actually makes the relevant routing decision* — see Phase 12's single-node-telemetry-source finding. Score/hysteresis/state are all real and shown; the lead-time number specifically is not.

---

## Phase 7 — Self-Healing / Rerouting

Traced `A→B→S` degrade → reroute → recover, purely from code (no hardware run):

```
B's link_score drops (real RSSI/PDR evidence at A)
  -> predictor::isUnhealthy(B) becomes true at A (after 3-eval debounce or staleness)
  -> routing_core::selectNextHop(dest=S, neighborUnhealthy[B]=true) prefers the
     healthy A-C-D-S candidate over the (now-deprioritized-but-not-invalidated) A-B-S one
  -> reliability::send()/forward calls this same selectNextHop() — real code path, not simulated
  -> apptraffic's next A->S packet takes the new route
  -> telemetry.cpp's onRouteEvent() detects the real hop-count change (Finding 6, Phase 7.1)
     and emits ROUTE_UPDATE + EVENT ROUTE_CHANGE with a derived (never guessed) reason
  -> S continues receiving DATA (packets, not yet decoded content — see Phase 10)
```

Recovery: B's link_score rises past `T_HIGH` for 3 consecutive evals → `aboveCount` triggers `becameHealthy` → `neighborUnhealthy[B]` flips false → `selectNextHop()` prefers the shorter A-B-S path again (lower hop count wins among healthy candidates) → same telemetry path fires again with `ROUTE_RECOVERY_R`.

**Every step above is CODE-IMPLEMENTED and, where it touches `*_core` logic, HOST-TESTED.** None of it is PHYSICAL-HARDWARE-VERIFIED. The scenario checklist (relay failure/recovery/poor RSSI/packet loss/ACK loss/route expiry/alternate route/simultaneous degradation/reboot) is each individually covered by the host suites listed in `docs/testing.md`, but **no test — host or otherwise — exercises this full chain as one scenario**; each `*_core` suite tests its own layer in isolation. There is no integration/simulation harness that feeds a synthetic "B degrades" signal through predictor→routing→reliability→telemetry as one connected run. That's a real gap in test *coverage of the chain*, distinct from each layer's own correctness.

---

## Phase 8 — Reliability

**ACK means:** "this specific hop-transmission reached the immediate next hop" — hop-level, never end-to-end. This is one of the most heavily and correctly documented facts in the whole project (4+ dedicated `decisions.md` entries, an explicit `gui-compatibility-matrix.md` caveat on `endToEndLatencyMs`). Telemetry's `STATISTICS.endToEndLatencyMs` reports this same per-hop `lastLatencyMs` under the contract's (misleadingly-named, but firmware-side-unfixable) field name — already documented as such.

| Check | Status | Evidence |
|---|---|---|
| ACK sender validation | **CONFIRMED GAP** | `onAckReceived()` matches purely on `(source,sequence)`, never checks the ACK's real `pkt.prev_hop` against the pending slot's own `nextHop`. Low practical impact today (one-pending-slot-per-identity invariant + unicast addressing) |
| Sequence uniqueness | real, per-source monotonic counter (`reliability_core::nextSequence`) | host-tested |
| Reboot identity | **CONFIRMED GAP** at the packet layer (no epoch/session field on `MeshPacket`); telemetry's own `bootId` is a separate, unrelated identity axis | mitigated by dup-cache TTL (2000ms) ≪ any real reboot time |
| Retry return values | **CONFIRMED GAP** — `tick()`'s RETRY branch ignores `transport::send()`'s return value, unlike the first-attempt path | asymmetric, self-heals via eventual timeout anyway |
| Duplicate behavior | real, TTL-based cache, host-tested | 88/88 |
| Forwarding failure | real — `PACKET_DROP` fired, logged, honest | host-tested |
| Pending pool exhaustion | real — `recordImmediateFailure()`, bounded (4 slots) | host-tested |
| Packet loss (bounded retry) | real — 3 retries, 200ms timeout, ~800ms worst case | host-tested |
| Retransmission | real | host-tested |

---

## Phase 9 — Sensor + Anomaly System

Full path confirmed identical for POT and LDR (`anomaly.cpp`/`anomaly_core.h`, both read in full):

```
analogRead(pin) [12-bit, explicit resolution]
  -> SensorObservation{sensor_id, millis(), raw, valid=true (always — ESP32 analogRead has
     no fault signal to report false with; not a hidden defect, a real API limitation)}
  -> anomaly_core::evaluate()
       WARMUP: buffer 100 samples, variance safety envelope, bounded-retry force-accept
               (loudly logged if forced)
       steady-state: median/MAD baseline -> modified Z-score (spike/jump) AND independent
               flatline/stuck counter -> debounced state machine
  -> SensorTelemetry snapshot (raw, median, mad, modified_z, flatline flags, state)
  -> telemetry::tick()'s SENSOR_STATUS (every 1000ms + on health change)
  -> oled.cpp (Node C only: SPIKE/JUMP + STUCK flags, read directly, no event dependency)
```

All 8 sub-states (normal/spike-anomaly/recovery/stuck-flatline/invalid/calibration/noisy-startup/extreme-values) are CODE-IMPLEMENTED and HOST-TESTED (50/50, `test_anomaly_core.cpp`). **Independence from routing, confirmed by code, not just by comment:** `anomaly.cpp`/`anomaly_core.cpp` import nothing from `routing`/`predictor`; the only place sensor and network state are ever combined is in the *payload* of application DATA packets (Phase 10) and side-by-side in telemetry/GUI/OLED — never in any decision logic. This matches the project's own stated design intent, verified by absence of any cross-import.

---

## Phase 10 — Application Data (the critical gap)

```
NODE_A: anomaly::sample() [already running every 150ms] -> apptraffic::sendOne() reads the
  LATEST cached SensorTelemetry (no duplicate ADC read) -> apptraffic_core::encodeData()
  [appSeq, potValue, ldrValue, timestampMs -> 10-byte packed DataWire] -> reliability::send()
  -> routing::getNextHop() -> transmitHop() -> real transport::send()
  -> [B or C/D relay, forwarding unchanged payload bytes] -> S: reliability.cpp::handleData(),
  destination==THIS_NODE_ID branch fires PACKET_RECEIVED with the RAW bytes
  -> ??? <- apptraffic_core::decodeData() is DEFINED, UNIT-TESTED (part of 29/29), but has
  ZERO CALLERS anywhere in src/ (grep-confirmed this pass). Nothing on NODE_S ever turns
  the raw payload bytes back into appSeq/potValue/ldrValue.
```

**Per the task's own instruction: this is marked exactly as required — IMPLEMENTED LIBRARY FUNCTION, LIVE PATH MISSING.** It does **not** count as end-to-end functionality. Confirmed via `grep -rn decodeData src/` returning only the declaration and definition, no call site.

---

## Phase 11 — Telemetry Contract, Message by Message

All 10 types read in both `telemetry_core.cpp` (builders) and `telemetry.cpp` (callers) this pass, cross-referenced against `gui-main/gui-main/docs/gui-telemetry-contract.md`'s frozen schema (also read in full this pass) and the GUI's own parser (`applyTelemetry()`'s `switch`).

| Type | Generator | Frequency (firmware) | Frequency (contract) | GUI consumer | OLED consumer | Test coverage |
|---|---|---|---|---|---|---|
| `HELLO` | `telemetry::init()` | once at boot | once at boot + reconnect | `nodeGrid` | no | host (buildHello) |
| `HEARTBEAT` | `tick()` | 1000ms | 1000ms | `nodeGrid` (lastSeenWall) | no | host |
| `NODE_STATUS` | `tick()` | 1000ms | on change + 1000ms | `nodeGrid` | no | host |
| `LINK_UPDATE` | `tick()`, per direct neighbor | 250ms | 250ms | firmware-link panel, chart (A↔B only) | S's LINK_QUALITY screen (via direct `predictor::` reads, not this JSON) | host |
| `ROUTE_UPDATE` | `onRouteEvent()`, on real change only | event-driven | on active/candidate/reason change | topology diagram, route-candidates panel | no | host + real-JSON-parse |
| `PREDICTION` | `tick()`, per direct neighbor | 250ms | 250ms + on state change | prediction panel | no | host |
| `SENSOR_STATUS` | `tick()` | 1000ms | 1000ms + on health change | sensor panel, anomaly flag (Node C special-cased: `raw.nodeId==='C'`) | C's SENSOR_ANOMALY screen (direct read) | host |
| `EVENT` | 5 event hooks | event-driven | immediately, one per event | event log | no | host |
| `STATISTICS` | `tick()` | 1000ms | 1000ms | stats (not visibly rendered as its own panel — folded into `state.pdr`) | no | host |
| `ERROR` | `reportError()` (only called on transport init failure) | event-driven | immediately | event log | no | not exercised by any test (no call site other than one fatal boot path) |

**Field-level mismatches found this pass, beyond the already-documented `endToEndLatencyMs`/`rssiSlopeDbPerSec`:** none new — every other field name/type/range in the contract matches what `telemetry_core.cpp`'s builders actually emit, confirmed by direct comparison of the wire `wPrintf` format strings against the contract's own field tables. `NODE_STATUS.reason` is `optional` in the contract and correctly omitted (`nullptr`) by firmware except never actually populated with a real value anywhere (`p.reason = nullptr;` unconditionally in `tick()`) — an honest absence, not a bug.

---

## Phase 12 — GUI Audit (rebuilt 2026-08-18 against the replacement GUI; superseded findings struck, not deleted)

`mesh-command-console (1).html` is a single 116KB file (140 dense lines) — no separate JS/CSS files, no build step, no framework, same no-CDN/no-network-dependency design as before. Every claim below is from a direct read of its actual `<script>` content this pass, cross-referenced against `telemetry.cpp`/`telemetry_core.cpp` (also re-read this pass) — not against `gui-main/gui-main/docs/manual.md`'s own descriptions, which are checked *against* the code, not trusted as ground truth for it.

### What's new vs. the original GUI
Judge-facing DEMO/EXPLAIN/EXPERT mode switching, a scripted 3-minute demo timeline (`runDemo()`), a conceptual "Data Center" deployment view and a "Bridge" view mapping the real prototype to it (both repeatedly, explicitly self-labeled `CONCEPTUAL` / `NOT A PRODUCTION DCIM INTEGRATION` — no overclaiming found), a 10-second telemetry replay buffer, an event timeline, a "tech inspector," per-phase "feature drawer" explainers, and a guided "learn" walkthrough. All of this is presentation layer on top of the same core telemetry consumption.

### The core parser is a verbatim carryover — confirmed by direct comparison
`applyTelemetryCore(raw)` — the function that actually interprets `HELLO`/`HEARTBEAT`/`NODE_STATUS`/`LINK_UPDATE`/`ROUTE_UPDATE`/`PREDICTION`/`SENSOR_STATUS`/`EVENT`/`STATISTICS`/`ERROR` — is **character-for-character identical** to the original GUI's `applyTelemetry()` (confirmed by direct comparison of both function bodies, not a paraphrase). `renderFirmware()` is likewise identical. Consequence: every finding from the original Phase 12 that concerned this core logic **still applies exactly**, unchanged:
- The `rssiSlopeDbPerSec` unit-naming mismatch (already fixed, documentation-only, in `docs/gui-compatibility-matrix.md`) — same field, same usage, still real.
- **Single-node telemetry source — still the most important finding, unchanged.** `let port,reader,bridge,serialSession=0;` — still singular variables; `navigator.serial.requestPort()` — still exactly one port at a time. Every consequence documented in the original finding (`NODE_S` never producing its own `ROUTE_UPDATE` for the A→S flow, the demo needing to choreograph which physical board is connected when) applies identically to this GUI. Not something a GUI redesign can fix — it's a transport-layer fact.
- The "Latest reroute lead-time" metric (now labeled "Predictive lead-time · live") is **still simulation-only** in live mode — see the dedicated finding below; the new GUI is honest about it (explicit `—` fallback) but the underlying gap (firmware never computes `leadTimeMs`) is unchanged.

### ~~Dead code from function redeclaration~~ — SUPERSEDED, now a deliberate two-layer design
The original finding (duplicate `applyTelemetry`/`draw` declarations silently overriding each other) doesn't describe this file. Instead, this GUI cleanly separates `applyTelemetryCore(raw)` (the verbatim-carried-over parser, called first) from an outer `applyTelemetry(raw)` **wrapper** that calls it, then layers real UI enrichment on top: mission-ribbon phase transitions, a "decision HUD" popup, node heartbeat pulse animation, packet-flow ingestion, and event-timeline entries. This is a real, intentional two-layer structure, not leftover dead code — the earlier "dead code" finding is retracted for this file (it may still be worth asking the GUI owner whether it was cleaned up deliberately or coincidentally superseded).

### NEW THIS PASS — a real improvement: route-hop rendering is no longer restricted to `ABS`/`ACDS`/`AS`
This is a genuine fix to a limitation `docs/decisions.md`/`docs/gui-compatibility-matrix.md` have documented as open since Phase 6. `applyTelemetryCore()` still only calls `setRoute()` for the three known hop-key strings (logging a `PARSE WARNING` for anything else) — **but** the outer `applyTelemetry()` wrapper *also* calls `setRoute(payload.active.hops, ...)` directly with the real, untruncated hops array from any `ROUTE_UPDATE`, unconditionally: `if(raw.type==='ROUTE_UPDATE'&&payload.active&&Array.isArray(payload.active.hops))setRoute(payload.active.hops,...)`. `setRoute()`'s own `normalizeHops()` accepts an arbitrary array directly, and the topology diagram's edge-highlighting (`render()`) builds edge IDs from consecutive hop pairs against a fixed map of the 5-node topology's 6 real edges (`AB`,`BS`,`AC`,`CD`,`DS`,`AS`) — meaning **any real, correctly-reconstructed multi-hop path will now render correctly**, not just the two demo-script routes. This matches the new manual's own claim ("The dashboard supports arbitrary hop arrays. It does not require a hard-coded route string.") and is confirmed true by tracing the actual code path, not just trusting the manual.

One real, minor side effect: because the inner `applyTelemetryCore()` still runs its own restrictive check *first*, a real, valid, non-`ABS`/`ACDS`/`AS` route will produce a spurious `"PARSE WARNING: unrecognized firmware route"` log entry even though the very same route then renders correctly a moment later via the outer wrapper's more general handling. Cosmetic log noise, not a functional break — worth a note to the GUI owner, not urgent.

**This finding is based on direct code tracing, not an executed test harness** (unlike the original GUI's `routeKey()` claim, which Phase 6/7.1 verified with a real Node.js parser harness run). If this matters for demo confidence, the same kind of harness could be built against this file before relying on it live — flagged as a reasonable follow-up, not done this pass.

### NEW THIS PASS — a real, major gap: the new GUI's own manual documents an event/message vocabulary firmware does not emit

`gui-main/gui-main/docs/manual.md` documents a `PACKET` message type (`src`/`dst`/`path`/`priority`/`seq`) and a "recommended" `EVENT.eventType` vocabulary of 13 names: `LINK_DEGRADING`, `REROUTE_PROPOSED`, `REROUTE_COMMITTED`, `NODE_SILENT`, `TIMEOUT_FALLBACK`, `PACKET_SENT`, `PACKET_DELIVERED`, `PACKET_RETRY`, `PACKET_RECOVERED`, `DUPLICATE_SUPPRESSED`, `ANOMALY_SPIKE`, `ANOMALY_STUCK`, `PRIORITY_OVERRIDE`, `RECOVERY`.

Firmware's real `telemetry.cpp` (re-confirmed this pass, all 5 event-emitting call sites) only ever emits **8 distinct `eventType` values**: `PRIORITY_ROUTE`, `ROUTE_CHANGE`, `LINK_DEGRADING`, `LINK_FAILURE`, `SENSOR_ANOMALY`, `SENSOR_FAILURE`, `PACKET_RETRY`, `PACKET_DROP`. Cross-referencing name-by-name against the manual's list: only `LINK_DEGRADING` and `PACKET_RETRY` match exactly. `REROUTE_COMMITTED` (manual) vs. `ROUTE_CHANGE` (firmware), `PRIORITY_OVERRIDE` (manual) vs. `PRIORITY_ROUTE` (firmware), and `ANOMALY_SPIKE`/`ANOMALY_STUCK` (manual) vs. `SENSOR_ANOMALY`/`SENSOR_FAILURE` (firmware) are the same underlying real event described with different names. `REROUTE_PROPOSED`, `NODE_SILENT`, `TIMEOUT_FALLBACK`, `PACKET_SENT`, `PACKET_DELIVERED`, `PACKET_RECOVERED`, `DUPLICATE_SUPPRESSED`, and `RECOVERY` have **no firmware equivalent at all** — nothing in `telemetry.cpp` ever emits them, by design (e.g. the code comment on `onReliabilityEvent()`: *"PACKET_TX/PACKET_ACK/PACKET_DELIVERED/.../DUPLICATE_DROPPED: no discrete EVENT"*).

Similarly, the manual's `PACKET` message type (needed for the topology diagram's animated packet-flow dots to show anything in live mode — confirmed by reading `ingestPacket()`, which requires a `path`/`hops` array firmware's real `EVENT` payloads never include) **does not exist anywhere in firmware.** `telemetry_core.cpp` builds exactly the same 10 message types as the original frozen contract (`gui-telemetry-contract.md`) — no `PACKET` builder exists.

**Consequence, precisely stated (traced through the actual code, not assumed):** the GUI's *core* rendering (topology active-path via `ROUTE_UPDATE`, link health, prediction/hysteresis, sensor state, priority-override styling) all work correctly against firmware's real, existing 10-message/8-eventType contract — none of that depends on the missing vocabulary. What's specifically dormant in live mode: the "Instantaneous failure" decision-HUD popup (needs `NODE_SILENT`/`TIMEOUT_FALLBACK`), the animated packet-flow dots (needs `PACKET`), and the duplicate-suppression flourish (needs `PACKET_RECOVERED`/`DUPLICATE_SUPPRESSED`) — all three still work fine in DEMO/SIMULATION mode, and none of them silently fabricate data in live mode (each path is honestly gated on the real message arriving). This is a real, **major** decision point for the team, not a bug: either extend firmware's telemetry to cover the new vocabulary (a real, non-trivial addition — new `MessageType`/wire builder for `PACKET`, new `EVENT` names, `leadTimeMs` computation to make `REROUTE_COMMITTED` meaningful), or accept these specific visual flourishes stay simulation-only for this demo and rely on the core panels (which are fully real and, per the finding above, now render *more* firmware output correctly than before).

### Live/replay/demo provenance safety — verified, no mislabeling risk found
Traced every path that could show fabricated data as real: `runDemo()` explicitly refuses to run (`if(state.mode!=='sim'){...return}`) whenever a real connection is active; `duplicatePacketScenario()` and `thermalScenario()` both independently guard the same way; `provenanceBadge`/`modeBadge`/`provenanceCompact` are recomputed from the true `state.mode` on every render tick (`syncWorldClass`/`syncJudgeShell`, every 120ms) — never set once and left stale; `prepareLive()` resets all display fields to `—`/null the moment live mode is entered, so no leftover simulated number can be mistaken for a real one. This matches the new manual's own "LIVE vs SIMULATION truth rules" section, and — more importantly — matches it in the actual code, not just the prose.

### GUI panel-by-panel data provenance (updated)

| Panel | Real or simulated? | Source |
|---|---|---|
| Topology diagram (active path) | Real IF connected to the node making that decision; **now supports any real reconstructed route**, not just `ABS`/`ACDS`/`AS` — see finding above | `ROUTE_UPDATE.active.hops` (arbitrary array) or `simTick()` |
| "Predictive lead-time · live" metric | **SIMULATION-ONLY in practice** — the code path for a real value exists and is honestly gated, but firmware never sends one | `EVENT.details.leadTimeMs` (never sent) or `simTick()`'s fabricated value |
| "Rolling packet delivery ratio" metric | Real if any `LINK_UPDATE`/`PREDICTION`/`STATISTICS` arrives | multiple real fields, last-write-wins |
| "Anomaly events" metric (renamed from "Anomaly false positives") | **Still dead in both modes** — confirmed no code anywhere writes to `#falsePos` in this file either | none |
| Firmware node grid | Real, but only for whichever node(s) are connected | `HELLO`/`HEARTBEAT`/`NODE_STATUS` |
| Link/route/prediction/sensor panels | Real | `LINK_UPDATE`/`ROUTE_UPDATE`/`PREDICTION`/`SENSOR_STATUS` |
| Event timeline / decision HUD | Real for the 8 eventTypes firmware emits; dormant (not fabricated) for the rest | `EVENT`, gated per-name |
| Packet-flow animation | **Simulation/demo only in practice** — no firmware `PACKET` message exists | `ingestPacket()`, fed only by `simTick()`/demo actions |
| Data Center / Bridge views | Explicitly, repeatedly labeled conceptual; driven by the same real `state.link`/`state.routeHops`/`state.flag` underneath | derived from the real panels above |

**The `"Anomaly events"` dead-metric finding carries over unchanged from the original audit** — still worth flagging to the GUI owner.

---

## Phase 13 — OLED Audit

| | Node S | Node C |
|---|---|---|
| Why this node has an OLED | guide §03: "Node S (mesh telemetry)" | guide §03: "Node C (local anomaly flag)" |
| Should show | live link_score for current best path, reroute events | SPIKE/JUMP + STUCK, shown independently, idle at rest |
| Actually shows | `NODE_STATUS` + `LINK_QUALITY` (per-direct-neighbor `linkScore`, best marked `*`) + `LINK_EVENT` override on a real neighbor health flip | `NODE_STATUS` + `SENSOR_ANOMALY` (POT/LDR SPIKE=Y/N, STUCK=Y/N) |
| Data source | `predictor::linkScore()`/`isUnhealthy()` — direct, side-effect-free reads, own edge-detection (no event-callback slot used) | `anomaly::getTelemetry()` — direct, side-effect-free reads |
| Update rate | `OLED_SCREEN_CYCLE_MS`=3s auto-cycle, `OLED_REFRESH_MIN_INTERVAL_MS`=400ms redraw floor | same |
| Can it block networking? | No — only ever called from `app::loop()`'s `oled::tick()`, never from the ESP-NOW receive callback or any routing/reliability callback | same |
| Shares callback infrastructure? | No — deliberately does not register on `predictor::setEventCallback()`/`anomaly::setEventCallback()` (both already single-subscriber, owned by `telemetry.cpp`) | same |
| Uses real hardware config? | `PIN_OLED_SDA`/`SCL`=21/22, `OLED_I2C_ADDRESS_S`=0x3C, `Adafruit_SSD1306` — all team-confirmed real values | `PIN_OLED_SDA`/`SCL`=21/22 (shared bus), `OLED_I2C_ADDRESS_C`=0x78 (team-corrected 2026-08-18 — differs from S, not a shared constant), `Adafruit_SH1106G` |
| Verification | ESP32-compile-verified (957,292 bytes, 0 warnings), 22/22 host-tested (`oled_core`) | same |
| **Physical** | **NOT RUN — no display has ever shown a frame** | same |

Nodes A, B, D: `hasOled=false` in `core/node_id.h`'s table; `oled::init()`/`tick()` are single-boolean-check no-ops on those boards, confirmed by code.

---

## Phase 14 — Simulation vs. Hardware

| Feature | Simulation (GUI/mock) | Firmware | GUI (live mode) | Physical HW | Same semantics? |
|---|---|---|---|---|---|
| RSSI | fabricated ramp (`state.link` interpolation) | real `info->rx_ctrl->rssi` | displays whatever arrives | never run | N/A — sim never claims to be RSSI, it's a pre-fused `linkAB` score |
| PDR | fabricated (`.99`, decaying in mock script) | real per-attempt EWMA | displays real value if connected | never run | consistent meaning (ratio, 0-1), different source |
| Prediction/hysteresis | not modeled — sim only tracks a single `link` scalar vs one threshold | real 2-threshold+debounce state machine | shows real `predictionState`/`hysteresisState` if `PREDICTION` arrives | never run | **GUI's own simulation is structurally simpler than firmware's real algorithm** — sim's `below===3` check is a coincidental echo of firmware's real debounce count, not derived from it |
| Sensor anomaly | scripted (`setFlag` on keypress/timer) | real MAD-Z + flatline | shows real `healthState` if `SENSOR_STATUS` arrives, mapped `flatline→stuck`/`anomaly→spike` | never run | consistent categories |
| Route failure/recovery | scripted demo timeline (`runDemo()`, 175s script) | real hysteresis-gated `selectNextHop()` | shows real `ROUTE_UPDATE` (**now any real hop array, not just `ABS`/`ACDS`**) if the right node is connected | never run | see Phase 12 findings |
| Reroute lead time | **fabricated random-ish number, always, in DEMO/SIM** | **never computed, ever** | honestly shows `—` in live mode; nothing in live mode from firmware | never run | **not the same feature — one is real-shaped fiction, the other genuinely doesn't exist** |
| Packet loss / duplicate handling | not modeled numerically; `duplicatePacketScenario()` is a scripted visual only | real, host-tested | not visible except via `STATISTICS` counters — the animated packet-flow dots need a `PACKET` message firmware doesn't emit | never run | GUI has no equivalent live-mode concept for this |
| Latency | not modeled | real per-hop `lastLatencyMs` | shown as `endToEndLatencyMs` (misleadingly named, documented) | never run | same caveat as above |
| Node failure | `duplicatePacketScenario()`/demo timeline model specific scripted beats, not a generic "node offline" action | real staleness→OFFLINE | `refreshFirmwareStaleness()` marks a node STALE after `offlineTimeoutMs` if no HEARTBEAT; the "instantaneous failure" decision-HUD popup needs `NODE_SILENT`/`TIMEOUT_FALLBACK` events firmware doesn't emit | never run | consistent for the one connected node only; the popup flourish is dormant, not wrong |

**Where GUI says one thing and firmware does another:** the lead-time metric is still the clearest case — in live mode it now honestly shows `—` rather than a stale number, but the underlying capability (measuring and reporting real lead time) still doesn't exist in firmware. The second-clearest case, new this pass: the replacement GUI's own manual documents an event/message vocabulary (`PACKET`, `NODE_SILENT`, `TIMEOUT_FALLBACK`, `PACKET_RECOVERED`, `DUPLICATE_SUPPRESSED`, `REROUTE_PROPOSED`) that firmware does not implement — see Phase 12.

---

## Phase 15 — Novelty Audit

**Baseline (well-precedented, not the novelty):** ESP-NOW mesh, distance-vector routing, multi-hop forwarding, hop-by-hop ACK/retry, RSSI monitoring, ADC sensors, a web dashboard, a local OLED.

| Candidate | Claim | Implementation | Evidence | Test | Physical demo | Limitation |
|---|---|---|---|---|---|---|
| Predictive link degradation | detect a worsening trend before outright failure | real (Phase 5 equations) | `predictor_core.cpp` | 31/31 host | not yet | lead time is never measured (Phase 6) |
| RSSI/PDR fusion | one fused `link_score` from two independent signals | real | same | same | not yet | fixed 0.5/0.5 weights, never tuned against real hardware |
| Hysteresis-based predictive route switching | avoid flapping while still reacting | real, 2-threshold+debounce | `routing_core::selectNextHop()`'s health mask | 37/37 host | not yet | none found |
| Self-healing route adaptation | traffic actually moves to a healthy alternate | real, traced Phase 7 | host-tested per layer | no integration test of the full chain | not yet | no test exercises the *whole* chain as one scenario |
| Simultaneous network + sensor health awareness | both monitored, kept independent | real — confirmed by absence of cross-imports (Phase 9) | `anomaly.cpp`/`predictor.cpp` | separate suites | not yet | none found |
| Local node visibility (OLED) | per-node-role-appropriate local display | real, this session's work | `src/oled/` | 22/22 host | not yet | never run against real hardware |
| Edge anomaly detection | MAD-Z + flatline, independent detectors | real | `anomaly_core.cpp` | 50/50 host | not yet | none found |
| Coordinated telemetry of network + sensor state | one wire contract carries both | real | `telemetry_core.cpp`, all 10 types | 99/99 host + real-JSON-parse | GUI can show both **only for whichever node is connected** (Phase 12) | single-source architecture limits which node's story the GUI can tell at once |

**Not novel, or not demonstrated:** "prediction lead time" as a measured, provable claim — the mechanism that would *cause* early rerouting is real, but the number that would *prove* it happened early is not computed anywhere. Any novelty claim resting on "we can show you the reroute happened before failure, by X ms" is **currently unsupportable by firmware** — only by the GUI's own fabricated number.

---

## Phase 16 — Demo Scenarios

| # | Scenario | Precondition | Action | Expected FW event | Expected telemetry | Expected GUI | Expected OLED | Success | Failure sign |
|---|---|---|---|---|---|---|---|---|---|
| 1 | Normal operation | mesh converged | (passive) | periodic `MSG_DATA` A→S | `LINK_UPDATE`/`PREDICTION`/`SENSOR_STATUS`/`STATISTICS` | node cards populate, chart moves | S: link scores; C: flags clean | S receives `PACKET_RECEIVED` events | no traffic / stale nodeGrid |
| 2 | Sensor anomaly | C calibrated | twist/hold pot on C | `SENSOR_ANOMALY`/`SENSOR_FAILURE` event | `EVENT`+`SENSOR_STATUS` | anomaly flag panel (only if C is the connected node) | C: SPIKE or STUCK flips Y | flag visibly changes | flag never updates |
| 3 | Link degradation | A-B link healthy | attenuate B (Faraday bag) | `LINK_DEGRADING`→`LINK_FAILURE` | `EVENT`, `PREDICTION` state change | prediction panel updates (if connected to A or B) | S: `*` marker may shift if B/D scores cross | `predictionState` transitions | no state change observed |
| 4 | Node failure | B alive | power off B | staleness at A (2-3s) | `ROUTE_UPDATE`/`EVENT ROUTE_CHANGE` **only visible if A (or the new forwarder) is connected — see Phase 12** | topology animates only if right node connected | S: B's `*` marker disappears from viable set once B's link is stale-derived unhealthy | traffic continues via A-C-D-S | S stops receiving data |
| 5 | Node recovery | B rejoins | power on B | hysteresis recovery (3 good evals) | `ROUTE_UPDATE` reason=`ROUTE_RECOVERY` | same caveat as #4 | S: B's score returns | route returns to A-B-S | route stuck on alternate forever |
| 6 | Combined | both 2+3 | sensor + link stress together | both event streams | both | both panels (still single-node-limited) | both nodes' OLEDs independently correct | both chains independently verifiable | either one masks the other |

**Every "Expected GUI" cell above carries the same caveat: correct only if the physically-connected node is the one that actually produces that specific telemetry.** This should be written into the actual demo rehearsal script, not left implicit. **Update, replacement GUI (2026-08-18):** once connected to the right node, the topology diagram will now correctly animate any real reconstructed route, not just the two demo-script routes (see Phase 12) — the "topology animates only if right node connected" caveat in scenarios 4/5 is about *which node*, not about whether the specific route string is recognized, which is no longer a separate risk.

---

## Phase 17 — Master Test Matrix (abbreviated — full detail in `docs/testing.md`)

| Category | Status | Evidence |
|---|---|---|
| UNIT (`*_core`) | 382/382, 8 suites | `docs/testing.md` |
| INTEGRATION (cross-module, one scenario) | **MISSING** — no test drives predictor→routing→reliability→telemetry as one connected run | this pass, Phase 7 |
| HOST | same as UNIT | — |
| ESP32 COMPILE | clean, both `ENABLE_UCB1` configs | `docs/testing.md` |
| HARDWARE | **NONE** | `docs/hardware-readiness.md` |
| GUI | real-JSON-parse harness run (Phase 6/7.1), static reasoning for the rest | `docs/testing.md` |
| END-TO-END | **NONE** | — |
| DEMO (rehearsed) | **NONE** | — |
| Negative: malformed packet length | **NOT TESTED** at the adapter boundary (the gap itself is real — see Phase 3); `*_core` functions that take a `len` do reject short inputs, host-tested | apptraffic/telemetry/routing wire parsers, 88+29+37 host checks partially cover this per-module |
| Negative: wrong node ID | partially — `routing_core`/`predictor_core`/`reliability_core` all reject out-of-range IDs, host-tested | — |
| Negative: wrong MAC | **NOT TESTED, NOT VALIDATED IN CODE** (Phase 2/8 gap) | — |
| Negative: wrong ACK sender | **NOT TESTED, NOT VALIDATED IN CODE** (Phase 8 gap) | — |
| Negative: duplicate | tested, 88/88 | — |
| Negative: route loop | tested for the specific immediate-bounce-back case; no explicit multi-node loop test exists | — |
| Negative: node reboot/disappearance/recovery | covered at the `*_core` staleness/hysteresis level; no scripted multi-node scenario | — |
| Negative: sensor stuck/spike | tested, 50/50 | — |
| Negative: OLED disconnect | `init()`'s `begin()` failure path is coded (fail-soft, no hang) but **never actually tested against a real failed display** | — |
| Negative: GUI disconnect | real code exists (`teardownSerial()`), not tested by this project's own test suite (GUI has no test suite at all) | — |
| Negative: serial corruption | GUI: `try/catch` around every `JSON.parse`, logs `PARSE WARNING`, never crashes — read and confirmed this pass | — |

---

## Phase 18 — Architectural Risks

Ranked by likelihood × impact × demo risk × fix complexity × regression risk, not just theoretical severity.

**P0 (demo/system correctness blocker):**
- Single-node telemetry source vs. the reroute-demo's actual event origin (Phase 12) — not a code bug, but will silently fail the demo's headline moment if not choreographed correctly.
- `leadTimeMs` structurally absent from firmware (Phase 6) — any claim resting on a real, on-screen lead-time number will show simulation data or nothing.
- Sink never decodes application payload (Phase 10) — a demo audience cannot see decoded POT/LDR values arrive at S from the JSON telemetry path; only raw-byte-length facts are visible unless this is wired up.

**P1 (serious reliability issue, real but lower likelihood of manifesting mid-demo):**
- Route advertisements accepted from non-neighbors (carried forward, prior pass).
- Hardware MAC never programmatically verified against compiled identity (carried forward — this is the exact mechanism behind the real zero-MAC symptom already reproduced on hardware in this session's history).
- Missing packet-length clamp (Phase 3).
- ACK sender not validated against pending slot's `nextHop` (Phase 8).
- Retry `send()` return value ignored (Phase 8).

**P2 (important, non-blocking):**
- No TTL/hop-limit field (mitigated by duplicate filter + `MAX_HOP_COUNT` ceiling).
- No split-horizon/poison-reverse (mitigated by the same ceiling).
- No ESP-NOW recovery/reinit path.
- No watchdog handling anywhere.
- Buzzer pin defined, never driven (Phase 2) — cosmetic, guide marks it optional.
- GUI's dead-code function redeclaration (Phase 12) — zero functional impact.
- GUI's permanently-dead "Anomaly false positives" metric (Phase 12).

**P3 (polish/future work):**
- Sequence identity resets on reboot (low practical impact given TTL timing).
- Init-order callback-liveness window (narrow, low likelihood).
- No integration test connecting predictor→routing→reliability→telemetry as one scenario.

---

## Phase 19 — Single Master Checklist

Format: `[ ] Item / Location / Depends on / Verification / Evidence required / Priority`

### A. Hardware
- [ ] Confirm UART/RESET/BOOT pins per physical board — Location: physical boards / Depends on: none / Verification: `arduino-cli board list` + continuity check / Evidence: photo+log per board / **P1**
- [ ] Confirm WiFi channel choice against real RF congestion at demo site — Location: `config.h::MESH_WIFI_CHANNEL` / Depends on: site access / Verification: RF survey / Evidence: survey log / **P2**
- [ ] Confirm OLED I2C address on both real modules via scanner sketch — Location: physical S/C boards / Depends on: boards wired / Verification: I2C scan / Evidence: scan output / **P1**
- [ ] Verify buzzer intentionally unused, or wire it — Location: `PIN_BUZZER`, no driver exists / Depends on: decision / Verification: code + hardware / Evidence: this document's Phase 2 finding / **P3**

### B. Firmware boot
- [ ] Confirm real `WiFi.macAddress()` behavior on actual hardware in the exact `main.cpp::setup()` calling context — Location: `main.cpp` / Depends on: flashed board / Verification: Serial log / Evidence: boot log showing real (non-zero) MAC / **P0** (already reproduced as a real failure once this session)
- [ ] Consider closing the init-order window (`transport::begin()` before `routing/predictor/reliability::init()`) — Location: `main.cpp` / Depends on: none / Verification: reorder + re-compile + re-test / Evidence: byte-identical compile, 382/382 host / **P3**

### C. Transport
- [ ] Decide on (and if approved, implement) moving heavy work out of the ESP-NOW recv callback — Location: `espnow_transport.cpp`/`main.cpp` / Depends on: explicit go-ahead (invasive) / Verification: new host tests + real compile + hardware soak / Evidence: no packet loss under burst / **P1**, invasive
- [ ] Add MAC-to-node cross-check (`evt.mac` vs `nodeInfo(prev_hop).mac`) — Location: `main.cpp::onTransportRx` / Depends on: none / Verification: host test (new) + real compile / Evidence: mismatch logged/rejected / **P1**

### D. Packet layer
- [ ] Clamp `payload_len` to `PACKET_MAX_PAYLOAD` on receive — Location: `main.cpp::onTransportRx` / Depends on: none / Verification: new host test, malformed-length case / Evidence: no OOB read / **P1**

### E. Routing
- [ ] Validate advertisement `from` against `neighborsOf(self)` — Location: `routing_core::applyRouteAdvertisement` / Depends on: none / Verification: new host test (non-neighbor rejected) / Evidence: 37+ host checks / **P0** (implicated in the real anomalous-route boot log from this session's history)
- [ ] Decide on TTL/hop-limit field addition — Location: `core/packet.h` (wire-format change) / Depends on: explicit go-ahead (touches every module) / Verification: full regression / Evidence: 382/382 unchanged elsewhere / **P2**, invasive
- [ ] Decide on split-horizon/poison-reverse — Location: `routing_core::buildAdvertisement` / Depends on: explicit go-ahead / Verification: new host tests, loop scenario / Evidence: convergence proof / **P2**, invasive

### F. Link quality
- [ ] Re-tune `SLOPE_REF`/`T_LOW`/`T_HIGH`/`ANOMALY_*` placeholders against real hardware data — Location: `config.h` / Depends on: flashed boards, real degradation test / Verification: physical Faraday-bag test / Evidence: recorded RSSI/score trace / **P1**, needs hardware

### G. Predictor
- [x] **DONE (2026-08-18)** — real `leadTimeMs` measurement implemented: `telemetry.cpp::onRouteEvent()`, `leadTimeMs = max(0, ROUTING_ENTRY_TIMEOUT_MS - stalenessOfOldNextHopMs)`, only for `LINK_DEGRADATION_R`. Real timestamps + one real constant, never extrapolated. See `docs/decisions.md`. Verified via real GUI-parser harness (`state.lead===1850` round-tripped through the real, unmodified GUI code).

### H. Reliability
- [ ] Validate ACK sender against pending slot's `nextHop` — Location: `reliability.cpp::handleAck` / Depends on: none / Verification: new host test / Evidence: wrong-sender ACK rejected / **P1** — still open, not touched this pass (out of this pass's scope; a real, separate hardening item)
- [ ] Check retry `send()` return value, mirror `transmitHop()`'s failure handling — Location: `reliability.cpp::tick()` / Depends on: none / Verification: new host test / Evidence: immediate cancel on rejected retry / **P1** — still open, not touched this pass

### I. Sensors
- [ ] Physically confirm calibration/flatline/spike thresholds against real ADC noise — Location: `config.h` placeholders / Depends on: flashed boards / Verification: bench test / Evidence: recorded traces / **P1**, needs hardware

### J. Anomaly detection
- [ ] No code changes identified as required — confirmed independent of routing, confirmed correctly debounced / **info only**

### K. Application traffic
- [x] **DONE (2026-08-18)** — `apptraffic_core::decodeData()` now has a real live caller: `telemetry.cpp::onReliabilityEvent()`'s `PACKET_RECEIVED` case, at the real sink. Decoded `appSeq`/`potValue`/`ldrValue`/`appTimestampMs` flow into the new `PACKET` telemetry message and a Serial log line. Verified: real ESP32 compile, host tests (`test_telemetry_core.cpp`'s `PacketPayload` tests), real GUI-parser harness.

### L. Telemetry
- [x] `rssiSlopeDbPerSec` unit mismatch documented (prior pass, doc-only) — **done**
- [x] **DONE (2026-08-18)** — `EVENT.details.leadTimeMs` is now populated for real, proactive reroutes. See section G above.
- [x] **DONE (2026-08-18)** — new `0x0B PACKET` message type + 5 new real `EVENT` types (`NODE_SILENT`, `TIMEOUT_FALLBACK`, `REROUTE_PROPOSED`, `PACKET_RECOVERED`, `DUPLICATE_SUPPRESSED`), all driven from real state, all host-tested (120/120 telemetry_core, 42/42 routing_core, 90/90 reliability_core), all real-ESP32-compile-verified (both `ENABLE_UCB1` configs), all confirmed against the real, unmodified GUI parser via a real Node.js harness run. See `docs/gui-compatibility-matrix.md`'s new EVENT-vocabulary-reconciliation table and `docs/decisions.md`.

### M. OLED
- [x] Implemented, host-tested, ESP32-compile-verified this session
- [ ] Physically verify on real S (SSD1306) and C (SH1106) hardware — Depends on: flashed boards / Verification: visual + Serial `[OLED] init ok` log / Evidence: photo / **P1** — still hardware-only, unchanged

### N. GUI
- [ ] Choreograph which physical node is connected to the GUI at which point in the demo — **still open** — but now substantially mitigated: see the new multi-node bridge below, which makes "which node is connected" no longer an either/or choice
- [x] ~~Clean up the duplicate `applyTelemetry`/`draw` function declarations~~ — **N/A for the replacement GUI**
- [ ] (GUI owner's call) decide whether to wire up or remove the dead "Anomaly events" metric (`#falsePos`) — **P3**, out of scope
- [x] **DONE (2026-08-18)** — firmware now implements the new vocabulary for real: `PACKET` message type, `NODE_SILENT`/`TIMEOUT_FALLBACK`/`REROUTE_PROPOSED`/`PACKET_RECOVERED`/`DUPLICATE_SUPPRESSED` events, all real, all tested. One real, non-blocking naming mismatch remains (`PRIORITY_OVERRIDE`/`RECOVERY` vs. this project's `PRIORITY_ROUTE`/no-discrete-recovery-event) — documented, not silently papered over, see `docs/gui-compatibility-matrix.md`.
- [x] Route-hop rendering restriction (`ABS`/`ACDS`/`AS`-only) — **RESOLVED in the replacement GUI**, confirmed
- [x] **DONE (2026-08-18) — multi-node GUI, without editing `gui-main/`:** `tools/multi-node-bridge.py` (new file, repo root) multiplexes N real per-node serial ports onto one WebSocket, which the GUI's existing, unmodified "Connect via Bridge" feature already knows how to consume (its state model is already keyed by real `nodeId`, confirmed by reading `applyTelemetryCore()`). Real-tested: `--mock` mode + a real Python `websockets` client confirmed all 5 simulated `nodeId`s arrive over one connection. See `docs/decisions.md`.

### O. Simulation
- [ ] None required — simulation mode's role (offline rehearsal) is honestly labeled and does not misrepresent itself as live data anywhere in the UI text

### P. Integration
- [ ] Build a host-level integration test that drives predictor→routing→reliability→telemetry as one connected scenario (B degrades → reroute → EVENT emitted) without touching hardware — Location: new `test/` file / Depends on: none / Verification: new suite, run alongside the 382 existing / Evidence: a passing scenario test / **P2**

### Q. Failure/recovery
- [ ] Physical relay-failure/recovery rehearsal (Demo Scenarios #4/#5) — Depends on: flashed boards / **P0** for demo, blocked on hardware

### R. Novelty
- [ ] Do not claim measured predictive lead time in any demo narration unless G above is implemented — **P0** for honesty/credibility

### S. Demo
- [ ] Write the actual demo runbook incorporating the Phase 12/16 single-node-connection choreography — **P0**

### T. Documentation
- [x] This document — `docs/full-system-audit.md`
- [ ] Fold confirmed P0/P1 findings (E, H, K, N, S above) into `docs/known-issues.md` once the team decides which to act on

---

## Phase 20 — Final Architecture Verdict

### 1. What definitely works (CODE-IMPLEMENTED + HOST-TESTED, cross-checked this pass)
Transport, packet framing, distance-vector routing table + selection, RSSI/PDR/link_score/hysteresis math, hop-by-hop reliability (ACK/retry/dedup/forward), sensor calibration/MAD-Z/flatline state machine, application DATA generation + encoding (send side only), all 10 telemetry message builders, OLED screen scheduling. 382/382 host checks, clean ESP32 compile both `ENABLE_UCB1` configs.

### 2. What is implemented but not proven
Everything in #1, on physical hardware — nothing has been flashed. The full self-healing chain (Phase 7) as one connected scenario — each layer is tested alone, never together.

### 3. What is simulation only
The GUI's `simTick()`/keyboard-action degradation/anomaly/priority scenarios and the scripted demo timeline, when not connected to live hardware. (The lead-time metric and packet-flow animation are **no longer** simulation-only as of the 2026-08-18 implementation pass — both now have real firmware code paths, confirmed by host tests, real compile, and a real GUI-parser harness run.)

### 4. What is GUI only
Nothing found that the GUI displays without ever being traceable to a real (if currently absent) firmware concept — the contract is genuinely firmware-authoritative, confirmed by reading the parser.

### 5. What is missing
A driven buzzer (Phase 2, likely intentional — implementation-guide.html marks it optional). An integration test connecting the full self-healing chain (Phase 7/17) as one scenario. ACK-sender validation and retry-`send()`-return-value checking (Phase 8/H — real, small, still open). Route-advertisement neighbor validation and MAC verification (Phase 4/8, still open — **not** addressed this implementation pass, out of its scope). Physical hardware verification of everything (nothing has been flashed).

### 6. What is broken
Nothing found is broken in the sense of "compiles/runs but produces wrong output." One real, precise, harness-confirmed GUI-side cosmetic issue was found and **fixed the same day**: the replacement GUI's own `applyTelemetryCore()` had no `case 'PACKET':`, so a real `PACKET` message logged a spurious "PARSE WARNING" even though it was already correctly animated by the same GUI's outer wrapper. Not a firmware defect. Reported with the exact one-line fix; the user explicitly authorized editing `gui-main/` for this one line ("fix"); applied and re-verified via a real harness re-run (24/24, was 23/24) — the only edit ever made to `gui-main/` in this project.

### 7. What is misleadingly documented
Nothing found this pass. The project's own docs remain unusually careful about not overclaiming.

### 8. What must be fixed (still open after the 2026-08-18 implementation pass)
Route-advertisement neighbor validation (E). MAC verification (B). ACK-sender validation and retry-send()-return-value checking (H). Physical hardware verification of literally everything (nothing has been flashed). **Resolved this pass, no longer on this list:** sink decoding (K), real lead-time (G), the new PACKET/EVENT vocabulary (L/N), multi-node GUI aggregation (N).

### 9. What should not be touched
Everything in #1, plus everything added this pass (`PACKET` telemetry, the 5 new EVENT types, the lead-time formula, the sink decode path) — all real, host-tested, real-compiled, and real-harness-verified. Do not restructure the ESP-NOW callback architecture, add TTL, or add split-horizon without an explicit go-ahead — all three are real but invasive, and the system works today without them.

### 10. What the team should implement next
In order of remaining impact: (1) route-advertisement neighbor validation and MAC verification (both P0/P1, both small, not touched this pass); (2) ACK-sender validation and retry-send()-return-value checking (both P1, small); (3) physically flash and rehearse — everything above is now software-complete and needs real hardware to prove out; (4) optionally, reconcile the last few EVENT-name mismatches (`PRIORITY_OVERRIDE`/`RECOVERY`) with the GUI owner if those specific decision-HUD popups matter for the demo — the core causal chain doesn't depend on them.

---

## Final Demo Readiness Scorecard

`GREEN` = verified · `YELLOW` = implemented/partially verified · `RED` = missing/broken · `BLUE` = simulation/documentation only

| Layer | Status | Why |
|---|---|---|
| Hardware | 🟡 YELLOW | 5 real boards, real MACs, pins/OLED confirmed on paper; zero physical verification |
| Firmware (boot/loop) | 🟡 YELLOW | correct order confirmed by code read; compiles clean; never booted with this exact tree |
| Networking (transport/routing basics) | 🟡 YELLOW | fully host-tested; one real gap (neighbor validation) confirmed |
| Routing (self-healing) | 🟡 YELLOW | each layer tested; full chain never tested together or on hardware |
| Prediction | 🟡 YELLOW | real math, real hysteresis; lead-time is now 🟡 YELLOW too (real, measured formula implemented 2026-08-18 — see decisions.md — not yet physically proven) |
| Reliability | 🟡 YELLOW | hop-level ACK correct and well-labeled; ACK-sender validation and retry-send()-return-value checks remain real, open, small gaps |
| Sensors | 🟡 YELLOW | full detector logic host-tested; thresholds are placeholders pending real hardware |
| Application data | 🟡 YELLOW (both directions) | send path complete; **receive-side decode is now real and live** (2026-08-18) — `decodeData()` has a real caller, decoded values flow into telemetry |
| Telemetry | 🟡 YELLOW | all 10 original types plus the new `0x0B PACKET` type, all contract-compliant; `rssiSlopeDbPerSec` naming fixed; `leadTimeMs` now real |
| GUI | 🟡 YELLOW | core parser confirmed verbatim-identical to the original; route-hop rendering genuinely improved; single-node-*hardware*-source limitation now mitigated by `tools/multi-node-bridge.py` (real, tested, outside `gui-main/`); the new `PACKET`/event vocabulary is now real on the firmware side, confirmed against the real GUI parser |
| OLED | 🟡 YELLOW | implemented and compiled; zero physical verification |
| End-to-end | 🟡 YELLOW (was 🔴 RED) | every individual leg now has a real, tested code path (decode, lead-time, packet telemetry, multi-node aggregation) — physical, on-hardware, all-legs-at-once verification still hasn't run (nothing is flashed) |
| Novelty demonstration | 🟡 YELLOW | the mechanisms are real; the lead-time claim is now backed by a real, defensible, documented formula (not simulation-only) — still needs a physical run to become a *proven* claim rather than a *measurable* one |

No fake percentage is given, per instruction. The honest one-line summary
(updated 2026-08-18): **every software-side capability gap this audit
found has a real, tested, documented implementation now — PACKET
telemetry, five new event types, a real measured lead-time formula, a
live sink-side decode path, and multi-node GUI aggregation without
touching `gui-main/`. What remains is exactly what remained before:
physical hardware. Nothing in this project has run on a flashed board
yet — that is the one gate between "software complete" and "demonstrated."**
