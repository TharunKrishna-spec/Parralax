# CLAUDE.md

Guidance for Claude Code (or any future session) working in this repository.

## What this project is

Firmware for a 5-node ESP32 predictive self-healing IoT mesh (hackathon
project). Hardware team is building the physical boards independently —
**this repo builds real Arduino-ESP32 firmware against an agreed hardware
contract, not a simulator.**

**Source of truth for architecture:** [`implementation-guide.html`](PERSONAL_DOCS/implementation-guide.html)
in `PERSONAL_DOCS/` (not the repo root — see
[`docs/known-issues.md`](docs/known-issues.md) for that discrepancy).
Topology, node roles, routing/predictor/anomaly/reliability design, packet
concepts, hardware pins, and ESP-NOW transport choice all come from that
document. Do not silently change any of it — if something in it looks
wrong or incomplete, document the concern in
[`docs/known-issues.md`](docs/known-issues.md) and ask before redesigning.

## Current state

**Phase 7.1 complete** (2026-08-17) — a red-team integration hardening
pass: fixed a real `ROUTE_UPDATE.hops`/`hopCount` contract-consistency bug
(new `routing_core::reconstructPath()`, a bounded/deterministic graph
search — never fabricated — that also resolves the Phase 6 GUI
topology-animation gap for both demo routes), fixed a real latent gap
where a health-driven reroute could never produce `ROUTE_UPDATE`/
`ROUTE_CHANGE` telemetry at all, extended `routeReason` with two real,
derivable values (`LINK_DEGRADATION`/`ROUTE_RECOVERY`), and fixed
`HELLO.mac` being omitted at first boot when the real MAC was actually
available earlier. Several other review findings (predictor/staleness
math, routing selection semantics, MAC table correctness, the Serial
priority trigger, OLED, hardware-doc consistency) were investigated and
confirmed NOT bugs — two doc-wording overclaims were tightened as a
result. See `docs/phase-log.md`'s Phase 7.1 entry and `docs/decisions.md`
for the full per-finding record. 360/360 host tests, both `ENABLE_UCB1`
compile configs clean. Builds on Phase 7's real application traffic:
(`src/apptraffic/`: `NODE_A` -> `NODE_S`, real POT/LDR payload,
Serial-triggered priority packet), on top of the Phase 0-6 firmware
foundation (routing/predictor/anomaly/reliability/UCB1/telemetry). See
[`docs/phase-log.md`](docs/phase-log.md) for the full record. The real
MAC address table is now populated (`core/node_id.h`), and the physical
board previously labeled "E" is confirmed as logical `NODE_C` — both
provided by the team this phase. **Physical hardware exists (5 boards,
real MACs known) but nothing has been flashed yet** — see
[`docs/hardware-readiness.md`](docs/hardware-readiness.md).

A **GUI implementation exists in this repository**, under
[`gui-main/`](gui-main/) — a teammate's, not this session's. Its own
frozen telemetry contract lives at
[`gui-main/gui-main/docs/gui-telemetry-contract.md`](gui-main/gui-main/docs/gui-telemetry-contract.md).
**Do not edit anything under `gui-main/`** (the HTML console, the two
Python bridge/mock scripts, or the contract doc) — see "Workflow rules"
below. Firmware now implements the contract for real (Phase 6) — all 10
message types, validated by a 94-check host test suite and by running real
firmware-generated JSON through the GUI's own unmodified parsing code (not
just static review). See
[`docs/gui-compatibility-matrix.md`](docs/gui-compatibility-matrix.md) for
the full field-by-field audit — one real, demonstrated GUI-compatibility
limitation was found and documented rather than worked around (the
topology diagram's route animation doesn't recognize a real 2-hop
`ROUTE_UPDATE`'s honestly-short `hops` array — distance-vector routing
can't know the full path; see
[`docs/decisions.md`](docs/decisions.md#guis-topology-animation-route-key-matching-doesnt-recognize-a-real-2-hop-route_update--flagged-not-worked-around-phase-6)).

What exists and works: Arduino sketch structure (`firmware/PredictiveMesh/`),
centralized node identity (`THIS_NODE_ID` in `src/config.h`), the
`MeshPacket` wire format, a real ESP-NOW transport layer, structured Serial
logging (Phase 0); a real distance-vector routing table with
HELLO/route-advertisement beacons, staleness expiry, and the priority-flag
override (Phase 1); a real predictor — per-neighbor RSSI EWMA +
least-squares slope, PDR EWMA, a fused `link_score`, a two-threshold
debounced hysteresis state machine, and an independent staleness fast-path,
feeding into NORMAL route selection (Phase 2); a real sensor anomaly
engine — boot-time median/MAD calibration, modified-Z-score spike/jump
detection, flatline/stuck detection, and a debounced
`WARMUP/NORMAL/ANOMALY/FLATLINE/STALE/INVALID` state machine, with local
telemetry and events, kept separate from routing/link health (Phase 3);
and now a real reliability layer — packet identity (`source`, `sequence`,
reusing existing `MeshPacket` header fields), real unicast ESP-NOW
transmission, an explicit application-level `MSG_ACK` distinct from the
raw ESP-NOW send callback, bounded retry with deterministic timeout, a
TTL-expiring duplicate filter, minimum loop-safe forwarding via the
existing routing decision, and — for the first time — `predictor`'s PDR
fed by real per-attempt delivery outcomes (Phase 4). And now, optionally
(`ENABLE_UCB1=1`), a UCB1 multi-armed-bandit layer that RANKS — never
replaces — NORMAL-traffic candidates routing already validated, learning
from real Phase 4 delivery outcomes (one trial per resolved hop-
transmission series, never per retry), with link-health preference and
loop-prevention both preserved and priority traffic structurally untouched
(Phase 5); real firmware<->GUI JSON telemetry — all 10 contract message
types, `bootId`/envelope `seq` distinct from `MeshPacket.sequence`, rate
limiting per `TELEMETRY_*_INTERVAL_MS`, validated by running real
firmware-generated JSON through the GUI's own unmodified parsing code
(Phase 6); and now real application traffic — `NODE_A` periodically sends
its own POT/LDR readings to `NODE_S` via `reliability::send()` (never a
raw `transport::send()`), NORMAL traffic routed by the existing
link-health-aware selection, PRIORITY traffic on a one-shot Serial `'p'`
trigger forcing the direct `A->S` hop (Phase 7). Routing, predictor,
anomaly, reliability, UCB1, telemetry, and apptraffic all keep the
Arduino-free-core / thin-adapter split (`src/routing/`, `src/predictor/`,
`src/anomaly/`, `src/reliability/`, `src/ucb1/`, `src/telemetry/`,
`src/apptraffic/`, each `*_core.h/.cpp` + adapter `.h/.cpp`), each
unit-tested with a host-compiled g++ harness — 28/28 (routing), 31/31
(predictor), 50/50 (anomaly), 88/88 (reliability), 26/26 (ucb1), 94/94
(telemetry), 29/29 (apptraffic) — **346/346 total**, see `docs/testing.md`.
The **whole sketch has been compiled for real in BOTH `ENABLE_UCB1=0` and
`ENABLE_UCB1=1` configurations** against the installed `esp32:esp32` core
3.3.11 (`arduino-cli`) — 0 errors, 0 warnings, both times, most recently
this phase (915,080 / 917,228 bytes flash). The repository's committed
default is `ENABLE_UCB1=0`.

Nothing left in `src/` is a stub interface. The one gap every phase since
Phase 4 documented — `reliability::send()` had no automatic caller — is
now resolved (`src/apptraffic/`); UCB1's bandit tables (when enabled) and
`STATISTICS`'s telemetry counters will accumulate real data once flashed,
instead of staying at their honest neutral defaults.

**OLED integration pass (2026-08-18, not phase-numbered — a standalone
addition after Phase 7.1):** `src/oled/` now exists and OLED is no longer
deferred. The team confirmed a real hardware fact this pass needed first:
Node S runs a 0.96" SSD1306, Node C a 1.3" SH1106 — two different
controllers, a real deviation from implementation-guide.html §03's "2x
identical" BOM (not silently resolved either direction — documented in
`docs/decisions.md`). `oled.cpp` selects the matching Adafruit driver per
`THIS_NODE_ID` at runtime (never a second per-node compile flag). Content
is grounded in the guide's own per-node text, not one generic layout: Node
S auto-cycles node identity and live `predictor::linkScore()` for each of
its own direct neighbors (the guide's "current best path"), with a
temporary override on a real neighbor HEALTHY↔UNHEALTHY transition; Node C
auto-cycles node identity and its two independent SPIKE/JUMP
(`SensorState::ANOMALY`) / STUCK (`SensorState::FLATLINE`) flags per
sensor (the guide's "shown independently"). `oled_core` (pure
screen-scheduling/rate-limiting logic) is host-tested, 22/22 checks;
`main.cpp` needed exactly 3 new lines (include + `init()` + `tick()`) —
zero changes to any existing single-subscriber event-callback wiring
(`telemetry`'s own `onRouteEvent`/`onLinkEvent`/etc. registrations are
untouched; OLED polls existing side-effect-free accessors instead of
adding a fourth subscriber). Compiles clean on a real ESP32 build, both
`ENABLE_UCB1` configs (957,292/50,376 bytes at the default `=0`). **Not
run on real hardware** — no display has actually shown a frame yet. See
`docs/decisions.md`'s OLED integration entry, `docs/testing.md`, and
`docs/hardware-readiness.md`'s Part 3/4/5.

**Presentation-focused GUI pass (2026-08-18, not phase-numbered — GUI-only,
no firmware touched):** the judge-facing primary view of
[`gui-main/gui-main/mesh-command-console.html`](gui-main/gui-main/mesh-command-console.html)
now shows, additively (nothing removed, EXPERT view untouched), a real
per-node role/online-status strip, independent real POT/LDR anomaly
readouts, and a persistent self-heal/link-degraded status line — all
sourced from telemetry fields that were already on the wire (`role`,
`status`, two independently-tagged `SENSOR_STATUS` messages,
`ROUTE_UPDATE`/`EVENT` `source`). This was the **second** explicit,
narrow, user-authorized exception to the standing "do not edit anything
under `gui-main/`" rule below (the first was the single-line `PACKET`
parse-warning fix) — see `docs/decisions.md`'s "Presentation-focused GUI
pass" entry for the full authorization record, what was verified vs.
assumed, and why Node C is shown with its real `RELAY` role (not a
fabricated "SENSOR" role — see that entry for the real-vs-requested
terminology reconciliation). Verified by running the entire real,
unmodified extracted `<script>` block against real firmware-generated
telemetry (16/16 checks) — see `docs/testing.md`. **Also found and
flagged, not fixed:** `config.h`'s `OLED_I2C_ADDRESS_C` is currently
`0x3C` in the uncommitted working tree, contradicting the team-confirmed
`0x78` this file's own OLED section and `docs/known-issues.md` already
document — needs a real I2C-scanner check before the next Node C flash,
not a guess in either direction. See `docs/known-issues.md`.

**Priority-broadcast milestone (2026-08-18, not phase-numbered):**
priority traffic (the existing one-shot Serial `'p'`/`'P'` trigger on
NODE_A) now goes through a new opportunistic-broadcast + overhearing +
RSSI-aware counter-based suppression mechanism
([`src/suppression/`](firmware/PredictiveMesh/src/suppression/)) instead
of the previous forced-shortest-hop unicast override. **This is a real,
deliberate, user-confirmed deviation from `implementation-guide.html`
§5.3** (which only ever describes priority as a routed unicast override —
never broadcast/overhearing/suppression) — confirmed via an explicit
architecture question before any code was written, not silently resolved;
see `docs/known-issues.md`'s top entry and `docs/decisions.md`'s
priority-broadcast entry for the full record. NORMAL traffic
(`reliability::send()`, unicast, ACK/retry/PDR, and the A↔S priority-
only-edge routing exclusion) is byte-for-byte unchanged — verified by
construction and by the full existing 410/410 host-test regression
staying green, unmodified. New: `MessageType::MSG_PRIORITY_BROADCAST`
(a real new wire type, not just `priority=1` on `MSG_DATA` — necessary so
`reliability::onPacketReceived()` never sees this traffic at all, avoiding
two competing priority mechanisms); `suppression_core`'s own 55/55 host
tests. **Real ESP32 compile NOT YET RUN** — `arduino-cli` isn't installed
in this session's environment; exact command is in `docs/testing.md`.
**Nothing hardware-verified** — spatial suppression's real-world behavior
is explicitly not claimed proven until it runs on the real five-node
hardware (see `docs/testing.md`'s 5-step incremental bring-up procedure).

**Final GUI integration audit for this milestone (2026-08-18, GUI-only, no
firmware touched):** a real, genuine gap was found and fixed — before
this pass, zero GUI code referenced any of the five new `PRIORITY_*`
event types (the generic EVENT handler logged them safely, but that's not
the same as representing the new broadcast/overhear/suppress/deliver
mechanism correctly). Fixed with a new, additive, real-telemetry-only
"Priority broadcast · live flow" panel in
[`gui-main/gui-main/mesh-command-console (1).html`](gui-main/gui-main/mesh-command-console%20%281%29.html)
— the **third** explicit, narrow, user-authorized exception to the
standing "do not edit anything under `gui-main/`" rule below. Verified
against real firmware-generated JSON run through the entire real,
unmodified extracted `<script>` block: 25/25 checks, including correct
per-node attribution (a real, easy-to-get-wrong schema fact: `EVENT.
payload.source` is always the packet's original sender, never the
reporting node — `envelope.nodeId` is authoritative) and zero PARSE
WARNING entries across 13 real/malformed/unknown-eventType/missing-field
messages. See `docs/decisions.md`'s "Final GUI integration audit" entry
and `docs/testing.md`.

Full doc set lives in [`docs/`](docs/): `architecture.md`, `decisions.md`,
`protocol.md`, `parameters.md`, `testing.md`, `phase-log.md`,
`known-issues.md`, `hardware-readiness.md`, `gui-compatibility-matrix.md`,
`system-map.md` (full interface-by-interface data-flow map),
`hardware-bringup.md` (step-by-step flash/test/troubleshooting procedure).
Read `architecture.md` first — it also teaches the Arduino-build-system
mechanics (`src/` subfolder compilation) this layout depends on. The
hardware team's own bench-test sketches live in `hardware code/` (not
`firmware/`) — treat as evidence, not something this session edits.

**Still outstanding regardless of phase:** the compile checks above
validate that the firmware *builds* and that its telemetry JSON is
schema-correct; nothing has run on real silicon yet — 5 boards physically
exist but none are flashed. See `docs/known-issues.md` and
`docs/hardware-readiness.md`.

## Next movement

**Waiting on explicit go-ahead for whatever comes next, including physical
flashing** — do not start anything unprompted. Phase 0-4 covers
implementation-guide.html §06's "required, not stretch" roadmap through
Hours 17-23; Phase 5 is that roadmap's only named stretch feature (Hours
23-28); Phase 6-7 are the software-only prerequisites for the guide's next
roadmap block (Hours 28-32, "Live demo staging & stress testing," which
also requires physical hardware rehearsal this phase deliberately did not
attempt). The two blockers Phase 7 needed team input for — the real MAC
mapping and the board-label/`NODE_C` question — are now resolved. Remaining
candidates for what comes next (none started, none scoped): physical
flashing (now unblocked on the software side, including OLED; still needs
the team's answer on UART/RESET/BOOT pin confirmation — see
`docs/hardware-readiness.md`). OLED is no longer an open item — `src/oled/`
is implemented (2026-08-18 pass), just not yet hardware-verified. Priority
delivery is also no longer an open item on the software side —
`src/suppression/` is implemented (2026-08-18 priority-broadcast
milestone), host-tested, but still needs (a) a real ESP32 compile — the
user's own `arduino-cli`, command ready in `docs/testing.md` — and (b) the
same physical bring-up flashing is waiting on generally.

Things worth rereading before starting whatever's next:
- [`docs/hardware-readiness.md`](docs/hardware-readiness.md) — the real
  MAC table and `NODE_C`=board-"E" confirmation are filled in (Phase 7);
  the OLED controller/size contradiction is now RESOLVED (Node S =
  SSD1306, Node C = SH1106, confirmed 2026-08-18 — see Part 3/4/5); the
  0.96" bench sketch's likely `0x78`/`0x3C` address bug is still a
  standing note for the hardware team's own sketch (doesn't block
  firmware, which already uses `0x3C`); UART/RESET/BOOT pins still need
  confirming against the physical boards before flashing.
- [`docs/decisions.md`](docs/decisions.md#phase-7--resolved-reliabilitysend-now-has-a-live-automatic-caller-node_a---node_s) —
  `reliability::send()` now has a real caller (`src/apptraffic/`, `NODE_A
  -> NODE_S`) as of Phase 7; UCB1's bandit tables and telemetry's
  `STATISTICS` message will both accumulate real data once flashed,
  purely as a consequence — no code change was needed in either.
- **`ENABLE_UCB1` must stay `0` (disabled) unless explicitly asked
  otherwise** — it's a stretch feature the guide itself only wants enabled
  "if ahead of schedule," and every UCB1-touching code path was built to
  be provably byte-identical to Phase 4 when disabled. Don't flip the
  default without being asked.
- [`docs/decisions.md`](docs/decisions.md#link-health-integrated-into-routing_coreselectnexthop-alongside-not-instead-of-the-priority-only-edge-rule) —
  `routing_core::isPriorityOnlyEdge`'s hard exclusion is still active
  alongside real link-health-aware selection, specifically because no
  hardware exists yet to prove `link_score` alone would reproduce the same
  NORMAL-avoids-the-weak-direct-link behavior. Revisit only once real
  hardware/attenuation data exists to check that condition for real — not
  before.
- [`docs/gui-compatibility-matrix.md`](docs/gui-compatibility-matrix.md) —
  the `ROUTE_UPDATE.hops` 2-hop limitation and its real, demonstrated
  effect on the GUI's topology-diagram animation. Not fixed this phase per
  explicit instruction not to modify the GUI; revisit only as a real,
  explicitly-requested design decision (link-state extension or a GUI-side
  change agreed with its owner), not a quick patch on either side.
- **GUI ownership is now a durable constraint, not just a Phase-4
  instruction** — see "Workflow rules" below.

## Workflow rules (durable — apply every session)

- **No `git commit` / `git push` / any commit creation, ever, unless the
  user explicitly asks in that exact message.** Inspecting `git status`/`log`/`diff`
  is fine. Report git status at the end of each phase; don't act on it.
- **No large installs or toolchain downloads (arduino-cli, board packages,
  compilers, etc.).** The user runs all big installs/CLI setup themselves.
  If a task needs a real compile check, hand the user the exact
  `arduino-cli` command to run (see `docs/testing.md`) rather than
  installing anything.
- **Do not deviate from the architecture in `implementation-guide.html`**
  — topology, node roles, routing/predictor/anomaly/reliability design,
  packet concepts, hardware pins, ESP-NOW as the transport. If a real
  conflict or gap is found, write it into `docs/known-issues.md` and ask;
  don't quietly redesign around it.
- **One phase at a time.** Each phase has an explicit scope (see
  `docs/phase-log.md` for what Phase 0 deliberately excluded). Don't
  implement a later phase's algorithm early just because it would be
  convenient to stub less.
- **Every non-obvious engineering decision goes in
  `docs/decisions.md`** — Decision / Reason / Alternatives considered / Why
  rejected / Impact / Phase-date. Don't let an assumption live only in
  chat.
- **Don't fake what isn't real yet.** No simulated RSSI, no invented MAC
  addresses treated as real, no "passing" hardware test that didn't
  actually run on hardware. Mark anything hardware-dependent as
  `NOT RUN — HARDWARE NOT AVAILABLE` until it actually runs.
- **Do not edit anything under [`gui-main/`](gui-main/)** — the HTML
  console (`mesh-command-console.html`), the two Python bridge/mock
  scripts (`serial-bridge.py`, `serial-mock.py`), the GUI's own README, or
  its telemetry contract
  (`gui-main/gui-main/docs/gui-telemetry-contract.md`). That's a
  teammate's code and its frozen contract, introduced ahead of Phase 4 —
  treat the contract as authoritative for what the GUI expects, and make
  firmware adapt to it in a future wire-serialization phase, never the
  other way around. If the contract genuinely needs to change, that's a
  conversation with the GUI owner, not a firmware-side edit.
