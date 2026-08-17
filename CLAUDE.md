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

**Phase 6 complete** (2026-08-17) — pre-flash hardware readiness audit +
real firmware<->GUI JSON telemetry (`src/telemetry/`), on top of the Phase
0-5 firmware foundation. See [`docs/phase-log.md`](docs/phase-log.md) for
the full record. **Physical hardware now exists (5 boards) but nothing has
been flashed yet** — see [`docs/hardware-readiness.md`](docs/hardware-readiness.md).

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
(Phase 5). Routing, predictor, anomaly, reliability, and UCB1 all keep the
Arduino-free-core / thin-adapter split (`src/routing/`, `src/predictor/`,
`src/anomaly/`, `src/reliability/`, `src/ucb1/`, each `*_core.h/.cpp` +
adapter `.h/.cpp`), each unit-tested with a host-compiled g++ harness —
28/28 (routing), 31/31 (predictor), 50/50 (anomaly), 88/88 (reliability),
26/26 (ucb1) — **223/223 total**, see `docs/testing.md`. As of Phase 5,
the **whole Phase 0+1+2+3+4+5 sketch has been compiled for real in BOTH
`ENABLE_UCB1=0` and `ENABLE_UCB1=1` configurations** against the installed
`esp32:esp32` core 3.3.11 (`arduino-cli`) — 0 errors, 0 warnings, both
times. The repository's committed default is `ENABLE_UCB1=0`.

Nothing left in `src/` is a stub interface — telemetry is real as of this
phase. What's still not built: any automatic caller of
`reliability::send()` (the mechanism is real and tested, but no real
application data source was invented — see `docs/decisions.md`) — this
also means UCB1's bandit tables have nothing to learn from yet even when
enabled, and `STATISTICS`'s telemetry counters stay at their honest
neutral defaults, since the reward/counter signal comes entirely from
`reliability::send()`'s outcomes; OLED wiring for the anomaly flags (Node
C — deferred, same reasoning as Phase 0's original OLED deferral).

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
23-28); Phase 6 is the software-only prerequisite for the guide's next
roadmap block (Hours 28-32, "Live demo staging & stress testing," which
also requires physical hardware rehearsal this phase deliberately did not
attempt). Candidates for what comes next (none started, none scoped):
physical flashing (blocked on the teammate's real MAC mapping and the
board-label/node-ID question — see `docs/hardware-readiness.md`), deciding
what real `MSG_DATA` application traffic should flow (resolving the shared
Phase 4/5/6 "no live caller/no live data" gap), and OLED wiring (deferred
since Phase 0).

Things worth rereading before starting whatever's next:
- [`docs/hardware-readiness.md`](docs/hardware-readiness.md) — the
  physical board inventory (A/B/D/S/E) doesn't match the required logical
  node set (A/B/C/D/S); confirm with the hardware teammate which physical
  board plays the `NODE_C` role before filling in any MAC address or
  flashing anything.
- [`docs/decisions.md`](docs/decisions.md#reliabilitysend-has-no-live-automatic-caller-in-phase-4--no-application-data-source-was-invented) —
  the reliability mechanism (ACK/retry/duplicate-filter/forwarding/PDR
  wiring) is real and tested, but nothing calls `reliability::send()`
  automatically yet; deciding what real application data a node should
  send, to whom, and how often is a real, undecided design question — not
  something to invent unprompted. UCB1 (Phase 5) and telemetry's
  `STATISTICS` message (Phase 6) both inherit this exact same gap.
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
