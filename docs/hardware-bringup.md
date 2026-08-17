# Hardware Bring-Up Guide

Operator/technician procedure for taking the 5-node mesh from "boards on
the bench" to "full demo running." Written to be followed literally, in
order.

**Update (Phase 7, 2026-08-17): both items that originally blocked
Section 4 are now RESOLVED — the real MAC table and the board-"E"=`NODE_C`
confirmation were provided by the hardware team.** `core/node_id.h`'s
`nodeTable()` already carries all five real addresses; Section 4 below is
no longer a "fill this in" step, only a "verify it's already correct"
step. This document previously described these as open blockers — kept
below only where the historical framing is still useful context, corrected
everywhere it stated current fact.

**Still genuinely open, confirm with the hardware team before or during
this procedure:** OLED controller/size for nodes S and C (Section 2), and
UART/RESET/BOOT pin behavior (standard for ESP32 DevKit boards, but not
individually confirmed against silkscreen — see
[hardware-readiness.md](hardware-readiness.md)). Neither blocks Section 7
(single-node `NODE_A` bring-up, no OLED involved).

---

## 1. System overview

Five boards, one shared firmware image (`firmware/PredictiveMesh/`),
differing only in one compile-time constant (`THIS_NODE_ID`, `src/config.h`).

```
        A (source)
       / \
      /   \  A-S: weak, direct, 1 hop (priority path only)
     B     \
     |      \
     S ------+
     |
     (via B: A -> B -> S, the normal path)

     A -> C -> D -> S  (backup path, used when B degrades)
```

| Node | Role | Direct neighbors | Has OLED | Physical label | MAC (confirmed, Phase 7) |
|---|---|---|---|---|---|
| A | source | B, C, S | no | A | `C0:CD:D6:CF:B9:B4` |
| B | relay (primary path) | A, S | no | B | `88:57:21:E0:89:48` |
| C | relay (backup path) | A, D | yes | **"E" (confirmed)** | `F4:65:0B:48:EE:AC` |
| D | relay (backup path) | C, S | no | D | `C0:CD:D6:8D:B7:08` |
| S | sink/root | B, D, A | yes | S | `C0:CD:D6:CF:62:98` |

A sends data toward S. Normally it goes A→B→S. If B's link degrades
(RSSI/PDR fusion drops below threshold), the predictor reroutes traffic
A→C→D→S before B's beacon actually times out — the "proactive" part of
this project. A priority-flagged packet always takes the direct A-S edge
instead, ignoring link quality — that edge is deliberately the weakest
link in the topology so the override is visibly different from normal
routing.

## 2. Hardware inventory

| Item | Qty | Notes |
|---|---|---|
| ESP32-WROOM-32 DevKit board | 4 | Nodes A, B, D, S |
| Classic ESP32 Dev Module | 1 | The board labeled "E" — confirmed as `NODE_C` (Phase 7) |
| 10kΩ linear potentiometer | 5 | One per node, wired to GPIO34 |
| LDR photoresistor + 10kΩ resistor (voltage divider) | 5 | One per node, wired to GPIO35 |
| 0.96" or 1.3" I2C OLED module | 2 | For S and C only — **size/controller unresolved, see hardware-readiness.md's Part 3/4/5.** Confirm which module is actually going on these two boards before wiring. |
| Piezo buzzer | 5 | GPIO25, all nodes wire it even though firmware doesn't drive it yet |
| Micro-USB or USB-C data cable | 5 | Must be a *data* cable, not charge-only — this is the single most common "board not detected" cause |

**Power:** each board is powered over its own USB connection during
bring-up. No shared power rail is assumed by firmware or this procedure.

**Programming:** each board is flashed individually over its own USB
connection (Section 5) — never all five plugged in and flashed at once.

## 3. Software prerequisites

| Requirement | Exact version used for every real compile so far |
|---|---|
| Arduino CLI | `1.5.2-rc.1` (or Arduino IDE 2.x with the same core installed — either works, this project's own real compiles all used `arduino-cli`) |
| ESP32 board core | `esp32:esp32` **3.3.11** — required specifically for the `esp_now_recv_info_t*` receive-callback signature (core 2.x lacks RSSI in the callback; the whole predictor layer depends on this) |
| FQBN | `esp32:esp32:esp32` ("ESP32 Dev Module") — used for every real compile this project has run, including this phase's board "E" (whose own label matches this generic FQBN's name) |
| Serial baud | `115200` — matches both the logger and the GUI telemetry contract |
| Arduino libraries | **None beyond the ESP32 core itself** for the real mesh firmware (`firmware/PredictiveMesh/`) — no ArduinoJson, no OLED library; deliberately kept dependency-free (see `docs/decisions.md`). The hardware team's own bench-test sketches (`hardware code/`) additionally need `Adafruit_GFX` + either `Adafruit_SSD1306` **or** `Adafruit_SH110X`, depending on which OLED module is confirmed (Section 2) — these are NOT required to build/flash the real mesh firmware, only the bench-test sketches. |

Do not install anything beyond this list. In particular, do not add an
OLED library to `firmware/PredictiveMesh/` — OLED wiring is not yet
implemented there (deferred since Phase 0), and the confirmed-controller
question (Section 2) needs answering before that library choice can even
be made correctly.

## 4. Node configuration

Real MACs and the board-C confirmation are already in the shared source
tree (`core/node_id.h`) — nothing to fill in. For each board, only one
line changes:

1. Open `firmware/PredictiveMesh/src/config.h`, find the line
   `#define THIS_NODE_ID NODE_S` (its current committed value — check
   before editing, don't assume).
2. Set it to exactly one of `NODE_A`, `NODE_B`, `NODE_C`, `NODE_D`,
   `NODE_S` — the one matching the physical board about to be flashed
   (using the table in Section 1: board "E" = `NODE_C`). **This is the
   only line that should ever differ between the five boards' compiled
   images.** Do not touch anything else in `config.h` for this step.
3. `src/core/node_id.h`'s `nodeTable()` — the shared MAC address book — is
   already correct and identical in every board's build; just sanity-check
   it once against Section 1's table before the first flash, don't edit it
   per-board.
4. Do not touch `MESH_WIFI_CHANNEL` (`config.h`, currently `6`) unless the
   team has picked a different channel after an RF survey at the actual
   flash/demo site — if you do change it, change it once, before flashing
   any board, so all five stay identical.
5. Leave `ENABLE_UCB1` at its default (`0`) unless explicitly told
   otherwise.

## 5. Flash procedure

**Flash boards one at a time. Never connect more than one board for
flashing at once.**

For each board, in this order — **A, then B, then D, then S, then the
board confirmed as C** (matches the incremental bring-up order in Section
20's philosophy below):

1. Disconnect every other board's USB cable.
2. Connect only the board being flashed.
3. Identify its COM port:
   ```
   arduino-cli board list
   ```
4. Set `THIS_NODE_ID` in `config.h` for this specific board (Section 4).
5. Compile:
   ```
   arduino-cli compile --fqbn esp32:esp32:esp32 firmware/PredictiveMesh --warnings all
   ```
   Confirm **0 errors, 0 warnings** before proceeding — do not flash a
   build with warnings.
6. Flash:
   ```
   arduino-cli upload -p <COM_PORT> --fqbn esp32:esp32:esp32 firmware/PredictiveMesh
   ```
7. Open the serial monitor at `115200` baud:
   ```
   arduino-cli monitor -p <COM_PORT> -c baudrate=115200
   ```
8. Confirm against Section 6's checklist before moving to the next board.
9. Label the physical board with its node letter (tape/marker) —
   implementation-guide.html's own bring-up checklist calls this out
   explicitly, and it's cheap insurance against mixing up boards later.

## 6. First-boot checklist

Expected Serial output, in order, on a healthy boot:

```
[INFO] ========================================
[INFO] Predictive Self-Healing IoT Mesh - Phase 7 firmware
[INFO] UCB1 adaptive routing: disabled (default)
[INFO] Node <X> initialized (role=<ROLE>)
[INFO] telemetry: init (mesh-json/v1 serialization, Phase 6, bootId=<X>-xxxxxxxx)
{"protocolVersion":"mesh-json/v1","type":"HELLO","nodeId":"<X>",...,"payload":{...,"mac":"XX:XX:XX:XX:XX:XX",...}}
[INFO] WiFi station mode set
[INFO] WiFi channel fixed to 6
[INFO] ESP-NOW initialized
[INFO] ESP-NOW callbacks registered
[INFO] Own MAC address: XX:XX:XX:XX:XX:XX (verify this matches core/node_id.h's NODE_TABLE entry for this node)
[INFO] Peer added: FF:FF:FF:FF:FF:FF   <- broadcast peer, always succeeds
[INFO] Peer added: XX:XX:XX:XX:XX:XX   <- one per direct neighbor (Section 1's table) — real MACs now register for real, Phase 7
[INFO] routing: init (distance-vector + priority override, Phase 1)
[INFO] predictor: init (RSSI EWMA/slope + PDR fused into link_score; independent staleness fast-path, Phase 2)
[INFO] anomaly: boot calibration complete for both sensors
[INFO] reliability: init (hop-by-hop ACK + bounded retry + duplicate filter + forwarding, Phase 4)
[INFO] apptraffic: init (NODE_A -> NODE_S demo workload, Phase 7, interval=2000ms)   <- NODE_A only; silent on every other node
[INFO] Phase 7 firmware ready - entering main loop
[DEBUG] alive uptime_ms=... free_heap=...
{"protocolVersion":"mesh-json/v1","type":"HEARTBEAT",...}    <- every 1000ms from here on
```

Note (Phase 7.1): `WiFi.mode(WIFI_STA)` and the real MAC read now happen
immediately before `telemetry::init()`, so the very first `HELLO` line
above genuinely carries the real MAC (`payload.mac`), not omitted as in
earlier phases.

| Line | Expected | Indicates failure if... |
|---|---|---|
| Node/role line | `Node <letter> initialized (role=<ROLE>)` | Wrong letter/role -> `THIS_NODE_ID` was set wrong before this build |
| `telemetry: init ... bootId=...` | A real bootId string, non-empty | Missing entirely -> build is stale/wrong binary |
| `HELLO`'s `payload.mac` | A real, non-`00:00:00:00:00:00` MAC, matching Section 1's table for this node | Missing or all-zero -> stop, this is a real firmware regression (Phase 7.1's MAC-before-HELLO fix broke) |
| WiFi channel line | `WiFi channel fixed to 6` (or the team's chosen channel) | `Failed to set WiFi channel` -> real hardware fault, stop and check the board |
| ESP-NOW init | `ESP-NOW initialized` | `esp_now_init() failed` -> real hardware fault |
| Own MAC address line | Matches the `HELLO` MAC above, and matches Section 1's table for this node | Mismatch against Section 1's table -> wrong board flashed as this `NODE_ID`, or a transcription error in `node_id.h` — stop and recheck before continuing |
| `Peer added:` lines | One per direct neighbor (Section 1), no `Peer MAC not yet configured` warnings | A `[WARN] Peer MAC not yet configured` line appearing -> the binary wasn't actually built from the current source tree (real MACs exist as of Phase 7) — rebuild and reflash |
| `apptraffic: init` line | Present **only** on the board flashed as `NODE_A` | Present on any other node -> `THIS_NODE_ID` was set wrong for that board |
| Main loop reached | `Phase 6 firmware ready - entering main loop` | Anything before this that halts (`while(true) delay(1000)`) -> transport init failed; a real `ERROR` JSON line (`TRANSPORT_INIT_FAILED`) is also emitted right before the halt |
| Periodic HEARTBEAT/etc. JSON lines | One per second at minimum | No JSON lines at all -> telemetry isn't running; check for a crash/reset loop instead |

**A boot loop** (repeating from the top every few seconds) is not
expected/normal at this stage — see Section 13's troubleshooting table.

## 7. Single-node test

For each board, alone (no other boards powered):

- [ ] Boots and reaches "entering main loop" within 5 seconds (matches
      implementation-guide.html's own §07 pre-flight checklist item)
- [ ] Serial output matches Section 6 exactly
- [ ] `analogRead()` on the potentiometer (via the hardware team's
      `0.96esp32node.ino`/`1.3esp32node.ino` bench sketch, or by watching
      `[ANOMALY]` log lines once the real firmware's boot calibration
      completes) sweeps 0-4095 as you turn it
- [ ] `analogRead()` on the LDR changes when you cover/uncover it
- [ ] **S and C only:** OLED responds — using the bench-test sketch
      matching whichever module was confirmed in Section 2, **after**
      fixing the `0x78`->`0x3C` address if using the 0.96" sketch (see
      hardware-readiness.md's OLED Finding 1)
- [ ] Telemetry JSON lines are well-formed (paste a line into any JSON
      validator, or just eyeball balanced braces) and contain this board's
      correct `nodeId`

## 8. Two-node test (A + B)

Power A and B together, both flashed with their correct real MACs filled
in and reflashed (Section 4/5's fill-in loop).

- [ ] A's log shows `[RX] src=B ...` — B's beacon is heard
- [ ] B's log shows `[RX] src=A ...` — symmetric
- [ ] Both logs show a real, non-zero, plausible RSSI value (negative dBm,
      roughly -30 to -80 depending on distance) — the first real evidence
      `info->rx_ctrl->rssi` is returning sane values on this hardware
- [ ] `[ROUTE]` log lines show both nodes converging on a route to each
      other (A: `dst=B next=B hops=1`; B has no reason to route to A
      specifically unless something targets it, but should show A as a
      live neighbor)
- [ ] Once real application traffic exists to test with (see
      hardware-readiness.md's Part F — **not resolved, needs a real
      MSG_DATA source before this specific check is meaningful**): a real
      unicast `MSG_DATA`/`MSG_ACK` exchange, visible as `[RELIABILITY] TX`
      / `[RELIABILITY] ACK matched` log lines
- [ ] `STATISTICS` telemetry line's `pdr` reflects real attempts once that
      traffic exists (stays at the neutral default `1.0` until then — not
      a failure)

## 9. Four-node test (A + B + D + S)

- [ ] Full distance-vector convergence: A's routing table shows both a
      route via B (2 hops) and a backup via D... wait, D can't be reached
      without C — **this test genuinely needs the fifth node (C) to reach
      the backup path** (A->C->D->S). With only A/B/D/S, the backup path
      is structurally unreachable (C is the only link between A and
      D/S's other side) — expect A's route to S to only ever show the
      via-B primary path in this specific 4-node subset. Don't mistake
      this for a bug.
- [ ] Priority packet (once real traffic exists) takes the direct A-S
      edge, visibly different from normal traffic's via-B path
- [ ] Faraday-bag (or equivalent RF attenuation) over B: predictor's
      `[PREDICTOR]` log lines show `link_score` falling in real time;
      once it crosses `T_LOW` (0.5) for 3 consecutive evaluations,
      `[ROUTE]` should show... again, without C, there's no real backup to
      reroute onto in this 4-node subset — expect this test to demonstrate
      *detection* (health going UNHEALTHY, visible in `LINK_UPDATE`'s
      `state` field over telemetry) but not a visible reroute until node C
      joins (Section 10)
- [ ] Removing the attenuation: link recovers, `[PREDICTOR]` shows the
      HEALTHY transition once score clears `T_HIGH` (0.7) for 3 consecutive
      evaluations

## 10. Fifth node test (add C)

- [ ] C (the board confirmed in Section 1/4) joins; A's routing table
      gains a real via-C candidate to S (3 hops, A->C->D->S)
- [ ] With B healthy: A still prefers the 2-hop via-B path (NORMAL
      selection prefers fewer hops among healthy candidates)
- [ ] With B attenuated (repeat the Faraday-bag test from Section 9, now
      with C in the mesh): A's route to S actually switches to via-C
      *before* B's heartbeat would have timed out — this is the real
      "reroute lead-time" metric implementation-guide.html §07 asks to be
      logged
- [ ] C's own OLED (once wired — not yet, see Section 2) would show local
      SPIKE/JUMP and STUCK flags; until then, watch C's own `[ANOMALY]`
      log lines and its `SENSOR_STATUS` telemetry instead
- [ ] If `ENABLE_UCB1=1` was explicitly requested for this test run: watch
      for UCB1's ranking to eventually prefer whichever path has the
      better real delivery history — **note this needs real `MSG_DATA`
      traffic to have anything to learn from (hardware-readiness.md's Part
      F) — with no live traffic, UCB1's bandit tables stay empty and its
      ranking is a no-op even when compiled in**

## 11. GUI test

Open `gui-main/gui-main/mesh-command-console.html` in Chrome or Edge (WebSerial
requires a Chromium-based browser). **Do not edit this file.**

Either:
- Click **Connect Hardware**, select the correct COM port at 115200 baud
  (requires a browser with WebSerial support, direct USB connection to one
  node — typically S, since it's the sink and the guide's own framing
  treats it as the mesh-telemetry-carrying node), or
- Run `python3 gui-main/gui-main/serial-bridge.py --source serial:<COM_PORT> --baud 115200`
  and click **Connect via Bridge** (`ws://localhost:8765`).

Verify every panel against real firmware output:

- [ ] "Firmware nodes" grid shows real node cards (name, status, firmware
      version, uptime) as `HELLO`/`HEARTBEAT` arrive
- [ ] "Authoritative link health" shows real RSSI/EWMA/slope/PDR/score for
      the A-B link specifically (the only link this panel visualizes —
      other real links are received and stored but not shown here, a
      known GUI-side limitation, not a firmware bug — see
      gui-compatibility-matrix.md)
- [ ] "Route candidates" shows the real candidate list from `ROUTE_UPDATE`
- [ ] **Known limitation, already verified and documented:** the topology
      diagram's animated path will **not** highlight for any real
      multi-hop route (2+ hops) — only the direct A-S edge could ever
      match. This was confirmed by running real firmware JSON through the
      GUI's own code before any hardware existed (see `testing.md`). Don't
      spend bring-up time debugging this as if it were new — it's a known,
      reported architecture gap, not something to fix live.
- [ ] "Prediction + hysteresis" shows real score/thresholds/staleness
- [ ] "Sensor health" shows real value/state/detector info
- [ ] Event log shows real `ROUTE_CHANGE`/`LINK_DEGRADING`/`LINK_FAILURE`/
      `SENSOR_ANOMALY`/`SENSOR_FAILURE` entries as they genuinely occur
- [ ] A sequence gap (unplug/replug the USB cable briefly) logs
      `SEQUENCE GAP`
- [ ] A reboot (power-cycle one node) logs `BOOT` (bootId changed)
- [ ] Statistics panel reflects real `STATISTICS` telemetry (stays at
      neutral defaults with no live traffic — expected, not a bug)

## 12. Failure tests

| Action | Expected firmware behavior | Expected GUI behavior | Recovery behavior |
|---|---|---|---|
| Power off one relay node (e.g. B) | Its neighbors' `[ROUTE]`/predictor staleness eventually expire its entries (`ROUTING_ENTRY_TIMEOUT_MS`=3000ms / `PREDICTOR_STALENESS_TIMEOUT_MS`=2000ms) | That node's status goes STALE after `HELLO.config.offlineTimeoutMs` (3000ms) with no new HEARTBEAT | Re-power it; it re-announces via its own next beacon, neighbors re-learn it |
| Restart one node (reset button) | Fresh boot — new `bootId`, routing table empty until beacons re-converge | GUI logs `BOOT` (bootId changed), telemetry history for that node resets | Automatic — nothing to do, this is expected reboot behavior |
| Sensor flatline (hold the pot still past `ANOMALY_STUCK_N`=50 samples) | `[ANOMALY]` transitions to FLATLINE, debounced entry | `SENSOR_STATUS.healthState` -> `FLATLINE`; on node C specifically, the GUI's dedicated anomaly-flag panel shows "STUCK" | Move the pot again; clears after `ANOMALY_FLATLINE_RECOVERY_COUNT`=2 non-flat samples |
| Sensor anomaly (twist the pot sharply) | `[ANOMALY]` transitions to ANOMALY after `ANOMALY_CONSECUTIVE_COUNT`=2 over-threshold samples | `SENSOR_STATUS.healthState` -> `ANOMALY`; on node C, the panel shows "SPIKE" | Clears after 2 under-threshold samples |
| Link degradation (Faraday bag over B) | Predictor's `link_score` falls; UNHEALTHY after 3 consecutive evaluations below `T_LOW` | `LINK_UPDATE.state` -> `DEGRADING` then `UNHEALTHY`; a `LINK_DEGRADING` then `LINK_FAILURE` EVENT fires | Remove the bag; HEALTHY after 3 consecutive evaluations above `T_HIGH` |
| Link disappearance (power off B entirely, not just attenuate) | Staleness fast-path forces UNHEALTHY immediately (bypasses the 3-sample debounce) once `PREDICTOR_STALENESS_TIMEOUT_MS` elapses with no packet | Same `LINK_FAILURE` EVENT, but faster than the gradual-degradation case | B re-powers; RSSI resumes, predictor re-evaluates from fresh evidence |
| ACK timeout (needs real `MSG_DATA` traffic — see Part F gap) | `RELIABILITY_ACK_TIMEOUT_MS`=200ms with no ACK -> `[RELIABILITY] ACK timeout - retrying` | `PACKET_RETRY` EVENT | Automatic — resends up to `RELIABILITY_MAX_RETRIES`=3 times |
| Retry exhaustion (same gap) | After the 4th total attempt with no ACK -> `[RELIABILITY] delivery FAILED` | `PACKET_DROP` EVENT | None — the packet is genuinely lost; a real application layer would need to decide whether to re-send at a higher level (not built — Part F gap) |
| Duplicate packet (same gap — needs real traffic, or a manual retransmit) | Hop-ACKed anyway (the hop itself succeeded), then dropped without re-delivery/re-forward | No dedicated EVENT (folded into `STATISTICS.duplicateCount`) | Automatic, no action needed |
| Route change (attenuate B with C in the mesh) | `[ROUTE-EVENT] CHANGED` | `ROUTE_UPDATE` + a `ROUTE_CHANGE` EVENT with real old/new hops and scores | Automatic once the underlying link recovers |

## 13. Troubleshooting

| Symptom | Likely cause | Check | Fix |
|---|---|---|---|
| Board not detected by `arduino-cli board list` | Charge-only USB cable, or driver missing (CP2102/CH340) | Try a known-good data cable; check Device Manager (Windows) for an unrecognized device | Swap cable; install the correct USB-serial driver for this board's onboard chip |
| Upload fails (`Failed to connect`) | Board not in bootloader mode | Some DevKit clones need BOOT held during upload | Hold BOOT while `arduino-cli upload` starts, release once "Connecting..." appears |
| Boot loop | Brownout (insufficient USB power, especially with OLED+radio both active), or a genuine firmware crash | Try a different USB port/cable/powered hub; check for a stack trace in the serial monitor | Use a powered USB hub if the host port can't supply enough current; if a real crash trace appears, that's a new firmware bug — capture the trace before doing anything else |
| No serial output at all | Wrong baud rate, or board isn't actually running this firmware | Confirm monitor is at 115200; confirm the upload actually succeeded | Re-flash; double check `SERIAL_BAUD_RATE` wasn't changed |
| No ESP-NOW discovery between two nodes | Different WiFi channels, or MACs not yet filled in (unicast peers) | Compare `WiFi channel fixed to N` in both logs; check for `Peer MAC not yet configured` warnings | Confirm `MESH_WIFI_CHANNEL` matches in both compiled images (it will, if both came from the same source tree); fill in real MACs |
| Wrong channel | `MESH_WIFI_CHANNEL` was edited between two boards' builds | Diff `config.h` at build time for each board | Always change `MESH_WIFI_CHANNEL` once, before flashing *any* board, never per-board |
| MAC mismatch (unicast fails even after filling in the table) | Real MAC recorded incorrectly (transposed digit, wrong node row) | Re-read the `Own MAC address:` line directly from that board's own boot log | Correct the specific row in `node_id.h`'s `nodeTable()`, reflash every board (the table is shared) |
| No ACK ever received | No live `MSG_DATA` traffic exists yet (Part F gap) — expected, not a bug, until the team defines real application traffic | Check whether anything is actually calling `reliability::send()` | Not a firmware bug to "fix" on the spot — this is the documented, open application-traffic gap |
| PDR stuck at the neutral default (1.0) | Same as above | Same check | Same |
| Route missing | Genuine staleness expiry (neighbor timeout), or a topology edge that was never wired (check `neighborsOf()` against the physical cabling/RF path) | `[ROUTE]` logs, `ROUTE_UPDATE` telemetry | Confirm the two nodes are actually within RF range and on the same channel |
| GUI shows no data at all | WebSerial/bridge not actually connected, or firmware isn't emitting JSON (old/wrong firmware flashed) | Check the GUI's own "connection" status text; check the raw serial monitor for JSON lines directly | Reconnect; reflash the correct build if JSON lines are absent |
| Malformed telemetry (`PARSE WARNING` in the GUI log) | A real bug in `telemetry_core`'s JSON construction (should not happen — 94/94 host-tested), or serial corruption/baud mismatch | Capture the exact raw line from the serial monitor | If it's a genuine encoding issue, check baud rate first; if a truly malformed JSON line is captured, that's a real regression — report it, don't work around it in the GUI |
| Wrong node identity (GUI shows the wrong letter, or two boards show the same letter) | Two boards were flashed with the same `THIS_NODE_ID`, or the E/C mapping was guessed instead of confirmed | Check each board's own `Node <X> initialized` boot line | Reflash with the correct, confirmed `THIS_NODE_ID` |
| OLED failure ("Allocation Failed") | Wrong I2C address (see hardware-readiness.md's Finding 1 — `0x78` vs `0x3C`), wrong controller library for the actual module, or a wiring fault | Run an I2C scanner sketch first (implementation-guide.html's own recommended bench step); confirm SDA=21/SCL=22 | Fix the address in the bench-test sketch to `0x3C`; confirm the module's actual controller (SSD1306 vs SH1106) matches the library in use |
| Sensor failure (pinned at 0 or 4095) | Floating/disconnected pin, or wired to the wrong GPIO (34/35 are input-only — fine for read, but confirm nothing else is trying to drive them) | Bench-test with the hardware team's own sketch first | Re-check the physical wiring against Section 2/hardware-readiness.md's pin table |

## 14. Go/No-Go checklist

```
[ ] All five boards identified
[ ] Correct SoC confirmed for board "E" (classic ESP32, not S2/S3/C3)
[ ] Correct FQBN confirmed (esp32:esp32:esp32) once boards connected
[ ] ESP32 core verified (esp32:esp32 3.3.11)
[ ] Node IDs unique — E/C mapping CONFIRMED WITH THE TEAM, not guessed
[ ] MACs recorded — real values from the team, filled into node_id.h
[ ] Channel identical across all five (structurally guaranteed by shared
    source tree — verify MESH_WIFI_CHANNEL wasn't hand-edited per board)
[ ] Sensor pins verified (GPIO34/35, confirmed against real hardware
    bring-up sketches)
[ ] OLED pins verified (GPIO21/22, confirmed against real hardware
    bring-up sketches, though only implicitly — no explicit Wire.begin()
    call in either sketch)
[ ] OLED controller verified — SSD1306 vs SH1106 CONFIRMED WITH THE TEAM,
    not guessed (see hardware-readiness.md's Finding 2)
[ ] OLED I2C address verified — 0x3C, and the 0.96" bench sketch's likely
    0x78 bug fixed if that module is the one actually used
[ ] Firmware compiles (0 errors, 0 warnings, both ENABLE_UCB1=0 and =1)
[ ] 317/317 host tests pass (223 pre-telemetry baseline + 94 new
    telemetry tests, all still passing — see testing.md)
[ ] GUI contract matches (9/10 message types verified clean against the
    GUI's own real parser; 1 known, documented limitation — ROUTE_UPDATE's
    topology-animation gap — not a blocker for anything except that one
    visual)
[ ] Telemetry verified (real JSON generated, schema-checked, run through
    the real GUI parser — all before any hardware existed)
[ ] Single-node boot verified (per board, Section 7)
[ ] A<->B verified (Section 8)
[ ] Four-node mesh verified (Section 9, with the caveat that a true
    backup-path test needs the fifth node)
[ ] Fifth node verified (Section 10)
[ ] Backup route verified (needs 5 nodes)
[ ] Priority route verified (needs real application traffic — Part F gap)
[ ] Prediction verified (Section 8/9's Faraday-bag test)
[ ] Reliability verified (needs real application traffic — Part F gap,
    or a manual/temporary test call to reliability::send() written and
    removed for this specific verification only, never left in place
    without being asked for)
[ ] GUI verified (Section 11)
[ ] Failure tests verified (Section 12)
```

## 20. Bring-up philosophy — incremental, never all-at-once

**Do not flash all five boards first and hope.** Bring the mesh up
incrementally, verifying and recording the result at every stage before
proceeding to the next:

```
Node A (alone)
  -> Node B (alone)
    -> A + B together
      -> Node D (alone)
        -> Node S (alone)
          -> four-node mesh (A + B + D + S)
            -> fifth node (C) joins
              -> full demo
```

Each arrow is a real checkpoint (Sections 7-10 above), not a formality —
a problem caught with two boards powered is minutes to debug; the same
problem caught for the first time with all five powered and the GUI
connected is much harder to isolate. This mirrors
implementation-guide.html's own Hours 0-2 "Bring-up" phase framing
exactly, just carried through the whole mesh rather than stopping at one
board.
