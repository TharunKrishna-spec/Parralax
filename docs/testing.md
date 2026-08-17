# Testing

No physical hardware exists yet, so nothing in this document claims a
hardware-dependent pass. What follows is exactly what was and wasn't
validated, and how.

## Phase 5 — UCB1 adaptive routing (stretch, optional), actually compiled and run (host g++) + real ESP32 compile, BOTH configurations

### 1. Host tests (bandit statistics + UCB1 selection formula)

```
$ g++ -std=c++17 -Wall -Wextra -I ../src ../src/ucb1/ucb1_core.cpp test_ucb1_core.cpp -o test_ucb1_core
(clean compile, zero warnings)
$ ./test_ucb1_core
... (26 checks, see below)
26/26 checks passed
EXIT_CODE=0
```

16 test functions, hand-verified against the actual UCB1 formula (see the
comments in `test/test_ucb1_core.cpp` for the worked arithmetic — e.g.
`test_high_success_candidate_dominates`'s exact 1.7739 vs. 0.8739 scores):

| # | Part 10 scenario | Test |
|---|---|---|
| 2 | First observation handled correctly | `test_first_observation_recorded_correctly` |
| 3 | Zero-observation candidate receives exploration priority | `test_zero_observation_gets_priority` |
| 4 | High-success candidate eventually dominates | `test_high_success_candidate_dominates` |
| 5 | Poor candidate does not dominate indefinitely | `test_poor_candidate_does_not_dominate_indefinitely` |
| 6 | Historical success influences selection | `test_historical_success_influences_selection` |
| 7 | Current unhealthy link not selected merely for historical success | `test_unhealthy_link_not_selected_despite_history` |
| 9 | Invalid candidate never selected | `test_never_selects_a_candidate_not_provided` |
| 10 | Stale candidate never selected | (same test — a stale candidate is simply never in the offered list) |
| 11 | nextHop == prevHop is rejected | `test_exclude_next_hop_rejects_prev_hop` |
| 12 | Two-node loop scenario is prevented | `test_two_node_loop_prevented` |
| 13 | Retry attempts do not inflate UCB1 trials | `test_retries_do_not_inflate_trials` |
| 14 | Successful delivery produces the correct reward | `test_successful_delivery_correct_reward` |
| 15 | Failed delivery produces the correct reward | `test_failed_delivery_correct_reward` |
| 16 | Multiple destinations maintain independent learning state | `test_independent_state_per_destination` |
| 17 | Multiple next hops maintain independent statistics | `test_independent_state_per_next_hop` |
| 18 | Counter overflow is safely handled | `test_counter_overflow_saturates` |
| 19 | Fixed-size memory remains bounded | `test_fixed_size_memory_and_bounds_safety` |

**Tests 1 and 8 are not pure-core tests** — see `test_ucb1_core.cpp`'s own
top comment for why (`ucb1_core` has no concept of "priority" at all, and
is a structurally separate module from `routing_core` with zero
dependency either direction). They're verified differently:
- **Test 1 ("UCB1 disabled preserves existing routing"):** the unchanged,
  still-passing `test_routing_core.cpp` suite (28/28, including the 2 new
  Phase 5 tests for `enumerateCandidates()` itself) IS the evidence —
  `routing_core::selectNextHop()` has zero lines changed, and
  `routing.cpp`'s `#if ENABLE_UCB1` gating makes the disabled path
  identical to Phase 4's, by code inspection (see
  [decisions.md](decisions.md#compile-time-gating-enable_ucb10-must-be-provably-byte-identical-to-phase-4-not-just-probably-fine)).
- **Test 20 ("disabling UCB1 reproduces Phase 1/2 routing decisions"):**
  same evidence as test 1.
- **Test 8 ("priority traffic ignores UCB1"):** verified by code review —
  `routing.cpp`'s `applyUcb1Ranking()` is only ever called inside
  `if (!priority)`; the real ESP32 compile below confirms it links/runs.

Two new tests were also added to `test_routing_core.cpp` for the new,
always-compiled `routing_core::enumerateCandidates()` function itself
(not gated behind `ENABLE_UCB1` — see decisions.md):

| # | Scenario | Test |
|---|---|---|
| 15 | Lists every valid NORMAL candidate, excludes priority-only edges, annotates health | `test_enumerate_candidates_lists_valid_normal_candidates` |
| 16 | Respects `excludeNextHop` — the Phase 5 loop-prevention guard | `test_enumerate_candidates_excludes_given_next_hop` |

Re-ran the full existing suite alongside these to confirm nothing
regressed: `test_routing_core` 28/28 (18 Phase 1 + 2 Phase 2 + 2 new Phase
5 test functions, 28 total `check()` assertions), `test_predictor_core`
31/31, `test_anomaly_core` 50/50, `test_reliability_core` 88/88,
`test_ucb1_core` 26/26 — **223/223 total, all five host suites, actually
run.**

**What this is not:** no real ESP-NOW, no real delivery outcomes, no
simulated multi-node interaction — every input is a hand-constructed
candidate list/outcome sequence, every expected score worked by hand
against the actual UCB1 formula. `ucb1.cpp` (the Arduino-facing adapter)
and `routing.cpp`'s new UCB1-integration code are untested by this
harness — reviewed by hand, validated by the real ESP32 compile below
(both configurations).

### 2. Real ESP32 compilation — BOTH configurations (Part 11)

```
$ arduino-cli compile --fqbn esp32:esp32:esp32 firmware/PredictiveMesh --warnings all   # ENABLE_UCB1=0 (default)
Sketch uses 906948 bytes (69%) of program storage space. Maximum is 1310720 bytes.
Global variables use 47664 bytes (14%) of dynamic memory, leaving 280016 bytes for local variables. Maximum is 327680 bytes.
```

```
# config.h temporarily edited to ENABLE_UCB1=1
$ arduino-cli compile --fqbn esp32:esp32:esp32 firmware/PredictiveMesh --warnings all   # ENABLE_UCB1=1
Sketch uses 909272 bytes (69%) of program storage space. Maximum is 1310720 bytes.
Global variables use 48064 bytes (14%) of dynamic memory, leaving 279616 bytes for local variables. Maximum is 327680 bytes.
# config.h restored to ENABLE_UCB1=0 (required default) and re-verified — identical
# byte counts to the first compile above, confirming an exact restore.
```

**Both configurations compiled clean on the first attempt — 0 errors, 0
warnings, `esp32:esp32` core 3.3.11.** `ENABLE_UCB1=1` adds 2,324 bytes
flash / 400 bytes RAM over the disabled build (the `ucb1_core`/`ucb1`
adapter code and the `Ucb1State` bandit table actually linking in) — a
small, expected footprint. `enumerateCandidates()` and `ucb1_core` itself
are always compiled (91-byte flash difference between Phase 4's own
number and this phase's `ENABLE_UCB1=0` build), which is expected per the
"always compiled, never gated" design for pure `*_core` modules.

| Layer | Status |
|---|---|
| HOST TESTS (all 5 suites) | **verified** — 223/223, see above |
| ESP32 COMPILATION, `ENABLE_UCB1=0` (default) | **verified** — clean, 0 warnings, 0 errors |
| ESP32 COMPILATION, `ENABLE_UCB1=1` | **verified** — clean, 0 warnings, 0 errors |
| PHYSICAL HARDWARE | **not yet verified** — no boards exist; see "Hardware-dependent tests" below |

**No live traffic to learn from, same as Phase 4:** UCB1's reward signal
comes from `reliability::send()`'s outcomes, and nothing calls that
automatically yet (see
[decisions.md](decisions.md#reliabilitysend-has-no-live-automatic-caller-in-phase-4--no-application-data-source-was-invented)).
Even with `ENABLE_UCB1=1` compiled in, the bandit tables would stay empty
on a real boot until that gap is resolved.

## Phase 4 — hop-by-hop reliable delivery, actually compiled and run (host g++) + real ESP32 compile

### 1. Host tests (packet identity + duplicate filter + retry/timeout + statistics)

```
$ g++ -std=c++17 -Wall -Wextra -I ../src ../src/reliability/reliability_core.cpp test_reliability_core.cpp -o test_reliability_core
(clean compile, zero warnings)
$ ./test_reliability_core
... (88 checks, see below)
88/88 checks passed
EXIT_CODE=0
```

18 test functions, hand-verified against the actual algorithm (see the
comments in `test/test_reliability_core.cpp` for the worked timing
arithmetic, e.g. the exact tick sequence that exhausts
`RELIABILITY_MAX_RETRIES`):

| # | Scenario | Test |
|---|---|---|
| 1 | A fresh identity is never a duplicate | `test_new_identity_not_duplicate` |
| 2 | The same identity seen again within TTL is a duplicate | `test_repeat_identity_is_duplicate` |
| 3 | Identity is the (source, sequence) pair, not sequence alone | `test_identity_is_source_and_sequence_pair` |
| 4 | Duplicate cache expiry | `test_duplicate_cache_expiry` |
| 5 | Duplicate cache eviction when full | `test_duplicate_cache_eviction_when_full` |
| 6 | beginTx reserves distinct slots for concurrent hop-transmissions | `test_begin_tx_reserves_distinct_slots` |
| 7 | beginTx refuses once the pending pool is exhausted | `test_begin_tx_pool_exhaustion` |
| 8 | A matching ACK resolves the pending transmission | `test_matching_ack_resolves_pending_tx` |
| 9 | An ACK for an unknown identity is never fabricated as a match | `test_unmatched_ack_is_not_fabricated` |
| 10 | No timeout fires before the deadline | `test_tick_timeouts_silent_before_deadline` |
| 11 | A first timeout fires RETRY | `test_tick_timeouts_fires_retry` |
| 12 | Exhausting all retries produces exactly one FAILED | `test_tick_timeouts_exhausts_retries_then_fails` |
| 13 | Part 9's worked example: 1 packet + 2 retries + success | `test_part9_one_packet_two_retries_then_success` |
| 14 | recordImmediateFailure counts an untracked failure | `test_record_immediate_failure` |
| 15 | Concurrent pending entries resolve independently | `test_concurrent_pending_entries_independent` |
| 16 | Sequence numbers are monotonic per node | `test_next_sequence_monotonic` |
| 17 | cancelTx immediately fails a reserved slot | `test_cancel_tx_immediate_failure` |
| 18 | tickTimeouts respects the caller's maxOut cap | `test_tick_timeouts_respects_max_out` |

Re-ran the full existing suite alongside this one to confirm nothing
regressed: `test_routing_core` 21/21, `test_predictor_core` 31/31,
`test_anomaly_core` 50/50, `test_reliability_core` 88/88 — **190/190
total, all four host suites, actually run.**

**What this is not:** no real ESP-NOW, no real MAC addresses, no simulated
radio loss/reordering — every input is a hand-constructed identity/
timestamp, every expected output worked by hand against
`reliability_core`'s own documented state machine (see
[decisions.md](decisions.md) for the exact retry/timeout/statistics
semantics). `reliability.cpp` (the Arduino-facing adapter — real
`MeshPacket` construction, `transport::send()`, ACK wire parsing,
forwarding) is untested by this harness, same caveat as every prior
phase's adapter half — reviewed by hand, validated by the real ESP32
compile below.

### 2. Real ESP32 compilation (whole sketch, Phase 0 + 1 + 2 + 3 + 4)

```
$ arduino-cli compile --fqbn esp32:esp32:esp32 firmware/PredictiveMesh --warnings all
Sketch uses 906856 bytes (69%) of program storage space. Maximum is 1310720 bytes.
Global variables use 47664 bytes (14%) of dynamic memory, leaving 280016 bytes for local variables. Maximum is 327680 bytes.
```

Clean on the first attempt — 0 errors, 0 warnings. First real use of
`esp_now_send()` for genuine unicast traffic (`MSG_DATA`/`MSG_ACK`) in this
project — Phase 0/1's only real send was `routing`'s broadcast beacon.

| Layer | Status |
|---|---|
| HOST TESTS (reliability math, all 4 suites) | **verified** — 190/190, see above |
| ESP32 COMPILATION (whole sketch) | **verified** — clean, 0 warnings, 0 errors, `esp32:esp32` core 3.3.11 |
| PHYSICAL HARDWARE | **not yet verified** — no boards exist; see "Hardware-dependent tests" below |

**No live application traffic yet:** `reliability::send()` is real,
tested, and callable, but nothing in the current firmware calls it
automatically — see
[decisions.md](decisions.md#reliabilitysend-has-no-live-automatic-caller-in-phase-4--no-application-data-source-was-invented).
This means the forwarding/ACK/retry path, while fully implemented and
host-tested via `reliability_core`, has not been exercised end-to-end with
real `MSG_DATA` traffic even in principle (no traffic exists to generate
it) — a distinct gap from "not run on hardware," since no hardware exists
for *anything* in this project yet.

## Phase 3 — sensor-health state machine, actually compiled and run (host g++) + real ESP32 compile

### 1. Host tests (median/MAD calibration + modified Z-score + flatline + debounce/recovery + staleness)

```
$ g++ -std=c++17 -Wall -Wextra -I ../src ../src/anomaly/anomaly_core.cpp ../src/routing/routing_core.cpp test_anomaly_core.cpp -o test_anomaly_core
(clean compile, zero warnings)
$ ./test_anomaly_core
... (50 checks, see below)
50/50 checks passed
EXIT_CODE=0
```

(`routing_core.cpp` is linked in only for test 14's cross-module
regression check — see below. `anomaly_core` itself has no dependency on
`routing_core`.)

14 test functions, one per required scenario, hand-verified against the
actual formulas (see the comments in `test/test_anomaly_core.cpp` for the
worked arithmetic — e.g. the exact variance/median/MAD numbers for the
outlier-robustness case):

| # | Required scenario | Test |
|---|---|---|
| 1 | Warmup with insufficient history | `test_warmup_insufficient_history` |
| 2 | Stable normal signal | `test_stable_normal_signal` |
| 3 | Single statistical outlier (debounced, no state flip) | `test_single_outlier_debounced` |
| 4 | Repeated statistical anomalies (state transitions) | `test_repeated_anomalies_trigger_state` |
| 5 | Flatline within tolerance | `test_flatline_within_tolerance` |
| 6 | Flatline outside tolerance | `test_flatline_outside_tolerance_never_triggers` |
| 7 | Recovery after anomaly | `test_recovery_after_anomaly` |
| 8 | Recovery after flatline | `test_recovery_after_flatline` |
| 9 | Invalid sample | `test_invalid_sample` |
| 10 | Stale sensor | `test_stale_sensor` |
| 11 | No false anomaly from normal noise | `test_no_false_anomaly_from_noise` |
| 12 | MAD robustness against an isolated extreme value | `test_mad_robust_to_isolated_outlier` |
| 13 | Multiple sensor IDs maintain independent state | `test_independent_sensor_state` |
| 14 | Sensor anomaly does not automatically become a link anomaly | `test_sensor_anomaly_does_not_affect_routing` |

Two real bugs were found and fixed while writing these tests (not
predicted, actually hit by running them):
- An off-by-one in the flatline tests: the first post-calibration sample
  only establishes a baseline for the "unchanged since last sample" check
  (there's nothing to compare it against yet), so reaching
  `ANOMALY_STUCK_N` consecutive *unchanged deltas* requires
  `ANOMALY_STUCK_N + 1` total samples, not `ANOMALY_STUCK_N` — the same
  category of off-by-one already caught and fixed in Phase 2's predictor
  tests.
- `test_mad_robust_to_isolated_outlier`'s first attempt used an outlier
  (raw value 5, ~1995 LSB from the calibrated median) extreme enough that
  the *calibration step's own separate variance safety gate* rejected the
  whole calibration attempt before median/MAD were ever computed — see
  [decisions.md](decisions.md#calibrations-variance-safety-gate-deliberately-uses-ordinary-variance-not-mad)
  for why that's the gate working correctly, not a bug in it. Fixed by
  using a smaller-magnitude (150 LSB) outlier that passes the variance
  gate while still clearly demonstrating median/MAD's robustness.

Re-ran the full existing suite alongside this one to confirm nothing
regressed: `test_routing_core` 21/21, `test_predictor_core` 31/31,
`test_anomaly_core` 50/50 — **102/102 total, all three host suites,
actually run.**

**What this is not:** no real ADC, no simulated potentiometer/LDR — every
input is a hand-constructed observation, every expected output worked by
hand against the guide's own median/MAD/modified-Z/flatline formulas plus
this phase's own debounce/recovery/staleness requirements.
`anomaly.cpp` (the Arduino-facing adapter — real `analogRead()`, the
blocking boot-calibration loop) is untested by this harness, same caveat
as `routing.cpp`/`predictor.cpp` before it.

### 2. Real ESP32 compilation (whole sketch, Phase 0 + 1 + 2 + 3)

```
$ arduino-cli compile --fqbn esp32:esp32:esp32 firmware/PredictiveMesh --warnings all
Sketch uses 902848 bytes (68%) of program storage space. Maximum is 1310720 bytes.
Global variables use 47056 bytes (14%) of dynamic memory, leaving 280624 bytes for local variables. Maximum is 327680 bytes.
```

Clean on the first attempt — 0 errors, 0 warnings. First real use of
`analogRead()`/`analogReadResolution()`/`pinMode()` on the sensor pins in
this project (Phase 0 deliberately deferred all sensor-reading code); no
new API surprises this time.

| Layer | Status |
|---|---|
| HOST TESTS (anomaly math, all 3 suites) | **verified** — 102/102, see above |
| ESP32 COMPILATION (whole sketch) | **verified** — clean, 0 warnings, 0 errors, `esp32:esp32` core 3.3.11 |
| PHYSICAL HARDWARE | **not yet verified** — no boards exist; see "Hardware-dependent tests" below |

**GUI telemetry contract:** this phase's task spec asked for wire-format
compatibility with an existing GUI teammate's telemetry contract. No such
contract exists anywhere in this repository — see
[decisions.md](decisions.md#gui-telemetry-contract-referenced-but-not-found-in-this-repository--flagged-not-fabricated)
and [known-issues.md](known-issues.md) for the full explanation. This is
not a validation gap in the usual sense (nothing was skipped) — it's a
flagged, unresolved input this phase could not honestly claim to satisfy.

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
- [ ] Two boards exchange a real unicast `MSG_DATA`/`MSG_ACK` pair (Phase 4)
- [ ] A real hop-by-hop retry actually fires after a genuine dropped frame
- [ ] Real forwarding across 2+ hops (e.g. A -> C -> D -> S) delivers correctly
- [ ] PDR observed from real hardware send/ACK outcomes (not just the wired mechanism)
- [ ] `ENABLE_UCB1=1` flashed to real boards, real delivery outcomes
      recorded into the bandit tables, and a real preference shift observed
      (e.g. a historically-poor path stops being chosen)
- [ ] `UCB1_EXPLORATION_C` (currently the textbook `sqrt(2)`) tuned against
      real hardware traffic patterns if warranted

No fake or predicted results are recorded for any of the above. Do not
mark any of these as passed until they've actually run on real hardware.

## What's still deliberately untested (later-phase stubs)

Anything belonging to a stub module (`telemetry::init()`) has no
meaningful test beyond "does it compile and return its documented safe
default" — there's no algorithm behind it yet to verify.
`routing::selectNextHop()`/`getNextHop()` are no longer in this category as
of Phase 1, `predictor::linkScore()`/`isUnhealthy()` as of Phase 2,
`anomaly::evaluate()`/the sensor state machine as of Phase 3,
`reliability`'s packet identity/ACK/retry/duplicate-filter/forwarding as of
Phase 4, and `ucb1_core`'s bandit statistics/selection formula as of Phase
5 — see the five *_core test suites above. The one real gap left anywhere
in this stack is `reliability::send()`'s live *caller* (not its
mechanism) — see
[decisions.md](decisions.md#reliabilitysend-has-no-live-automatic-caller-in-phase-4--no-application-data-source-was-invented),
the same category of gap Phase 2 documented and accepted for PDR itself.
Phase 5 inherits this exact same gap one layer further up: even with
`ENABLE_UCB1=1`, the bandit tables have nothing to learn from until that
caller exists.
