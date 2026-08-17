# Known Issues

## Hardware not currently available

The physical ESP32 hardware described in the hardware contract does not
exist yet — it's being built independently by the hardware team. Firmware
is written against the agreed contract (see [parameters.md](parameters.md))
but the following cannot be validated until real boards exist:

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
