# Decisions

Every meaningful engineering decision made while building the firmware,
recorded so implementation assumptions never exist only in chat. New
entries go at the bottom. Format:

```
## <short title>
- Decision:
- Reason:
- Alternatives considered:
- Why alternatives were rejected:
- Impact:
- Phase/date:
```

---

## Arduino sketch `src/` subfolder for module layout
- **Decision:** Organize firmware code as `firmware/PredictiveMesh/PredictiveMesh.ino`
  (thin entry point) + `firmware/PredictiveMesh/src/` containing all real
  modules (`core/`, `transport/`, `routing/`, `predictor/`, `anomaly/`,
  `reliability/`, `telemetry/`), matching the exact tree requested for
  Phase 0.
- **Reason:** Arduino's build system (arduino-builder / arduino-cli)
  compiles files in the sketch root but ignores arbitrary subfolders,
  *except* a folder literally named `src`, which is compiled recursively
  using library rules. This is documented, standard behavior — not a hack —
  so the requested modular layout is directly buildable in stock Arduino
  IDE with no extra tooling.
- **Alternatives considered:** (a) Flatten everything into the sketch root
  with prefixed filenames (e.g. `transport_espnow_transport.cpp`). (b)
  Switch to PlatformIO, whose `src/` convention is native and slightly more
  flexible.
- **Why alternatives were rejected:** (a) defeats the point of a modular
  architecture and directly contradicts the "do not scatter" instruction.
  (b) The hardware/dev-environment contract explicitly specifies Arduino
  IDE, not PlatformIO; switching toolchains wasn't requested and would add
  a second build system to document and keep in sync.
- **Impact:** All cross-module includes use explicit relative paths (e.g.
  `"../core/logger.h"`) rather than relying on `src/` being implicitly on
  the include path, so the code doesn't depend on understanding this
  Arduino-specific mechanic to read correctly.
- **Phase/date:** Phase 0, 2026-08-17.

## Compile-time `THIS_NODE_ID` over MAC-address auto-detection
- **Decision:** Node identity is selected by a single `#define THIS_NODE_ID`
  in `src/config.h`, edited before each board's compile/flash.
- **Reason:** implementation-guide.html §04 explicitly allows either "a
  compile-time flag or a MAC-address lookup table." A compile-time flag is
  simpler, deterministic, requires no bootstrapping step, and — critically
  — doesn't require already knowing each board's MAC address, which isn't
  possible yet since no hardware exists. `NODE_TABLE` in `core/node_id.h`
  still carries a `mac[6]` field per node, ready for a MAC-based peer table
  once real MACs are collected after flashing.
- **Alternatives considered:** MAC-address lookup table (build one image,
  flash all five boards, each identifies its own role by matching its own
  MAC against a hardcoded table).
- **Why alternatives were rejected:** Requires knowing every board's MAC
  address *before* writing the table that determines what that board does —
  circular for a project with no hardware yet. Also more error-prone during
  a hackathon build (a wrong MAC-to-role mapping is a silent misconfiguration;
  a wrong `THIS_NODE_ID` value is a one-line, obviously-visible diff).
- **Impact:** Exactly one line differs between the five boards' source
  trees. `docs/parameters.md` documents this as the per-board build step.
  Revisit if a future phase wants zero-touch reflashing across roles.
- **Phase/date:** Phase 0, 2026-08-17.

## Broadcast peer as the Phase 0 ESP-NOW bootstrap
- **Decision:** At boot, every node registers the ESP-NOW broadcast peer
  (`FF:FF:FF:FF:FF:FF`) via `transport::addBroadcastPeer()`, in addition to
  attempting unicast peer registration for its topology neighbors (which
  will no-op with a warning until real MACs are filled in).
- **Reason:** `esp_now_send()` requires the destination to be a registered
  peer, except for the broadcast address, which is always valid to send to
  without prior registration. Since no physical boards exist yet and MAC
  addresses can't be pre-populated, unicast peering can't be exercised at
  all right now. Broadcast lets two freshly flashed boards exchange raw
  ESP-NOW frames — a real exchange, real RSSI — the moment hardware
  arrives, with zero manual MAC-entry step, satisfying "once physical
  boards arrive, the same code can be compiled/flashed" without a
  pre-flight configuration step blocking the very first test.
- **Alternatives considered:** (a) Send nothing until unicast peer MACs are
  manually filled in. (b) Fake/hardcode placeholder MAC addresses now so
  `addPeer()` "succeeds" for every neighbor.
- **Why alternatives were rejected:** (a) blocks the first hardware
  bring-up test on a manual step. (b) is explicitly forbidden — it would
  create the illusion of working ESP-NOW packet delivery in code review
  without ever having sent a real byte to a real peer; that's exactly the
  "no fake packet delivery" instruction.
- **Impact:** Once real MACs are known, `NODE_TABLE` gets updated and
  unicast peers register normally alongside the still-present broadcast
  peer. No code changes required, only data (see
  `docs/known-issues.md`).
- **Phase/date:** Phase 0, 2026-08-17.

## `MeshPacket` field set matches the spec exactly, no extras
- **Decision:** `core/packet.h`'s `MeshPacket` struct contains exactly:
  version, type, source, destination, prev_hop, next_hop, priority,
  sequence, timestamp_ms, payload_len, payload[]. Two explicit 1- and
  2-byte `_reserved` padding fields exist purely for alignment, not as
  usable fields.
- **Reason:** implementation-guide.html's "PACKET / FRAME FOUNDATION"
  section lists exactly this field set and explicitly warns against adding
  fields "just because they may be useful." No TTL/hop-count field was
  added, even though a future loop-prevention mechanism will likely want
  one — that's routing's problem to introduce when routing is actually
  implemented, not Phase 0's to guess at.
- **Alternatives considered:** Add a `ttl`/`hop_count` field now since
  multi-hop forwarding will need loop prevention eventually.
- **Why alternatives were rejected:** Speculative — the actual
  loop-prevention strategy (TTL vs. visited-node bitmap vs. sequence-based)
  isn't decided yet, and guessing now risks a wire-format change later
  anyway. Deferred, tracked in `docs/known-issues.md`.
- **Impact:** `PACKET_HEADER_SIZE` is 17 bytes (`offsetof(MeshPacket,
  payload)`); `PACKET_MAX_PAYLOAD` is a single `#define` (currently 64) so
  raising it later needs no struct redesign.
- **Phase/date:** Phase 0, 2026-08-17.

## `priority` is a packet field, not a separate `MessageType`
- **Decision:** `MessageType` has three values (`MSG_HEARTBEAT`,
  `MSG_DATA`, `MSG_ACK`); "priority" is a separate `uint8_t priority` field
  on the packet, applicable to `MSG_DATA`.
- **Reason:** §5.3 of the implementation guide describes priority as "a
  priority flag ... set" checked independently of packet type ("priority
  flag set? -> force shortest-hop"). Folding it into the type enum
  (`MSG_PRIORITY_DATA`) would require every future switch over
  `MessageType` to also handle the priority/non-priority cross product,
  when the two concerns (what kind of message, and whether it overrides
  routing) are orthogonal.
- **Alternatives considered:** A single `MSG_PRIORITY` message type instead
  of a flag.
- **Why alternatives were rejected:** Doesn't match the guide's own
  framing of priority as a flag, and conflates "is this a data packet" with
  "should this bypass quality-optimal routing" — two independent decisions
  in the architecture.
- **Impact:** `routing::selectNextHop()` (once implemented) branches on
  `pkt.priority`, not `pkt.type`.
- **Phase/date:** Phase 0, 2026-08-17.

## `packed` struct with explicit alignment padding, never pointer-cast a raw receive buffer
- **Decision:** `MeshPacket` uses `#pragma pack(push, 1)` for a
  deterministic wire layout, but includes two `_reserved` padding fields
  so `sequence` (uint16_t) and `timestamp_ms` (uint32_t) still fall on
  naturally aligned offsets within that packed layout. Firmware code is
  expected to `memcpy()` a received byte buffer into a locally declared
  `MeshPacket` before reading fields from it, never `reinterpret_cast` the
  raw ESP-NOW receive buffer pointer directly.
- **Reason:** On classic ESP32 (Xtensa LX6, the BOM's target chip),
  dereferencing a misaligned multi-byte field can raise a `LoadStoreError`
  exception. A `#pragma pack(1)` struct with no padding would put
  `sequence` at an odd byte offset and `timestamp_ms` at a non-4-aligned
  offset if accessed in place over an arbitrary buffer. `memcpy()`-ing into
  a stack-declared struct sidesteps the whole issue (the compiler
  guarantees the destination's alignment), and the reserved bytes make the
  in-struct offsets clean even if some future code takes a shortcut.
- **Alternatives considered:** (a) Unpacked struct (let the compiler insert
  padding automatically). (b) Packed struct with no explicit reserved bytes.
- **Why alternatives were rejected:** (a) makes the wire size
  compiler/flag dependent, which is fragile for a hand-rolled protocol
  meant to be identical across five independently-compiled images (even
  though they're the same compiler here, relying on implicit padding rules
  is bad practice for anything crossing a wire). (b) works today (all
  reads Phase 0 does go through `memcpy()`), but leaves a trap for anyone
  who later takes the "just cast the buffer" shortcut under time pressure.
- **Impact:** Documented at the top of `core/packet.h`. `PACKET_HEADER_SIZE`
  is 17 bytes, not 14 (the padding costs 3 bytes of wire size in exchange
  for this guarantee).
- **Phase/date:** Phase 0, 2026-08-17.

## Serial logger lives in `core/`, not a new top-level module
- **Decision:** `logger.h`/`logger.cpp` were added under `src/core/`,
  which the Phase 0 file tree in the spec didn't originally list.
- **Reason:** Structured Serial logging was an explicit Phase 0 objective
  ("11. serial logging") but the given module tree only names
  `core/node_id.h`, `core/packet.h`, `core/message_types.h`. Logging is
  cross-cutting infrastructure used by every layer (transport, and
  eventually routing/predictor/anomaly/reliability/telemetry), the same
  category as node identity and packet definitions — not a layer in the
  §01 architecture stack itself. Adding a new top-level folder (e.g.
  `src/log/`) for one small module seemed like a larger deviation from the
  given tree than adding one file inside the existing shared-infrastructure
  bucket.
- **Alternatives considered:** New top-level `src/log/` folder.
  Header-only implementation (Meyer's-singleton pattern, like
  `core/node_id.h`) to avoid adding any file the tree didn't list.
- **Why alternatives were rejected:** A new top-level folder is a larger,
  less obviously-justified structural change than one file in an existing
  folder. Header-only was rejected because the logger needs simple mutable
  state (the minimum log level) and Serial formatting logic substantial
  enough that a `.cpp` is the more conventional, readable choice — unlike
  `node_id.h`, which is pure data plus trivial lookups.
- **Impact:** None outside `core/`; flagged here explicitly per "if
  something appears questionable or incomplete: document it, do not
  silently redesign it."
- **Phase/date:** Phase 0, 2026-08-17.

## `core/node_id.h` and `core/packet.h` are header-only
- **Decision:** `node_id.h` and `packet.h` contain no companion `.cpp` —
  all functions (`nodeInfo()`, `neighborsOf()`, `packetInit()`, etc.) are
  `inline` functions, using function-local `static const` arrays (a
  Meyer's-singleton pattern) for the node/adjacency tables instead of a
  file-scope global defined in a `.cpp`.
- **Reason:** The Phase 0 file tree explicitly lists these two files as
  header-only (no `.cpp` sibling), unlike `transport/`, `routing/`,
  `predictor/`, `anomaly/`, `reliability/`, `telemetry/`, which all show
  `.h` + `.cpp` pairs. Meeting that literally, rather than "close enough,"
  avoids introducing files not in the agreed contract.
- **Alternatives considered:** `core/node_id.cpp` defining `NODE_TABLE` as
  a file-scope `const` array (the more common embedded-C++ pattern).
- **Why alternatives were rejected:** Works, but deviates from the given
  tree without a strong enough reason — the header-only pattern is
  standard C++ (valid since C++98 via `inline` functions with
  function-local statics; doesn't require C++17 inline variables) and has
  no real downside here given how small these tables are.
- **Impact:** None functionally. Noted so a future contributor doesn't
  "fix" this into a `.cpp` file thinking it was an oversight.
- **Phase/date:** Phase 0, 2026-08-17.

## No OLED/sensor library dependency introduced yet
- **Decision:** `src/telemetry/` contains only a stub (`init()` that logs
  and returns); no OLED driver library (e.g. Adafruit_SSD1306/GFX or U8g2)
  is added to the project, and `setup()` never calls `pinMode()` /
  `analogRead()` on the sensor pins.
- **Reason:** Phase 0's objective list stops at "future modules have clean
  interfaces" and explicitly excludes "final OLED UI." Pulling in a display
  library now would (a) require that library to be installed before this
  Phase 0 firmware even compiles, for functionality that isn't being
  exercised yet, and (b) started implementing a layer explicitly deferred.
  Pin numbers and the I2C address are still fully documented in `config.h`
  and `docs/parameters.md` so wiring can proceed in parallel on the
  hardware side.
- **Alternatives considered:** Add the OLED library and a minimal
  "hello world" init call now, to prove the wiring contract end-to-end.
- **Why alternatives were rejected:** Not a Phase 0 objective, and
  hardware to test it against doesn't exist yet anyway — see
  `docs/known-issues.md`.
- **Impact:** `docs/known-issues.md` tracks "actual OLED validation" as
  pending hardware. Revisit when the reporting layer's phase starts.
- **Phase/date:** Phase 0, 2026-08-17.

## `routing_core` split out as an Arduino-free pure module
- **Decision:** The actual distance-vector algorithm (neighbor table,
  route candidate table, Bellman-Ford-style relaxation, expiry, next-hop
  selection) lives in `src/routing/routing_core.h`/`.cpp`, which has zero
  dependency on Arduino.h, millis(), Serial, or `transport::*`. Every
  function takes `now` as an explicit `uint32_t` parameter instead of
  calling `millis()` internally. `src/routing/routing.cpp` is a thin
  adapter: it owns the single `RoutingState` instance, calls
  `routing_core::*` for all real logic, and is the only place that touches
  `logger::*`, `millis()`, or `transport::send()`.
- **Reason:** This is the first module with real algorithmic logic worth
  verifying on its own, and "verified on its own" only means something if
  it can run outside the ESP32 toolchain — which isn't reliably available
  in this environment (see `docs/testing.md`). Splitting the math out lets
  `firmware/PredictiveMesh/test/test_routing_core.cpp` compile and run
  against the exact same code that ships on-device, using a plain host
  compiler (g++), with no simulated network, radio, or timing involved.
- **Alternatives considered:** (a) Keep everything in one Arduino-coupled
  `routing.cpp` and only static-review it, matching Phase 0's precedent.
  (b) Build a small PC-side mesh simulator to exercise the algorithm
  end-to-end.
- **Why alternatives were rejected:** (a) Phase 1 explicitly asks for
  "deterministic firmware-level tests" where practical, not just static
  review — this phase's logic (Bellman-Ford relaxation, tie-breaking,
  staleness) is exactly the kind of thing static review is bad at catching
  regressions in. (b) explicitly forbidden by the task spec ("do NOT build
  a general PC simulator") — a unit test harness that calls pure functions
  with hand-constructed inputs is not a simulator; it never fakes ESP-NOW,
  never fakes a multi-node network, and never fakes timing.
- **Impact:** Two new files (`routing_core.h/.cpp`) instead of one. All 10
  of Phase 1's required test scenarios are implemented and actually run —
  see `docs/testing.md` for the real output. Later phases (predictor
  feeding link-quality into selection) should extend `routing_core`, not
  bypass it by adding logic straight into `routing.cpp`.
- **Phase/date:** Phase 1, 2026-08-17.

## A↔S edge modeled as priority-only, excluded from NORMAL selection
- **Decision:** `routing_core::isPriorityOnlyEdge(a, b)` returns true only
  for the A↔S pair. `selectNextHop(..., priority=false)` skips any
  candidate reached via a priority-only edge; `priority=true` considers
  all candidates, including priority-only ones.
- **Reason:** implementation-guide.html §01's own topology diagram labels
  the direct A–S link `"(priority path only)"` in the SVG text — this is
  the source-of-truth document stating that edge is not meant to
  participate in normal-mode route selection at all. It has to be encoded
  somewhere: Phase 1 has no link-quality metric yet (EWMA/RSSI-slope/PDR
  are Phase 2), so a plain "always take the fewest hops" rule would make
  NORMAL traffic take the direct 1-hop A–S link too, which is exactly the
  outcome §5.3 and the Phase 1 acceptance criteria say should NOT happen
  (NORMAL must produce A→B→S, PRIORITY must produce A→S). Without some
  encoded signal distinguishing the two, "quality-optimal" and
  "shortest-hop" are mathematically identical in Phase 1 and the priority
  override would be invisible — which would also stay true forever, since
  a real link never gets *worse* than "sometimes drops packets", it
  doesn't stop being the shortest hop count.
- **Alternatives considered:** (a) Do nothing in Phase 1 — let NORMAL and
  PRIORITY both resolve to the same shortest-hop answer until Phase 2's
  link_score exists to actually distinguish them. (b) Give the A-S edge an
  artificially inflated "cost" in the distance-vector metric itself (e.g.
  treat it as 2 hops instead of 1) so plain shortest-hop math naturally
  avoids it.
- **Why alternatives were rejected:** (a) fails Phase 1's own explicit
  acceptance criteria (`A → B → S` normal / `A → S` priority must both be
  demonstrated in Phase 1, not deferred to Phase 2) and the "walk me
  through S→B→A" learning requirement, which only makes sense if the two
  modes already diverge. (b) fabricates a fake hop count for a real 1-hop
  link, which corrupts the distance-vector table's actual meaning (other
  nodes computing distances through A would inherit a wrong number) and
  contradicts "no fake data" — inflating cost is a Phase 2 link-quality
  concept smuggled in early under a different name.
- **Impact:** One small, isolated function
  (`routing_core::isPriorityOnlyEdge`), documented and easy to find/remove
  once Phase 2's real link_score makes it unnecessary (at that point NORMAL
  selection can weigh the A-S edge by quality like any other edge, instead
  of a hard exclusion). Flagged here explicitly as an interpretation of an
  otherwise-implicit part of the guide, per "if ambiguous, document and
  choose the smallest implementation consistent with the guide."
- **Phase/date:** Phase 1, 2026-08-17.

## HELLO and route advertisement share one wire message (`MSG_HEARTBEAT`)
- **Decision:** There is no separate `MSG_HELLO` or `MSG_ROUTE_UPDATE`
  message type. `MSG_HEARTBEAT` (already defined in Phase 0 as "periodic
  liveness / link-quality probe between direct neighbors") carries a
  distance-vector payload and serves as both: its mere arrival tells the
  receiver the sender is alive (neighbor discovery), and its payload tells
  the receiver the sender's current distances (route advertisement).
  Internally, `routing.cpp` still keeps these as two separate steps
  (`noteNeighborSeen()` always runs; `processRouteUpdate()` only runs for
  `MSG_HEARTBEAT`) dispatched from one `routing::onPacketReceived()` entry
  point, rather than two independently-called public functions.
- **Reason:** This is a standard, real distance-vector design (the same
  shape RIP uses) — periodic beacons that *are* the route advertisements —
  and it satisfies "do not create a second incompatible packet format" by
  construction, since nothing new is added to `MeshPacket` itself, only a
  payload layout inside the existing `payload[]` field.
- **Alternatives considered:** A separate lightweight HELLO with no
  payload, plus a second, less frequent `MessageType` purely for route
  advertisements.
- **Why alternatives were rejected:** Two message types double the wire
  traffic for no benefit in a 5-node topology this small, and nothing in
  implementation-guide.html asks for HELLO and route advertisement to be
  distinguishable on the wire — only that both concepts exist. Keeping
  them as one message is the smaller change to the packet format.
- **Impact:** `docs/protocol.md` documents the route-advertisement payload
  layout that now rides inside `MSG_HEARTBEAT.payload`. If a future phase
  needs HELLOs to be cheaper/more frequent than convergence-relevant route
  data, revisit and split them then.
- **Phase/date:** Phase 1, 2026-08-17.

## Route table stores one candidate per (destination, via-neighbor) pair
- **Decision:** `routing_core::RoutingState::candidates` is a
  `NODE_ID_COUNT × NODE_ID_COUNT` matrix — one `RouteCandidate` slot per
  (destination, neighbor-it-was-learned-from) pair — not a single
  best-route-per-destination table.
- **Reason:** The task spec explicitly asks that "if multiple next hops
  exist, retain enough information for future link-quality-based
  selection" and that a route advertisement stay "associated with the
  neighbor that advertised it." A single best-route table would discard
  A's route to S via C the moment the route via B is learned (both
  destination S), losing exactly the backup-path information the §01
  narrative depends on ("on B degradation, reroute to A→C→D→S"). With only
  5 nodes, the full matrix is 25 slots — trivial memory cost.
- **Alternatives considered:** (a) One `RouteEntry{next_hop, hop_count}`
  per destination, overwritten whenever a strictly better route arrives.
  (b) A dynamically-sized list of candidates per destination.
- **Why alternatives were rejected:** (a) can't represent "the backup
  route still exists but isn't currently selected" — exactly what test #4
  and the B-degradation narrative require. (b) needless for a fixed
  5-node universe; a dynamic container adds allocation/complexity Arduino
  embedded code should avoid without a real need.
- **Impact:** `selectNextHop()` scans the destination's row and picks the
  minimum hop_count candidate not excluded by policy (see the priority-only
  edge decision above). This is O(NODE_ID_COUNT) per lookup — irrelevant
  at this scale.
- **Phase/date:** Phase 1, 2026-08-17.

## Shared neighbor/route staleness timeout, 3x the beacon interval
- **Decision:** One constant, `ROUTING_ENTRY_TIMEOUT_MS` (3000 ms), governs
  both neighbor-liveness and route-candidate expiry.
  `ROUTING_HELLO_INTERVAL_MS` (1000 ms) governs how often each node
  beacons. Both live in `src/config.h`.
- **Reason:** Neighbor liveness and route freshness are refreshed by
  exactly the same beacon (see the combined-HELLO decision above), so
  there's no information difference that would justify two separate
  timeouts in Phase 1. 3x the beacon interval follows the same "tolerate
  a couple of missed beats before declaring something down" convention
  `docs/parameters.md` already documents for the (future) predictor
  heartbeat timeout ("3-5x predictor reaction").
- **Alternatives considered:** Separate `NEIGHBOR_TIMEOUT_MS` and
  `ROUTE_TIMEOUT_MS` constants.
- **Why alternatives were rejected:** No Phase 1 behavior currently
  depends on them differing; adding two knobs where one suffices is
  premature configuration surface. Revisit if a later phase has a real
  reason to decouple them (e.g. the predictor wanting faster neighbor
  liveness than route convergence needs).
- **Impact:** Documented in `docs/parameters.md`. `routing::tick()` passes
  the single constant to `routing_core::expireStale()`.
- **Phase/date:** Phase 1, 2026-08-17.

## No TTL/hop-count field added to `MeshPacket` in Phase 1
- **Decision:** `MeshPacket` is unchanged from Phase 0. The deferred
  question flagged in `docs/known-issues.md` ("loop prevention on
  multi-hop forwarding needs a decision before multi-hop forwarding is
  real") is resolved for Phase 1 as: **not needed yet**, not "decided and
  added."
- **Reason:** Two things would need a TTL/hop-count to avoid loops:
  flooding route advertisements past one hop, and relaying a live DATA
  packet hop-by-hop. Phase 1 does neither. Route advertisements are
  single-hop by construction (standard distance-vector: each node only
  ever sends its vector to its own direct neighbors, once per beacon —
  convergence happens through repeated 1-hop exchanges, not
  flooding/relaying a single message further). And actual hop-by-hop
  relaying of a received, not-self-destined `MSG_DATA` packet isn't
  implemented in Phase 1 either — `routing::selectNextHop()`/`getNextHop()`
  only *decide* a next hop; nothing in this phase acts on that decision by
  re-sending someone else's packet onward. That's tied to the reliability
  layer's hop-by-hop ACK mechanism (§5.4), which Phase 1's own "DO NOT"
  list explicitly excludes.
- **Alternatives considered:** Add a `ttl`/`hop_count` field now anyway,
  reasoning that routing is "close enough" to the layer that will need it.
- **Why alternatives were rejected:** Would repeat the exact mistake
  Phase 0 already declined to make (see "MeshPacket field set matches the
  spec exactly, no extras," above) — guessing a wire-format change before
  the layer that actually needs it (real hop-by-hop relay) is implemented
  and its real requirements are known.
- **Impact:** `docs/known-issues.md` updated to reflect this resolution
  rather than left as an open question — deferred specifically to whichever
  phase implements real hop-by-hop `MSG_DATA` relaying.
- **Phase/date:** Phase 1, 2026-08-17.

## `esp_now_send_cb_t` signature adapted for Arduino-ESP32 core 3.3.11
- **Decision:** `transport::onEspNowSent()` in `espnow_transport.cpp` now
  takes `const esp_now_send_info_t* tx_info` as its first parameter
  (reading the destination MAC from `tx_info->des_addr`), not the raw
  `const uint8_t* mac` it was originally written against.
- **Reason:** The first real `arduino-cli compile` against the actually
  installed `esp32:esp32` core (3.3.11) failed with `invalid conversion
  from 'void(*)(const uint8_t*, esp_now_send_status_t)' to
  'esp_now_send_cb_t' {aka 'void(*)(const wifi_tx_info_t*,
  esp_now_send_status_t)'}`. Checked directly against the installed core's
  own headers (`esp_wifi/include/esp_now.h`,
  `esp_wifi/include/esp_wifi_types_generic.h`), not guessed: as of this
  core version, `esp_now_send_cb_t` is
  `void(*)(const esp_now_send_info_t*, esp_now_send_status_t)`, where
  `esp_now_send_info_t` is a typedef of `wifi_tx_info_t`, which carries the
  destination MAC in its `des_addr` member. This is exactly the kind of
  API-drift risk `docs/testing.md` had flagged as unverified before a real
  compile ran — the `esp_now_recv_cb_t` signature and `esp_now_peer_info_t`
  (including `ifidx`) were also checked against the same headers and found
  unchanged from what the code already assumed.
- **Alternatives considered:** None — this is a mechanical adaptation to a
  fixed external API, not a design choice with real alternatives.
- **Why alternatives were rejected:** N/A.
- **Impact:** One function signature and one added line
  (`const uint8_t* mac = tx_info->des_addr;`) in
  `src/transport/espnow_transport.cpp`. `transport::TxEvent`/`TxCallback`
  (the module's own public interface, consumed by the rest of the
  firmware) are unchanged — the adaptation is fully contained inside the
  one function that talks directly to the ESP-NOW API. No architecture
  change. See `docs/testing.md` for the real compile result.
- **Phase/date:** Post-Phase-1 toolchain validation, 2026-08-17.

## `predictor_core` split out as an Arduino-free pure module (mirrors `routing_core`)
- **Decision:** The real EWMA/least-squares-slope/PDR/hysteresis math lives
  in `src/predictor/predictor_core.h`/`.cpp`, zero Arduino/ESP-NOW/Serial
  dependency, every function takes `now` explicitly. `src/predictor/predictor.cpp`
  is a thin adapter: owns the one `PredictorState`, feeds it real RSSI
  samples from the same receive dispatch point `routing::onPacketReceived`
  already uses, and is the only half that touches `logger::*`/`millis()`.
- **Reason:** Identical reasoning to
  [routing_core's split](decisions.md#routing_core-split-out-as-an-arduino-free-pure-module)
  in Phase 1: this is real algorithmic math worth verifying on its own, and
  that only means something if it runs on a host compiler. Unlike
  `routing_core.h` though, `predictor_core.h` deliberately `#include`s
  `../config.h` directly — every numeric constant this module needs (EWMA
  alphas, SLOPE_REF, fusion weights, hysteresis thresholds, debounce
  counts, staleness timeout) is a real deployment/tuning parameter the
  Phase 2 task spec explicitly requires centralized in `config.h` ("do not
  scatter magic constants through predictor.cpp"), unlike `routing_core`'s
  one internal constant (`MAX_HOP_COUNT`), which is an algorithm-intrinsic
  safety bound, not a tunable. `config.h` has no Arduino dependency itself,
  so this doesn't compromise host-testability.
- **Alternatives considered:** (a) Keep predictor math directly in
  Arduino-coupled `predictor.cpp`, static-review only. (b) Duplicate the
  tuning constants as local `static const` values inside `predictor_core.h`
  instead of including `config.h`.
- **Why alternatives were rejected:** (a) Part 11 of the Phase 2 spec
  explicitly requires deterministic, actually-run tests for "the
  mathematics/state machine," which static review can't verify. (b) would
  create two sources of truth for the same tunable values (or require
  `predictor.cpp` to pass them all through as function parameters on every
  call), directly contradicting "centralize... do not scatter."
- **Impact:** Two new files (`predictor_core.h/.cpp`), a new
  `test/test_predictor_core.cpp` (31/31 checks passing — see
  `docs/testing.md`), and `config.h` gaining a full `PREDICTOR_*` constant
  block.
- **Phase/date:** Phase 2, 2026-08-17.

## RSSI sample cadence reuses the existing Phase 1 beacon, not a new fast heartbeat
- **Decision:** Phase 2 does not add any new wire message or a faster,
  dedicated predictor heartbeat. RSSI samples arrive exactly when they
  already did in Phase 1 — once per `ROUTING_HELLO_INTERVAL_MS` (1000ms),
  via the existing `MSG_HEARTBEAT` beacon's arrival. `PREDICTOR_SLOPE_WINDOW`
  is set to 8 samples (not the guide's literal "15-20") specifically to
  keep the real-world slope-reaction window in the same single-digit-second
  range implementation-guide.html §5.1 intends, given the slower ~1Hz
  sample rate this phase actually has.
- **Reason:** Part 1 of the Phase 2 task spec is explicit: "Do not
  duplicate the radio reception mechanism. The predictor consumes
  observations produced by the existing transport/routing architecture."
  The guide's own timing table (100-200ms heartbeat, 15-20 sample window,
  ~2-4s reaction) is an aspirational target that assumes a heartbeat
  Phase 1 deliberately did not build at that cadence (see
  `ROUTING_HELLO_INTERVAL_MS`'s own Phase 1 decision, which explicitly
  anticipated this exact moment: "Deliberately decoupled from the
  predictor's future heartbeat cadence... reusing one constant would
  prematurely couple two layers' timing before the predictor layer
  exists"). Now that the predictor layer exists, the honest choice is
  between (a) adding a second, faster wire message just to hit that
  cadence, or (b) reusing what exists and scaling the sample-count-based
  parameters to match. A second wire message is real new protocol surface
  and channel traffic for a 5-node hackathon demo where "visibly moves the
  Serial monitor within a demo-able timeframe" (the guide's own Faraday-bag
  acceptance bar, §06) does not require sub-second resolution.
- **Alternatives considered:** (a) Add a dedicated, faster
  `MSG_PREDICTOR_PING`-style beacon at 100-200ms. (b) Literally keep
  `PREDICTOR_SLOPE_WINDOW` at 15-20 despite the slower cadence.
- **Why alternatives were rejected:** (a) is real scope growth (a new
  message type, more channel traffic, another timer) for a benefit the
  Phase 2 spec doesn't ask for and the demo bar doesn't need — and Part 1
  explicitly forbids duplicating the reception mechanism. (b) would make
  the slope window span ~15-20 seconds of real time, far slower than the
  guide's own stated intent ("Gives the slope estimator resolution — 1 Hz
  is far too slow to beat a timeout") and slower than useful for a live
  demo.
- **Impact:** `PREDICTOR_SLOPE_WINDOW = 8` in `config.h`, documented there
  and in `docs/parameters.md`. Revisit if a later phase adds real
  hardware-driven demo timing pressure that this window can't meet.
- **Phase/date:** Phase 2, 2026-08-17.

## `link_score` fusion constants: guide values used exactly where given, derived where not
- **Decision:** `PREDICTOR_RSSI_EWMA_ALPHA` (0.3) and
  `PREDICTOR_LINK_SCORE_W1`/`W2` (0.5/0.5) are taken directly from
  implementation-guide.html §5.1, which states them explicitly.
  `PREDICTOR_PDR_EWMA_ALPHA` (0.1) is *derived*, not guessed: the guide
  states a 20-frame PDR window, and the standard EWMA/simple-moving-average
  equivalence `alpha = 2/(N+1)` gives `2/21 ≈ 0.0952`, rounded to 0.1.
  `PREDICTOR_SLOPE_REF_DBM_PER_SAMPLE` (1.5) is a genuine placeholder — the
  guide names `SLOPE_REF` in its pseudocode but gives no numeric value —
  chosen so a sustained ~1.5 dBm-per-sample decline fully saturates
  `degrade_term` to 1.0.
- **Reason:** Where the guide gives an exact value, using anything else
  would be an unjustified deviation from the source of truth. Where it
  doesn't (PDR alpha, SLOPE_REF), the task spec's own instruction applies:
  "choose the smallest defensible formula and record the decision" — PDR
  alpha has a principled derivation from a value the guide *does* give
  (the 20-frame window), so it's not arbitrary; SLOPE_REF has no such
  anchor and is honestly documented as a starting figure needing real
  hardware to tune, exactly the kind of parameter the guide itself expects
  to be empirically adjusted (§06's "Faraday bag" test).
- **Alternatives considered:** Implement PDR as an explicit 20-slot ring
  buffer (literal windowed ratio) instead of an EWMA.
- **Why alternatives were rejected:** Part 4 of the Phase 2 spec explicitly
  permits "an EWMA-smoothed PDR or equivalent bounded communication-quality
  metric" — an EWMA needs one float of state per neighbor instead of 20
  bytes/booleans, a real memory saving on a 5-node embedded target, with no
  loss of the guide's intended behavior (a ~20-observation effective
  memory).
- **Impact:** All five constants centralized in `config.h`, documented in
  `docs/parameters.md`. `PREDICTOR_SLOPE_REF_DBM_PER_SAMPLE` is flagged
  there as the one value most likely to need retuning once real hardware
  attenuation data exists.
- **Phase/date:** Phase 2, 2026-08-17.

## Two-threshold hysteresis combined with the guide's own 3-consecutive-sample debounce
- **Decision:** `predictor_core`'s health state machine uses two thresholds
  (`PREDICTOR_HYSTERESIS_T_LOW` = 0.5, `PREDICTOR_HYSTERESIS_T_HIGH` = 0.7)
  *and* a consecutive-evaluation counter in each direction
  (`PREDICTOR_CONSECUTIVE_BAD_COUNT` = 3,
  `PREDICTOR_CONSECUTIVE_GOOD_COUNT` = 3) — not either mechanism alone.
  While HEALTHY, `belowCount` only increments on a `< T_LOW` evaluation and
  a transition to UNHEALTHY only fires once it reaches the bad-count
  threshold; the symmetric logic (`aboveCount`/`T_HIGH`) governs recovery.
  A score sitting between the two thresholds never advances either
  counter, in either state.
- **Reason:** implementation-guide.html §5.1's own pseudocode uses a single
  `THRESHOLD` plus a 3-consecutive-evaluation debounce ("reroute if below
  threshold for 3 consecutive evaluations"). The Phase 2 task spec
  explicitly overrides the single-threshold part ("Do NOT use a single
  threshold... Use two thresholds: T_LOW, T_HIGH") while Part 9 separately
  asks for the guide's own consecutive-sample gating to still be
  implemented. These aren't in conflict — T_LOW plays the role the guide
  calls `THRESHOLD` for the degrade direction; T_HIGH is the new
  recovery-direction threshold; the debounce count applies to crossings of
  whichever threshold is currently relevant. `PREDICTOR_CONSECUTIVE_GOOD_COUNT`
  reuses the same value as the bad-direction count since the guide gives no
  separate recovery-direction figure and nothing suggests recovering should
  be easier to trigger than degrading was to detect.
- **Alternatives considered:** (a) Two thresholds with no debounce (react
  instantly on any single crossing). (b) A three-state machine
  (HEALTHY/DEGRADING/UNHEALTHY) instead of two states plus a boolean
  `degrading` result flag.
- **Why alternatives were rejected:** (a) drops the guide's own explicit
  debounce requirement (Part 9), reintroducing exactly the single-noisy-
  sample sensitivity hysteresis is meant to prevent. (b) A third persistent
  state adds a whole extra set of transition rules for a distinction
  (soft-warning vs. hard-unhealthy) the Phase 2 spec's test scenarios don't
  actually require as *state* — `RecomputeResult.degrading` (Part 10's
  `LINK_DEGRADING` event) already carries that information as a
  transient signal without complicating the persistent state machine.
- **Impact:** `predictor_core.cpp`'s `recomputeLocked()`. Test scenarios 9,
  10, 11, 12 in `test/test_predictor_core.cpp` exercise exactly this
  combined logic and all pass — see `docs/testing.md`.
- **Phase/date:** Phase 2, 2026-08-17.

## Independent staleness fast-path deliberately bypasses the debounce
- **Decision:** `predictor_core::tickStaleness()` (and the equivalent check
  inside `onSendOutcome`) forces `UNHEALTHY` the instant
  `PREDICTOR_STALENESS_TIMEOUT_MS` (2000ms) elapses with no fresh RSSI
  sample — it does not go through `belowCount`/`PREDICTOR_CONSECUTIVE_BAD_COUNT`
  at all.
- **Reason:** Part 5 of the Phase 2 spec frames staleness as a *fast path*
  specifically because slope-based detection structurally cannot see a
  sudden silence ("no new RSSI samples -> no meaningful RSSI slope ->
  slope-only predictor can miss the failure"). Gating it behind the same
  3-consecutive-evaluation debounce used for noisy-but-still-arriving
  samples would defeat the entire purpose — there is no new evaluation
  happening during silence for a counter to accumulate against.
  `PREDICTOR_STALENESS_TIMEOUT_MS` is deliberately set to 2x the beacon
  interval (2000ms), faster than `ROUTING_ENTRY_TIMEOUT_MS`'s 3x (3000ms),
  so this fast path can flag a dying link before routing's own hard
  fallback expires the route entirely — matching the guide's stated intent
  that the proactive path should normally act first ("heartbeat timeout
  stays armed regardless, as a hard fallback").
- **Alternatives considered:** (a) One shared timeout for both routing
  staleness and predictor staleness (reusing `ROUTING_ENTRY_TIMEOUT_MS`).
  (b) Apply the same debounce counters to the staleness path too.
- **Why alternatives were rejected:** (a) would make the predictor's "fast
  path" exactly as slow as routing's hard fallback, defeating the
  "proactive... before a heartbeat timeout" framing from
  implementation-guide.html's own opening description of this feature. (b)
  is structurally impossible in a meaningful way — silence produces no new
  evaluations to debounce over. This is a deliberate, documented case of
  *not* collapsing two timers into one, the opposite of Phase 1's shared
  `ROUTING_ENTRY_TIMEOUT_MS` decision — justified here specifically because
  the two timeouts now need to race each other, not describe the same
  fact.
- **Impact:** `PREDICTOR_STALENESS_TIMEOUT_MS` in `config.h`, documented in
  `docs/parameters.md` alongside the HELLO-interval/route-timeout
  relationship it's designed against. Test scenario 7 in
  `test/test_predictor_core.cpp` exercises this directly.
- **Phase/date:** Phase 2, 2026-08-17.

## PDR measurement boundary: not wired to live send outcomes in Phase 2
- **Decision:** `predictor::onSendResult(NodeId, bool)` and
  `predictor_core::onSendOutcome()` are real, fully implemented, and
  independently tested (`test/test_predictor_core.cpp` scenarios 5, 6, 8),
  but nothing in the live firmware calls `predictor::onSendResult()` yet.
  `main.cpp`'s `onTransportTx()` remains the Phase 0 no-op stub.
- **Reason:** Two independent problems block wiring this for real in Phase
  2, both explicitly called out by the task spec rather than papered over.
  First: implementation-guide.html's PDR concept ("delivered/sent") and
  the task spec's own warning both require *unicast* send outcomes — ESP-NOW
  broadcast frames get no 802.11 MAC-layer ACK, so a broadcast send's
  "success" callback fires once the driver accepts the frame for
  transmission, not on confirmed delivery to any receiver ("Do not use
  broadcast transmission success callbacks as proof of MAC-layer
  delivery"). Every send this firmware currently performs is the
  `MSG_HEARTBEAT` beacon, sent to the broadcast MAC (see
  [decisions.md](decisions.md#hello-and-route-advertisement-share-one-wire-message-msg_heartbeat)) —
  there is no unicast traffic anywhere in Phase 0-2's runtime behavior to
  measure at all; real unicast `MSG_DATA` relaying is reliability-layer
  scope (§5.4), excluded from both Phase 1 and Phase 2. Second, even if
  there were unicast sends, `transport::TxEvent` identifies a destination
  by MAC address, not `NodeId`, and `core/node_id.h`'s `NODE_TABLE` MACs
  are still the Phase 0 all-zero placeholder sentinel (no hardware exists
  to populate them yet — see
  [known-issues.md](known-issues.md#peer-mac-addresses-are-placeholders)).
  Building a MAC->NodeId reverse lookup against placeholder zero MACs
  would make every neighbor indistinguishable and match ambiguously or
  falsely — exactly the kind of invented/fabricated observation the task
  spec forbids ("Do not invent packet success").
- **Alternatives considered:** (a) Wire `onTransportTx()` to
  `predictor::onSendResult()` anyway, treating broadcast beacon
  send-accepted status as a rough proxy for delivery. (b) Fabricate a
  MAC->NodeId table now so the wiring can be demonstrated end-to-end.
- **Why alternatives were rejected:** (a) is precisely the broadcast/
  MAC-layer-ACK confusion the task spec explicitly warns against — a
  broadcast "success" here would almost always be true regardless of
  whether any neighbor actually heard it, making the PDR signal
  meaningless-but-confidently-wrong, worse than having no signal at all.
  (b) is inventing data to make a demo path look wired when it isn't -
  forbidden by the project's standing "no fake packet delivery" rule (see
  [decisions.md](decisions.md#broadcast-peer-as-the-phase-0-espnow-bootstrap)'s
  same principle applied to Phase 0).
- **Impact:** `link_score` in Phase 2's live runtime is computed from real
  RSSI evidence + the staleness fast-path, with PDR evidence sitting at its
  documented neutral default (1.0, "no data yet," not a fabricated good
  reading) until a later phase's real unicast reliability traffic exists to
  drive it. The math and API are complete and tested now specifically so
  wiring it later is a one-line change (call `predictor::onSendResult()`
  from wherever real unicast delivery confirmation first exists), not a
  redesign. Documented here as the explicit Phase 2 measurement boundary,
  per the task spec's own instruction to do exactly that rather than invent
  one.
- **Phase/date:** Phase 2, 2026-08-17.

## Link health integrated into `routing_core::selectNextHop` alongside, not instead of, the priority-only-edge rule
- **Decision:** `routing_core::selectNextHop()` gained a new, optional,
  default-`nullptr` parameter, `const bool* neighborUnhealthy`. NORMAL
  selection (`priority == false`) now prefers the minimum-hop-count
  candidate among those that are both (a) not reached via a priority-only
  edge (Phase 1's rule, unchanged) and (b) not flagged unhealthy — falling
  back to the best candidate regardless of health if every eligible
  candidate is unhealthy (health gates *preference*, never *validity*).
  PRIORITY selection (`priority == true`) ignores `neighborUnhealthy`
  entirely and unconditionally, structurally guaranteeing link_score can
  never suppress the priority override. The `isPriorityOnlyEdge` exclusion
  itself is **kept, unchanged** — not removed or replaced.
- **Reason:** `CLAUDE.md`'s own standing instruction anticipated this
  moment: "Phase 1's priority-only-edge special case in `routing_core` is
  meant to be replaced by real link-quality-aware selection once
  `link_score` exists — don't leave both mechanisms active at once without
  a documented reason." This is that documented reason. Removing the
  exclusion outright was considered and rejected: without real hardware,
  `predictor_core` cannot yet organically produce a *worse* score for the
  direct A-S link than for A-B — both start at, and without live
  attenuation data stay at, the same neutral defaults. Ranking purely by
  link_score-then-hop-count with the exclusion removed would therefore
  make NORMAL traffic take the 1-hop A-S link exactly like PRIORITY
  traffic does, silently breaking the Phase 1 acceptance criterion
  (`test_normal_selects_b_not_direct_s`, still passing, unchanged) and the
  documented demo narrative — not because link-quality selection is wrong,
  but because there is no real evidence yet for it to act on. Fabricating
  a worse starting score for A-S to force the old behavior back would
  violate the "no fake data" rule the same way inventing PDR would. The
  two mechanisms therefore answer two different, non-overlapping
  questions: `isPriorityOnlyEdge` decides *eligibility* for NORMAL traffic
  (a static, topology-level fact from implementation-guide.html §01's own
  diagram), while `neighborUnhealthy` decides *preference* among eligible
  candidates (a dynamic, evidence-based fact). Test scenario 14
  (`test_normal_avoids_unhealthy_b`) exercises exactly this: B and C are
  both NORMAL-eligible (neither is the priority-only edge), and marking B
  unhealthy correctly promotes C — proving the new mechanism works without
  ever needing to touch the A-S exclusion.
- **Alternatives considered:** (a) Remove `isPriorityOnlyEdge` entirely,
  rank NORMAL selection purely by `[health tier, then hop count]`. (b)
  Keep both mechanisms with no interaction documented (add link_score
  filtering as a second, separate pass with no explanation of why the
  exclusion still exists).
- **Why alternatives were rejected:** (a) — see Reason above; breaks a
  passing Phase 1 test and the demo narrative on the basis of a score
  difference that doesn't exist yet without real hardware. (b) is exactly
  what `CLAUDE.md` says not to do — leaving two mechanisms coexisting with
  no stated reason for why neither fully subsumes the other.
- **Impact:** `routing_core.h`/`.cpp`'s `selectNextHop()` signature change
  is backward-compatible (default parameter) — all 18 pre-existing Phase 1
  tests pass unmodified. `routing.cpp`'s `getNextHop()` now builds the
  `neighborUnhealthy` array from `predictor::isUnhealthy()` before calling
  `routing_core::selectNextHop()`. The condition for revisiting the
  `isPriorityOnlyEdge` exclusion is now precise and testable: once real
  hardware demonstrates the A-S link's `link_score` genuinely and
  persistently scoring worse than A-B's under real attenuation, the
  exclusion becomes provably redundant and should be removed then — not
  before.
- **Phase/date:** Phase 2, 2026-08-17.

## No `MeshPacket`/wire format changes needed for Phase 2
- **Decision:** `core/packet.h` and `docs/protocol.md`'s wire layout are
  completely unchanged from Phase 1. No new `MessageType`, no new packet
  field, no link_score/health data added to any beacon payload.
- **Reason:** `link_score` as implemented in Phase 2 is a purely local
  quantity — each node's own evaluation of its own direct radio links,
  consumed only by that same node's own `routing::getNextHop()`. Part 7 of
  the Phase 2 task spec asks for routing to evaluate "the health of the
  link to each candidate next hop," which is exactly this local
  relationship (candidate's via-neighbor = a direct neighbor = a link this
  node itself observes) — it never requires a node to know a *remote*
  node's assessment of a *different* link. Nothing in Phase 2's stated
  scope asks for link-quality data to be advertised over the air.
- **Alternatives considered:** Extend the `MSG_HEARTBEAT` route-advertisement
  payload to also carry each entry's link_score, so neighbors could
  propagate quality information multiple hops.
- **Why alternatives were rejected:** Not required by Part 7's actual
  integration scope (local candidate evaluation only), and doing it anyway
  would be speculative protocol growth — exactly the "don't add fields
  because they may be useful" principle Phase 0 already established for
  `MeshPacket`. If a later phase wants multi-hop-aware link quality
  (propagating "the B-S link is degrading" to A), that's a real, separate
  design question deserving its own decision when that phase actually
  needs it.
- **Impact:** None to the wire format. `docs/protocol.md` gets a short note
  confirming this was considered, not overlooked.
- **Phase/date:** Phase 2, 2026-08-17.

## `anomaly_core` split out as an Arduino-free pure module (third instance of the pattern)
- **Decision:** The real median/MAD boot-calibration, modified-Z-score,
  flatline, debounce, recovery, and staleness math lives in
  `src/anomaly/anomaly_core.h`/`.cpp`, zero Arduino/ADC/Serial dependency.
  `src/anomaly/anomaly.cpp` is the thin adapter: owns two
  `anomaly_core::SensorCore` instances (POT, LDR), calls
  `analogRead()`/`delay()`, and is the only half that touches `logger::*`.
- **Reason:** Same reasoning as
  [routing_core](decisions.md#routing_core-split-out-as-an-arduino-free-pure-module)
  and
  [predictor_core](decisions.md#predictor_core-split-out-as-an-arduino-free-pure-module-mirrors-routing_core)
  before it — real algorithmic math (sort-based median, MAD, a boot-time
  variance safety check, a 6-state debounced/recovering state machine)
  worth verifying on its own. `anomaly_core.h` includes `../config.h`
  directly for the same reason `predictor_core.h` does: every numeric
  constant here (`ANOMALY_*`) is a real tuning parameter, not an
  algorithm-intrinsic bound.
- **Alternatives considered:** None beyond what was already rejected for
  the same reasoning in the routing_core/predictor_core entries above.
- **Why alternatives were rejected:** See those entries — the reasoning is
  identical the third time.
- **Impact:** Two new files (`anomaly_core.h/.cpp`), a new
  `test/test_anomaly_core.cpp` (50/50 checks passing, covering all 14
  scenarios required by this phase — see `docs/testing.md`), and
  `config.h` gaining a full `ANOMALY_*`/`SENSOR_SAMPLE_INTERVAL_MS`
  constant block.
- **Phase/date:** Phase 3, 2026-08-17.

## Sensor abstraction is generic (`SensorObservation`), not hardwired to the potentiometer/LDR
- **Decision:** `anomaly_core` never sees `PIN_SENSOR_POT`/`PIN_SENSOR_LDR`
  or any ADC-specific concept. It accepts a generic
  `SensorObservation{sensor_id, timestamp_ms, value, valid}` — `value` is
  a plain `float`, `sensor_id` an opaque `uint8_t` the core never branches
  on. `anomaly.cpp` (the adapter) is the only place that knows POT is
  `sensor_id=0` at pin `GPIO34` and LDR is `sensor_id=1` at pin `GPIO35`.
- **Reason:** This phase's task spec explicitly requires the anomaly
  engine be reusable across sensors, not hardcoded to one physical
  implementation, and requires the core to accept "sample value,
  timestamp/time, configuration" generically. A generic observation struct
  is also what makes `STALE` (time-since-last-observation) and `INVALID`
  (a caller-flagged bad read) meaningful as first-class states — without
  an explicit timestamp parameter (the Phase 3 first-draft design this
  replaced didn't have one), staleness can't be computed at all.
- **Alternatives considered:** Keep evaluating a raw `uint16_t rawAdcValue`
  directly (this phase's own first-draft design, discarded before
  finishing documentation - see git history/conversation for context),
  with the POT/LDR distinction baked into a `SensorId` enum passed straight
  into the core.
- **Why alternatives were rejected:** Ties the pure algorithm module to a
  specific enum/physical concept it shouldn't need to know about, and
  provides no way to express staleness or validity - both explicitly
  required states for this phase.
- **Impact:** `anomaly.h`'s `SensorId` enum (POT/LDR) still exists, but
  purely at the adapter layer, mapped to `sensor_id` integers when
  constructing observations.
- **Phase/date:** Phase 3, 2026-08-17.

## One discrete `SensorState`, not two independent booleans — with a documented FLATLINE-over-ANOMALY priority
- **Decision:** `anomaly_core::evaluate()` classifies each sensor into
  exactly one of `{WARMUP, NORMAL, ANOMALY, FLATLINE, STALE, INVALID}` at
  a time (Part 4's explicit state-machine requirement) — not two
  independent booleans, which is what this phase's discarded first draft
  used. When a sample is simultaneously far from the calibrated median
  (would trigger the MAD-Z spike detector) **and** unchanged for
  `ANOMALY_STUCK_N` consecutive samples (would trigger the flatline
  detector), **FLATLINE wins** — a sensor that has stopped producing new
  information at all is treated as the more fundamental failure than one
  that is merely reading an unusual-but-still-live value.
- **Reason:** implementation-guide.html §5.2's own diagram captions the two
  detectors "reported independently — never merged," while this phase's
  own task spec (Part 4) explicitly asks for a single discrete state
  machine with a specific NORMAL → {ANOMALY, FLATLINE} branch shown as
  mutually exclusive. These aren't actually in conflict once read
  carefully: the guide's "never merged" describes the two *detectors'
  math* never influencing each other's computation (true here — MAD-Z
  never looks at flatline state and vice versa; both raw signals
  (`modified_z`, flatline duration) are still independently exposed in
  `SensorTelemetry`, Part 7) — it does not mandate that the top-level
  reported *state* be a compound value. Given the task spec explicitly
  wants one discrete state, a priority rule is needed for the rare case
  both conditions hold at once, and FLATLINE is the more defensible
  choice: a stuck sensor is a hardware/wiring failure mode, arguably more
  urgent than "value looks unusual but the sensor is still live."
- **Alternatives considered:** (a) ANOMALY takes priority over FLATLINE.
  (b) Keep both as independent booleans instead of one discrete state, as
  this phase's discarded first draft did.
- **Why alternatives were rejected:** (a) is equally defensible in the
  abstract, but FLATLINE was chosen as the more severe condition — no
  strong reason favored (a) over this choice, so it's recorded as the
  documented, if somewhat arbitrary, tie-break, the same category of
  decision as routing_core's NodeId tie-break. (b) doesn't satisfy this
  phase's explicit Part 4 requirement for a discrete state machine with
  named states, and doesn't map cleanly onto Part 8's discrete event list
  (`SENSOR_ANOMALY`/`SENSOR_FLATLINE`/...).
- **Impact:** `anomaly_core::SensorState` is the reported classification;
  `SensorTelemetry.modified_z` and `.flatline_active`/`.flatline_duration_ms`
  remain independently populated regardless of which state won, so no
  evidence is actually lost — only the single top-level label picks a
  side.
- **Phase/date:** Phase 3, 2026-08-17.

## Debounce/recovery persistence counts for ANOMALY; flatline's own `ANOMALY_STUCK_N` already provides entry persistence
- **Decision:** The sensor STATE (not the raw per-sample `modified_z`
  evidence, which is always computed and reported instantly) only
  transitions NORMAL → ANOMALY after `ANOMALY_CONSECUTIVE_COUNT` (2)
  consecutive over-threshold samples, and only recovers ANOMALY → NORMAL
  after `ANOMALY_RECOVERY_COUNT` (2) consecutive under-threshold samples.
  FLATLINE's *entry* needs no separate debounce (`ANOMALY_STUCK_N`, 50, is
  already a sustained-evidence requirement by construction), but its
  *exit* does: `ANOMALY_FLATLINE_RECOVERY_COUNT` (2) consecutive non-flat
  samples are required before FLATLINE → NORMAL, so one changed sample
  alone can't instantly clear it (this phase's Part 6 explicitly requires
  this).
- **Reason:** implementation-guide.html §5.2's own MAD-Z pseudocode has no
  debounce at all — a single spike is meant to flag instantly, which is
  the detector's whole point. This phase's task spec (Part 5) explicitly
  requires that no sensor be classified as failed from one noisy sample,
  overriding the guide's instant-flag design specifically for the
  *discrete state transition* (not the raw evidence, which stays instant
  and is still exposed via `SensorTelemetry.modified_z` on every sample).
  `ANOMALY_CONSECUTIVE_COUNT` is kept smaller than the predictor's 3-sample
  debounce (Phase 2) to stay closer to the guide's "catch it fast" intent
  while still refusing to act on a single sample.
- **Alternatives considered:** (a) No debounce at all for ANOMALY,
  honoring the guide's literal instant-flag design. (b) A larger debounce
  count (e.g. matching the predictor's 3).
- **Why alternatives were rejected:** (a) directly contradicts this
  phase's explicit Part 5 instruction. (b) No strong reason favored a
  larger count; 2 is the smallest value that meaningfully distinguishes
  "one noisy sample" (test scenario 3,
  `test_single_outlier_debounced`) from "sustained anomaly" (test scenario
  4), matching "choose the smallest defensible persistence mechanism."
- **Impact:** `ANOMALY_CONSECUTIVE_COUNT`, `ANOMALY_RECOVERY_COUNT`,
  `ANOMALY_FLATLINE_RECOVERY_COUNT` in `config.h`. Test scenarios 3, 4, 7,
  8 in `test/test_anomaly_core.cpp` exercise exactly this logic.
- **Phase/date:** Phase 3, 2026-08-17.

## Boot calibration retry is bounded, not infinite
- **Decision:** `anomaly_core`'s boot-calibration retry (on failing the
  variance safety envelope) is bounded at `ANOMALY_CALIBRATION_MAX_RETRIES`
  (10) attempts, tracked internally as `SensorCore::warmupRetryCount` — not
  infinite, as implementation-guide.html's own boot-sequence diagram
  literally draws the "restart calibration" loop-back. Past that many
  attempts, `evaluate()` force-accepts the next full buffer regardless of
  variance (bypassing the safety gate, via the internal
  `tryFinalizeCalibration(core, forceAccept=true)` path), using real (if
  statistically unsafe) samples, loudly logged at ERROR by the adapter.
  `anomaly.cpp`'s `calibrateSensor()` needs no retry-counting logic of its
  own — it simply keeps feeding real samples through `evaluate()` while
  `core.state == WARMUP`, and the bounded-retry/force-accept decision is
  made entirely inside `anomaly_core`.
- **Reason:** Real hardware could plausibly have a genuinely noisy or
  unwired sensor pin (especially before hardware exists at all, or during
  early bring-up), and an unbounded retry would mean the whole node —
  including its already-initialized ESP-NOW/routing participation — never
  reaches its main loop. A firmware that fails loudly but keeps running is
  more useful for debugging and for the rest of the mesh (which still
  needs this node routing/relaying) than one that silently hangs forever
  waiting for a sensor reading that may never stabilize.
- **Alternatives considered:** (a) Implement the diagram literally with an
  unconditional retry loop. (b) Skip the variance check entirely and
  always accept the first calibration attempt.
- **Why alternatives were rejected:** (a) is a real boot-reliability risk
  for a device that needs to come up cleanly during a live demo. (b)
  discards a real, guide-specified safety check for no benefit.
- **Impact:** `ANOMALY_CALIBRATION_MAX_RETRIES` in `config.h`. Test
  scenario `test_force_accept_bypasses_variance_gate`-equivalent coverage:
  see `test_mad_robust_to_isolated_outlier` and the calibration-path tests
  in `test/test_anomaly_core.cpp` for the underlying `tryFinalizeCalibration`
  behavior; the adapter's bounded-loop-then-force-accept control flow
  itself is in `anomaly.cpp` (Arduino-coupled, reviewed by hand per the
  same convention as `routing.cpp`/`predictor.cpp`).
- **Phase/date:** Phase 3, 2026-08-17.

## Calibration's variance safety gate deliberately uses ordinary variance, not MAD
- **Decision:** The boot-calibration "variance within safety envelope?"
  check (implementation-guide.html's own boot-sequence diagram) computes
  plain statistical variance (mean/sum-of-squared-deviations) over the raw
  calibration buffer — the same statistic this phase's task spec (Part 2)
  otherwise explicitly says not to substitute for MAD in the anomaly
  detector itself.
- **Reason:** These are two different questions asked at two different
  points in the pipeline. The MAD-Z *detector* (steady-state, once
  calibrated) must be robust to a single outlier *within* its evidence
  window — that's the whole reason MAD/median exist, and ordinary
  mean/stddev would be exactly the wrong tool there. The calibration
  *safety gate* asks a different question: "was this 100-sample window, as
  a whole, a trustworthy resting baseline, or was something actively
  disturbing the sensor during calibration?" — and the guide's own
  diagram/Q&A literally names this check "variance," not MAD. Using
  ordinary variance here is a deliberate, correct match to a real
  difference in what's being measured, not an inconsistency. A concrete
  consequence, surfaced by writing `test_mad_robust_to_isolated_outlier`:
  a calibration buffer containing one *sufficiently extreme* isolated
  outlier will be rejected by this variance gate before median/MAD are
  even computed — which is the gate doing its job (catching a genuinely
  disturbed calibration window), not a bug. The test uses a smaller-
  magnitude (but still clearly demonstrative) outlier specifically chosen
  to pass the variance gate while still showing median/MAD's robustness
  compared to what a naive mean/stddev would have done.
- **Alternatives considered:** Use MAD (instead of variance) for the
  calibration safety check too, for consistency with the steady-state
  detector.
- **Why alternatives were rejected:** Would contradict the guide's own
  literal wording ("variance within safety envelope") for no real benefit
  — MAD's outlier-robustness is exactly the wrong property to want here;
  the calibration gate's job is to be *sensitive* to the buffer being
  disturbed, not robust to it.
- **Impact:** `ANOMALY_MAX_CALIBRATION_VARIANCE` in `config.h`, documented
  as governing ordinary variance specifically. `docs/testing.md` documents
  the outlier-magnitude choice in `test_mad_robust_to_isolated_outlier`.
- **Phase/date:** Phase 3, 2026-08-17.

## Sensor health and network/link health are separate failure domains — no coupling added
- **Decision:** `anomaly_core`/`anomaly.cpp` have zero references to
  `routing_core`/`routing.cpp`/`predictor_core`/`predictor.cpp`, and vice
  versa. A sensor entering `ANOMALY`/`FLATLINE`/`STALE` has no effect
  whatsoever on any routing decision.
- **Reason:** This phase's task spec (Part 9) explicitly requires this
  separation and explicitly forbids modifying routing behavior based on
  sensor health "unless explicitly required by the implementation guide"
  — and nothing in implementation-guide.html §5.2/§5.3 ties sensor anomaly
  detection to route selection; they're presented as entirely independent
  pieces of the architecture (§01's layer stack lists Anomaly and Routing
  as separate boxes with no edge between them). Conflating "this node's
  potentiometer looks weird" with "this node's mesh link is degrading"
  would be a real correctness bug, not just an architectural tidiness
  concern — a stuck potentiometer says nothing about RF conditions.
- **Alternatives considered:** Have a sensor `ANOMALY`/`FLATLINE`
  transition mark this node's outgoing link quality as suspect, on the
  theory that a node having *any* kind of problem is worth flagging to
  routing.
- **Why alternatives were rejected:** Explicitly forbidden by Part 9, and
  would be a real design mistake independent of that instruction — sensor
  and RF/link health are governed by completely different physical
  phenomena with no causal relationship.
- **Impact:** `test_sensor_anomaly_does_not_affect_routing` in
  `test/test_anomaly_core.cpp` is a real regression test proving this:
  driving a `SensorCore` into `ANOMALY` and re-querying
  `routing_core::selectNextHop()` on an unrelated `RoutingState` produces
  a byte-identical result before and after.
- **Phase/date:** Phase 3, 2026-08-17.

## GUI telemetry contract referenced but not found in this repository — flagged, not fabricated
- **Decision:** This phase's task spec asked for `SENSOR_STATUS`/`EVENT`
  payloads to "match the documented GUI telemetry contract exactly,"
  describing a GUI teammate who has "already implemented support for
  HELLO, HEARTBEAT, NODE_STATUS, LINK_UPDATE, ROUTE_UPDATE, PREDICTION,
  SENSOR_STATUS, EVENT, STATISTICS, ERROR." A repository search (for
  `gui-telemetry-contract.md`, any `*gui*`/`*telemetry*` file, and those
  message-type names anywhere in `docs/`, `PERSONAL_DOCS/`, or the
  firmware source) found **no such contract, file, or message-type list
  anywhere in this repository** — none of those names match this
  project's actual `MessageType` enum (`MSG_HEARTBEAT`, `MSG_DATA`,
  `MSG_ACK`, unchanged since Phase 0) or anything in
  implementation-guide.html, which frames the "Reporting Layer" as
  OLED + Serial/WebSerial, not a separate GUI application with its own
  wire protocol. Rather than invent a `docs/gui-telemetry-contract.md` or
  a message-type list to "match," this phase implements the underlying
  *data* Part 7 asks for (a complete `SensorTelemetry` snapshot: raw
  value, median, MAD, modified-Z, threshold, flatline state/duration,
  sensor state, validity) as a clean, local accessor
  (`anomaly::getTelemetry()`) and a real event stream
  (`anomaly::setEventCallback()`), ready to be serialized into whatever
  format a real contract turns out to specify, once one is actually
  provided.
- **Reason:** This project's standing rule, restated in nearly every prior
  phase, is "no fake data, no invented protocols, document rather than
  silently redesign." Fabricating a wire-format/message-type list with no
  basis in this repository would be exactly that kind of invention, dressed
  up as "matching an existing contract" — the opposite of what actually
  matching a contract would mean. This is flagged directly rather than
  silently worked around, per the task spec's own fallback instruction
  ("If the contract needs modification: document the change... and the
  reason") — there is no existing contract to modify; one needs to be
  supplied.
- **Alternatives considered:** (a) Invent a plausible-looking
  `docs/gui-telemetry-contract.md` describing the listed message types, so
  the firmware would have something concrete to "match." (b) Silently
  ignore Part 7/8's GUI-specific framing and just build local logging, without
  flagging the mismatch at all.
- **Why alternatives were rejected:** (a) would fabricate provenance for a
  document that doesn't exist and could actively mislead a real GUI
  integration effort later (code review/onboarding would reasonably assume
  a file named `gui-telemetry-contract.md` reflects a real, agreed
  contract). (b) leaves a real, potentially blocking gap
  undocumented — the opposite of this project's "document, don't silently
  drop" principle, applied consistently in every other phase's scope
  decisions (see the OLED-deferral and PDR-measurement-boundary entries).
- **Impact:** `docs/known-issues.md` tracks this explicitly as an open
  question for the user to resolve (share the real contract, or confirm
  none exists yet) before any firmware claims wire-format compatibility
  with a GUI. No fabricated file was created.
- **Phase/date:** Phase 3, 2026-08-17.

## Calibration uses a separate, faster sample interval than steady-state evaluation
- **Decision:** `ANOMALY_CALIBRATION_SAMPLE_INTERVAL_MS` (10ms) is a
  distinct, faster constant from `SENSOR_SAMPLE_INTERVAL_MS` (150ms, the
  steady-state main-loop cadence). At 150ms, buffering
  `ANOMALY_CALIBRATION_SAMPLE_COUNT` (100) samples per sensor would cost
  ~15 seconds of boot delay per sensor (~30s for both, worse under
  retries); at 10ms it costs roughly 1 second per sensor.
- **Reason:** Nothing in implementation-guide.html specifies the
  calibration sampling rate distinctly from the steady-state rate — this
  is the same "guide names the concept, not the number" situation as
  Phase 2's `PREDICTOR_SLOPE_WINDOW`/staleness-timeout choices. A ~30-second
  (or longer, under retries) boot hang on every single reboot is a real
  cost for a live hackathon demo, and the calibration window's *purpose*
  (capturing a real resting-noise baseline) doesn't obviously require
  100-200ms spacing the way the predictor's slope estimation does — even
  10ms apart, 100 real ADC reads still see real quantization/thermal noise
  variation, just compressed in time.
- **Alternatives considered:** Reuse `SENSOR_SAMPLE_INTERVAL_MS` for both
  calibration and steady-state sampling (one constant, simpler).
- **Why alternatives were rejected:** Would reintroduce the ~15-30 second
  boot delay for no documented benefit — the calibration window's
  real requirement is "spread out, not simultaneous" samples, which 10ms
  spacing already satisfies.
- **Impact:** Two `SENSOR_*`/`ANOMALY_*` timing constants in `config.h`
  instead of one — an intentional, documented case of *not* sharing a
  timer, the same category of decision as Phase 2's independent staleness
  fast-path (deliberately decoupled where the two things being timed have
  different real requirements).
- **Phase/date:** Phase 3, 2026-08-17.

## Anomaly detection scope: no OLED/telemetry wiring in Phase 3
- **Decision:** Phase 3 implements real sensor anomaly detection (boot
  calibration, MAD Z-score, flatline detector) and real Serial logging
  (`[ANOMALY]`/`[ANOMALY-EVENT]` lines, an `anomaly::setEventCallback()`
  hook), but does **not** add an OLED driver library or wire flags to
  Node C's display. `src/telemetry/` remains an untouched stub.
- **Reason:** implementation-guide.html §06's roadmap bundles "Wire both
  flags to the OLED on Node C" into the same Hours 12-17 bucket as the
  anomaly algorithm itself — but this codebase's own established structure
  (unchanged since Phase 0's own decision to defer OLED) treats Anomaly
  and Reporting as two separate layers in `docs/architecture.md`'s layer
  stack, developed as separate phases (matching how Phase 1 was
  "routing only," not "routing plus telemetry," and Phase 2 was
  "predictor only"). More concretely: wiring a real OLED requires adding
  an external Arduino library (Adafruit_SSD1306/GFX or U8g2) that doesn't
  exist in this project yet — installing a new dependency is exactly the
  kind of consequential, hard-to-reverse action this project's standing
  guidance says to flag/ask about rather than decide silently, unlike
  `analogRead()` (a built-in Arduino core function, no new dependency,
  already fully documented as this phase's real hardware target since
  Phase 0's `docs/parameters.md`). The guide's own Hours 12-17 sync
  checkpoint ("twisting the pot on Node C shows SPIKE/JUMP; holding it
  still shows STUCK") is still honestly demonstrable through this phase's
  real `[ANOMALY]` Serial log lines, the same verification channel every
  prior phase has used.
- **Alternatives considered:** (a) Add the OLED library now and wire it up
  to fully match the guide's Hours 12-17 bucket. (b) Silently skip
  mentioning the OLED gap at all.
- **Why alternatives were rejected:** (a) is a real, consequential new
  dependency decision that should be confirmed rather than assumed,
  especially given Phase 0 explicitly punted on exactly this question with
  the same reasoning ("Revisit when the reporting layer's phase starts" —
  see
  [decisions.md](decisions.md#no-oledsensor-library-dependency-introduced-yet)).
  (b) would leave a real guide/roadmap-vs-codebase-structure mismatch
  undocumented, contradicting the project's standing "document, don't
  silently redesign or silently drop" principle.
- **Impact:** `docs/known-issues.md` tracks OLED wiring as explicitly
  deferred, not forgotten. Revisit when a future phase takes on the
  reporting/dashboard layer.
- **Phase/date:** Phase 3, 2026-08-17.

## No `MeshPacket`/wire format changes for anomaly flags in Phase 3
- **Decision:** Anomaly flags are not added to any packet payload in
  Phase 3. `core/packet.h`/`docs/protocol.md` are unchanged.
- **Reason:** Same shape as
  [Phase 2's equivalent decision](decisions.md#no-meshpacketwire-format-changes-needed-for-phase-2) —
  nothing in this phase's real scope (local sensor anomaly detection,
  logged locally) requires transmitting a flag over the mesh yet. Sending
  an anomaly flag as part of a real `MSG_DATA` packet (per
  `message_types.h`'s own comment, `"application payload (sensor reading,
  anomaly flag, ...)"`) requires the reliability layer's real hop-by-hop
  relay to actually be useful beyond a single hop — still a later phase's
  scope (§5.4), unbuilt as of Phase 3.
- **Alternatives considered:** Add an anomaly-flag field to `MeshPacket`
  or a new payload sub-format now, in anticipation of the reliability
  layer needing it.
- **Why alternatives were rejected:** Speculative protocol growth ahead of
  the layer that actually needs it — the same "don't add fields because
  they may be useful" principle Phase 0 established for `MeshPacket` and
  Phase 1 re-applied to the TTL/hop-count question.
- **Impact:** None to the wire format.
- **Phase/date:** Phase 3, 2026-08-17.

## GUI integration audit performed before Phase 4 — no firmware changes made
- **Decision:** With the real GUI implementation now present in the repo
  (`gui-main/gui-main/`, including its own authored
  `docs/gui-telemetry-contract.md`), a read-only audit compared the GUI's
  actual parser/contract against current firmware, producing a full
  compatibility matrix (GUI-expects / firmware-provides / status) across
  all 10 contract message types. No firmware architecture, enums, structs,
  or serialization code were changed as part of this audit, and
  `docs/gui-telemetry-contract.md` was **not** created at the repo root —
  the existing file already inside `gui-main/gui-main/docs/` is the real
  one; duplicating or paraphrasing it into a second location risked the
  two drifting out of sync.
- **Reason:** The task explicitly required inspecting the GUI's real
  source as the authoritative reference (not assuming the contract
  referenced in the Phase 3 spec still matched, and not fabricating a
  canonical contract file until a real comparison justified one), and
  explicitly forbade starting Phase 4 or modifying firmware architecture
  during the audit.
- **Alternatives considered:** (a) Immediately update firmware
  enums/structs (e.g. `predictor_core::LinkHealth`,
  `anomaly_core::SensorState`) to match the GUI's richer vocabulary
  (`linkState`'s 6 values, `sensorHealth`'s 6 values) while doing the
  audit. (b) Copy `gui-main/gui-main/docs/gui-telemetry-contract.md` to
  `docs/gui-telemetry-contract.md` so firmware docs had a local copy.
- **Why alternatives were rejected:** (a) is exactly the "no firmware
  changes yet" instruction the audit was scoped to avoid, and some of the
  needed mappings are genuinely ambiguous (e.g. does `SensorState::WARMUP`
  map to `sensorHealth::SUSPECT`, or to nothing/`NORMAL`? does
  `SensorState::INVALID` map to `OUT_OF_RANGE`?) — a real design decision,
  not a mechanical rename, and not this audit's job to decide unilaterally.
  (b) creates two copies of the same frozen contract that can silently
  diverge; the GUI team's copy is the authoritative one and should stay
  the only one until a real wire-serialization phase needs firmware's own
  reference copy.
- **Impact:** `docs/known-issues.md`'s GUI-telemetry-contract entry is
  updated to reflect that the contract now genuinely exists (resolving the
  Phase 3 "not found" flag) while recording that firmware still implements
  none of its wire format. The full message-by-message compatibility
  matrix was delivered directly to the user rather than duplicated into a
  docs file, since it's an audit finding tied to this specific moment in
  both codebases, not a standing architectural decision.
- **Phase/date:** Post-Phase-3, pre-Phase-4, 2026-08-17.

## reliability_core split out as an Arduino-free pure module (Phase 4)
- **Decision:** `src/reliability/` follows the exact routing_core/
  predictor_core/anomaly_core split from Phases 1-3: `reliability_core.h/.cpp`
  (packet identity, duplicate cache, pending-transmission tracking,
  retry/timeout state machine, statistics — zero Arduino/ESP-NOW
  dependency, every function takes `now` explicitly) and `reliability.h/.cpp`
  (the Arduino-facing adapter — real `MeshPacket` construction/parsing,
  `transport::send()`, `millis()`, `logger::*`). `reliability_core` also
  deliberately never stores payload bytes — a resend needs the *original*
  bytes, and only the adapter (which owns `transport::send()`) has any
  business owning them; the adapter keeps its own `g_pendingPackets[]`
  array, parallel-indexed to `reliability_core`'s own `pending[]` by slot.
- **Reason:** Retry/timeout/duplicate-filter bookkeeping is real
  algorithmic logic (not just glue), and this project's established
  pattern is that real algorithmic logic gets verified by a host-compiled
  test harness, which only means something if the module has no ESP32
  toolchain dependency.
- **Alternatives considered:** Fold pending-transmission tracking directly
  into `reliability.cpp`, tested only via the real ESP32 compile (like
  `main.cpp` itself).
- **Why alternatives were rejected:** Every prior phase's decision to split
  out a pure core paid for itself immediately in bugs caught before ESP32
  compilation (Phase 2's off-by-one, Phase 3's two bugs); there's no reason
  this phase's bookkeeping — arguably the most state-machine-heavy of the
  four — would be exempt from that benefit.
- **Impact:** `firmware/PredictiveMesh/test/test_reliability_core.cpp` — 18
  test functions, 88/88 checks, compiled and run with host g++, zero
  Arduino/ESP-NOW dependency. See docs/testing.md.
- **Phase/date:** Phase 4, 2026-08-17.

## Packet identity is (source, sequence), reusing MeshPacket's existing header fields — no new wire format
- **Decision:** Part 1's packet identity is `reliability_core::PacketId{source, sequence}`,
  built directly from `MeshPacket.source`/`MeshPacket.sequence` —
  fields Phase 0 already defined and Phase 1-3 already documented as
  reserved for this exact purpose (`packet.h`'s own comment: "per-source
  monotonically increasing counter, for future duplicate filtering, §5.4").
  `sequence` is assigned once, by whichever node originates a packet
  (`reliability_core::nextSequence()`, a per-node monotonic counter
  starting at 0), and is preserved byte-for-byte unchanged through every
  hop of a forward (Part 6) — a relay never reassigns it.
- **Reason:** This identity has to be stable across the packet's entire
  multi-hop journey (Part 6: "for forwarded packets, preserve the original
  source identity and sequence identity") for the duplicate filter to work
  at every hop, not just the first. Reusing the existing header fields also
  means Phase 4 adds zero new bytes to the wire frame — `protocol.md`'s
  layout is unchanged.
- **This identity is explicitly NOT the GUI telemetry contract's envelope
  `seq`** (`gui-main/gui-main/docs/gui-telemetry-contract.md`). That field
  doesn't exist in firmware at all yet (see docs/known-issues.md), and even
  once it does, it will number GUI *telemetry messages*, not mesh
  *packets* — a `HELLO`/`STATISTICS`/etc. message and a `MeshPacket` are
  different things at different layers. Conflating the two would silently
  break either the duplicate filter (if GUI sequence semantics differ) or
  the telemetry contract (if mesh retransmits altered the GUI's own
  message numbering). See Part 1's explicit instruction: "Do not reuse the
  GUI telemetry sequence number."
- **Alternatives considered:** A separate packet-identity field (e.g. a
  128-bit UUID) distinct from the existing `sequence` field.
- **Why alternatives were rejected:** `(source, sequence)` is already
  exactly what §5.4 asks for ("a sequence-number-based duplicate filter"),
  already exists in the wire format, and a 16-bit per-source counter is
  more than sufficient for a 5-node topology's traffic volume — a UUID
  would be speculative complexity with no real requirement behind it.
- **Impact:** `core/packet.h`'s `sequence` field comment updated to reflect
  real (not "future") use. No wire-format bytes added.
- **Phase/date:** Phase 4, 2026-08-17.

## beginTx() reserves a tracking slot BEFORE the real radio send, with cancelTx() for synchronous failures
- **Decision:** The adapter calls `reliability_core::beginTx()` first
  (reserving a pending slot, `attemptCount=1`), *then* attempts the actual
  `transport::send()`. If the pending pool is already full, no radio
  transmission is ever launched at all. If the real send is rejected
  synchronously (e.g. an unregistered peer), the adapter calls the new
  `reliability_core::cancelTx(slot)` to declare failure immediately,
  rather than reserving the slot and waiting up to
  `RELIABILITY_ACK_TIMEOUT_MS` to learn something already known for
  certain.
- **Reason:** The alternative order (send first, track second) has a real
  failure mode: if the pool is full at that point, a frame has already
  gone out over the radio with nothing watching for its ACK — a wasted
  transmission whose outcome is silently discarded. Reserving first avoids
  ever launching an untracked frame, and `cancelTx()` avoids delaying a
  known-synchronous failure behind a timer for no reason (Part 5's "do not
  block... indefinitely" read in spirit: don't needlessly delay a known
  outcome either).
- **Alternatives considered:** Send-first-then-track (rejected above).
  Also considered: no `cancelTx()` at all, letting a synchronously-rejected
  send simply time out normally.
- **Why alternatives were rejected:** A silent, un-tracked wasted
  transmission is worse than a slightly more complex call order costing
  one extra core function. Letting a known failure play out through the
  full timeout/retry cycle (`RELIABILITY_MAX_RETRIES` more wasted attempts,
  each also synchronously rejected) would multiply that waste for zero
  benefit — the outcome is already certain.
- **Impact:** `reliability_core.h`/`.cpp` — `cancelTx()`. Tested directly
  (`test_cancel_tx_immediate_failure`).
- **Phase/date:** Phase 4, 2026-08-17.

## ACK packets are fire-and-forget — never themselves acknowledged
- **Decision:** `MSG_ACK` packets are sent via a direct `transport::send()`
  call with no `reliability_core::beginTx()` tracking, no retry, and no
  expectation of a reply.
- **Reason:** Acknowledging an ACK would require acknowledging *that*
  acknowledgement too, recursing forever — every real reliable-delivery
  protocol (TCP included) treats the ACK itself as best-effort. If an ACK
  is lost, the *original* sender's own retry (of the DATA packet, not the
  ACK) naturally recovers — the receiver will simply re-ACK the
  retransmitted DATA packet when it arrives again (Part 6's duplicate
  filter still catches it, but the ACK still goes out — see the next
  entry).
- **Alternatives considered:** A second, lighter acknowledgement tier for
  ACK packets specifically.
- **Why alternatives were rejected:** Unbounded recursion risk for a
  problem the existing DATA-packet retry already solves for free.
- **Impact:** `reliability.cpp`'s `sendAck()` — one `transport::send()`
  call, no pending-slot involvement.
- **Phase/date:** Phase 4, 2026-08-17.

## Every received MSG_DATA is hop-ACKed BEFORE the duplicate check, unconditionally
- **Decision:** `reliability::onPacketReceived()`'s `handleData()` sends
  the hop-ACK back to `prev_hop` as its very first action — before
  checking whether the packet is a duplicate, before checking whether it's
  addressed here, before attempting to forward it.
- **Reason:** The hop-ACK's meaning is narrowly "the transmission from
  `prev_hop` to me, right now, over the radio, succeeded" — a fact that is
  true regardless of what happens to the packet afterward. Withholding the
  ACK on a duplicate (because "we already have this one") would make
  `prev_hop` keep retrying a hop-transmission that actually succeeded,
  wasting `RELIABILITY_MAX_RETRIES` more attempts for no reason — exactly
  the kind of unnecessary retry Part 4/5 asks to bound.
- **Alternatives considered:** ACK only on a genuinely new (non-duplicate)
  packet.
- **Why alternatives were rejected:** Conflates two different questions —
  "did this specific radio hop succeed" (always yes, if we're here to ask
  the question at all) and "has the application already seen this
  packet" (a separate, receiver-side concern the duplicate filter alone
  answers). Answering the first question wrong to encode the second would
  make `prev_hop`'s own retry logic behave incorrectly for a case (a lost
  ACK, not a lost DATA frame) it isn't designed to detect.
- **Impact:** `reliability.cpp`'s `handleData()` — `sendAck()` is
  unconditional, called before `isDuplicateAndRecord()`.
- **Phase/date:** Phase 4, 2026-08-17.

## Forwarding loop prevention relies on routing_core correctness + a next-hop-not-prev-hop guard + the duplicate filter — no new TTL field
- **Decision:** Part 7's forwarding path adds exactly one new defensive
  check beyond calling `routing::selectNextHop()`: refuse to forward if
  the selected next hop equals the packet's own `prev_hop` (would bounce
  the packet straight back to whoever just sent it). No hop-count/TTL
  field was added to `MeshPacket`.
- **Reason:** This revisits, rather than repeats, the Phase 0/1 TTL
  decision (`docs/decisions.md#no-ttlhop-count-field-added-to-meshpacket-in-phase-1`)
  now that real forwarding actually exists — that earlier decision's own
  reasoning ("Phase 1 does not implement actual hop-by-hop relaying...
  revisit when that layer is built") explicitly pointed at this exact
  phase as the revisit point. Three independent, already-real mechanisms
  bound any potential loop without a new field: (1) `routing_core`'s
  distance-vector table is proven (21/21 tests, including that a node
  never selects itself as next hop) and this topology is small/fixed (5
  nodes, max real path 4 hops); (2) the new `nextHop != prevHop` guard
  catches the most likely accidental case (an immediate one-hop bounce);
  (3) even in an unlikely transient-table-inconsistency scenario, the
  duplicate filter (Part 6) recognizes a packet's `(source, sequence)`
  coming back around and drops it before forwarding it again, bounding any
  possible loop to at most one extra circuit.
- **Alternatives considered:** Add a `hop_count`/TTL field to `MeshPacket`,
  decremented per hop, dropped at zero.
- **Why alternatives were rejected:** Would be the first new field added to
  `MeshPacket` since Phase 0, for a failure mode this topology's own
  proven routing correctness plus two already-existing mechanisms already
  bound. Speculative protocol growth ahead of an actually-observed problem
  — the same standard Phase 0/1/2/3 have consistently applied to this
  struct. Revisit if real hardware testing ever shows an actual loop.
- **Impact:** `reliability.cpp`'s `handleData()` forward branch. No wire
  format change; `docs/protocol.md`'s "What's deliberately NOT in this
  packet yet" TTL entry updated to reflect this as the real revisit
  decision rather than a still-open question.
- **Phase/date:** Phase 4, 2026-08-17.

## PDR is fed per-attempt, not per-packet — and never from the raw ESP-NOW send callback
- **Decision:** `predictor::onSendResult(neighbor, success)` is called
  exactly once per individual unicast *attempt's* outcome: `false` for
  every attempt that times out (whether or not a retry follows — both
  `TimeoutAction::RETRY` and `TimeoutAction::FAILED` represent one failed
  attempt), `true` exactly once when a real `MSG_ACK` matches. It is never
  fed from `transport::TxCallback`/`onEspNowSent()` (the raw ESP-NOW
  driver signal) — that callback only confirms the radio accepted/
  transmitted the frame at the 802.11 layer, not that the application on
  the other end ever processed it. The Phase 0 stub
  `reliability::onSendResult(NodeId, bool)` — apparently designed to
  receive exactly that raw radio signal — is removed rather than repurposed
  (see the dedicated entry below).
- **Reason:** Part 3 explicitly requires distinguishing the ESP-NOW send
  callback from application-level delivery ACK, and forbids treating the
  former as delivery evidence. Feeding PDR from attempts (not from the
  final packet-series outcome) matches PDR's own meaning — "packet
  delivery ratio" is inherently a per-transmission-attempt statistic; a
  hop-transmission that took 2 failed attempts then succeeded really did
  experience 2 dropped radio frames and 1 delivered one, and that's the
  granularity `predictor_core`'s PDR EWMA was already built to consume in
  Phase 2.
- **Alternatives considered:** (a) Feed PDR the ESP-NOW `TxCallback`
  result directly (simplest, but explicitly forbidden by Part 3). (b) Feed
  PDR once per whole packet series (final success/fail only), not per
  attempt.
- **Why alternatives were rejected:** (a) would let a frame that
  transmitted fine over the radio but never got an application response
  (e.g. the neighbor's firmware crashed, or its own ACK got lost) look
  like a healthy link — exactly the false-positive Part 3 warns against.
  (b) would under-count real degradation: a link needing 2 retries per
  packet to succeed is measurably worse than one that never needs a retry,
  but "final outcome only" PDR would report both as 100% — hiding real
  RF-layer degradation from the very metric meant to detect it.
- **Impact:** `reliability.cpp`'s `tick()` (RETRY/FAILED branches) and
  `handleAck()` are the only two call sites for `predictor::onSendResult()`.
  Resolves the Phase 2 gap documented at
  [decisions.md](decisions.md#pdr-measurement-boundary-not-wired-to-live-send-outcomes-in-phase-2) —
  the wiring is now real, though see the "no live automatic caller"
  entry below for what's still not exercised without real application
  traffic.
- **Phase/date:** Phase 4, 2026-08-17.

## PDR represents per-hop unicast delivery, not end-to-end delivery
- **Decision:** Every PDR observation `reliability` feeds to `predictor`
  is scoped to exactly one radio hop — "did THIS node's unicast
  transmission to THIS direct neighbor succeed" — never "did the packet
  reach its ultimate, possibly-multi-hop destination."
- **Reason:** This isn't actually a new choice Phase 4 gets to make —
  `predictor_core.h`'s own file header already documents this scope
  explicitly, unchanged since Phase 2: "link_score is always a statement
  about THIS node's own radio link to that neighbor, never a multi-hop/
  end-to-end quantity." Phase 4 just had to honor that existing contract
  when wiring real observations into it, per Part 8's explicit instruction
  not to mix the two definitions.
- **Alternatives considered:** An end-to-end delivery ratio (source-to-
  final-destination), tracked separately or instead.
- **Why alternatives were rejected:** Would require a fundamentally
  different mechanism (only the final destination could confirm true
  end-to-end delivery, requiring its own return-path signal back to the
  original source — not something any part of this phase's spec asked
  for) and would conflate two genuinely different questions ("is my radio
  link to B healthy" vs. "did my packet reach S three hops away") into one
  number, which Part 8 explicitly forbids doing.
- **Impact:** `reliability_core::Statistics.packetsDelivered` is
  documented as per-hop (mirroring PDR's own scope), not end-to-end — see
  its field comment in `reliability_core.h`.
- **Phase/date:** Phase 4, 2026-08-17.

## reliability::send() has no live automatic caller in Phase 4 — no application data source was invented
- **Decision:** Nothing in `main.cpp`'s `loop()` calls `reliability::send()`
  automatically or periodically. The function is real, fully implemented,
  and tested via `reliability_core`'s host suite plus a real ESP32 compile
  of the whole adapter — but Phase 4 does not invent what real application
  payload a node should periodically send, to whom, or how often.
- **Reason:** No document in this repository (implementation-guide.html,
  the task specs for any phase so far, or the GUI's own telemetry
  contract) defines what real periodic application data this mesh's nodes
  are supposed to exchange over `MSG_DATA`. Inventing one now — e.g. "each
  node pings the sink every N seconds with its latest sensor reading" —
  would be exactly the kind of fabricated protocol/schedule this project's
  standing "no fake data, document rather than silently invent" rule
  (reinforced in every phase so far, most recently for the Phase 3
  GUI-telemetry-contract gap) exists to prevent. This is structurally the
  same category of gap Phase 2 already accepted and documented for PDR
  itself ("the math and API are complete and independently tested, but
  nothing calls it yet") — Phase 4 provides the mechanism one layer up
  (real hop-by-hop reliable delivery, not just the PDR math it feeds), but
  still has no live traffic source, for the same reason.
- **Alternatives considered:** Add a minimal periodic "keepalive" unicast
  ping from each node toward the sink, purely to exercise the pipeline and
  give PDR real, live numbers once hardware exists.
- **Why alternatives were rejected:** Even a "minimal ping" requires
  inventing a schedule, a payload shape, and a purpose — real design
  decisions with no basis in any existing spec. Building the mechanism
  correctly and leaving its use to a phase (or explicit instruction) that
  actually defines the application-level data contract is safer than
  guessing at one now. `routing`'s own beacon is not a precedent for
  inventing new traffic here — it exists because distance-vector routing
  is structurally impossible without periodic advertisements; nothing
  about reliable delivery requires traffic to exist for its own sake.
- **Impact:** `reliability::send()` is real, public, and callable by any
  future phase (or a manual test/demo trigger) with zero further wiring
  needed. Until something calls it, `reliability_core::Statistics` and
  live PDR values stay at their initialized defaults on real hardware —
  documented in `docs/known-issues.md`, not silently implied to be "live."
- **Phase/date:** Phase 4, 2026-08-17.

## reliability::onSendResult(NodeId, bool) (Phase 0 stub) removed, not repurposed
- **Decision:** The Phase 0 stub `void onSendResult(NodeId dst, bool success);`
  (declared in the original `reliability.h`, logged-only, never wired to
  any real caller in Phases 0-3) is deleted entirely rather than kept or
  repurposed to receive the raw ESP-NOW `TxCallback` signal.
- **Reason:** Its name and signature — identical in shape to
  `predictor::onSendResult()` — was the single most natural place a future
  maintainer would be tempted to route ESP-NOW's raw `TxCallback` directly
  into PDR, which is exactly the conflation Part 3 forbids (see the
  "PDR is fed per-attempt" entry above). Removing it entirely, rather than
  leaving it as an unwired or repurposed function, avoids that trap by
  construction. `espnow_transport.cpp`'s own `logger::tx()` call already
  provides equivalent per-send diagnostic visibility (`[TX] dst=... status=...`)
  without needing a second hop through `reliability`.
- **Alternatives considered:** Keep the stub, wire it to the raw
  `TxCallback`, and use it purely for logging/diagnostics distinct from
  PDR.
- **Why alternatives were rejected:** A same-named, similar-shaped,
  *almost*-but-not-quite-the-real-PDR-signal function sitting right next
  to the real one is a maintenance hazard, not a diagnostic feature — and
  `logger::tx()` already covers the diagnostic need. Matches this
  project's CLAUDE.md guidance to delete code that's confirmed unused
  rather than leave an ambiguous half-implementation around.
- **Impact:** `reliability.h`'s public API no longer includes
  `onSendResult()`. `main.cpp`'s `onTransportTx()` remains the pre-existing
  no-op it already was in Phases 0-3 — not a regression, since nothing
  ever called the removed function for real.
- **Phase/date:** Phase 4, 2026-08-17.

## Statistics counters: packet-series granularity vs. attempt granularity, kept explicitly separate (Part 9)
- **Decision:** `reliability_core::Statistics` deliberately mixes two
  different counting granularities, each documented per-field:
  `packetsSent`/`packetsDelivered`/`packetsFailed` count once per
  hop-transmission *series* (one (source, sequence, nextHop) triple,
  however many attempts it took — incremented at `beginTx()`,
  `onAckReceived()`'s match, and `tickTimeouts()`'s `FAILED` branch /
  `cancelTx()` / `recordImmediateFailure()` respectively), while
  `retries`/`acknowledgements` count at individual-*attempt* granularity
  (`tickTimeouts()`'s `RETRY` branch; `onAckReceived()`'s match). The
  worked example from Part 9 (1 original + 2 retries + final success) is a
  direct test (`test_part9_one_packet_two_retries_then_success`):
  `packetsSent=1`, `retries=2`, `acknowledgements=1`, `packetsDelivered=1`.
- **Reason:** Part 9 explicitly requires being unambiguous about which
  granularity each number represents rather than silently picking one and
  hoping it's obvious. A retry is not a second application packet (Part
  9: "do not double-count retries as separate application packets"), but
  it IS a second real attempt worth its own count for diagnosing link
  quality — both facts need their own field rather than collapsing into
  one ambiguous "packets" number.
- **Alternatives considered:** A single unified counting scheme (e.g.
  count every individual radio transmission as "a packet").
- **Why alternatives were rejected:** Would make `packetsSent` `+`
  `retries` conflate "how many distinct application-level things this node
  tried to deliver" with "how much radio airtime this node used" — two
  genuinely different, both useful, numbers that Part 9 asked to be kept
  distinct.
- **Impact:** `reliability_core.h`'s `Statistics` struct — each field's
  granularity documented in its own comment, not left implicit.
- **Phase/date:** Phase 4, 2026-08-17.
