#include "Arduino.h"
#include "../../config.h"
#include "../../globals.h"
#include "../../types.h"
#include "ui.h"
#include "draw.h"
#include "notes.h"
#include "record.h"
#include "battery.h"
#include "rtc.h"
#include "settings.h"
#include "../../sounds.h"
#include "../storage/sd_card.h"

#define W   200
#define H   200

static const char* homeReady() {
  const String language = classicLanguage();
  if (language == "de") return "BEREIT";
  if (language == "fr") return "PRET";
  if (language == "es") return "LISTO";
  if (language == "it") return "PRONTO";
  if (language == "pl") return "GOTOWE";
  if (language == "cs") return "PRIPRAVENO";
  if (language == "ru") return "GOTOVO";
  if (language == "uk") return "HOTOVO";
  if (language == "nl") return "KLAAR";
  if (language == "pt") return "PRONTO";
  return "READY";
}

static const char* homeNotes() {
  const String language = classicLanguage();
  if (language == "de") return "NOTIZEN";
  if (language == "fr") return "NOTES";
  if (language == "es") return "NOTAS";
  if (language == "it") return "NOTE";
  if (language == "pl") return "NOTATKI";
  if (language == "cs") return "POZNAMKY";
  if (language == "ru") return "ZAPISI";
  if (language == "uk") return "NOTATKY";
  if (language == "nl") return "NOTITIES";
  if (language == "pt") return "NOTAS";
  return "NOTES";
}

static const char* homeRecordHint() {
  const String language = classicLanguage();
  if (language == "de") return "HALTEN: AUFNEHMEN";
  if (language == "fr") return "MAINTENIR: ENREG.";
  if (language == "es") return "MANTENER: GRABAR";
  if (language == "it") return "TIENI: REGISTRA";
  if (language == "pl") return "TRZYMAJ: NAGRAJ";
  if (language == "cs") return "DRZET: NAHRAT";
  if (language == "ru") return "DERZHAT: ZAPIS";
  if (language == "uk") return "TRYMATY: ZAPYS";
  if (language == "nl") return "HOUD: OPNAME";
  if (language == "pt") return "SEGURE: GRAVAR";
  return "HOLD: RECORD";
}

static const char* homeMenuHint() {
  const String language = classicLanguage();
  if (language == "de") return "UNTEN: MENUE";
  if (language == "fr") return "BAS: MENU";
  if (language == "es") return "ABAJO: MENU";
  if (language == "it") return "SOTTO: MENU";
  if (language == "pl") return "DOL: MENU";
  if (language == "cs") return "DOLE: MENU";
  if (language == "ru") return "VNIZ: MENU";
  if (language == "uk") return "VNYZ: MENU";
  if (language == "nl") return "ONDER: MENU";
  if (language == "pt") return "BAIXO: MENU";
  return "LOWER: MENU";
}

static void drawCompactBattery(int percent) {
  if (percent < 0) return;
  percent = constrain(percent, 0, 100);
  const int x = 160, y = 9, width = 23, height = 12;
  strokeRect(x, y, width, height, 1, BLACK);
  fillRect(x + width, y + 3, 2, 6, BLACK);
  const int bars = percent > 66 ? 3 : percent > 33 ? 2 : percent > 10 ? 1 : 0;
  for (int i = 0; i < bars; i++) fillRect(x + 3 + i * 6, y + 3, 5, 6, BLACK);
}

static const char* classicMonth(int month) {
  static const char* en[] = {"JANUARY","FEBRUARY","MARCH","APRIL","MAY","JUNE","JULY","AUGUST","SEPTEMBER","OCTOBER","NOVEMBER","DECEMBER"};
  static const char* de[] = {"JANUAR","FEBRUAR","MAERZ","APRIL","MAI","JUNI","JULI","AUGUST","SEPTEMBER","OKTOBER","NOVEMBER","DEZEMBER"};
  static const char* fr[] = {"JANVIER","FEVRIER","MARS","AVRIL","MAI","JUIN","JUILLET","AOUT","SEPTEMBRE","OCTOBRE","NOVEMBRE","DECEMBRE"};
  static const char* es[] = {"ENERO","FEBRERO","MARZO","ABRIL","MAYO","JUNIO","JULIO","AGOSTO","SEPTIEMBRE","OCTUBRE","NOVIEMBRE","DICIEMBRE"};
  static const char* it[] = {"GENNAIO","FEBBRAIO","MARZO","APRILE","MAGGIO","GIUGNO","LUGLIO","AGOSTO","SETTEMBRE","OTTOBRE","NOVEMBRE","DICEMBRE"};
  static const char* pl[] = {"STYCZEN","LUTY","MARZEC","KWIECIEN","MAJ","CZERWIEC","LIPIEC","SIERPIEN","WRZESIEN","PAZDZIERNIK","LISTOPAD","GRUDZIEN"};
  static const char* cs[] = {"LEDEN","UNOR","BREZEN","DUBEN","KVETEN","CERVEN","CERVENEC","SRPEN","ZARI","RIJEN","LISTOPAD","PROSINEC"};
  static const char* ru[] = {"YANVAR","FEVRAL","MART","APREL","MAY","IYUN","IYUL","AVGUST","SENTYABR","OKTYABR","NOYABR","DEKABR"};
  static const char* uk[] = {"SICHEN","LYUTYI","BEREZEN","KVITEN","TRAVEN","CHERVEN","LYPEN","SERPEN","VERESEN","ZHOVTEN","LYSTOPAD","HRUDEN"};
  static const char* nl[] = {"JANUARI","FEBRUARI","MAART","APRIL","MEI","JUNI","JULI","AUGUSTUS","SEPTEMBER","OKTOBER","NOVEMBER","DECEMBER"};
  static const char* pt[] = {"JANEIRO","FEVEREIRO","MARCO","ABRIL","MAIO","JUNHO","JULHO","AGOSTO","SETEMBRO","OUTUBRO","NOVEMBRO","DEZEMBRO"};
  month = constrain(month, 1, 12) - 1;
  const String language = classicLanguage();
  if (language == "de") return de[month]; if (language == "fr") return fr[month];
  if (language == "es") return es[month]; if (language == "it") return it[month];
  if (language == "pl") return pl[month]; if (language == "cs") return cs[month];
  if (language == "ru") return ru[month]; if (language == "uk") return uk[month];
  if (language == "nl") return nl[month]; if (language == "pt") return pt[month];
  return en[month];
}

// ─── Icons ────────────────────────────────────────────────────────────────

void iconMicWhite(int cx, int cy) {
  fillRect(cx-13, cy-36, 26, 44, WHITE);
  fillCircle(cx, cy-36, 13, WHITE);
  fillCircle(cx, cy+8,  13, WHITE);
  strokeCircle(cx, cy-4, 40, 5, WHITE);
  fillRect(cx-50, cy-50, 100, 50, BLACK);
  fillRect(cx-3,  cy+38, 6,  18, WHITE);
  fillRect(cx-24, cy+54, 48,  5, WHITE);
}

void iconRecordBig(int cx, int cy) {
  fillCircle(cx, cy, 36, WHITE);
  strokeCircle(cx, cy, 52, 5, WHITE);
  strokeCircle(cx, cy, 68, 2, WHITE);
}

void iconCheck(int cx, int cy, bool filled) {
  if (filled) {
    fillCircle(cx, cy, 44, BLACK);
    for (int t=-3;t<=3;t++) {
      line(cx-22, cy-2+t, cx-6, cy+17+t, WHITE);
      line(cx-6,  cy+17+t, cx+30, cy-22+t, WHITE);
    }
  } else {
    strokeCircle(cx, cy, 44, 3, BLACK);
    for (int t=-2;t<=2;t++) {
      line(cx-22, cy-2+t, cx-6, cy+17+t, BLACK);
      line(cx-6,  cy+17+t, cx+30, cy-22+t, BLACK);
    }
  }
}

void iconError(int cx, int cy) {
  strokeCircle(cx, cy, 44, 3, BLACK);
  for (int t=-3;t<=3;t++) {
    line(cx-22, cy-22+t, cx+22, cy+22+t, BLACK);
    line(cx+22, cy-22+t, cx-22, cy+22+t, BLACK);
  }
}

void iconThinking(int cx, int cy) {
  fillCircle(cx-28, cy, 8, BLACK);
  fillCircle(cx,    cy, 8, BLACK);
  fillCircle(cx+28, cy, 8, BLACK);
}

void iconTag(int cx, int cy) {
  const int pts[5][2] = {
    {cx-26, cy+4}, {cx-4, cy-18}, {cx+32, cy-18},
    {cx+32, cy+12}, {cx+4,  cy+36}
  };
  for(int i=0;i<4;i++) thickLine(pts[i][0],pts[i][1],pts[i+1][0],pts[i+1][1],4,BLACK);
  thickLine(pts[4][0],pts[4][1],pts[0][0],pts[0][1],4,BLACK);
  fillCircle(cx-2, cy-4, 5, BLACK);
}

void iconSync(int cx, int cy) {
  strokeCircle(cx, cy, 40, 4, BLACK);
  fillRect(cx+16, cy-46, 20, 20, WHITE);
  thickLine(cx+16, cy-36, cx+36, cy-36, 3, BLACK);
  thickLine(cx+36, cy-36, cx+26, cy-46, 3, BLACK);
  thickLine(cx+36, cy-36, cx+26, cy-26, 3, BLACK);
  fillRect(cx-36, cy+26, 20, 20, WHITE);
  thickLine(cx-36, cy+36, cx-16, cy+36, 3, BLACK);
  thickLine(cx-16, cy+36, cx-26, cy+26, 3, BLACK);
  thickLine(cx-16, cy+36, cx-26, cy+46, 3, BLACK);
}

void iconWifi(int cx, int cy) {
  int base = cy + 26;
  strokeCircle(cx, base, 50, 5, BLACK);
  strokeCircle(cx, base, 32, 5, BLACK);
  strokeCircle(cx, base, 14, 5, BLACK);
  fillRect(0, base, W, H - base, WHITE);
  fillCircle(cx, base, 5, BLACK);
}

void iconNoteLines(int cx, int cy) {
  fillRect(cx-32, cy-12, 64, 6, BLACK);
  fillRect(cx-32, cy+2,  64, 6, BLACK);
  fillRect(cx-32, cy+16, 44, 6, BLACK);
}

// ─── Layout helpers ────────────────────────────────────────────────────────

void drawHeader(const char* title, const char* rightInfo) {
  fillRect(0, 0, W, 28, BLACK);
  drawStrC(W/2, 10, title, 1, WHITE);
  if (rightInfo) {
    int rw = textW(rightInfo, 1);
    drawStr(W - 8 - rw, 10, rightInfo, 1, WHITE);
  }
}

void drawHints(const char* recLabel, const char* pwrLabel) {
  hline(0, 179, W, BLACK);
  fillRect(0, 180, W, 20, WHITE);
  drawStr(8, 186, recLabel, 1, BLACK);
  int rw = textW(pwrLabel, 1);
  drawStr(W - 8 - rw, 186, pwrLabel, 1, BLACK);
}

void drawBadge(int cx, int cy, const char* text, bool filled) {
  char up[32]; uppercaseCopy(up, text, sizeof(up));
  int tw = textW(up, 1);
  int bw = tw + 20, bh = 20;
  int bx = cx - bw/2, by = cy - bh/2;
  if (filled) {
    fillRoundRect(bx, by, bw, bh, 9, BLACK);
    drawStrC(cx, by + 6, up, 1, WHITE);
  } else {
    strokeRoundRect(bx, by, bw, bh, 9, 2, BLACK);
    drawStrC(cx, by + 6, up, 1, BLACK);
  }
}

void drawPageDots(int cur, int total) {
  if (total <= 1) return;
  int n = min(total, 7);
  int gap = 16;
  int startX = W/2 - ((n-1)*gap)/2;
  for (int i = 0; i < n; i++) {
    int x = startX + i*gap, y = 168;
    if (i == cur % n) fillCircle(x, y, 5, BLACK);
    else              strokeCircle(x, y, 4, 1, BLACK);
  }
}

void drawChevronRight(int x, int cy, uint8_t c) {
  thickLine(x,   cy-8, x+8, cy,   2, c);
  thickLine(x+8, cy,   x,   cy+8, 2, c);
}

void drawTinyHint(const char* left, const char* right) {
  (void)left; (void)right;
}

void drawKicker(const char* txt, int y) {
  char up[40]; uppercaseCopy(up, txt, sizeof(up));
  drawStrC(W/2, y, up, 1, BLACK);
}

void drawSoftFrame() {
  strokeRoundRect(12, 12, W-24, H-24, 10, 1, BLACK);
}

void drawModernPill(int x, int y, int w, int h, const char* label, bool active) {
  if (active) {
    fillRoundRect(x, y, w, h, h/2, BLACK);
    drawStrInBox(x, y, w, h, label, 1, WHITE);
  } else {
    strokeRoundRect(x, y, w, h, h/2, 1, BLACK);
    drawStrInBox(x, y, w, h, label, 1, BLACK);
  }
}

void drawDotSelector(int cur, int total, int y) {
  int gap = 17, startX = W/2 - ((total-1)*gap)/2;
  for (int i=0; i<total; i++) {
    int x = startX + i*gap;
    if (i == cur) fillCircle(x, y, 4, BLACK);
    else          strokeCircle(x, y, 4, 1, BLACK);
  }
}

void drawCheckSmall(int cx, int cy, uint8_t color) {
  strokeCircle(cx, cy, 13, 1, color);
  thickLine(cx-6, cy, cx-1, cy+5, 2, color);
  thickLine(cx-1, cy+5, cx+8, cy-6, 2, color);
}

void drawMinimalDocIcon(int cx, int cy, uint8_t color) {
  strokeRoundRect(cx-13, cy-16, 26, 32, 3, 2, color);
  hline(cx-7, cy-5, 14, color);
  hline(cx-7, cy+4, 14, color);
  hline(cx-7, cy+13, 9, color);
}

void drawMinimalTagIcon(int cx, int cy, uint8_t color) {
  thickLine(cx-13, cy, cx-2, cy-13, 2, color);
  thickLine(cx-2, cy-13, cx+14, cy-13, 2, color);
  thickLine(cx+14, cy-13, cx+14, cy+2, 2, color);
  thickLine(cx+14, cy+2, cx+2, cy+15, 2, color);
  thickLine(cx+2, cy+15, cx-13, cy, 2, color);
  fillCircle(cx+4, cy-5, 3, color);
}

void drawMinimalCloudIcon(int cx, int cy, uint8_t color) {
  strokeCircle(cx-8, cy+2, 10, 2, color);
  strokeCircle(cx+4, cy-4, 13, 2, color);
  strokeCircle(cx+15, cy+4, 9, 2, color);
  fillRect(cx-22, cy+4, 47, 16, WHITE);
  hline(cx-21, cy+10, 44, color);
}

void drawMenuTile(int x, int y, int w, int h, const char* label, int icon, bool active) {
  if (active) fillRoundRect(x, y, w, h, 12, BLACK);
  else        strokeRoundRect(x, y, w, h, 12, 1, BLACK);
  uint8_t col = active ? WHITE : BLACK;
  int cx = x + w/2;
  fillCircle(cx, y + 17, 4, col);
  drawStrInBox(x + 4, y + 29, w - 8, 18, label, 1, col);
}

void drawNoteCard(int y, int idx, bool active) {
  const int x = 16, w = 168, h = 39;
  if (active) fillRoundRect(x, y, w, h, 8, BLACK);
  else        strokeRoundRect(x, y, w, h, 8, 1, BLACK);
  uint8_t col = active ? WHITE : BLACK;

  char n[8]; snprintf(n, sizeof(n), "#%03d", noteIndex[idx].num);
  String tagLabel = normalizeForDisplay(String(noteIndex[idx].tag));
  drawStr(x + 10, y + 5, n, 1, col);
  drawStrFit(x + 66, y + 5, 88, tagLabel.c_str(), 1, col);
  String ticker = noteTickerText(idx);
  drawTickerText(x + 10, y + 22, 145, ticker, active, col);
}

void drawListMenuCard(int y, const char* title, const char* meta, bool active) {
  const int x = 16, w = 168, h = 32;
  if (active) fillRoundRect(x, y, w, h, 8, BLACK);
  else        strokeRoundRect(x, y, w, h, 8, 1, BLACK);
  uint8_t col = active ? WHITE : BLACK;
  drawStrFit(x + 10, y + 8, meta ? 92 : 140, title, 1, col);
  if (meta && strlen(meta) > 0) {
    int mw = min(textW(meta, 1), 56);
    drawStrFit(x + w - 10 - mw, y + 8, 56, meta, 1, col);
  }
}

// ─── Screens ──────────────────────────────────────────────────────────────

void showIdle(bool locked) {
  clearWhite();
  drawStr(14, 10, "REC", 1, BLACK);
  drawCompactBattery(readBatteryPercent());
  if (locked) {
    strokeRoundRect(139, 8, 8, 10, 3, 1, BLACK);
    fillRect(136, 14, 14, 9, BLACK);
    fillRect(142, 17, 2, 3, WHITE);
  }
  hline(14, 31, 172, BLACK);
  drawStrC(100, 62, "Klar til", 3, BLACK);
  drawStrC(100, 104, "at optage", 3, BLACK);
  hline(14, 168, 172, BLACK);
  drawStrC(100, 180, DEVICE_LABEL, 1, BLACK);
  refresh();
}

void showBatteryLow(int pct) {
  fillRect(0, 0, W, H, BLACK);
  fillRect(95, 48, 10, 50, WHITE);
  fillRect(95, 108, 10, 10, WHITE);
  char buf[8]; snprintf(buf, sizeof(buf), "%d%%", pct);
  drawStrC(100, 132, buf,       2, WHITE);
  drawStrC(100, 160, "batteri", 1, WHITE);
  drawStrC(100, 176, "lavt",    1, WHITE);
  refresh();
}

void showRecording() {
  fillRect(0, 0, W, H, BLACK);
  drawStrC(W/2, 20, "optager", 1, WHITE);
  fillCircle(W/2, 82, 27, WHITE);
  drawStrC(W/2, 124, "hold = stop og gem", 1, WHITE);
  drawStrC(W/2, 145, "maks. 2 timer", 1, WHITE);
  refresh();
}

void showSaved(int num) {
  clearWhite();
  drawCheckSmall(100, 46, BLACK);
  drawStrC(100, 76, "gemt", 1, BLACK);
  char b[8]; snprintf(b, sizeof(b), "#%03d", num);
  drawStrC(100, 105, b, 2, BLACK);
  refresh();
}

void showTagSelect(int cursor) {
  clearWhite();
  if (tagCount <= 0) {
    drawKicker("ingen tags", 34);
    drawStrC(100, 100, "brug portalen", 1, BLACK);
    refresh();
    return;
  }
  drawKicker("hvilket tag?", 17);
  const int x = 36, w = 128, h = 21, gap = 7;
  int y0 = 40;
  cursor = constrain(cursor, 0, max(tagCount - 1, 0));
  const int visible = 5;
  const int first = (cursor / visible) * visible;
  const int last = min(tagCount, first + visible);
  for (int i = first; i < last; i++) {
    int y = y0 + (i - first) * (h + gap);
    drawModernPill(x, y, w, h, normalizeForDisplay(String(tags[i])).c_str(), i == cursor);
  }
  refresh();
}

void showMenu(int cursor) {
  clearWhite();
  drawStr(16, 14, "menu", 1, BLACK);
  hline(16, 32, W-32, BLACK);
  const int y0 = 42, step = 36;
  for (int row = 0; row < MENU_COUNT; row++) {
    bool active = row == cursor;
    int y = y0 + row * step;
    if (active) fillRoundRect(16, y, 168, 31, 8, BLACK);
    else        strokeRoundRect(16, y, 168, 31, 8, 1, BLACK);
    uint8_t col = active ? WHITE : BLACK;
    drawStrInBox(16, y, 168, 31, MENU_ITEMS[row], 1, col);
  }
  refresh();
}

void showTagBrowser(int cursor) {
  clearWhite();
  if (tagCount <= 0) {
    drawKicker("tags", 16);
    drawStrC(100, 100, "ingen tags", 1, BLACK);
    refresh();
    return;
  }
  drawKicker("tags", 16);
  fillRoundRect(28, 56, 144, 54, 17, BLACK);
  cursor = constrain(cursor, 0, max(tagCount - 1, 0));
  drawStrInBox(28, 56, 144, 54, normalizeForDisplay(String(tags[cursor])).c_str(), 2, WHITE);
  int cnt = 0;
  for (int i=0; i<(int)noteIndex.size(); i++)
    if (strcmp(noteIndex[i].tag, tags[cursor])==0) cnt++;
  char cb[20]; snprintf(cb, sizeof(cb), "%d noter", cnt);
  drawStrC(100, 130, cb, 1, BLACK);
  refresh();
}

void showNoteList(int cursor) {
  if (tickerCursor != cursor) {
    tickerCursor = cursor;
    tickerOffset = 0;
    tickerLastMs = millis();
  }
  clearWhite();
  int count = filteredCount();
  char cb[16]; snprintf(cb, sizeof(cb), "%d noter", count);
  drawStr(16, 14, "noter", 1, BLACK);
  int cw = textW(cb, 1);
  drawStr(W-16-cw, 14, cb, 1, BLACK);
  if (count <= 0) {
    drawMinimalDocIcon(100, 76, BLACK);
    drawStrC(100, 116, "ingen noter endnu", 1, BLACK);
    refresh();
    return;
  }
  const int pageSize = 3;
  int pageStart = (cursor / pageSize) * pageSize;
  int activeRow = cursor - pageStart;
  const int y0 = 43, step = 47;
  int shown = min(pageSize, count - pageStart);
  for (int row=0; row<shown; row++) {
    int vis = pageStart + row;
    int idx = noteAtFilteredIndex(vis);
    if (idx >= 0) drawNoteCard(y0 + row*step, idx, row == activeRow);
  }
  refresh();
}

void showNoteDetail(int cursor) {
  clearWhite();
  int idx = noteAtFilteredIndex(cursor);
  if (idx < 0) {
    drawStrC(100, 96, "ikke fundet", 1, BLACK);
    refresh();
    return;
  }
  char n[8]; snprintf(n, sizeof(n), "#%03d", noteIndex[idx].num);
  drawStr(16, 14, n, 1, BLACK);
  String tagLabel = normalizeForDisplay(String(noteIndex[idx].tag));
  int tw = textW(tagLabel.c_str(), 1);
  drawStrFit(W-16-min(tw, 82), 14, 82, tagLabel.c_str(), 1, BLACK);
  hline(16, 32, W-32, BLACK);

  // Transcripts are read in the portal; the device shows only note facts.
  detailTotalLines = 0;
  const int num = noteIndex[idx].num;
  drawStrC(100, 46, noteCreatedDeviceLabel(num).c_str(), 1, BLACK);

  const uint32_t seconds = recordingDurationSeconds(num);
  char length[12];
  if (seconds >= 3600) snprintf(length, sizeof(length), "%lu:%02lu:%02lu", (unsigned long)(seconds / 3600),
                                (unsigned long)((seconds / 60) % 60), (unsigned long)(seconds % 60));
  else snprintf(length, sizeof(length), "%lu:%02lu", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
  drawStrC(100, 76, length, 3, BLACK);

  drawStrC(100, 124, noteIndex[idx].hasText ? "transskriberet" : "ikke syncet", 1, BLACK);
  hline(16, 168, W-32, BLACK);
  drawStrC(100, 178, "tryk = afspil", 1, BLACK);
  refresh();
}

void showDeleteConfirm(int noteNum) {
  clearWhite();
  fillRect(0, 0, W, 28, BLACK);
  drawStrC(W/2, 10, "SLET", 1, WHITE);
  char label[16]; snprintf(label, sizeof(label), "#%03d", noteNum);
  drawStrC(W/2, 52, label, 2, BLACK);
  drawStrC(W/2, 88, "Slet denne note?", 1, BLACK);
  drawStrC(W/2, 108, "lyd + tekst + data", 1, BLACK);
  hline(0, 179, W, BLACK);
  fillRect(0, 180, W, 20, WHITE);
  drawStr(8, 186, "slet", 1, BLACK);
  int rw = textW("fortryd", 1);
  drawStr(W - 8 - rw, 186, "fortryd", 1, BLACK);
  refresh();
}

void showTranscribing(int done, int total) {
  clearWhite();
  drawKicker("sync", 20);
  iconThinking(100, 76);
  int barW = 144, barH = 10, barX = 28, barY = 116;
  strokeRoundRect(barX, barY, barW, barH, 5, 1, BLACK);
  if (total > 0) {
    int fill = (done * (barW - 4)) / max(total, 1);
    if (fill > 0) fillRoundRect(barX+2, barY+2, fill, barH-4, 3, BLACK);
    char b[20]; snprintf(b, sizeof(b), "%d / %d", done, total);
    drawStrC(100, 142, b, 1, BLACK);
  } else {
    drawStrC(100, 142, "vent lidt", 1, BLACK);
  }
  refresh();
}

void showWifiConnecting(int attempt, int maxA) {
  clearWhite();
  drawKicker("wifi", 20);
  iconWifi(100, 84);
  int barW = 130, barH = 10, barX = 35, barY = 140;
  strokeRoundRect(barX, barY, barW, barH, 5, 1, BLACK);
  int fill = (attempt * (barW - 4)) / max(maxA, 1);
  if (fill > 0) fillRoundRect(barX+2, barY+2, fill, barH-4, 3, BLACK);
  char b[20]; snprintf(b, sizeof(b), "%d / %d", attempt, maxA);
  drawStrC(100, 164, b, 1, BLACK);
  refresh();
}

void showDone() {
  clearWhite();
  drawCheckSmall(100, 70, BLACK);
  drawStrC(100, 105, "alt er syncet", 1, BLACK);
  refresh();
}

void showSyncPending() {
  const String language = classicLanguage();
  const char* label = "GEMT TIL SENERE";
  if (language == "de") label = "NOCH OFFEN";
  else if (language == "fr") label = "EN ATTENTE";
  else if (language == "es") label = "PENDIENTE";
  else if (language == "it") label = "IN ATTESA";
  else if (language == "pl") label = "W KOLEJCE";
  else if (language == "cs") label = "CEKA NA POZDEJI";
  else if (language == "ru") label = "OTLOZHENO";
  else if (language == "uk") label = "VIDKLADENO";
  else if (language == "nl") label = "VOOR LATER";
  else if (language == "pt") label = "PENDENTE";
  clearWhite();
  iconThinking(100, 70);
  drawStrC(100, 118, label, 1, BLACK);
  refresh();
}

void showError(const char* msg) {
  clearWhite();
  iconError(100, 70);
  if (msg && strlen(msg) > 0) drawStrC(100, 118, msg, 1, BLACK);
  else drawStrC(100, 118, "fejl", 1, BLACK);
  refresh();
}

void showPlaybackOverlay(int part, int parts) {
  // Replaces the "tryk = afspil" footer of the note detail screen.
  fillRect(16, 169, W-32, H-169, WHITE);
  char label[20];
  if (parts > 1) snprintf(label, sizeof(label), "afspiller %d/%d", part, parts);
  else snprintf(label, sizeof(label), "afspiller");
  const int x = 100 - (textW(label, 1) + 14) / 2;
  fillTriangle(x, 179, x, 191, x + 9, 185, BLACK);
  drawStr(x + 14, 178, label, 1, BLACK);
  refresh();
}

void showTransferConnecting() {
  clearWhite();
  drawKicker("portal", 18);
  iconWifi(100, 82);
  drawStrC(100, 138, "forbinder", 1, BLACK);
  refresh();
}

void showTransferMode(const char* host, const char* ip) {
  clearWhite();
  drawKicker("portal", 16);
  fillRoundRect(26, 48, 148, 58, 16, BLACK);
  drawStrInBox(26, 48, 148, 24, "portal", 1, WHITE);
  drawStrInBox(26, 74, 148, 24, "aktiv", 1, WHITE);
  drawStrC(100, 119, "i browseren:", 1, BLACK);
  drawStrC(100, 139, host, 1, BLACK);
  drawStrC(100, 156, ip, 1, BLACK);
  drawStrC(100, 177, "hold rec = afslut", 1, BLACK);
  refresh();
}

void showSettings(int cursor) {
  clearWhite();
  drawStr(16, 14, "indstillinger", 1, BLACK);
  hline(16, 32, W-32, BLACK);
  const int y0 = 42, step = 39;
  for (int row = 0; row < SETTINGS_COUNT; row++) {
    bool active = row == cursor;
    int y = y0 + row * step;
    if (active) fillRoundRect(16, y, 168, 32, 8, BLACK);
    else        strokeRoundRect(16, y, 168, 32, 8, 1, BLACK);
    uint8_t col = active ? WHITE : BLACK;
    if (row == 0) {
      drawStr(28, y + 8, "lyde", 1, col);
      drawStr(W - 70, y + 8, soundIsEnabled() ? "til" : "fra", 1, col);
    } else if (row == 1) {
      drawStr(28, y + 8, "portal", 1, col);
    } else {
      drawStr(28, y + 8, "enhed", 1, col);
    }
  }
  refresh();
}

void showDeviceInfo() {
  clearWhite();
  drawStr(16, 14, "enhed", 1, BLACK);
  hline(16, 32, W-32, BLACK);
  drawStr(18, 50, "firmware", 1, BLACK);
  drawStrFit(18, 68, 160, FIRMWARE_VERSION, 1, BLACK);
  drawStr(18, 94, "board", 1, BLACK);
  drawStrFit(18, 112, 160, "ESP32-C6 ePaper 1.54", 1, BLACK);
  char b[24]; snprintf(b, sizeof(b), "%d noter", (int)noteIndex.size());
  drawStr(18, 138, b, 1, BLACK);
  drawStr(18, 160, soundIsEnabled() ? "lyde til" : "lyde fra", 1, BLACK);
  drawStr(18, 178, rtcUtcIso().length() ? "ur indstillet" : "ur ikke indstillet", 1, BLACK);
  refresh();
}
