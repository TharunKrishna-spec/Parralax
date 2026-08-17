// Predictive Self-Healing IoT Mesh — Phase 0 firmware foundation.
//
// Same image is flashed to all five physical boards (A, B, C, D, S); which
// role this build is compiles from src/config.h's THIS_NODE_ID. See
// docs/architecture.md before changing anything in src/.
//
// This .ino is intentionally thin: it exists only because Arduino IDE
// requires a sketch-folder-matching .ino as the build entry point. All real
// logic lives in src/ (compiled as the sketch's special "src" subfolder —
// see docs/architecture.md for why that's a real Arduino build feature and
// not a workaround).
#include "src/main.h"

void setup() {
  app::setup();
}

void loop() {
  app::loop();
}
