#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "notes.h"
#include "rtc.h"
#include "draw.h"
#include "../storage/sd_card.h"
#include "storage_commit.h"
#include "utf8_text.h"
#include <algorithm>

static bool storeText(const String& path, const String& content) {
  const String temp = path + ".tmp";
  if (!recoverStoredFile(SDCard, path) && SDCard.exists((path + ".bak").c_str())) return false;
  if (SDCard.exists(temp.c_str()) && !SDCard.remove(temp.c_str())) return false;
  File file = SDCard.open(temp.c_str(), FILE_WRITE);
  if (!file) return false;
  const bool written = file.print(content) == content.length();
  file.flush();
  const bool sized = file.size() == content.length();
  file.close();
  return written && sized && commitStoredFile(SDCard, path, temp);
}

// ─── Index ────────────────────────────────────────────────────────────────

void loadIndex() {
  noteIndex.clear();
  // RC5 used extension-replacing backup names. Recover an interrupted old
  // commit before switching to the shared path-suffixed backup convention.
  if (!SDCard.exists(INDEX_FILE) && SDCard.exists("/notes/index.bak"))
    SDCard.rename("/notes/index.bak", INDEX_FILE);
  recoverStoredFile(SDCard, String(INDEX_FILE));
  File f = SDCard.open(INDEX_FILE);
  if (!f) return;
  while (f.available()) {
    String ln = f.readStringUntil('\n'); ln.trim();
    if (!ln.length()) continue;
    int c1=ln.indexOf(','), c2=ln.indexOf(',',c1+1);
    if (c1<0||c2<0) continue;
    NoteEntry e;
    e.num = ln.substring(0,c1).toInt();
    strncpy(e.tag, ln.substring(c1+1,c2).c_str(), 31);
    e.tag[31]='\0';
    e.hasText = (ln.substring(c2+1).toInt()==1);
    noteIndex.push_back(e);
  }
  f.close();
}

bool saveIndex() {
  String content;
  for (int i=0; i<(int)noteIndex.size(); i++)
    content += String(noteIndex[i].num) + "," + noteIndex[i].tag + "," + (noteIndex[i].hasText ? "1\n" : "0\n");
  if (!content.length()) content = "\n";
  return storeText(String(INDEX_FILE), content);
}

bool addToIndex(int num, const char* tag, bool hasText) {
  for (NoteEntry& existing : noteIndex) {
    if (existing.num != num) continue;
    const NoteEntry previous = existing;
    strncpy(existing.tag, tag, 31);
    existing.tag[31] = '\0';
    existing.hasText = existing.hasText || hasText;
    if (saveIndex()) return true;
    existing = previous;
    return false;
  }
  NoteEntry e;
  e.num = num;
  strncpy(e.tag, tag, 31);
  e.tag[31]='\0';
  e.hasText = hasText;
  noteIndex.push_back(e);
  if (saveIndex()) return true;
  noteIndex.pop_back();
  return false;
}

bool updateIndexHasText(int num) {
  const char* foundTag = "";
  bool oldState = false;
  for (int i=0; i<(int)noteIndex.size(); i++) {
    if (noteIndex[i].num==num) {
      oldState = noteIndex[i].hasText;
      noteIndex[i].hasText=true;
      foundTag = noteIndex[i].tag;
      break;
    }
  }
  if (!saveIndex()) {
    for (NoteEntry& entry : noteIndex) if (entry.num == num) entry.hasText = oldState;
    return false;
  }
  return writeNoteMeta(num, foundTag);
}

void deleteNote(int num) {
  const char* exts[] = {"wav", "txt", "meta", "marks", "meta.bak", "meta.tmp", "txt.bak", "txt.tmp", nullptr};
  char path[64];
  for (int e = 0; exts[e]; e++) {
    snprintf(path, sizeof(path), "%s/note_%03d.%s", NOTES_DIR, num, exts[e]);
    if (SDCard.exists(path)) SDCard.remove(path);
  }
  for (int part = 1; part < 100; part++) {
    snprintf(path, sizeof(path), "%s/note_%03d_p%02d.wav", NOTES_DIR, num, part);
    if (part > 1 && SDCard.exists(path)) SDCard.remove(path);
    for (const char* ext : {"txt", "txt.bak", "txt.tmp"}) {
      snprintf(path, sizeof(path), "%s/note_%03d_p%02d.%s", NOTES_DIR, num, part, ext);
      if (SDCard.exists(path)) SDCard.remove(path);
    }
  }
  for (int i = 0; i < (int)noteIndex.size(); i++) {
    if (noteIndex[i].num == num) {
      noteIndex.erase(noteIndex.begin() + i);
      break;
    }
  }
  saveIndex();
}

int nextNoteNumber() {
  int maxNum = 0;
  for (int i=0; i<(int)noteIndex.size(); i++) if (noteIndex[i].num > maxNum) maxNum = noteIndex[i].num;
  int next = maxNum + 1;
  char path[64];
  do {
    snprintf(path, sizeof(path), "%s/note_%03d.wav", NOTES_DIR, next);
    if (!SDCard.exists(path)) return next;
    next++;
  } while (next < 1000000);
  return -1;
}

bool saveTag(int num, const char* tag) {
  const auto found = std::find_if(noteIndex.begin(), noteIndex.end(), [num](const NoteEntry& entry) { return entry.num == num; });
  if (found == noteIndex.end()) return false;
  const String oldTag = found->tag;
  if (!writeNoteMeta(num, tag)) return false;
  if (addToIndex(num, tag, false)) return true;
  writeNoteMeta(num, oldTag.c_str());
  return false;
}

void recoverOrphanNotes() {
  File directory = SDCard.open(NOTES_DIR);
  if (!directory || !directory.isDirectory()) return;
  bool changed = false;
  File file = directory.openNextFile();
  while (file) {
    String name = file.name();
    const int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    if (name.startsWith("note_") && name.endsWith(".wav") && name.length() >= 12) {
      const String digits = name.substring(5, name.length() - 4);
      bool mainPart = digits.length() > 0;
      for (size_t i = 0; i < digits.length(); ++i)
        if (digits[i] < '0' || digits[i] > '9') mainPart = false;
      if (!mainPart || file.size() <= 44) {
        file.close(); file = directory.openNextFile(); continue;
      }
      const int num = name.substring(5, name.length() - 4).toInt();
      bool known = false;
      for (const NoteEntry& entry : noteIndex) if (entry.num == num) { known = true; break; }
      if (num > 0 && !known) {
        char textPath[64];
        snprintf(textPath, sizeof(textPath), "%s/note_%03d.txt", NOTES_DIR, num);
        NoteEntry recovered{};
        recovered.num = num;
        strncpy(recovered.tag, "Untagged", sizeof(recovered.tag) - 1);
        // A final transcript is published only after every audio part succeeds.
        recoverStoredFile(SDCard, String(textPath));
        recovered.hasText = SDCard.exists(textPath);
        noteIndex.push_back(recovered);
        writeNoteMeta(num, recovered.tag);
        changed = true;
      }
    }
    file.close();
    file = directory.openNextFile();
  }
  directory.close();
  if (changed) saveIndex();
}

// ─── Tags ─────────────────────────────────────────────────────────────────

bool saveTagsToFile() {
  String content;
  for (int i = 0; i < tagCount; i++) if (strlen(tags[i]) > 0) content += String(tags[i]) + "\n";
  if (!storeText(String(TAG_FILE), content)) {
    Serial.println("[SD] tag storage failed");
    return false;
  }
  return true;
}

void createDefaultTags() {
  tagCount = 0;
  for (int i = 0; i < DEFAULT_TAG_COUNT && tagCount < MAX_TAGS; i++) {
    strncpy(tags[tagCount], DEFAULT_TAGS[i], 31);
    tags[tagCount][31] = 0;
    tagCount++;
  }
  saveTagsToFile();
}

void loadTags() {
  tagCount = 0;
  recoverStoredFile(SDCard, String(TAG_FILE));
  if (!SDCard.exists(TAG_FILE) && SDCard.exists("/notes/tags.bak"))
    SDCard.rename("/notes/tags.bak", TAG_FILE);
  if (!SDCard.exists(TAG_FILE)) { createDefaultTags(); return; }
  File f = SDCard.open(TAG_FILE);
  if (!f) { createDefaultTags(); return; }
  while (f.available() && tagCount < MAX_TAGS) {
    String line = f.readStringUntil('\n'); line.trim();
    if (!line.length()) continue;
    bool exists = false;
    for (int i = 0; i < tagCount; i++) {
      if (strcasecmp(tags[i], line.c_str()) == 0) { exists = true; break; }
    }
    if (exists) continue;
    strncpy(tags[tagCount], line.c_str(), 31);
    tags[tagCount][31] = 0;
    tagCount++;
  }
  f.close();
  if (tagCount == 0) createDefaultTags();
}

bool addCustomTag(const char* newTag) {
  if (!newTag) return false;
  if (tagCount >= MAX_TAGS) return false;
  String clean = String(newTag); clean.trim();
  clean.replace(",", " "); clean.replace("\n", " "); clean.replace("\r", " ");
  while (clean.indexOf("  ") >= 0) clean.replace("  ", " ");
  if (clean.length() < 1) return false;
  if (clean.length() > 31) { clean = clean.substring(0, 31); trimIncompleteUtf8(clean); }
  for (int i = 0; i < tagCount; i++)
    if (strcasecmp(tags[i], clean.c_str()) == 0) return false;
  strncpy(tags[tagCount], clean.c_str(), 31);
  tags[tagCount][31] = 0;
  tagCount++;
  if (!saveTagsToFile()) {
    tags[--tagCount][0] = 0;
    return false;
  }
  return true;
}

bool tagHasNotes(const char* tag) {
  if (!tag) return false;
  for (int i = 0; i < (int)noteIndex.size(); i++)
    if (strcmp(noteIndex[i].tag, tag) == 0) return true;
  return false;
}

void replaceTagOnNotes(const char* oldTag, const char* newTag) {
  if (!oldTag || !newTag) return;
  for (int i = 0; i < (int)noteIndex.size(); i++) {
    if (strcmp(noteIndex[i].tag, oldTag) == 0) {
      strncpy(noteIndex[i].tag, newTag, 31);
      noteIndex[i].tag[31] = 0;
    }
  }
  saveIndex();
}

bool deleteTag(const char* tagName) {
  if (!tagName) return false;
  if (strcasecmp(tagName, "Untagged") == 0) return false;

  bool hadNotes = tagHasNotes(tagName);
  if (hadNotes) {
    bool hasUntagged = false;
    for (int i = 0; i < tagCount; i++)
      if (strcasecmp(tags[i], "Untagged") == 0) { hasUntagged = true; break; }
    if (!hasUntagged && tagCount < MAX_TAGS) {
      strncpy(tags[tagCount], "Untagged", 31);
      tags[tagCount][31] = 0;
      tagCount++;
    }
    replaceTagOnNotes(tagName, "Untagged");
  }

  int removeIdx = -1;
  for (int i = 0; i < tagCount; i++)
    if (strcmp(tags[i], tagName) == 0) { removeIdx = i; break; }
  if (removeIdx < 0) return false;

  for (int i = removeIdx; i < tagCount - 1; i++) strcpy(tags[i], tags[i + 1]);
  tagCount--;

  if (tagCount <= 0) createDefaultTags();
  else               saveTagsToFile();
  return true;
}

// ─── Meta ─────────────────────────────────────────────────────────────────

String noteMetaPath(int num) {
  char path[64];
  snprintf(path, sizeof(path), "%s/note_%03d.meta", NOTES_DIR, num);
  return String(path);
}

String readNoteMetaValue(int num, const char* key) {
  String path = noteMetaPath(num);
  recoverStoredFile(SDCard, path);
  File f = SDCard.open(path.c_str());
  if (!f) return "";
  String prefix = String(key) + "=";
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.startsWith(prefix)) { f.close(); return line.substring(prefix.length()); }
  }
  f.close();
  return "";
}

bool writeNoteMeta(int num, const char* tag) {
  String path = noteMetaPath(num);
  String existingCreated = readNoteMetaValue(num, "created_utc");
  String created = existingCreated.length() ? existingCreated : currentUtcIso();
  bool hasText = false;
  for (int i = 0; i < (int)noteIndex.size(); i++)
    if (noteIndex[i].num == num) { hasText = noteIndex[i].hasText; break; }
  return storeText(path, "created_utc=" + created + "\ntag=" + String(tag ? tag : "") +
                   "\nsynced=" + (hasText ? "1\n" : "0\n"));
}

// ─── Time helpers ─────────────────────────────────────────────────────────

String noteCreatedUtc(int num) {
  return readNoteMetaValue(num, "created_utc");
}

String utcToLocalDeviceLabel(const String& utcIso) {
  if (utcIso.length() < 16) return "time not set";
  struct tm utc;
  memset(&utc, 0, sizeof(struct tm));
  utc.tm_year = utcIso.substring(0, 4).toInt() - 1900;
  utc.tm_mon  = utcIso.substring(5, 7).toInt() - 1;
  utc.tm_mday = utcIso.substring(8, 10).toInt();
  utc.tm_hour = utcIso.substring(11, 13).toInt();
  utc.tm_min  = utcIso.substring(14, 16).toInt();
  utc.tm_sec  = 0;
  time_t epoch = utcTmToEpoch(utc);
  struct tm localTm;
  setenv("TZ", LOCAL_TZ_RULE, 1);
  tzset();
  localtime_r(&epoch, &localTm);
  char buf[22];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &localTm);
  return String(buf);
}

String noteCreatedDeviceLabel(int num) {
  String utc = noteCreatedUtc(num);
  if (utc.length() < 16) return "time not set";
  return utcToLocalDeviceLabel(utc);
}

String currentUtcIso() {
  String fromRtc = rtcUtcIso();
  if (fromRtc.length() > 0) { timeReady = true; return fromRtc; }
  time_t now = time(nullptr);
  if (!timeReady || now < 1700000000) return "";
  struct tm utc;
  gmtime_r(&now, &utc);
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return String(buf);
}

// ─── Ticker + preview ─────────────────────────────────────────────────────

String notePreviewText(int num, size_t maxLen) {
  char txtPath[64];
  snprintf(txtPath, sizeof(txtPath), "%s/note_%03d.txt", NOTES_DIR, num);
  File f = SDCard.open(txtPath);
  if (!f) return "";
  String out = "";
  while (f.available() && out.length() < maxLen) {
    char c = (char)f.read();
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    out += c;
  }
  f.close();
  return normalizeForDisplay(out);
}

String noteTickerText(int idx) {
  if (idx < 0) return "";
  // Transcripts are read in the portal; the device list shows only when.
  String dt = noteCreatedDeviceLabel(noteIndex[idx].num);
  if (dt == "time not set") return noteIndex[idx].hasText ? "transskriberet" : "ikke syncet";
  return dt;
}

bool activeTickerNeedsScroll(int cursor) {
  int count = filteredCount();
  if (count <= 0) return false;
  int idx = noteAtFilteredIndex(cursor);
  if (idx < 0) return false;
  String ticker = noteTickerText(idx);
  return textW(ticker.c_str(), 1) > 145;
}

void drawTickerText(int x, int y, int maxW, const String& rawText, bool active, uint8_t color) {
  String text = normalizeForDisplay(rawText);
  if (textW(text.c_str(), 1) <= maxW || !active) {
    drawStrFit(x, y, maxW, text.c_str(), 1, color);
    return;
  }
  String spacer = "     ";
  String loopText = text + spacer + text;
  int cycleLen = text.length() + spacer.length();
  int start = tickerOffset % max(1, cycleLen);
  String window = "";
  for (int i = start; i < (int)loopText.length(); i++) {
    String candidate = window + loopText[i];
    if (textW(candidate.c_str(), 1) > maxW) break;
    window = candidate;
  }
  if (window.length() == 0) drawStrFit(x, y, maxW, text.c_str(), 1, color);
  else                      drawStr(x, y, window.c_str(), 1, color);
}

// ─── Filtering ────────────────────────────────────────────────────────────

int filteredCount() {
  if (activeFilter < 0) return (int)noteIndex.size();
  int c = 0;
  for (int i=0; i<(int)noteIndex.size(); i++)
    if (strcmp(noteIndex[i].tag, tags[activeFilter])==0) c++;
  return c;
}

int noteAtFilteredIndex(int visIdx) {
  std::vector<int> matches;
  for (int i=0; i<(int)noteIndex.size(); i++)
    if (activeFilter<0 || strcmp(noteIndex[i].tag, tags[activeFilter])==0)
      matches.push_back(i);
  int rev = (int)matches.size() - 1 - visIdx;
  if (rev < 0 || rev >= (int)matches.size()) return -1;
  return matches[rev];
}
