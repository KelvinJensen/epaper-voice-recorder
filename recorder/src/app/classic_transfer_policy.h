#pragma once
#include <cstdint>
#include <cstddef>

inline bool classicTransientStatus(int status) {
  return status <= 0 || status == 408 || status == 425 || status == 429 ||
         (status >= 500 && status <= 599);
}

template <typename Client, typename Clock, typename Yield>
bool classicWriteAll(Client& client, const uint8_t* bytes, size_t length,
                     Clock now, Yield yield, uint32_t started, uint32_t limitMs) {
  size_t offset = 0;
  uint32_t progress = now();
  while (offset < length) {
    if (!client.connected() || uint32_t(now() - started) >= limitMs ||
        uint32_t(now() - progress) >= 15000) return false;
    const size_t sent = client.write(bytes + offset, length - offset);
    if (sent > length - offset) return false;
    if (sent) { offset += sent; progress = now(); }
    else yield();
  }
  return true;
}
