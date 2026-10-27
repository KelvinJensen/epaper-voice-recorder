#pragma once
#include <cstdint>

inline bool wavChunkFits(uint32_t offset, uint32_t size, uint32_t fileSize) {
  return offset <= fileSize && size <= fileSize - offset;
}
inline bool wavFramesValid(uint32_t bytes, uint16_t channels) {
  return (channels == 1 || channels == 2) && bytes > 0 && bytes % (channels * 2U) == 0;
}
