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

**Phase 3 complete** (2026-08-17) — sensor anomaly/failure detection
(MAD-Z + flatline + a 6-state sensor health machine), on top of the
Phase 0-2 firmware foundation. See [`docs/phase-log.md`](docs/phase-log.md)
for the full record.

What exists and works: Arduino sketch structure (`firmware/PredictiveMesh/`),
centralized node identity (`THIS_NODE_ID` in `src/config.h`), the
`MeshPacket` wire format, a real ESP-NOW transport layer, structured Serial
logging (Phase 0); a real distance-vector routing table with
HELLO/route-advertisement beacons, staleness expiry, and the priority-flag
override (Phase 1); a real predictor — per-neighbor RSSI EWMA +
least-squares slope, PDR EWMA, a fused `link_score`, a two-threshold
debounced hysteresis state machine, and an independent staleness fast-path,
feeding into NORMAL route selection (Phase 2); and now a real sensor
anomaly engine — boot-time median/MAD calibration (with a variance safety
envelope and bounded retry), a modified-Z-score spike/jump detector, an
independent flatline/stuck detector, and a debounced
`WARMUP/NORMAL/ANOMALY/FLATLINE/STALE/INVALID` state machine, with local
telemetry (`anomaly::getTelemetry()`) and events, explicitly kept separate
from routing/link health (Phase 3). Routing, predictor, and anomaly all
keep the Arduino-free-core / thin-adapter split
(`src/routing/routing_core.h/.cpp` + `routing.h/.cpp`,
`src/predictor/predictor_core.h/.cpp` + `predictor.h/.cpp`,
`src/anomaly/anomaly_core.h/.cpp` + `anomaly.h/.cpp`), each unit-tested
with a host-compiled g++ harness — 21/21 (routing), 31/31 (predictor),
50/50 (anomaly) checks passing, see `docs/testing.md`. As of Phase 3, the
**whole Phase 0+1+2+3 sketch has also been compiled for real** against the
installed `esp32:esp32` core 3.3.11 (`arduino-cli`) — 0 errors, 0 warnings.

What's stubbed (interfaces only, no algorithms): `reliability/`,
`telemetry/`. Also not yet built: actual hop-by-hop relaying of a received
`MSG_DATA` packet (routing decides a next hop; nothing acts on that
decision for someone else's packet yet — that's the reliability layer's
job), live PDR evidence (`predictor::onSendResult()` is implemented and
tested but has no live caller yet — no real unicast traffic exists to
measure), and OLED wiring for the anomaly flags (Node C — deferred, same
reasoning as Phase 0's original OLED deferral). See `docs/decisions.md`.

**Unresolved, needs your input:** Phase 3's task instructions referenced an
external "GUI telemetry contract" (specific message types, GUI panels for
sensor health) that does not exist anywhere in this repository — flagged
explicitly rather than fabricated. See
[`docs/known-issues.md`](docs/known-issues.md#gui-telemetry-contract--referenced-by-the-phase-3-task-spec-not-found-anywhere-in-this-repository).
Share the real contract, or confirm none exists yet, before any future
phase claims wire-format compatibility with a GUI.

Full doc set lives in [`docs/`](docs/): `architecture.md`, `decisions.md`,
`protocol.md`, `parameters.md`, `testing.md`, `phase-log.md`,
`known-issues.md`. Read `architecture.md` first — it also teaches the
Arduino-build-system mechanics (`src/` subfolder compilation) this layout
depends on.

**Still outstanding regardless of phase:** the compile check above
validates that the firmware *builds*; nothing has run on real silicon yet
— no boards exist. See `docs/known-issues.md`.

## Next movement

**Waiting on explicit go-ahead for Phase 4** — do not start it
unprompted. Per the roadmap in implementation-guide.html §06 (Hours
17-23, "required, not stretch"), the next phase is the reliability layer:
hop-by-hop ACK, bounded retransmit, sequence-number duplicate filtering,
and Packet Recovery Ratio logging (§07).

Things worth rereading before starting Phase 4:
- [`docs/decisions.md`](docs/decisions.md#pdr-measurement-boundary-not-wired-to-live-send-outcomes-in-phase-2) —
  the reliability layer's hop-by-hop ACK mechanism is the natural point to
  finally wire `predictor::onSendResult()` to a real unicast delivery
  signal; don't build a second, parallel PDR mechanism instead.
- [`docs/decisions.md`](docs/decisions.md#link-health-integrated-into-routing_coreselectnexthop-alongside-not-instead-of-the-priority-only-edge-rule) —
  `routing_core::isPriorityOnlyEdge`'s hard exclusion is still active
  alongside real link-health-aware selection, specifically because no
  hardware exists yet to prove `link_score` alone would reproduce the same
  NORMAL-avoids-the-weak-direct-link behavior. Revisit only once real
  hardware/attenuation data exists to check that condition for real — not
  before.
- The GUI telemetry contract question above — resolve it before any phase
  claims wire-format compatibility with a GUI.

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
