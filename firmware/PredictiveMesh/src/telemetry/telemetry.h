#pragma once

// ============================================================
// Reporting layer (implementation-guide.html §01, §07 "Reporting Layer"):
// OLED on Nodes S and C, Serial/WebSerial dashboard feed on all nodes.
//
// NOT YET IMPLEMENTED in Phase 0. Deliberately no OLED library dependency
// (e.g. Adafruit_SSD1306) is pulled in yet, so this compiles cleanly
// without requiring that library to be installed before Phase 0 is even
// flashed — see docs/decisions.md. Pin/address contract (GPIO21/22,
// 0x3C) already lives in config.h, ready for when this is implemented.
// ============================================================

namespace telemetry {

void init();

}  // namespace telemetry
