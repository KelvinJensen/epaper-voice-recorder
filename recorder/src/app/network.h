#pragma once
#include <Arduino.h>

bool transcribe(const String& wavPath, int noteNum);
bool transcribeAll();

String htmlEscape(const String& s);
String readSmallFile(const char* path, size_t maxLen = 1600);
String portalCss();

void handlePortalRoot();
void handlePortalJson();
void handleExportTxt();
void sendFileByNum(const char* ext, const char* mime, bool attachment);
void handleTagsPage();
void handleTagAdd();
void handleTagDelete();
void handleNoteDelete();
void handleNoteTag();

void setupTransferServer();
void stopTransferMode();
