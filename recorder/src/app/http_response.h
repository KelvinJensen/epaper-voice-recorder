#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <algorithm>
#include <cctype>
#include <functional>

// Incremental HTTP/1 response framing; no socket calls or timing assumptions.
class HttpResponseBuffer {
 public:
  explicit HttpResponseBuffer(size_t limit = 65536) : limit_(limit) {}
  std::function<bool(char)> sink;
  size_t received = 0;
  int status = 0;
  std::string body;
  const char* error = nullptr;
  bool done() const { return state_ == Done; }
  bool failed() const { return error != nullptr; }

  void eof() {
    if (state_ == CloseBody) state_ = Done;
    else if (!done()) fail("incomplete response");
  }

  void feed(char c) {
    if (done() || failed()) return;
    if (state_ == FixedBody || state_ == CloseBody || state_ == ChunkBody) {
      if (received >= limit_) { fail("response too large"); return; }
      if (sink) { if (!sink(c)) { fail("body write failed"); return; } }
      else body += c;
      ++received;
      if (state_ == FixedBody && --remaining_ == 0) state_ = Done;
      else if (state_ == ChunkBody && --remaining_ == 0) state_ = ChunkCR;
      return;
    }
    if (state_ == ChunkCR) { if (c != '\r') fail("invalid chunk ending"); else state_ = ChunkLF; return; }
    if (state_ == ChunkLF) { if (c != '\n') fail("invalid chunk ending"); else state_ = ChunkSize; return; }
    // Chunk framing grows with audio length; bound each chunk-size line,
    // not the cumulative framing of an otherwise valid large response.
    if ((state_ != ChunkSize && ++metadataBytes_ > 32768) || line_.size() >= 2048) { fail("headers too large"); return; }
    if (c != '\n') { line_ += c; return; }
    if (line_.empty() || line_.back() != '\r') { fail("invalid line ending"); return; }
    line_.pop_back();
    parseLine();
    line_.clear();
  }

 private:
  enum State { Status, Headers, FixedBody, CloseBody, ChunkSize, ChunkBody, ChunkCR, ChunkLF, Trailers, Done };
  State state_ = Status;
  size_t limit_, remaining_ = 0, metadataBytes_ = 0;
  bool hasLength_ = false, chunked_ = false;
  std::string line_;
  void fail(const char* reason) { error = reason; }
  static std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    return s.substr(first, s.find_last_not_of(" \t") - first + 1);
  }
  static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
  }
  static bool number(const std::string& s, unsigned base, size_t cap, size_t& value) {
    if (s.empty()) return false;
    value = 0;
    for (unsigned char c : s) {
      unsigned digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
      if (digit >= base || digit > cap || value > (cap - digit) / base) return false;
      value = value * base + digit;
    }
    return true;
  }
  void parseLine() {
    if (state_ == Status) {
      if (line_.size() < 12 || (line_.compare(0, 9, "HTTP/1.1 ") && line_.compare(0, 9, "HTTP/1.0 "))) { fail("invalid status"); return; }
      size_t code;
      if (!number(line_.substr(9, 3), 10, 599, code) || code < 100 || (line_.size() > 12 && line_[12] != ' ')) { fail("invalid status"); return; }
      status = static_cast<int>(code); state_ = Headers; return;
    }
    if (state_ == ChunkSize) {
      if (!number(trim(line_.substr(0, line_.find(';'))), 16, limit_ - received, remaining_)) { fail("invalid or oversized chunk"); return; }
      state_ = remaining_ ? ChunkBody : Trailers; return;
    }
    if (state_ == Trailers) { if (line_.empty()) state_ = Done; return; }
    if (line_.empty()) {
      if (status >= 100 && status < 200) {
        if (status == 101) { fail("unsupported upgrade"); return; }
        hasLength_ = chunked_ = false; remaining_ = 0; state_ = Status; return;
      }
      if (chunked_ && hasLength_) { fail("ambiguous framing"); return; }
      if (status == 204 || status == 304) state_ = Done;
      else if (chunked_) state_ = ChunkSize;
      else if (hasLength_) state_ = remaining_ ? FixedBody : Done;
      else state_ = CloseBody;
      return;
    }
    const auto colon = line_.find(':');
    if (colon == std::string::npos || colon == 0) { fail("invalid header"); return; }
    const auto name = lower(line_.substr(0, colon)), value = trim(line_.substr(colon + 1));
    if (name == "content-length") {
      size_t length;
      if (!number(value, 10, limit_, length) || (hasLength_ && remaining_ != length)) { fail("invalid content length"); return; }
      hasLength_ = true; remaining_ = length;
    } else if (name == "transfer-encoding") {
      if (chunked_ || lower(value) != "chunked") { fail("unsupported transfer encoding"); return; }
      chunked_ = true;
    } else if (name == "content-encoding" && lower(value) != "identity") {
      fail("unsupported content encoding");
    }
  }
};
