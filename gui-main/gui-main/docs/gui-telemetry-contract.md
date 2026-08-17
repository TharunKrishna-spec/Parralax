# Firmware ↔ GUI Telemetry Contract v1

Status: **FROZEN** for JSON integration
Owner: Firmware + Dashboard team  
Revision: `1.0`
Date: 2026-08-17

Firmware is authoritative. The GUI renders these declared values and never derives a
link score, route reason, anomaly decision, threshold, timeout, or event meaning.

## Transport

| Property | Frozen value |
|---|---|
| Transport | USB UART serial, or WebSocket bridge relay |
| Serial baud | `115200` 8-N-1 |
| Framing | One UTF-8 JSON object per `LF` line; optional `CR` accepted |
| Maximum JSON line | `4096` bytes |
| WebSocket endpoint | `ws://localhost:8765` |
| Protocol version | `mesh-json/v1` |
| Checksum | None in JSON transport; transport errors are connection errors |
| Timestamp | `timestampMs`: monotonic milliseconds since boot |
| Reboot identity | `bootId`: opaque non-empty string, changes on reboot |
| Sequence | `seq`: unsigned integer, monotonic per node and boot; gaps are reported |

Every message is one object with this exact envelope:

```json
{
  "protocolVersion":"mesh-json/v1",
  "type":"LINK_UPDATE",
  "nodeId":"A",
  "bootId":"a-0007",
  "seq":104,
  "timestampMs":123456,
  "payload":{}
}
```

Envelope fields are required on every message. `nodeId` is one of `A`, `B`, `C`, `D`,
`S`; `seq` and `timestampMs` are non-negative integers. Unknown fields may be added;
unknown message types must be ignored and logged. Malformed lines must not terminate the
stream.

## Canonical enums

- `role`: `SOURCE`, `RELAY`, `SINK`
- `nodeState`: `ONLINE`, `STALE`, `OFFLINE`, `ERROR`
- `linkState`: `UNKNOWN`, `HEALTHY`, `DEGRADING`, `UNHEALTHY`, `STALE`, `RECOVERING`
- `routeState`: `ACTIVE`, `BACKUP`, `UNAVAILABLE`, `EXPIRED`
- `routeReason`: `LINK_DEGRADATION`, `LINK_FAILURE`, `STALE_NEIGHBOR`, `ROUTE_EXPIRED`,
  `PRIORITY_OVERRIDE`, `ROUTE_RECOVERY`, `MANUAL`, `UNKNOWN`
- `trafficClass`: `NORMAL`, `PRIORITY`
- `predictionState`: `UNKNOWN`, `STABLE`, `DEGRADING`, `UNHEALTHY`, `RECOVERING`, `TIMEOUT`
- `hysteresisState`: `BELOW_LOW`, `BETWEEN_THRESHOLDS`, `ABOVE_HIGH`
- `sensorHealth`: `NORMAL`, `SUSPECT`, `ANOMALY`, `FLATLINE`, `OUT_OF_RANGE`, `STALE`
- `severity`: `INFO`, `WARN`, `ERROR`, `CRITICAL`

## Message registry

| ID | Message type | Direction | Exact frequency | Purpose |
|---:|---|---|---|---|
| `0x01` | `HELLO` | FW → GUI | once at boot and once after reconnect | identity and configuration |
| `0x02` | `HEARTBEAT` | FW → GUI | every `1000 ms` | liveness and uptime |
| `0x03` | `NODE_STATUS` | FW → GUI | on change and every `1000 ms` | node state |
| `0x04` | `LINK_UPDATE` | FW → GUI | every `250 ms` | link measurements |
| `0x05` | `ROUTE_UPDATE` | FW → GUI | on active/candidate/reason change | routes |
| `0x06` | `PREDICTION` | FW → GUI | every `250 ms` and on state change | predictor/hysteresis |
| `0x07` | `SENSOR_STATUS` | FW → GUI | every `1000 ms` and on health change | sensor/detector state |
| `0x08` | `EVENT` | FW → GUI | immediately, one per event | discrete event record |
| `0x09` | `STATISTICS` | FW → GUI | every `1000 ms` | reliability aggregates |
| `0x0A` | `ERROR` | FW → GUI | immediately, one per error | firmware error |

## Exact message schemas

For each table below, the listed field names are payload field names. All messages also
carry the required envelope above. `required` means firmware must send the field;
`optional` means the GUI displays `—` when absent.

### `0x01 HELLO`

Frequency: once at boot and once after reconnect. Units: `uptimeMs` is ms; `mac` is a
canonical lowercase colon-separated MAC string.

| Field | Type | Units | Valid range | Enum/required |
|---|---|---|---|---|
| `nodeName` | string | — | 1–32 UTF-8 chars | required |
| `role` | string | — | — | `SOURCE`/`RELAY`/`SINK`, required |
| `mac` | string | — | `XX:XX:XX:XX:XX:XX` | required when available |
| `firmwareVersion` | string | — | 1–32 chars | required |
| `config.heartbeatIntervalMs` | uint32 | ms | `>=1` | required |
| `config.offlineTimeoutMs` | uint32 | ms | `>= heartbeatIntervalMs` | required |
| `config.routeTimeoutMs` | uint32 | ms | `>=1` | required |
| `config.tLow` | float | score | `0.0–1.0` | required |
| `config.tHigh` | float | score | `0.0–1.0`, `>tLow` | required |
| `config.ewmaAlpha` | float | — | `0.0–1.0` | required |
| `config.telemetryRatesHz` | object | Hz | non-negative values | required |

Example:

```json
{"protocolVersion":"mesh-json/v1","type":"HELLO","nodeId":"A","bootId":"a-0007","seq":0,"timestampMs":0,"payload":{"nodeName":"Node A","role":"SOURCE","mac":"24:6F:28:AA:BB:01","firmwareVersion":"1.0.0","config":{"heartbeatIntervalMs":1000,"offlineTimeoutMs":3000,"routeTimeoutMs":2000,"tLow":0.55,"tHigh":0.75,"ewmaAlpha":0.25,"telemetryRatesHz":{"link":4,"prediction":4,"statistics":1}}}}
```

### `0x02 HEARTBEAT`

Frequency: every `1000 ms`. Units: `uptimeMs` is monotonic ms since boot.

| Field | Type | Units | Valid range | Enum/required |
|---|---|---|---|---|
| `uptimeMs` | uint32 | ms | `>=0` | required |

Example:

```json
{"protocolVersion":"mesh-json/v1","type":"HEARTBEAT","nodeId":"A","bootId":"a-0007","seq":42,"timestampMs":42000,"payload":{"uptimeMs":42000}}
```

### `0x03 NODE_STATUS`

Frequency: immediately on change and every `1000 ms`.

| Field | Type | Units | Valid range | Enum/required |
|---|---|---|---|---|
| `status` | string | — | — | node state enum, required |
| `nodeName` | string | — | 1–32 chars | optional |
| `role` | string | — | — | role enum, optional |
| `uptimeMs` | uint32 | ms | `>=0` | required |
| `firmwareVersion` | string | — | 1–32 chars | optional |
| `reason` | string | — | 1–64 chars | optional |

Example:

```json
{"protocolVersion":"mesh-json/v1","type":"NODE_STATUS","nodeId":"B","bootId":"b-0003","seq":8,"timestampMs":8000,"payload":{"status":"ONLINE","nodeName":"Node B","role":"RELAY","uptimeMs":8000,"firmwareVersion":"1.0.0"}}
```

### `0x04 LINK_UPDATE`

Frequency: every `250 ms` for each monitored directed link. `pdr` and `linkScore` are
fractions, never percentages.

| Field | Type | Units | Valid range | Enum/required |
|---|---|---|---|---|
| `from` | string | — | canonical node ID | required |
| `to` | string | — | canonical node ID; different from `from` | required |
| `rssiDbm` | int8 | dBm | `-127–0` | required |
| `rssiEwmaDbm` | float | dBm | `-127.0–0.0` | required |
| `rssiSlopeDbPerSec` | float | dB/s | finite | required |
| `pdr` | float | ratio | `0.0–1.0` | required |
| `pdrEwma` | float | ratio | `0.0–1.0` | required |
| `stalenessMs` | uint32 | ms | `>=0` | required |
| `linkScore` | float | score | `0.0–1.0` | required |
| `state` | string | — | — | link state enum, required |

Example:

```json
{"protocolVersion":"mesh-json/v1","type":"LINK_UPDATE","nodeId":"A","bootId":"a-0007","seq":104,"timestampMs":26000,"payload":{"from":"A","to":"B","rssiDbm":-64,"rssiEwmaDbm":-62.0,"rssiSlopeDbPerSec":-1.8,"pdr":0.91,"pdrEwma":0.92,"stalenessMs":120,"linkScore":0.78,"state":"DEGRADING"}}
```

### `0x05 ROUTE_UPDATE`

Frequency: on any active route, candidate route, traffic class, or route reason change.
`hops` is an ordered array; `hopCount` equals `hops.length - 1`.

| Field | Type | Units | Valid range | Enum/required |
|---|---|---|---|---|
| `destination` | string | — | canonical node ID | required |
| `active.hops` | string[] | — | 2–8 canonical IDs | required |
| `active.hopCount` | uint8 | hops | `>=1` | required |
| `active.score` | float | score | `0.0–1.0` | required |
| `active.state` | string | — | — | route state, must be `ACTIVE` | required |
| `candidates` | object[] | — | zero or more | required |
| `candidates[].hops` | string[] | — | 2–8 canonical IDs | required |
| `candidates[].hopCount` | uint8 | hops | `>=1` | required |
| `candidates[].score` | float | score | `0.0–1.0` | required |
| `candidates[].state` | string | — | — | route state, required |
| `trafficClass` | string | — | — | `NORMAL`/`PRIORITY`, required |
| `reason` | string | — | — | route reason enum, required |

Example:

```json
{"protocolVersion":"mesh-json/v1","type":"ROUTE_UPDATE","nodeId":"A","bootId":"a-0007","seq":105,"timestampMs":26000,"payload":{"destination":"S","active":{"hops":["A","B","S"],"hopCount":2,"score":0.82,"state":"ACTIVE"},"candidates":[{"hops":["A","B","S"],"hopCount":2,"score":0.82,"state":"ACTIVE"},{"hops":["A","C","D","S"],"hopCount":3,"score":0.91,"state":"BACKUP"}],"trafficClass":"NORMAL","reason":"LINK_DEGRADATION"}}
```

### `0x06 PREDICTION`

Frequency: every `250 ms` and immediately on prediction/hysteresis state change.

| Field | Type | Units | Valid range | Enum/required |
|---|---|---|---|---|
| `neighborId` | string | — | canonical node ID | required |
| `rssiDbm` | float | dBm | `-127.0–0.0` | required |
| `rssiEwmaDbm` | float | dBm | `-127.0–0.0` | required |
| `rssiSlopeDbPerSec` | float | dB/s | finite | required |
| `pdr` | float | ratio | `0.0–1.0` | required |
| `pdrEwma` | float | ratio | `0.0–1.0` | required |
| `stalenessMs` | uint32 | ms | `>=0` | required |
| `linkScore` | float | score | `0.0–1.0` | required |
| `predictionState` | string | — | — | prediction state enum, required |
| `tLow` | float | score | `0.0–1.0` | required |
| `tHigh` | float | score | `0.0–1.0`, `>tLow` | required |
| `hysteresisState` | string | — | — | hysteresis enum, required |

Example:

```json
{"protocolVersion":"mesh-json/v1","type":"PREDICTION","nodeId":"B","bootId":"b-0003","seq":201,"timestampMs":50250,"payload":{"neighborId":"A","rssiDbm":-61.0,"rssiEwmaDbm":-59.0,"rssiSlopeDbPerSec":-1.7,"pdr":0.89,"pdrEwma":0.91,"stalenessMs":120,"linkScore":0.54,"predictionState":"DEGRADING","tLow":0.55,"tHigh":0.75,"hysteresisState":"BELOW_LOW"}}
```

### `0x07 SENSOR_STATUS`

Frequency: every `1000 ms` and immediately on health-state change. Sensor values are
sensor-specific and must be documented in `sensorType` metadata.

| Field | Type | Units | Valid range | Enum/required |
|---|---|---|---|---|
| `sensorId` | string | — | 1–32 chars | required |
| `sensorType` | string | — | 1–32 chars | required |
| `value` | float | sensor-specific | sensor-specific finite range | required when valid |
| `healthState` | string | — | — | sensor health enum, required |
| `durationMs` | uint32 | ms | `>=0` | required for `FLATLINE`, otherwise optional |
| `rawValue` | float | sensor-specific | finite | optional |
| `baseline` | float | sensor-specific | finite | optional |
| `mad` | float | sensor-specific | `>=0` | optional |
| `zScore` | float | — | finite | optional |
| `threshold` | float | detector-specific | `>=0` | optional |

Example:

```json
{"protocolVersion":"mesh-json/v1","type":"SENSOR_STATUS","nodeId":"C","bootId":"c-0002","seq":77,"timestampMs":77000,"payload":{"sensorId":"vibration","sensorType":"accelerometer","value":2.31,"healthState":"FLATLINE","durationMs":8200,"rawValue":2.31,"baseline":2.30,"mad":0.01,"zScore":0.4,"threshold":3.5}}
```

### `0x08 EVENT`

Frequency: immediately, exactly one message per discrete event. `details` is a typed
object whose fields depend on `eventType`; the required common fields are fixed.

| Field | Type | Units | Valid range | Enum/required |
|---|---|---|---|---|
| `eventType` | string | — | — | event registry below, required |
| `severity` | string | — | — | severity enum, required |
| `source` | string | — | 1–32 chars | required |
| `details` | object | — | valid JSON object | required |

Event enum: `NODE_JOIN`, `NODE_LEAVE`, `LINK_DEGRADING`, `LINK_FAILURE`, `ROUTE_CHANGE`,
`ROUTE_RECOVERY`, `SENSOR_ANOMALY`, `SENSOR_FAILURE`, `PACKET_RETRY`, `PACKET_DROP`,
`PRIORITY_ROUTE`, `ERROR`.

For `ROUTE_CHANGE`, `details` must contain `oldHops`, `newHops`, `reason`, `oldScore`,
`newScore`, and optional `leadTimeMs` (ms, `>=0`).

Example:

```json
{"protocolVersion":"mesh-json/v1","type":"EVENT","nodeId":"A","bootId":"a-0007","seq":106,"timestampMs":26100,"payload":{"eventType":"ROUTE_CHANGE","severity":"INFO","source":"A","details":{"oldHops":["A","B","S"],"newHops":["A","C","D","S"],"reason":"LINK_DEGRADATION","oldScore":0.54,"newScore":0.91,"leadTimeMs":232}}}
```

### `0x09 STATISTICS`

Frequency: every `1000 ms`. Counters are cumulative since boot unless firmware adds a
future `windowMs` field; the GUI must not reset or reinterpret them.

| Field | Type | Units | Valid range | Enum/required |
|---|---|---|---|---|
| `windowMs` | uint32 | ms | `>=1` | required |
| `pdr` | float | ratio | `0.0–1.0` | required |
| `packetsTransmitted` | uint32 | packets | `>=0` | required |
| `packetsAcknowledged` | uint32 | packets | `>=0` | required |
| `packetsDropped` | uint32 | packets | `>=0` | required |
| `retryCount` | uint32 | retries | `>=0` | required |
| `duplicateCount` | uint32 | packets | `>=0` | required |
| `endToEndLatencyMs` | float | ms | `>=0` | required |

Example:

```json
{"protocolVersion":"mesh-json/v1","type":"STATISTICS","nodeId":"S","bootId":"s-0004","seq":88,"timestampMs":88000,"payload":{"windowMs":1000,"pdr":0.98,"packetsTransmitted":1042,"packetsAcknowledged":1021,"packetsDropped":21,"retryCount":34,"duplicateCount":2,"endToEndLatencyMs":38.0}}
```

### `0x0A ERROR`

Frequency: immediately, exactly one message per firmware error. An error does not close
the stream unless the transport itself fails.

| Field | Type | Units | Valid range | Enum/required |
|---|---|---|---|---|
| `severity` | string | — | — | `ERROR`/`CRITICAL`, required |
| `code` | string | — | 1–32 chars | stable firmware code, required |
| `message` | string | — | 1–256 chars | required |
| `recoverable` | bool | — | `true`/`false` | required |
| `details` | object | — | valid JSON object | optional |

Example:

```json
{"protocolVersion":"mesh-json/v1","type":"ERROR","nodeId":"B","bootId":"b-0003","seq":209,"timestampMs":52200,"payload":{"severity":"ERROR","code":"ROUTE_TABLE_FULL","message":"No candidate route slot available","recoverable":true,"details":{}}}
```

## Invalid, unavailable, stale, and sequence semantics

- A missing optional field is unavailable and displayed as `—`.
- A required field with the wrong type/range makes the message invalid; the GUI logs a
  parse warning and does not partially apply that message.
- `stalenessMs` is firmware-reported link staleness, not a GUI-calculated link score.
- Heartbeat status becomes stale after the `HELLO.config.offlineTimeoutMs` interval;
  the GUI preserves the last values and labels them stale.
- A changed `bootId` starts a new node history. A sequence gap is logged as missing data.
- `0` is a valid value where ranges allow it; it never means unavailable.

## Legacy rehearsal compatibility

Until firmware emits v1 envelopes, the GUI accepts this temporary flat object:

```json
{"linkAB":0.82,"route":"ABS","trafficType":"normal","flagC":"clean","pdr":0.99,"rerouteLeadMs":143}
```

The adapter is not part of the frozen firmware protocol. `route` accepts `ABS` or
`ACDS`; `AS` is valid only with `trafficType:"priority"`.

## Sign-off

Firmware owner: ____________________  Date: __________  Version: `mesh-json/v1`
GUI owner: _________________________  Date: __________  Contract revision: `1.0`
