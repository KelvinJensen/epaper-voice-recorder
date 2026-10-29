#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "record.h"
#include "../storage/sd_card.h"
#include "esp_heap_caps.h"
#include "notes.h"
#include "../../sounds.h"
#include "ui.h"
#include "sleep.h"
#include "wav_payload.h"
#include "battery.h"
#include "buttons.h"
#include <atomic>

extern "C" {
#include "../../src/audio/audio_bsp.h"
}

namespace {
std::atomic<bool> recordingUiBusy{false};
void recordingUiTask(void*) {
  showRecording();
  recordingUiBusy = false;
  vTaskDelete(nullptr);
}
bool startRecordingUi() {
  if (recordingUiBusy.exchange(true)) return false;
  if (xTaskCreatePinnedToCore(recordingUiTask, "recording-ui", 4096, nullptr, 1, nullptr, 0) == pdPASS) return true;
  recordingUiBusy = false;
  return false;
}
void waitForRecordingUi() { while (recordingUiBusy.load()) delay(1); }
}

static void recordingPath(char* path, size_t size, int noteNum, int part) {
  if (part <= 1) snprintf(path, size, "%s/note_%03d.wav", NOTES_DIR, noteNum);
  else snprintf(path, size, "%s/note_%03d_p%02d.wav", NOTES_DIR, noteNum, part);
}

static bool writeWavHeader(File& file, uint32_t dataBytes) {
  const uint32_t resumeAt = file.position();
  if (!file.seek(0)) return false;
  const uint32_t fileSize = dataBytes + 36;
  const uint32_t byteRate = SAMPLE_RATE * 2;
  const uint32_t fmtLength = 16;
  const uint32_t sampleRate = SAMPLE_RATE;
  const uint16_t blockAlign = 2, audioFormat = 1, channels = 1, bits = 16;
  bool ok = file.write((uint8_t*)"RIFF", 4) == 4;
  ok &= file.write((uint8_t*)&fileSize, 4) == 4;
  ok &= file.write((uint8_t*)"WAVE", 4) == 4;
  ok &= file.write((uint8_t*)"fmt ", 4) == 4;
  ok &= file.write((uint8_t*)&fmtLength, 4) == 4;
  ok &= file.write((uint8_t*)&audioFormat, 2) == 2;
  ok &= file.write((uint8_t*)&channels, 2) == 2;
  ok &= file.write((uint8_t*)&sampleRate, 4) == 4;
  ok &= file.write((uint8_t*)&byteRate, 4) == 4;
  ok &= file.write((uint8_t*)&blockAlign, 2) == 2;
  ok &= file.write((uint8_t*)&bits, 2) == 2;
  ok &= file.write((uint8_t*)"data", 4) == 4;
  ok &= file.write((uint8_t*)&dataBytes, 4) == 4;
  file.flush();
  const bool restored = file.seek(max<uint32_t>(resumeAt, 44));
  return ok && restored;
}

static bool finalizeWav(File& file, uint32_t dataBytes) {
  const bool ok = file && writeWavHeader(file, dataBytes);
  file.close();
  return ok && dataBytes > 1000;
}

static File openRecordingPart(int noteNum, int part) {
  char path[72]; recordingPath(path, sizeof(path), noteNum, part);
  if (SDCard.exists(path)) return File();
  File file = SDCard.open(path, FILE_WRITE);
  if (file) {
    uint8_t header[44] = {};
    if (file.write(header, sizeof(header)) != sizeof(header) || !writeWavHeader(file, 0)) {
      file.close(); return File();
    }
  }
  return file;
}

int recordingPartCount(int noteNum) {
  int count = 0; char path[72];
  for (int part = 1; part < 100; part++) {
    recordingPath(path, sizeof(path), noteNum, part);
    if (!SDCard.exists(path)) break;
    File file = SDCard.open(path);
    const bool hasAudio = file && file.size() > 44;
    file.close();
    if (!hasAudio) break;
    count++;
  }
  return count;
}

uint32_t recordingDurationSeconds(int noteNum) {
  uint32_t seconds = 0; char path[72];
  const int parts = recordingPartCount(noteNum);
  for (int part = 1; part <= parts; part++) {
    recordingPath(path, sizeof(path), noteNum, part);
    File file = SDCard.open(path);
    if (file && file.size() > 44) seconds += (file.size() - 44) / (SAMPLE_RATE * 2);
    file.close();
  }
  return seconds;
}

bool record(bool* lowBattery) {
  if (lowBattery) *lowBattery = false;
  int num = nextNoteNumber();
  if (num <= 0) return false;
  int part = 1;
  File f = openRecordingPart(num, part);
  if (!f) return false;

  int16_t* sbuf = (int16_t*)heap_caps_malloc(REC_BUF,   MALLOC_CAP_8BIT);
  int16_t* mbuf = (int16_t*)heap_caps_malloc(REC_BUF/2, MALLOC_CAP_8BIT);
  if (!sbuf||!mbuf) {
    if(sbuf)heap_caps_free(sbuf); if(mbuf)heap_caps_free(mbuf);
    f.close(); return false;
  }

  if (!startRecordingUi()) {
    heap_caps_free(sbuf); heap_caps_free(mbuf); f.close(); return false;
  }
  bool audioOk = true, previousPartSaved = false;
  uint32_t totalMono = 0, partBytes = 0, started = millis(), partStarted = started, lastCheckpoint = started;
  uint32_t pressedAt = 0;
  bool wasPressed = false, stop = false, controlsArmed = false;
  uint32_t lastBatCheck = started;
  int lowBatReads = 0;

  while (!stop && millis() - started < RECORD_MAX_MS) {
    if (audio_playback_read((void*)sbuf, REC_BUF) != 0) { audioOk = false; break; }
    int mono = REC_BUF/4;
    for (int i=0;i<mono;i++) mbuf[i] = sbuf[i*2];
    size_t written = f.write((uint8_t*)mbuf, mono*2);
    totalMono += written - (written & 1U); partBytes += written - (written & 1U);
    if (written != (size_t)(mono * 2)) { audioOk = false; break; }

    // Hold upper to start, hold upper again to stop. The start press must be
    // released first; short taps and the lower button are ignored.
    const bool pressed = digitalRead(BTN_REC) == LOW;
    if (!controlsArmed) {
      if (!pressed) controlsArmed = true;
    } else if (pressed) {
      if (!wasPressed) { wasPressed = true; pressedAt = millis(); }
      else if (millis() - pressedAt >= BTN_LONG_MS) stop = true;
    } else {
      wasPressed = false;
    }

    if (!stop && millis() - partStarted >= RECORD_CHUNK_MS) {
      if (!finalizeWav(f, partBytes)) { audioOk = false; break; }
      previousPartSaved = true;
      part++; partBytes = 0; partStarted = millis();
      f = openRecordingPart(num, part);
      if (!f) { audioOk = false; break; }
    }
    if (millis() - lastCheckpoint >= 5000) {
      if (!writeWavHeader(f, partBytes)) { audioOk = false; break; }
      lastCheckpoint = millis();
      const uint64_t total = SDCard.totalBytes(), used = SDCard.usedBytes();
      if (used >= total || total - used < RECORD_MIN_FREE_BYTES) stop = true;
    }
    // Stop and save before the cell browns out. Two low reads in a row, so a
    // single load spike does not end the recording.
    if (!stop && millis() - lastBatCheck >= BAT_REC_CHECK_MS) {
      lastBatCheck = millis();
      const float v = readBatteryVoltageFast();
      if (v > 0.1f && v < BAT_REC_CUTOFF_V) lowBatReads++;
      else lowBatReads = 0;
      if (lowBatReads >= BAT_REC_CUTOFF_READS) {
        Serial.printf("[Rec] battery low (%.2f V), stopping\n", v);
        if (lowBattery) *lowBattery = true;
        stop = true;
      }
    }
  }

  // Swallow the stop press so it cannot also confirm the tag screen.
  while (digitalRead(BTN_REC) == LOW) delay(5);
  waitForRecordingUi();
  audio_idle_suspend();
  heap_caps_free(sbuf); heap_caps_free(mbuf);

  const bool tailSaved = f ? finalizeWav(f, partBytes) : false;
  // Releasing just after a chunk boundary can leave a header-only tail.
  // Earlier finalized parts still form a usable recording.
  if (previousPartSaved && !tailSaved && partBytes <= 1000) {
    char path[72]; recordingPath(path, sizeof(path), num, part);
    if (SDCard.exists(path)) SDCard.remove(path);
  }
  const bool finalSaved = previousPartSaved || tailSaved;

  lastRecNum = num;
  Serial.printf("[Rec] done: %lu bytes, %d parts\n", (unsigned long)totalMono, part);
  if (!audioOk) Serial.println("[Rec] interrupted recording retained");
  return finalSaved && totalMono > 1000;
}

bool playWavFile(const char* path, bool* cancelled) {
  if (cancelled) *cancelled = false;
  File f = SDCard.open(path);
  if (!f) return false;
  if (f.size() <= 44) { f.close(); return false; }

  // Do not assume that PCM starts at byte 44. Speech services may add LIST,
  // JUNK or an extended fmt chunk, especially for longer generations.
  uint8_t riff[12];
  if (f.read(riff, sizeof(riff)) != sizeof(riff) ||
      memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
    f.close(); return false;
  }
  uint16_t format = 0, channels = 0, bits = 0;
  uint32_t sampleRate = 0, dataOffset = 0, dataBytes = 0;
  while (f.available() >= 8) {
    uint8_t chunk[8];
    if (f.read(chunk, sizeof(chunk)) != sizeof(chunk)) break;
    const uint32_t chunkSize = (uint32_t)chunk[4] | ((uint32_t)chunk[5] << 8) |
                               ((uint32_t)chunk[6] << 16) | ((uint32_t)chunk[7] << 24);
    const uint32_t chunkData = f.position();
    if (!wavChunkFits(chunkData, chunkSize, f.size())) { f.close(); return false; }
    if (memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
      uint8_t fmt[16];
      if (f.read(fmt, sizeof(fmt)) != sizeof(fmt)) break;
      format = fmt[0] | ((uint16_t)fmt[1] << 8);
      channels = fmt[2] | ((uint16_t)fmt[3] << 8);
      sampleRate = (uint32_t)fmt[4] | ((uint32_t)fmt[5] << 8) |
                   ((uint32_t)fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
      bits = fmt[14] | ((uint16_t)fmt[15] << 8);
    } else if (memcmp(chunk, "data", 4) == 0) {
      dataOffset = chunkData;
      dataBytes = chunkSize;
      break;
    }
    const uint64_t next = uint64_t(chunkData) + chunkSize + (chunkSize & 1U);
    if (next > f.size() || !f.seek(static_cast<uint32_t>(next))) break;
  }
  if (format != 1 || (channels != 1 && channels != 2) || bits != 16 ||
      sampleRate != SAMPLE_RATE || !dataOffset || !wavFramesValid(dataBytes, channels) ||
      !f.seek(dataOffset)) {
    Serial.printf("[Audio] invalid WAV fmt=%u ch=%u rate=%lu bits=%u data=%lu\n",
                  format, channels, static_cast<unsigned long>(sampleRate), bits,
                  static_cast<unsigned long>(dataBytes));
    f.close(); return false;
  }
  Serial.printf("[Audio] playing %s: %lu bytes, %lu Hz, %u ch\n", path,
                static_cast<unsigned long>(dataBytes),
                static_cast<unsigned long>(sampleRate), channels);


  const int monoBytes = 1024;
  uint8_t* monoBuf   = (uint8_t*)heap_caps_malloc(monoBytes,     MALLOC_CAP_8BIT);
  int16_t* stereoBuf = (int16_t*)heap_caps_malloc(monoBytes * 2, MALLOC_CAP_8BIT);

  if (!monoBuf || !stereoBuf) {
    if (monoBuf)   heap_caps_free(monoBuf);
    if (stereoBuf) heap_caps_free(stereoBuf);
    f.close();
    return false;
  }

  audioPlaying  = true;
  stopPlayback  = false;

  soundSetEnabled(false);
  audio_playback_set_vol(85);

  uint32_t remaining = dataBytes;
  bool playbackOk = true, cancelArmed = digitalRead(BTN_REC) == HIGH;
  uint32_t playedBytes = 0;
  while (remaining && !stopPlayback) {
    int readBytes = f.read(monoBuf, min<uint32_t>(monoBytes, remaining));
    if (readBytes <= 0 || !wavFramesValid(readBytes, channels)) { playbackOk = false; break; }
    remaining -= readBytes;

    int samples = readBytes / (2 * channels);
    int16_t* mono = (int16_t*)monoBuf;
    for (int i = 0; i < samples; i++) {
      int16_t s = channels == 1 ? mono[i] : (int16_t)(((int32_t)mono[i*2] + mono[i*2+1]) / 2);
      stereoBuf[i * 2 + 0] = s;
      stereoBuf[i * 2 + 1] = s;
    }
    const uint32_t outputBytes = samples * 2 * sizeof(int16_t);
    if (audio_playback_write((void*)stereoBuf, outputBytes) != 0) { playbackOk = false; break; }
    playedBytes += outputBytes;

    if (digitalRead(BTN_REC) == HIGH) cancelArmed = true;
    else if (cancelArmed) {
      delay(20);
      if (digitalRead(BTN_REC) == LOW) {
        stopPlayback = true;
      }
    }
  }

  if (playbackOk && !stopPlayback && playedBytes) {
    memset(stereoBuf, 0, monoBytes * 2);
    for (int i = 0; i < 5; ++i)
      if (audio_playback_write(stereoBuf, monoBytes * 2) != 0) { playbackOk = false; break; }
  }
  audio_playback_set_vol(0);
  audio_idle_suspend();
  soundSetEnabled(true);

  heap_caps_free(monoBuf);
  heap_caps_free(stereoBuf);
  f.close();

  if (cancelled) *cancelled = stopPlayback;
  audioPlaying = false;
  stopPlayback = false;
  return playbackOk && playedBytes > 0;
}

// Plays every ten-minute part in order; a tap on the upper button stops all.
bool playRecording(int noteNum) {
  const int parts = recordingPartCount(noteNum);
  bool played = false;
  for (int part = 1; part <= parts; part++) {
    char path[72]; recordingPath(path, sizeof(path), noteNum, part);
    showPlaybackOverlay(part, parts);
    bool cancelled = false;
    const bool ok = playWavFile(path, &cancelled);
    played = played || ok;
    if (cancelled || !ok) break;
  }
  // The tap that stopped playback must not count as a new action: on release
  // it would replay the note, and held it would open the delete prompt.
  while (digitalRead(BTN_REC) == LOW) delay(5);
  clearButtonPresses();
  // Playback blocks the loop; restart the sleep timer so the device stays awake.
  resetActivity();
  return played;
}
