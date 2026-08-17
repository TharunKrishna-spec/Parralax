#include "oled.h"
#include "oled_core.h"
#include "../config.h"
#include "../core/node_id.h"
#include "../core/logger.h"
#include "../predictor/predictor.h"
#include "../anomaly/anomaly.h"

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SH110X.h>

namespace {

// Both concrete driver objects are constructed unconditionally (cheap —
// neither allocates its I2C framebuffer until begin() actually runs, and
// only one of the two ever has begin() called on a given board, per
// THIS_NODE_ID). This keeps the "one shared source tree, only THIS_NODE_ID
// differs per board" convention (docs/hardware-readiness.md Part C) intact
// instead of adding a second per-node compile-time selector — the driver
// choice is a runtime comparison against THIS_NODE_ID, the same pattern
// src/apptraffic/apptraffic.cpp already uses for its own single-node gate.
Adafruit_SSD1306 g_ssd1306(128, 64, &Wire, -1);
Adafruit_SH1106G g_sh1106(128, 64, &Wire, -1);
Adafruit_GFX* g_display = nullptr;  // drawing only (setCursor/print/etc. - real Adafruit_GFX base members)

// clearDisplay()/display() are NOT Adafruit_GFX base members (each driver
// declares them separately further down its own hierarchy), so the
// framebuffer push has to go through whichever concrete object is active,
// tracked here rather than through g_display.
enum class Driver : uint8_t { NONE, SSD1306, SH1106 };
Driver g_driver = Driver::NONE;

void pushClear() {
  if (g_driver == Driver::SSD1306) g_ssd1306.clearDisplay();
  else if (g_driver == Driver::SH1106) g_sh1106.clearDisplay();
}

void pushFrame() {
  if (g_driver == Driver::SSD1306) g_ssd1306.display();
  else if (g_driver == Driver::SH1106) g_sh1106.display();
}

bool g_active = false;  // true only once a real display.begin() has succeeded
oled_core::State g_state;
const char* g_roleHint = "";

// Text color is passed as the literal foreground pixel value (1) rather
// than either library's own SSD1306_WHITE/SH110X_WHITE macro — both
// headers are included in this file, and using the shared literal avoids
// depending on either macro's name (they're equal, but that's incidental,
// not guaranteed API).
const uint16_t OLED_FG = 1;

// ---- Node S: per-direct-neighbor link quality + event edge-detection ----
// S never calls routing::selectNextHop()/getNextHop() for destination==S
// (it is always the final destination in this project's one real traffic
// flow, never a relay for itself), so it has no "current route" of its own
// to observe via routing::RouteEvent. What it DOES have, honestly: real
// predictor::linkState() for each of ITS OWN direct neighbors (B, D, A —
// core/node_id.h::neighborsOf(NODE_S)), which is exactly the "current best
// path" signal implementation-guide.html's checklist asks Node S's OLED to
// show. Read directly here (predictor::linkState()/isUnhealthy() are
// documented side-effect-free reads), never through an event-emitting
// query.
NodeId g_sNeighbors[NODE_ID_COUNT];
uint8_t g_sNeighborCount = 0;
bool g_sNeighborEverChecked[NODE_ID_COUNT] = { false, false, false, false, false };
bool g_sNeighborWasUnhealthy[NODE_ID_COUNT] = { false, false, false, false, false };

NodeId g_eventNeighbor = NODE_ID_UNKNOWN;
bool g_eventBecameUnhealthy = false;
float g_eventScore = 0.0f;

void drawNodeStatus() {
  g_display->setCursor(0, 0);
  g_display->print("NODE ");
  g_display->println(thisNode().name);
  g_display->print("ROLE: ");
  g_display->println(roleName(thisNode().role));
  g_display->println(g_roleHint);
}

// Node C: implementation-guide.html "local anomaly flag ... SPIKE/JUMP vs
// STUCK, shown independently" - SPIKE/JUMP maps directly to
// anomaly_core::SensorState::ANOMALY (the modified-Z detector's own
// state), STUCK maps directly to SensorState::FLATLINE (the independent
// flatline/stuck detector) - anomaly_core.h's own docstring already
// requires these stay two independent detectors, never merged into one
// score, so this display just surfaces that existing separation, not a
// new derivation.
void drawSensorAnomaly() {
  anomaly_core::SensorTelemetry pot = anomaly::getTelemetry(anomaly::SensorId::POT);
  anomaly_core::SensorTelemetry ldr = anomaly::getTelemetry(anomaly::SensorId::LDR);

  g_display->setCursor(0, 0);
  g_display->println("ANOMALY FLAGS");
  g_display->print("POT SPIKE: ");
  g_display->println(pot.state == anomaly_core::SensorState::ANOMALY ? "Y" : "N");
  g_display->print("POT STUCK: ");
  g_display->println(pot.state == anomaly_core::SensorState::FLATLINE ? "Y" : "N");
  g_display->print("LDR SPIKE: ");
  g_display->println(ldr.state == anomaly_core::SensorState::ANOMALY ? "Y" : "N");
  g_display->print("LDR STUCK: ");
  g_display->println(ldr.state == anomaly_core::SensorState::FLATLINE ? "Y" : "N");
}

// Node S: live link_score for each direct neighbor, best one marked - the
// checklist's "show live link_score for the current best path".
void drawLinkQuality() {
  g_display->setCursor(0, 0);
  g_display->println("LINK SCORES");

  uint8_t bestIdx = 0;
  float bestScore = -1.0f;
  for (uint8_t i = 0; i < g_sNeighborCount; i++) {
    float score = predictor::linkScore(g_sNeighbors[i]);
    if (score > bestScore) {
      bestScore = score;
      bestIdx = i;
    }
  }

  for (uint8_t i = 0; i < g_sNeighborCount; i++) {
    NodeId n = g_sNeighbors[i];
    g_display->print(i == bestIdx ? "*" : " ");
    g_display->print(nodeName(n));
    g_display->print(": ");
    g_display->println(predictor::linkScore(n), 2);
  }
}

// Node S: temporary override, shown only on a real HEALTHY<->UNHEALTHY
// transition for one of S's own direct neighbors - the checklist's
// "reroute events" (S can't see the far side's routing decision directly,
// but a direct neighbor's own link health flipping is the real, locally-
// observed signal that correlates with one).
void drawLinkEvent() {
  g_display->setCursor(0, 0);
  g_display->println("LINK EVENT");
  g_display->print(g_eventNeighbor == NODE_ID_UNKNOWN ? "?" : nodeName(g_eventNeighbor));
  g_display->println(g_eventBecameUnhealthy ? " UNHEALTHY" : " RECOVERED");
  g_display->print("SCORE: ");
  g_display->println(g_eventScore, 2);
}

void drawCurrentScreen(oled_core::Screen screen) {
  pushClear();
  g_display->setTextSize(1);
  g_display->setTextColor(OLED_FG);

  switch (screen) {
    case oled_core::Screen::NODE_STATUS:   drawNodeStatus(); break;
    case oled_core::Screen::SENSOR_ANOMALY: drawSensorAnomaly(); break;
    case oled_core::Screen::LINK_QUALITY:  drawLinkQuality(); break;
    case oled_core::Screen::LINK_EVENT:    drawLinkEvent(); break;
  }

  pushFrame();
}

// Node S only: cheap, side-effect-free edge-detection over predictor's own
// per-neighbor health state - no event callback wiring needed (avoids
// touching main.cpp's existing single-subscriber predictor::setEventCallback()
// slot, which telemetry already owns).
void tickLinkEvents(uint32_t now) {
  for (uint8_t i = 0; i < g_sNeighborCount; i++) {
    NodeId n = g_sNeighbors[i];
    bool unhealthyNow = predictor::isUnhealthy(n);
    if (g_sNeighborEverChecked[n] && unhealthyNow != g_sNeighborWasUnhealthy[n]) {
      g_eventNeighbor = n;
      g_eventBecameUnhealthy = unhealthyNow;
      g_eventScore = predictor::linkScore(n);
      oled_core::triggerOverride(g_state, oled_core::Screen::LINK_EVENT, now);
    }
    g_sNeighborWasUnhealthy[n] = unhealthyNow;
    g_sNeighborEverChecked[n] = true;
  }
}

}  // namespace

namespace oled {

void init() {
  if (!thisNode().hasOled) return;

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);

  bool ok = false;
  if (THIS_NODE_ID == NODE_S) {
    g_roleHint = "MESH TELEMETRY";
    ok = g_ssd1306.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS);
    g_display = &g_ssd1306;
    g_driver = Driver::SSD1306;

    const NodeId* neighbors = neighborsOf(NODE_S, g_sNeighborCount);
    for (uint8_t i = 0; i < g_sNeighborCount; i++) g_sNeighbors[i] = neighbors[i];

    static const oled_core::Screen kScreensS[] = { oled_core::Screen::NODE_STATUS, oled_core::Screen::LINK_QUALITY };
    oled_core::init(g_state, kScreensS, 2);
  } else if (THIS_NODE_ID == NODE_C) {
    g_roleHint = "ANOMALY FLAG";
    ok = g_sh1106.begin(OLED_I2C_ADDRESS, true);
    g_display = &g_sh1106;
    g_driver = Driver::SH1106;

    static const oled_core::Screen kScreensC[] = { oled_core::Screen::NODE_STATUS, oled_core::Screen::SENSOR_ANOMALY };
    oled_core::init(g_state, kScreensC, 2);
  }

  if (!ok || g_display == nullptr) {
    logger::warn("[OLED] display init failed (node=%s) - continuing without local display, see docs/hardware-readiness.md",
                  thisNode().name);
    g_display = nullptr;
    g_driver = Driver::NONE;
    g_active = false;
    return;
  }

  pushClear();
  pushFrame();
  g_active = true;
  logger::info("[OLED] init ok (node=%s)", thisNode().name);
}

void tick() {
  if (!g_active) return;

  uint32_t now = millis();
  if (THIS_NODE_ID == NODE_S) tickLinkEvents(now);

  oled_core::TickResult r = oled_core::tick(g_state, now);
  if (r.shouldRedraw) drawCurrentScreen(r.screen);
}

}  // namespace oled
