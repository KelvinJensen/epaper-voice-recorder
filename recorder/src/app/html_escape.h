#pragma once
#include <string>

inline std::string escapeViewerHtml(const char* value) {
  std::string out;
  if (!value) return out;
  for (; *value; ++value) {
    switch (*value) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += *value;
    }
  }
  return out;
}
