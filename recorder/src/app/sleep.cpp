#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "sleep.h"
#include "ui.h"
#include "network.h"
#include "../../sounds.h"
#include "WiFi.h"
#include "../storage/sd_card.h"
#include "driver/gpio.h"

extern "C" {
#include "../../src/audio/audio_bsp.h"
}

static void powerDownAndSleep();

void resetActivity() {
  lastActivityMs = millis();
}

void releasePeripheralPowerHolds() {
  // Power rails are TCA9554 outputs, which keep their state through deep
  // sleep without GPIO holds, so there is nothing to release on this board.
}

void enterUltraSleep() {
  // EXT1 wakes on a low level: a held PWR key would immediately reboot.
  if (digitalRead(BTN_PWR) == LOW) { resetActivity(); return; }
  delay(50);
  if (digitalRead(BTN_PWR) == LOW) { resetActivity(); return; }
  const esp_err_t wakeResult = esp_sleep_enable_ext1_wakeup(
      (1ULL << BTN_PWR), ESP_EXT1_WAKEUP_ANY_LOW);
  if (wakeResult != ESP_OK) { resetActivity(); return; }
  // A sleeping tag/menu screen looked like a frozen UI. Retain a clearly
  // locked Home instead, before powering the panel down.
  showIdle(true);
  if (digitalRead(BTN_PWR) == LOW) {
    resetActivity();
    showIdle();
    return;
  }
  powerDownAndSleep();
}

void enterErrorSleep() {
  // Keep the error on the panel but stop draining the battery. A PWR press
  // retries at once; after ERROR_SLEEP_AFTER_MS the device sleeps until one.
  const uint32_t started = millis();
  while (millis() - started < ERROR_SLEEP_AFTER_MS) {
    if (digitalRead(BTN_PWR) == LOW) { delay(30); if (digitalRead(BTN_PWR) == LOW) esp_restart(); }
    delay(20);
  }
  if (esp_sleep_enable_ext1_wakeup((1ULL << BTN_PWR), ESP_EXT1_WAKEUP_ANY_LOW) != ESP_OK) {
    while (true) delay(1000);
  }
  powerDownAndSleep();
}

static void powerDownAndSleep() {
  stopTransferMode();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  audio_playback_set_vol(0);
  soundSetEnabled(false);
  audio_prepare_sleep();

  SDCard.end();

  board.POWEER_Audio_OFF();
  board.POWEER_EPD_OFF();

  board.VBAT_POWER_ON();

  esp_deep_sleep_start();
}
