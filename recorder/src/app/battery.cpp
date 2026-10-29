#include "Arduino.h"
#include "../../globals.h"
#include "../../types.h"
#include "battery.h"
#include "draw.h"
#include "../../config.h"
#include <math.h>

static bool adcReady = false;

void batteryInit() {
  if (adcReady) return;
  // The first read attaches the pin to the ADC; attenuation can only be set after.
  analogReadMilliVolts(BAT_ADC_PIN);
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
  adcReady = true;
}

float readBatteryVoltage() {
  if (!adcReady) batteryInit();
  const int samples = 16;
  uint32_t sum = 0;
  for (int i = 0; i < samples; i++) { sum += analogReadMilliVolts(BAT_ADC_PIN); delay(2); }
  float mv = (float)sum / (float)samples;
  return (mv / 1000.0f) * 2.0f;
}

// Short read for the recording loop: no delays, so the I2S stream keeps up.
float readBatteryVoltageFast() {
  if (!adcReady) batteryInit();
  const int samples = 4;
  uint32_t sum = 0;
  for (int i = 0; i < samples; i++) sum += analogReadMilliVolts(BAT_ADC_PIN);
  float mv = (float)sum / (float)samples;
  return (mv / 1000.0f) * 2.0f;
}

// Typical single-cell LiPo discharge curve. Most of the capacity sits between
// 3.7 and 3.9 V, and the voltage falls off quickly below 3.7 V.
int batteryPercentFromVoltage(float v) {
  const float volts[] = {3.30f, 3.50f, 3.65f, 3.70f, 3.73f, 3.77f, 3.79f, 3.82f, 3.87f, 3.92f, 4.00f, 4.10f, 4.20f};
  const int   pct[]   = {0,     5,     10,    15,    20,    30,    40,    50,    60,    70,    80,    90,    100};
  const int   n = sizeof(pct) / sizeof(pct[0]);
  if (v <= volts[0]) return 0;
  if (v >= volts[n-1]) return 100;
  for (int i = 1; i < n; i++) {
    if (v <= volts[i]) {
      float t = (v - volts[i-1]) / (volts[i] - volts[i-1]);
      int p = pct[i-1] + (int)((pct[i] - pct[i-1]) * t + 0.5f);
      p = ((p + 2) / 5) * 5;
      return constrain(p, 0, 100);
    }
  }
  return 100;
}

int readBatteryPercent() {
  float v = readBatteryVoltage();
  if (v <= 0.1f) return -1;
  return batteryPercentFromVoltage(v);
}

void drawThickArcDot(int cx, int cy, int r, int deg, int thickness, uint8_t color) {
  float a = ((float)deg - 90.0f) * PI / 180.0f;
  int x = cx + (int)roundf(cosf(a) * r);
  int y = cy + (int)roundf(sinf(a) * r);
  if (thickness <= 1) px(x, y, color);
  else fillCircle(x, y, thickness / 2, color);
}

void drawBatteryRing(int percent) {
  const int cx = 100, cy = 100, r = 82;
  strokeCircle(cx, cy, r, 1, BLACK);
  if (percent < 0) return;
  percent = constrain(percent, 0, 100);
  int endDeg = (360 * percent) / 100;
  for (int deg = 0; deg <= endDeg; deg += 2)
    drawThickArcDot(cx, cy, r, deg, 3, BLACK);
}
