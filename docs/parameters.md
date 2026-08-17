# Parameters

Every configuration constant Phase 0 introduces, where it lives, and why.
All of these are defined exactly once — see
[decisions.md](decisions.md) for the "why centralize" reasoning where it's
not obvious.

## Node identity

| Parameter | Location | Values | Notes |
|---|---|---|---|
| `THIS_NODE_ID` | `src/config.h` | `NODE_A`, `NODE_B`, `NODE_C`, `NODE_D`, `NODE_S` | The **only** line that should differ between the five boards' source trees. Set before each board's compile/flash. |

## Radio

| Parameter | Location | Value | Notes |
|---|---|---|---|
| `MESH_WIFI_CHANNEL` | `src/config.h` | `6` (placeholder) | All five nodes must match. Not yet validated against real RF conditions — pick a channel with the least local WiFi congestion once hardware exists. Referenced by `transport::begin()` for `esp_wifi_set_channel()` and by `transport::addPeer()` for the peer's `channel` field. |

## Hardware pins

Fixed by implementation-guide.html §03. Every node (A, B, C, D, S) wires
identically; only OLED presence differs by role (see
[architecture.md](architecture.md)'s topology table).

| Pin | Macro | Function | Notes |
|---|---|---|---|
| GPIO34 | `PIN_SENSOR_POT` | Potentiometer wiper (ADC1_CH6) | Input-only pin — fine, read-only channel. **ADC1 only** — ADC2 pins stop working once WiFi/ESP-NOW is active. |
| GPIO35 | `PIN_SENSOR_LDR` | LDR divider midpoint (ADC1_CH7) | Input-only pin. ADC1 only, same reason. |
| GPIO25 | `PIN_BUZZER` | Piezo buzzer digital out | Regular output-capable GPIO. Never use 34/35/36/39 — they can't drive an output. |
| GPIO21 | `PIN_OLED_SDA` | OLED I2C data | Nodes S and C only. Default ESP32 I2C pin. |
| GPIO22 | `PIN_OLED_SCL` | OLED I2C clock | Nodes S and C only. Default ESP32 I2C pin. |
| — | `OLED_I2C_ADDRESS` | `0x3C` | SSD1306 128x64 default address. |

**Why ADC1, not ADC2:** the classic ESP32's ADC2 shares hardware with the
WiFi radio. Once `WiFi.mode()`/ESP-NOW is active, `analogRead()` on an
ADC2 pin returns garbage or an error — and only *after* the radio starts,
so it passes a naive pre-mesh bench test and then silently breaks. Both
sensors are wired to ADC1 pins specifically to avoid this. Reference:
implementation-guide.html §03's "single most likely 'sensor is lying' bug"
callout.

**Pins intentionally not used:** strapping pins (0, 2, 12, 15) — wrong pull
resistors here can prevent boot; flash-reserved pins (6–11) — not broken
out on most DevKit boards anyway.

## Packet format

| Parameter | Location | Value | Notes |
|---|---|---|---|
| `PACKET_PROTOCOL_VERSION` | `src/core/packet.h` | `1` | Bump if the `MeshPacket` layout ever changes. |
| `PACKET_MAX_PAYLOAD` | `src/core/packet.h` | `64` bytes | Chosen to comfortably cover small structured telemetry (a couple of ADC readings, a link score, a few flags) with 3x headroom under ESP-NOW's 250-byte frame ceiling. Single `#define` — raise later if needed, no protocol redesign required. |
| `PACKET_HEADER_SIZE` | `src/core/packet.h` (computed) | 17 bytes | `offsetof(MeshPacket, payload)`. |

## Routing (Phase 1)

| Parameter | Location | Value | Notes |
|---|---|---|---|
| `ROUTING_HELLO_INTERVAL_MS` | `src/config.h` | `1000` ms | How often each node broadcasts its distance-vector beacon (HELLO + route advertisement combined). Fast enough for a 5-node static topology to converge within a few cycles, slow enough not to spam the channel/log. Deliberately a separate constant from the predictor's future heartbeat cadence (100-200 ms, below) — routing convergence doesn't need that resolution. |
| `ROUTING_ENTRY_TIMEOUT_MS` | `src/config.h` | `3000` ms | Neighbor/route staleness cutoff, shared by both tables since both are refreshed by the same beacon. 3x the beacon interval — tolerates a couple of missed beacons before declaring a link down, matching the "3-5x reaction time" convention used for the predictor's timeout below. |
| `routing_core::MAX_HOP_COUNT` | `src/routing/routing_core.h` | `15` | RIP-style "unreachable" sentinel. Any computed distance at or above this is treated as infinity, not a real distance — bounds a corrupt/absurd advertisement from being stored as a plausible finite value. With 5 real nodes, any genuine route is at most 4 hops, so 15 is generous headroom, not a tuned value. |

## Predictor (Phase 2)

| Parameter | Location | Value | Notes |
|---|---|---|---|
| `PREDICTOR_RSSI_EWMA_ALPHA` | `src/config.h` | `0.3` | implementation-guide.html §5.1's exact stated value ("alpha ~ 0.3"). Higher = more reactive, lower = smoother. |
| `PREDICTOR_SLOPE_WINDOW` | `src/config.h` | `8` samples | Least-squares slope fit window. The guide's own reference (15-20 samples, ~2-4s) assumes a 100-200ms heartbeat; Phase 2 reuses the existing ~1s beacon as its RSSI sample source instead of adding a faster wire message, so the sample count is scaled down to keep the real-world reaction window in the same single-digit-second range. See [decisions.md](decisions.md#rssi-sample-cadence-reuses-the-existing-phase-1-beacon-not-a-new-fast-heartbeat). |
| `PREDICTOR_SLOPE_REF_DBM_PER_SAMPLE` | `src/config.h` | `1.5` dBm/sample | From the guide's `degrade_term = clamp(-slope/SLOPE_REF, 0, 1)` formula. The guide names `SLOPE_REF` but gives no numeric value — this is a starting/placeholder figure, expected to be re-tuned once real hardware attenuation testing (the guide's "Faraday bag on Node B" demo, §06) is possible. |
| `PREDICTOR_LINK_SCORE_W1` / `W2` | `src/config.h` | `0.5` / `0.5` | implementation-guide.html §5.1's exact stated fusion weights ("w1 = w2 = 0.5 to start"). |
| `PREDICTOR_PDR_EWMA_ALPHA` | `src/config.h` | `0.1` | Derived from the guide's stated 20-frame PDR window via the standard EWMA/SMA equivalence `alpha = 2/(N+1)` = `2/21 ≈ 0.0952`, rounded to 0.1. |
| `PREDICTOR_HYSTERESIS_T_LOW` / `T_HIGH` | `src/config.h` | `0.5` / `0.7` | Two-threshold hysteresis on `link_score` `[0,1]` (higher = healthier), replacing the guide's single `THRESHOLD` per the Phase 2 task spec's explicit requirement. `T_LOW` plays the role the guide calls `THRESHOLD`; `T_HIGH` is new. |
| `PREDICTOR_CONSECUTIVE_BAD_COUNT` / `GOOD_COUNT` | `src/config.h` | `3` / `3` | Consecutive-evaluation debounce, per the guide's own pseudocode ("reroute if below threshold for 3 consecutive evaluations"). Applied symmetrically to the recovery direction since the guide gives no separate figure for it. |
| `PREDICTOR_STALENESS_TIMEOUT_MS` | `src/config.h` | `2000` ms | Independent staleness fast-path timeout — deliberately faster than `ROUTING_ENTRY_TIMEOUT_MS` (3000ms) so the predictor's silence-detection can flag a dying link before routing's own hard fallback expires it. 2x the beacon interval (vs. routing's 3x). See [decisions.md](decisions.md#independent-staleness-fast-path-deliberately-bypasses-the-debounce). |

**How the three timeouts relate** (`ROUTING_HELLO_INTERVAL_MS` = 1000ms is
the shared cause; the other two are effects, racing each other on purpose):

```
0ms                 1000ms               2000ms               3000ms
|--- beacon interval ---|                    |                    |
|                        |--- predictor staleness (2x) fires here -->|
|                                             |--- routing hard-expiry (3x) fires here -->|
```

The predictor's fast path is meant to win this race — it's the "proactive"
half of implementation-guide.html's own framing ("reroutes traffic before
a heartbeat timeout... heartbeat timeout stays armed regardless, as a hard
fallback").

## Serial

| Parameter | Location | Value | Notes |
|---|---|---|---|
| `SERIAL_BAUD_RATE` | `src/config.h` | `115200` | Standard rate for both logging and, later, Serial/WebSerial telemetry (§ implementation-guide.html "Reporting Layer"). |

## Peer MAC table

`core/node_id.h`'s `nodeTable()` carries a `mac[6]` field per node — **real,
team-confirmed addresses as of Phase 7** (previously all-zero placeholders;
see [decisions.md](decisions.md#real-mac-address-table-populated-physical-board-e-confirmed-as-logical-node_c)):

| Node | MAC |
|---|---|
| `NODE_A` | `C0:CD:D6:CF:B9:B4` |
| `NODE_B` | `88:57:21:E0:89:48` |
| `NODE_C` | `F4:65:0B:48:EE:AC` (the physical board previously labeled "E") |
| `NODE_D` | `C0:CD:D6:8D:B7:08` |
| `NODE_S` | `C0:CD:D6:CF:62:98` |

`registerConfiguredPeers()` in `main.cpp` still checks for the all-zero
sentinel and logs a warning instead of registering a unicast peer if it
ever sees one again (e.g. a sixth node added later without a real MAC
yet) — that guard is unchanged, it simply no longer triggers for any of
the current five. See
[decisions.md](decisions.md#broadcast-peer-as-the-phase-0-espnow-bootstrap)
for why a broadcast peer is also registered.

## Anomaly (Phase 3)

| Parameter | Location | Value | Notes |
|---|---|---|---|
| `ANOMALY_CALIBRATION_SAMPLE_COUNT` | `src/config.h` | `100` samples | implementation-guide.html §5.2 / boot-sequence diagram: "Buffer ~100 raw ADC samples per sensor (boot calibration window)". |
| `ANOMALY_MAD_FLOOR` | `src/config.h` | `3.0` ADC LSB | The guide's exact stated value: "~3 ADC LSB on a 12-bit ADC — real noise floor". |
| `ANOMALY_MODIFIED_Z_THRESHOLD` | `src/config.h` | `3.5` | Iglewicz & Hoaglin modified Z-score threshold — the guide's exact stated value ("\|z\| > 3.5"). |
| `ANOMALY_FLATLINE_EPS` | `src/config.h` | `2.0` ADC LSB | The guide names `EPS` in its pseudocode but gives no numeric value — a starting/placeholder figure, chosen just above the expected ADC quantization noise floor. |
| `ANOMALY_STUCK_N` | `src/config.h` | `50` samples | The guide's exact stated value ("STUCK_N ~ 50 samples"). |
| `ANOMALY_MAX_CALIBRATION_VARIANCE` | `src/config.h` | `400.0` LSB² | The guide's boot-sequence diagram describes a "variance within safety envelope?" check with no numeric envelope given — a starting/placeholder figure (std-dev ≈ 20 LSB), well above expected resting-noise variance, well below an actively-manipulated or floating-pin signal's. |
| `ANOMALY_CALIBRATION_MAX_RETRIES` | `src/config.h` | `10` | Bounds the guide's "restart calibration" loop, which its own diagram draws as an unconditional retry. See [decisions.md](decisions.md#boot-calibration-is-a-blocking-anomalyinit-step-with-bounded-not-infinite-retry). |
| `SENSOR_SAMPLE_INTERVAL_MS` | `src/config.h` | `150` ms | Matches the guide's stated main-loop cadence ("every evaluation cycle (~100-200 ms)"). |
| `ANOMALY_CALIBRATION_SAMPLE_INTERVAL_MS` | `src/config.h` | `10` ms | Deliberately faster than `SENSOR_SAMPLE_INTERVAL_MS` — keeps the ~100-sample boot calibration to ~1s/sensor instead of ~15s. See [decisions.md](decisions.md#calibration-uses-a-separate-faster-sample-interval-than-steady-state-evaluation). |
| `ANOMALY_CONSECUTIVE_COUNT` | `src/config.h` | `2` samples | Consecutive over-threshold samples required before the sensor STATE transitions NORMAL → ANOMALY. Not guide-specified (the guide's own MAD-Z pseudocode has no debounce); required by this phase's own task spec. See [decisions.md](decisions.md#debounceecovery-persistence-counts-for-anomaly-flatlines-own-anomaly_stuck_n-already-provides-entry-persistence). |
| `ANOMALY_RECOVERY_COUNT` | `src/config.h` | `2` samples | Consecutive under-threshold samples required to recover ANOMALY → NORMAL. Symmetric with `ANOMALY_CONSECUTIVE_COUNT`. |
| `ANOMALY_FLATLINE_RECOVERY_COUNT` | `src/config.h` | `2` samples | Consecutive non-flat samples required to recover FLATLINE → NORMAL — a single changed sample alone must not instantly clear FLATLINE. |
| `ANOMALY_STALE_TIMEOUT_MS` | `src/config.h` | `450` ms (`3 × SENSOR_SAMPLE_INTERVAL_MS`) | Independent, time-driven staleness check for a sensor's observation stream. In this phase's actual local-ADC wiring this will rarely fire (samples are read synchronously); the mechanism exists because `anomaly_core` is designed to be reusable for a sensor whose observations could genuinely stop arriving. See [decisions.md](decisions.md#sensor-abstraction-is-generic-sensorobservation-not-hardwired-to-the-potentiometerldr). |

## Reliability (Phase 4)

| Parameter | Location | Value | Notes |
|---|---|---|---|
| `RELIABILITY_MAX_RETRIES` | `src/config.h` | `3` retries (4 attempts total) | implementation-guide.html §5.4 says "bounded retransmit" with no numeric value. Matches this project's established "tolerate up to 3" convention (`PREDICTOR_CONSECUTIVE_BAD_COUNT`/`GOOD_COUNT`, routing's 3x timeout multiplier). Bounds *resends* beyond the original attempt — total attempts = `1 + RELIABILITY_MAX_RETRIES`. |
| `RELIABILITY_ACK_TIMEOUT_MS` | `src/config.h` | `200` ms | Not guide-specified — a starting/placeholder figure. Chosen so the worst case (`(1 + RELIABILITY_MAX_RETRIES) * RELIABILITY_ACK_TIMEOUT_MS` = 800ms) resolves well before `PREDICTOR_STALENESS_TIMEOUT_MS` (2000ms), so a dead link's PDR degrades via real ACK/retry failures before the predictor's own staleness fast-path would otherwise have to catch it from silence alone. |
| `RELIABILITY_MAX_PENDING` | `src/config.h` | `4` slots | Fixed-size pool of concurrently-tracked outgoing hop-transmissions (`NODE_ID_COUNT - 1`). No dynamic allocation, matching this project's established fixed-array convention. |
| `RELIABILITY_DUP_CACHE_SIZE` | `src/config.h` | `16` entries | Duplicate-detection cache (Part 6). Larger than `NODE_ID_COUNT` since each of the other 4 nodes can have more than one recent sequence in flight at once. A starting/placeholder figure. |
| `RELIABILITY_DUP_CACHE_TTL_MS` | `src/config.h` | `2000` ms | How long a recorded `(source, sequence)` identity stays "seen." Set comfortably above the worst-case in-flight window for one hop-transmission's own retries (`RELIABILITY_MAX_RETRIES * RELIABILITY_ACK_TIMEOUT_MS` = 600ms), so a legitimate retransmission is recognized as a duplicate for its whole real lifetime. |

See [decisions.md](decisions.md) for the full reasoning behind each value
and the retry/timeout/duplicate-filter/statistics semantics they drive.

## UCB1 adaptive routing (Phase 5 — stretch, optional)

| Parameter | Location | Value | Notes |
|---|---|---|---|
| `ENABLE_UCB1` | `src/config.h` | `0` (disabled) — required default | Compile-time feature flag. implementation-guide.html §06 frames this stretch phase as needing to be "gate[d] behind a compile flag so it can be disabled instantly if unstable." With it at 0, every UCB1 call site is compiled out entirely — Phase 1/2/4 routing/reliability behavior is byte-for-byte unchanged. Both `0` and `1` were independently validated via a real ESP32 compile (see [testing.md](testing.md)); the repository is left at `0`. |
| `UCB1_EXPLORATION_C` | `src/config.h` | `1.41421356` (`sqrt(2)`) | The `C` in `meanReward + C * sqrt(ln(N)/n)`. The guide gives no formula or coefficient for this stretch feature — `sqrt(2)` is the standard textbook UCB1 value (Auer, Cesa-Bianchi & Fischer, 2002), matching this project's own cited reference [10]. |

**Bandit state shape:** `ucb1_core::Ucb1State.arms[NODE_ID_COUNT][NODE_ID_COUNT]`
— one `ArmStats{everObserved, attempts, successes, failures}` per
`(destination, next-hop)` pair, fixed-size, no dynamic allocation. No time
field, no decay — see [decisions.md](decisions.md#no-decay--fixed-unbounded-in-time-but-bounded-in-size-counters-are-sufficient).

See [decisions.md](decisions.md) for the full reasoning behind the reward
definition (one trial per resolved hop-transmission series, never per
retry), the health-tiering/priority-bypass/loop-prevention ordering, and
why the compile flag's default must stay `0`.

## Telemetry (Phase 6 — firmware<->GUI wire serialization)

| Parameter | Location | Value | Notes |
|---|---|---|---|
| `FIRMWARE_VERSION` | `src/config.h` | `"1.0.0"` | New this phase — no versioning scheme existed before (known-issues.md flagged this as unsourced). A literal string bumped by convention only, not tied to git tags. |
| `TELEMETRY_HEARTBEAT_INTERVAL_MS` | `src/config.h` | `1000` ms | Matches the frozen GUI contract's own stated `HEARTBEAT` frequency exactly — reproduced, not re-derived. |
| `TELEMETRY_NODE_STATUS_INTERVAL_MS` | `src/config.h` | `1000` ms | Matches the contract's `NODE_STATUS` frequency. |
| `TELEMETRY_LINK_INTERVAL_MS` | `src/config.h` | `250` ms | Matches the contract's `LINK_UPDATE` frequency (4 Hz). |
| `TELEMETRY_PREDICTION_INTERVAL_MS` | `src/config.h` | `250` ms | Matches the contract's `PREDICTION` frequency (4 Hz). |
| `TELEMETRY_SENSOR_INTERVAL_MS` | `src/config.h` | `1000` ms | Matches the contract's `SENSOR_STATUS` frequency. |
| `TELEMETRY_STATISTICS_INTERVAL_MS` | `src/config.h` | `1000` ms | Matches the contract's `STATISTICS` frequency. |
| `TELEMETRY_OFFLINE_TIMEOUT_MS` | `src/config.h` | `3000` ms (`3 * TELEMETRY_HEARTBEAT_INTERVAL_MS`) | New this phase — reported in `HELLO.config.offlineTimeoutMs`, drives the GUI's own client-side staleness clock. Numerically equal to `ROUTING_ENTRY_TIMEOUT_MS` but conceptually distinct (a telemetry-reporting concern, not a mesh-routing one) — kept as its own constant rather than reused. |
| `telemetry_core::LINE_BUF_SIZE` | `src/telemetry/telemetry_core.h` | `768` bytes | An algorithm-intrinsic buffer bound (comfortably covers the largest real message with headroom under the contract's 4096-byte ceiling), not a deployment tunable — lives in the header, not this file, matching `routing_core::MAX_HOP_COUNT`'s own precedent. |

See [decisions.md](decisions.md) for the full reasoning behind every enum
mapping (link/prediction state classification, sensor health, route
reason), the `ewmaAlpha`/`endToEndLatencyMs` field-naming caveats, and why
`bootId` is a random nonce rather than a persistent counter.

## Application traffic (Phase 7 — demo workload for `reliability::send()`)

| Parameter | Location | Value | Notes |
|---|---|---|---|
| `APPLICATION_TX_INTERVAL_MS` | `src/config.h` | `2000` ms | How often `NODE_A` sends one application `DATA` packet to `NODE_S`. Not guide-specified — chosen above the reliability layer's own worst-case retry window (`(1 + RELIABILITY_MAX_RETRIES) * RELIABILITY_ACK_TIMEOUT_MS` = 800ms) and below a rate that would compete with `ROUTING_HELLO_INTERVAL_MS` (1000ms) every cycle. See [decisions.md](decisions.md#application_tx_interval_ms--2000ms). |
| `apptraffic_core::DATA_WIRE_SIZE` | `src/apptraffic/apptraffic_core.h` | `10` bytes | Fixed wire size of one application `DATA` payload (`appSeq:2, potValue:2, ldrValue:2, timestampMs:4`). Well under `PACKET_MAX_PAYLOAD` (64). See [protocol.md](protocol.md#application-data-payload-phase-7-rides-inside-msg_data). |

Priority trigger (a single Serial `'p'`/`'P'` byte read on `NODE_A`) has no
numeric parameter — see
[decisions.md](decisions.md#priority-traffic-trigger-a-single-serial-character-pp-read-on-node_a-not-a-new-command-protocol).

## OLED (local display — Nodes S and C only)

| Parameter | Location | Value | Notes |
|---|---|---|---|
| `OLED_SCREEN_CYCLE_MS` | `src/config.h` | `3000` ms | How long the auto-cycling display stays on each regular status screen. Not guide-specified — a starting/placeholder figure. |
| `OLED_EVENT_DISPLAY_MS` | `src/config.h` | `4000` ms | How long a real event override (Node S: a direct neighbor's link health just flipped HEALTHY/UNHEALTHY) takes over the display before resuming the normal auto-cycle. Longer than `OLED_SCREEN_CYCLE_MS` so it's genuinely noticeable. |
| `OLED_REFRESH_MIN_INTERVAL_MS` | `src/config.h` | `400` ms | Minimum real time between successive I2C pushes to the display, even with no screen change — the actual rate-limit. |

Node S and Node C run genuinely different Adafruit drivers (a real,
team-confirmed hardware fact, not a firmware choice) — see
[decisions.md](decisions.md#oled-integration-per-node-driver-selection-screen-content-and-why-polling-not-a-third-event-callback-slot):

| Node | Display | Controller | Library |
|---|---|---|---|
| S | 0.96" | SSD1306 | `Adafruit_SSD1306` |
| C | 1.3" | SH1106 | `Adafruit_SH110X` (`Adafruit_SH1106G`) |

Both use `OLED_I2C_ADDRESS` (`0x3C`, see "Hardware pins" above) and
`PIN_OLED_SDA`/`PIN_OLED_SCL` (GPIO21/22).

## Timing/threshold parameters — documented in implementation-guide.html, not yet wired into code

Nothing remains in this category as of Phase 7 — the predictor's timing
parameters were wired in Phase 2, the anomaly engine's in Phase 3, the
reliability layer's in Phase 4, UCB1's in Phase 5, telemetry's in Phase 6,
and application traffic's in Phase 7 (see the tables above). This section
is kept as a placeholder heading for whatever a future phase introduces
next.
