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

**Phase 5 complete** (2026-08-17) — UCB1 adaptive next-hop ranking, a
**stretch, optional feature disabled by default** (`ENABLE_UCB1=0` in
`src/config.h`), on top of the Phase 0-4 firmware foundation. See
[`docs/phase-log.md`](docs/phase-log.md) for the full record.

A **GUI implementation now exists in this repository**, under
[`gui-main/`](gui-main/) — a teammate's, not this session's. Its own
frozen telemetry contract lives at
[`gui-main/gui-main/docs/gui-telemetry-contract.md`](gui-main/gui-main/docs/gui-telemetry-contract.md).
**Do not edit anything under `gui-main/`** (the HTML console, the two
Python bridge/mock scripts, or the contract doc) — see "Workflow rules"
below. A full GUI-vs-firmware compatibility audit was performed before
Phase 4 (delivered directly to the user, summarized in
[`docs/known-issues.md`](docs/known-issues.md)): firmware implements none
of the GUI's wire format yet (no JSON serialization exists anywhere in
`src/`), though most of the underlying data the contract needs is now
real internally (especially sensor telemetry and, as of Phase 4,
reliability statistics).

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

What's stubbed (interfaces only, no algorithms): `telemetry/`. Also not
yet built: any automatic caller of `reliability::send()` (the mechanism is
real and tested, but no real application data source was invented — see
`docs/decisions.md`) — this also means UCB1's bandit tables have nothing
to learn from yet even when enabled, since its reward signal comes
entirely from `reliability::send()`'s outcomes; OLED wiring for the
anomaly flags (Node C — deferred, same reasoning as Phase 0's original
OLED deferral); and JSON serialization of any telemetry (the GUI's
contract, or otherwise, including UCB1's own diagnostic state). See
`docs/decisions.md`.

Full doc set lives in [`docs/`](docs/): `architecture.md`, `decisions.md`,
`protocol.md`, `parameters.md`, `testing.md`, `phase-log.md`,
`known-issues.md`. Read `architecture.md` first — it also teaches the
Arduino-build-system mechanics (`src/` subfolder compilation) this layout
depends on.

**Still outstanding regardless of phase:** the compile check above
validates that the firmware *builds*; nothing has run on real silicon yet
— no boards exist. See `docs/known-issues.md`.

## Next movement

**Waiting on explicit go-ahead for whatever comes next** — do not start
anything unprompted. Phase 0-4 covers implementation-guide.html §06's
"required, not stretch" roadmap through Hours 17-23; Phase 5 is that
roadmap's only named stretch feature (Hours 23-28), now implemented and
disabled by default. Candidates for what comes next (none started, none
scoped): the final telemetry/reporting system (the natural point to
decide what real `MSG_DATA` application traffic should flow — resolving
the shared Phase 4/5 "no live caller" gap — and wire
`reliability::getStatistics()`/UCB1 diagnostics into the now-real GUI
telemetry contract), and OLED wiring (deferred since Phase 0).

Things worth rereading before starting whatever's next:
- [`docs/decisions.md`](docs/decisions.md#reliabilitysend-has-no-live-automatic-caller-in-phase-4--no-application-data-source-was-invented) —
  the reliability mechanism (ACK/retry/duplicate-filter/forwarding/PDR
  wiring) is real and tested, but nothing calls `reliability::send()`
  automatically yet; deciding what real application data a node should
  send, to whom, and how often is a real, undecided design question — not
  something to invent unprompted. UCB1 (Phase 5) inherits this exact same
  gap — its bandit tables stay empty until this is resolved, even with
  `ENABLE_UCB1=1`.
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
