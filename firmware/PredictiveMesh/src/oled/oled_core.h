#pragma once
#include <stdint.h>

// ============================================================
// oled_core — pure display-scheduling logic, deliberately free of any
// Arduino/Wire/Adafruit_GFX dependency (no millis(), no I2C, no drawing).
// Mirrors the routing_core/predictor_core/anomaly_core/apptraffic_core
// split: this is real, deterministic logic (auto-cycle timing, a
// temporary event override, redraw rate-limiting) worth verifying on its
// own with a host compiler. src/oled/oled.cpp is the thin Arduino-facing
// adapter that owns the actual display object(s), I2C bus, and Adafruit_GFX
// drawing calls — none of that lives here.
//
// This module knows nothing about node identity, routing, predictor, or
// anomaly state — it only answers two questions for a caller ticking it
// once per app::loop() iteration: "which screen (from a caller-supplied
// fixed rotation) should be showing right now" and "is it time to actually
// push a new frame to the display." See docs/decisions.md.
// ============================================================

namespace oled_core {

// A caller (oled.cpp) assigns real per-node meaning to each value — this
// module never interprets them. NODE_STATUS/SENSOR_ANOMALY/LINK_QUALITY
// are regular auto-cycled screens; LINK_EVENT is only ever reached via
// triggerOverride() (a real, transition-only event), never by the normal
// auto-cycle.
enum class Screen : uint8_t { NODE_STATUS, SENSOR_ANOMALY, LINK_QUALITY, LINK_EVENT };

static const uint8_t MAX_CYCLE_SCREENS = 4;

struct State {
  Screen cycleScreens[MAX_CYCLE_SCREENS];
  uint8_t cycleCount;
  uint8_t cycleIndex;

  Screen currentScreen;
  uint32_t screenEnteredMs;

  bool overrideActive;
  uint32_t overrideUntilMs;

  uint32_t lastRefreshMs;
  bool everRefreshed;
};

struct TickResult {
  Screen screen;
  bool shouldRedraw;
};

// `screens`/`count` (1 <= count <= MAX_CYCLE_SCREENS) define the fixed
// auto-cycle rotation this node uses — e.g. Node C cycles
// {NODE_STATUS, SENSOR_ANOMALY}, Node S cycles {NODE_STATUS, LINK_QUALITY}.
// Starts on cycleScreens[0]. A count of 0 is treated as 1 (defensive —
// never leaves the state with no valid current screen).
void init(State& state, const Screen* screens, uint8_t count);

// Call once per oled::tick() (i.e. once per app::loop() iteration).
// Advances the auto-cycle every OLED_SCREEN_CYCLE_MS unless an override is
// active; expires an active override once OLED_EVENT_DISPLAY_MS has
// elapsed, resuming the auto-cycle from cycleScreens[0]. `shouldRedraw` is
// true exactly when the caller should actually push a frame — either the
// shown screen just changed, or OLED_REFRESH_MIN_INTERVAL_MS has elapsed
// since the last redraw — so a caller ticking every ~10ms loop iteration
// doesn't hit the display far more often than a human could read it or
// than the rate limit allows. Wraparound-safe against millis() rollover
// (~49.7 days), matching this project's existing convention.
TickResult tick(State& state, uint32_t now);

// Forces `screen` onto the display for OLED_EVENT_DISPLAY_MS, then resumes
// the normal auto-cycle from cycleScreens[0]. The caller is responsible for
// only invoking this on a real state transition (never on an unchanged
// re-observation) — oled_core itself has no domain knowledge to detect
// that.
void triggerOverride(State& state, Screen screen, uint32_t now);

}  // namespace oled_core
