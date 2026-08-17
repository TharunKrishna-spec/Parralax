# Testing

No physical hardware exists yet, so nothing in this document claims a
hardware-dependent pass. What follows is exactly what was and wasn't
validated, and how.

## Phase 2 — predictor math + routing integration, both layers actually run

Two independent, real validation passes, per the Phase 2 task spec's
explicit requirement to perform both:

### 1. Host tests (predictor mathematics/state machine + routing integration)

```
$ g++ -std=c++17 -Wall -Wextra -I ../src ../src/predictor/predictor_core.cpp test_predictor_core.cpp -o test_predictor_core
(clean compile, zero warnings)
$ ./test_predictor_core
... (31 checks, see below)
31/31 checks passed
EXIT_CODE=0
```

All 12 required predictor scenarios pass, hand-verified against the actual
formulas (see the comments in `test/test_predictor_core.cpp` for the
worked arithmetic — e.g. the recovery scenario's exact PDR EWMA sequence,
the least-squares slope for the "combined degradation" case):

| # | Required scenario | Test |
|---|---|---|
| 1 | Stable RSSI → approximately-zero slope → healthy | `test_stable_rssi_is_healthy` |
| 2 | Improving RSSI → positive slope | `test_improving_rssi_positive_slope` |
| 3 | Degrading RSSI → EWMA follows trend, negative slope | `test_degrading_rssi_negative_slope` |
| 4 | Noisy RSSI → EWMA smoother than raw | `test_noisy_rssi_ewma_smoother_than_raw` |
| 5 | PDR degradation → PDR decreases | `test_pdr_degrades_on_failures` |
| 6 | Stable good PDR → healthy evidence | `test_stable_good_pdr_is_healthy` |
| 7 | Sudden silence → staleness fast-path activates | `test_staleness_fast_path` |
| 8 | Combined RSSI+PDR degradation → lower link_score | `test_combined_degradation_lowers_score` |
| 9 | Hysteresis: crosses T_LOW → unhealthy | `test_hysteresis_crosses_t_low_to_unhealthy` |
| 10 | Score between thresholds → no flapping | `test_midband_score_does_not_flap` |
| 11 | Recovery: crosses T_HIGH → healthy | `test_recovery_crosses_t_high_to_healthy` |
| 12 | Single noisy bad sample → no immediate reroute (debounce) | `test_single_bad_sample_does_not_immediately_reroute` |

Scenarios 13/14 (routing integration: priority ignores link_score;
unhealthy B promotes C) live in `test/test_routing_core.cpp` instead,
since that's the module the new health-aware `selectNextHop()` logic
actually lives in:

```
$ g++ -std=c++17 -Wall -Wextra -I ../src ../src/routing/routing_core.cpp test_routing_core.cpp -o test_routing_core
(clean compile, zero warnings)
$ ./test_routing_core
... (21 checks: all 18 Phase 1 checks, unchanged and still passing, plus 2 new)
ok:   PRIORITY routing still forces the direct A-S edge even when it's marked unhealthy
ok:   sanity: with both healthy, NORMAL still prefers B (2 hops) over C (3 hops)
ok:   NORMAL routing: unhealthy B allows the surviving C candidate (3 hops) to become preferred
21/21 checks passed
EXIT_CODE=0
```

| # | Required scenario | Test |
|---|---|---|
| 13 | Priority routing ignores poor link_score | `test_priority_ignores_unhealthy_link` |
| 14 | Normal routing: unhealthy B promotes C | `test_normal_avoids_unhealthy_b` |

**What this is not:** not a network simulator — no ESP-NOW, no real RSSI
hardware, no simulated send outcomes. Every input is a hand-constructed
number fed directly to a pure function; every expected output is worked by
hand against the actual EWMA/least-squares/hysteresis formulas, not
guessed. `predictor.cpp` (the Arduino-facing adapter) is untested by this
harness, same caveat as `routing.cpp` in Phase 1 — reviewed by hand, not
independently verified; see the ESP32 compile below for whether it at
least *compiles* correctly.

### 2. Real ESP32 compilation (whole sketch, Phase 0 + 1 + 2)

```
$ arduino-cli compile --fqbn esp32:esp32:esp32 firmware/PredictiveMesh --warnings all
Sketch uses 890160 bytes (67%) of program storage space. Maximum is 1310720 bytes.
Global variables use 46064 bytes (14%) of dynamic memory, leaving 281616 bytes for local variables. Maximum is 327680 bytes.
```

Clean on the first attempt — 0 errors, 0 warnings (`--warnings all`
explicit). No API-drift issues this time (Phase 2 doesn't call any new
ESP-NOW APIs — `predictor_core`/`predictor.cpp` only consume the RSSI
value and NodeId the transport/routing layers already extracted). Notably,
`vsnprintf`'s `%.2f`/`%.3f` float format specifiers used in the new
`[PREDICTOR]` log lines compiled and are expected to work correctly —
Arduino-ESP32 core links a full newlib with float `printf` support by
default (unlike AVR Arduino, which strips it), so this isn't the classic
embedded "float printf silently does nothing" trap; still, actual Serial
output has not been observed on real hardware (see below).

**Explicit distinction, per the task's own requirement:**

| Layer | Status |
|---|---|
| HOST TESTS (predictor math + routing integration) | **verified** — 31/31 + 21/21, see above |
| ESP32 COMPILATION (whole sketch) | **verified** — clean, 0 warnings, 0 errors, `esp32:esp32` core 3.3.11 |
| PHYSICAL HARDWARE | **not yet verified** — no boards exist; see "Hardware-dependent tests" below |

## Real ESP32 toolchain compile — actually run (2026-08-17, post-Phase-1)

The full Arduino sketch (Phase 0 + Phase 1, everything under
`firmware/PredictiveMesh/`) was compiled for real against the actually
installed toolchain:

```
$ arduino-cli version
arduino-cli  Version: 1.5.2-rc.1

$ arduino-cli core list
esp32:esp32   3.3.11  esp32

$ arduino-cli compile --fqbn esp32:esp32:esp32 firmware/PredictiveMesh
```

**First attempt failed** with a real compiler error (not a prediction):

```
espnow_transport.cpp:84:32: error: invalid conversion from
'void (*)(const uint8_t*, esp_now_send_status_t)' to 'esp_now_send_cb_t'
{aka 'void (*)(const wifi_tx_info_t*, esp_now_send_status_t)'} [-fpermissive]
```

This confirms the exact risk this file previously flagged as unverified
("`esp_now_send_cb_t`'s exact signature ... hasn't changed across core
2.x->3.x as far as documented ... hasn't been compiler-verified") — it
turned out to have changed by core 3.3.11. Checked directly against the
installed core's own headers (not guessed) and fixed; see
[decisions.md](decisions.md#esp_now_send_cb_t-signature-adapted-for-arduino-esp32-core-3311)
for the full before/after and header citations. `esp_now_recv_cb_t` and
`esp_now_peer_info_t` (including `ifidx`) were checked against the same
headers and found unchanged from what the code already assumed.

**Second attempt, after the one-function fix, succeeded clean:**

```
$ arduino-cli compile --fqbn esp32:esp32:esp32 firmware/PredictiveMesh --warnings all
Sketch uses 888168 bytes (67%) of program storage space. Maximum is 1310720 bytes.
Global variables use 45696 bytes (13%) of dynamic memory, leaving 281984 bytes for local variables. Maximum is 327680 bytes.
```

Zero warnings, even with `--warnings all` explicitly passed. Build
artifacts (`.bin`/`.elf`/`.map`) land in
`firmware/PredictiveMesh/build/esp32.esp32.esp32/` — gitignored, not
committed.

**What this validates:** the entire Phase 0 + Phase 1 firmware — including
`routing.cpp`/`main.cpp`, the Arduino-facing halves the host g++ harness
below cannot exercise — actually compiles against the real, installed
ESP32 Arduino toolchain. **What this does not validate:** anything that
only shows up at runtime on real silicon (RSSI values, timing, actual
packet exchange) — see "Hardware-dependent tests" below, still all `NOT
RUN — HARDWARE NOT AVAILABLE`.

| Layer | Status |
|---|---|
| Host g++ unit tests (`routing_core` math) | **Verified** — 18/18, see below |
| ESP32 `arduino-cli` compilation (whole sketch) | **Verified** — clean, 0 warnings, 0 errors |
| Physical hardware | **Not yet verified** — no boards exist |

## Phase 1 — routing logic, actually compiled and run (host g++)

Unlike Phase 0, Phase 1 has real algorithmic logic (`src/routing/routing_core.h/.cpp`)
that doesn't touch Arduino/ESP-NOW APIs at all — see
[decisions.md](decisions.md#routing_core-split-out-as-an-arduino-free-pure-module).
That means it can be compiled and actually executed on this development
machine with the host C++ compiler, with no ESP32 toolchain involved. This
was done for real, not predicted:

```
$ g++ -std=c++17 -Wall -Wextra -I ../src ../src/routing/routing_core.cpp test_routing_core.cpp -o test_routing_core.exe
(clean compile, zero warnings)

$ ./test_routing_core.exe
ok:   S advertises at least one entry
ok:   S's first advertised entry is itself at distance 0
ok:   B's table changes after hearing S's advertisement
ok:   B's route to S is direct, 1 hop
ok:   A's route to S via B is 2 hops (A->B->S)
ok:   A still prefers B (2 hops) as the primary route to S once C's route also exists
ok:   With no route via B, A falls back to C (3 hops, A->C->D->S)
ok:   route to S is valid before timeout
ok:   expireStale reports at least one invalidated entry past the timeout
ok:   route to S is gone after the timeout elapses with no refresh
ok:   NORMAL routing picks B (2 hops), not the shorter direct A-S edge
ok:   PRIORITY routing forces the direct A-S edge (1 hop), overriding NORMAL's choice
ok:   A's route 'to A' is always NODE_ID_UNKNOWN - destination==self is rejected structurally
ok:   a neighbor's false self-distance claim is rejected outright
ok:   no candidate is created from the rejected entry
ok:   an advertisement describing this node's own distance to itself is rejected
ok:   B's candidate (via B, 2 hops) survives after C also advertises S
ok:   C's candidate (via C, 3 hops) was stored independently, keyed by neighbor C

18/18 checks passed
EXIT_CODE=0
```

Covers all 10 scenarios the Phase 1 task spec required, hand-mapped:

| # | Required scenario | Test(s) |
|---|---|---|
| 1 | S knows itself at distance 0 | `test_self_distance_zero` |
| 2 | B can learn S at distance 1 | `test_b_learns_s_at_one_hop` |
| 3 | A can learn S through B at distance 2 | `test_a_learns_s_via_b_at_two_hops` |
| 4 | A can learn S through C/D as an alternate route | `test_a_learns_alternate_via_c` |
| 5 | Invalid/stale routes removed or marked invalid | `test_stale_routes_invalidated` |
| 6 | Normal traffic selects the intended baseline route | `test_normal_selects_b_not_direct_s` |
| 7 | Priority traffic selects the shortest-hop route | `test_priority_selects_direct_s` |
| 8 | A cannot select itself as its own next hop | `test_cannot_select_self` |
| 9 | A route update cannot incorrectly reduce a destination's distance below valid bounds | `test_invalid_advertisement_rejected` |
| 10 | A route advertisement is associated with the neighbor that advertised it | `test_route_associated_with_neighbor` |

**What this is not:** not a network simulator (no ESP-NOW, no multi-node
process, no simulated timing/jitter/loss) and not a substitute for
`arduino-cli compile` (routing.cpp, the Arduino-facing adapter half, is
untested by this harness — it's straightforward glue code:
parse-payload/call-routing_core/call-transport::send, reviewed by hand,
not independently verified). See
[known-issues.md](known-issues.md#phase-1-routing--not-yet-run-on-hardware)
for exactly what real-hardware validation is still outstanding.

**To reproduce:** from `firmware/PredictiveMesh/test/`:
```sh
g++ -std=c++17 -I ../src ../src/routing/routing_core.cpp test_routing_core.cpp -o test_routing_core
./test_routing_core
```
This uses the host system's own C++ compiler (found at `C:\mingw64\bin\g++.exe`
in this environment) — not `arduino-cli`, not an ESP32 board package, no
install performed to run it.

## Phase 0 — static inspection and packet-format review

## What was validated

**Static inspection** — performed:
- Every `.h`/`.cpp` file's `#include` graph was checked by hand for
  consistency (no missing companion header, no circular includes, relative
  paths resolve to real files).
- `MeshPacket`'s field layout and `PACKET_HEADER_SIZE`/`PACKET_MAX_PAYLOAD`
  arithmetic were checked by hand against the documented offsets in
  [`protocol.md`](protocol.md).
- `core/node_id.h`'s topology/adjacency table (`neighborsOf()`) was checked
  against implementation-guide.html §01's diagram edge-by-edge (A-B, A-C,
  A-S, B-S, C-D, D-S).
- Every module's public header was checked against
  implementation-guide.html's Phase 0 "DO NOT IMPLEMENT YET" list to
  confirm no stub secretly contains real algorithm logic.
- ESP-NOW API usage (`esp_now_recv_info_t*`, `info->rx_ctrl->rssi`,
  `esp_now_send_cb_t`, `esp_now_peer_info_t`) was written against the
  documented Arduino-ESP32 core 3.x / ESP-IDF >= 5.1 API surface referenced
  in implementation-guide.html §04's toolchain-constraint callout.

**Real toolchain compile** — **now performed**, see "Real ESP32 toolchain
compile — actually run" at the top of this file for the full result
(one real compile error found and fixed, then a clean second build: 0
warnings, 0 errors, against `esp32:esp32` core 3.3.11). This superseded
everything below, which was written while the toolchain was still
unavailable and is kept only as a historical record of what was
*predicted* to be the risk before the real compile ran.

### Predictions made before the real compile (for the record)

The predictions below turned out to be **one hit, two clean**:
- `esp_now_send_cb_t`'s exact signature — **this was the real failure.**
  Predicted as "hasn't changed across core 2.x->3.x as far as documented,
  only the receive callback did" — wrong for core 3.3.11 specifically; see
  [decisions.md](decisions.md#esp_now_send_cb_t-signature-adapted-for-arduino-esp32-core-3311).
- `esp_now_peer_info_t.ifidx` — compiled clean, no changes needed.
- `#pragma pack` / `offsetof` interaction — compiled clean, no changes
  needed.

## Hardware-dependent tests

All marked **NOT RUN — HARDWARE NOT AVAILABLE**, per
[known-issues.md](known-issues.md):

- [ ] Flash to a real ESP32 board, confirm boot over Serial
- [ ] Two boards exchange a real ESP-NOW frame (broadcast peer path)
- [ ] `info->rx_ctrl->rssi` returns a real, sane value on receipt
- [ ] All five boards agree on `MESH_WIFI_CHANNEL` in practice
- [ ] `analogRead()` on GPIO34/35 behaves correctly with ESP-NOW active
- [ ] OLED (SSD1306) answers at `0x3C` on GPIO21/22 (Nodes S, C)
- [ ] Buzzer drives correctly on GPIO25

No fake or predicted results are recorded for any of the above. Do not
mark any of these as passed until they've actually run on real hardware.

## What's still deliberately untested (later-phase stubs)

Anything belonging to a stub module (`anomaly::evaluate()`,
`reliability::onSendResult()`, `telemetry::init()`) has no meaningful test
beyond "does it compile and return its documented safe default" — there's
no algorithm behind it yet to verify.
`routing::selectNextHop()`/`getNextHop()` are no longer in this category as
of Phase 1, and `predictor::linkScore()`/`isUnhealthy()` are no longer in
this category as of Phase 2 — see the routing_core and predictor_core test
suites above. The one real gap within predictor is the PDR evidence
stream's live wiring (not the math) — see
[decisions.md](decisions.md#pdr-measurement-boundary-not-wired-to-live-send-outcomes-in-phase-2).
