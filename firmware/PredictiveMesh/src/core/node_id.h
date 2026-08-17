#pragma once
#include <stdint.h>

// ============================================================
// Node identity — the single source of truth for the 5-node topology.
//
// This header is intentionally free of any THIS_NODE_ID selection: it just
// describes the fixed universe of nodes and their fixed relationships,
// derived directly from implementation-guide.html §01 (topology) and §03
// (hardware/OLED/buzzer allocation per node). Which one of these five a
// given physical board *is* gets decided in config.h — see that file for
// why a compile-time define was chosen over MAC-address auto-detection.
// ============================================================

enum NodeId : uint8_t {
  NODE_A = 0,  // source
  NODE_B = 1,  // relay (primary path)
  NODE_C = 2,  // relay (alternate path), has OLED
  NODE_D = 3,  // relay (alternate path)
  NODE_S = 4,  // sink / root, has OLED
  NODE_ID_COUNT = 5,
  NODE_ID_UNKNOWN = 0xFF
};

enum NodeRole : uint8_t {
  ROLE_SOURCE,
  ROLE_RELAY,
  ROLE_SINK
};

struct NodeInfo {
  NodeId id;
  NodeRole role;
  const char* name;
  bool hasOled;     // per §03 build checklist: only S and C carry a display
  bool hasBuzzer;   // per §03 pin table: every node wires the buzzer
  uint8_t mac[6];   // ESP-NOW peer MAC. All-zero = not yet configured.
                     // Hardware doesn't exist yet, so these start as a
                     // placeholder sentinel — see docs/known-issues.md.
                     // Fill in with the real MAC (each board logs its own
                     // MAC over Serial at boot) once flashed.
};

inline const char* roleName(NodeRole role) {
  switch (role) {
    case ROLE_SOURCE: return "SOURCE";
    case ROLE_RELAY:  return "RELAY";
    case ROLE_SINK:   return "SINK";
    default:          return "UNKNOWN";
  }
}

// Meyer's-singleton pattern (function-local static, `inline` function) so
// this whole module stays header-only — no node_id.cpp needed, and no risk
// of ODR violations across translation units. Safe in any C++ standard
// since C++98; doesn't require C++17 inline variables.
inline const NodeInfo* nodeTable() {
  static const NodeInfo table[NODE_ID_COUNT] = {
    { NODE_A, ROLE_SOURCE, "A", false, true, {0, 0, 0, 0, 0, 0} },
    { NODE_B, ROLE_RELAY,  "B", false, true, {0, 0, 0, 0, 0, 0} },
    { NODE_C, ROLE_RELAY,  "C", true,  true, {0, 0, 0, 0, 0, 0} },
    { NODE_D, ROLE_RELAY,  "D", false, true, {0, 0, 0, 0, 0, 0} },
    { NODE_S, ROLE_SINK,   "S", true,  true, {0, 0, 0, 0, 0, 0} },
  };
  return table;
}

inline const NodeInfo& nodeInfo(NodeId id) {
  if (id >= NODE_ID_COUNT) id = NODE_A;  // defensive fallback; should never happen with valid config
  return nodeTable()[id];
}

inline const char* nodeName(NodeId id) {
  return nodeInfo(id).name;
}

// Static neighbor adjacency, derived from the fixed topology in
// implementation-guide.html §01:
//   A-B, A-C, A-S (weak direct), B-S, C-D, D-S
// This is who each node needs an ESP-NOW peer entry for — NOT the full
// 5-node set. Routing/reliability (later phases) forward hop-by-hop across
// these edges; Phase 0 only uses this to know which peers to register.
inline const NodeId* neighborsOf(NodeId id, uint8_t& count) {
  static const NodeId NEIGHBORS_A[] = { NODE_B, NODE_C, NODE_S };
  static const NodeId NEIGHBORS_B[] = { NODE_A, NODE_S };
  static const NodeId NEIGHBORS_C[] = { NODE_A, NODE_D };
  static const NodeId NEIGHBORS_D[] = { NODE_C, NODE_S };
  static const NodeId NEIGHBORS_S[] = { NODE_B, NODE_D, NODE_A };

  switch (id) {
    case NODE_A: count = 3; return NEIGHBORS_A;
    case NODE_B: count = 2; return NEIGHBORS_B;
    case NODE_C: count = 2; return NEIGHBORS_C;
    case NODE_D: count = 2; return NEIGHBORS_D;
    case NODE_S: count = 3; return NEIGHBORS_S;
    default:     count = 0; return nullptr;
  }
}
