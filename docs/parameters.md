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

## Serial

| Parameter | Location | Value | Notes |
|---|---|---|---|
| `SERIAL_BAUD_RATE` | `src/config.h` | `115200` | Standard rate for both logging and, later, Serial/WebSerial telemetry (§ implementation-guide.html "Reporting Layer"). |

## Peer MAC table

`core/node_id.h`'s `NODE_TABLE` carries a `mac[6]` field per node,
currently all-zero for every node. All-zero is a **sentinel meaning "not
yet configured,"** not a real MAC — `registerConfiguredPeers()` in
`main.cpp` checks for it and logs a warning instead of registering a
unicast peer when it sees the sentinel. See
[known-issues.md](known-issues.md) for the fill-in procedure once hardware
exists, and
[decisions.md](decisions.md#broadcast-peer-as-the-phase-0-espnow-bootstrap)
for why a broadcast peer is registered in the meantime.

## Timing parameters — documented in implementation-guide.html, not yet wired into code

These belong to the predictor layer (§5.1), which isn't implemented in
Phase 0. Recorded here so they're not lost before that phase starts:

| Parameter | Starting value | Why |
|---|---|---|
| Heartbeat interval | 100–200 ms | Gives the slope estimator resolution. |
| Slope window W | 15–20 samples (~2–4 s) | Enough to see a trend without lagging the event. |
| PDR window | 20 frames | Matches ~2–4 s at the heartbeat rate above. |
| Heartbeat timeout | 3–5x predictor reaction (~1–2 s) | Safety-net fallback; proactive reroute should normally win. |
| RSSI EWMA alpha | ~0.3 | Higher = more reactive, lower = smoother. |
| MAD-Z threshold | 3.5 | Iglewicz & Hoaglin modified Z-score threshold. |
| Flatline STUCK_N | ~50 samples | Consecutive unchanged samples before flagging STUCK. |
