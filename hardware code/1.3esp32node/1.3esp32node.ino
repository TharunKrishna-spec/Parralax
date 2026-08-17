#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h> // Changed library for SH1106 1.3" displays

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// 0x78 printed on the PCB translates to 7-bit 0x3C in Arduino
#define SCREEN_ADDRESS 0x3C 

// Instantiate for SH1106G driver
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int POT_PIN = 34; // Channel A
const int LDR_PIN = 35; // Channel B

void setup() {
  Serial.begin(115200);

  // Initialize SH1106 display (Address, enable internal charge pump)
  if (!display.begin(SCREEN_ADDRESS, true)) {
    Serial.println(F("OLED Allocation Failed! Check SDA/SCL wiring or address."));
    for (;;); // Pause program if display fails
  }

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE); // Changed from SSD1306_WHITE
  display.setTextSize(1);
  display.setCursor(15, 25);
  display.println("NODE INITIALIZING");
  display.display();
  delay(1000);
}

void loop() {
  // Read Analog Sensors (0 to 4095)
  int potVal = analogRead(POT_PIN);
  int ldrVal = analogRead(LDR_PIN);

  // 1. Output to Serial Monitor
  Serial.print("POT (GPIO34): ");
  Serial.print(potVal);
  Serial.print("  |  LDR (GPIO35): ");
  Serial.println(ldrVal);

  // 2. Output to OLED Display
  display.clearDisplay();
  
  // Header
  display.setTextSize(1);
  display.setCursor(20, 0);
  display.println("== NODE TEST ==");
  
  // Channel A
  display.setCursor(0, 24);
  display.print("Ch A (POT): ");
  display.print(potVal);

  // Channel B
  display.setCursor(0, 44);
  display.print("Ch B (LDR): ");
  display.print(ldrVal);

  display.display();

  delay(250); // Refresh rate
}