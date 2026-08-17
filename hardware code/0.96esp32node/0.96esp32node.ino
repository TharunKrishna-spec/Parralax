#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x78// Default I2C address for 0.96" OLEDor3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int POT_PIN = 34; // Channel A
const int LDR_PIN = 35; // Channel B

void setup() {
  Serial.begin(115200);

  // Initialize I2C OLED (SDA=GPIO21, SCL=GPIO22)
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("OLED Allocation Failed! Check SDA/SCL wiring or 0x3C address."));
    for (;;); // Pause program if display fails
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
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