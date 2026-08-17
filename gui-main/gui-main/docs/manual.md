# PARALLAX Dashboard Operating Manual

## Purpose

`mesh-command-console.html` is a single-file, offline-capable visualization for the PARALLAX predictive self-healing IoT mesh.

It has three data states:

- **SIMULATION** — browser-generated rehearsal events. No firmware is required.
- **LIVE** — authoritative telemetry received from an ESP32 sink over USB Serial or a WebSocket bridge.
- **REPLAY** — previously recorded LIVE telemetry replayed at 0.5× speed.

The dashboard does not make routing, prediction, anomaly, retry, or recovery decisions. Firmware telemetry is authoritative. The frontend renders and explains those decisions.

## Files

- `mesh-command-console.html` — complete GUI, CSS, JavaScript, simulation engine, telemetry adapter, prototype graph, Data Center view, Bridge view, replay, and inspectors.
- `manual.md` — this operating and integration reference.

The HTML has no CDN or Internet dependency.

## Starting the GUI

### Chrome or Edge

1. Open `mesh-command-console.html` in Chrome or Microsoft Edge.
2. For SIMULATION, do not connect hardware.
3. For LIVE USB operation, connect the ESP32 sink/root node to the laptop.
4. Select the baud rate used by the firmware. The default is `115200`.
5. Click **Connect Hardware**.
6. Choose the ESP32 serial port in the browser permission dialog.
7. Confirm the badge changes from `SIMULATION MODE` to `LIVE MODE`.

Web Serial requires a supported Chromium browser and user permission. Opening the file locally is supported by Chrome/Edge; no local web server is required for the Serial workflow.

### WebSocket bridge

Use this when the browser cannot access the serial device directly or when a serial-to-WebSocket bridge is already running.

1. Start the bridge on the laptop.
2. Enter its URL in the bridge field, for example `ws://localhost:8765`.
3. Click **Connect via Bridge**.
4. The bridge must forward one JSON object per line/message.
5. On WebSocket open, the GUI changes to LIVE mode.

## The first screen

The default view is the prototype mesh.

The hero elements are:

- five-node topology: A, B, C, D, S;
- active route, normally `A → B → S`;
- packet-flow overlay;
- mission ribbon: `OBSERVE → PREDICT → DECIDE → RECOVER`;
- predictive lead-time;
- current event/HUD;
- compact LIVE/SIM provenance.

The conceptual topology is:

```text
Normal:   A → B → S
Backup:   A → C → D → S
Priority: A → S
```

## Operating modes

### DEMO mode

The default judge-facing mode. It minimizes technical panels and emphasizes the causal story.

Click **▶ RUN DEMO** to start the deterministic simulation. During an active demo, **PAUSE** and **RESET** appear.

The simulation schedule is:

| Time | Event |
|---|---|
| 00:00 | Healthy baseline |
| 00:15 | Node B attenuation |
| 00:20 onward | Predictive degradation / ghost route |
| 00:45 approximately | Reroute after firmware-equivalent debounce |
| 01:10 | Sensor spike |
| 01:30 | Sensor stuck/flatline |
| 02:00 | Priority packet |
| 02:20 | Duplicate suppression |
| 02:40 | Bridge View |
| 02:55 | Demo complete |

The exact visual timing is driven by the simulation loop; LIVE mode never uses this schedule.

### EXPLAIN mode

Opens the guided explanation layer. It explains what happened, why it happened, how the firmware decision is represented, and what result followed.

The mission ribbon is also interactive:

- **OBSERVE** — heartbeat, node status, sensor state, and telemetry.
- **PREDICT** — RSSI, EWMA, slope, PDR, staleness, link score, and hysteresis.
- **DECIDE** — route candidates, distance-vector reasoning, and priority override.
- **RECOVER** — route migration, ACK, retry, duplicate suppression, and recovery evidence.

### EXPERT mode

Reveals the technical firmware panels:

- node metadata and liveness;
- authoritative link health;
- RSSI and RSSI EWMA;
- RSSI slope;
- PDR and staleness;
- link score;
- T_LOW and T_HIGH;
- route candidates and route reason;
- prediction state;
- sensor state and detector fields;
- event chronology and telemetry provenance.

## Main controls

### MODE

Opens DEMO, EXPLAIN, and EXPERT.

### MORE

Contains progressive-disclosure controls:

- **PROTOTYPE** — actual five-node laboratory topology.
- **DEPLOYMENT** — conceptual data-center application layer.
- **BRIDGE** — prototype-to-deployment mapping.
- **REPLAY LAST EVENT** — replays recorded telemetry when available.
- **WHY / ARCHITECTURE** — system map.
- **TECH INSPECTOR** — object-level technical evidence.
- **PACKET FLOW** — cycles AUTO, ON, and OFF.
- **VISUAL LANGUAGE** — legend.
- **THERMAL EVENT** — clearly labelled conceptual scenario; not live ESP32 temperature data.
- **SIMULATION** — browser-only actions 1, 2, 3, 4, 5, and reset.

### Simulation actions

These are disabled semantically while LIVE. They are intended for rehearsal:

| Command | Meaning |
|---|---|
| `1` | Attenuate Node B / degrade A→B |
| `2` | SPIKE/JUMP at Node C |
| `3` | STUCK/FLATLINE at Node C |
| `4` | Priority packet using A→S |
| `5` | Duplicate packet suppression scenario |
| `R` | Reset simulation state |

The same actions are available from `MORE → SIMULATION` and from the keyboard. In LIVE mode, frontend simulation actions do not override firmware state.

### Views

#### PROTOTYPE

Shows the actual ESP32 mesh model:

- A = source/aggregator;
- B = primary relay;
- C = alternate sensor/relay;
- D = backup relay;
- S = sink/root.

#### DEPLOYMENT

Shows a clearly-labelled conceptual data-center deployment model driven by prototype semantics. It is not a claim that the ESP32 boards are installed in a production data center.

The view contains six racks, rack-unit structure, wireless sensor fabric, gateway, cold/hot aisle cues, thermal abstraction, and mapped incidents.

Deployment-only controls:

- Overview;
- Rack Focus;
- Incident;
- Both;
- Physical;
- Network.

Mapping:

| Prototype | Conceptual deployment |
|---|---|
| A | Source / sensor aggregator |
| B | Rack 02 network relay |
| C | Rack 03 environmental sensor node |
| D | Alternate relay |
| S | Wireless sensor gateway / edge collector |

#### BRIDGE

Shows the actual prototype on the left, the mission sequence in the center, and the conceptual deployment on the right.

It is labelled `SAME INTELLIGENCE · DIFFERENT DEPLOYMENT SCALE`.

The deployment side is conceptual and must not be described as a production DCIM integration.

## LIVE telemetry contract

The preferred format is newline-delimited JSON over Serial or one JSON object per WebSocket message.

Each rich message has this envelope:

```json
{
  "type": "MESSAGE_TYPE",
  "nodeId": "B",
  "seq": 42,
  "bootId": "B-boot-001",
  "timestampMs": 123456,
  "payload": {}
}
```

The sink/root S should aggregate and emit the mesh telemetry. Do not print debug text into the JSON telemetry stream.

### HELLO

```json
{
  "type":"HELLO",
  "nodeId":"B",
  "seq":1,
  "bootId":"B-boot-001",
  "payload":{
    "protocolVersion":"PARALLAX-1",
    "role":"RELAY",
    "firmwareVersion":"1.0.0",
    "config":{"tLow":0.40,"tHigh":0.60}
  }
}
```

### HEARTBEAT

```json
{
  "type":"HEARTBEAT",
  "nodeId":"B",
  "seq":2,
  "bootId":"B-boot-001",
  "payload":{"uptimeMs":123456,"status":"ONLINE"}
}
```

### NODE_STATUS

```json
{
  "type":"NODE_STATUS",
  "nodeId":"B",
  "seq":3,
  "payload":{"status":"ONLINE","role":"RELAY","uptimeMs":123456}
}
```

Valid status values are `ONLINE`, `STALE`, `OFFLINE`, and `ERROR`.

### LINK_UPDATE

```json
{
  "type":"LINK_UPDATE",
  "nodeId":"B",
  "seq":4,
  "payload":{
    "from":"A",
    "to":"B",
    "rssiDbm":-77,
    "rssiEwmaDbm":-74,
    "rssiSlopeDbPerSec":-3.2,
    "pdr":0.71,
    "stalenessMs":140,
    "linkScore":0.42,
    "state":"DEGRADING",
    "tLow":0.40,
    "tHigh":0.60
  }
}
```

Valid link states are `HEALTHY`, `DEGRADING`, `UNHEALTHY`, `STALE`, `FAILED`, and `RECOVERING`.

### PREDICTION

This is the authoritative source for the predictive ghost route:

```json
{
  "type":"PREDICTION",
  "nodeId":"B",
  "seq":5,
  "payload":{
    "predictionState":"DEGRADING",
    "belowCount":1,
    "linkScore":0.42,
    "pdr":0.71,
    "stalenessMs":140,
    "tLow":0.40,
    "tHigh":0.60,
    "hysteresisState":"WATCHING"
  }
}
```

Send `belowCount: 1` and `belowCount: 2` while the primary route is still active. Do not send a browser-only prediction.

### ROUTE_UPDATE

Normal or backup route:

```json
{
  "type":"ROUTE_UPDATE",
  "nodeId":"S",
  "seq":6,
  "payload":{
    "active":{"hops":["A","C","D","S"]},
    "trafficClass":"NORMAL",
    "reason":"PRIMARY_DEGRADED",
    "candidates":[
      {"hops":["A","C","D","S"],"state":"ACTIVE","score":0.80},
      {"hops":["A","B","S"],"state":"UNHEALTHY","score":0.42}
    ]
  }
}
```

Priority route:

```json
{
  "type":"ROUTE_UPDATE",
  "nodeId":"S",
  "seq":7,
  "payload":{
    "active":{"hops":["A","S"]},
    "trafficClass":"PRIORITY",
    "reason":"SHORTEST_HOP_OVERRIDE",
    "candidates":[]
  }
}
```

The dashboard supports arbitrary hop arrays. It does not require a hard-coded route string.

### SENSOR_STATUS

Spike/anomaly:

```json
{
  "type":"SENSOR_STATUS",
  "nodeId":"C",
  "seq":8,
  "payload":{
    "sensorId":"C",
    "healthState":"ANOMALY",
    "value":88,
    "median":42,
    "mad":4.2,
    "zScore":5.2,
    "threshold":3.5
  }
}
```

Flatline/stuck:

```json
{
  "type":"SENSOR_STATUS",
  "nodeId":"C",
  "seq":9,
  "payload":{
    "sensorId":"C",
    "healthState":"FLATLINE",
    "value":88,
    "durationMs":900,
    "flatlineCount":12
  }
}
```

### PACKET

Only emit a packet path when firmware knows the actual path:

```json
{
  "type":"PACKET",
  "nodeId":"A",
  "seq":10,
  "payload":{
    "src":"A",
    "dst":"S",
    "path":["A","C","D","S"],
    "priority":false,
    "seq":142
  }
}
```

Priority packets use `path:["A","S"]` and `priority:true`.

### EVENT

```json
{
  "type":"EVENT",
  "nodeId":"S",
  "seq":11,
  "payload":{
    "eventType":"REROUTE_COMMITTED",
    "details":{
      "from":["A","B","S"],
      "to":["A","C","D","S"],
      "leadTimeMs":184
    }
  }
}
```

Recommended event names:

`LINK_DEGRADING`, `REROUTE_PROPOSED`, `REROUTE_COMMITTED`, `NODE_SILENT`, `TIMEOUT_FALLBACK`, `PACKET_SENT`, `PACKET_DELIVERED`, `PACKET_RETRY`, `PACKET_RECOVERED`, `DUPLICATE_SUPPRESSED`, `ANOMALY_SPIKE`, `ANOMALY_STUCK`, `PRIORITY_OVERRIDE`, and `RECOVERY`.

`leadTimeMs` must be measured by firmware. The GUI does not invent it.

### STATISTICS

```json
{
  "type":"STATISTICS",
  "nodeId":"S",
  "seq":12,
  "payload":{
    "pdr":0.93,
    "packetsSent":200,
    "packetsDelivered":186,
    "packetsRetried":11,
    "packetsRecovered":8
  }
}
```

### ERROR

```json
{
  "type":"ERROR",
  "nodeId":"S",
  "seq":13,
  "payload":{"code":"NODE_TIMEOUT","message":"Node B heartbeat expired"}
}
```

## Required live event sequences

### Predictable degradation

```text
LINK_UPDATE state=DEGRADING
PREDICTION belowCount=1
PREDICTION belowCount=2
ROUTE_UPDATE active=[A,C,D,S]
EVENT REROUTE_COMMITTED leadTimeMs=<measured value>
PACKET/PACKET_RECOVERED when available
RECOVERY only after real recovery evidence
```

Expected visual result:

1. A→B becomes unhealthy.
2. A - - - C - - - D - - - S appears.
3. The old route remains active until commit.
4. The alternate route becomes solid only after ROUTE_UPDATE.
5. Traffic migrates only when actual packet telemetry exists.
6. Measured lead-time freezes.

### Instantaneous failure

```text
HEARTBEAT stops
NODE_STATUS STALE/OFFLINE
EVENT NODE_SILENT or TIMEOUT_FALLBACK
ROUTE_UPDATE active=[A,C,D,S]
RECOVERY after delivery/monitoring evidence
```

This must look different from predictive degradation. Do not label a timeout as predicted.

## LIVE vs SIMULATION truth rules

### LIVE

- Firmware controls route and anomaly state.
- Browser does not generate live packets.
- Browser does not generate live lead-time.
- Browser does not calculate a conflicting link score.
- Missing optional telemetry is shown as unavailable.
- Data Center view is a conceptual mapping of the real prototype telemetry.

### SIMULATION

- Browser may run deterministic scenario actions.
- Values and event timing are rehearsal values.
- The UI remains explicitly labelled SIMULATION.

### REPLAY

- Only recorded telemetry is replayed.
- The provenance badge becomes `RECORDED LIVE` or `REPLAY`.
- Replay uses the same rendering path as LIVE telemetry.

## Demonstration procedure with real ESP32 data

1. Connect the sink S to the laptop.
2. Open the HTML in Chrome/Edge.
3. Click Connect Hardware and grant serial permission.
4. Wait for HELLO and HEARTBEAT messages.
5. Confirm node metadata appears in EXPERT mode.
6. Confirm the route is `A → B → S`.
7. Put Node B into the Faraday bag/attenuation condition.
8. Watch LINK_UPDATE and PREDICTION events.
9. Confirm the ghost path appears before ROUTE_UPDATE.
10. Confirm route commit and measured lead-time.
11. Test Node C anomaly telemetry.
12. Test priority packet telemetry.
13. Use BRIDGE or DEPLOYMENT only after the prototype event is visible.
14. Disconnect USB and verify safe fallback to simulation.

## Troubleshooting

### The GUI stays in SIMULATION

- Use Chrome or Edge.
- Confirm the browser supports Web Serial.
- Click Connect Hardware and select the correct port.
- Confirm the firmware is emitting newline-delimited JSON.
- Confirm the selected baud matches the firmware.
- Ensure another serial monitor is not holding the port.

### HELLO is not visible

- Confirm the sink emits HELLO after boot or connection.
- Confirm the message is valid JSON.
- Confirm the message ends with a newline.
- Confirm `type`, `nodeId`, and `payload.protocolVersion` are present.

### LIVE badge appears but values do not update

- Check that messages use the supported `payload` structure.
- Confirm LINK_UPDATE uses `from`, `to`, `pdr`, and `linkScore`.
- Confirm STATISTICS uses `payload.pdr`.
- Open EXPERT mode and inspect the raw state panels.

### Ghost route does not appear

- LINK_UPDATE alone is not the predictive event.
- Emit PREDICTION with `predictionState:"DEGRADING"` and `belowCount` 1 or 2.
- Do not emit only a frontend-calculated threshold crossing.

### Route changes too early

- Check that ROUTE_UPDATE is emitted only when firmware commits the route.
- The GUI should not be asked to infer a route from link score.

### Packets are not visible in LIVE

- The GUI intentionally shows no fabricated live packets.
- Emit PACKET messages with a real `path` array.

### USB disconnect

The GUI should return to SIMULATION and show the connection-loss message. Reconnect the sink and click Connect Hardware again.

## Firmware integration checklist

- [ ] Sink S emits newline-delimited JSON.
- [ ] HELLO includes protocol version, role, firmware version, and boot ID.
- [ ] HEARTBEAT is emitted periodically.
- [ ] LINK_UPDATE includes measured RSSI/PDR/score fields.
- [ ] PREDICTION is emitted before route commit.
- [ ] ROUTE_UPDATE contains the authoritative hop array.
- [ ] T_LOW and T_HIGH come from firmware, not hard-coded frontend values.
- [ ] SENSOR_STATUS distinguishes ANOMALY/SPIKE from FLATLINE/STUCK.
- [ ] PACKET path telemetry is emitted only when real.
- [ ] Lead-time is measured by firmware.
- [ ] Sequence numbers are monotonic.
- [ ] Boot ID changes after reboot.
- [ ] Serial output contains no non-JSON debug text.
- [ ] Disconnect/reconnect behavior has been tested.

## Important limitations

- A physical ESP32 connection cannot be validated without the actual board, USB driver, and firmware running.
- The Data Center view is a conceptual deployment representation, not a production DCIM integration.
- Thermal temperatures are scenario values unless firmware provides a real temperature sensor field.
- UCB1 is not shown as active unless firmware telemetry explicitly proves it is active.
- The GUI does not send route-control commands back to the ESP32; it consumes telemetry.
