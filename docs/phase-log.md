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
- **Real `arduino-cli compile` performed post-Phase-1 (2026-08-17)** against
  the actually installed `esp32:esp32` core 3.3.11: found and fixed one
  real API-drift error (`esp_now_send_cb_t`'s signature), then a clean
  build — 0 errors, 0 warnings. See [testing.md](testing.md) and
  [decisions.md](decisions.md#esp_now_send_cb_t-signature-adapted-for-arduino-esp32-core-3311).
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

---

## Phase 2 — Predictive link health (RSSI EWMA/slope + PDR + staleness fusion)
**Date:** 2026-08-17
**Status:** Complete, awaiting explicit go-ahead for Phase 3.

### Objective
Implement the fused link-degradation predictor — per
implementation-guide.html §5.1 and §06 (Hours 6-12) — combining RSSI EWMA
smoothing + least-squares slope, PDR, and an independent staleness
fast-path into a single `link_score`, gated by a two-threshold hysteresis
state machine, and integrate it into Phase 1's routing layer without
destroying its candidate-route architecture. Explicitly excluded: anomaly
detection (MAD-Z, flatline), the reliability layer (ACK/retransmit/
dup-filter), UCB1, the dashboard, and any Phase 3 work.

### What was built
- `src/predictor/predictor_core.h/.cpp` — the real algorithm, Arduino-free
  (mirrors `routing_core`'s Phase 1 split): per-neighbor RSSI EWMA + ring
  buffer, least-squares slope, PDR EWMA, fused `link_score`, and a
  two-threshold (`T_LOW`/`T_HIGH`) hysteresis state machine combined with
  implementation-guide.html's own 3-consecutive-evaluation debounce, plus
  an independent staleness fast-path that bypasses the debounce entirely.
- `src/predictor/predictor.h/.cpp` — the Arduino-facing adapter: feeds
  real RSSI from the same receive dispatch point routing already uses,
  logs the evidence behind every score (`[PREDICTOR] neighbor=... rssi_ewma=...
  slope=... pdr=... score=... health=...`), and exposes
  `LINK_SCORE_UPDATED`/`LINK_DEGRADING`/`LINK_UNHEALTHY`/`LINK_RECOVERED`
  events via `predictor::setEventCallback()`.
- `src/routing/routing_core.h/.cpp` extended (not replaced): `selectNextHop()`
  gained a backward-compatible, default-`nullptr` `neighborUnhealthy`
  parameter. NORMAL selection now prefers healthy candidates over
  unhealthy ones (falling back to the best available if all are
  unhealthy); PRIORITY selection ignores it unconditionally. All 18 Phase
  1 tests pass unmodified.
- `src/routing/routing.cpp`'s `getNextHop()` now builds that health mask
  from `predictor::isUnhealthy()` before calling `routing_core::selectNextHop()`.
- `src/config.h`: a full `PREDICTOR_*` constant block (EWMA alphas,
  SLOPE_REF, fusion weights, hysteresis thresholds, debounce counts,
  staleness timeout) — see [parameters.md](parameters.md) and
  [decisions.md](decisions.md) for every value's derivation.
- `firmware/PredictiveMesh/test/test_predictor_core.cpp` (new) — 31/31
  checks, covering all 12 required predictor scenarios.
- `firmware/PredictiveMesh/test/test_routing_core.cpp` — 2 new checks
  (scenarios 13-14: priority ignores link health; unhealthy B promotes C),
  alongside the 18 unmodified Phase 1 checks — 21/21 total.
- One real, documented design call: the Phase 1 `isPriorityOnlyEdge`
  exclusion is **kept, unchanged** — link health integrates *alongside* it,
  not as a replacement — because no real hardware exists yet to make
  `link_score` organically distinguish the A-S edge from A-B. See
  [decisions.md](decisions.md#link-health-integrated-into-routing_coreselectnexthop-alongside-not-instead-of-the-priority-only-edge-rule).
- `MeshPacket`/the wire format are unchanged — `link_score` is a purely
  local quantity, never advertised over the air in Phase 2. See
  [decisions.md](decisions.md#no-meshpacketwire-format-changes-needed-for-phase-2).

### What was explicitly NOT built (by design)
Anomaly detection (MAD Z-score, flatline), the reliability layer (ACK,
retransmit, duplicate filtering), UCB1, dashboard/WebSerial, live PDR
wiring (the math/API exist and are tested; `predictor::onSendResult()` has
no live caller yet — see
[decisions.md](decisions.md#pdr-measurement-boundary-not-wired-to-live-send-outcomes-in-phase-2)),
and any change to `implementation-guide.html`'s topology/hardware pins/
transport choice.

### Validation performed
- `firmware/PredictiveMesh/test/test_predictor_core.cpp`: real,
  host-compiled (g++ 15.2.0 / MinGW-W64), actually executed — 31/31 checks
  passed, covering all 12 required predictor scenarios.
- `firmware/PredictiveMesh/test/test_routing_core.cpp`: re-run after
  extension — 21/21 checks passed (18 original + 2 new).
- **Real `arduino-cli compile` performed** against the full Phase 0+1+2
  sketch, `esp32:esp32` core 3.3.11 — clean on the first attempt, 0
  errors, 0 warnings (`--warnings all`). See [testing.md](testing.md).
- No hardware-dependent validation — see
  [known-issues.md](known-issues.md#phase-2-predictor--not-yet-run-on-hardware).

### Git
No commits were made this phase. Working tree left uncommitted for the
user to review.

### Next phase (not started, awaiting explicit go-ahead)
Per implementation-guide.html §06 (Hours 12+), later phases would add the
anomaly engine (§5.2: MAD Z-score + flatline detector), the reliability
layer (§5.4: hop-by-hop ACK, retransmit, duplicate filtering, and actual
multi-hop `MSG_DATA` relaying — which is also the natural point to wire
`predictor::onSendResult()` to real unicast delivery outcomes), and
eventually the reporting/dashboard layer.

---

## Phase 3 — Sensor anomaly + sensor failure detection (MAD-Z + flatline + state machine)
**Date:** 2026-08-17
**Status:** Complete, awaiting explicit go-ahead for Phase 4.

### Objective
Implement the sensor-health/anomaly layer per implementation-guide.html
§5.2 and this phase's own task spec: distinguish a legitimate sensor
value from a statistical anomaly, a stuck/flatlined sensor, and a
stale/unavailable sensor, using two complementary detectors (MAD-Z,
flatline) feeding one explicit sensor state machine, with debounce and
recovery, local telemetry/events, and an explicit separation from
network/link health. Explicitly excluded: routing modification based on
sensor health, ACK/retry, duplicate filtering, UCB1, the final telemetry
system, final demo orchestration, and any Phase 4 work.

### What was built
- `src/anomaly/anomaly_core.h/.cpp` — the real algorithm, Arduino-free
  (mirrors `routing_core`/`predictor_core`'s split): a generic, timestamped
  `SensorObservation` abstraction (not hardwired to the potentiometer/LDR);
  boot-time median/MAD calibration with a variance safety envelope and
  bounded (not infinite) retry; the modified-Z-score spike/jump detector;
  an independent flatline/stuck detector; and a 6-state machine
  (`WARMUP`/`NORMAL`/`ANOMALY`/`FLATLINE`/`STALE`/`INVALID`) with
  debounced entry/recovery on both the anomaly and flatline paths.
- `src/anomaly/anomaly.h/.cpp` — the Arduino-facing adapter: owns two
  `SensorCore` instances (POT/`GPIO34`, LDR/`GPIO35`), performs the
  blocking boot-calibration sequence for real, calls `analogRead()` every
  `SENSOR_SAMPLE_INTERVAL_MS`, logs `[ANOMALY] sensor=... raw=... state=...
  modified_z=... flatline_ms=...`, exposes `anomaly::getTelemetry()`, and
  fires `SENSOR_ANOMALY`/`SENSOR_FLATLINE`/`SENSOR_RECOVERED`/
  `SENSOR_STALE`/`SENSOR_INVALID` events via `anomaly::setEventCallback()`.
- `src/config.h`: a full `ANOMALY_*`/`SENSOR_SAMPLE_INTERVAL_MS` constant
  block (calibration count, MAD floor, Z threshold, flatline EPS, STUCK_N,
  calibration variance envelope + retry bound, consecutive/recovery
  debounce counts, staleness timeout) — see
  [parameters.md](parameters.md) and [decisions.md](decisions.md) for
  every value's derivation.
- `firmware/PredictiveMesh/test/test_anomaly_core.cpp` (new) — 50/50
  checks, covering all 14 required scenarios; two real bugs found and
  fixed while writing it (an off-by-one in the flatline tests, and an
  outlier magnitude that tripped the calibration's own variance gate
  before reaching median/MAD) — see [testing.md](testing.md).
- `main.cpp` updated: `anomaly::init()`/`setEventCallback()` wired in
  `setup()`; `anomaly::tick()` added to `loop()`'s per-iteration calls;
  sensor sampling added at `SENSOR_SAMPLE_INTERVAL_MS`.
- Several real, documented design calls (not silently made): a single
  discrete `SensorState` with FLATLINE taking priority over ANOMALY when
  both would fire (rather than two independent booleans); a debounce
  layered onto the guide's instant-flag MAD-Z design specifically for
  state transitions, not raw evidence; the calibration safety gate
  deliberately using ordinary variance (not MAD) for a genuinely different
  question than the steady-state detector asks; and an explicit,
  test-backed guarantee that sensor health never influences routing. See
  `docs/decisions.md` for all of these in full.
- **Flagged, not fabricated:** this phase's task spec referenced an
  external "GUI telemetry contract" (specific message types, GUI panels)
  that does not exist anywhere in this repository. Rather than invent one,
  this is documented explicitly in
  [known-issues.md](known-issues.md#gui-telemetry-contract--referenced-by-the-phase-3-task-spec-not-found-anywhere-in-this-repository)
  as an open question for the user to resolve. The underlying telemetry
  *data* Part 7 asked for is fully implemented and accessible locally
  (`anomaly::getTelemetry()`), ready to be serialized into a real contract
  once one is provided.

### What was explicitly NOT built (by design)
OLED wiring on Node C (would require a new external display library
dependency, not yet added — deferred, same reasoning as Phase 0's original
OLED deferral), any routing/predictor modification based on sensor health
(explicitly forbidden this phase, and a real regression test proves the
separation holds), a GUI wire-format implementation (no real contract
exists to implement against — see above), ACK/retry, duplicate filtering,
UCB1, the final telemetry system, final demo orchestration.

### Validation performed
- `firmware/PredictiveMesh/test/test_anomaly_core.cpp`: real, host-compiled
  (g++ 15.2.0 / MinGW-W64), actually executed — 50/50 checks passed,
  covering all 14 required scenarios.
- Full existing suite re-run alongside it to confirm no regressions:
  `test_routing_core` 21/21, `test_predictor_core` 31/31 — **102/102
  total, all three host suites.**
- **Real `arduino-cli compile` performed** against the full Phase 0+1+2+3
  sketch, `esp32:esp32` core 3.3.11 — clean on the first attempt, 0
  errors, 0 warnings (`--warnings all`). First real use of
  `analogRead()`/`analogReadResolution()`/`pinMode()` in this project. See
  [testing.md](testing.md).
- No hardware-dependent validation — see
  [known-issues.md](known-issues.md#phase-3-anomaly-engine--not-yet-run-on-hardware).

### Git
No commits were made this phase. Working tree left uncommitted for the
user to review.

### Next phase (not started, awaiting explicit go-ahead)
Per implementation-guide.html §06 (Hours 17-23, "required, not stretch"),
the next phase would be the reliability layer: hop-by-hop ACK on every
forwarded frame, bounded retransmit on a missing ACK, a sequence-number
duplicate filter, and Packet Recovery Ratio logging (§07) — also the
natural point to finally wire `predictor::onSendResult()` to a real
unicast delivery signal (see Phase 2's PDR measurement-boundary decision).
The GUI telemetry contract question above should be resolved before any
phase claims wire-format compatibility with a GUI.

## Phase 4 — Reliable unicast delivery (hop-by-hop ACK + bounded retry + duplicate filter + forwarding)
**Date:** 2026-08-17
**Status:** Complete, awaiting explicit go-ahead for whatever comes next.

### Objective
Implement the reliability layer per implementation-guide.html §5.4 and
this phase's own task spec: a deterministic packet identity, real unicast
ESP-NOW transmission, an explicit application-level ACK distinct from the
raw ESP-NOW send callback, bounded retry with deterministic timeout,
sequence-based duplicate filtering, minimum forwarding, and — for the
first time — live wiring of Phase 2's `predictor::onSendResult()` PDR path
to real per-hop delivery observations. A GUI implementation was added to
the repository immediately before this phase (see the GUI integration
audit delivered directly to the user, and
[decisions.md](decisions.md#gui-integration-audit-performed-before-phase-4--no-firmware-changes-made));
per explicit instruction, the GUI itself (`gui-main/`) was not touched.

### What was built
- `src/reliability/reliability_core.h/.cpp` (new) — the real algorithm,
  Arduino-free (mirrors `routing_core`/`predictor_core`/`anomaly_core`'s
  split): packet identity (`source`, `sequence`); a fixed-size pending-
  hop-transmission pool with `beginTx`/`cancelTx`/`onAckReceived`/
  `tickTimeouts`; a TTL-expiring, ring-buffer-replaced duplicate cache
  (`isDuplicateAndRecord`); and deterministic statistics counters with
  explicit attempt-vs-packet-series granularity (Part 9).
- `src/reliability/reliability.h/.cpp` (rewritten from the Phase 0 stub) —
  the Arduino-facing adapter: real unicast `MSG_DATA`/`MSG_ACK`
  construction/parsing, `transport::send()` to a resolved peer MAC,
  hop-ACK-before-duplicate-check receive handling, forwarding via the
  exact Phase 1/2 `routing::selectNextHop()` decision with loop guards, and
  `PACKET_TX`/`PACKET_ACK`/`PACKET_RETRY`/`PACKET_DELIVERED`/`PACKET_DROP`/
  `DUPLICATE_DROPPED`/`PACKET_RECEIVED` events via
  `reliability::setEventCallback()`. The Phase 0 stub
  `onSendResult(NodeId, bool)` was removed (not repurposed) — see
  decisions.md.
- `src/config.h`: a full `RELIABILITY_*` constant block (max retries, ACK
  timeout, pending-pool size, duplicate-cache size/TTL) — see
  [parameters.md](parameters.md) and [decisions.md](decisions.md) for
  every value's derivation.
- `firmware/PredictiveMesh/test/test_reliability_core.cpp` (new) — 88/88
  checks across 18 test functions, including a direct test of Part 9's
  worked example (1 packet + 2 retries + success) and a concurrent-
  entries-resolve-independently scenario.
- `main.cpp` updated: `reliability::onPacketReceived()` wired into
  `onTransportRx()` alongside routing/predictor; `reliability::tick()`
  added to `loop()`; `onReliabilityEvent()` logger added; banners → "Phase
  4 firmware".
- `core/packet.h`/`core/message_types.h` comments updated — `sequence` and
  `MSG_ACK` are now real, not "future"/"not yet implemented".
- Several real, documented design calls: `beginTx()` reserves a tracking
  slot *before* the real radio send (with `cancelTx()` for synchronous
  failures), so no frame is ever launched untracked; PDR is fed per
  individual attempt, never from the raw ESP-NOW send callback; PDR
  represents per-hop delivery only, never end-to-end; loop prevention
  relies on routing correctness + a `nextHop != prevHop` guard + the
  duplicate filter, not a new TTL field (revisiting, and resolving, Phase
  1's original open question); `reliability::send()` has no automatic
  live caller — no application data source was invented. See
  `docs/decisions.md` for all of these in full.

### What was explicitly NOT built (by design)
Packet Recovery Ratio (§07's alternate-route-recovery metric — the task
spec's own Part 11 statistics list didn't ask for it, and it requires
cross-referencing forwarding decisions end-to-end in a way this phase's
node-local pure-core testing model doesn't cover); any automatic caller of
`reliability::send()` (no real application data source was invented — see
decisions.md); UCB1; the final telemetry system; final demo orchestration;
any GUI changes (explicitly forbidden this phase); any change to
`gui-main/`.

### Validation performed
- `firmware/PredictiveMesh/test/test_reliability_core.cpp`: real,
  host-compiled (g++ 15.2.0 / MinGW-W64), actually executed — 88/88
  checks passed.
- Full existing suite re-run alongside it to confirm no regressions:
  `test_routing_core` 21/21, `test_predictor_core` 31/31,
  `test_anomaly_core` 50/50 — **190/190 total, all four host suites.**
- **Real `arduino-cli compile` performed** against the full Phase
  0+1+2+3+4 sketch, `esp32:esp32` core 3.3.11 — clean on the first
  attempt, 0 errors, 0 warnings (`--warnings all`). First real use of
  `esp_now_send()` for genuine unicast traffic in this project. See
  [testing.md](testing.md).
- No hardware-dependent validation, and no live end-to-end exercise of the
  forwarding/ACK/retry path even in principle — no real application
  traffic exists yet to generate it (see
  [decisions.md](decisions.md#reliabilitysend-has-no-live-automatic-caller-in-phase-4--no-application-data-source-was-invented)).

### Git
No commits were made this phase. Working tree left uncommitted for the
user to review.

### Next phase (not started, awaiting explicit go-ahead)
Not yet specified by the user. Candidates named in
implementation-guide.html's own roadmap beyond Hours 17-23 include UCB1
(explicitly deferred every phase so far), the final telemetry/reporting
system (would be the natural point to define what real `MSG_DATA`
application traffic flows and wire `reliability::send()`/`getStatistics()`
into the now-real GUI telemetry contract), and OLED wiring (deferred since
Phase 0). Do not start any of these without explicit instruction.

## Phase 5 — UCB1 adaptive routing (stretch, optional, compile-time-gated, disabled by default)
**Date:** 2026-08-17
**Status:** Complete, awaiting explicit go-ahead for whatever comes next.

### Objective
Implement UCB1 multi-armed-bandit next-hop ranking per
implementation-guide.html §06's "[stretch, optional]" label and this
phase's own detailed task spec: an additional adaptive ranking layer
(never a replacement for distance-vector/link-health/priority routing),
gated behind a compile-time flag defaulting to disabled, ranking only
among candidates the existing routing layer already considers valid, fed
by real Phase 4 reliability observations (never fabricated rewards).

### What was built
- `src/ucb1/ucb1_core.h/.cpp` (new) — the real algorithm, Arduino-free
  (mirrors the other three `*_core` splits): a fixed-size
  `[destination][nextHop]` bandit-statistics table, `recordOutcome()`,
  and `selectNextHop()` implementing the standard UCB1 formula
  (`meanReward + sqrt(2)*sqrt(ln(N)/n)`) with explicit zero-observation
  handling, health-tiering, loop-guard exclusion, and deterministic
  lowest-NodeId tie-breaking. Always compiled regardless of `ENABLE_UCB1`.
- `src/ucb1/ucb1.h/.cpp` (new) — the Arduino-facing adapter, owning the one
  `Ucb1State` instance. `ucb1.cpp`'s entire body is wrapped in
  `#if ENABLE_UCB1`, compiling to an empty translation unit when disabled.
- `src/routing/routing_core.h/.cpp`: one new, purely additive function
  (`enumerateCandidates()`) reusing `selectNextHop()`'s own validity rules
  exactly, plus an `excludeNextHop` parameter for the loop-prevention
  guard. Zero lines of any existing function changed.
- `src/routing/routing.cpp`: `getNextHop()`/`selectNextHop(pkt)` refactored
  into thin wrappers around a new internal `getNextHopInternal()` — with
  `ENABLE_UCB1=0`, this is Phase 4's exact original code path (a pure
  extraction, not a behavior change); with `ENABLE_UCB1=1`, NORMAL-traffic
  decisions are additionally ranked by UCB1 among routing's own
  already-validated candidates, with an unconditional final loop-guard
  check regardless of which path produced the answer.
- `src/reliability/reliability.cpp`: three `#if ENABLE_UCB1`-gated calls to
  `ucb1::onRouteOutcome()`, at exactly the points that already represent a
  hop-transmission series's FINAL outcome (never per retry) —
  `handleAck()`'s match, `tick()`'s `FAILED` branch, and `transmitHop()`'s
  synchronous-rejection path.
- `src/reliability/reliability_core.h/.cpp`: `AckResult` gained a `slot`
  field so the adapter can recover the original packet's destination for
  the UCB1 reward call without `reliability_core` itself needing to store
  one.
- `src/config.h`: `ENABLE_UCB1` (default `0`) and `UCB1_EXPLORATION_C`
  (`sqrt(2)`, the standard textbook value — the guide specifies neither).
- `firmware/PredictiveMesh/test/test_ucb1_core.cpp` (new) — 26/26 checks
  across 16 test functions, covering Part 10's scenarios 2-7 and 9-19.
  `firmware/PredictiveMesh/test/test_routing_core.cpp` gained 2 new tests
  (unconditional, not gated behind `ENABLE_UCB1`) for `enumerateCandidates()`
  itself.
- Several real, documented design calls: resolving implementation-guide.html's
  own "as an alternative to distance-vector" framing as "ranks only among
  already-valid candidates" per this phase's own explicit, current
  instructions (not a silent redesign); the exact packet-series-not-retry
  reward-trial definition, reusing Phase 4's own established boundary; two
  independent, redundant loop-prevention layers for a "must never" safety
  property; deliberately ignoring hop count in the ranking formula so
  learned evidence can actually override the static heuristic; no decay
  (fixed counters, per Part 7's explicit permission). See
  `docs/decisions.md` for all of these in full.

### What was explicitly NOT built (by design)
Any change to `routing_core::selectNextHop()`'s own logic (zero lines
touched); a hop-count/TTL field on `MeshPacket` (Part 8 explicitly asked
to preserve that prior decision); decay/windowing of bandit statistics
(not required by the guide); randomized exploration (Part 9 explicitly
forbids it — UCB1's own exploration term is the only exploration
mechanism); any GUI changes (explicitly forbidden this phase); any change
to `gui-main/`; any automatic caller of `reliability::send()` (still not
built — inherited unchanged from Phase 4, so UCB1 has no live traffic to
learn from yet either).

### Validation performed
- `firmware/PredictiveMesh/test/test_ucb1_core.cpp`: real, host-compiled
  (g++ 15.2.0 / MinGW-W64), actually executed — 26/26 checks passed.
- Full existing suite re-run alongside it to confirm no regressions:
  `test_routing_core` 28/28 (18 Phase 1 + 2 Phase 2 + 2 new Phase 5),
  `test_predictor_core` 31/31, `test_anomaly_core` 50/50,
  `test_reliability_core` 88/88 — **223/223 total, all five host suites.**
- **Real `arduino-cli compile` performed for BOTH configurations** (Part
  11): `ENABLE_UCB1=0` (906,948 bytes flash / 47,664 bytes RAM) and
  `ENABLE_UCB1=1` (909,272 bytes flash / 48,064 bytes RAM) — both clean on
  the first attempt, 0 errors, 0 warnings (`--warnings all`). The
  repository's committed state was restored to `ENABLE_UCB1=0` and
  re-verified identical to the first compile's byte counts, confirming an
  exact restore. See [testing.md](testing.md).
- No hardware-dependent validation, and (inherited from Phase 4) no live
  end-to-end exercise of the pipeline — no real application traffic exists
  yet to generate the delivery outcomes UCB1 would learn from. See
  [known-issues.md](known-issues.md).

### Git
No commits were made this phase. Working tree left uncommitted for the
user to review. `gui-main/` was not touched.

### Next phase (not started, awaiting explicit go-ahead)
Not yet specified by the user. `docs/phase-log.md`'s Phase 4 entry's own
candidate list still applies unchanged: the final telemetry/reporting
system (the natural point to both define what real `MSG_DATA` application
traffic flows — resolving the shared Phase 4/5 "no live caller" gap — and
wire `reliability::getStatistics()`/UCB1 diagnostics into the now-real GUI
telemetry contract), and OLED wiring (deferred since Phase 0). Do not
start any of these without explicit instruction.
