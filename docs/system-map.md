# System Map — Data Flow, Hardware to GUI

Every real interface boundary in the system, in the order data actually
crosses them. "Real" means: implemented and either host-tested or
ESP32-compiled — nothing here describes a planned-but-unbuilt interface
(those are called out explicitly as gaps). See
[architecture.md](architecture.md) for the prose version of this layering
and [gui-compatibility-matrix.md](gui-compatibility-matrix.md) for the
telemetry-specific field-by-field detail.

```
Hardware (ADC pins, radio)
  -> transport (ESP-NOW)
    -> main.cpp (parses MeshPacket, fans out to 3 consumers)
      -> routing        (distance-vector table, next-hop decisions)
      -> predictor       (RSSI/PDR -> link_score -> health)
      -> reliability      (ACK/retry/dup-filter/forwarding)
        -> ucb1 (optional, ranks routing's own candidates)
    -> anomaly (independent path: analogRead(), not packet-driven)
  -> telemetry (reads all of the above, never the reverse)
    -> Serial
      -> GUI (mesh-command-console.html, via WebSerial or the bridge)
```

## 1. Hardware -> firmware (ADC)

| | |
|---|---|
| Producer | Physical potentiometer (GPIO34) / LDR voltage divider (GPIO35) |
| Consumer | `anomaly::sample()` (`src/anomaly/anomaly.cpp`) |
| Data format | `analogRead()` raw integer |
| Units | ADC counts, 12-bit (0-4095) — `analogReadResolution(12)` set explicitly in `anomaly::init()` |
| Timing/rate | Sampled every `SENSOR_SAMPLE_INTERVAL_MS` (150ms) from `app::loop()`; boot calibration samples faster, every `ANOMALY_CALIBRATION_SAMPLE_INTERVAL_MS` (10ms), for `ANOMALY_CALIBRATION_SAMPLE_COUNT` (100) samples |
| Identifier | `anomaly::SensorId::POT` / `LDR` (adapter-assigned; `anomaly_core` treats it as an opaque `sensor_id`) |
| Failure behavior | No hardware failure detection at this boundary (a floating/disconnected pin just reads garbage ADC noise — indistinguishable from a real signal at this layer). Caught one layer up by `anomaly_core`'s calibration variance gate / MAD-Z / flatline detectors, not here. **NOT RUN — HARDWARE NOT AVAILABLE.** |

## 2. Hardware -> firmware (radio)

| | |
|---|---|
| Producer | ESP-NOW driver (`esp_now_register_recv_cb`) |
| Consumer | `main.cpp::onTransportRx()` |
| Data format | `transport::RxEvent{mac[6], rssi, data*, len, timestamp_ms}` |
| Units | RSSI in dBm (`int8_t`, from `info->rx_ctrl->rssi`); `len` in bytes |
| Timing/rate | Asynchronous, driven by real radio reception — no firmware-imposed rate |
| Identifier | Sender MAC (raw radio-layer identity; firmware immediately re-derives a `NodeId` from the packet's own `prev_hop` field, not from the MAC, once parsed) |
| Failure behavior | Frames shorter than `PACKET_HEADER_SIZE` are silently dropped (`if (evt.len < PACKET_HEADER_SIZE) return;`) — never partially parsed. **NOT RUN — HARDWARE NOT AVAILABLE.** |

## 3. `main.cpp` -> routing / predictor / reliability (packet fan-out)

| | |
|---|---|
| Producer | `main.cpp::onTransportRx()` |
| Consumers | `routing::onPacketReceived()`, `predictor::onPacketReceived()`, `reliability::onPacketReceived()` — same `(pkt, rssi)` handed to all three, independently |
| Data format | `MeshPacket` (packed struct, `core/packet.h`) — `memcpy()`'d from the raw radio buffer, never pointer-cast |
| Units | N/A (structured fields, see `protocol.md`) |
| Timing/rate | Once per received frame |
| Identifier | `pkt.source`/`pkt.prev_hop`/`pkt.next_hop`/`pkt.destination` — all `NodeId` (`uint8_t`, `core/node_id.h`); `pkt.sequence` (16-bit, per-source) is the packet identity for dup-filtering/ACK matching |
| Failure behavior | Each consumer independently ignores message types it doesn't care about (routing ignores non-HEARTBEAT for the route-update half; reliability ignores HEARTBEAT entirely) — no consumer can crash another |

## 4. routing (distance-vector table + next-hop decision)

| | |
|---|---|
| Producer | `routing_core::selectNextHop()` / `enumerateCandidates()`, called via the `routing::` adapter |
| Consumers | `reliability::send()` and `reliability`'s own forwarding path (both call `routing::selectNextHop(pkt)`/`getNextHop()`); `telemetry::onRouteEvent()` (event-driven); `ucb1::selectNextHop()` (only when `ENABLE_UCB1=1`, ranks `routing_core`'s own candidate list) |
| Data format | `NodeId` (the chosen next hop) + `uint8_t hopCount`; `routing::RouteEvent{type, destination, next_hop, hop_count, priority}` for the event stream |
| Units | Hop count is an integer distance, not a physical unit |
| Timing/rate | Beacons every `ROUTING_HELLO_INTERVAL_MS` (1000ms); decisions made on-demand (every `getNextHop()`/`selectNextHop()` call, not on a timer); staleness sweep every `app::loop()` iteration, expiring entries older than `ROUTING_ENTRY_TIMEOUT_MS` (3000ms) |
| Identifier | `NodeId` throughout — no MAC address appears at this layer |
| Failure behavior | Returns `NODE_ID_UNKNOWN` when no valid route exists (never fabricates a route); a corrupt/impossible advertised distance is rejected outright at ingestion (`applyRouteAdvertisement`'s validity guards), never stored |

## 5. predictor (link-quality evidence)

| | |
|---|---|
| Producer | `predictor_core::onRssiSample()`/`onSendOutcome()`/`tickStaleness()`, via the `predictor::` adapter |
| Consumers | `routing::getNextHopInternal()` (health mask, gates NORMAL-traffic preference); `telemetry` (`predictor::linkState()`, new Phase-6 accessor, feeds `LINK_UPDATE`/`PREDICTION`); `telemetry::onLinkEvent()` (event-driven) |
| Data format | `predictor_core::NeighborLinkState` (RSSI/EWMA/slope/PDR/link_score/hysteresis-debounce counters) |
| Units | RSSI in dBm; slope in dB per sample step; PDR/link_score as a `[0,1]` ratio |
| Timing/rate | RSSI fed on every received packet (reuses the routing beacon's own ~1s cadence, no separate fast heartbeat — see `decisions.md`); staleness fast-path checked every `app::loop()` iteration against `PREDICTOR_STALENESS_TIMEOUT_MS` (2000ms) |
| Identifier | `NodeId` (direct neighbor) |
| Failure behavior | A neighbor never observed defaults to `HEALTHY` (optimistic default, can't cause a false "avoid this link"); silence longer than the staleness timeout forces `UNHEALTHY` immediately, bypassing the normal debounce |

## 6. reliability (hop-by-hop delivery)

| | |
|---|---|
| Producer | `reliability_core` (via the `reliability::` adapter): `beginTx`/`onAckReceived`/`tickTimeouts` |
| Consumers | `predictor::onSendResult()` (real PDR evidence, per attempt); `ucb1::onRouteOutcome()` (only when enabled, per resolved series); `telemetry` (`reliability::getStatistics()`, feeds `STATISTICS`); `telemetry::onReliabilityEvent()` (event-driven, `PACKET_RETRY`/`PACKET_DROP` only) |
| Data format | Real unicast `MSG_DATA`/`MSG_ACK` `MeshPacket`s over `transport::send()`; `reliability_core::Statistics` counters |
| Units | Counts (packets, retries, duplicates); `lastLatencyMs` in milliseconds (per-hop, not end-to-end — see decisions.md) |
| Timing/rate | Bounded retry: up to `RELIABILITY_MAX_RETRIES` (3) resends, `RELIABILITY_ACK_TIMEOUT_MS` (200ms) apart, worst case 800ms to a final FAILED |
| Identifier | `PacketId{source, sequence}` — reuses `MeshPacket`'s own header fields, distinct from the GUI's own envelope `seq` (see Part 12 below) |
| Failure behavior | A synchronous send rejection (unregistered peer) is treated as an immediate, honest failure (`cancelTx()`), never a fabricated success; pool exhaustion is a real, counted failure (`recordImmediateFailure`), never silently dropped uncounted |

## 7. UCB1 (optional, `ENABLE_UCB1=1` only)

| | |
|---|---|
| Producer | `ucb1_core::selectNextHop()`, via the `ucb1::` adapter |
| Consumer | `routing::applyUcb1Ranking()` — replaces `routing_core`'s own baseline pick for NORMAL traffic only, never priority |
| Data format | Ranks `routing_core::CandidateInfo[]` (already validity/health-filtered) via the UCB1 formula; writes into `ucb1_core::Ucb1State.arms[destination][nextHop]` |
| Units | `meanReward` is a `[0,1]` success ratio; the exploration bonus is dimensionless |
| Timing/rate | Reward recorded once per resolved hop-transmission series (never per retry) — see decisions.md |
| Identifier | `(destination, nextHop)` `NodeId` pair |
| Failure behavior | Structurally compiled out entirely when `ENABLE_UCB1=0` — not a runtime branch, an absent translation unit. No dedicated telemetry message (see below) — its effect is only visible indirectly, through `ROUTE_UPDATE.active` |

## 8. anomaly (sensor health, independent of the packet path)

| | |
|---|---|
| Producer | `anomaly_core::evaluate()`/`tickStaleness()`, via the `anomaly::` adapter |
| Consumer | `telemetry` (`anomaly::getTelemetry()`, feeds `SENSOR_STATUS`); `telemetry::onAnomalyEvent()` (event-driven) |
| Data format | `anomaly_core::SensorTelemetry{raw_value, median, mad, modified_z, threshold, flatline_active/duration, state, valid}` |
| Units | Raw ADC counts for value/median/mad; modified-Z is dimensionless; durations in ms |
| Timing/rate | Sampled every `SENSOR_SAMPLE_INTERVAL_MS` (150ms); state-machine transitions debounced (`ANOMALY_CONSECUTIVE_COUNT`=2, `ANOMALY_RECOVERY_COUNT`=2) |
| Identifier | `SensorId::POT`/`LDR` — completely disjoint from `NodeId`/mesh identity; never touches `MeshPacket` |
| Failure behavior | `INVALID` (caller-flagged bad reading) and `STALE` (observation stream stopped) are both reachable independently from any state; never influences routing (a structurally enforced, tested separation — see decisions.md) |

## 9. telemetry (the only consumer of everything above, producer of nothing back)

| | |
|---|---|
| Producer | `telemetry::tick()` (periodic) + `telemetry::onXxxEvent()` (event-driven), reading the real accessors listed in rows 4-8 above |
| Consumer | `Serial` (the physical/virtual UART) |
| Data format | One newline-terminated `mesh-json/v1` JSON object per message — see [gui-compatibility-matrix.md](gui-compatibility-matrix.md) for the full per-message-type schema |
| Units | Per-field, matching the frozen contract exactly (dBm, ratios, ms) |
| Timing/rate | `TELEMETRY_HEARTBEAT/NODE_STATUS/SENSOR/STATISTICS_INTERVAL_MS` = 1000ms; `TELEMETRY_LINK/PREDICTION_INTERVAL_MS` = 250ms; `EVENT`/`ERROR` event-driven, no interval |
| Identifier | Envelope `nodeId` (`"A"`.."S"`, from `core/node_id.h::nodeName()`) + `bootId` (random nonce, changes on reboot) + `seq` (telemetry-owned counter, distinct from `MeshPacket.sequence` — see Part 12 below) |
| Failure behavior | A message that wouldn't fit `LINE_BUF_SIZE` (768 bytes) is refused entirely, never emitted truncated (`Writer::ok` latches false, `buildXxx()` returns 0) — verified by `test_telemetry_core.cpp` |

## 10. Serial -> GUI

| | |
|---|---|
| Producer | Firmware's `Serial.println()` |
| Consumer | `mesh-command-console.html`'s `applyTelemetry()`, via either the browser's native WebSerial API ("Connect Hardware") or `serial-bridge.py`'s WebSocket relay ("Connect via Bridge") |
| Data format | UTF-8, one JSON object per `LF` line (optional `CR` accepted), max 4096 bytes/line per the contract |
| Units | N/A at this layer — pure byte transport |
| Timing/rate | Whatever firmware emits, unthrottled by the transport itself — see row 9 for the real emission rate |
| Identifier | None at the transport layer — the GUI reads `nodeId`/`bootId`/`seq` back out of each parsed JSON object |
| Failure behavior | Malformed lines are logged as a `PARSE WARNING` and skipped, never terminate the GUI's read loop (verified against the GUI's own real code — see `testing.md`); a dropped USB/WebSocket connection reverts the GUI to simulation mode automatically (`teardownSerial()`) |

## Part 12 — telemetry identity audit: three genuinely distinct concepts

| Concept | Purpose | Lifetime | Reset behavior | Wraparound | Consumer |
|---|---|---|---|---|---|
| `MeshPacket.sequence` | Radio-layer packet identity, `(source, sequence)`, used for the duplicate filter and ACK matching | One packet's entire multi-hop lifetime (unchanged across every forward) | `uint16_t`, per-source counter in `reliability_core::ReliabilityState.nextSeqCounter`; resets to 0 on reboot (no persistent storage) | Wraps at 65,536 — at real mesh traffic volumes, effectively never a practical concern within a demo's runtime | `reliability_core` (dup-filter, ACK matching); never read by telemetry or the GUI |
| GUI telemetry `seq` | Detects a dropped/out-of-order telemetry *message* (not packet) | One telemetry envelope | `uint32_t`, telemetry-owned (`telemetry.cpp`'s `g_seq`); resets to 0 on reboot | Wraps at ~4.29 billion — years of continuous uptime at this project's real emission rate, not a practical concern | `mesh-command-console.html`'s `trackFirmwareMeta()` (`SEQUENCE GAP` detection) |
| `bootId` | Lets the GUI distinguish "still the same boot" from "node rebooted" | One physical boot | Freshly random (`esp_random()`) every boot — see decisions.md for why this isn't a persistent counter | N/A (random, not incrementing) | `mesh-command-console.html`'s `trackFirmwareMeta()` (`BOOT` reboot-detection log line) |

**Verified structurally distinct, not just by convention:** `MeshPacket.sequence`
lives in `reliability_core::ReliabilityState`, a struct `telemetry.cpp` never
even includes a pointer to; the GUI `seq`/`bootId` live entirely inside
`telemetry.cpp`'s own file-local state. No code path anywhere copies one
into the other.
