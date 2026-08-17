# Known Issues

## Physical hardware now exists (2026-08-17) — not yet flashed

Update, Phase 6: 5 physical boards are now in hand (4x ESP32-WROOM-32
labeled A/B/D/S, 1x classic ESP32 Dev Module labeled "E" — see
[hardware-readiness.md](hardware-readiness.md) for the full pre-flash
audit). **Nothing has been flashed yet** — this phase was explicitly
audit-and-prepare only. Everything below this heading, written while
hardware genuinely didn't exist, is now superseded by
[hardware-readiness.md](hardware-readiness.md)'s more detailed Part B
audit, but is kept here as the running checklist of what's still
unvalidated.

**RESOLVED as of Phase 7 (2026-08-17): physical board "E" is confirmed to
be logical `NODE_C`.** The team-confirmed answer to the board-label
question this section originally flagged as unresolved. `core/node_id.h`'s
`nodeTable()` now carries the real, team-confirmed MAC address table (see
[decisions.md](decisions.md#real-mac-address-table-populated-physical-board-e-confirmed-as-logical-node_c)) —
the original board-label-mismatch entry below is kept as the historical
record of why the question was flagged rather than silently assumed, per
[decisions.md](decisions.md#board-labelnode-id-mismatch-flagged-not-silently-resolved-phase-6).

## Two new, real findings from the hardware team's own bring-up sketches (`hardware code/`)

**Update (2026-08-17):** `hardware code/0.96esp32node/0.96esp32node.ino`
and `hardware code/1.3esp32node/1.3esp32node.ino` (added to the repo ahead
of this audit) are the hardware team's own sensor+OLED bench-test
sketches. Auditing them against `firmware/PredictiveMesh/` and
implementation-guide.html found:

1. **Likely real bug in the 0.96" sketch's OLED address** (`0x78` where
   `0x3C` is expected by the Adafruit_SSD1306 API) — very likely causes
   `display.begin()` to fail on real hardware even with correct wiring.
   Not fixed here (belongs to the hardware team's own file, out of this
   session's scope) — flagged for the team. Firmware's own address for
   Node S (`OLED_I2C_ADDRESS_S`) is `0x3C`, matching this reasoning.
   **Node C is different** — see the 2026-08-18 update below.
2. **Real contradiction between the guide's BOM and the hardware
   evidence:** the guide specifies 2x *identical* 0.96" SSD1306 displays;
   the hardware team has bench-tested two *different* controllers (0.96"
   SSD1306 and 1.3" SH1106, needing different Arduino libraries).

**RESOLVED as of the OLED integration pass (2026-08-18): confirmed by the
team as a real, permanent hardware fact, not a still-open evaluation.**
Node S runs the 0.96" SSD1306, Node C runs the 1.3" SH1106 — two genuinely
different physical modules, a real deviation from the guide's "2x
identical" BOM. `src/oled/oled.cpp` selects the matching Adafruit driver
per `THIS_NODE_ID` at runtime (never a per-node compile flag — see
[decisions.md](decisions.md#oled-integration-per-node-driver-selection-screen-content-and-why-polling-not-a-third-event-callback-slot)).

**Update, 2026-08-18 — I2C addresses split per node, team-corrected:** the
two drivers do **not** share one address. Node S (SSD1306) uses
`OLED_I2C_ADDRESS_S` = `0x3C`. Node C (SH1106) uses `OLED_I2C_ADDRESS_C` =
`0x78` — a real, directly team-confirmed hardware value that overrides
this document's own earlier "0x78 on the PCB means 0x3C in Arduino"
inference for that same 1.3" module. Trusted as a direct hardware fact,
same standing as the real MAC table. See
[decisions.md](decisions.md#oled-i2c-addresses-split-per-node-team-corrected-2026-08-18).

Full detail, including the exact sketch excerpts and reasoning:
[hardware-readiness.md](hardware-readiness.md)'s Part 3/4/5 section and
[decisions.md](decisions.md#hardware-teams-096-oled-bench-sketchs-i2c-address-0x78-flagged-as-a-likely-bug-not-silently-fixed).
OLED is now implemented in `firmware/PredictiveMesh/src/oled/` (compiles
clean, real ESP32 build, both `ENABLE_UCB1` configs) — see
[decisions.md](decisions.md#oled-integration-per-node-driver-selection-screen-content-and-why-polling-not-a-third-event-callback-slot)
and [testing.md](testing.md). **Still `NOT RUN — HARDWARE NOT AVAILABLE`**
on a real display — nothing has been flashed yet.

## Hardware not currently flashed

Firmware is written against the agreed contract (see [parameters.md](parameters.md))
but the following cannot be validated until boards are actually flashed:

- [ ] Actual ESP32 flash test (does this firmware boot on real hardware?)
- [ ] Actual ESP-NOW packet exchange between two or more real boards
- [ ] Actual RSSI validation (`info->rx_ctrl->rssi` returning real, sane values on core 3.x)
- [ ] Actual WiFi channel validation (do all five boards actually agree on `MESH_WIFI_CHANNEL` in practice?)
- [ ] Actual ADC validation (`analogRead()` on GPIO34/35 behaving correctly with ESP-NOW active — this is exactly the ADC2-after-radio-init trap documented in `docs/parameters.md`)
- [ ] Actual OLED validation (Node S's SSD1306 answering at `0x3C`, Node
      C's SH1106 answering at `0x78`, both on GPIO21/22, and
      `src/oled/oled.cpp` actually driving them — code exists and compiles
      clean as of 2026-08-18, but has never run against a real display)
- [ ] Actual buzzer validation (GPIO25 driving the piezo module)

Software validation for Phase 0 consisted of compilation, static
inspection, and configuration/architecture review only — see
[testing.md](testing.md) for exactly what ran and what didn't. **No
hardware-dependent test result in this repository should be read as
"passed."** Anything not explicitly logged as run in `testing.md` is
`NOT RUN — HARDWARE NOT AVAILABLE`.

## Peer MAC addresses are placeholders

`core/node_id.h`'s `NODE_TABLE` has every node's `mac[6]` field set to
`{0,0,0,0,0,0}` — a sentinel meaning "not yet configured," not a real
address. `main.cpp`'s `registerConfiguredPeers()` detects this sentinel and
skips unicast peer registration with a `[WARN]` log line, falling back to
the broadcast peer (see
[decisions.md](decisions.md#broadcast-peer-as-the-phase-0-espnow-bootstrap)).

**Fill-in procedure once hardware exists:**
1. Flash the Phase 0 firmware to each board (with the correct
   `THIS_NODE_ID` per board).
2. Open the Serial monitor at 115200 baud — boot log prints
   `[INFO] Own MAC address: AA:BB:CC:DD:EE:FF (record this in core/node_id.h's NODE_TABLE once hardware exists)`.
3. Record each board's MAC against its role.
4. Update `NODE_TABLE` in `core/node_id.h` with the five real MAC
   addresses.
5. Reflash all five boards. Unicast peer registration will now succeed for
   real neighbors instead of logging the placeholder warning.

## Deferred design questions (not blocking, tracked for later phases)

- ~~**Loop prevention on multi-hop forwarding.**~~ — **Resolved for
  Phase 1**, not closed: no TTL/hop-count field was needed because route
  advertisements are single-hop by construction (distance-vector, not
  flooding) and Phase 1 doesn't implement actual hop-by-hop relaying of a
  received `MSG_DATA` packet — `routing::getNextHop()` only decides, it
  doesn't act. Revisit when a phase implements real relay (§5.4
  reliability layer / hop-by-hop ACK). See
  [decisions.md](decisions.md#no-ttlhop-count-field-added-to-meshpacket-in-phase-1).
- **`PACKET_MAX_PAYLOAD` (64 bytes) is an estimate**, not derived from a
  finalized payload schema (since the sensor/anomaly telemetry format
  itself isn't designed yet). Revisit once the anomaly/predictor layers
  define what they actually need to send.
- **`MESH_WIFI_CHANNEL` (6) is a placeholder value**, not chosen based on
  real RF-environment testing. Pick based on local WiFi congestion once
  hardware/demo-site testing is possible.

## Toolchain

- **Resolved 2026-08-17 (post-Phase-1):** `esp32:esp32` core 3.3.11 is now
  installed and verified (`arduino-cli` 1.5.2-rc.1). A real
  `arduino-cli compile --fqbn esp32:esp32:esp32 firmware/PredictiveMesh`
  was run against the full Phase 0 + Phase 1 firmware — one real API-drift
  error found (`esp_now_send_cb_t`'s signature; see
  [decisions.md](decisions.md#esp_now_send_cb_t-signature-adapted-for-arduino-esp32-core-3311)),
  fixed, then a clean rebuild: 0 errors, 0 warnings (`--warnings all`).
  Full detail and the real compiler output in `docs/testing.md`. The
  firmware requires core 3.x (ESP-IDF >= 5.1) specifically for the
  `esp_now_recv_info_t*` receive callback signature — see
  implementation-guide.html §04's toolchain-constraint callout; that part
  of the assumption held unchanged on 3.3.11. If a contributor's local
  Arduino IDE has an older core installed, RSSI extraction will not
  compile as written.

## `implementation-guide.html` location doesn't match `CLAUDE.md`

`CLAUDE.md` describes it as living at the repo root. The actual file is at
`PERSONAL_DOCS/implementation-guide.html` — there is no copy at the repo
root. Noted here rather than silently "fixed" by moving the file (it may
be there deliberately, e.g. kept out of a public repo root on purpose);
`CLAUDE.md`'s pointer has been corrected to the real path. Flag if the
file was actually meant to be at the root and got moved by mistake.

## GUI telemetry contract — now implemented for real (Phase 6)

**Update (2026-08-17, Phase 6):** Firmware now implements the frozen
`mesh-json/v1` contract (`gui-main/gui-main/docs/gui-telemetry-contract.md`)
for real — `src/telemetry/telemetry_core.h/.cpp` + `src/telemetry/telemetry.h/.cpp`,
all 10 message types, real JSON over Serial, validated both by a
94-check host test suite and by running real firmware-generated output
through the GUI's own unmodified parsing code. See
[gui-compatibility-matrix.md](gui-compatibility-matrix.md) for the
complete field-by-field audit and [decisions.md](decisions.md) for every
enum-mapping/derivation decision. This section is kept (rather than
deleted) as a historical record of the pre-Phase-6 gap and now records
what's still genuinely open after implementation:

**RESOLVED, Phase 7.1, for both demo-relevant routes: the topology-diagram
animation gap.** The console's topology-diagram animation only recognizes
this exact demo topology's three known full-path route strings (`"ABS"`,
`"ACDS"`, `"AS"`). `ROUTE_UPDATE.hops` previously could only ever honestly
report 2 elements (`[thisNode, nextHop]`), so any real 2+-hop route
produced a `routeKey` the GUI's hardcoded matcher didn't recognize. A
red-team review pass found this fixable without touching the GUI or
fabricating anything: `routing_core::reconstructPath()` (new, pure,
read-only) searches the compiled-in static adjacency graph for the UNIQUE
path matching `routing_core`'s own already-computed real hop count —
provably correct for `A→B→S` and `A→C→D→S` (this topology's two demo
routes), refusing to guess (returns nothing, honest 2-element fallback
used instead) for the handful of other (self, destination) pairs where the
graph is genuinely ambiguous. Verified against the GUI's real, unmodified
`routeKey(hops){return hops.join('')}` source: a correctly reconstructed
`["A","B","S"]`/`["A","C","D","S"]` now produces exactly `"ABS"`/`"ACDS"`
— the GUI's own recognized strings. See
[decisions.md](decisions.md#phase-71-red-team-pass--finding-5-route_update-hops-fixed--real-bounded-deterministic-path-reconstruction)
and [gui-compatibility-matrix.md](gui-compatibility-matrix.md) for the
full detail. Not yet re-verified through the full Phase 6 Node.js
GUI-parser harness (that harness was disposable/scratchpad and wasn't
rebuilt this pass — a reasonable follow-up before physical demo
rehearsal); the fix itself is host-test-verified (`test_routing_core.cpp`)
and confirmed via a real ESP32 compile.

**Other honest limitations, all documented, none fabricated:**
`ROUTE_UPDATE.score` reuses the next hop's own `link_score` (no multi-hop
composite score exists anywhere in this codebase); `ROUTE_UPDATE.reason`
reports `UNKNOWN` outside the priority/expiry/health-driven cases
`routing_core`/`telemetry` can actually distinguish (as of Phase 7.1,
`LINK_DEGRADATION`/`ROUTE_RECOVERY` are also real and derived from a real
hop-count comparison — see decisions.md's Finding 6 entry; `STALE_NEIGHBOR`/
`MANUAL` remain the only 2 of the contract's 8 `routeReason` values with no
firmware source to report honestly, down from 5); `STATISTICS.endToEndLatencyMs`
actually reports per-hop ACK latency, not a true multi-hop measurement (no
such mechanism exists); `HELLO.mac` is now populated in the very first
HELLO (fixed Phase 7.1 — `WiFi.mode(WIFI_STA)`/`WiFi.macAddress()` moved
ahead of `telemetry::init()`, since the real MAC doesn't structurally
require the rest of `transport::begin()` to finish first); `HELLO.config.ewmaAlpha`
reports one of two real EWMA constants this project has (RSSI's, not
PDR's); `NODE_JOIN`/`NODE_LEAVE` have no firmware event source at all
(routing tracks liveness, but nothing distinguishes "first contact" from
ordinary beacon traffic). UCB1 has no dedicated telemetry message (the
frozen contract defines none, and Part 13 of the Phase 6 instructions
explicitly forbade inventing one) — its effect is already visible through
`ROUTE_UPDATE` whenever `ENABLE_UCB1=1`.

**`STATISTICS`'s counters are real but currently read their honest neutral
defaults** (`pdr:1.0`, all packet counts `0`) on any fresh boot, because no
`MSG_DATA` traffic flows yet — see the "Application traffic" entry below
and [hardware-readiness.md](hardware-readiness.md)'s Part F. Not a bug;
exactly the same gap Phase 4/5 already documented, now visible in the
telemetry output itself rather than only in internal counters.

## Phase 3 anomaly engine — OLED wiring deliberately deferred — SUPERSEDED (OLED integration pass, 2026-08-18)

implementation-guide.html §06's roadmap bundles "wire both flags to the
OLED on Node C" into the same Hours 12-17 bucket as the anomaly algorithm
itself. Phase 3 implemented the algorithm and real Serial logging
(`[ANOMALY]` lines) but deliberately did not add an OLED driver library or
wire anything to Node C's display — see
[decisions.md](decisions.md#anomaly-detection-scope-no-oledtelemetry-wiring-in-phase-3)
for the original reasoning (no display library existed yet in this
project).

**Superseded, 2026-08-18: `src/oled/` now exists.** Node C's OLED shows
exactly the two independent flags this section originally deferred
(SPIKE/JUMP from `anomaly_core::SensorState::ANOMALY`, STUCK from
`SensorState::FLATLINE`), read directly via `anomaly::getTelemetry()` — no
change to `anomaly_core`/`anomaly.cpp` themselves was needed or made. See
[decisions.md](decisions.md#oled-integration-per-node-driver-selection-screen-content-and-why-polling-not-a-third-event-callback-slot).

## Phase 3 anomaly engine — not yet run on hardware

Everything in `src/anomaly/` (boot calibration, MAD Z-score, flatline
detector, state machine, debounce/recovery, staleness) is verified two
ways: a host-compiled, actually-executed unit test suite
(`firmware/PredictiveMesh/test/test_anomaly_core.cpp` — 50/50 checks, see
`docs/testing.md`) and a real `arduino-cli` compile of the whole sketch
(including, for the first time in this project, real `analogRead()`/
`analogReadResolution()`/`pinMode()` calls). Neither is a substitute for
the real thing:

- [ ] Twisting a real potentiometer on Node C produces a real ANOMALY
      transition on the Serial monitor (the guide's own Hours 12-17 sync
      checkpoint)
- [ ] Holding the potentiometer still produces a real FLATLINE transition
- [ ] `ANOMALY_MAX_CALIBRATION_VARIANCE` (currently a placeholder, 400) is
      tuned against real ADC resting-noise data instead of a guess
- [ ] `ANOMALY_FLATLINE_EPS` (currently a placeholder, 2.0 LSB) is tuned
      against real hardware
- [ ] The LDR sensor's real environmental disturbance (a shadow/occlusion,
      per the guide's own framing) is confirmed to trigger the same
      detector that the potentiometer does, proving generalization across
      signal shapes

All `NOT RUN — HARDWARE NOT AVAILABLE` per the checklist at the top of
this file.

## Phase 4 reliability layer — not yet run on hardware (the "no live application traffic" gap is RESOLVED as of Phase 7)

Everything in `src/reliability/` (packet identity, unicast send, ACK,
bounded retry, timeout, duplicate filter, forwarding, PDR wiring) is
verified two ways: a host-compiled, actually-executed unit test suite
(`firmware/PredictiveMesh/test/test_reliability_core.cpp` — 88/88 checks,
see `docs/testing.md`) and a real `arduino-cli` compile of the whole
sketch (including, for the first time in this project, real
`esp_now_send()` calls for genuine unicast traffic). Neither is a
substitute for the real thing.

**Update, Phase 7:** the extra gap this section originally flagged beyond
"no hardware exists yet" — **no real application traffic existed to
generate even in principle**, since `reliability::send()` had no automatic
caller — is now resolved at the code level: `src/apptraffic/` calls
`reliability::send()` for real (`NODE_A -> NODE_S`, see
[decisions.md](decisions.md#phase-7--resolved-reliabilitysend-now-has-a-live-automatic-caller-node_a---node_s)).
The original entry is kept below as the historical record of why the gap
existed; every item below remains genuinely `NOT RUN` until real hardware
exists to run it on.

- [ ] Two real boards exchange a genuine unicast `MSG_DATA`/`MSG_ACK` pair
      (mechanism real since Phase 4, live caller real since Phase 7 — still
      needs actual hardware)
- [ ] A real dropped frame (e.g. Faraday-bag attenuation, per the guide's
      own §06 demo) actually triggers a real retry, observed on the Serial
      monitor
- [ ] `RELIABILITY_ACK_TIMEOUT_MS` (currently a placeholder, 200ms) is
      tuned against real hardware round-trip timing
- [ ] Real multi-hop forwarding (e.g. A -> C -> D -> S) delivers correctly
      end-to-end
- [ ] `predictor`'s PDR reflects real, observed per-hop delivery ratios —
      not just the wired-but-unexercised mechanism
- [x] ~~A real application data source is decided and wired to
      `reliability::send()`~~ — **done, Phase 7** (`src/apptraffic/`,
      `NODE_A -> NODE_S`). Observing its real hardware behavior remains
      `NOT RUN`, tracked by the items above.

All remaining items `NOT RUN — HARDWARE NOT AVAILABLE` per the checklist
at the top of this file.

## Phase 5 UCB1 adaptive routing — not yet run on hardware (inherited "no live traffic" gap is RESOLVED as of Phase 7)

`src/ucb1/` (bandit statistics, UCB1 selection formula, health-tiering,
loop-guard exclusion) is verified two ways: a host-compiled,
actually-executed unit test suite
(`firmware/PredictiveMesh/test/test_ucb1_core.cpp` — 26/26 checks, see
`docs/testing.md`) and a real `arduino-cli` compile of the whole sketch in
**both** `ENABLE_UCB1=0` and `ENABLE_UCB1=1` configurations. Neither is a
substitute for the real thing.

**Update, Phase 7:** the gap this section inherited directly from Phase 4
— UCB1's reward signal comes entirely from `reliability::send()`'s
outcomes, and nothing called that automatically — is resolved the same
way: `src/apptraffic/` is now that caller, and (per
[decisions.md](decisions.md#ucb1pdr-outcome-wiring-required-no-code-changes-in-reliabilitycpp-routingcpp-or-ucb1cpp--verified-not-re-implemented))
required zero changes to `ucb1.cpp`/`ucb1_core.cpp` themselves — the
outcome-feeding call sites already existed, gated behind `#if
ENABLE_UCB1`, and simply had no real events flowing through them yet. With
`ENABLE_UCB1=1` flashed to real boards running real `apptraffic` traffic,
the bandit tables will now have real delivery history to learn from.

- [ ] `ENABLE_UCB1=1` flashed to real boards, with real `apptraffic`
      traffic (`NODE_A -> NODE_S`, resolved Phase 7) actually generating
      delivery outcomes
- [ ] A real preference shift is observed — e.g. after a route's real
      delivery rate degrades (a Faraday-bag attenuation on a specific
      neighbor, per the guide's own §06 demo), UCB1 measurably stops
      preferring it
- [ ] `UCB1_EXPLORATION_C` (currently `sqrt(2)`, the textbook default) is
      confirmed to produce sensible explore/exploit behavior at this
      topology's real traffic volume, or re-tuned if not
- [ ] The two-node-loop guard is confirmed to never trigger falsely on
      real, legitimately-converged distance-vector data (it's expected to
      be a no-op in the real topology — see
      [decisions.md](decisions.md#forwarding-loop-prevention-relies-on-routing_core-correctness--a-next-hop-not-prev-hop-guard--the-duplicate-filter--no-new-ttl-field))

All `NOT RUN — HARDWARE NOT AVAILABLE` per the checklist at the top of
this file — real hardware is now the only remaining blocker for every item
above.

## Phase 6 telemetry — not yet run on hardware

`src/telemetry/` (JSON envelope/payload construction, all 10 message
types, event wiring) is verified two ways: a host-compiled,
actually-executed unit test suite
(`firmware/PredictiveMesh/test/test_telemetry_core.cpp` — 94/94 checks,
see `docs/testing.md`) and a real run of firmware-generated JSON through
the GUI's own unmodified parsing code (also `docs/testing.md`). Neither is
a substitute for the real thing:

- [ ] Real telemetry JSON observed over an actual USB/Serial connection at
      115200 baud, not just generated by a host program
- [ ] The GUI's real "Connect Hardware" (WebSerial) or "Connect via Bridge"
      path successfully parses live firmware output end-to-end
- [ ] `HELLO.mac` observed carrying a real MAC in the first HELLO on real
      hardware (code-level fix landed Phase 7.1 — `WiFi.macAddress()` is
      now read before `telemetry::init()`; genuinely unverified on real
      silicon until flashed)
- [ ] Telemetry emission volume (6 periodic message types + event-driven
      ones) confirmed not to overwhelm a real 115200-baud UART or the
      GUI's own parsing loop at real sustained rates
- [ ] `TELEMETRY_OFFLINE_TIMEOUT_MS` (currently 3000ms, a placeholder
      derivation) confirmed sane against real multi-node boot/heartbeat
      timing

All `NOT RUN — HARDWARE NOT AVAILABLE` per the checklist at the top of
this file. The second item's original caveat — needing the "no live
`MSG_DATA` traffic" gap resolved before `STATISTICS`'s counters would show
anything but their neutral defaults — is resolved at the code level as of
Phase 7 (`src/apptraffic/`, see
[hardware-readiness.md](hardware-readiness.md)'s Part F and
[decisions.md](decisions.md#phase-7--resolved-reliabilitysend-now-has-a-live-automatic-caller-node_a---node_s));
real hardware is still required to actually observe non-default counters.

## Phase 7 application traffic — not yet run on hardware

`src/apptraffic/` (send-decision logic, payload encode/decode, the Serial
priority trigger) is verified two ways: a host-compiled,
actually-executed unit test suite
(`firmware/PredictiveMesh/test/test_apptraffic_core.cpp` — 29/29 checks,
see `docs/testing.md`) and a real `arduino-cli` compile of the whole
sketch in both `ENABLE_UCB1` configurations. Neither is a substitute for
the real thing:

- [ ] A real `NODE_A` sends a real `MSG_DATA` packet to `NODE_S` on real
      hardware, observed as `[APPTRAFFIC] TX ...` on the Serial monitor
- [ ] `APPLICATION_TX_INTERVAL_MS` (currently 2000ms, a placeholder) is
      tuned against real hardware send/ACK/retry timing once observed
- [ ] A real Serial `'p'`/`'P'` keypress on `NODE_A` produces a real
      `PRIORITY` packet, observed taking the direct `A -> S` hop instead
      of the NORMAL `A -> B -> S` path
- [ ] Whether the GUI's own `serial-bridge.py` forwards keystrokes back to
      the firmware (undetermined — not assumed either way, see
      decisions.md) — if not, the priority trigger requires a direct
      terminal connection to `NODE_A`, not just the GUI

All `NOT RUN — HARDWARE NOT AVAILABLE` per the checklist at the top of
this file.

## Phase 7.1 red-team fixes (ROUTE_UPDATE path reconstruction, route reason, HELLO MAC) — not yet run on hardware

`routing_core::reconstructPath()` (new), the widened `onRouteEvent()`
health-driven-reroute detection, and the `WiFi.mode(WIFI_STA)`-before-
`telemetry::init()` reordering are verified two ways: host-compiled,
actually-executed unit test suites (`test_routing_core.cpp` 37/37 including
7 new `reconstructPath()` checks, `test_telemetry_core.cpp` 99/99 including
5 new `RouteReason` checks — see `docs/testing.md`) and a real
`arduino-cli` compile of the whole sketch in both `ENABLE_UCB1`
configurations. Neither is a substitute for the real thing:

- [ ] A real health-driven reroute (Faraday-bag B, per the guide's own
      §06 demo) is observed producing a real `ROUTE_UPDATE`/`ROUTE_CHANGE`
      with `hops:["A","C","D","S"]`, `hopCount:3`, `reason:"LINK_DEGRADATION"`
      — the exact scenario this fix exists for, genuinely untested until
      real hardware exists to degrade a real link on
- [ ] The GUI's real topology-diagram animation is observed actually
      highlighting the `A-C-D-S` backup path once that `ROUTE_UPDATE`
      arrives over a real Serial connection (verified so far only by
      reading the GUI's real, unmodified `routeKey()` source — not by
      running the full Phase 6 GUI-parser harness against this specific
      fix, and not over real hardware)
- [ ] `HELLO.mac` observed carrying the real MAC on actual first boot
      (code fix verified by ESP32 compile only, not real Serial output)

All `NOT RUN — HARDWARE NOT AVAILABLE` per the checklist at the top of
this file.

## Phase 1 routing — not yet run on hardware

Everything in `src/routing/` (distance-vector table, HELLO/route-ad
beacons, priority override) is verified two ways: a host-compiled,
actually-executed unit test suite
(`firmware/PredictiveMesh/test/test_routing_core.cpp` — see
`docs/testing.md` for the real pass/fail output) and static review against
implementation-guide.html. Neither is a substitute for the real thing:

- [ ] Two or more real boards actually converge (beacons exchanged over
      real ESP-NOW, real RSSI, real timing/jitter) to the same routing
      table this test suite predicts
- [ ] A real priority packet visibly takes a different path than a real
      normal packet on the Serial monitor / dashboard
- [ ] Real route expiry behavior when a board is physically powered off or
      RF-attenuated, not just a fast-forwarded `now` value in a test

All `NOT RUN — HARDWARE NOT AVAILABLE` per the checklist at the top of
this file.

## Phase 2 predictor — not yet run on hardware

Everything in `src/predictor/` (RSSI EWMA/slope, PDR EWMA, fused
`link_score`, two-threshold hysteresis, staleness fast-path) is verified
two ways: a host-compiled, actually-executed unit test suite
(`firmware/PredictiveMesh/test/test_predictor_core.cpp` — 31/31 checks,
see `docs/testing.md`) and a real `arduino-cli` compile of the whole
sketch. Neither is a substitute for the real thing:

- [ ] Real RSSI on a genuinely degrading link (e.g. the guide's own
      "Faraday bag on Node B" test) actually produces a visibly falling
      `link_score` on the Serial monitor
- [ ] `PREDICTOR_SLOPE_REF_DBM_PER_SAMPLE` (currently a placeholder, 1.5)
      is tuned against real attenuation data instead of a guess
- [ ] Real NORMAL traffic visibly reroutes from B to C when B's real link
      degrades, not just in the host test's hand-constructed inputs
- [ ] The staleness fast-path's 2000ms timeout is validated against real
      beacon jitter/loss rates, not just a fast-forwarded `now` value

All `NOT RUN — HARDWARE NOT AVAILABLE` per the checklist at the top of
this file.

## PDR evidence stream: real math, not yet fed by live data

Tracked in full in
[decisions.md](decisions.md#pdr-measurement-boundary-not-wired-to-live-send-outcomes-in-phase-2) —
summarized here per this file's own checklist convention. `predictor::onSendResult()`
is implemented and tested but has no live caller: every send this firmware
performs is a broadcast `MSG_HEARTBEAT` beacon (no real per-neighbor
delivery signal — ESP-NOW broadcast isn't MAC-layer-ACKed), and even a real
unicast send's `TxEvent` identifies a MAC, not a `NodeId`, which can't be
safely reverse-mapped while `NODE_TABLE`'s MACs remain the Phase 0
all-zero placeholder (see "Peer MAC addresses are placeholders" above).
Revisit once both (a) real unicast traffic exists (reliability layer,
§5.4) and (b) real MACs are populated post-hardware.
