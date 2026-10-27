#pragma once
#include <Arduino.h>

void   loadIndex();
bool   saveIndex();
bool   addToIndex(int num, const char* tag, bool hasText);
bool   updateIndexHasText(int num);
void   deleteNote(int num);
int    nextNoteNumber();
bool   saveTag(int num, const char* tag);
void   recoverOrphanNotes();

void   loadTags();
bool   saveTagsToFile();
void   createDefaultTags();
bool   addCustomTag(const char* newTag);
bool   deleteTag(const char* tagName);
bool   tagHasNotes(const char* tag);
void   replaceTagOnNotes(const char* oldTag, const char* newTag);

String noteMetaPath(int num);
String readNoteMetaValue(int num, const char* key);
bool   writeNoteMeta(int num, const char* tag);

String noteCreatedUtc(int num);
String utcToLocalDeviceLabel(const String& utcIso);
String noteCreatedDeviceLabel(int num);
String currentUtcIso();

String notePreviewText(int num, size_t maxLen = 90);
String noteTickerText(int idx);
bool   activeTickerNeedsScroll(int cursor);
void   drawTickerText(int x, int y, int maxW, const String& rawText, bool active, uint8_t color);

int  filteredCount();
int  noteAtFilteredIndex(int visIdx);
