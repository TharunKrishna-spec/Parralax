# Firmware <-> GUI Compatibility Matrix (Phase 6)

> **Note (2026-08-18):** `gui-main/gui-main/mesh-command-console.html` (the
> file this matrix was originally audited against) was replaced with
> `mesh-command-console (1).html`, then later renamed back to the plain
> `mesh-command-console.html` (its current, real filename on disk — the
> `(1)` suffix is gone). The field-by-field mappings below remain
> accurate — the current file's core telemetry parser
> (`applyTelemetryCore()`) is character-for-character identical to the
> original `applyTelemetry()` this matrix cites, aside from the one
> authorized `case'PACKET':break;` fix (see decisions.md). See
> [full-system-audit.md](full-system-audit.md)'s Phase 12 for the fresh
> audit of what the replacement GUI adds on top (and one real gap: its own
> manual documents a `PACKET` message type and several `EVENT` names
> firmware doesn't implement).
>
> **Note (2026-08-18, presentation-focused pass):** a new, additive
> "judge summary" panel (`#judgeSummary`) was added on top of this same
> contract — real per-node `role`/`status` (from `HELLO`/`NODE_STATUS`,
> already covered below), real independent POT/LDR `healthState` (from
> two separately-tagged `SENSOR_STATUS` messages, already covered below),
> and a persistent self-heal/link-degraded status line driven by
> `ROUTE_UPDATE`/`EVENT` (`LINK_DEGRADING`/`NODE_SILENT`/
> `TIMEOUT_FALLBACK`'s real `source` field). No new message type, no new
> field, no firmware change — see
> [decisions.md](decisions.md#presentation-focused-gui-pass--judge-summary-panel-added-to-mesh-command-consolehtml)
> and [testing.md](testing.md#presentation-focused-gui-pass--judge-summary-panel-2026-08-18).

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
| `reliability::PACKET_DROP` | `PACKET_DROP` | real |
| `reliability::PACKET_DROP` with real `attemptCount > 1` (retry-exhaustion, never the pool-full/sync-rejection cases, which always report 0/1) | `TIMEOUT_FALLBACK` (2026-08-18, new) | real — see the EVENT vocabulary table below |
| `reliability::PACKET_DELIVERED` with real `attemptCount > 1` | `PACKET_RECOVERED` (2026-08-18, new) | real |
| `reliability::DUPLICATE_DROPPED` | `DUPLICATE_SUPPRESSED` (2026-08-18, new) | real — previously fired no discrete EVENT at all (only the aggregate `STATISTICS.duplicateCount`); now also a real, per-occurrence EVENT |
| `reliability::PACKET_TX/ACK/RECEIVED` | *(none)* | not anomalies — already covered in aggregate by the periodic `STATISTICS` message, matching the contract's own EVENT-vs-STATISTICS division of concerns. (`PACKET_TX`/`PACKET_RECEIVED`/etc. now also drive the new `0x0B PACKET` message below — a distinct, non-EVENT channel.) |
| `routing::NEIGHBOR_SILENT` (2026-08-18, new — routing.cpp's own existing `ROUTING_ENTRY_TIMEOUT_MS` sweep, no second timeout system) | `NODE_SILENT` (2026-08-18, new) | real |
| `predictor::LINK_DEGRADING`, only when `routing::getCandidates()` shows a real, different viable candidate not via the degrading neighbor | `REROUTE_PROPOSED` (2026-08-18, new) | real — never fired merely because a score changed; see decisions.md |
| *(none in firmware)* | `NODE_JOIN` / `NODE_LEAVE` | **no firmware source exists** — routing tracks neighbor liveness but fires no discrete "first contact" event distinct from ordinary beacon traffic; not fabricated, flagged as a real gap |
| `suppression::broadcastPriority()` (real successful `transport::send()` at origination) | `PRIORITY_BROADCAST` (2026-08-18, priority-broadcast milestone, new) | real — GUI-verified this pass, see below |
| `suppression::onPacketReceived()`, genuine relay rebroadcast overheard (`prevHop != source`), whether or not this entry is already decided | `PRIORITY_OVERHEARD` (2026-08-18, new) | real — GUI-verified this pass |
| `suppression::tick()`, this node's own backoff expired below `SUPPRESSION_THRESHOLD` | `PRIORITY_FORWARD` (2026-08-18, new) | real — GUI-verified this pass |
| `suppression::tick()`, this node's own backoff expired at/above `SUPPRESSION_THRESHOLD` | `PRIORITY_SUPPRESSED` (2026-08-18, new) | real — GUI-verified this pass; **never means packet loss**, see below |
| `suppression::onPacketReceived()`, first genuine reception at the real destination | `PRIORITY_DELIVERED` (2026-08-18, new) | real — GUI-verified this pass |

**`PRIORITY_ROUTE` is now dead code in practice (2026-08-18):** the row
above ("`routing::ROUTE_SELECTED` (priority only)") describes real code
that still exists and still compiles, but nothing calls
`routing::getNextHop()`/`selectNextHop()` with `priority=true` anymore —
`apptraffic.cpp`'s priority branch now calls
`suppression::broadcastPriority()` instead of
`reliability::send(..., priority=true)`, and priority-broadcast packets
never reach `reliability::handleData()`'s forwarding path either (see
`docs/decisions.md`'s priority-broadcast entry). This EVENT type will not
fire from real firmware going forward. Not removed (still a real, honest
description of what the code does if it were ever called), just flagged
as unreachable in the current architecture — the GUI's own
`PRIORITY_OVERRIDE`-matching HUD branch (see the reconciliation table's
"not reconciled by name" row) was already dormant before this milestone
and remains so.

### Priority-broadcast milestone (2026-08-18) — verified in the live GUI, not just "the parser doesn't crash"

All five `PRIORITY_*` `eventType` values ride the pre-existing, generic
`0x08 EVENT` message — confirmed by reading the real, current
`telemetry.cpp::onSuppressionEvent()` before writing anything, not
assumed. **One real, easy-to-get-wrong schema fact, verified from the
actual code:** `EVENT.payload.source` is ALWAYS the priority packet's
*original* sender (e.g. `"A"`) on all five event types, regardless of
which physical node is reporting the event — `envelope.nodeId` (the
top-level `nodeId` field, not inside `payload`) is the *reporting* node
and is the field that must be used to attribute an OVERHEARD/FORWARD/
SUPPRESSED/DELIVERED event to the correct physical node. (`details.
currentNode`, present on OVERHEARD/FORWARD/SUPPRESSED, is a redundant
real confirmation of the same value.) Getting this backwards would have
silently misattributed every relay/suppression/delivery event to node A
in a live multi-node demo.

**Found genuinely missing, not just "parser accepts it":** before this
pass, the GUI had zero code referencing any `PRIORITY_BROADCAST`/
`_OVERHEARD`/`_FORWARD`/`_SUPPRESSED`/`_DELIVERED` string anywhere
(confirmed by `grep -o "PRIORITY_[A-Z]*"` against the live script before
any edit — the only match was the old `PRIORITY_OVERRIDE`). The generic
EVENT case already logged and timelined every one of the five safely (no
parser crash, no PARSE WARNING) — but that is not the same as the new
broadcast/overhear/suppress/deliver mechanism being legible as what it
actually is. **Fixed:** a new, additive, real-telemetry-only "Priority
broadcast · live flow" panel (`#priorityFlow`, hidden until a real
`PRIORITY_BROADCAST` arrives) plus a lightweight violet node-pulse
(`.priority-pulse`, reusing the pre-existing violet "priority traffic"
legend color) — see `docs/decisions.md` for the exact functions added
(`handlePriorityEvent`/`renderPriorityFlow`/`pulsePriorityNode`) and full
reasoning, including why this deliberately does NOT reuse the topology's
existing edge/route-highlight visual language (a flood is not a routed
path — highlighting specific edges would misrepresent it as one).
`PRIORITY_SUPPRESSED` renders with the SAME neutral/muted styling as
"stood down," never the red failure color used elsewhere for real
failures — a deliberate choice so it can never read as packet loss.
Verified against real firmware-generated JSON (the exact scenario from
this pass's audit request — `A BROADCAST -> B/C OVERHEARD -> D FORWARD ->
B/C SUPPRESSED -> S DELIVERED`, interleaved across 4 distinct real
`nodeId`s) run through the entire real, unmodified extracted `<script>`
block: 25/25 checks passed, zero PARSE WARNING entries across 13 real/
malformed/unknown-eventType/missing-field messages. See
`docs/testing.md`.

### EVENT vocabulary reconciliation (2026-08-18) — every value the replacement GUI's manual documents, cross-checked

| Event | Trigger (real condition) | Firmware call site | Telemetry payload | GUI effect | Test |
|---|---|---|---|---|---|
| `LINK_DEGRADING` | `predictor_core` HEALTHY-but-below-T_HIGH | `telemetry.cpp::onLinkEvent()` | `{"score":F}` | decision HUD "PREDICTED DEGRADATION", mission phase -> PREDICT | host (predictor_core, telemetry_core) + real GUI-parser harness |
| `LINK_FAILURE` | `predictor_core` HEALTHY->UNHEALTHY (score or staleness) | `telemetry.cpp::onLinkEvent()` | `{"score":F}` | logged; no dedicated HUD in the replacement GUI | host + harness |
| `REROUTE_PROPOSED` | `LINK_DEGRADING` fires AND `routing::getCandidates()` shows a real different viable candidate | `telemetry.cpp::onLinkEvent()` | `{degradingNeighbor,alternateNextHop,alternateHopCount,alternateScore}` | logged (no dedicated HUD wired in the replacement GUI's code — only in its manual's vocabulary list; see decisions.md) | host (routing_core candidates) + real harness (this pass) |
| `ROUTE_CHANGE` (this project's real name for the manual's `REROUTE_COMMITTED`) | a genuine health-driven or table-mutation route change | `telemetry.cpp::onRouteEvent()` | `{oldHops,newHops,reason,oldScore,newScore,leadTimeMs?}` | `setRoute()` -> topology animates the real hop array (any real path, not just `ABS`/`ACDS`) | host (route reason derivation) + real harness |
| `NODE_SILENT` | a direct neighbor crosses `ROUTING_ENTRY_TIMEOUT_MS` with no packet heard | `routing.cpp::tick()` -> `telemetry.cpp::onRouteEvent()` | `{neighbor,silentForMs}` | logged; GUI's `NODE_SILENT`/`TIMEOUT_FALLBACK` HUD branch fires (`state.network='FALLBACK'`) | host (routing_core `neighborLastSeenMs`) + real harness |
| `TIMEOUT_FALLBACK` | a hop-transmission's retries are genuinely exhausted (`reliability_core::tickTimeouts` FAILED, `attemptCount>1`) | `telemetry.cpp::onReliabilityEvent()` | `{sequence,neighbor,attemptCount}` | logged; same HUD branch as `NODE_SILENT` | host (reliability_core FAILED-branch tests) + real harness |
| `PACKET_RECOVERED` | a delivery series' ACK matched with real `attemptCount>1` | `telemetry.cpp::onReliabilityEvent()` | `{sequence,neighbor,attemptCount}` | logged; GUI's `PACKET_RECOVERED` branch sets `state.network='RECOVERING'` | host (`AckResult.attemptCount`, new) + real harness |
| `DUPLICATE_SUPPRESSED` | `reliability_core::isDuplicateAndRecord()` returns true | `telemetry.cpp::onReliabilityEvent()` | `{sequence,neighbor,attemptCount}` | logged | host (existing dup-filter tests) + real harness |
| `PRIORITY_ROUTE` (this project's real name for the manual's `PRIORITY_OVERRIDE`) | a priority `ROUTE_SELECTED` | `telemetry.cpp::onRouteEvent()` | `{destination,nextHop,hopCount}` | GUI's `PRIORITY_OVERRIDE` branch (name-matched via a different string — see "not yet reconciled" below) | host + real harness |
| `SENSOR_ANOMALY` (manual's `ANOMALY_SPIKE`) | real modified-Z spike | `telemetry.cpp::onAnomalyEvent()` | `{modifiedZ,threshold}` | GUI's `SENSOR_STATUS healthState==ANOMALY` decision HUD | host + real harness (Phase 3/6) |
| `SENSOR_FAILURE` (manual's `ANOMALY_STUCK`) | real flatline/stale/invalid | `telemetry.cpp::onAnomalyEvent()` | `{reason,durationMs?}` | GUI's `SENSOR_STATUS healthState==FLATLINE` decision HUD | host + real harness |
| `REROUTE_PROPOSED`/`RECOVERY` (manual names) vs. this project's `ROUTE_CHANGE`/`(none)` | — | — | — | **not reconciled by name** — the replacement GUI's own code checks `eventName==='PRIORITY_OVERRIDE'`/`==='RECOVERY'` in a few decision-HUD branches that this project's real `eventType` strings (`PRIORITY_ROUTE`, and no discrete recovery EVENT at all — see `LINK_RECOVERED`/`SENSOR_RECOVERED` above) don't literally match; those specific HUD popups stay dormant, not fabricated. A real, documented, non-blocking naming gap — see decisions.md |
| `PACKET_SENT`/`PACKET_DELIVERED` (manual names, as EVENT types) | — | — | — | this project reports these as the new `0x0B PACKET` message's own `status` field instead (`SENT`/`DELIVERED`), not as separate EVENT eventTypes — a deliberate design choice (see decisions.md), not a gap |
| `PRIORITY_BROADCAST` (2026-08-18) | a real successful `transport::send()` at priority-broadcast origination | `suppression.cpp::broadcastPriority()` -> `telemetry.cpp::onSuppressionEvent()` | `{sequence,destination}`, `source`="A" | new `#priorityFlow` panel resets to a fresh flow, adds a BROADCAST step at the real originating node, violet node-pulse | host (`suppression_core`, 55/55) + real GUI-parser harness (this pass, 25/25) |
| `PRIORITY_OVERHEARD` (2026-08-18) | a genuine relay rebroadcast overheard (`prevHop != source`) — never the original transmission | `suppression.cpp::onPacketReceived()` -> `telemetry.cpp::onSuppressionEvent()` | `{sequence,rssi,overheardCount,currentNode}`, `source`=original sender | flow panel adds an OVERHEARD step at `envelope.nodeId` (NOT `payload.source`), violet node-pulse | host + real harness |
| `PRIORITY_FORWARD` (2026-08-18) | this node's own RSSI-aware backoff expired with `overheardCount < SUPPRESSION_THRESHOLD` | `suppression.cpp::tick()` -> `telemetry.cpp::onSuppressionEvent()` | `{sequence,overheardCount,threshold,backoffMs,currentNode}`, `source`=original sender | flow panel adds a FORWARD step (green), violet node-pulse | host + real harness |
| `PRIORITY_SUPPRESSED` (2026-08-18) | this node's own backoff expired with `overheardCount >= SUPPRESSION_THRESHOLD` — another relay already covered it | `suppression.cpp::tick()` -> `telemetry.cpp::onSuppressionEvent()` | `{sequence,overheardCount,threshold,currentNode}`, `source`=original sender | flow panel adds a SUPPRESSED step, deliberately styled NEUTRAL (muted), never red/failure — must never read as packet loss | host + real harness (explicitly checked: `cls==='suppressed'`, never a failure class) |
| `PRIORITY_DELIVERED` (2026-08-18) | the real destination's first genuine reception of this identity | `suppression.cpp::onPacketReceived()` -> `telemetry.cpp::onSuppressionEvent()` | `{sequence,appSeq?,potValue?,ldrValue?}` (pot/ldr omitted if the real payload didn't decode — never fabricated), `source`=original sender | flow panel adds a DELIVERED step and marks the flow complete (✓ in the meta line) | host + real harness, including the missing-optional-field (decode-failure) case |

## `0x0B PACKET` (2026-08-18, new)

Real per-hop application/mesh packet movement, generated only from
`reliability::ReliabilityEvent` — never a parallel simulator. See
`telemetry_core.h`'s `PacketPayload` file header for the full three-
(really four-, once the decoded app-level timestamp is counted) identity-
axis explanation (`meshSequence` vs `appSeq` vs the envelope's own `seq`
vs `appTimestampMs`).

| Field | Source | Status |
|---|---|---|
| `meshSequence` / `seq` | `ReliabilityEvent.sequence` (real `MeshPacket` identity) | real |
| `appSeq` | `apptraffic_core::decodeData()`'s real decode of the received payload — present only on a real `PACKET_RECEIVED` at the real sink | real, live decode path (Phase 17 of the 2026-08-18 implementation pass — previously `decodeData()` existed and was unit-tested but had no live caller; now wired into `telemetry.cpp::onReliabilityEvent()`) |
| `potValue`/`ldrValue`/`appTimestampMs` | same real decode | real, same gate as `appSeq` |
| `src`/`dst` | `ReliabilityEvent.source`/`.destination` (`.destination` is new — reliability.cpp didn't carry this before this pass) | real |
| `currentNode` | `thisNode().name` | real |
| `nextHop` | `ReliabilityEvent.neighbor`, when meaningful | real |
| `path` | the real `[currentNode, neighbor]` (or `[neighbor, currentNode]` for `RECEIVED`) pair for THIS hop — deliberately not a full multi-hop reconstruction (`ROUTE_UPDATE` already owns that); matches the GUI's own `ingestPacket()`/`normalizeHops()` expected shape, confirmed by a real harness run (2026-08-18) | real |
| `trafficClass`/`priority` | `ReliabilityEvent.priority` (new — reliability.cpp didn't carry this before this pass) | real |
| `status` | `SENT`/`RETRIED`/`DELIVERED`/`FAILED`/`RECEIVED`, mapped 1:1 from the real `ReliabilityEventType` | real |
| `attemptCount` | `ReliabilityEvent.attemptCount`, including the real, final value `reliability_core::AckResult` now reports (new) | real |

**RESOLVED, 2026-08-18 (with explicit user authorization to edit `gui-main/`
for this one line):** the replacement GUI's own `applyTelemetryCore()`
switch statement had no `case 'PACKET':`, so a real `PACKET` message also
produced a spurious `"PARSE WARNING: unrecognized firmware message type"`
log line — despite the same message being correctly consumed and animated
by the outer `applyTelemetry()` wrapper's own independent
`raw.type==='PACKET'` check into `ingestPacket()`. Confirmed cosmetic-only
(not a functional defect) by a real Node.js harness run, then fixed with
a single added case (`case'PACKET':break;`, added in
`mesh-command-console (1).html` right before the `default:` branch) —
the user explicitly authorized this one specific, minimal edit to
`gui-main/`, an otherwise-standing off-limits rule (see `docs/decisions.md`).
Re-ran the same harness against the fixed file: 24/24, zero PARSE WARNING
lines for all 9 real messages fed. See `docs/testing.md`.

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
