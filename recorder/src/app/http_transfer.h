#pragma once
#include "http_response.h"
#include <stdint.h>

// A connected socket can remain silent forever. Bound the complete transfer,
// not just individual reads, and require valid HTTP framing before success.
template <typename Client, typename Clock, typename Yield>
bool receiveFramedHttp(Client& client, HttpResponseBuffer& response, Clock now,
                       Yield yield, uint32_t started, uint32_t deadlineMs,
                       uint32_t idleMs) {
  uint32_t lastByte = now();
  while (!response.done() && !response.failed() &&
         uint32_t(now() - started) < deadlineMs &&
         uint32_t(now() - lastByte) < idleMs) {
    int budget = 2048;
    while (budget-- > 0 && client.available() && !response.done() && !response.failed()) {
      const int value = client.read();
      if (value < 0) break;
      response.feed(static_cast<char>(value));
      lastByte = now();
    }
    if (!response.done() && !client.available() && !client.connected()) response.eof();
    yield();
  }
  return response.done() && !response.failed();
}
