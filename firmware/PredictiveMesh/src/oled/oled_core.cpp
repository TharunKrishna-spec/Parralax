#include "oled_core.h"
#include "../config.h"

namespace oled_core {

namespace {

// Wraparound-safe "now >= deadline", matching the project's existing
// millis()-rollover convention used throughout *_core (e.g.
// predictor_core's staleness checks): unsigned subtraction wraps correctly
// as long as the true elapsed time never exceeds ~24.8 days (half the
// uint32_t range), which every timeout in this project is far under.
bool reached(uint32_t now, uint32_t deadline) {
  return (now - deadline) < 0x80000000u;
}

}  // namespace

void init(State& state, const Screen* screens, uint8_t count) {
  uint8_t n = count;
  if (n == 0) n = 1;
  if (n > MAX_CYCLE_SCREENS) n = MAX_CYCLE_SCREENS;

  for (uint8_t i = 0; i < n; i++) state.cycleScreens[i] = screens[i];
  state.cycleCount = n;
  state.cycleIndex = 0;

  state.currentScreen = state.cycleScreens[0];
  state.screenEnteredMs = 0;

  state.overrideActive = false;
  state.overrideUntilMs = 0;

  state.lastRefreshMs = 0;
  state.everRefreshed = false;
}

void triggerOverride(State& state, Screen screen, uint32_t now) {
  state.overrideActive = true;
  state.overrideUntilMs = now + OLED_EVENT_DISPLAY_MS;
  state.currentScreen = screen;
  state.screenEnteredMs = now;
}

TickResult tick(State& state, uint32_t now) {
  bool screenChanged = false;

  if (state.overrideActive) {
    if (reached(now, state.overrideUntilMs)) {
      state.overrideActive = false;
      state.cycleIndex = 0;
      state.currentScreen = state.cycleScreens[state.cycleIndex];
      state.screenEnteredMs = now;
      screenChanged = true;
    }
    // else: override still showing — auto-cycle timing is frozen (an
    // active override always wins over the regular rotation).
  } else if (now - state.screenEnteredMs >= OLED_SCREEN_CYCLE_MS) {
    state.cycleIndex = static_cast<uint8_t>((state.cycleIndex + 1) % state.cycleCount);
    state.currentScreen = state.cycleScreens[state.cycleIndex];
    state.screenEnteredMs = now;
    screenChanged = true;
  }

  bool intervalElapsed = !state.everRefreshed || (now - state.lastRefreshMs) >= OLED_REFRESH_MIN_INTERVAL_MS;
  bool shouldRedraw = screenChanged || intervalElapsed;
  if (shouldRedraw) {
    state.lastRefreshMs = now;
    state.everRefreshed = true;
  }

  return TickResult{ state.currentScreen, shouldRedraw };
}

}  // namespace oled_core
