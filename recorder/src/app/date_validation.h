#pragma once
#include <cstring>
#include <cstdint>

inline bool validCalendarDate(int year, int month, int day) {
  if (year < 2020 || year > 2100 || month < 1 || month > 12 || day < 1) return false;
  const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  return day <= days[month - 1] + (month == 2 && leap ? 1 : 0);
}

inline bool validBcdByte(uint8_t value) {
  return (value & 0xf) <= 9 && (value >> 4) <= 9;
}

inline bool validUtcTimestamp(const char* value) {
  if (!value || std::strlen(value) != 20) return false;
  for (int i = 0; i < 20; ++i) {
    const char expected = i == 4 || i == 7 ? '-' : i == 10 ? 'T'
      : i == 13 || i == 16 ? ':' : i == 19 ? 'Z' : 0;
    if (expected ? value[i] != expected : value[i] < '0' || value[i] > '9') return false;
  }
  auto number = [value](int start, int length) {
    int result = 0;
    for (int i = 0; i < length; ++i) result = result * 10 + value[start + i] - '0';
    return result;
  };
  const int year = number(0, 4), month = number(5, 2), day = number(8, 2);
  if (!validCalendarDate(year, month, day) ||
      number(11, 2) > 23 || number(14, 2) > 59 || number(17, 2) > 59) return false;
  return true;
}
