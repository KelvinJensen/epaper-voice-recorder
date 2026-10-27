#pragma once
#include <string>

inline bool viewerSameOrigin(const std::string& value, const std::string& host) {
  if (host.empty()) return false;
  const std::string base = "http://" + host;
  return value == base || (value.size() > base.size() &&
    value.compare(0, base.size(), base) == 0 && value[base.size()] == '/');
}
inline bool viewerWriteAllowed(const std::string& host, const std::string& origin,
                                const std::string& referer, const std::string& fetchSite) {
  if (fetchSite == "cross-site") return false;
  if (!origin.empty()) return viewerSameOrigin(origin, host);
  if (!referer.empty()) return viewerSameOrigin(referer, host);
  // Local API clients don't send browser headers. The Viewer remains a local,
  // trusted-network tool, not an authenticated public service.
  return true;
}
