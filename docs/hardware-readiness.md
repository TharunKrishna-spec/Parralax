# Hardware Readiness — Pre-Flash Audit

Physical hardware exists as of 2026-08-17 (5 boards), but **nothing has been
flashed yet**. This document is the audit performed before any flashing —
covers node/board configuration, MAC/peer resolution, WiFi channel, and
application traffic. See [gui-compatibility-matrix.md](gui-compatibility-matrix.md)
for the firmware<->GUI wire-format audit, [system-map.md](system-map.md) for
the full interface-by-interface data-flow map,
[hardware-bringup.md](hardware-bringup.md) for the step-by-step bring-up
procedure, and [known-issues.md](known-issues.md) for the full running list
of what's still `NOT RUN — HARDWARE NOT AVAILABLE`.

**Update (2026-08-17, hardware bring-up sketches added):** the hardware
team's own bench-test sketches (`hardware code/0.96esp32node/0.96esp32node.ino`,
`hardware code/1.3esp32node/1.3esp32node.ino`) were added to the repository
(outside this session, in a commit ahead of this audit) and are now treated
as authoritative evidence for the physical hardware's actual sensor/OLED
wiring — see the new Part 3/4/5 section below. These are **bench-test
sketches only** (matching implementation-guide.html §03's own "Bench-test
order" callout — wire the sensor, confirm `analogRead()`, then wire the
OLED and confirm it responds), not the real mesh firmware; they will not be
flashed as a node's final image.

**Update (Phase 7, 2026-08-17): the board-label/node-ID question and the
MAC table are RESOLVED.** The team confirmed physical board "E" is logical
`NODE_C`, and provided the real MAC address for all five boards.
`core/node_id.h`'s `nodeTable()` now carries them for real. The
"⚠️ Board-label / node-ID mismatch" section, Part B items 11/12/26/27, and
Part C's tables below are kept with their original PENDING/BLOCKED
language struck through rather than deleted, so this document still shows
its own audit trail — see
[decisions.md](decisions.md#real-mac-address-table-populated-physical-board-e-confirmed-as-logical-node_c).
**Update (OLED integration pass, 2026-08-18): the OLED controller/size
contradiction is RESOLVED** — see Part 3/4/5 below. Everything else this
document flags (UART/RESET/BOOT pins, WiFi channel RF survey) remains
genuinely open.

## Physical board inventory as reported

| Label | SoC/module | Notes |
|---|---|---|
| A | ESP32-WROOM-32 | — |
| B | ESP32-WROOM-32 | — |
| D | ESP32-WROOM-32 | — |
| S | ESP32-WROOM-32 | — |
| E | classic ESP32 Dev Module | confirmed classic ESP32 SoC (not S2/S3/C3) |

## ⚠️ Board-label / node-ID mismatch — RESOLVED Phase 7 (board "E" confirmed as `NODE_C`)

The firmware topology (`core/node_id.h`), `implementation-guide.html` §01,
and the **frozen** GUI contract (`nodeId` is one of `A`, `B`, `C`, `D`, `S`)
all agree on exactly five logical node names: **A, B, C, D, S**. The
physical board inventory above uses **A, B, D, S, E** — there is no board
labeled "C", and "E" doesn't correspond to any logical node name anywhere
in this project.

**This is not something firmware can silently resolve** — per
`CLAUDE.md`'s standing rule, a real discrepancy against the source-of-truth
topology gets flagged and asked about, not guessed. The overwhelmingly
likely resolution is that physical board "E" is intended to be flashed as
logical **NODE_C** (the relay-with-OLED-and-primary-anomaly-sensor role) —
five physical boards, five logical roles, and "E" is the only physical
label left over once A/B/D/S are accounted for. But this is an assumption,
not a confirmed fact:

- If "E" is meant to play the C role: flash it with `#define THIS_NODE_ID
  NODE_C` in `config.h` — nothing else changes. `node_id.h`'s `NodeInfo`
  table already carries a real `name` string per logical role ("A".."S");
  the physical board's own inventory label ("E") never needs to appear
  anywhere in firmware, since `THIS_NODE_ID` is a compile-time role
  selection, not a board-serial mapping.
- If "E" is something else (a genuine 6th spare board, a different SoC
  variant meant to replace one of the other four, etc.) — that changes the
  MAC table below and needs to be confirmed before flashing.

~~**Action needed:** confirm with the hardware teammate that board "E" is
the board to flash as `NODE_C`, before filling in the MAC table below.~~
**Confirmed, Phase 7: board "E" is `NODE_C`.** The MAC table below is now
filled in for real.

## Part B — hardware assumption audit

| # | Item | Classification | Notes |
|---|---|---|---|
| 1 | Board/SoC target | REQUIRED BEFORE FLASH | FQBN `esp32:esp32:esp32` ("ESP32 Dev Module") used for every real compile so far (Phases 0-5); this is the generic classic-ESP32 board definition and is almost certainly correct for all 5 boards, including "E" (whose own label — "classic ESP32 Dev Module" — matches this FQBN's name closely). Confirm once boards are connected via `arduino-cli board list`. |
| 2 | ESP32-WROOM-32 compatibility | READY | The generic `esp32:esp32:esp32` FQBN is the standard target for WROOM-32 DevKit-style boards; no board-specific quirks assumed anywhere in firmware. |
| 3 | Classic ESP32 Dev Module (board "E") compatibility | REQUIRED BEFORE FLASH | Same FQBN as above is expected to work, but confirm flash size (this project assumes the default 4MB; `arduino-cli board list` reports real flash size once connected) and that it is genuinely classic-ESP32 (Xtensa LX6) silicon, not a mislabeled S2/S3/C3 variant — those use different receive-callback ABI/architecture and would break the "all five boards are identical architecture, no byte-order conversion needed" assumption documented in `architecture.md`'s "Practical theory notes". |
| 4 | FQBN | READY | `esp32:esp32:esp32`, used and verified clean across every phase's real compile, most recently this phase (both `ENABLE_UCB1` configs). |
| 5 | ESP32 core version | READY | `esp32:esp32` 3.3.11 installed and verified (`arduino-cli core list`). |
| 6 | WiFi configuration | READY | `WiFi.mode(WIFI_STA)` + `WiFi.disconnect()` in `transport::begin()` — unchanged since Phase 0, compiles and matches implementation-guide.html §04. |
| 7 | ESP-NOW channel | READY (mechanism) / NEEDS TEAM INPUT (value) | Single `MESH_WIFI_CHANNEL` (config.h, `=6`) referenced everywhere needed (`esp_wifi_set_channel()`, every `addPeer()` call). Still a placeholder value (`known-issues.md`) — pick based on local WiFi congestion at the actual flash/demo site. |
| 8 | Wi-Fi mode | READY | Station mode only, no AP — matches the guide, no change needed. |
| 9 | ESP-NOW initialization | READY | `esp_now_init()` + real callback registration, verified compiling against core 3.3.11 since Phase 1. |
| 10 | Peer registration | READY | Real MACs now populated (Phase 7) — `registerConfiguredPeers()` registers a real unicast peer for every neighbor; its all-zero-sentinel skip/warn path is unchanged but no longer triggers. See Part D below. |
| 11 | MAC address mapping | READY — real MACs provided (Phase 7) | See Part D. |
| 12 | Node ID mapping | READY — board "E" confirmed as `NODE_C` (Phase 7) | See the board-label mismatch above. |
| 13 | Maximum node count | READY | `NODE_ID_COUNT = 5`, matches the 5-board inventory exactly (once the E/C question above is resolved). |
| 14 | Routing node IDs | READY | `core/node_id.h`'s fixed topology (A/B/C/D/S) is unchanged since Phase 0; no redesign needed. |
| 15 | Sensor GPIO assumptions | READY | GPIO34 (pot, ADC1_CH6), GPIO35 (LDR, ADC1_CH7) — both ADC1-only pins, correctly avoiding the documented ADC2-dies-after-WiFi trap. |
| 16 | ADC assumptions | REQUIRED BEFORE MESH TEST | `analogReadResolution(12)` set explicitly in `anomaly::init()`; real behavior with ESP-NOW active is `NOT RUN — HARDWARE NOT AVAILABLE` (known-issues.md) — this is the single highest-risk untested assumption in the whole project per the guide's own "single most likely 'sensor is lying' bug" callout. |
| 17 | GPIO initialization | READY | `pinMode()`/`analogReadResolution()` calls compile clean; no strapping pins (0/2/12/15) touched anywhere. |
| 18 | Boot sequence | READY | `logger::begin()` → telemetry bootId → transport → peers → routing/predictor/anomaly/reliability init, in that order; compiles clean, matches the guide's boot-sequence diagram. |
| 19 | Reset behavior | REQUIRED BEFORE MESH TEST | No NVS/persistent-state usage anywhere in this firmware — every reboot is a genuinely clean boot (fresh routing table, fresh bandit stats if UCB1 enabled, fresh telemetry bootId/seq). Never run on real hardware yet. |
| 20 | Timing assumptions | REQUIRED BEFORE MESH TEST | All derived from `millis()`/beacon-interval math (documented in `parameters.md`); no real jitter/loss data exists yet to validate the derived timeouts. |
| 21 | millis()/timer assumptions | READY | Every `*_core` module takes `now` as an explicit parameter (never calls `millis()` itself) specifically so this is testable and so 32-bit `millis()` wraparound (~49.7 days) is handled via unsigned subtraction throughout — matches the existing convention project-wide, unchanged this phase. |
| 22 | Serial baud rate | READY | `115200`, matches the GUI contract's own frozen `Serial baud` value exactly — confirmed consistent, not just coincidentally equal. |
| 23 | UART logging | READY | Structured `[INFO]`/`[RX]`/`[TX]`/etc. logger output, unchanged; now interleaved with the new newline-delimited JSON telemetry lines on the same Serial stream (both are just `Serial.print*` calls — no separate UART used, matching the contract's own transport section). |
| 24 | Memory usage (RAM) | READY | 48,536 bytes (14%) with the new telemetry module, `ENABLE_UCB1=0` — comfortable headroom (279,144 bytes free). |
| 25 | Flash usage | READY | 914,988 bytes (69%) — comfortable headroom (395,732 bytes free). |
| 26 | Placeholder hardware values | PARTIALLY RESOLVED (Phase 7) | The MAC table is now real (Phase 7). `MESH_WIFI_CHANNEL` (6) remains a placeholder, still waiting on a real-world RF-congestion survey at the flash/demo site — not a code defect. |
| 27 | All-zero MAC placeholders | RESOLVED (Phase 7) | `core/node_id.h`'s `nodeTable()` now carries five real, team-confirmed MAC addresses — no node's `mac[]` is all-zero any longer. `registerConfiguredPeers()`'s skip/warn path is unchanged but no longer exercised for any of the current five nodes. |
| 28 | Simulation-only code accidentally enabled | NO CHANGE — confirmed clean | Grepped for any simulation/mock/fake data path in `firmware/PredictiveMesh/src/` — none exists; the GUI's own `serial-mock.py`/simulation mode lives entirely in `gui-main/` and is never referenced by firmware. |
| 29 | Mock data accidentally enabled | NO CHANGE — confirmed clean | Same check as above — no firmware code path fabricates a sensor reading, RSSI value, or delivery outcome; every telemetry field this phase added is sourced from real `*_core` state or explicitly omitted when unavailable (see gui-compatibility-matrix.md). |
| 30 | `ENABLE_UCB1` default | NO CHANGE — confirmed | Still `0` (disabled) — reconfirmed via `grep` after both real compiles this phase; restored build is byte-identical to the pre-toggle build (914,988/48,536 both times). |

## Part C — node configuration

**Authoritative configuration mechanism (already correct, no redesign
needed):** one shared source tree, compiled 5 times with only `THIS_NODE_ID`
(`src/config.h`) differing per board — this is already exactly "one clean
configuration point," matching implementation-guide.html §04's own stated
option ("compile-time flag ... over a MAC-address lookup table", the choice
already made and documented in Phase 0's `decisions.md`). **Do not create
five divergent firmware copies** — none are needed; this was already true
before this phase and remains true.

| NODE_ID | Node name | MAC address | Wi-Fi channel | Role | Node-specific GPIO |
|---|---|---|---|---|---|
| `NODE_A` | A | `C0:CD:D6:CF:B9:B4` (real, Phase 7) | 6 (`MESH_WIFI_CHANNEL`) | source | none (no OLED) |
| `NODE_B` | B | `88:57:21:E0:89:48` (real, Phase 7) | 6 | relay (primary path) | none (no OLED) |
| `NODE_C` | C | `F4:65:0B:48:EE:AC` (real, Phase 7 — physical board "E", confirmed) | 6 | relay (alternate path) | OLED (GPIO21/22, `0x3C`) |
| `NODE_D` | D | `C0:CD:D6:8D:B7:08` (real, Phase 7) | 6 | relay (alternate path) | none (no OLED) |
| `NODE_S` | S | `C0:CD:D6:CF:62:98` (real, Phase 7) | 6 | sink/root | OLED (GPIO21/22, `0x3C`) |

Every row shares: `SERIAL_BAUD_RATE=115200`, `PIN_SENSOR_POT=GPIO34`,
`PIN_SENSOR_LDR=GPIO35`, `PIN_BUZZER=GPIO25` (all five boards wire
identically per implementation-guide.html §03's own device table).

### Full per-node hardware matrix

Every field below is traced to a hardware file, firmware config, the
guide, or marked `UNKNOWN — TEAM INPUT REQUIRED`. Nothing is invented.

| Field | A | B | D | S | E / `NODE_C` (confirmed, Phase 7) |
|---|---|---|---|---|---|
| Logical `NODE_ID` | `NODE_A` | `NODE_B` | `NODE_D` | `NODE_S` | `NODE_C` — **confirmed, Phase 7** |
| Board/module | ESP32-WROOM-32 (reported) | ESP32-WROOM-32 (reported) | ESP32-WROOM-32 (reported) | ESP32-WROOM-32 (reported) | classic ESP32 Dev Module (reported) |
| ESP32 SoC | classic ESP32 (implied by WROOM-32) | classic ESP32 (implied) | classic ESP32 (implied) | classic ESP32 (implied) | classic ESP32, explicitly confirmed by the team |
| MAC address source | teammate's mapping (Phase 7, real) | same | same | same | same |
| MAC address | `C0:CD:D6:CF:B9:B4` | `88:57:21:E0:89:48` | `C0:CD:D6:8D:B7:08` | `C0:CD:D6:CF:62:98` | `F4:65:0B:48:EE:AC` |
| Wi-Fi channel | 6 (`MESH_WIFI_CHANNEL`, config.h — placeholder) | 6 | 6 | 6 | 6 |
| Wi-Fi mode | `WIFI_STA` (`transport::begin()`) | same | same | same | same |
| ESP-NOW role | source | relay (primary path) | relay (alternate path) | sink/root | relay (alternate path) |
| Sensor(s) | Pot + LDR | Pot + LDR | Pot + LDR | Pot + LDR | Pot + LDR (all 5 wire identically per §03) |
| Sensor GPIOs | 34 (pot), 35 (LDR) | 34, 35 | 34, 35 | 34, 35 | 34, 35 — confirmed against real hardware sketches (see Part 3/4/5 below) |
| OLED type | none | none | none | 0.96" — **confirmed, 2026-08-18** | 1.3" — **confirmed, 2026-08-18** |
| OLED controller | N/A | N/A | N/A | SSD1306 — **confirmed** (`Adafruit_SSD1306`) | SH1106 — **confirmed** (`Adafruit_SH110X`/`Adafruit_SH1106G`) |
| OLED I2C address | N/A | N/A | N/A | `0x3C` (firmware constant, used for both drivers; the 0.96" bring-up sketch's own `0x78` is that sketch's own likely bug — see Part 3/4/5) | same |
| OLED SDA | N/A | N/A | N/A | GPIO21 (firmware constant + ESP32 Arduino default) | same |
| OLED SCL | N/A | N/A | N/A | GPIO22 (same) | same |
| UART TX/RX | UNKNOWN — TEAM INPUT REQUIRED (fixed by board design, not yet confirmed against silkscreen) | same | same | same | same |
| RESET/EN | UNKNOWN — TEAM INPUT REQUIRED | same | same | same | same |
| BOOT | UNKNOWN — TEAM INPUT REQUIRED | same | same | same | same |
| Serial baud | 115200 | 115200 | 115200 | 115200 | 115200 |
| Firmware config | `THIS_NODE_ID NODE_A` | `THIS_NODE_ID NODE_B` | `THIS_NODE_ID NODE_D` | `THIS_NODE_ID NODE_S` | `THIS_NODE_ID NODE_C` |
| GUI identity (`nodeId`) | `"A"` | `"B"` | `"D"` | `"S"` | `"C"` |

See [system-map.md](system-map.md) for how this identity flows through
every layer above the hardware, and
[decisions.md](decisions.md#board-labelnode-id-mismatch-flagged-not-silently-resolved-phase-6)
for the E/C reasoning.

## Part D — MAC address / ESP-NOW peer resolution

**Audited, no redesign needed.** `NodeId -> MAC` resolution is already a
single authoritative table: `core/node_id.h`'s `nodeTable()`, one `mac[6]`
field per `NodeInfo`, looked up everywhere via `nodeInfo(id).mac` (routing
advertisement targets excluded — beacons stay broadcast; `reliability.cpp`'s
`transmitHop()`/`sendAck()` and `main.cpp`'s `registerConfiguredPeers()` are
the only real per-neighbor unicast call sites, and both already resolve
through this one table, never a hardcoded MAC literal anywhere else in
`src/`).

Peer registration: `registerConfiguredPeers()` iterates `neighborsOf(THIS_NODE_ID)`
and calls `transport::addPeer(mac)` for each, skipping (with a `[WARN]` log)
any neighbor whose MAC is still the all-zero sentinel. Once real MACs are
filled in, this requires **no code change** — it starts registering real
unicast peers automatically. Confirmed by the adjacency table
(`neighborsOf()`, unchanged since Phase 0) that resolution is symmetric
where the topology requires it (e.g. A's neighbor list includes B, and B's
includes A) — every edge in implementation-guide.html §01's diagram is
present in both directions.

**Do not fabricate MAC addresses.** None were invented this phase (Phase
6) or the next (Phase 7) — the sentinel remained all-zero until the
teammate provided the real mapping directly, which `core/node_id.h`'s
`nodeTable()` now carries verbatim (see
[decisions.md](decisions.md#real-mac-address-table-populated-physical-board-e-confirmed-as-logical-node_c)).
The fill-in procedure this section originally documented (flash, read each
board's own logged MAC over Serial at boot, record, update, reflash) was
not needed in the end — the real mapping was provided directly rather than
harvested per-board — but remains accurate as a fallback procedure for any
future board (e.g. a spare, or a re-flash after a hardware swap).

## Part E — Wi-Fi channel

**Audited, no redesign needed.** `MESH_WIFI_CHANNEL` (`config.h`, currently
`6`) is the single authoritative constant — compile-time, referenced by
`transport::begin()`'s `esp_wifi_set_channel()` call and by every
`transport::addPeer()`'s `peer.channel` field, nowhere else. Guaranteed
identical across all five boards structurally, not by convention: all five
compile from the same source tree, and this constant is never touched by
`THIS_NODE_ID` or any other per-board setting. No duplicate channel
constant exists anywhere in `src/`. Still a placeholder value pending a
real RF-congestion survey at the flash/demo site (unchanged limitation,
tracked in `known-issues.md` since Phase 0).

## Part 3/4/5 — pin, sensor, and OLED audit against the real hardware bring-up sketches

Two bench-test sketches exist, named by OLED screen size, not by node
label — neither sketch references `THIS_NODE_ID`, a MAC address, WiFi, or
ESP-NOW in any way, so they provide **no evidence** for MAC/channel/node-ID
questions (those remain PENDING, unchanged). What they *do* provide real,
traceable evidence for:

### Pin-by-pin audit

| Signal | Hardware sketch | Firmware (`config.h`) | implementation-guide.html §03 | Match |
|---|---|---|---|---|
| POT (Channel A) | `POT_PIN = 34` (both sketches) | `PIN_SENSOR_POT = 34` | GPIO34, ADC1_CH6 | **YES** |
| LDR (Channel B) | `LDR_PIN = 35` (both sketches) | `PIN_SENSOR_LDR = 35` | GPIO35, ADC1_CH7 | **YES** |
| OLED SDA | not set explicitly — relies on `Wire`'s ESP32-Arduino default (GPIO21) | `PIN_OLED_SDA = 21` | GPIO21 | **YES, but implicit** — neither sketch calls `Wire.begin(SDA, SCL)` explicitly; correct only because it matches the ESP32 Arduino core's own default. Not yet confirmed against the actual board silkscreen. |
| OLED SCL | not set explicitly — same default (GPIO22) | `PIN_OLED_SCL = 22` | GPIO22 | **YES, but implicit** — same caveat. |
| OLED RESET | `OLED_RESET = -1` (both sketches — "no dedicated reset pin used") | firmware has no `OLED_RESET` concept (no OLED code exists yet) | not specified | N/A — consistent, no reset pin wired on either module |
| UART TX/RX | not present in either sketch (`Serial.begin(115200)` uses the board's default USB-UART) | `SERIAL_BAUD_RATE = 115200` | not pin-specified | **YES** for baud; TX/RX pins are fixed by the ESP32 DevKit board design (typically GPIO1/GPIO3), not firmware-configurable, **UNKNOWN — TEAM INPUT REQUIRED** to confirm against the actual boards' silkscreen |
| RESET/EN, BOOT | not present in either sketch | not applicable — fixed board-level pins, not firmware-configurable | not specified | **UNKNOWN — TEAM INPUT REQUIRED** (standard on ESP32 DevKit boards, but not yet confirmed against these specific 5 boards, especially board "E") |
| Buzzer | not present in either sketch (these are sensor/OLED-only bench tests) | `PIN_BUZZER = 25` | GPIO25 | not exercised by either sketch — no contradiction, just not tested here |
| LEDs / buttons | none referenced in either sketch | none defined in firmware | none specified | N/A — no LEDs/buttons in this project's design |

**Zero unexplained pin mismatches; zero BLOCKERs from this audit.** Every
firmware pin assignment that the sketches actually exercise (POT, LDR)
matches exactly.

### Sensor audit

- `analogRead()` on GPIO34/35, no explicit `analogReadResolution()` call in
  either sketch (defaults to the ESP32 Arduino core's own default, which is
  12-bit/0-4095 — matches firmware's explicit `analogReadResolution(12)`
  call in `anomaly::init()`, so no real discrepancy, just an implicit vs.
  explicit difference).
- Sample timing: both sketches sample at a fixed `delay(250)` per loop
  iteration (250ms) — firmware's `SENSOR_SAMPLE_INTERVAL_MS` is `150`ms.
  Different cadence, but this is expected and not a conflict: these are
  bench-test sketches for visual OLED readability (250ms is comfortable to
  read on a screen), not the real firmware's sampling loop — no code
  reuse or rate constraint is implied between them.
- **`SensorObservation` compatibility:** the sketches confirm real
  `analogRead()` values in the expected 0-4095 range are actually produced
  on these exact pins on this exact hardware — directly supporting
  `anomaly_core::SensorObservation{sensor_id, timestamp_ms, value, valid}`'s
  existing generic design; no firmware change needed.
- **Calibration-period question (explicitly asked):** `ANOMALY_CALIBRATION_SAMPLE_COUNT`
  (100 samples) at `ANOMALY_CALIBRATION_SAMPLE_INTERVAL_MS` (10ms) takes
  ~1s per sensor at boot — nothing in either bring-up sketch contradicts
  this being reasonable; both sketches show the sensors producing readings
  immediately at boot with no warm-up delay of their own. **Whether
  calibration can accidentally accept a stuck-at-boot sensor is already a
  documented, handled limitation, not a gap**: `anomaly_core::init()`'s
  variance safety envelope (`ANOMALY_MAX_CALIBRATION_VARIANCE`) specifically
  exists to catch a suspiciously *low*-variance calibration window and
  retry (bounded by `ANOMALY_CALIBRATION_MAX_RETRIES`) — but a sensor stuck
  at one exact value would itself present as near-zero variance, which is
  exactly the condition that envelope is designed to catch and retry
  against. It cannot distinguish "genuinely stable resting signal" from "a
  broken sensor stuck at one value" by variance alone (no algorithm could,
  from statistics alone) — after `ANOMALY_CALIBRATION_MAX_RETRIES` retries,
  firmware proceeds anyway with a loud `[WARN]` log rather than blocking
  boot forever (see `docs/decisions.md`'s existing Phase 3 entry). This
  real limitation was already documented before this audit; nothing new
  was found.

### OLED audit — two real, concrete findings

**Finding 1 (real bug, hardware team's own sketch): 0.96" sketch's I2C
address is very likely wrong.** `0.96esp32node.ino` calls
`display.begin(SSD1306_SWITCHCAPVCC, 0x78)`. Adafruit_SSD1306's `begin()`
expects a **7-bit** I2C address (0x3C is the SSD1306's real 7-bit address,
confirmed by implementation-guide.html §03/§06 and by `1.3esp32node.ino`'s
own comment on the same physical fact); `0x78` is the *8-bit write*
address (`0x3C << 1`), often silkscreened on cheap OLED breakout PCBs but
**not** what the Arduino `Wire`/Adafruit API expects. The sketch's own
comment — `"Default I2C address for 0.96" OLEDor3C"` — reads as the
author hedging on exactly this uncertainty. **Very likely this sketch as
committed will fail `display.begin()` on real hardware** ("OLED
Allocation Failed") even with correct wiring. `1.3esp32node.ino` gets this
exact same fact right (`"0x78 printed on the PCB translates to 7-bit 0x3C
in Arduino"`, then correctly uses `0x3C`) — the fix, if needed, is a
one-line change in the 0.96" sketch, but **that sketch is the hardware
team's own file, not part of `firmware/PredictiveMesh/`** — flagging this
for the team rather than silently editing hardware-test code outside this
project's own firmware tree. (Firmware's own `OLED_I2C_ADDRESS`, `config.h`,
is already `0x3C` — correct, unaffected by this.)

**Finding 2 (real, CONFIRMED contradiction, source-of-truth vs. hardware
evidence — resolved 2026-08-18): implementation-guide.html's BOM specifies
"2x 0.96" SSD1306 OLED" — two *identical* displays. The team has since
confirmed the physical boards genuinely carry two *different* OLED
modules — reading (b) from the two-reading analysis this section
originally posed, not reading (a):**

| | implementation-guide.html §03 BOM | Hardware bring-up sketches |
|---|---|---|
| Display count/size | 2x **0.96"**, identical | One sketch for **0.96"**, one for **1.3"** |
| Controller | **SSD1306** (only one ever named) | 0.96" sketch: **SSD1306** (`Adafruit_SSD1306`). 1.3" sketch: **SH1106** (`Adafruit_SH110X`/`Adafruit_SH1106G`) — a genuinely different controller chip requiring a genuinely different driver library |
| I2C address | `0x3C` (guide, both instances) | 0.96" sketch: `0x78` (likely wrong, see Finding 1). 1.3" sketch: `0x3C` (correct) |

**RESOLVED, 2026-08-18: reading (b) is the real, confirmed answer** — the
two physical OLED-equipped boards genuinely have two different modules
installed (Node S: 0.96" SSD1306; Node C: 1.3" SH1106), a real, permanent
deviation from the guide's "2x identical" BOM, confirmed directly by the
team rather than assumed. `src/oled/oled.cpp` now implements genuine
node-specific OLED handling (not "one library across all nodes") — both
`Adafruit_SSD1306` and `Adafruit_SH110X`/`Adafruit_SH1106G` are real
dependencies of `firmware/PredictiveMesh/` as of this pass, with the
correct driver selected per `THIS_NODE_ID` at runtime. See
[decisions.md](decisions.md#oled-integration-per-node-driver-selection-screen-content-and-why-polling-not-a-third-event-callback-slot).

**Remaining action:** whether the hardware team's own 0.96" bring-up
sketch (`hardware code/0.96esp32node/0.96esp32node.ino`, not
`firmware/PredictiveMesh/`) needs its own `0x78` fixed to `0x3C` — still
open, does not block firmware (which already uses `0x3C` for both
drivers).

## Part F — application traffic — RESOLVED Phase 7

At the time this audit was written (Phase 6), implementation-guide.html
and this repository were searched for a defined `MSG_DATA`
application-payload format or a specified traffic generator/schedule, and
**none existed** — the guide's own wording, `MessageType`'s comment
"application payload (sensor reading, anomaly flag, ...)," was
illustrative, not a specification. That gap was reported rather than
filled with an invented protocol, per this project's standing rule.

**Phase 7 update:** this session provided the missing specification
directly — `NODE_A -> NODE_S`, a small binary payload built from real
POT/LDR readings, and a deterministic priority trigger. `src/apptraffic/`
now implements it and calls `reliability::send()` for real; see
[decisions.md](decisions.md#phase-7--resolved-reliabilitysend-now-has-a-live-automatic-caller-node_a---node_s)
and [phase-log.md](phase-log.md)'s Phase 7 entry. This was **not** guessed
or invented — the original gap analysis above is kept as the historical
record of why nothing was built here before an explicit specification
existed.

Once flashed to real hardware, `STATISTICS.pdr`/packet counters,
`predictor`'s live PDR, and (if `ENABLE_UCB1=1`) UCB1's bandit tables will
accumulate real values instead of their honest neutral defaults — none of
that has actually run on hardware yet (see
[known-issues.md](known-issues.md)'s Phase 7 section).
