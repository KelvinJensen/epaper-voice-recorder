#pragma once
#include <Arduino.h>

// Drops a multi-byte character (æ, ø, å …) left incomplete by a byte cut.
inline void trimIncompleteUtf8(String& text) {
  int end = text.length();
  int lead = end - 1;
  while (lead >= 0 && ((uint8_t)text[lead] & 0xC0) == 0x80) lead--;
  if (lead < 0) return;
  const uint8_t c = (uint8_t)text[lead];
  const int needed = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
  if (end - lead < needed) text.remove(lead);
}
