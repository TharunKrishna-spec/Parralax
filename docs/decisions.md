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
