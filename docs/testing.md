# Testing

No physical hardware exists yet, so nothing in this document claims a
hardware-dependent pass. What follows is exactly what was and wasn't
validated, and how.

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

**Real toolchain compile** — **still not performed, including for Phase 1's
new files.** As of Phase 1 (2026-08-17), no `esp32:esp32` Arduino core is
installed anywhere accessible in this environment — the only `arduino-cli`
binary present is a leftover, never-completed install attempt in a
temporary scratchpad directory, and `core list` against it reports "No
platforms installed." (An ESP32 core install may be running separately on
your end per your instruction — this file will be updated with the real
result once you report `arduino-cli core list` showing `esp32:esp32` at
3.x.) **Neither the Phase 0 firmware nor the new Phase 1 routing files
(`routing.cpp`, `routing_core.cpp`) have been run through
`arduino-cli compile` or the Arduino IDE.** Treat the static inspection
above, plus the host-run `routing_core` unit tests, as "internally
consistent by hand-review and pure-logic verification" — not as "confirmed
to build for ESP32."

### To actually verify compilation

Once you have `arduino-cli` (or the Arduino IDE) with the ESP32 board
package (`esp32:esp32`, core 3.x / ESP-IDF >= 5.1) installed:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 firmware/PredictiveMesh
```

or in Arduino IDE: open `firmware/PredictiveMesh/PredictiveMesh.ino`,
select an ESP32 Dev Module board under the `esp32` core, and hit Verify.

If it doesn't compile clean, the most likely failure points given what's
new/unverified here are:
- `esp_now_send_cb_t`'s exact signature on whatever specific core 3.x
  patch version you have installed (it's written as
  `void(*)(const uint8_t*, esp_now_send_status_t)` here — this one hasn't
  changed across core 2.x->3.x as far as documented, only the receive
  callback did, but hasn't been compiler-verified).
- `esp_now_peer_info_t.ifidx` — set explicitly to `WIFI_IF_STA` in
  `transport::addPeer()`; confirm this field/enum name still matches your
  installed core version.
- `#pragma pack` / `offsetof` interaction — should be fine on GCC/Xtensa,
  but unverified without an actual build.

Please report back what actually happens (clean build, or the exact
error) so `docs/known-issues.md` and this file can be updated with a real
result instead of a prediction.

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

Anything belonging to a stub module (`predictor::linkScore()`,
`anomaly::evaluate()`, `reliability::onSendResult()`, `telemetry::init()`)
has no meaningful test beyond "does it compile and return its documented
safe default" — there's no algorithm behind it yet to verify.
`routing::selectNextHop()`/`getNextHop()` are no longer in this category as
of Phase 1 — see the routing_core test suite above.
