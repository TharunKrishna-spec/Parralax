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

**Flagged, unresolved: physical board "E" doesn't match any logical node
name.** Firmware/guide/GUI all require exactly A/B/C/D/S; the physical
inventory has no "C" and an unexplained "E". See
[hardware-readiness.md](hardware-readiness.md)'s board-label section and
[decisions.md](decisions.md#board-labelnode-id-mismatch-flagged-not-silently-resolved-phase-6) —
needs team confirmation before the MAC table can be filled in.

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
   session's scope) — flagged for the team. Firmware's own
   `OLED_I2C_ADDRESS` constant is already correct (`0x3C`), unaffected.
2. **Real contradiction between the guide's BOM and the hardware
   evidence:** the guide specifies 2x *identical* 0.96" SSD1306 displays;
   the hardware team has bench-tested two *different* controllers (0.96"
   SSD1306 and 1.3" SH1106, needing different Arduino libraries). Not
   resolved — reported per the standing "identify the contradiction,
   don't silently resolve it" rule.

Full detail, including the exact sketch excerpts and reasoning:
[hardware-readiness.md](hardware-readiness.md)'s Part 3/4/5 section and
[decisions.md](decisions.md#hardware-teams-096-oled-bench-sketchs-i2c-address-0x78-flagged-as-a-likely-bug-not-silently-fixed).
Neither finding blocks anything currently built — OLED wiring in
`firmware/PredictiveMesh/` remains deferred, unchanged since Phase 0 —
but both need team input before that future phase can safely start.

## Hardware not currently flashed

Firmware is written against the agreed contract (see [parameters.md](parameters.md))
but the following cannot be validated until boards are actually flashed:

- [ ] Actual ESP32 flash test (does this firmware boot on real hardware?)
- [ ] Actual ESP-NOW packet exchange between two or more real boards
- [ ] Actual RSSI validation (`info->rx_ctrl->rssi` returning real, sane values on core 3.x)
- [ ] Actual WiFi channel validation (do all five boards actually agree on `MESH_WIFI_CHANNEL` in practice?)
- [ ] Actual ADC validation (`analogRead()` on GPIO34/35 behaving correctly with ESP-NOW active — this is exactly the ADC2-after-radio-init trap documented in `docs/parameters.md`)
- [ ] Actual OLED validation (SSD1306 answering at `0x3C` on GPIO21/22)
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

**One real, demonstrated GUI-compatibility limitation, not fixed (per
explicit instruction not to modify the GUI):** the console's
topology-diagram animation only recognizes this exact demo topology's
three known full-path route strings (`"ABS"`, `"ACDS"`, `"AS"`). Firmware's
`ROUTE_UPDATE.hops` can only ever honestly report 2 elements
(`[thisNode, nextHop]`) — distance-vector routing never learns a
destination's full multi-hop path, only the next hop and total distance —
so any real 2+-hop route produces a `routeKey` the GUI's hardcoded matcher
doesn't recognize, and the topology diagram's animated path won't
highlight for it (verified, not predicted — see
[testing.md](testing.md)'s Phase 6 section). The underlying route data is
still received and displayed correctly elsewhere (the "Route candidates"
panel doesn't depend on `routeKey` matching). Resolving this for real
would mean either a link-state protocol extension (out of scope, a genuine
future design decision) or the GUI accepting a shorter/different route
representation — a conversation with the GUI owner, not a firmware-side
workaround.

**Other honest limitations, all documented, none fabricated:**
`ROUTE_UPDATE.score` reuses the next hop's own `link_score` (no multi-hop
composite score exists anywhere in this codebase); `ROUTE_UPDATE.reason`
reports `UNKNOWN` outside the priority/expiry cases `routing_core` can
actually distinguish (5 of the contract's 8 `routeReason` values have no
firmware source to report honestly); `STATISTICS.endToEndLatencyMs`
actually reports per-hop ACK latency, not a true multi-hop measurement (no
such mechanism exists); `HELLO.mac` is omitted at boot until
`transport::begin()` succeeds; `HELLO.config.ewmaAlpha` reports one of two
real EWMA constants this project has (RSSI's, not PDR's); `NODE_JOIN`/
`NODE_LEAVE` have no firmware event source at all (routing tracks
liveness, but nothing distinguishes "first contact" from ordinary beacon
traffic). UCB1 has no dedicated telemetry message (the frozen contract
defines none, and Part 13 of this phase's instructions explicitly forbade
inventing one) — its effect is already visible through `ROUTE_UPDATE`
whenever `ENABLE_UCB1=1`.

**`STATISTICS`'s counters are real but currently read their honest neutral
defaults** (`pdr:1.0`, all packet counts `0`) on any fresh boot, because no
`MSG_DATA` traffic flows yet — see the "Application traffic" entry below
and [hardware-readiness.md](hardware-readiness.md)'s Part F. Not a bug;
exactly the same gap Phase 4/5 already documented, now visible in the
telemetry output itself rather than only in internal counters.

## Phase 3 anomaly engine — OLED wiring deliberately deferred

implementation-guide.html §06's roadmap bundles "wire both flags to the
OLED on Node C" into the same Hours 12-17 bucket as the anomaly algorithm
itself. This phase implements the algorithm and real Serial logging
(`[ANOMALY]` lines) but does **not** add an OLED driver library or wire
anything to Node C's display — doing so would mean adding a new external
Arduino library dependency (Adafruit_SSD1306/GFX or U8g2) that doesn't
exist in this project yet, the same open question Phase 0 already
deferred with the same reasoning. See
[decisions.md](decisions.md#anomaly-detection-scope-no-oledtelemetry-wiring-in-phase-3).
Revisit when a future phase takes on the reporting/dashboard layer.

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

## Phase 4 reliability layer — not yet run on hardware, and no live application traffic yet even in principle

Everything in `src/reliability/` (packet identity, unicast send, ACK,
bounded retry, timeout, duplicate filter, forwarding, PDR wiring) is
verified two ways: a host-compiled, actually-executed unit test suite
(`firmware/PredictiveMesh/test/test_reliability_core.cpp` — 88/88 checks,
see `docs/testing.md`) and a real `arduino-cli` compile of the whole
sketch (including, for the first time in this project, real
`esp_now_send()` calls for genuine unicast traffic). Neither is a
substitute for the real thing — and this phase has an extra gap beyond
"no hardware exists yet": **no real application traffic exists to
generate even in principle**, since `reliability::send()` has no automatic
caller (see
[decisions.md](decisions.md#reliabilitysend-has-no-live-automatic-caller-in-phase-4--no-application-data-source-was-invented)).

- [ ] Two real boards exchange a genuine unicast `MSG_DATA`/`MSG_ACK` pair
- [ ] A real dropped frame (e.g. Faraday-bag attenuation, per the guide's
      own §06 demo) actually triggers a real retry, observed on the Serial
      monitor
- [ ] `RELIABILITY_ACK_TIMEOUT_MS` (currently a placeholder, 200ms) is
      tuned against real hardware round-trip timing
- [ ] Real multi-hop forwarding (e.g. A -> C -> D -> S) delivers correctly
      end-to-end
- [ ] `predictor`'s PDR reflects real, observed per-hop delivery ratios —
      not just the wired-but-unexercised mechanism
- [ ] A real application data source is decided and wired to
      `reliability::send()` (a real design decision, not a hardware test —
      but nothing above can be observed on hardware until one exists)

All `NOT RUN — HARDWARE NOT AVAILABLE` per the checklist at the top of
this file (except the last item, which additionally needs a real design
decision this phase deliberately did not make — see decisions.md).

## Phase 5 UCB1 adaptive routing — not yet run on hardware, and inherits Phase 4's "no live traffic" gap directly

`src/ucb1/` (bandit statistics, UCB1 selection formula, health-tiering,
loop-guard exclusion) is verified two ways: a host-compiled,
actually-executed unit test suite
(`firmware/PredictiveMesh/test/test_ucb1_core.cpp` — 26/26 checks, see
`docs/testing.md`) and a real `arduino-cli` compile of the whole sketch in
**both** `ENABLE_UCB1=0` and `ENABLE_UCB1=1` configurations. Neither is a
substitute for the real thing — and this phase inherits Phase 4's gap
directly and unavoidably: **UCB1's reward signal comes entirely from
`reliability::send()`'s outcomes, and nothing calls that automatically
yet** (see
[decisions.md](decisions.md#reliabilitysend-has-no-live-automatic-caller-in-phase-4--no-application-data-source-was-invented)).
Even with `ENABLE_UCB1=1` flashed to real boards, the bandit tables would
stay empty — there is no real delivery history for UCB1 to learn from
until that gap is resolved.

- [ ] `ENABLE_UCB1=1` flashed to real boards, with a real application
      traffic source (still undecided — see above) actually generating
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
this file (the first item additionally needs the same real design
decision — a real application traffic source — Phase 4 already flagged as
undecided; nothing UCB1-specific above can be observed until that exists
either).

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
- [ ] `HELLO.mac` populated with a real MAC once `transport::begin()`
      succeeds on real hardware (currently omitted at boot — honest, not a
      bug, but unverified in practice)
- [ ] Telemetry emission volume (6 periodic message types + event-driven
      ones) confirmed not to overwhelm a real 115200-baud UART or the
      GUI's own parsing loop at real sustained rates
- [ ] `TELEMETRY_OFFLINE_TIMEOUT_MS` (currently 3000ms, a placeholder
      derivation) confirmed sane against real multi-node boot/heartbeat
      timing

All `NOT RUN — HARDWARE NOT AVAILABLE` per the checklist at the top of
this file (the second item additionally needs the "no live MSG_DATA
traffic" gap from Phase 4 resolved before `STATISTICS`'s counters would
show anything but their neutral defaults — see
[hardware-readiness.md](hardware-readiness.md)'s Part F).

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
