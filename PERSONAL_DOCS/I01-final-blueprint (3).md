# I01 — Predictive Self-Healing IoT Mesh with Real-Time Anomaly Detection
**Parallax 2026, VIT Chennai — Internet of Things Track**
**Status: Final consolidated blueprint — supersedes all earlier drafts**

---

## 1. Problem Statement

Build a wireless IoT mesh of at least 4 ESP32 nodes that predicts link degradation from
real-time signal-strength trends and reroutes traffic **before** a connection fully
fails, instead of waiting for a timeout. Each node must simultaneously:

- Monitor its own analog sensors and flag anomalous readings (spikes, stuck values,
  sudden jumps) using simple statistical methods — no prior training, no cloud
  coordination.
- Support priority messages that override the normal quality-optimal route.

Organizer-specified dependencies: 4–5 ESP32 boards on peer-to-peer wireless (ESP-NOW,
WiFi, BLE, or LoRa); neighbor discovery for dynamic bidirectional links; a real-time
signal-quality predictor (RSSI-based EWMA/slope or Kalman filter) with heartbeat-timeout
as fallback; lightweight routing (distance-vector or flooding) with proactive rerouting
and a priority/shortest-hop override; at least 2 analog sensors per node.

## 2. Why This Problem Statement

| Factor | I01 | C01 (FHSS mesh) | H01 (med auth) |
|---|---|---|---|
| Physical failure risk on stage | Low — firmware/algorithm only | Medium — RF sync + live jammer must work flawlessly | High — load-cell tare drift, camera lighting sensitivity |
| Hardware complexity | Off-the-shelf ESP32 boards only | SPI RF transceivers + dedicated jammer node | RFID, HX711 load cell, ESP32-CAM, Flask/SQLite backend |
| Depth of published research to draw novelty from | Deep — RSSI/link-quality estimation is a mature WSN research area | Moderate | Moderate, calibration-heavy |
| Sponsor technical fit | Genuine — Nokia SON research, Keysight Coexistence/Continuity | Genuine — Keysight RF testing, Nokia mission-critical comms | Weaker than assumed — e-con's real hardware (MIPI/GMSL, Jetson-class) doesn't interface with ESP32-CAM |

I01 gives the best ratio of demo reliability to genuine, citable algorithmic novelty.

## 3. Proposed Solution — Architecture

```
┌───────────────────────────────────────────────────────────────────────┐
│                    EDGE MESH FIRMWARE ARCHITECTURE                    │
├───────────────────────────────────────────────────────────────────────┤
│ Reporting Layer     OLED (Nodes S, C)  |  Serial/WebSerial Dashboard  │
├───────────────────────────────────────────────────────────────────────┤
│ Routing Layer       Distance-Vector (baseline)  |  Priority Override  │
│                      [Stretch: UCB1 Multi-Armed Bandit next-hop]      │
├───────────────────────────────────────────────────────────────────────┤
│ Reliability Layer    Hop-by-Hop ACK  |  Retransmit  |  Dup Filter     │
├───────────────────────────────────────────────────────────────────────┤
│ Predictor Layer      Fused Score: slope(EWMA-RSSI) + PDR Window        │
├───────────────────────────────────────────────────────────────────────┤
│ Anomaly Engine       Calibrate → MAD-Z (spikes/jumps) + Flatline(stuck)│
├───────────────────────────────────────────────────────────────────────┤
│ Transport Layer      ESP-NOW P2P  (Arduino core 3.x, fixed channel)    │
└───────────────────────────────────────────────────────────────────────┘
```

**Note on UCB1 bandit routing [10]:** treat this as a stretch goal, not a core deliverable.
Distance-vector routing satisfies the spec on its own; only add the bandit layer if
Phase 5 of the timeline (below) starts early. If you fall behind, degrade to
distance-vector and simply mention UCB1 as a documented next step — don't claim it's
running if it isn't.

### 3.1 Predictive Link Degradation Model

RSSI alone is a weak stand-alone predictor — it's sampled only during the packet
preamble and often stays flat even as real interference builds, so predicting failure
from RSSI slope alone risks missing exactly the degradation you're trying to catch [3].
The fix, drawn from published link-quality estimators (WMEWMA [4], F-LQE [5], and the
fused RSSI-plus-delivery link-prediction work [2]), is to fuse the RSSI trend with a
delivery-based metric.

**Order of operations matters — smooth first, then differentiate.** Do *not* take the
raw derivative and smooth it afterwards; differentiating a noisy signal amplifies the
noise. Smooth RSSI with an EWMA, then estimate the trend from the smoothed series:

```
rssi_ewma(t) = α·rssi(t) + (1-α)·rssi_ewma(t-1)     (α ≈ 0.3)
slope(t)     = least-squares slope of rssi_ewma over the last W samples  (signed: <0 = degrading)
PDR(t)       = delivered / sent over a 20-frame sliding window            (already in [0,1])

# Polarity convention: HIGHER link_score = HEALTHIER link.
degrade_term = clamp(-slope(t) / SLOPE_REF, 0, 1)    # only a FALLING link contributes; rising ⇒ 0
link_score   = w1·(1 - degrade_term) + w2·PDR(t)      # w1 = w2 = 0.5 to start
```

Note the sign handling: because `slope` is signed, an *improving* link (positive slope)
must never push you toward a reroute. `degrade_term` is zero unless the trend is falling,
so `link_score` only drops when the link is genuinely degrading **and/or** PDR is falling.
Reroute when `link_score` stays **below** threshold for 3 consecutive evaluations, with
the heartbeat timeout as a hard fallback if the predictor misses.

**Timing (pin these — "predict before timeout" depends on it):** the whole thesis fails
if the timeout fires before the predictor reacts. Use a fast heartbeat so the slope has
resolution, and set the timeout several times longer than the predictor's reaction:

| Parameter | Starting value | Why |
|---|---|---|
| Heartbeat interval | 100–200 ms | Gives the slope estimator resolution; 1 Hz is far too slow to beat a timeout. |
| Slope window `W` | ~15–20 samples (≈2–4 s) | Enough to see a trend without lagging the event. |
| PDR window | 20 frames | Matches ~2–4 s at the above rate. |
| Heartbeat timeout | 3–5× predictor reaction (~1–2 s) | Guarantees the proactive reroute normally wins; timeout is only a safety net. |

#### 3.1.1 Transport & toolchain constraints (get these right at Hour 0)

- **RSSI source is core-version-dependent.** Per-packet peer RSSI is only exposed in the
  ESP-NOW receive callback on **Arduino-ESP32 core 3.x (ESP-IDF ≥ 5.1)**, where the
  callback's first argument is `esp_now_recv_info_t*` and you read `info->rx_ctrl->rssi`.
  On core **2.x** the callback gives you only the MAC — **no RSSI** — and you'd have to
  add a promiscuous-mode sniffer. Pin the toolchain to core 3.x and use the new callback
  signature; do not copy pre-3.0 tutorials (several fail to compile on 3.0.0). The entire
  predictor depends on this one fact.
- **Fix the WiFi channel on every node.** ESP-NOW peers must share a channel; if any
  node's channel drifts (anything touching WiFi STA/AP can cause it), links drop silently.
  Set the channel explicitly at boot on all five nodes. Keep the dashboard on **serial /
  WebSerial (USB)** — not WiFi — so it never forces a channel change.
- **ESP-NOW limits:** ≤ 250-byte payload, < 20 peers total. Fine at 5 nodes; keep packets
  small. Per-hop PDR comes free from the send callback's success/fail status (a link-layer
  ACK), so the delivery half of `link_score` needs no extra protocol.

### 3.2 Robust Statistical Anomaly Detection (Modified Z-Score / MAD)

A standard Z-score is undermined by the anomaly it's meant to catch — one extreme
reading drags the mean and inflates the standard deviation, hiding the very spike
you're looking for. The fix is the modified Z-score based on median absolute deviation
(MAD) [6], demonstrated directly on time-series signal-strength outliers [8], applied
here per-node, per-sensor:

```cpp
// MAD_FLOOR ≈ a few ADC LSB (e.g. ~3 counts on the 12-bit ADC), NOT an arbitrary tiny number.
// A static-but-healthy sensor has MAD≈0; flooring to 0.001 makes normal ±1–2 LSB noise
// read as |z|≫3.5 and fire false anomalies. Floor to real quantisation noise instead.
float compute_modified_z_score(float new_val, float median, float mad) {
    if (mad < MAD_FLOOR) mad = MAD_FLOOR;     // realistic ADC-noise floor, not 0.001
    return 0.6745f * fabsf(new_val - median) / mad;
}
// flag anomaly if |modified_z| > 3.5   (threshold per Iglewicz & Hoaglin [6])
```

**Boot calibration (train → infer):** buffer the first ~100 raw ADC samples per node
at boot to compute a local median/MAD baseline, freeze it into RAM, then evaluate every
subsequent reading against that frozen baseline. This on-device compute-then-infer
pattern — statistics learned live on the MCU with no cloud step — follows the autonomous
Z-score TinyML workflow validated on resource-constrained microcontrollers [7]. It
satisfies the "no cloud, no pre-training" requirement while still giving each node a sensible per-environment
threshold instead of a hardcoded one. If the standard deviation during the boot window
exceeds a safety envelope, restart calibration automatically.

**Stuck-value detection (separate detector — MAD-Z cannot do this).** The spec asks for
three anomaly types: spikes, jumps, and **stuck values**. The modified-Z score catches
the first two, but a sensor frozen at (or near) its own median gives `z ≈ 0` and is
*invisible* to it — a point-anomaly test structurally cannot flag a flatline. Add a
tiny dedicated detector that runs alongside MAD-Z:

```cpp
// Flag "stuck" if the reading hasn't moved more than EPS (a few ADC LSB) for N samples.
if (fabsf(new_val - last_val) < EPS) stuck_count++;   else stuck_count = 0;
last_val = new_val;
bool stuck = (stuck_count >= STUCK_N);                 // e.g. STUCK_N ≈ 50 samples
```

Report the two detectors as distinct flags (`SPIKE/JUMP` vs `STUCK`) so the demo can
show both: twist the pot for a spike, then **hold it dead still** to trip the stuck
flag — the second half is the part a naive Z-score-only entry silently fails.

## 4. Hardware Topology (5 Nodes)

```
                        [Node A: Source]  ESP32 + Pot + LDR
                       /        |          \
        weak direct   /  quality path       \  backup path
        (1 hop,      /   (2 hop, strong)      \  (3 hop, strong)
         low RSSI)  v          v               v
        ┌──────────┘   [Node B: Relay]    [Node C: Relay]
        │              ESP32+Pot+LDR       ESP32+Pot+LDR + OLED #2
        │                   |                    |
        │                   v                    v
        │               (to Sink)           [Node D: Relay]  ESP32+Pot+LDR
        │                   |                    |
        v                   v                    v
                        [Node S: Sink/Root]  ESP32 + OLED #1 + Serial
```

**Links:** `A–S` (direct, deliberately weak/long — fewest hops but lowest quality),
`A–B–S` (2-hop, strong = normal quality-optimal path), `A–C–D–S` (3-hop, strong = backup).

**Two routing modes, made visibly different by this topology:**
- *Normal / quality-optimal traffic* takes **A→B→S** while B is healthy. Drop the Faraday
  bag over **Node B** → the fused link score falls → mesh proactively reroutes to
  **A→C→D→S** (it prefers the strong 3-hop path over the weak 1-hop direct link). Point to
  **Node S's OLED/dashboard** for the reroute telemetry and the lead-time.
- *Priority traffic* ignores link quality and forces **shortest-hop A→S** (1 hop) — the
  weak-but-fastest path. This is why the priority override is a *distinct*, visible
  behaviour: send a normal packet (goes A→B→S / A→C→D→S) and a priority packet (goes direct
  A→S) back-to-back and the two take different routes on the dashboard. Without the weak
  direct link, shortest-hop and quality-optimal would coincide and the override would be
  invisible.

**Anomaly demo:** twist the potentiometer on **Node C** → **Node C's OLED** shows the
`SPIKE/JUMP` flag from the MAD-Z detector; then hold it dead still → the same OLED shows
the separate `STUCK` flag from the flatline detector.

If a judge asks about Node D specifically, note that it reports over serial only — no
dedicated OLED, by design, to keep the BOM at 5 nodes.

## 5. Complete Bill of Materials

**Compute (5 nodes: S, A, B, C, D)**
- 5× ESP32 DevKitC / WROOM-32 (30-pin, dual-core, 240 MHz, 520 KB SRAM)
- 5× Micro-USB or USB-C data cables (flashing + power — match your boards' port type)

**Sensing (2 analog channels per node × 5 nodes)**
- 5× rotary potentiometers (10 kΩ, single-turn linear) — Channel A, anomaly injection
- 5× LDR photoresistors (GL5528 or similar) — Channel B, ambient/shadow-step detection
- 5× 10 kΩ resistors — pairs with each LDR to form the voltage divider

> **Critical wiring rule — use ADC1 pins only.** ESP-NOW runs on the WiFi radio, and on
> the classic ESP32 the ADC2 pins are shared with the WiFi driver: once the mesh is up,
> any `analogRead()` on an ADC2 pin returns an error or garbage. Wire **both** the pot and
> the LDR on every node to **ADC1 pins only — GPIO 32, 33, 34, 35, 36, or 39** (34/35/36/39
> are input-only, which is fine for analog reads). Do **not** use GPIO 0/2/4/12–15/25–27
> for these sensors. This is the single most likely "sensor is lying" bug on the day, and
> it only appears *after* the radio starts — so it won't show up in a quick pre-mesh test.

**Display / local feedback**
- 2× 0.96" SSD1306 OLED, I2C, 128×64 — Node S (mesh telemetry), Node C (local anomaly flag)
- 5× piezo buzzer modules (optional, cheap) — instant audible anomaly cue on every node.
  If used, drive them from a **regular output GPIO** — never GPIO 34/35/36/39, which are
  input-only and cannot drive an output.

**Prototyping / wiring**
- 5× solderless breadboards (400-point)
- 1 pack jumper wires — M-M, M-F, F-F, 22 AWG
- Small roll of tape or heat-shrink (strain-relief so a lead doesn't fall out mid-demo)

**Power**
- 1× powered USB hub (5V/2.4A per port min) **or** 5× 18650 battery packs with USB out
  — decouples nodes from a single laptop's throttled ports
- If using 18650s: 5× cells + a 5-bay charger, charged the night before
- 1× multi-outlet power strip for the venue table

**Demo-staging fixtures**
- 1× anti-static Faraday bag or small metal enclosure — RF attenuation over Node B
- 1× laptop running a serial/WebSerial telemetry dashboard (pyserial + matplotlib, or
  a simple web page) plotting RSSI, link_score, and anomaly flags live

**Tools**
- Small Phillips screwdriver + tweezers
- Basic multimeter (verify voltage dividers before assuming firmware is the bug)
- Spare jumper wires and one spare ESP32 board

**Stretch only — don't buy until core system (through Phase 6) is done**
- 1–2× ESP32-S3 dev board, if attempting the optional TinyML autoencoder goal

**Software / dev environment (set up before the event starts)**
- Arduino IDE or PlatformIO with **ESP32 board support core 3.x (IDF ≥ 5.1)** pre-installed
  — required for per-packet RSSI in the ESP-NOW receive callback
- ESP-NOW example tested on 2 boards beforehand, confirming `info->rx_ctrl->rssi` reads
- Python 3 with `pyserial` and `matplotlib` installed on the dashboard laptop

## 6. 36-Hour Build Timeline & Cut-Line

| Hours | Phase |
|---|---|
| 0–2 | Bring-up: pin Arduino core 3.x, fix WiFi channel on all nodes, ESP-NOW peering, confirm `rx_ctrl->rssi` reads |
| 2–6 | Multi-hop discovery & static (distance-vector) routing + priority shortest-hop override |
| 6–12 | Fused link predictor (smooth RSSI → slope + PDR), with pinned timing |
| 12–17 | Anomaly engine: boot-calibrated MAD-Z (spikes/jumps) **and** flatline detector (stuck) |
| 17–23 | Hop-by-hop ACK, retransmit, dup-filter & packet-recovery metrics |
| 23–28 | [Stretch] UCB1 bandit next-hop selection |
| 28–32 | Live demo staging & hardware stress testing |
| 32–36 | Pitch polish, real metrics capture, panel practice |

**Cut-line at Hour 23:** the fused predictor, MAD-Z + flatline anomaly engine, priority
override, and hop-by-hop ACK are all **core, required** deliverables — keep them. Only
the UCB1 bandit (Phase 23–28) is optional; demote it to plain distance-vector if you're
behind and present the bandit code as an integrated-but-optional module rather than
claiming it's live if it isn't. Note that priority override and reliability moved *out*
of the stretch slot — they're spec requirements (R6 / self-healing), not extras.

## 7. Evaluation Metrics — Report as Targets, Not Pre-Measured Results

Do **not** present specific numbers (e.g., a lead-time or recovery-ratio figure) to
judges until you've actually logged them from your own hardware. Track these three
live during testing and quote your real numbers on the day:

1. **Reroute Lead-Time** — time between the proactive reroute and when the heartbeat
   timeout *would* have fired. This is a counterfactual, so instrument it explicitly: log
   the last-heartbeat timestamp, compute the timeout deadline you deliberately avoided, and
   report `deadline − reroute_time`. It won't appear on its own — you have to record both.
2. **Packet Recovery Ratio (PRR%)** — dropped packets successfully re-delivered via an
   alternate route, out of total dropped. (A comparable ESP32 mesh reports ~88% PRR via
   hop-by-hop ACK + retransmit + alternate-link triggering [9] — a fair yardstick to aim
   near, but quote *your own* measured figure on the day.)
3. **Anomaly False-Positive Rate** — false alarms during clean operation ÷ total
   baseline samples.

## 8. Live Judging Script (3 Minutes)

**[0:00–0:30] The interactive failure.** Drop the Faraday bag over Node B — no
software controls touched. *"We just introduced a sudden RF attenuation drop on our
primary routing node. A standard mesh would freeze and wait for a link timeout."*

**[0:30–1:30] Proactive recovery.** Point to Node S's OLED/dashboard. *"Our fused
RSSI-slope and PDR score detected degradation and rerouted through Node C and D before
the heartbeat timeout expired — you can see it happen live on the plot."*

**[1:30–2:20] Edge intelligence + priority override.** Twist the potentiometer on Node C
sharply, then hold it dead still. *"Node C's OLED flags the spike instantly with a robust
MAD Z-score — and when I hold it frozen, a separate flatline detector catches the stuck
value, which a plain Z-score would miss entirely. The baseline wasn't hardcoded; Node C
learned its own median and MAD at boot, zero cloud."* Then fire a priority packet: *"Normal
traffic takes the healthy multi-hop path; a priority message overrides that and forces the
shortest hop — you can see the two take different routes here."*

**[2:20–3:00] Sponsor framing.** *"Our proactive link optimization is inspired by the
architectural goals of Nokia's Self-Organizing Networks — we're not claiming
literal parity with a cellular-grade SON stack, just the same predictive philosophy.
Our link evaluation maps to Keysight's Coexistence and Continuity pillars specifically.
All of this runs on off-the-shelf microcontrollers, fully offline."*

## 9. Sponsor Framing

- **Nokia** — "inspired by SON principles" (predictive optimization, autonomous
  self-healing). Do not claim literal equivalence with cellular macro-network SON
  software.
- **Keysight** — cite **Coexistence** and **Continuity** specifically, two of the real
  5 C's of IoT (Connectivity, Continuity, Compliance, Coexistence, Cybersecurity).
  Don't fold "Signal Integrity" or "Channel Emulation" into that named framework —
  they're separate Keysight product lines.
- **e-con Systems** — skip this hook entirely. Their hardware (MIPI CSI-2/GMSL for
  Jetson-class SoCs) has no real interface path to ESP32.

## 10. Pre-Empt These Judge Questions

**"RSSI is notoriously noisy — how can you trust it for routing?"**
*"We agree — that's why we don't use RSSI alone. We EWMA-smooth RSSI first, then take the
slope of the smoothed signal (not the raw derivative), and fuse it with a 20-frame
packet-delivery-ratio window — so a noisy-but-still-delivering link doesn't trigger a
false reroute."*

**"A Z-score can't detect a stuck sensor — it sits at the median. How do you catch that?"**
*"Correct, and that's exactly why we run two detectors. The MAD Z-score catches spikes and
jumps; a separate flatline detector counts consecutive unchanged samples to catch stuck
values. Both flags are reported independently."*

**"Does your MAD Z-score assume a clean training phase?"**
*"Yes — boot calibration assumes reasonably stable initial conditions. We check
variance during the first ~100 samples and auto-restart calibration if it's out of
a safe range."*

## 11. References

All entries below were verified against their source this round. Where the earlier draft
carried invented author/venue details, they have been corrected; where real papers had
been mistakenly flagged "unverified," they have been restored.

**Transport**
1. Espressif Systems, "ESP-NOW Wireless Communication Protocol," Espressif docs.
   https://www.espressif.com/en/solutions/low-power-solutions/esp-now

**Link-quality estimation / prediction**
2. "Estimating and predicting link quality in wireless IoT networks," *Annals of
   Telecommunications*, Springer, 2021. DOI 10.1007/s12243-021-00835-1.
   https://link.springer.com/article/10.1007/s12243-021-00835-1
   *(This is the fused RSSI + delivery-metric / decision-tree link-prediction source. The
   earlier "Cluster Computing 2025 / 10.1007/s10586-025-05231-1" citation was fabricated —
   do not use it.)*
3. "Assessing Link Quality in IEEE 802.11 Wireless Networks," PIMRC, 2008.
   https://cs.ucr.edu/~krish/pimrc08.pdf
   *(This is the paper the URL actually points to — the RSSI-is-a-weak-standalone-metric
   evidence. The earlier "Aguayo et al., ACM SIGCOMM 2004" label was the wrong paper for
   this link.)*
4. "Improving the Accuracy Rate of Link Quality Estimation Using Fuzzy Logic in Mobile
   Wireless Sensor Network" (describes WMEWMA), Hindawi *Advances in Fuzzy Systems*, 2019.
   https://www.hindawi.com/journals/afs/2019/3478027/
5. N. Baccour, A. Koubâa, H. Youssef, M. Ben Jamâa, D. do Rosário, M. Alves, L. B. Becker,
   "F-LQE: A Fuzzy Link Quality Estimator for Wireless Sensor Networks," EWSN 2010, LNCS
   vol. 5970, pp. 240–255, Springer. DOI 10.1007/978-3-642-11917-0_16.
   *(Authorship corrected — the earlier "F. O. Silva et al." attribution was wrong.)*

**Anomaly detection (statistical + on-device)**
6. B. Iglewicz and D. C. Hoaglin, *How to Detect and Handle Outliers*, ASQC Basic
   References in Quality Control, vol. 16, 1993. *(Source of the 0.6745 constant and the
   3.5 modified-Z threshold. Note: Boris Iglewicz — initial B., not D.)*
7. A. Albaiz, F. Amsaad, "Fully Autonomous Z-Score-Based TinyML Anomaly Detection on
   Resource-Constrained MCUs Using Power Side-Channel Data," arXiv:2604.08581, 2026.
   https://arxiv.org/abs/2604.08581 *(Backs the boot-time train→infer, cloud-free
   calibration.)*
8. "Outlier Detection in Time-Series Receive Signal Strength Observation Using Z-Score
   Method with Sn Scale Estimator for Indoor Localization," *MDPI Applied Sciences*,
   vol. 13, no. 6, art. 3900, 2023. https://www.mdpi.com/2076-3417/13/6/3900
   *(Journal corrected: Applied Sciences, not Sensors. Real, usable citation for a robust
   Z-score on RSS time series. Note it uses the Sn scale estimator rather than MAD
   specifically — both are robust scale estimators, so cite it as "robust Z-score on RSS,"
   not as a MAD result.)*

**Self-healing mesh + reliability (real — restored)**
9. D. Arregui Almeida, J. Chafla Altamirano, M. Román Cañizares, P. Palacios Játiva,
   J. Guaña-Moya, I. Sánchez, "Gateway-Free LoRa Mesh on ESP32: Design, Self-Healing
   Mechanisms, and Empirical Performance," *MDPI Sensors*, vol. 25, no. 19, art. 6036,
   2025. DOI 10.3390/s25196036. https://www.mdpi.com/1424-8220/25/19/6036
   *(Verified real. Reports ~88% packet-recovery via hop-by-hop ACK + retransmit +
   alternate-link triggering — the evidence for your PRR / self-healing claim.)*

**Learning-based routing (real — restored)**
10. "Multi-Armed Bandit Algorithms in Next-Generation Wireless Networks: A Lightweight,
    Stateless Alternative to Reinforcement Learning," Tutorial TUT-02, IEEE Consumer
    Communications & Networking Conference (CCNC), 2026.
    https://ccnc2026.ieee-ccnc.org/node/12936
    *(Verified real. Backs framing the UCB1 bandit as a lighter, faster-converging
    alternative to RL for routing on constrained nodes.)*
