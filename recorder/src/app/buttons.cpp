#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "buttons.h"

extern void startRecordFlow();
extern void resetActivity();
static volatile bool upperPressed = false;
static void ARDUINO_ISR_ATTR captureUpperPress() { upperPressed = true; }

void beginButtonCapture() {
  attachInterrupt(digitalPinToInterrupt(BTN_REC), captureUpperPress, FALLING);
}

void clearButtonPresses() { upperPressed = false; }

bool isDown(int pin) { return digitalRead(pin) == LOW; }

ButtonEvent readButtonEvent(int pin) {
  static bool pwrReady = true;

  if (pin == BTN_PWR) {
    if (!isDown(BTN_PWR)) { pwrReady = true; return EV_NONE; }
    delay(6);
    if (!isDown(BTN_PWR)) return EV_NONE;
    if (!pwrReady) return EV_NONE;
    pwrReady = false;
    resetActivity();
    return EV_SINGLE;
  }

  if (pin == BTN_REC && upperPressed && !isDown(pin)) {
    upperPressed = false;
    resetActivity();
    return EV_SINGLE;
  }
  if (!isDown(pin)) return EV_NONE;
  if (pin == BTN_REC) upperPressed = false;
  delay(6);
  if (!isDown(pin)) return EV_NONE;

  uint32_t t0 = millis();
  while (isDown(pin)) {
    if (millis() - t0 > BTN_LONG_MS) {
      while (isDown(pin)) delay(3);
      resetActivity();
      upperPressed = false;
      return EV_LONG;
    }
    delay(3);
  }

  uint32_t rel = millis();
  while (millis() - rel < DOUBLE_MS) {
    if (isDown(pin)) {
      delay(10);
      if (isDown(pin)) {
        while (isDown(pin)) delay(3);
        resetActivity();
        upperPressed = false;
        return EV_DOUBLE;
      }
    }
    delay(3);
  }

  resetActivity();
  if (pin == BTN_REC) upperPressed = false;
  return EV_SINGLE;
}

bool handleIdleRec() {
  if (!isDown(BTN_REC)) return false;
  resetActivity();
  delay(20);
  if (!isDown(BTN_REC)) return false;

  uint32_t t0 = millis();
  while (isDown(BTN_REC) && millis()-t0 < REC_HOLD_MS) delay(5);

  if (isDown(BTN_REC)) {
    startRecordFlow();
    return true;
  }
  return true;
}
