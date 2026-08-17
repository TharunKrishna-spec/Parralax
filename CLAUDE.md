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
instead of staying at their honest neutral defaults. What's still not
built: OLED wiring for the anomaly flags (Node C — deferred, same
reasoning as Phase 0's original OLED deferral).

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
flashing (now unblocked on the software side; still needs the team's
answers on OLED controller/size and UART/RESET/BOOT pin confirmation — see
`docs/hardware-readiness.md`), and OLED wiring (deferred since Phase 0).

Things worth rereading before starting whatever's next:
- [`docs/hardware-readiness.md`](docs/hardware-readiness.md) — the real
  MAC table and `NODE_C`=board-"E" confirmation are filled in (Phase 7),
  but the OLED controller/size contradiction (SSD1306 vs SH1106) and the
  0.96" bench sketch's likely `0x78`/`0x3C` address bug are still open team
  questions, and UART/RESET/BOOT pins still need confirming against the
  physical boards before flashing.
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
