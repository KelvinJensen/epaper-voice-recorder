#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "network.h"
#include "notes.h"
#include "record.h"
#include "battery.h"
#include "rtc.h"
#include "ui.h"
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include <WebServer.h>
#include "../storage/sd_card.h"
#include "esp_heap_caps.h"
#include "settings.h"
#include "sleep.h"
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include "utf8_text.h"
#include "http_transfer.h"
#include "storage_commit.h"
#include "html_escape.h"
#include "classic_transfer_policy.h"
#include "viewer_request_policy.h"

extern const uint8_t ca_bundle_start[] asm("_binary_certs_ca_bundle_pem_start");

static String portalToken;

static String urlEncode(const String& value) {
  static const char* hex = "0123456789ABCDEF";
  String encoded;
  for (size_t i = 0; i < value.length(); i++) {
    const uint8_t c = (uint8_t)value[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') encoded += (char)c;
    else { encoded += '%'; encoded += hex[c >> 4]; encoded += hex[c & 0x0f]; }
  }
  return encoded;
}

static bool portalMutationAllowed() {
  if (transferServer.method() == HTTP_POST && transferServer.hasArg("token") &&
      transferServer.arg("token") == portalToken &&
      viewerWriteAllowed(transferServer.hostHeader().c_str(), transferServer.header("Origin").c_str(),
                         transferServer.header("Referer").c_str(), transferServer.header("Sec-Fetch-Site").c_str())) return true;
  transferServer.send(403, "text/plain; charset=utf-8", "Anmodningen blev afvist");
  return false;
}

namespace {
bool transcriptionRetryable = false;
bool storeTranscript(const String& path, const String& text) {
  const String temp = path + ".tmp";
  if (!recoverStoredFile(SDCard, path) && SDCard.exists((path + ".bak").c_str())) return false;
  if (SDCard.exists(temp.c_str()) && !SDCard.remove(temp.c_str())) return false;
  File file = SDCard.open(temp.c_str(), FILE_WRITE);
  if (!file) return false;
  const bool written = file.print(text) == text.length();
  file.flush();
  const bool sized = file.size() == text.length();
  file.close();
  return written && sized && commitStoredFile(SDCard, path, temp);
}
String partTranscriptPath(int noteNum, int part) {
  char path[80];
  snprintf(path, sizeof(path), "%s/note_%03d_p%02d.txt", NOTES_DIR, noteNum, part);
  return String(path);
}
}

// Uploads one range of PCM data as a WAV of its own. OpenAI stops reading a
// request body after about 60 s, so a whole 10-minute part (~19 MB) cannot
// make it at the recorder's ~250 KB/s; transcribeFile() sends it in pieces.
static bool transcribeOnce(File& file, uint32_t dataOffset, uint32_t dataBytes, String& text) {
  transcriptionRetryable = false;
  uint8_t wavHeader[44];
  const uint32_t riffSize = dataBytes + 36, fmtLength = 16, sampleRate = SAMPLE_RATE, byteRate = SAMPLE_RATE * 2;
  const uint16_t audioFormat = 1, channels = 1, blockAlign = 2, bits = 16;
  memcpy(wavHeader, "RIFF", 4); memcpy(wavHeader + 4, &riffSize, 4); memcpy(wavHeader + 8, "WAVEfmt ", 8);
  memcpy(wavHeader + 16, &fmtLength, 4); memcpy(wavHeader + 20, &audioFormat, 2); memcpy(wavHeader + 22, &channels, 2);
  memcpy(wavHeader + 24, &sampleRate, 4); memcpy(wavHeader + 28, &byteRate, 4); memcpy(wavHeader + 32, &blockAlign, 2);
  memcpy(wavHeader + 34, &bits, 2); memcpy(wavHeader + 36, "data", 4); memcpy(wavHeader + 40, &dataBytes, 4);
  const size_t fileSize = sizeof(wavHeader) + dataBytes;
  if (!file.seek(dataOffset)) return false;
  String boundary = "----RecorderBoundary";
  String pre = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\n" TRANSCRIBE_MODEL "\r\n"
    "--" + boundary + "\r\nContent-Disposition: form-data; name=\"languages[]\"\r\n\r\n" TRANSCRIBE_LANGUAGE "\r\n"
    "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"note.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
  String post = "\r\n--" + boundary + "--\r\n";
  WiFiClientSecure client;
  client.setCACert(reinterpret_cast<const char*>(ca_bundle_start));
  client.setHandshakeTimeout(12);
  client.setTimeout(15);
  const uint32_t started = millis();
  // Allow ~64 KB/s for the upload on top of the base budget, and give the
  // server time to transcribe before it answers.
  const uint32_t limitMs = 90000UL + fileSize / 64;
  const auto now = []() { return millis(); };
  const auto yield = []() { delay(1); };
  transcriptionRetryable = true;
  if (!client.connect("api.openai.com", 443)) {
    char err[100] = "";
    client.lastError(err, sizeof(err));
    Serial.printf("[Transcribe] connect failed: %s\n", err);
    return false;
  }
  const String header = "POST /v1/audio/transcriptions HTTP/1.1\r\nHost: api.openai.com\r\nAuthorization: Bearer " +
    classicOpenAiKey() + "\r\nContent-Type: multipart/form-data; boundary=" + boundary +
    "\r\nContent-Length: " + String(pre.length() + fileSize + post.length()) +
    "\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n";
  const auto send = [&](const String& value) {
    return classicWriteAll(client, (const uint8_t*)value.c_str(), value.length(), now, yield, started, limitMs);
  };
  bool sent = send(header) && send(pre) &&
    classicWriteAll(client, wavHeader, sizeof(wavHeader), now, yield, started, limitMs);
  uint8_t buffer[4096];
  size_t remaining = dataBytes;
  while (sent && remaining) {
    const size_t count = file.read(buffer, min<size_t>(sizeof(buffer), remaining));
    if (!count) { transcriptionRetryable = false; sent = false; break; }
    remaining -= count;
    sent = classicWriteAll(client, buffer, count, now, yield, started, limitMs);
  }
  sent = sent && send(post);
  HttpResponseBuffer response(128 * 1024);
  const bool complete = sent && receiveFramedHttp(client, response, now, yield, started, limitMs, 120000);
  client.stop();
  if (!complete || response.status != 200) {
    transcriptionRetryable = transcriptionRetryable && (classicTransientStatus(response.status) ||
      (response.status == 200 && !complete));
    Serial.printf("[Transcribe] failed status=%d framing=%s sent=%s after %lu s (%u bytes)\n", response.status,
                  response.error ? response.error : (complete ? "complete" : "timeout"),
                  sent ? "yes" : "no", (unsigned long)((millis() - started) / 1000), (unsigned)fileSize);
    if (response.status >= 400 && response.body.size())
      Serial.printf("[Transcribe] %.*s\n", (int)min<size_t>(response.body.size(), 400), response.body.c_str());
    return false;
  }
  transcriptionRetryable = false;
  JsonDocument doc;
  if (deserializeJson(doc, response.body.c_str(), response.body.size()) ||
      !doc["text"].is<const char*>()) return false;
  text = doc["text"].as<String>();
  text.trim();
  return true;
}

static bool transcribeFile(const String& wavPath, const String& textPath) {
  transcriptionRetryable = false;
  File file = SDCard.open(wavPath.c_str());
  if (!file || file.size() <= 44) {
    Serial.printf("[Transcribe] skipped %s: %s, %u bytes\n", wavPath.c_str(),
                  file ? "bad size" : "cannot open", file ? (unsigned)file.size() : 0U);
    return false;
  }
  const uint32_t dataBytes = (file.size() - 44) & ~1UL;
  String text;
  for (uint32_t offset = 0, length = 0; offset < dataBytes; offset += length) {
    length = min<uint32_t>(TRANSCRIBE_PIECE_BYTES, dataBytes - offset);
    // OpenAI rejects a sliver of audio as corrupted: fold a short tail into this piece.
    if (dataBytes - offset - length < TRANSCRIBE_PIECE_MIN_BYTES) length = dataBytes - offset;
    String piece;
    bool ok = false;
    for (int attempt = 0; attempt < 2 && !ok; ++attempt) {
      if (WiFi.status() != WL_CONNECTED) { file.close(); return false; }
      if (attempt) delay(1000);
      ok = transcribeOnce(file, 44 + offset, length, piece);
      if (!ok && !transcriptionRetryable) break;
    }
    if (!ok) { file.close(); return false; }
    if (piece.length() && text.length()) text += ' ';
    text += piece;
  }
  file.close();
  return text.length() && storeTranscript(textPath, text);
}

bool transcribe(const String& wavPath, int noteNum) {
  // Publish the final transcript only when every segment is durable. Each
  // segment is independent, so reconnect/reboot never repeats completed work.
  const int parts = recordingPartCount(noteNum);
  if (!parts) return false;
  String finalPath = wavPath;
  finalPath.replace(".wav", ".txt");
  const String temp = finalPath + ".tmp";
  if (SDCard.exists(temp.c_str()) && !SDCard.remove(temp.c_str())) return false;
  File assembled = SDCard.open(temp.c_str(), FILE_WRITE);
  if (!assembled) return false;
  bool ok = true;
  size_t expected = 0;
  for (int part = 1; ok && part <= parts; ++part) {
    char audioPath[80];
    if (part == 1) snprintf(audioPath, sizeof(audioPath), "%s/note_%03d.wav", NOTES_DIR, noteNum);
    else snprintf(audioPath, sizeof(audioPath), "%s/note_%03d_p%02d.wav", NOTES_DIR, noteNum, part);
    const String cached = partTranscriptPath(noteNum, part);
    recoverStoredFile(SDCard, cached);
    File saved = SDCard.open(cached.c_str());
    const bool cachedReady = saved && saved.size() > 0;
    saved.close();
    if (!cachedReady && !transcribeFile(String(audioPath), cached)) { ok = false; break; }
    saved = SDCard.open(cached.c_str());
    if (!saved || !saved.size()) { ok = false; break; }
    if (part > 1) {
      const uint32_t seconds = (part - 1) * (RECORD_CHUNK_MS / 1000UL);
      char label[40];
      snprintf(label, sizeof(label), "\n\n[%02lu:%02lu:%02lu]\n",
        (unsigned long)(seconds / 3600), (unsigned long)((seconds / 60) % 60), (unsigned long)(seconds % 60));
      const size_t length = strlen(label);
      if (assembled.print(label) != length) ok = false;
      expected += length;
    }
    uint8_t buffer[2048];
    size_t remaining = saved.size();
    while (ok && remaining) {
      const size_t count = saved.read(buffer, min<size_t>(sizeof(buffer), remaining));
      if (!count || assembled.write(buffer, count) != count) { ok = false; break; }
      expected += count;
      remaining -= count;
    }
    saved.close();
  }
  assembled.flush();
  ok = ok && expected > 0 && assembled.size() == expected;
  assembled.close();
  if (!ok || !commitStoredFile(SDCard, finalPath, temp)) return false;
  return updateIndexHasText(noteNum);
}

bool transcribeAll() {
  int pending = 0;
  for (const NoteEntry& entry : noteIndex) if (!entry.hasText) pending++;
  int processed = 0;
  bool allDone = true;
  int failures = 0;
  for (const NoteEntry& entry : noteIndex) {
    if (entry.hasText) continue;
    if (WiFi.status() != WL_CONNECTED) { allDone = false; break; }
    showTranscribing(processed, pending);
    char path[64]; snprintf(path, sizeof(path), "%s/note_%03d.wav", NOTES_DIR, entry.num);
    if (!transcribe(String(path), entry.num)) {
      allDone = false;
      if (!transcriptionRetryable || ++failures >= 2) break;
    }
    processed++;
  }
  return allDone;
}

// ─── Portal helpers ────────────────────────────────────────────────────────

String htmlEscape(const String& s) {
  return String(escapeViewerHtml(s.c_str()).c_str());
}

String readSmallFile(const char* path, size_t maxLen) {
  File f = SDCard.open(path);
  if (!f) return "";
  String out;
  while (f.available() && out.length() < maxLen) out += (char)f.read();
  f.close();
  return out;
}

// Download names must stay plain ASCII inside a quoted header value.
static String safeFileNamePart(String value) {
  value.replace("\xC3\xA6", "ae"); value.replace("\xC3\x86", "Ae");
  value.replace("\xC3\xB8", "oe"); value.replace("\xC3\x98", "Oe");
  value.replace("\xC3\xA5", "aa"); value.replace("\xC3\x85", "Aa");
  String out;
  for (size_t i = 0; i < value.length(); i++) {
    const char c = value[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') out += c;
    else if (out.length() && out[out.length() - 1] != '-') out += '-';
  }
  while (out.endsWith("-")) out.remove(out.length() - 1);
  return out;
}

// e.g. 2026-10-19_Interview_012
static String noteDownloadName(int num) {
  char number[12]; snprintf(number, sizeof(number), "%03d", num);
  String name;
  const String created = noteCreatedDeviceLabel(num);
  if (created.length() >= 10 && created != "time not set") name = created.substring(0, 10) + "_";
  for (const NoteEntry& entry : noteIndex) {
    if (entry.num != num) continue;
    const String tag = safeFileNamePart(String(entry.tag));
    if (tag.length()) name += tag + "_";
    break;
  }
  return name + number;
}

String portalCss() {
  // Portal styling: green on pale grey, square corners,
  // Playfair Display headings, Open Sans text and small Inconsolata labels.
  return String(R"CSS(<style>
:root{--bg:#f6f8f7;--fg:#004d33;--muted:#666;--line:#ccc;--card:#fff;--accent:#e8f2ed;--danger:#b42318;
--display:'Playfair Display',Georgia,serif;--body:'Open Sans',Arial,sans-serif;--mono:'Inconsolata',ui-monospace,Menlo,monospace}
*{box-sizing:border-box}[hidden]{display:none!important}
body{margin:0;background:var(--bg);color:var(--fg);font:400 15px/1.6 var(--body);font-variant-numeric:lining-nums}
.wrap{max-width:860px;margin:0 auto;padding:40px 24px}
h1,h2{font-family:var(--display);font-weight:700;color:var(--fg);margin:0}
.top{display:flex;align-items:flex-end;justify-content:space-between;gap:16px;padding-bottom:16px}
.top h1{font-size:clamp(30px,6vw,48px);line-height:1.1}
.count,.kicker,.label,.btn,button,.stat span,.meta,.part,.hint{font-family:var(--mono)}
.count{font-size:11px;text-transform:uppercase;letter-spacing:.05em;color:var(--muted)}
.kicker{display:flex;gap:20px;align-items:center;border-top:1px solid var(--line);border-bottom:1px solid var(--line);padding:8px 0;margin-bottom:40px;font-size:10px;text-transform:uppercase;letter-spacing:.05em;color:var(--muted)}
.kicker a{color:var(--muted);text-decoration:none;border-bottom:2px solid transparent}
.kicker a.active,.kicker a:hover{color:var(--fg);border-color:var(--fg)}
.label{font-size:10px;font-weight:400;text-transform:uppercase;letter-spacing:.1em;color:var(--fg);margin:0 0 16px}
.stats{display:grid;grid-template-columns:repeat(4,1fr);gap:16px;margin-bottom:40px}
.stat,.box{background:var(--card);border:1px solid var(--line);border-top:2px solid var(--fg);padding:16px}
.stat strong{display:block;font-family:var(--display);font-size:28px;line-height:1.2}
.stat span{font-size:10px;text-transform:uppercase;letter-spacing:.05em;color:var(--muted)}
.toolbar{display:grid;grid-template-columns:1fr auto;gap:12px;margin-bottom:16px}
input,select{font:inherit;font-size:14px;color:var(--fg);background:var(--card);border:1px solid var(--line);border-radius:0;padding:9px 12px;outline:none;min-width:0}
input:focus,select:focus{border-color:var(--fg)}
.actions,.filters{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
.filters{margin-bottom:40px}
.btn,button{display:inline-block;font-size:10px;text-transform:uppercase;letter-spacing:.08em;text-decoration:none;cursor:pointer;border:1px solid var(--fg);background:transparent;color:var(--fg);padding:9px 14px;border-radius:0}
.btn:hover,button:hover{background:var(--accent)}
.btn.primary,button.primary{background:var(--fg);color:#fff}
.btn.primary:hover,button.primary:hover{opacity:.9}
button.danger{border-color:var(--line);color:var(--danger)}
.list{border-top:1px solid var(--fg)}
.note{display:grid;grid-template-columns:140px 1fr;gap:16px;padding:20px 0;border-bottom:1px solid var(--line)}
.meta{font-size:11px;text-transform:uppercase;color:var(--muted);line-height:1.8}
.tag{display:inline-block;margin-top:4px;background:var(--accent);color:var(--fg);padding:1px 8px;font-size:10px;letter-spacing:.05em}
.note h2,.tagrow h2{font-size:19px;line-height:1.3;margin-bottom:8px;overflow-wrap:anywhere}
.text{white-space:pre-wrap;margin:0 0 12px;overflow-wrap:anywhere}
.pending{color:var(--muted);font-style:italic}
.part{font-size:10px;text-transform:uppercase;letter-spacing:.05em;color:var(--muted);margin:10px 0 0}
audio{display:block;width:100%;margin:4px 0 10px}
.parts{margin:4px 0 12px;border:1px solid var(--line);background:var(--card)}
.parts summary{cursor:pointer;list-style:none;font-family:var(--mono);font-size:10px;text-transform:uppercase;letter-spacing:.08em;padding:10px 14px}
.parts summary::-webkit-details-marker{display:none}.parts summary::before{content:'▸ '}.parts[open] summary::before{content:'▾ '}
.partrow{display:grid;grid-template-columns:64px 1fr auto;gap:12px;align-items:center;padding:8px 14px;border-top:1px solid var(--line)}
.partrow .part,.partrow audio{margin:0}
.edit{display:flex;flex-wrap:wrap;gap:8px;margin-top:14px;padding-top:14px;border-top:1px dashed var(--line)}
.edit form{display:flex;gap:8px}.edit select{padding:6px 10px;font-size:13px}.edit .del{margin-left:auto}
.empty{padding:40px 0;margin:0;color:var(--muted);text-align:center;border-bottom:1px solid var(--line)}
.box{margin-bottom:40px;padding:20px}
.add{display:grid;grid-template-columns:1fr auto;gap:10px}
.hint{font-size:11px;color:var(--muted);margin:10px 0 0}
.message{background:var(--accent);padding:12px 16px;margin-bottom:24px}
.tagrow{display:flex;justify-content:space-between;align-items:center;gap:16px;padding:16px 0;border-bottom:1px solid var(--line)}
.tagrow h2{margin:0}
@media(max-width:640px){.partrow{grid-template-columns:1fr auto}.partrow audio{grid-column:1/-1;grid-row:2}.stats{grid-template-columns:repeat(2,1fr)}.toolbar,.add{grid-template-columns:1fr}.note{grid-template-columns:1fr;gap:6px}}
</style>)CSS");
}

static String portalHead(const char* title) {
  return String("<!doctype html><html lang='da'><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'><title>") + title +
         "</title><link rel='stylesheet' href='https://fonts.googleapis.com/css2?family=Playfair+Display:wght@700"
         "&family=Open+Sans:wght@400;600&family=Inconsolata&display=swap'>" + portalCss() + "</head><body><div class='wrap'>";
}

static String portalNav(bool notesActive) {
  return String("<nav class='kicker'><a") + (notesActive ? " class='active'" : "") + " href='/'>Optagelser</a><a" +
         (notesActive ? "" : " class='active'") + " href='/tags'>Tags</a></nav>";
}

static String portalFoot() {
  return "</div></body></html>";
}

// Shortens at a word boundary and marks the cut.
static String cutAtWord(String text, size_t maxBytes) {
  text.trim();
  if (text.length() <= maxBytes) return text;
  String cut = text.substring(0, maxBytes);
  const int space = cut.lastIndexOf(' ');
  if (space > (int)maxBytes / 2) cut = cut.substring(0, space);
  else trimIncompleteUtf8(cut);
  cut.trim();
  return cut + " …";
}

// Splits off a first sentence short enough to work as a title.
static bool splitFirstSentence(const String& text, String& sentence, String& rest) {
  for (int i = 0; i < (int)text.length() && i < 90; i++) {
    const char c = text[i];
    if ((c == '.' || c == '?' || c == '!') && (i + 1 == (int)text.length() || text[i + 1] == ' ')) {
      if (i < 8) continue;
      sentence = text.substring(0, i + 1);
      rest = text.substring(i + 1); rest.trim();
      return true;
    }
  }
  return false;
}

static String durationLabel(uint32_t seconds) {
  char label[16];
  if (seconds >= 3600) snprintf(label, sizeof(label), "%lu:%02lu:%02lu", (unsigned long)(seconds / 3600),
                                (unsigned long)((seconds / 60) % 60), (unsigned long)(seconds % 60));
  else snprintf(label, sizeof(label), "%lu:%02lu", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
  return String(label);
}

// ─── Portal handlers ───────────────────────────────────────────────────────

void handlePortalRoot() {
  loadIndex();
  loadTags();

  String filter = "All";
  if (transferServer.hasArg("tag")) filter = transferServer.arg("tag");

  int transcribed = 0;
  for (const NoteEntry& entry : noteIndex) if (entry.hasText) transcribed++;
  const int total = (int)noteIndex.size();
  const int battery = readBatteryPercent();

  String html = portalHead("Optagelser");
  html += "<header class='top'><h1>Optagelser</h1><span class='count'>" + String(total) + (total == 1 ? " optagelse" : " optagelser") + "</span></header>";
  html += portalNav(true);

  html += "<div class='stats'><div class='stat'><strong>" + String(total) + "</strong><span>Optagelser</span></div>"
          "<div class='stat'><strong>" + String(transcribed) + "</strong><span>Transskriberet</span></div>"
          "<div class='stat'><strong>" + String(tagCount) + "</strong><span>Tags</span></div>"
          "<div class='stat'><strong>" + (battery >= 0 ? String(battery) + "%" : String("–")) + "</strong><span>Batteri</span></div></div>";

  html += "<div class='toolbar'><input id='search' type='search' placeholder='Søg i optagelser …' aria-label='Søg i optagelser'>"
          "<div class='actions'><a class='btn primary' href='/export.txt'>Download al tekst</a></div></div>";

  html += "<div class='filters'><a class='btn" + String(filter == "All" ? " primary" : "") + "' href='/'>Alle</a>";
  for (int t = 0; t < tagCount; t++) {
    const String tag = String(tags[t]);
    html += "<a class='btn" + String(filter == tag ? " primary" : "") + "' href='/?tag=" + urlEncode(tag) + "'>" + htmlEscape(tag) + "</a>";
  }
  if (filter != "All")
    html += "<a class='btn' href='/export.txt?tag=" + urlEncode(filter) + "'>Download filtreret tekst</a>";
  html += "</div>";

  html += "<h2 class='label'>" + (filter == "All" ? String("Alle optagelser") : htmlEscape(filter)) + "</h2><section class='list'>";
  int visible = 0;
  for (int v = 0; v < total; v++) {
    const int i = total - 1 - v;
    if (!(filter == "All" || filter == String(noteIndex[i].tag))) continue;
    visible++;
    const int num = noteIndex[i].num;

    char txtPath[64];
    snprintf(txtPath, sizeof(txtPath), "%s/note_%03d.txt", NOTES_DIR, num);
    String transcript = readSmallFile(txtPath, 1200);
    trimIncompleteUtf8(transcript);
    const bool pending = transcript.length() == 0;
    if (pending) transcript = noteIndex[i].hasText ? "(Tom transskription)" : "Ikke transskriberet endnu.";

    // Title is the first sentence; the excerpt continues after it.
    String flat = transcript; flat.replace("\n", " "); flat.trim();
    String title, excerpt = flat;
    if (pending) title = String("Optagelse ") + String(num);
    else if (!splitFirstSentence(flat, title, excerpt)) { title = cutAtWord(flat, 70); excerpt = flat; }
    excerpt = pending ? flat : cutAtWord(excerpt, 280);

    String searchable = title + " " + transcript + " " + String(noteIndex[i].tag);
    searchable.toLowerCase();
    char number[8]; snprintf(number, sizeof(number), "#%03d", num);
    const String createdUtc = noteCreatedUtc(num);

    html += "<article class='note' data-search='" + htmlEscape(searchable) + "'><div class='meta'>" + String(number) + "<br>";
    if (createdUtc.length()) html += "<time data-utc='" + createdUtc + "'>" + createdUtc + "</time><br>";
    else html += "Tidspunkt ukendt<br>";
    html += durationLabel(recordingDurationSeconds(num)) + "<br><span class='tag'>" + htmlEscape(String(noteIndex[i].tag)) + "</span></div>";

    html += "<div><h2>" + htmlEscape(title) + "</h2>";
    if (excerpt.length()) html += "<p class='text" + String(pending ? " pending" : "") + "'>" + htmlEscape(excerpt) + "</p>";
    const int partCount = recordingPartCount(num);
    if (partCount == 1) {
      html += "<audio controls preload='none' src='/audio?num=" + String(num) + "&part=1'></audio>";
    } else if (partCount > 1) {
      // Long recordings: one folded list instead of a player per ten minutes.
      html += "<details class='parts'><summary>Afspil og download · " + String(partCount) + " dele</summary>";
      for (int part = 1; part <= partCount; part++) {
        const String query = "num=" + String(num) + "&part=" + String(part);
        html += "<div class='partrow'><span class='part'>Del " + String(part) + " / " + String(partCount) + "</span>"
                "<audio controls preload='none' src='/audio?" + query + "'></audio>"
                "<a class='btn' href='/wav?" + query + "'>Download</a></div>";
      }
      html += "</details>";
    }
    html += "<div class='actions'>";
    if (noteIndex[i].hasText) {
      html += "<a class='btn' href='/txt?num=" + String(num) + "&view=1' target='_blank' rel='noopener'>Læs hele teksten</a>";
      html += "<a class='btn primary' href='/txt?num=" + String(num) + "'>Download tekst</a>";
    }
    if (partCount == 1) html += "<a class='btn' href='/wav?num=" + String(num) + "'>Download lyd</a>";
    html += "</div><div class='edit'><form action='/note/tag' method='post'><input type='hidden' name='token' value='" + portalToken +
            "'><input type='hidden' name='num' value='" + String(num) + "'><select name='tag' aria-label='Tag'>";
    for (int t = 0; t < tagCount; t++)
      html += "<option" + String(strcmp(tags[t], noteIndex[i].tag) == 0 ? " selected" : "") + ">" + htmlEscape(String(tags[t])) + "</option>";
    html += "</select><button type='submit'>Gem tag</button></form>"
            "<form class='del' action='/note/delete' method='post' onsubmit='return confirm(this.dataset.confirm)' data-confirm='Slet denne optagelse permanent?'>"
            "<input type='hidden' name='token' value='" + portalToken + "'><input type='hidden' name='num' value='" + String(num) +
            "'><button class='danger' type='submit'>Slet</button></form></div></div></article>";
  }
  if (!visible) html += String("<p class='empty'>") + (total ? "Ingen optagelser med dette tag." : "Ingen optagelser endnu.") + "</p>";
  html += "</section>";

  html += "<script>"
          "document.querySelectorAll('[data-utc]').forEach(function(el){"
          "var d=new Date(el.dataset.utc);"
          "if(!isNaN(d)){el.textContent=d.toLocaleString('da-DK',{year:'numeric',month:'short',day:'numeric',hour:'2-digit',minute:'2-digit'});}"
          "});"
          "var search=document.getElementById('search');if(search){search.addEventListener('input',function(){"
          "var q=this.value.toLocaleLowerCase();document.querySelectorAll('[data-search]').forEach(function(card){card.hidden=q&&!card.dataset.search.includes(q);});"
          "});}"
          "</script>";
  html += portalFoot();
  transferServer.send(200, "text/html; charset=utf-8", html);
}

void handlePortalJson() {
  loadIndex();
  // ArduinoJson escapes tag names, so quotes or backslashes cannot break the list.
  JsonDocument doc;
  JsonArray notes = doc.to<JsonArray>();
  for (int v = 0; v < (int)noteIndex.size(); v++) {
    const NoteEntry& entry = noteIndex[noteIndex.size() - 1 - v];
    JsonObject note = notes.add<JsonObject>();
    note["num"] = entry.num;
    note["tag"] = entry.tag;
    note["hasText"] = entry.hasText;
    const String created = noteCreatedUtc(entry.num);
    if (created.length()) note["created"] = created;
    else note["created"] = nullptr;
    note["durationSeconds"] = recordingDurationSeconds(entry.num);
    note["parts"] = recordingPartCount(entry.num);
  }
  String json;
  serializeJson(doc, json);
  transferServer.send(200, "application/json; charset=utf-8", json);
}

void handleExportTxt() {
  loadIndex();
  String filter = "All";
  if (transferServer.hasArg("tag")) filter = transferServer.arg("tag");

  String filename = "optagelser_eksport";
  if (filter != "All") filename += "_" + safeFileNamePart(filter);
  filename += ".txt";
  // Stream note by note in chunks so transcripts of any length fit in memory.
  transferServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  transferServer.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  transferServer.send(200, "text/plain; charset=utf-8", "");

  transferServer.sendContent(String("Eksport af optagelser\nFilter: ") + (filter == "All" ? String("Alle") : filter) +
                             "\n------------------------------\n\n");

  uint8_t buffer[2048];
  for (int v = 0; v < (int)noteIndex.size(); v++) {
    int i = (int)noteIndex.size() - 1 - v;
    if (!(filter == "All" || filter == String(noteIndex[i].tag))) continue;
    int num = noteIndex[i].num;
    char number[8]; snprintf(number, sizeof(number), "#%03d", num);
    String header = String(number) + " · " + String(noteIndex[i].tag) + "\n";
    const String created = noteCreatedDeviceLabel(num);
    if (created != "time not set") header += created + "\n";
    transferServer.sendContent(header + "\n");

    char txtPath[64]; snprintf(txtPath, sizeof(txtPath), "%s/note_%03d.txt", NOTES_DIR, num);
    File file = SDCard.open(txtPath);
    size_t sent = 0;
    while (file && file.available()) {
      const size_t count = file.read(buffer, sizeof(buffer));
      if (!count) break;
      transferServer.sendContent(reinterpret_cast<const char*>(buffer), count);
      sent += count;
    }
    if (file) file.close();
    if (!sent)
      transferServer.sendContent(noteIndex[i].hasText ? "(Tom transskription)" : "Ikke transskriberet endnu.");
    transferServer.sendContent("\n\n------------------------------\n\n");
  }
  transferServer.sendContent("");
}

void sendFileByNum(const char* ext, const char* mime, bool attachment) {
  if (!transferServer.hasArg("num")) { transferServer.send(400, "text/plain", "Missing num"); return; }
  int num = transferServer.arg("num").toInt();
  if (num <= 0) { transferServer.send(400, "text/plain", "Invalid num"); return; }
  const int requestedPart = transferServer.hasArg("part") ? (int)transferServer.arg("part").toInt() : 1;
  const int part = max(1, requestedPart);
  char path[72];
  if (strcmp(ext, "wav") == 0 && part > 1) snprintf(path, sizeof(path), "%s/note_%03d_p%02d.wav", NOTES_DIR, num, part);
  else snprintf(path, sizeof(path), "%s/note_%03d.%s", NOTES_DIR, num, ext);
  File f = SDCard.open(path);
  if (!f) { transferServer.send(404, "text/plain; charset=utf-8", "Filen findes ikke"); return; }
  if (attachment) {
    const bool numbered = strcmp(ext, "wav") == 0 && (part > 1 || recordingPartCount(num) > 1);
    String filename = noteDownloadName(num) + (numbered ? String("_part") + String(part) : String()) + "." + String(ext);
    transferServer.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  }
  transferServer.streamFile(f, mime);
  f.close();
}

void handleTagAdd() {
  if (!portalMutationAllowed()) return;
  if (!transferServer.hasArg("name")) {
    transferServer.sendHeader("Location", "/tags?msg=missing");
    transferServer.send(303); return;
  }
  String name = transferServer.arg("name");
  bool ok = addCustomTag(name.c_str());
  transferServer.sendHeader("Location", ok ? "/tags?msg=added" : "/tags?msg=exists");
  transferServer.send(303);
}

void handleTagDelete() {
  if (!portalMutationAllowed()) return;
  if (!transferServer.hasArg("name")) {
    transferServer.sendHeader("Location", "/tags?msg=missing");
    transferServer.send(303); return;
  }
  String name = transferServer.arg("name");
  bool hadNotes = tagHasNotes(name.c_str());
  bool ok = deleteTag(name.c_str());
  if (ok && hadNotes) transferServer.sendHeader("Location", "/tags?msg=moved");
  else                transferServer.sendHeader("Location", ok ? "/tags?msg=deleted" : "/tags?msg=protected");
  transferServer.send(303);
}

void handleTagsPage() {
  loadTags();
  loadIndex();
  activeFilter = -1;

  String html = portalHead("Tags");
  html += "<header class='top'><h1>Tags</h1><span class='count'>" + String(tagCount) + (tagCount == 1 ? " tag" : " tags") + "</span></header>";
  html += portalNav(false);

  if (transferServer.hasArg("msg")) {
    const String msg = transferServer.arg("msg");
    const char* text = "Skriv et navn til tagget.";
    if (msg == "added")          text = "Tagget er tilføjet.";
    else if (msg == "exists")    text = "Tagget findes allerede eller kunne ikke tilføjes.";
    else if (msg == "deleted")   text = "Tagget er slettet.";
    else if (msg == "moved")     text = "Tagget er slettet. Optagelserne med tagget er flyttet til Untagged.";
    else if (msg == "protected") text = "Dette tag kan ikke slettes.";
    html += "<div class='message'>" + String(text) + "</div>";
  }

  html += "<div class='box'><h2 class='label'>Nyt tag</h2><form class='add' action='/tag/add' method='post'>"
          "<input type='hidden' name='token' value='" + portalToken + "'><input name='name' maxlength='31' placeholder='Fx Møde' aria-label='Navn på nyt tag'>"
          "<button class='primary' type='submit'>Tilføj</button></form>"
          "<p class='hint'>Højst 31 tegn. Æ, ø og å vises som ae, oe og aa på optageren.</p></div>";

  html += "<h2 class='label'>Alle tags</h2><section class='list'>";
  for (int i = 0; i < tagCount; i++) {
    int count = 0;
    for (const NoteEntry& entry : noteIndex) if (strcmp(entry.tag, tags[i]) == 0) count++;
    html += "<div class='tagrow'><div><h2>" + htmlEscape(String(tags[i])) + "</h2><div class='meta'>" + String(count) + (count == 1 ? " optagelse" : " optagelser");
    if (count > 0) html += " · slettes tagget, flyttes optagelserne til Untagged";
    html += "</div></div>";
    if (strcasecmp(tags[i], "Untagged") != 0) {
      html += "<form action='/tag/delete' method='post' onsubmit='return confirm(this.dataset.confirm)' data-confirm='Slet tagget? Optagelserne bevares.'>"
              "<input type='hidden' name='token' value='" + portalToken + "'><input type='hidden' name='name' value='" +
              htmlEscape(String(tags[i])) + "'><button class='danger' type='submit'>Slet</button></form>";
    }
    html += "</div>";
  }
  html += "</section>" + portalFoot();
  transferServer.send(200, "text/html; charset=utf-8", html);
}

void handleNoteDelete() {
  if (!portalMutationAllowed()) return;
  if (!transferServer.hasArg("num")) { transferServer.send(400, "text/plain", "Missing num"); return; }
  int num = transferServer.arg("num").toInt();
  if (num <= 0) { transferServer.send(400, "text/plain", "Invalid num"); return; }
  deleteNote(num);
  transferServer.sendHeader("Location", "/");
  transferServer.send(303);
}

void handleNoteTag() {
  if (!portalMutationAllowed()) return;
  if (!transferServer.hasArg("num") || !transferServer.hasArg("tag")) {
    transferServer.send(400, "text/plain", "Missing values"); return;
  }
  const int num = transferServer.arg("num").toInt();
  const String tag = transferServer.arg("tag");
  bool validTag = false;
  for (int i = 0; i < tagCount; i++) if (tag == tags[i]) { validTag = true; break; }
  if (num <= 0 || !validTag) { transferServer.send(400, "text/plain", "Invalid values"); return; }
  if (!saveTag(num, tag.c_str())) { transferServer.send(500, "text/plain", "Tag storage failed"); return; }
  transferServer.sendHeader("Location", "/");
  transferServer.send(303);
}

// Every portal request keeps Transfer mode awake, also while a long download runs.
static void portalRoute(const Uri& uri, HTTPMethod method, WebServer::THandlerFunction handler) {
  transferServer.on(uri, method, [handler]() { resetActivity(); handler(); resetActivity(); });
}

void setupTransferServer() {
  const char* headers[] = {"Origin", "Referer", "Sec-Fetch-Site"};
  transferServer.collectHeaders(headers, 3);
  if (portalToken.length() == 0) portalToken = String((uint32_t)esp_random(), HEX) + String((uint32_t)esp_random(), HEX);
  portalRoute("/", HTTP_GET, handlePortalRoot);
  portalRoute("/tags", HTTP_GET, handleTagsPage);
  portalRoute("/tag/add", HTTP_POST, handleTagAdd);
  portalRoute("/tag/delete", HTTP_POST, handleTagDelete);
  portalRoute("/note/delete", HTTP_POST, handleNoteDelete);
  portalRoute("/note/tag", HTTP_POST, handleNoteTag);
  portalRoute("/api/notes", HTTP_GET, handlePortalJson);
  portalRoute("/export.txt", HTTP_GET, handleExportTxt);
  portalRoute("/txt",   HTTP_GET, [](){ sendFileByNum("txt", "text/plain; charset=utf-8", !transferServer.hasArg("view")); });
  portalRoute("/wav",   HTTP_GET, [](){ sendFileByNum("wav", "audio/wav",  true); });
  portalRoute("/audio", HTTP_GET, [](){ sendFileByNum("wav", "audio/wav",  false); });
  transferServer.onNotFound([](){
    transferServer.send(404, "text/plain; charset=utf-8", "Siden findes ikke");
  });
  MDNS.begin("recorder");
  MDNS.addService("http", "tcp", 80);
}

void stopTransferMode() {
  if (transferServerActive) {
    transferServer.stop();
    transferServerActive = false;
  }
  WiFi.disconnect(true);
  MDNS.end();
  WiFi.mode(WIFI_OFF);
  transferUrl = "";
}
