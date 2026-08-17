# I01 — Presentation & Polish Directions
### Parallax 2026 · Team of 5 · How to win on delivery, not just the tech

> Directions only — nothing to build here. This packages the "make it look good"
> plan: who owns what, how to run the live demo, and the slide / video / booth
> assets that impress judges beyond the innovation. Pairs with
> `I01-final-blueprint.md` (the engineering) and `mesh-command-console.html` (the
> live dashboard).

---

## 1. Split the 5 into lanes (nobody idle, everything owned)

| Person | Lane | Owns these deliverables |
|---|---|---|
| P1 | **Firmware lead** | ESP-NOW, predictor, routing, priority override — the core that must work |
| P2 | **Anomaly & sensors** | MAD-Z + flatline detector, boot calibration, sensor wiring (ADC1 only) |
| P3 | **Dashboard & telemetry** | The live console, serial bridge to the sink node, metric logging |
| P4 | **Demo director** | Runs the booth script, handles the Faraday bag + pot, watches the clock |
| P5 | **Pitch & design** | Slides, 60-sec video, one-pager, sponsor framing, answering judge Qs |

Rule: P1/P2 protect the *core loop*; P3/P4/P5 make it *legible and memorable*. If the
schedule slips, the polish lanes flex to help firmware — but the demo director and one
dashboard owner stay on presentation so the booth is never bare.

---

## 2. Asset priority (build in this order)

1. **Live dashboard** — highest leverage; it's the "magic moment" screen and the
   hardware-failure insurance. Already built: `mesh-command-console.html`.
2. **3-minute run-of-show** — the choreographed demo (Section 4). Rehearse it more than
   you build anything else.
3. **60-second loop video** — pulls people to the booth and covers you if hardware sulks.
4. **8-slide deck** — support act, not the star. Judges skim; the demo carries the pitch.
5. **Booth staging + one-pager** — the physical touches that read "serious team."

Don't invert this. A beautiful deck with a shaky demo loses to a plain deck with a
flawless live reroute.

---

## 3. The live dashboard — how to use it

Open `mesh-command-console.html` in any modern browser. **No internet needed** — it runs
fully offline, which matters at a venue with bad WiFi.

- Four inject controls (buttons or keys **1/2/3/4**, **R** to reset) map one-to-one to
  your physical booth actions, so the screen and the rig tell the same story:
  - **1 · Attenuate Node B** = you drop the Faraday bag → link score slides to threshold →
    reroute fires with a **lead-time in ms** before the timeout. This is the money moment.
  - **2 · Spike @ C** / **3 · Hold Stuck @ C** = twist then freeze the pot → the two
    *separate* detectors light up (spike vs stuck). Proves full-spec coverage.
  - **4 · Send Priority Packet** = a priority packet takes the direct A→S path while normal
    traffic doesn't → the override is visibly different.
- The **SIMULATION MODE** badge is intentional. Keep it honest: quote your **own measured
  hardware numbers** to judges, not the sim's. Use the sim to *rehearse* and as *fallback*.
- To drive it from real hardware: uncomment the WebSerial stub at the bottom of the file
  and have the sink node print newline-delimited JSON
  (`{"linkAB":0.82,"route":"ABS","flagC":"spike","pdr":0.99}`). Chrome/Edge only.
- Keep the whole booth in the **same dark RF-console look** (dashboard, slides, labels) so
  it reads as one product, not a school project.

---

## 4. The 3-minute run-of-show (rehearse until it's muscle memory)

Assign every line to a person. Demo director drives hardware; pitch lead talks.

**[0:00–0:30] Break it on purpose.** No slides. Demo director drops the Faraday bag over
Node B. Pitch lead: *"We just hit our primary relay with an RF drop. A normal mesh freezes
and waits for a timeout."* Say nothing more technical yet — let them lean in.

**[0:30–1:20] The recovery.** Point at the dashboard. *"Our fused RSSI-slope + delivery
score saw it degrading and rerouted through C and D — [X] ms before the timeout would have
fired. No packets lost, no cloud."* Let the reroute marker and lead-time do the talking.

**[1:20–2:10] Edge intelligence + override.** Twist the pot on C (spike), then hold it
still (stuck): *"Two detectors — a robust Z-score for spikes, a separate flatline detector
for stuck values a plain Z-score would miss. And the baseline isn't hardcoded; each node
learned its own at boot."* Fire a priority packet: *"Normal traffic takes the healthy path;
priority overrides to the shortest hop — different route, live."*

**[2:10–3:00] Depth + sponsors + close.** One line on the fused metric, one on the bandit
as a documented next step, then: *"Prediction-before-failure is the SON principle;
surviving link degradation maps to Keysight's Coexistence and Continuity. All on ₹300
boards, fully offline."* Stop. Invite questions.

**Timing discipline:** if a judge interrupts (they will), the demo director quietly resets
(**R**) so you can re-run the reroute on demand without restarting the pitch.

---

## 5. Slide deck directions (8 slides, ≤ 90 sec if talked straight)

Keep it to eight. One idea per slide. Dark console aesthetic, big type, almost no bullets.

1. **Title** — project name, one-line promise ("a mesh that reroutes *before* it fails"),
   team + track.
2. **The gap** — most systems *react* (wait for timeout). Show the cost of that lag.
3. **Architecture** — the layered firmware diagram from the blueprint. Anchor the talk here.
4. **3 novelties** — fused predictor / self-calibrating anomaly (spike **and** stuck) /
   learning-based routing. One line each.
5. **"LIVE DEMO"** — a holding slide. Stop presenting, run the rig. Don't narrate slides.
6. **Metrics** — reroute lead-time, PRR %, false-positive rate. **Your real numbers**,
   filled in the night before.
7. **Sponsor fit** — Nokia SON principle, Keysight Coexistence/Continuity. Honest framing.
8. **Team + what's next** — faces, lanes, and the bandit/ TinyML as roadmap.

Rules: no slide read verbatim, no wall of text, no fake metrics before slide 6 is measured.

---

## 6. 60-second loop video directions

Purpose: draw a crowd from across the room + insurance if the hardware misbehaves.

- **Content:** screen-record the dashboard running the full narrative (attenuate → reroute
  → spike → stuck → priority), intercut with a phone clip of the *real* rig rerouting.
- **Style:** muted, captioned (venues are loud), 60 s, seamless loop.
- **Where it plays:** a second monitor or a phone on a stand, angled to the aisle.
- **Owner:** P5. Shoot the real-rig clip as soon as the hardware first works — don't wait
  for the "perfect" run that may never come.

---

## 7. Booth staging — the "serious team" nooks

Small touches, big credibility:

- **Node labels** (A/B/C/D/S) printed in the same typeface as the dashboard.
- **Faraday bag out as a prop** with a small "pull me" tag — invites judges to break it
  themselves, which is far more convincing than you doing it.
- **One-pager** (A4/Letter): architecture diagram + three-line novelty + QR to the repo.
- **Dashboard laptop angled to the aisle**, video looping on a second screen.
- **A handwritten index card** with your three metric numbers from the last test run.
  Handwritten reads as *real measured data* — exactly the credibility you want.
- Keep the table uncluttered: rig, laptop, one-pager, card, bag. Nothing else.

---

## 8. Rehearsal & fallback plan

- **Rehearse the run-of-show ≥ 5 times** end-to-end, with someone playing an interrupting
  judge. The demo should survive being stopped and restarted mid-sentence.
- **Three failure fallbacks, decided in advance:**
  1. A node dies → swap the spare board (self-healing is your theme — spin it as a feature).
  2. RF too flaky to reroute cleanly → run the dashboard sim; the narrative is identical.
  3. Everything's down → play the 60-sec video and talk over it.
- **Pre-flight checklist (morning of):**
  - [ ] All 5 nodes on Arduino core 3.x, same WiFi channel, RSSI reads confirmed
  - [ ] Both sensors per node on ADC1 pins (GPIO 32/33/34/35/36/39)
  - [ ] Power banks / USB hub charged; spare board flashed and ready
  - [ ] Dashboard opens offline; buttons + keys work
  - [ ] Metric card filled in from last night's real run
  - [ ] Video looping; one-pager + QR printed; node labels on
  - [ ] Faraday bag + spare pot at the booth

---

*Directions only — pairs with `I01-final-blueprint.md` (engineering) and
`mesh-command-console.html` (live dashboard). Protect the core loop first; everything in
this document is how you make that loop legible, memorable, and judge-proof.*
