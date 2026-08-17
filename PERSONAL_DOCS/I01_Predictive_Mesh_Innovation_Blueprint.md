# I01 — Predictive Self-Healing IoT Mesh
## Innovation Blueprint & Build Plan (Parallax 2026, VIT Chennai — IoT Track)

> **Purpose of this document:** take the baseline I01 idea (fused RSSI+PDR link score, modified
> Z-score anomaly detection over ESP-NOW) and push it to a *defensible, judge-proof novelty
> ceiling* using techniques that are (a) published in 2025–2026, (b) genuinely runnable on
> ESP32-class hardware, and (c) low-risk to demo live. Every novelty layer below is mapped to a
> real organizer requirement and backed by a citation.

---

## 0. TL;DR — What makes this build "innovative" and not just "correct"

Most teams will hit the *minimum* requirement: ESP-NOW mesh, RSSI threshold, standard Z-score,
reroute on timeout. That is a passing project, not a winning one. The four upgrades that move you
from "correct" to "novel" — all cheap, all defensible:

1. **Predict, don't react** — fused RSSI-slope + Packet-Delivery-Ratio (PDR) score reroutes
   *before* a timeout. (baseline novelty, already in your plan)
2. **Self-calibrating anomaly detection** — each node *learns its own* median/MAD baseline live
   at boot (autonomous train→infer), so there are zero hard-coded thresholds and no cloud. This is
   the single highest-value, lowest-cost upgrade. ⭐
3. **Learning-based next-hop selection** — replace static distance-vector with a lightweight
   **multi-armed bandit (UCB1 / ε-greedy)** where the "reward" is a successful hop ACK. Stateless,
   faster-converging than RL, fits an MCU. ⭐
4. **Measurable self-healing** — hop-by-hop ACK + controlled retransmit + duplicate suppression,
   which lets you report a real **Packet Recovery Ratio (PRR)** number, not just "it rerouted."

Together these give you three *quantified* talking points (reroute lead-time in ms, PRR %, anomaly
false-positive rate) — judges reward numbers over adjectives.

---

## 1. The Innovation Span (Real Requirement → Novelty Area)

This is the core table. Left = what the organizers literally demand. Middle = what a typical team
ships. Right = your defensible upgrade and the evidence for it.

| # | Organizer Requirement (real) | Baseline (what most teams ship) | **Your Novelty Layer** | Evidence / Why it's defensible |
|---|---|---|---|---|
| R1 | "Predict link degradation from signal-strength trends and reroute **before** failure" | RSSI < fixed threshold → reroute | **Fused link score:** `w1·norm(dRSSI/dt) + w2·norm(PDR_window)`, EWMA-smoothed slope + delivery ratio; reroute on threshold-cross, timeout only as fallback | RSSI alone is a known-weak predictor (measured only in preamble, misses interference); published estimators fuse RSSI with a delivery metric — WMEWMA, F-LQE, LQE-DT (2024) [3][4][5][6][2] |
| R2 | "Flag anomalous readings (spikes, stuck, jumps) with **simple statistics, no training**" | Standard Z-score with hard-coded mean/σ | **Modified (MAD) Z-score** — median/MAD instead of mean/σ, `|0.6745·(x−med)/MAD| > 3.5`; robust to the outlier corrupting its own baseline | Iglewicz–Hoaglin robust outlier formulation; applied directly to RSS/RSSI time-series outliers [7][9][10] |
| R3 | (implied by R2) baseline must exist without cloud or pre-training | Thresholds guessed/hard-coded per sensor | ⭐ **Autonomous on-device baseline learning** — each node runs a short *training phase* at boot to compute its own median/MAD, then transitions to *inference*; self-calibrates per node & per sensor, survives power loss | Validated on MCUs: fully autonomous Z-score TinyML that computes stat params on-device then switches to inference, MCU-only, no cloud [19] |
| R4 | "Lightweight routing (distance-vector or flooding) + proactive reroute" | Static distance-vector, shortest hop count | ⭐ **Bandit-based adaptive next-hop** — each candidate next-hop is an "arm"; reward = delivered+ACKed hop; UCB1/ε-greedy converges online to the best path under changing RF | MAB is a lightweight, *stateless*, faster-converging alternative to RL for routing/resource allocation (IEEE CCNC 2026 tutorial); MAB energy-aware WSN routing precedent [27][24] |
| R5 | "Reroute before a heartbeat timeout would fire" (self-healing) | Reroute, hope packets weren't lost | **Hop-by-hop ACK + controlled retransmit + duplicate suppression (alternate-link triggering)** → yields a reportable **Packet Recovery Ratio** | 5-node ESP32 mesh (MDPI *Sensors* 2025) reports 88.3% avg packet recovery via exactly these self-healing mechanisms [20] |
| R6 | "Support priority messages that override the quality-optimal route" | Single routing mode | **Dual-mode routing:** a priority flag forces shortest-hop/lowest-latency path, bypassing the bandit/link-score path; QoS-style override | Standard QoS-routing pattern; keeps priority latency deterministic regardless of link-score state |
| R7 | "≥4 nodes, peer-to-peer, no cloud coordination" | 4 nodes, ESP-NOW | **ESP-NOW** as transport: connectionless, ~1–10 ms latency vs 50–500 ms for WiFi/MQTT, natively exposes peer RSSI (free input for R1) | Espressif ESP-NOW docs + latency characterizations [1][11][12][13][14] |
| R8 | (stretch / differentiator, optional) | — | **TinyML autoencoder** on ESP32-S3 for anomaly, as an *optional* second detector benchmarked against the MAD Z-score | Honest caveat: ESP32 is **inference-only**; train/quantize off-device, run int8 on-device. Use S3, keep model tiny [12(tinyml)][20(gizan)] |

**How to talk about the span to judges:** "The organizers asked for prediction, anomaly detection,
and routing. For each, the naive answer has a known failure mode — RSSI lies, Z-score corrupts its
own baseline, static routing can't adapt. We took the published fix for each and made all three run
on-device with zero cloud and zero pre-training."

---

## 2. Required Components (Bill of Materials)

### 2.1 Core (mandatory — satisfies all minimum requirements)

| Item | Qty | Notes |
|---|---|---|
| ESP32 DevKitC (or WROOM-32 dev board) | **4–5** | The mesh nodes. 4 is the minimum; 5 gives a redundant path that makes the reroute demo far more convincing. |
| Analog sensor A — **10 kΩ rotary potentiometer** | 1 per node | Cheapest, most *controllable* anomaly source — you can twist it to inject a spike/stuck value live on demand. |
| Analog sensor B — **LDR (photoresistor) + 10 kΩ resistor** | 1 per node | Second analog channel (satisfies "≥2 analog sensors per node"); cover it with your hand to inject a jump. |
| 0.96" **SSD1306 OLED** (I²C) | 1–2 | At least on the "monitor"/sink node for the live readout: link score, reroute event, anomaly flag. Put on 2 nodes if budget allows. |
| Breadboards | 4–5 | One per node. |
| Jumper wires (M-M, M-F) | 1 pack | — |
| Micro-USB / USB-C cables | 4–5 | Match your ESP32 board's port. |
| Powered USB hub **or** power banks | 1–2 | Powering 5 boards off one laptop is unreliable; use a powered hub or 18650 power banks so you can physically *walk a node away* during the demo. |

### 2.2 Demo-staging (strongly recommended)

| Item | Qty | Notes |
|---|---|---|
| Metal box / aluminium foil / anti-static (Faraday) bag | 1 | The "break it live" prop — shield a node to force a predictable RF drop instead of relying on walking away. |
| Laptop running serial plotter / mini dashboard | 1 | Python (`pyserial` + `matplotlib`) or a tiny web page reading serial. Shows RSSI slope + fused score curve — this *is* your novelty proof. |

### 2.3 Optional / stretch (only if a teammate is free and core is already stable)

| Item | Qty | Notes |
|---|---|---|
| **ESP32-S3** dev board | 1–2 | For the TinyML autoencoder stretch (S3 has SIMD + more RAM; classic ESP32 is fine for tiny models but S3 is the honest default for anything ML-shaped). |
| MPU6050 (accel/gyro) or a vibration sensor | 1 | If you want a *richer* anomaly channel (FFT/spectral feature) instead of pot+LDR. FFT-based fault detection on ESP32 is proven (~96% accuracy) but adds integration time. |
| Buzzer / RGB LED | 1 per node | Audible/visible local alert on anomaly or reroute — cheap "wow" factor. |

> **Sourcing note:** everything in 2.1 is off-the-shelf and cheap. This is the whole point of I01 —
> **no motors, no load cells, no camera calibration, nothing that mechanically fails on stage.** If
> a board dies, swap it; the mesh is supposed to self-heal anyway, which you can spin as a feature.

---

## 3. System Architecture

```
        ┌─────────┐   ESP-NOW    ┌─────────┐   ESP-NOW    ┌─────────┐
        │ Node A  │─────────────▶│ Node B  │─────────────▶│ Node C  │
        │ (source)│              │ (relay) │              │ (sink)  │
        └────┬────┘              └────┬────┘              └────┬────┘
             │                        │                        │  OLED + dashboard
             │        ┌─────────┐     │     ┌─────────┐        │
             └───────▶│ Node D  │◀────┴────▶│ Node E  │◀───────┘
                      │ (relay) │           │ (relay) │
                      └─────────┘           └─────────┘
   Redundant paths: A→B→C  and  A→D→E→C  (bandit picks; priority forces shortest)

Per-node firmware stack (bottom → top):
  [ ESP-NOW transport ]  ← peer RSSI exposed here (free)
  [ Neighbor discovery / HELLO beacons ]  → bidirectional link table
  [ Link predictor: EWMA(dRSSI/dt) + PDR_window → link_score ]   (R1)
  [ Anomaly engine: autonomous MAD-Z baseline → inference ]      (R2, R3)
  [ Router: MAB next-hop (UCB1) + priority shortest-hop override ] (R4, R6)
  [ Reliability: hop-by-hop ACK + retransmit + dup-suppression ]  (R5)
  [ Reporting: OLED + serial → dashboard ]
```

**Data every node keeps (all tiny, all in RAM):**
- Neighbor table: `{peer_mac, last_rssi, ewma_slope, pdr_window[N], link_score}`
- Anomaly baseline per sensor: `{median, MAD}` (frozen after training phase)
- Bandit state per next-hop: `{reward_count, try_count}` (for UCB1)

---

## 4. Build Order (36-hour plan)

| Hours | Milestone | Deliverable / checkpoint |
|---|---|---|
| 0–2 | **Bring-up** | Flash 4–5 boards; ESP-NOW broadcast + peering works; confirm you can read **peer RSSI on every packet**. |
| 2–6 | **Neighbor discovery + baseline routing** | HELLO beacons build a bidirectional link table; a packet from A reaches C multi-hop (static distance-vector first). |
| 6–12 | **Link predictor (R1)** | EWMA RSSI-slope → add PDR window → fuse into `link_score`. **Log raw RSSI *and* fused score** so the improvement is visible live. |
| 12–17 | **Anomaly engine (R2+R3)** | Standard Z-score first (for the A/B comparison slide), then MAD Z-score, then wrap it in the **autonomous train→infer** phase. Inject synthetic spikes/stuck values on pot+LDR to validate. |
| 17–23 | **Adaptive routing (R4+R6)** | Swap static next-hop for **UCB1 bandit** (reward = ACKed hop). Add the **priority flag** → shortest-hop override. |
| 23–28 | **Self-healing reliability (R5)** | Hop-by-hop ACK + controlled retransmit + duplicate suppression. Start logging **PDR** and **PRR**. |
| 28–32 | **Demo staging** | Build the "break it live" moment: shield/move a node, show OLED/serial reporting the *predictive* reroute **before** timeout would have fired. Capture the reroute lead-time in ms. |
| 32–36 | **Polish + pitch** | 3-min demo script; metrics slide (lead-time, PRR, false-positive rate); novelty talking points with citations. |

**Cut-line discipline:** if you're behind at hour 23, ship R1+R2+R3+R5 solidly and demote the
bandit (R4) to "static distance-vector + we describe the bandit as designed-and-partially-integrated."
Never let the stretch TinyML (R8) touch the critical path.

---

## 5. Metrics to Report (this is what wins)

Bring a slide with real numbers from your own runs:

| Metric | What it proves | How to measure |
|---|---|---|
| **Reroute lead-time (ms)** | You predicted, didn't react | Time between `link_score` crossing threshold and when the heartbeat-timeout *would* have fired |
| **Packet Delivery Ratio (PDR %)** | Baseline link health | Delivered ÷ sent DATA packets |
| **Packet Recovery Ratio (PRR %)** | Self-healing actually recovers loss | Lost packets later recovered via retransmit ÷ total lost (the 2025 ESP32 mesh paper reports ~88% [20] — a good comparison anchor) |
| **Anomaly false-positive rate** | Your detector isn't crying wolf | Flags raised on known-normal injected data ÷ total normal samples |
| **Standard-Z vs MAD-Z catch rate** | Justifies the "modified" choice | Run both on the same injected anomaly; show standard-Z misses the absorbed outlier |

---

## 6. The 3-Minute Live Demo Script

**[0:00–0:30] The hook — break it on purpose.** Don't open slides. Drop the Faraday bag over
Node B (or walk it away). Say nothing technical yet.

**[0:30–1:30] The recovery — the "magic."** Point at the OLED/dashboard:
"Link score on A→B just crossed threshold — the mesh rerouted A→D→E→C **12 ms before** the timeout
would have even fired. No packets lost, no cloud, all on-device." Show the serial plotter: the
RSSI-slope curve dipping and the reroute event firing *ahead* of the flat-line.

**[1:30–2:30] Architectural depth.** Show three things fast: (1) the fused `link_score` formula and
why RSSI alone fails; (2) the MAD Z-score self-calibrating baseline — twist the potentiometer,
watch the anomaly flag fire, note "we never hard-coded that threshold, the node learned it at boot";
(3) the bandit picking the recovered path and a priority packet jumping to shortest-hop.

**[2:30–3:00] Sponsor + close.** "Prediction-driven optimization before failure is exactly the
principle behind Self-Organizing Networks; surviving link degradation maps to Keysight's
Coexistence and Continuity. Everything you saw ran on ₹300 ESP32 boards with zero cloud."

---

## 7. Sponsor Framing (accurate — don't over-claim)

- **Nokia** — pitch as *"inspired by Self-Organizing Network (SON) principles"* (proactive,
  prediction-driven optimization before failure). Nokia Bell Labs' published SON work is on
  cellular macro-network handover/load-balancing, **not** IoT mesh routing — so claim the
  *conceptual parallel*, not literal equivalence [15][16].
- **Keysight** — cite **Coexistence** and **Continuity** from the real "5 C's of IoT" (Connectivity,
  Continuity, Compliance, Coexistence, Cybersecurity); both map directly to a mesh that predicts and
  survives link degradation [17]. Don't fold "Signal Integrity" or "Channel Emulation/HIL" into the
  5 C's — those are separate Keysight areas.
- **e-con Systems** — **skip.** Their embedded-vision hardware targets Jetson/NXP/Qualcomm-class SoCs
  via MIPI CSI-2/GMSL, which doesn't naturally interface with this ESP32 build [18]. Forcing this
  hook will read as padding to a technical judge.

---

## 8. Honesty Guardrails (so a sharp judge can't puncture you)

- **RSSI is noisy** — say so first, then explain that's *why* you fuse it with PDR. Owning the
  weakness pre-empts the obvious question.
- **The bandit needs a few packets to converge** — frame it as online learning; show it settling.
- **TinyML on ESP32 is inference-only** — if you demo the autoencoder, state that you train/quantize
  off-device and run the int8 model on-device. Never imply the ESP32 "trains a neural net."
- **Your MAD baseline assumes a mostly-clean training window** — mention you validate the training
  phase before freezing the baseline.

---

## 9. References

**Link quality / prediction**
1. Espressif — ESP-NOW Wireless Communication Protocol. https://www.espressif.com/en/solutions/low-power-solutions/esp-now
2. Estimating and Predicting Link Quality in Wireless IoT Networks (LQE-DT, 2024). https://www.researchgate.net/publication/350145894
3. Assessing Link Quality in IEEE 802.11 Wireless Networks (RSSI limitations). https://cs.ucr.edu/~krish/pimrc08.pdf
4. Improving Link Quality Estimation Accuracy Using Fuzzy Logic in Mobile WSN (WMEWMA). https://www.hindawi.com/journals/afs/2019/3478027/
5. F-LQE: A Fuzzy Link Quality Estimator for WSNs. https://www.researchgate.net/publication/225122668
6. RADIUS: Detecting Anomalous Link-Quality Degradation in WSNs. https://arxiv.org/pdf/1701.00963

**Anomaly detection (statistical + on-device)**
7. Outlier Detection in Time-Series RSS Using Modified Z-Score / MAD. https://www.mdpi.com/2076-3417/13/6/3900
8. Z-Score Anomaly Detection — thresholds & robust Z-scores. https://mcpanalytics.ai/articles/z-score-anomaly-detection-practical-guide-for-data-driven-decisions
9. Modified Z-Score (Iglewicz–Hoaglin robust outlier). https://metricgate.com/docs/robust-z-score-modified/
10. Z-Score & Modified Z-Score — outlier techniques. https://medium.com/@fawwazmts/z-score-and-modified-z-score-f689296e4d3a
19. **Fully Autonomous Z-Score-Based TinyML Anomaly Detection on Resource-Constrained MCUs** (2026) — on-device train→infer, MCU-only, no cloud. https://arxiv.org/pdf/2604.08581

**ESP-NOW transport**
11. ESP-NOW: Ultra-Fast Peer-to-Peer ESP32 Communication. https://zbotic.in/esp-now-protocol-ultra-fast-peer-to-peer-esp32-communication/
12. Espressif — ESP-NOW, ESP-IDF Programming Guide. https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html
13. Exploring Wireless Protocols on ESP32 for Outdoor Applications. https://developer.espressif.com/blog/esp-now-for-outdoor-applications/
14. MicroPython espnow module (peer RSSI). https://docs.micropython.org/en/latest/library/espnow.html

**Self-healing mesh + reliability (new, 2025)**
20. **Gateway-Free LoRa Mesh on ESP32: Self-Healing Mechanisms & Empirical Performance** (MDPI *Sensors* 2025, 25(19):6036) — hop-by-hop ACK, controlled retransmit, alternate-link triggering; ~88% packet recovery; PDR/PRR metrics. https://www.mdpi.com/1424-8220/25/19/6036
21. Espressif — ESP-WIFI-MESH self-healing / root re-election docs. https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/esp-wifi-mesh.html

**Learning-based routing (new)**
24. MABRP: Multi-Armed Bandit energy-aware routing for WSN. https://www.researchgate.net/publication/261228058
27. **MAB Algorithms in Next-Gen Wireless Networks — a Lightweight, Stateless Alternative to RL** (IEEE CCNC 2026 tutorial). https://ccnc2026.ieee-ccnc.org/node/12936

**Sponsor references**
15. Nokia Bell Labs — AI for Self-Organized Networks. https://www.bell-labs.com/research-innovation/projects-and-initiatives/air-lab/modelling-optimization/research/ai-for-self-organized-networks/
16. Compositional Learning for Modular Multi-Agent SON (Nokia Bell Labs). https://arxiv.org/pdf/2506.02616
17. Keysight — IoT Test Solutions ("5 C's of IoT"). https://www.keysight.com/us/en/learn/hubs/internet-of-things-iot.html
18. e-con Systems — MIPI CSI-2 Camera Module line. https://www.e-consystems.com/cameramodule.asp

**TinyML feasibility (stretch)**
- Edge AI & TinyML on ESP32 — inference-only honest caveat, S3 as default. https://gizantech.com/blog/edge-ai-tinyml-on-esp32-on-device-machine-learning

---

*Prepared for Parallax 2026 · VIT Chennai · IoT Track · I01. Novelty layers marked ⭐ are the
highest value-to-effort upgrades; ship those first after the minimum requirements are met.*
