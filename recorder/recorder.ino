#include "Arduino.h"
#include "src/storage/sd_card.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include "WiFiClientSecure.h"
#include <WebServer.h>
#include <vector>
#include "driver/i2c_master.h"
#include "esp_heap_caps.h"
#include <ArduinoJson.h>
#include <esp_partition.h>
#include <algorithm>

extern "C" {
#include "config.h"
#include "src/i2c_bsp/i2c_bsp.h"
#include "src/audio/audio_bsp.h"
}

#include "src/power/board_power_bsp.h"
#include "src/display/epaper_driver_bsp.h"
#include "sounds.h"

#include <Adafruit_GFX.h>
#include <pgmspace.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>

#include "types.h"
#include "globals.h"
#include "src/app/draw.h"
#include "src/app/battery.h"
#include "src/app/rtc.h"
#include "src/app/notes.h"
#include "src/app/ui.h"
#include "src/app/buttons.h"
#include "src/app/network.h"
#include "src/app/sleep.h"
#include "src/app/record.h"
#include "src/app/settings.h"

// All pin, timing, path and threshold constants live in config.h.

// ─── Content arrays ───────────────────────────────────────────────────────
const char* DEFAULT_TAGS[]    = { "Interview", "Tanker" };
const char* MENU_ITEMS[]     = { "Noter", "Tags", "Sync", "Indstillinger" };
const char* SETTINGS_ITEMS[] = { "Lyde", "Portal", "Enhed" };

// ─── Global variable definitions ─────────────────────────────────────────
board_power_bsp_t      board(EPD_PWR_PIN, Audio_PWR_PIN, VBAT_PWR_PIN);
epaper_driver_display* display = nullptr;

std::vector<NoteEntry> noteIndex;

AppState state          = STATE_IDLE;
int      listCursor     = 0;
int      tagCursor      = 2;
int      menuCursor     = 0;
int      settingsCursor = 0;
bool     soundsOn       = true;
int      activeFilter   = -1;
int      lastRecNum     = -1;

uint32_t lastActivityMs      = 0;
bool     wokeFromUltraSleep  = false;
bool     wakeToMenuRequested = false;
bool     wakeToRecRequested  = false;

uint32_t tickerLastMs = 0;
int      tickerOffset = 0;
int      tickerCursor = -1;

WebServer transferServer(80);
bool      transferServerActive = false;
String    transferUrl          = "";

bool timeReady    = false;
bool audioPlaying = false;
bool stopPlayback = false;

int detailScrollPage = 0;
int detailTotalLines = 0;

uint32_t lastBatCheckMs    = 0;
bool     batLowWarned      = false;
bool     batWarnActive     = false;
uint32_t batWarnShowUntilMs = 0;

char tags[20][32];
int  tagCount = 0;

struct FlashSetupHeader {
  char magic[8];
  uint32_t jsonLength;
  uint32_t checksum;
};

uint32_t setupChecksum(const uint8_t* data, size_t length) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < length; i++) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return hash;
}

bool importFlashSetup() {
  const esp_partition_t* partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "setupcfg");
  if (!partition) return false;
  FlashSetupHeader header{};
  if (esp_partition_read(partition, 0, &header, sizeof(header)) != ESP_OK ||
      memcmp(header.magic, "SETUPCF1", 8) != 0 || header.jsonLength == 0 ||
      header.jsonLength > 700) return false;
  std::vector<uint8_t> bytes(header.jsonLength);
  if (esp_partition_read(partition, sizeof(header), bytes.data(), bytes.size()) != ESP_OK ||
      setupChecksum(bytes.data(), bytes.size()) != header.checksum) {
    std::fill(bytes.begin(), bytes.end(), 0);
    esp_partition_erase_range(partition, 0, partition->size);
    return false;
  }
  JsonDocument doc;
  const bool parsed = !deserializeJson(doc, bytes.data(), bytes.size());
  const String ssid = parsed ? String(doc["ssid"] | "") : String();
  const String password = parsed ? String(doc["password"] | "") : String();
  const String apiKey = parsed ? String(doc["apiKey"] | "") : String();
  const String language = parsed ? String(doc["language"] | "en") : String();
  std::fill(bytes.begin(), bytes.end(), 0);
  const bool saved = parsed && classicSaveSettings(ssid, password, apiKey, language);
  esp_partition_erase_range(partition, 0, partition->size);
  return saved;
}

// ─── Power latch ──────────────────────────────────────────────────────────
void keepBatteryPowerOn() {
  board.VBAT_POWER_ON();
}

// ─── Flow functions ───────────────────────────────────────────────────────
void startRecordFlow() {
  state = STATE_RECORDING;

  soundSetEnabled(false);
  bool recOk = record();
  soundSetEnabled(true);

  if (!recOk) {
    showError("OPTAGEFEJL");
    delay(1600);
    state = STATE_IDLE;
    showIdle();
    return;
  }

  // A recording is durable immediately. Tag selection is optional metadata;
  // a reset here must never leave an invisible WAV behind.
  if (!addToIndex(lastRecNum, "Untagged", false) || !writeNoteMeta(lastRecNum, "Untagged")) {
    showError("SD-FEJL");
    delay(1600);
    state = STATE_IDLE;
    showIdle();
    return;
  }

  soundSaved();

  state = STATE_SAVED;
  showSaved(lastRecNum);
  delay(900);

  tagCursor = min(2, max(tagCount - 1, 0));
  state = STATE_TAG_SELECT;
  resetActivity();
  clearButtonPresses();
  showTagSelect(tagCursor);
}

void startSyncFlow() {
  resetActivity();
  const int MAX_TRIES = 20;
  showWifiConnecting(0, MAX_TRIES);

  WiFi.mode(WIFI_STA);
  WiFi.begin(classicWifiSsid().c_str(), classicWifiPassword().c_str());
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < MAX_TRIES) {
    delay(500); tries++;
    showWifiConnecting(tries, MAX_TRIES);
  }

  if (WiFi.status() == WL_CONNECTED) {
    syncTimeFromNTP(6000);
    const bool synced = transcribeAll();
    loadIndex();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    if (synced) { showDone(); soundSuccess(); }
    else showSyncPending();
    delay(1600);
  } else {
    showError("INGEN WIFI");
    delay(1800);
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  resetActivity();
  clearButtonPresses();
  state = STATE_MENU;
  showMenu(menuCursor);
}

void startTransferMode() {
  state = STATE_TRANSFER;
  showTransferConnecting();

  WiFi.mode(WIFI_STA);
  WiFi.begin(classicWifiSsid().c_str(), classicWifiPassword().c_str());

  const int MAX_TRIES = 24;
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < MAX_TRIES) {
    delay(500); tries++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    showError("INGEN WIFI");
    delay(1600);
    resetActivity();
    clearButtonPresses();
    state = STATE_SETTINGS;
    showSettings(settingsCursor);
    return;
  }

  // Modem sleep makes the portal miss first ARP/mDNS requests from browsers.
  WiFi.setSleep(false);
  syncTimeFromNTP(8000);
  // Transfer is for accessing the portal, not an implicit blocking Sync.
  loadIndex();
  setupTransferServer();
  transferServer.begin();
  transferServerActive = true;

  IPAddress ip = WiFi.localIP();
  transferUrl = ip.toString();
  resetActivity();
  clearButtonPresses();
  showTransferMode("recorder.local", transferUrl.c_str());
}

// ─── Setup ────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Voice Recorder " FIRMWARE_VERSION " ===");

  releasePeripheralPowerHolds();
  classicSettingsBegin();
  importFlashSetup();

  pinMode(BTN_REC, INPUT_PULLUP);
  beginButtonCapture();
  pinMode(BTN_PWR, INPUT_PULLUP);

  // Power rails sit behind the I2C expander, so the bus comes up first.
  i2c_master_Init();
  board.VBAT_POWER_ON();

  wokeFromUltraSleep  = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1);
  delay(50);

  // The wake press is always consumed: PWR wakes to the idle screen first.
  wakeToMenuRequested = false;
  wakeToRecRequested  = false;

  resetActivity();
  keepBatteryPowerOn();
  delay(20);

  board.POWEER_EPD_ON();
  board.POWEER_Audio_ON();
  delay(200);

  // The TF card shares the panel's SPI bus; keep it deselected until mounted.
  gpio_reset_pin(SD_CS);
  gpio_set_direction(SD_CS, GPIO_MODE_OUTPUT);
  gpio_set_level(SD_CS, 1);

  custom_lcd_spi_t dispCfg = {};
  dispCfg.cs       = EPD_CS_PIN;
  dispCfg.dc       = EPD_DC_PIN;
  dispCfg.rst      = EPD_RST_PIN;
  dispCfg.busy     = EPD_BUSY_PIN;
  dispCfg.mosi     = EPD_MOSI_PIN;
  dispCfg.miso     = EPD_MISO_PIN;
  dispCfg.scl      = EPD_SCK_PIN;
  dispCfg.spi_host = EPD_SPI_NUM;
  dispCfg.buffer_len = (200*200)/8;

  display = new epaper_driver_display(200, 200, dispCfg);
  display->EPD_Init();
  display->EPD_Clear();
  display->EPD_DisplayPartBaseImage();
  display->EPD_Init_Partial();

  rtcSyncSystemFromChip();
  Serial.printf("[RTC] %s\n", timeReady ? rtcUtcIso().c_str() : "time not set");
  delay(50);

  audio_bsp_init();
  audio_play_init();

  if (!SDCard.begin(EPD_SPI_NUM, SD_CS)) {
    showError("SD-FEJL");
    enterErrorSleep();
  }
  if (!SDCard.exists(NOTES_DIR)) SDCard.mkdir(NOTES_DIR);
  loadTags();
  loadIndex();
  recoverOrphanNotes();
  Serial.printf("[SD] %d notes\n", (int)noteIndex.size());

  if (!classicHasSetup()) {
    showError("SETUP MANGLER");
    enterErrorSleep();
  }

  if (wakeToMenuRequested) {
    menuCursor = 0;
    state = STATE_MENU;
    showMenu(menuCursor);
  } else if (wakeToRecRequested) {
    startRecordFlow();
  } else {
    showIdle();
  }
}

// ─── Main loop ────────────────────────────────────────────────────────────
void loop() {

  if (state != STATE_RECORDING && !audioPlaying) audio_idle_suspend();

  if (state != STATE_RECORDING) {
    // Portal requests and button presses count as activity in Transfer mode.
    const uint32_t idleLimit = state == STATE_TRANSFER ? TRANSFER_IDLE_MS : ULTRA_SLEEP_MS;
    if (millis() - lastActivityMs > idleLimit) {
      enterUltraSleep();
      return;
    }
  }

  if (state == STATE_NOTE_LIST && activeTickerNeedsScroll(listCursor)) {
    if (millis() - tickerLastMs > TICKER_INTERVAL_MS) {
      tickerLastMs = millis();
      tickerOffset++;
      showNoteList(listCursor);
      return;
    }
  }

  if (transferServerActive) transferServer.handleClient();

  // Battery warning: dismiss after 2.5 s without blocking
  if (batWarnActive && millis() >= batWarnShowUntilMs) {
    batWarnActive = false;
    switch (state) {
      case STATE_IDLE:           showIdle();                     break;
      case STATE_MENU:           showMenu(menuCursor);           break;
      case STATE_NOTE_LIST:      showNoteList(listCursor);       break;
      case STATE_NOTE_DETAIL:    showNoteDetail(listCursor);     break;
      case STATE_TAG_SELECT:     showTagSelect(tagCursor);       break;
      case STATE_TAG_BROWSER:    showTagBrowser(tagCursor);      break;
      case STATE_SETTINGS:       showSettings(settingsCursor);   break;
      case STATE_DEVICE_INFO:    showDeviceInfo();               break;
      case STATE_DELETE_CONFIRM: {
        int idx = noteAtFilteredIndex(listCursor);
        if (idx >= 0) showDeleteConfirm(noteIndex[idx].num);
        break;
      }
      default: break;
    }
  }

  // Periodic battery check
  if (state != STATE_RECORDING && !audioPlaying && !batWarnActive) {
    if (millis() - lastBatCheckMs > BAT_CHECK_INTERVAL_MS) {
      lastBatCheckMs = millis();
      int pct = readBatteryPercent();
      if (pct >= 0 && pct <= BAT_LOW_THRESHOLD && !batLowWarned) {
        batLowWarned        = true;
        batWarnActive       = true;
        batWarnShowUntilMs  = millis() + 2500;
        showBatteryLow(pct);
      } else if (pct > BAT_RECOVER_THRESHOLD) {
        batLowWarned = false;
      }
    }
  }

  // IDLE ─────────────────────────────────────────────────────────────────
  if (state == STATE_IDLE) {
    if (handleIdleRec()) return;

    ButtonEvent pwr = readButtonEvent(BTN_PWR);
    if (pwr == EV_SINGLE || pwr == EV_LONG) {
      soundSelect();
      menuCursor = 0;
      state = STATE_MENU;
      showMenu(menuCursor);
    }
  }

  // TAG SELECT after recording ──────────────────────────────────────────
  else if (state == STATE_TAG_SELECT) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (rec == EV_SINGLE || rec == EV_LONG || rec == EV_DOUBLE) {
      soundSelect();
      if (!saveTag(lastRecNum, tags[constrain(tagCursor, 0, max(tagCount - 1, 0))])) {
        showError("SD-FEJL"); delay(1600); showTagSelect(tagCursor); return;
      }
      state = STATE_IDLE;
      resetActivity();
      clearButtonPresses();
      showIdle();
    } else if (pwr == EV_SINGLE) {
      soundNext();
      if (tagCount > 0) tagCursor = (tagCursor + 1) % tagCount;
      showTagSelect(tagCursor);
    }
  }

  // MENU ────────────────────────────────────────────────────────────────
  else if (state == STATE_MENU) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (pwr == EV_SINGLE) {
      soundNext();
      menuCursor = (menuCursor + 1) % MENU_COUNT;
      showMenu(menuCursor);
    } else if (rec == EV_SINGLE) {
      soundSelect();
      if (menuCursor == 0) {
        activeFilter = -1; listCursor = 0;
        state = STATE_NOTE_LIST;
        showNoteList(listCursor);
      } else if (menuCursor == 1) {
        tagCursor = 0;
        state = STATE_TAG_BROWSER;
        showTagBrowser(tagCursor);
      } else if (menuCursor == 2) {
        startSyncFlow();
      } else {
        settingsCursor = 0;
        state = STATE_SETTINGS;
        showSettings(settingsCursor);
      }
    } else if (rec == EV_LONG || rec == EV_DOUBLE) {
      soundBack();
      state = STATE_IDLE;
      showIdle();
    }
  }

  // SETTINGS ────────────────────────────────────────────────────────────
  else if (state == STATE_SETTINGS) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (pwr == EV_SINGLE) {
      soundNext();
      settingsCursor = (settingsCursor + 1) % SETTINGS_COUNT;
      showSettings(settingsCursor);
    } else if (rec == EV_SINGLE) {
      soundSelect();
      if (settingsCursor == 0) {
        soundSetEnabled(!soundIsEnabled());
        showSettings(settingsCursor);
      } else if (settingsCursor == 1) {
        startTransferMode();
      } else {
        state = STATE_DEVICE_INFO;
        showDeviceInfo();
      }
    } else if (rec == EV_DOUBLE || rec == EV_LONG) {
      soundBack();
      state = STATE_MENU;
      showMenu(menuCursor);
    }
  }

  // DEVICE INFO ─────────────────────────────────────────────────────────
  else if (state == STATE_DEVICE_INFO) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (rec == EV_DOUBLE || rec == EV_LONG || rec == EV_SINGLE || pwr == EV_SINGLE) {
      soundBack();
      state = STATE_SETTINGS;
      showSettings(settingsCursor);
    }
  }

  // TRANSFER MODE ───────────────────────────────────────────────────────
  else if (state == STATE_TRANSFER) {
    if (transferServerActive) transferServer.handleClient();
    ButtonEvent rec = readButtonEvent(BTN_REC);
    if (rec == EV_DOUBLE || rec == EV_LONG) {
      soundBack();
      stopTransferMode();
      state = STATE_SETTINGS;
      showSettings(settingsCursor);
    }
  }

  // TAG BROWSER ─────────────────────────────────────────────────────────
  else if (state == STATE_TAG_BROWSER) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (pwr == EV_SINGLE) {
      soundNext();
      if (tagCount > 0) tagCursor = (tagCursor + 1) % tagCount;
      showTagBrowser(tagCursor);
    } else if (rec == EV_SINGLE) {
      soundSelect();
      activeFilter = tagCursor; listCursor = 0;
      state = STATE_NOTE_LIST;
      showNoteList(listCursor);
    } else if (rec == EV_LONG || rec == EV_DOUBLE) {
      soundBack();
      state = STATE_MENU;
      showMenu(menuCursor);
    }
  }

  // NOTE LIST ───────────────────────────────────────────────────────────
  else if (state == STATE_NOTE_LIST) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);
    int count = filteredCount();

    if (pwr == EV_SINGLE && count > 0) {
      soundNext();
      listCursor = (listCursor + 1) % count;
      showNoteList(listCursor);
    } else if (pwr == EV_DOUBLE && count > 0) {
      soundNext();
      listCursor = (listCursor - 1 + count) % count;
      showNoteList(listCursor);
    } else if (rec == EV_SINGLE && count > 0) {
      soundSelect();
      detailScrollPage = 0;
      state = STATE_NOTE_DETAIL;
      showNoteDetail(listCursor);
    } else if (rec == EV_LONG || rec == EV_DOUBLE) {
      soundBack();
      state = STATE_MENU;
      showMenu(menuCursor);
    }
  }

  // NOTE DETAIL ─────────────────────────────────────────────────────────
  else if (state == STATE_NOTE_DETAIL) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (pwr == EV_SINGLE) {
      soundNext();
      const int linesPerPage = 7;
      int totalPages = (detailTotalLines + linesPerPage - 1) / linesPerPage;
      if (detailScrollPage + 1 < totalPages) {
        detailScrollPage++;
      } else {
        detailScrollPage = 0;
        int count = filteredCount();
        if (count > 0) listCursor = (listCursor + 1) % count;
      }
      showNoteDetail(listCursor);
    } else if (rec == EV_SINGLE) {
      int idx = noteAtFilteredIndex(listCursor);
      if (idx >= 0) {
        playRecording(noteIndex[idx].num);
        showNoteDetail(listCursor);
      }
    } else if (rec == EV_LONG) {
      int idx = noteAtFilteredIndex(listCursor);
      if (idx >= 0) {
        state = STATE_DELETE_CONFIRM;
        showDeleteConfirm(noteIndex[idx].num);
      }
    } else if (rec == EV_DOUBLE) {
      soundBack();
      detailScrollPage = 0;
      state = STATE_NOTE_LIST;
      showNoteList(listCursor);
    }
  }

  // DELETE CONFIRM ──────────────────────────────────────────────────────
  else if (state == STATE_DELETE_CONFIRM) {
    ButtonEvent rec = readButtonEvent(BTN_REC);
    ButtonEvent pwr = readButtonEvent(BTN_PWR);

    if (rec == EV_SINGLE) {
      int idx = noteAtFilteredIndex(listCursor);
      if (idx >= 0) {
        deleteNote(noteIndex[idx].num);
        soundDelete();
      }
      detailScrollPage = 0;
      listCursor = constrain(listCursor, 0, max(filteredCount() - 1, 0));
      state = STATE_NOTE_LIST;
      showNoteList(listCursor);
    } else if (pwr == EV_SINGLE || rec == EV_DOUBLE || rec == EV_LONG) {
      soundBack();
      state = STATE_NOTE_DETAIL;
      showNoteDetail(listCursor);
    }
  }

  delay(15);
}
