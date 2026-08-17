# Firmware <-> GUI Compatibility Matrix (Phase 6)

Audited against `gui-main/gui-main/docs/gui-telemetry-contract.md` ("Firmware
<-> GUI Telemetry Contract v1", `FROZEN`, revision `1.0`) and against the
real, unmodified GUI parser (`mesh-command-console.html`'s `applyTelemetry()`).
Firmware producers live in `src/telemetry/telemetry_core.h/.cpp` (pure JSON
construction) and `src/telemetry/telemetry.h/.cpp` (the Arduino adapter that
reads real state from routing/predictor/anomaly/reliability and calls the
builders). See [decisions.md](decisions.md) for the reasoning behind every
enum mapping and every field marked "derived" below, and
[testing.md](testing.md) for how this was actually validated (94 host-test
checks + a real run of the JSON output through the GUI's own verbatim JS —
not just static review).

**Every message type is now implemented and emitted for real.** Nothing in
this table describes a stub.

## Envelope

| Field | Firmware source | Status |
|---|---|---|
| `protocolVersion` | literal `"mesh-json/v1"` | real |
| `type` | one literal per builder function | real |
| `nodeId` | `nodeName(THIS_NODE_ID)` (`core/node_id.h`, unchanged from Phase 0) | real |
| `bootId` | `esp_random()`-derived, generated once in `telemetry::init()` | real (see decisions.md — not a monotonic counter, no persistent storage exists) |
| `seq` | `telemetry.cpp`'s own `g_seq`, incremented once per envelope, reset to 0 each boot | real |
| `timestampMs` | `millis()` | real |

## `0x01 HELLO`

| Field | Source | Status |
|---|---|---|
| `nodeName` | `thisNode().name` | real |
| `role` | `thisNode().role`, mapped via `roleStr()` | real |
| `mac` | `WiFi.macAddress()` | real, populated in the FIRST HELLO — fixed Phase 7.1 (see decisions.md's Finding 4 entry: `WiFi.mode(WIFI_STA)`/`WiFi.macAddress()` now run before `telemetry::init()`, which the real MAC doesn't structurally require `transport::begin()`'s later, more failure-prone calls to have completed first) |
| `firmwareVersion` | new `FIRMWARE_VERSION` constant (`config.h`) | real, but newly introduced this phase — no prior versioning scheme existed (known-issues.md flagged this as unsourced pre-Phase-6) |
| `config.heartbeatIntervalMs` | `TELEMETRY_HEARTBEAT_INTERVAL_MS` | real |
| `config.offlineTimeoutMs` | new `TELEMETRY_OFFLINE_TIMEOUT_MS` constant | real, newly introduced (3x heartbeat interval, matching this project's established staleness-tolerance convention) |
| `config.routeTimeoutMs` | `ROUTING_ENTRY_TIMEOUT_MS` (existing) | real |
| `config.tLow` / `tHigh` | `PREDICTOR_HYSTERESIS_T_LOW` / `T_HIGH` (existing) | real |
| `config.ewmaAlpha` | `PREDICTOR_RSSI_EWMA_ALPHA` | real, but this project has **two** real EWMA alphas (RSSI 0.3, PDR 0.1) and the contract's single field can only report one — RSSI's is reported as the more prominent of the two (see decisions.md) |
| `config.telemetryRatesHz` | computed from `TELEMETRY_LINK/PREDICTION/STATISTICS_INTERVAL_MS` | real |

**Emitted once at boot**, before `transport::begin()`'s channel-set/
`esp_now_init()` calls — see decisions.md for why (so a real
`reportError()` channel exists before anything that could genuinely fail).
`WiFi.mode(WIFI_STA)` and the real MAC read now happen just before this,
also ahead of anything failure-prone (Phase 7.1 fix).

## `0x02 HEARTBEAT`

| Field | Source | Status |
|---|---|---|
| `uptimeMs` | `millis()` | real |

Every `TELEMETRY_HEARTBEAT_INTERVAL_MS` (1000ms), matching the contract exactly.

## `0x03 NODE_STATUS`

| Field | Source | Status |
|---|---|---|
| `status` | always `"ONLINE"` | real, by construction — a node can only self-report while it's running to emit anything at all; OFFLINE/STALE are concepts the GUI derives about *other* nodes from heartbeat timeout (`refreshFirmwareStaleness()`), not something a node says about itself |
| `nodeName` / `role` / `uptimeMs` / `firmwareVersion` | same sources as HELLO | real |
| `reason` | omitted | no firmware condition currently sets a non-ONLINE self-status, so there's nothing to explain yet |

## `0x04 LINK_UPDATE`

| Field | Source | Status |
|---|---|---|
| `from` / `to` | `thisNode().name` / `nodeName(neighbor)` | real |
| `rssiDbm` | `predictor::linkState(neighbor).latestRssi` (new accessor) | real |
| `rssiEwmaDbm` | `.ewmaRssi` | real |
| `rssiSlopeDbPerSec` | `.slope` | **misleadingly named in the contract for this firmware** — `predictor_core`'s `slope` is a least-squares fit against *sample index* (0, 1, 2, ...), not elapsed wall-clock time (`predictor_core.h`: "dBm per sample step" — a deliberate simplification, since RSSI samples arrive on the fixed `ROUTING_HELLO_INTERVAL_MS` beacon cadence, not a documented-as-fixed real-time interval). Reported under the contract's field name since it's the closest and only real slope figure available, documented here rather than silently passed off as a true per-second rate — same pattern as `endToEndLatencyMs` below |
| `pdr` / `pdrEwma` | `.pdrEwma` (both fields report the same EWMA — predictor_core has no separate raw/smoothed PDR distinction, only the EWMA itself) | real, with the noted duplication |
| `stalenessMs` | `now - .lastUpdateMs` | real |
| `linkScore` | `.linkScore` | real |
| `state` | derived via `classifyLink()` from `everObserved`/`stale`/`health`/`belowCount`/`aboveCount` | **derived**, not a 1:1 stored field — see decisions.md's Part-K entry for the full DEGRADING/RECOVERING derivation rationale |

Emitted every `TELEMETRY_LINK_INTERVAL_MS` (250ms) for each direct neighbor
in `neighborsOf(THIS_NODE_ID)` — matches the contract's stated frequency
exactly, and matches its own suggested `telemetryRatesHz.link=4` example.

**GUI display caveat (observed, not a firmware defect):** the console's
"Authoritative link health" panel only ever displays whichever link is
keyed `A>B` or `B>A` (hardcoded in `renderFirmware()`). Every other real
link (A-C, A-S, B-S, C-D, D-S) is received, parsed, and stored correctly in
`state.firmware.links`, just not shown on that one panel. Confirmed by
reading the GUI's own source, not something firmware can or should work
around.

## `0x05 ROUTE_UPDATE`

| Field | Source | Status |
|---|---|---|
| `destination` | `nodeName(evt.destination)` | real |
| `active.hops` | full reconstructed path `[self, ..., destination]` when legitimately determinable, else honest minimal `[self, nextHop]` fallback | **real, fixed Phase 7.1** — see below |
| `active.hopCount` | derived as `hops.length - 1` at serialization time (`telemetry_core::derivedHopCount()`) — never a separately-trusted value | real, and now structurally guaranteed consistent with `hops` (see below) |
| `active.score` | `predictor::linkScore(nextHop)` | **derived** — the next-hop's own link score, used as a proxy for "route score" since `routing_core` has no multi-hop composite score concept |
| `active.state` | always `"ACTIVE"` | real |
| `candidates[]` | `routing::getCandidates()` (new accessor, wraps the existing Phase-5 `routing_core::enumerateCandidates()`) | real |
| `candidates[].hops/score/state` | same reconstruction as `active`'s, `state` is `"ACTIVE"` for the chosen next hop, `"BACKUP"` for the rest | real/derived, same caveats |
| `trafficClass` | `evt.priority ? "PRIORITY" : "NORMAL"` | real |
| `reason` | `routeReasonStr()`: `PRIORITY_OVERRIDE` / `ROUTE_EXPIRED` / `LINK_DEGRADATION` / `ROUTE_RECOVERY` / `UNKNOWN` | **real, extended Phase 7.1** — `LINK_DEGRADATION`/`ROUTE_RECOVERY` are now derived from a real hop-count comparison at the moment of a health-gated reroute (see decisions.md's Finding 6 entry); `STALE_NEIGHBOR`/`MANUAL` remain never-produced — no derivable firmware signal exists for either |

**`hops` — fixed Phase 7.1 (was: honest, documented 2-element-only
limitation).** `routing_core::reconstructPath()` (new, pure, read-only) now
searches the compiled-in static adjacency graph for the UNIQUE loop-free
path matching `routing_core`'s own already-computed real hop count, and
returns it only when that uniqueness is provable — never a guess, never
fabricated intermediate nodes. Verified (7 new `test_routing_core.cpp`
checks) to correctly reconstruct both demo-relevant routes
(`A→B→S`, `A→C→D→S`) and the direct priority path (`A→S`), while correctly
refusing to guess for a real, separately-verified graph-level ambiguity
that exists for some other (self, destination) pairs in this topology (see
decisions.md). `hopCount` is no longer capable of disagreeing with
`hops.length - 1` — it's derived from `hops` at serialization time, not
stored/passed independently, closing off the inconsistency class entirely
(not just this one instance of it).

**GUI topology-diagram-animation gap — RESOLVED for both demo routes, verified
against the GUI's real code, not assumed.** `mesh-command-console.html`'s
`routeKey(hops){return hops.join('')}` (read directly from `gui-main/`,
never modified) turns a correctly-reconstructed `["A","B","S"]`/
`["A","C","D","S"]`/`["A","S"]` into exactly `"ABS"`/`"ACDS"`/`"AS"` — the
three literal strings the GUI's topology-diagram animation already
recognized (hardcoded on the GUI side, unchanged). The Phase 6 gap
(`decisions.md`'s "GUI's topology-animation route-key matching doesn't
recognize a real 2-hop ROUTE_UPDATE" entry, kept as the historical record
of why it existed) is resolved not by touching the GUI, but by firmware
finally reporting the real full path it was always structurally capable of
determining for this fixed topology. Not re-run through the full Phase 6
Node.js GUI-parser harness this pass (time-scoped decision — the fix was
verified by reading `routeKey()`'s real, unchanged one-line source
directly rather than rebuilding the disposable harness); re-running that
harness would be a reasonable follow-up before physical demo rehearsal.

Emitted on a real `ROUTE_CHANGED` table mutation, **or** (fixed Phase 7.1
— see decisions.md's Finding 6 entry) a `ROUTE_SELECTED` decision whose
winning next hop genuinely differs from what was last reported for that
destination — never on the many `ROUTE_SELECTED` calls (one per
`reliability::send()`/forward) where the choice is unchanged, so this
still isn't "every decision query." This widening is what makes a
health-driven reroute (the demo's "B degrades → A reroutes via C-D"
scenario) visible at all — the table itself doesn't mutate when only link
health changes, so `ROUTE_CHANGED` alone would never fire for it (a real,
now-fixed gap, not by design). Skipped entirely when there's no valid next
hop (`ROUTE_INVALIDATED`) — firmware never claims an active route exists
when it doesn't.

## `0x06 PREDICTION`

| Field | Source | Status |
|---|---|---|
| all RSSI/PDR/score fields | identical source to `LINK_UPDATE`, same neighbor loop | real |
| `predictionState` | `classifyLink()`, `predictionStateStr()` (STABLE/TIMEOUT vocabulary) | derived, same evidence as `LINK_UPDATE.state` |
| `hysteresisState` | direct comparison of `linkScore` against `T_LOW`/`T_HIGH` | real, direct threshold comparison |

Emitted every `TELEMETRY_PREDICTION_INTERVAL_MS` (250ms), matching the
contract exactly.

## `0x07 SENSOR_STATUS`

| Field | Source | Status |
|---|---|---|
| `sensorId` / `sensorType` | literals `"pot"`/`"potentiometer"`, `"ldr"`/`"photoresistor"` | real, newly assigned identifiers (no prior naming existed) |
| `value` / `rawValue` | `anomaly_core::SensorTelemetry.raw_value` (both fields report the same real reading — no separate "cleaned" value exists) | real, noted duplication |
| `healthState` | `sensorHealthStr()`: WARMUP->SUSPECT, INVALID->OUT_OF_RANGE, others map 1:1 | derived — 2 of 6 values are a real judgment call, documented in decisions.md |
| `durationMs` | `flatline_duration_ms`, only included when `flatline_active` | real, contract's "required for FLATLINE" rule honored |
| `baseline` | `.median` | real (closest available concept to "baseline") |
| `mad` / `zScore` / `threshold` | `.mad` / `.modified_z` / `.anomaly_threshold` | real |

Emitted every `TELEMETRY_SENSOR_INTERVAL_MS` (1000ms) for both sensors on
every node — matches the contract exactly. GUI only visibly reacts to
Node C's (the anomaly-flag panel is hardcoded to `nodeId==='C'`), but
every node's real sensor data is sent and stored regardless.

## `0x08 EVENT`

| Firmware event source | Mapped `eventType` | Status |
|---|---|---|
| `routing::ROUTE_SELECTED` (priority only) | `PRIORITY_ROUTE` | real |
| `routing::ROUTE_CHANGED`, plus a genuinely-changed `ROUTE_SELECTED` (fixed Phase 7.1) | `ROUTE_CHANGE`, with real old-vs-new `oldHops`/`newHops` (now full reconstructed multi-hop paths, not single-letter abbreviations)/`oldScore`/`newScore`, and a real derived `reason` (`LINK_DEGRADATION`/`ROUTE_RECOVERY`/`UNKNOWN`) (adapter-local cache, see decisions.md) | real |
| `routing::ROUTE_INVALIDATED` | `ROUTE_CHANGE` (`reason:"ROUTE_EXPIRED"`, empty `newHops`) | real |
| `predictor::LINK_DEGRADING` | `LINK_DEGRADING` | real |
| `predictor::LINK_UNHEALTHY` | `LINK_FAILURE` | real |
| `predictor::LINK_RECOVERED` / `LINK_SCORE_UPDATED` | *(none)* | no contract enum value for "recovered"; already visible via `LINK_UPDATE`/`PREDICTION`'s own `state` returning to HEALTHY — see decisions.md |
| `anomaly::SENSOR_ANOMALY` | `SENSOR_ANOMALY` | real |
| `anomaly::SENSOR_FLATLINE` / `SENSOR_STALE` / `SENSOR_INVALID` | `SENSOR_FAILURE` (with `details.reason` distinguishing which) | real, coarsened — contract has one failure enum value for three distinct firmware conditions |
| `anomaly::SENSOR_RECOVERED` | *(none)* | same reasoning as `LINK_RECOVERED` |
| `reliability::PACKET_RETRY` | `PACKET_RETRY` | real (dormant until flashed to hardware — the code-level "no live `MSG_DATA` traffic" gap is resolved as of Phase 7's `src/apptraffic/`, see decisions.md; will fire on a real retried hop-transmission once running on real boards) |
| `reliability::PACKET_DROP` | `PACKET_DROP` | real (same — dormant only until flashed, not blocked on any remaining code gap) |
| `reliability::PACKET_TX/ACK/DELIVERED/RECEIVED/DUPLICATE_DROPPED` | *(none)* | not anomalies — already covered in aggregate by the periodic `STATISTICS` message, matching the contract's own EVENT-vs-STATISTICS division of concerns |
| *(none in firmware)* | `NODE_JOIN` / `NODE_LEAVE` | **no firmware source exists** — routing tracks neighbor liveness but fires no discrete "first contact" / "now gone" event distinct from ordinary beacon traffic; not fabricated, flagged as a real gap |

## `0x09 STATISTICS`

| Field | Source | Status |
|---|---|---|
| `windowMs` | `TELEMETRY_STATISTICS_INTERVAL_MS` (reporting cadence, not a reset window — counters stay cumulative per the contract's own note) | real |
| `pdr` | `packetsDelivered / packetsSent` (1.0 neutral default when `packetsSent==0`) | derived from real counters |
| `packetsTransmitted` | `Statistics.packetsSent` | real |
| `packetsAcknowledged` | `Statistics.acknowledgements` | real |
| `packetsDropped` | `Statistics.packetsFailed` | real |
| `retryCount` | `Statistics.retries` | real |
| `duplicateCount` | `Statistics.duplicatesDropped` | real |
| `endToEndLatencyMs` | `Statistics.lastLatencyMs` | **misleadingly named in the contract for this firmware** — this is the last *per-hop* ACK latency, not a true multi-hop end-to-end measurement (no such mechanism exists); reported under the contract's field name since it's the closest and only real latency figure available, documented here rather than silently passed off as something it isn't |

Emitted every `TELEMETRY_STATISTICS_INTERVAL_MS` (1000ms) — matches
exactly. All counters still read `0`/`1.0` on a fresh boot, and will until
flashed to real hardware — but the underlying "no live `MSG_DATA` traffic"
gap this note originally described is resolved at the code level as of
Phase 7 (`src/apptraffic/`, `NODE_A -> NODE_S` — see decisions.md); once
running on real boards these counters will move.

## `0x0A ERROR`

| Real call site | `code` | Status |
|---|---|---|
| `transport::begin()` failure (`main.cpp`) | `TRANSPORT_INIT_FAILED` | real, wired, `recoverable:false` (this path halts the node) |

No other real, non-fabricated firmware fault condition was found to wire
this phase — see decisions.md. `telemetry::reportError()` exists as a
general-purpose entry point for a future real fault source to use.

## UCB1 — deliberately not serialized

The frozen contract has **no dedicated message type** for UCB1 bandit
diagnostics. Per Part 13's explicit instruction ("do not invent a GUI
protocol"), none was added. UCB1's actual effect (when `ENABLE_UCB1=1`) is
already transparently visible through `ROUTE_UPDATE`'s `active` field,
since `routing::getNextHop()` already incorporates UCB1's ranking before
telemetry ever reads the decision — no separate wiring needed or added.
