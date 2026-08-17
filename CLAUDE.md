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

**Phase 1 complete** (2026-08-17) — distance-vector routing + priority
override, on top of the Phase 0 firmware foundation. See
[`docs/phase-log.md`](docs/phase-log.md) for the full record.

What exists and works: Arduino sketch structure (`firmware/PredictiveMesh/`),
centralized node identity (`THIS_NODE_ID` in `src/config.h`), the
`MeshPacket` wire format, a real ESP-NOW transport layer, structured Serial
logging (all Phase 0) — plus, as of Phase 1, a real distance-vector routing
table with HELLO/route-advertisement beacons, staleness expiry, and the
priority-flag override, split into an Arduino-free algorithm core
(`src/routing/routing_core.h/.cpp`, unit-tested with a host-compiled g++
harness — 18/18 checks passing, see `docs/testing.md`) and a thin Arduino
adapter (`src/routing/routing.h/.cpp`).

What's stubbed (interfaces only, no algorithms): `predictor/`, `anomaly/`,
`reliability/`, `telemetry/`. Also not yet built: actual hop-by-hop
relaying of a received `MSG_DATA` packet (routing decides a next hop;
nothing acts on that decision for someone else's packet yet — that's the
reliability layer's job).

Full doc set lives in [`docs/`](docs/): `architecture.md`, `decisions.md`,
`protocol.md`, `parameters.md`, `testing.md`, `phase-log.md`,
`known-issues.md`. Read `architecture.md` first — it also teaches the
Arduino-build-system mechanics (`src/` subfolder compilation) this layout
depends on.

**Still outstanding regardless of phase:** no `esp32:esp32` Arduino core is
installed anywhere accessible in this environment, so neither Phase 0 nor
Phase 1 firmware has been run through a real `arduino-cli compile`. See
`docs/testing.md`.

## Next movement

**Waiting on explicit go-ahead for Phase 2** — do not start it
unprompted. Per the roadmap in implementation-guide.html §06 (Hours 6-12),
Phase 2 is the fused link predictor: RSSI EWMA smoothing, least-squares
slope over window W, PDR sliding window, and the fused `link_score`
(§5.1), landing in `src/predictor/`. Anomaly (§5.2) and reliability (§5.4)
are later phases still.

Before touching `src/predictor/` for real: reread
[`docs/parameters.md`](docs/parameters.md) (the predictor timing table —
heartbeat interval, slope window W, PDR window, MAD-Z threshold, flatline
STUCK_N — documented but not yet wired into code) and
[`docs/decisions.md`](docs/decisions.md#a↔s-edge-modeled-as-priority-only-excluded-from-normal-selection)
(Phase 1's priority-only-edge special case in `routing_core` is meant to
be replaced by real link-quality-aware selection once `link_score` exists
— don't leave both mechanisms active at once without a documented reason).

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
