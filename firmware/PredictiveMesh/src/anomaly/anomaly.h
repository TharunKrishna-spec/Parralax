#pragma once
#include <stdint.h>

// ============================================================
// Anomaly engine (implementation-guide.html §5.2).
//
// Target design: boot-time median/MAD calibration, then a modified Z-score
// (spike/jump detector) running alongside an independent flatline/stuck
// detector, both reported per-sample without merging. NOT YET IMPLEMENTED
// — see docs/known-issues.md.
// ============================================================

namespace anomaly {

enum class Flag : uint8_t { NONE, SPIKE_JUMP, STUCK };

void init();

// Evaluates one new raw ADC sample.
// Phase 0 stub: always returns Flag::NONE — no boot calibration exists yet,
// so no real flag could be meaningful. Deliberately fails safe rather than
// guessing.
Flag evaluate(uint16_t rawAdcValue);

}  // namespace anomaly
