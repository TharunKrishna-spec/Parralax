// Minimal host-side unit test harness for oled_core's pure display-
// scheduling logic (screen auto-cycle, temporary event override, redraw
// rate-limiting). Like every other *_core test suite in this project, this
// never touches Arduino/Wire/Adafruit_GFX — oled_core.h/.cpp have zero such
// dependency specifically so this can compile and run with a plain host
// compiler. See docs/testing.md.
//
// Build & run (host g++ - NOT the ESP32 toolchain; run from this file's
// directory):
//   g++ -std=c++17 -Wall -Wextra -I ../src ../src/oled/oled_core.cpp test_oled_core.cpp -o test_oled_core
//   ./test_oled_core

#include "../src/oled/oled_core.h"
#include "../src/config.h"
#include <cstdio>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const char* description) {
  g_checks++;
  if (!condition) {
    g_failures++;
    std::printf("FAIL: %s\n", description);
  } else {
    std::printf("ok:   %s\n", description);
  }
}

using namespace oled_core;

const Screen kTwoScreenCycle[] = { Screen::NODE_STATUS, Screen::LINK_QUALITY };

// ---- 1. First tick always redraws (never seen a frame yet) ----
void test_first_tick_redraws() {
  State s;
  init(s, kTwoScreenCycle, 2);

  TickResult r = tick(s, 1000);
  check(r.screen == Screen::NODE_STATUS, "starts on cycleScreens[0]");
  check(r.shouldRedraw == true, "the very first tick always redraws");
}

// ---- 2. Immediate re-tick at the same timestamp is rate-limited ----
void test_immediate_retick_is_rate_limited() {
  State s;
  init(s, kTwoScreenCycle, 2);

  tick(s, 1000);
  TickResult r2 = tick(s, 1000);
  check(r2.screen == Screen::NODE_STATUS, "screen unchanged on an immediate re-tick");
  check(r2.shouldRedraw == false, "an immediate re-tick (same timestamp) does not redraw again");
}

// ---- 3. A tick within OLED_SCREEN_CYCLE_MS but past OLED_REFRESH_MIN_INTERVAL_MS redraws (same screen) ----
void test_interval_driven_redraw_without_screen_change() {
  State s;
  init(s, kTwoScreenCycle, 2);

  tick(s, 0);
  TickResult r = tick(s, OLED_REFRESH_MIN_INTERVAL_MS);
  check(r.screen == Screen::NODE_STATUS, "screen unchanged - still within one cycle interval");
  check(r.shouldRedraw == true, "redraw fires once OLED_REFRESH_MIN_INTERVAL_MS has elapsed, even with no screen change");
}

// ---- 4. Auto-cycle advances after OLED_SCREEN_CYCLE_MS ----
void test_auto_cycle_advances() {
  State s;
  init(s, kTwoScreenCycle, 2);

  tick(s, 0);
  TickResult r = tick(s, OLED_SCREEN_CYCLE_MS);
  check(r.screen == Screen::LINK_QUALITY, "auto-cycle advances to the next screen after OLED_SCREEN_CYCLE_MS");
  check(r.shouldRedraw == true, "a screen change always redraws regardless of the refresh-interval rate limit");
}

// ---- 5. A 2-screen rotation wraps back to the first screen ----
void test_cycle_wraps_around() {
  State s;
  init(s, kTwoScreenCycle, 2);

  tick(s, 0);
  tick(s, OLED_SCREEN_CYCLE_MS);
  TickResult r = tick(s, 2u * OLED_SCREEN_CYCLE_MS);
  check(r.screen == Screen::NODE_STATUS, "a 2-screen rotation wraps back to cycleScreens[0] on the 3rd interval");
}

// ---- 6. A 3-screen rotation cycles through all three in order ----
void test_three_screen_cycle_order() {
  State s;
  const Screen threeScreens[] = { Screen::NODE_STATUS, Screen::SENSOR_ANOMALY, Screen::LINK_QUALITY };
  init(s, threeScreens, 3);

  TickResult r0 = tick(s, 0);
  TickResult r1 = tick(s, OLED_SCREEN_CYCLE_MS);
  TickResult r2 = tick(s, 2u * OLED_SCREEN_CYCLE_MS);
  TickResult r3 = tick(s, 3u * OLED_SCREEN_CYCLE_MS);

  check(r0.screen == Screen::NODE_STATUS, "3-screen cycle: step 0 is NODE_STATUS");
  check(r1.screen == Screen::SENSOR_ANOMALY, "3-screen cycle: step 1 is SENSOR_ANOMALY");
  check(r2.screen == Screen::LINK_QUALITY, "3-screen cycle: step 2 is LINK_QUALITY");
  check(r3.screen == Screen::NODE_STATUS, "3-screen cycle: step 3 wraps back to NODE_STATUS");
}

// ---- 7. triggerOverride() immediately switches the shown screen ----
void test_override_switches_screen_immediately() {
  State s;
  init(s, kTwoScreenCycle, 2);
  tick(s, 0);

  triggerOverride(s, Screen::LINK_EVENT, 500);
  TickResult r = tick(s, 500);
  check(r.screen == Screen::LINK_EVENT, "triggerOverride() puts the override screen on the very next tick");
}

// ---- 8. An active override suppresses the normal auto-cycle, even past OLED_SCREEN_CYCLE_MS ----
void test_override_suppresses_auto_cycle() {
  State s;
  init(s, kTwoScreenCycle, 2);
  tick(s, 0);
  triggerOverride(s, Screen::LINK_EVENT, 100);

  TickResult r = tick(s, 100u + OLED_SCREEN_CYCLE_MS + 1);
  check(r.screen == Screen::LINK_EVENT,
        "an active override keeps showing even after enough time for a normal auto-cycle step would have elapsed");
}

// ---- 9. Override expires after OLED_EVENT_DISPLAY_MS and resumes the auto-cycle from screen 0 ----
void test_override_expires_and_resumes_cycle() {
  State s;
  init(s, kTwoScreenCycle, 2);
  tick(s, 0);
  triggerOverride(s, Screen::LINK_EVENT, 100);
  tick(s, 100);

  TickResult r = tick(s, 100u + OLED_EVENT_DISPLAY_MS);
  check(r.screen == Screen::NODE_STATUS, "the override expires at OLED_EVENT_DISPLAY_MS and resumes from cycleScreens[0]");
  check(r.shouldRedraw == true, "the override expiring is itself a screen change, so it redraws immediately");
}

// ---- 10. Override expiry resets the auto-cycle's own timing base ----
void test_override_expiry_resets_cycle_timer() {
  State s;
  init(s, kTwoScreenCycle, 2);
  tick(s, 0);
  triggerOverride(s, Screen::LINK_EVENT, 100);
  TickResult expired = tick(s, 100u + OLED_EVENT_DISPLAY_MS);
  (void)expired;

  // Immediately after expiry, one more OLED_SCREEN_CYCLE_MS must elapse
  // (from the expiry moment, not from the original t=0) before the next
  // auto-advance — i.e. the cycle timer restarts at expiry.
  uint32_t expiryMs = 100u + OLED_EVENT_DISPLAY_MS;
  TickResult tooSoon = tick(s, expiryMs + OLED_SCREEN_CYCLE_MS - 1);
  check(tooSoon.screen == Screen::NODE_STATUS, "auto-cycle does not advance until a full interval after the override expired");

  TickResult onTime = tick(s, expiryMs + OLED_SCREEN_CYCLE_MS);
  check(onTime.screen == Screen::LINK_QUALITY, "auto-cycle advances exactly one interval after the override expired");
}

// ---- 11. millis() wraparound is handled correctly for the override deadline ----
void test_override_wraparound_safety() {
  State s;
  init(s, kTwoScreenCycle, 2);

  uint32_t nearWrap = 0xFFFFFFF0u;  // 16 ms before uint32_t rollover
  tick(s, nearWrap);
  triggerOverride(s, Screen::LINK_EVENT, nearWrap);  // deadline = nearWrap + OLED_EVENT_DISPLAY_MS, wraps past 0

  // Shortly after the real rollover (now has wrapped to a small value),
  // but still before the override's real deadline — must NOT report expired.
  TickResult stillActive = tick(s, 50u);  // 50ms of wall-clock time after wrap began, well before 4000ms elapses
  check(stillActive.screen == Screen::LINK_EVENT, "override survives a millis() wraparound without falsely expiring early");

  // Well past the real deadline, post-wraparound.
  uint32_t deadline = nearWrap + OLED_EVENT_DISPLAY_MS;  // wraps naturally
  TickResult expired = tick(s, deadline + 10u);
  check(expired.screen == Screen::NODE_STATUS, "override correctly expires after its deadline even across a millis() wraparound");
}

// ---- 12. init() defensively clamps a zero screen count to 1 ----
void test_init_clamps_zero_count() {
  State s;
  const Screen one[] = { Screen::SENSOR_ANOMALY };
  init(s, one, 0);
  TickResult r = tick(s, 0);
  check(r.screen == Screen::SENSOR_ANOMALY, "init() with count=0 defensively falls back to a 1-screen rotation using screens[0]");
}

}  // namespace

int main() {
  test_first_tick_redraws();
  test_immediate_retick_is_rate_limited();
  test_interval_driven_redraw_without_screen_change();
  test_auto_cycle_advances();
  test_cycle_wraps_around();
  test_three_screen_cycle_order();
  test_override_switches_screen_immediately();
  test_override_suppresses_auto_cycle();
  test_override_expires_and_resumes_cycle();
  test_override_expiry_resets_cycle_timer();
  test_override_wraparound_safety();
  test_init_clamps_zero_count();

  std::printf("\n%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
